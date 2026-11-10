package main

import (
	"errors"
	"fmt"
	"io"
	"io/fs"
	"math"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"slices"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"
)

type kernelRow struct {
	Running        bool
	Secs           float64
	Objects, Total int
	Built          string
}

type job struct {
	Arch, Pkg string
	Secs      float64
}

type imageRow struct {
	Name, State, When string
}

type snapshot struct {
	Start            float64
	Failed           bool
	Done, Total, Run [4]int
	Todo             [4][]string
	X32Image         [2]int
	Kernels          [3]kernelRow
	Jobs             []job
	CPU, Mem, Load   []float64
	Info             string
	Images           []imageRow
	Steps            []string
	Logs             []string
	LogName          string
	Progress         float64
}

type sampler struct {
	lastIdle, lastTotal uint64
	cpu, mem, load      []float64
	kmax, klast         [3]int
	kstart              [3]float64
	src                 map[string]float64
	srcAt               time.Time
	ccache              string
	ccacheAt            time.Time
	btime               float64
	session, active     float64
	imgMu               sync.Mutex
	imgState            map[string]string
	imgBusy             atomic.Bool
	imgAt               time.Time
}

func (s *sampler) checkImages() {
	defer s.imgBusy.Store(false)
	state := map[string]string{}
	args := []string{"-n", "19", "make", "-q"}
	for _, pat := range []string{"*.lock", "*.toml", "*.mk", "*.txt"} {
		matches, _ := filepath.Glob(filepath.Join(bdir, pat))
		for _, m := range matches {
			args = append(args, "-o", rel(m))
		}
	}
	for _, img := range imageNames {
		if _, ok := mtime(filepath.Join(top, img)); !ok {
			state[img] = "missing"
			continue
		}
		cmd := exec.Command("nice", append(args, img)...)
		cmd.Dir = top
		for _, e := range os.Environ() {
			if !strings.HasPrefix(e, "MAKEFLAGS=") && !strings.HasPrefix(e, "MFLAGS=") && !strings.HasPrefix(e, "MAKELEVEL=") {
				cmd.Env = append(cmd.Env, e)
			}
		}
		err := cmd.Run()
		var exit *exec.ExitError
		switch {
		case err == nil:
			state[img] = "up to date"
		case errors.As(err, &exit) && exit.ExitCode() == 1:
			state[img] = "outdated"
		default:
			state[img] = "unknown"
		}
	}
	s.imgMu.Lock()
	s.imgState = state
	s.imgMu.Unlock()
}

func fdTarget(pid string, fd int) string {
	t, err := os.Readlink(fmt.Sprintf("/proc/%s/fd/%d", pid, fd))
	if err != nil || !strings.HasPrefix(t, "/") {
		return ""
	}
	if fi, err := os.Stat(t); err != nil || !fi.Mode().IsRegular() {
		return ""
	}
	return t
}

func tail(path string, n int) []string {
	f, err := os.Open(path)
	if err != nil {
		return nil
	}
	defer f.Close()
	if fi, err := f.Stat(); err == nil && fi.Size() > 65536 {
		f.Seek(fi.Size()-65536, io.SeekStart)
	}
	b, _ := io.ReadAll(f)
	lines := strings.Split(strings.TrimRight(string(b), "\n"), "\n")
	if len(lines) > n {
		lines = lines[len(lines)-n:]
	}
	return lines
}

func rel(p string) string {
	if r, err := filepath.Rel(top, p); err == nil && !strings.HasPrefix(r, "..") {
		return r
	}
	return p
}

func stepFor(a []string) string {
	arg := func(i int) string {
		if i < len(a) {
			return a[i]
		}
		return ""
	}
	base := filepath.Base(a[0])
	script := ""
	if (base == "sh" || base == "bash" || base == "python3" || base == "python") && len(a) > 1 {
		script = filepath.Base(a[1])
		a = a[1:]
	} else if strings.HasPrefix(a[0], "scripts/") {
		script = base
	}
	switch {
	case script == "mkkernel.sh":
		return "compiling the " + arg(2) + " kernel"
	case script == "mkrootfs.sh":
		return "assembling " + rel(arg(2)) + " (" + arg(1) + " stage)"
	case script == "mkinitramfs.sh":
		return "making the initramfs " + rel(arg(1))
	case script == "mkimg.sh" || script == "mkimg-aarch64.sh" || script == "mkimg-rpi.sh":
		return "writing the disk image " + rel(arg(1))
	case script == "mkiso.sh":
		return "making an ISO from " + rel(arg(1))
	case script == "mklicenses.py":
		return "collecting license files"
	case script == "elfshrink.py":
		return "shrinking programs with UPX and sstrip"
	case script == "fwprune.py":
		return "pruning firmware"
	case script == "pkg.py" && arg(1) == "extract":
		return "unpacking packages into " + rel(arg(4))
	case script == "pkg.py" && arg(1) == "fetch":
		return "downloading packages"
	case base == "mksquashfs":
		return "squashing " + rel(arg(1)) + " into " + rel(arg(2))
	case base == "upx":
		return "packing programs with UPX"
	case base == "VBoxManage" || base == "qemu-img":
		return "converting a disk image"
	case strings.HasPrefix(base, "qemu-system-"):
		return "QEMU test running (" + strings.TrimPrefix(base, "qemu-system-") + ")"
	}
	return ""
}

func push(s []float64, v float64) []float64 {
	s = append(s, v)
	if len(s) > 300 {
		s = s[len(s)-300:]
	}
	return s
}

func (s *sampler) procStart(pid string) float64 {
	st := readFile("/proc/" + pid + "/stat")
	i := strings.LastIndexByte(st, ')')
	if i < 0 {
		return now()
	}
	f := strings.Fields(st[i+1:])
	if len(f) < 20 {
		return now()
	}
	ticks, _ := strconv.ParseFloat(f[19], 64)
	return s.btime + ticks/100
}

func newest(dir string, skipTiny bool) float64 {
	t := 0.0
	entries, _ := os.ReadDir(dir)
	for _, e := range entries {
		if strings.HasPrefix(e.Name(), ".") || (skipTiny && e.Name() == "tiny.lua") {
			continue
		}
		if info, err := e.Info(); err == nil {
			t = math.Max(t, float64(info.ModTime().UnixNano())/1e9)
		}
	}
	return t
}

func countObjects(dir string) int {
	n := 0
	filepath.WalkDir(dir, func(p string, d fs.DirEntry, err error) error {
		if err == nil && !d.IsDir() && strings.HasSuffix(p, ".o") {
			n++
		}
		return nil
	})
	return n
}

var ruleRe = regexp.MustCompile(`(?m)^\$\(B\)/pkg/(\w+)/([^:\s]+):(.*)$`)
var wildRe = regexp.MustCompile(`^(?:\$\(filter-out [^,]*,)?\$\(wildcard (packages/[^/]+)/\*\)\)?$`)
var stampRe = regexp.MustCompile(`^\$\(B\)/pkg/(\w+)/(.+)$`)
var tokenRe = regexp.MustCompile(`\$\(filter-out [^,]*,\$\(wildcard [^)]*\)\)|\$\(wildcard [^)]*\)|\S+`)
var listRe = regexp.MustCompile(`(?m)^(ROOTFS_I686_STAMPS|ROOTFS_X32_STAMPS|X32_STAMPS|X86_64_STAMPS|AARCH64_STAMPS) :=((?:.*\\\n)*.*)$`)
var listStampRe = regexp.MustCompile(`\$\(B\)/pkg/(\w+)/([^\s\\]+)`)

func (s *sampler) sample() *snapshot {
	snap := &snapshot{}
	for line := range strings.SplitSeq(readFile("/proc/stat"), "\n") {
		f := strings.Fields(line)
		if len(f) > 4 && f[0] == "cpu" {
			var total uint64
			for _, x := range f[1:] {
				v, _ := strconv.ParseUint(x, 10, 64)
				total += v
			}
			idle4, _ := strconv.ParseUint(f[4], 10, 64)
			idle5, _ := strconv.ParseUint(f[5], 10, 64)
			idle := idle4 + idle5
			if s.lastTotal > 0 && total > s.lastTotal {
				s.cpu = push(s.cpu, 1-float64(idle-s.lastIdle)/float64(total-s.lastTotal))
			}
			s.lastIdle, s.lastTotal = idle, total
		}
		if len(f) == 2 && f[0] == "btime" {
			s.btime, _ = strconv.ParseFloat(f[1], 64)
		}
	}
	var memTotal, memAvail float64
	for line := range strings.SplitSeq(readFile("/proc/meminfo"), "\n") {
		f := strings.Fields(line)
		if len(f) >= 2 {
			v, _ := strconv.ParseFloat(f[1], 64)
			switch f[0] {
			case "MemTotal:":
				memTotal = v
			case "MemAvailable:":
				memAvail = v
			}
		}
	}
	if memTotal > 0 {
		s.mem = push(s.mem, 1-memAvail/memTotal)
	}
	if f := strings.Fields(readFile("/proc/loadavg")); len(f) > 0 {
		v, _ := strconv.ParseFloat(f[0], 64)
		s.load = push(s.load, v)
	}

	type proc struct {
		pid  string
		argv []string
	}
	var procs []proc
	entries, _ := os.ReadDir("/proc")
	for _, e := range entries {
		if e.Name()[0] < '0' || e.Name()[0] > '9' {
			continue
		}
		raw := strings.TrimRight(readFile("/proc/"+e.Name()+"/cmdline"), "\x00")
		if raw != "" {
			procs = append(procs, proc{e.Name(), strings.Split(raw, "\x00")})
		}
	}
	start := 0.0
	running := [3]bool{}
	ksecs := [3]float64{}
	jobs := map[string]job{}
	steps := map[string]float64{}
	var logFiles []string
	jobLog, jobLogAt := "", 0.0
	for _, p := range procs {
		a := p.argv
		if (a[0] == "make" && !slices.Contains(a, "-q") && !slices.Contains(a, "-n")) || (len(a) > 1 && a[0] == "python3" && a[1] == "scripts/failfast.py") {
			if t := s.procStart(p.pid); start == 0 || t < start {
				start = t
			}
		}
		if len(a) > 2 && strings.HasSuffix(a[1], "scripts/makepkg.sh") {
			arch := "i686"
			pkg := "?"
			for i, x := range a {
				if x == "--arch" && i+1 < len(a) {
					arch = a[i+1]
				}
				if strings.HasPrefix(x, "packages/") && pkg == "?" {
					pkg = x[9:]
				}
			}
			secs := now() - s.procStart(p.pid)
			key := arch + "/" + pkg
			if old, ok := jobs[key]; !ok || secs > old.Secs {
				jobs[key] = job{arch, pkg, secs}
			}
		}
		if step := stepFor(a); step != "" {
			secs := now() - s.procStart(p.pid)
			if old, ok := steps[step]; !ok || secs > old {
				steps[step] = secs
			}
		}
		if len(a) > 1 && a[0] == "python3" && a[1] == "scripts/failfast.py" {
			if t := fdTarget(p.pid, 1); t != "" && !slices.Contains(logFiles, t) {
				logFiles = append(logFiles, t)
			}
		}
		if len(a) > 2 && strings.HasSuffix(a[1], "scripts/makepkg.sh") {
			if t := fdTarget(p.pid, 1); t != "" {
				if mt, ok := mtime(t); ok && mt > jobLogAt {
					jobLog, jobLogAt = t, mt
				}
			}
		}
		if len(a) > 3 && strings.HasSuffix(a[1], "scripts/mkkernel.sh") {
			i := map[string]int{"i386": 0, "x86_64": 1, "arm64": 2}[a[3]]
			running[i] = true
			t := s.procStart(p.pid)
			if s.kstart[i] == 0 {
				s.kstart[i] = t
			}
			ksecs[i] = now() - t
		}
	}
	if start > 0 {
		if s.session == 0 || start < s.session {
			s.session = start
		}
		s.active = now()
	} else if now()-s.active > 600 {
		s.session = 0
	}
	start = s.session
	snap.Start = start
	for step, secs := range steps {
		snap.Steps = append(snap.Steps, fmt.Sprintf("%-60s %3d min %02d s", step, int(secs)/60, int(secs)%60))
	}
	slices.Sort(snap.Steps)
	for _, j := range jobs {
		snap.Jobs = append(snap.Jobs, j)
	}
	for i := range snap.Jobs {
		for k := i + 1; k < len(snap.Jobs); k++ {
			if snap.Jobs[k].Secs > snap.Jobs[i].Secs {
				snap.Jobs[i], snap.Jobs[k] = snap.Jobs[k], snap.Jobs[i]
			}
		}
	}
	for i := range arches {
		row := &snap.Kernels[i]
		if running[i] {
			n := countObjects(filepath.Join(bdir, kernelDirs[i]))
			if n > s.klast[i] {
				s.klast[i] = n
			}
			*row = kernelRow{Running: true, Secs: ksecs[i], Objects: n, Total: s.kmax[i]}
			continue
		}
		if s.kstart[i] != 0 {
			if t, ok := mtime(filepath.Join(bdir, kernelOuts[i])); ok && t > s.kstart[i] && s.klast[i] > 0 {
				s.kmax[i] = s.klast[i]
				os.WriteFile(filepath.Join(bdir, ".tinyhat-build-viewer-kernel-"+arches[i]), []byte(strconv.Itoa(s.kmax[i])), 0o644)
			}
			s.kstart[i], s.klast[i] = 0, 0
		}
		if t, ok := mtime(filepath.Join(bdir, kernelOuts[i])); ok {
			row.Built = time.Unix(int64(t), 0).Format("15:04")
		}
	}

	mk := readFile(filepath.Join(bdir, "pkgs.mk"))
	rules := map[string][]string{}
	for _, m := range ruleRe.FindAllStringSubmatch(mk, -1) {
		rules[m[1]+"/"+m[2]] = tokenRe.FindAllString(m[3], -1)
	}
	wanted := map[string]bool{}
	var want func(key string)
	want = func(key string) {
		if wanted[key] {
			return
		}
		if _, ok := rules[key]; !ok {
			return
		}
		wanted[key] = true
		for _, tok := range rules[key] {
			if m := stampRe.FindStringSubmatch(tok); m != nil {
				want(m[1] + "/" + m[2])
			}
		}
	}
	lists := map[string][]string{}
	for _, m := range listRe.FindAllStringSubmatch(mk, -1) {
		for _, st := range listStampRe.FindAllStringSubmatch(m[2], -1) {
			want(st[1] + "/" + st[2])
			lists[m[1]] = append(lists[m[1]], st[1]+"/"+st[2])
		}
	}
	if time.Since(s.srcAt) > time.Minute {
		s.src, s.srcAt = map[string]float64{}, time.Now()
	}
	mt := map[string]float64{}
	stat := func(path string) (float64, bool) {
		if v, ok := mt[path]; ok {
			return v, v >= 0
		}
		v, ok := mtime(path)
		if !ok {
			v = -1
		}
		mt[path] = v
		return v, ok
	}
	memo := map[string]bool{}
	var stale func(key string) bool
	stale = func(key string) bool {
		if v, ok := memo[key]; ok {
			return v
		}
		memo[key] = true
		t, ok := stat(filepath.Join(bdir, "pkg", key))
		result := !ok
		for _, tok := range rules[key] {
			if result || tok == "|" {
				break
			}
			if m := wildRe.FindStringSubmatch(tok); m != nil {
				skipTiny := strings.HasPrefix(tok, "$(filter-out")
				d := filepath.Join(top, m[1])
				k := d
				if skipTiny {
					k += "|tiny"
				}
				if _, ok := s.src[k]; !ok {
					s.src[k] = newest(d, skipTiny)
				}
				result = s.src[k] > t
				continue
			}
			if m := stampRe.FindStringSubmatch(tok); m != nil {
				if _, ok := rules[m[1]+"/"+m[2]]; ok {
					dt, _ := stat(filepath.Join(bdir, "pkg", m[1], m[2]))
					result = stale(m[1]+"/"+m[2]) || dt > t
					continue
				}
			}
			path := filepath.Join(top, tok)
			if strings.HasPrefix(tok, "$(B)") {
				path = bdir + tok[4:]
			}
			dt, ok := stat(path)
			result = !ok || dt > t
		}
		memo[key] = result
		return result
	}
	done, total := 0, 0
	for key := range wanted {
		arch := key[:strings.IndexByte(key, '/')]
		if arch == "boot" {
			arch = "x32"
		}
		for i, a := range pkgArches {
			if a != arch {
				continue
			}
			snap.Total[i]++
			if !stale(key) {
				snap.Done[i]++
			} else {
				snap.Todo[i] = append(snap.Todo[i], key[strings.IndexByte(key, '/')+1:])
			}
			if t, ok := stat(filepath.Join(bdir, "pkg", key)); ok && start > 0 && t >= start {
				snap.Run[i]++
			}
		}
	}
	for _, key := range lists["ROOTFS_X32_STAMPS"] {
		if strings.HasPrefix(key, "x32/") || strings.HasPrefix(key, "boot/") {
			snap.X32Image[0]++
		} else {
			snap.X32Image[1]++
		}
	}
	for i := range pkgArches {
		done += snap.Done[i]
		total += snap.Total[i]
		slices.Sort(snap.Todo[i])
	}
	if total > 0 {
		snap.Progress = float64(done) / float64(total)
	}

	if time.Since(s.ccacheAt) > 30*time.Second {
		s.ccacheAt = time.Now()
		home, _ := os.UserHomeDir()
		cmd := exec.Command("ccache", "--print-stats")
		cmd.Env = append(os.Environ(), "CCACHE_DIR="+filepath.Join(home, ".ccache"))
		if out, err := cmd.Output(); err == nil {
			st := map[string]float64{}
			for line := range strings.SplitSeq(string(out), "\n") {
				if k, v, ok := strings.Cut(line, "\t"); ok {
					st[k], _ = strconv.ParseFloat(v, 64)
				}
			}
			hit := st["direct_cache_hit"] + st["preprocessed_cache_hit"]
			miss := st["cache_miss"]
			limit := "?"
			cmd := exec.Command("ccache", "-k", "max_size")
			cmd.Env = append(os.Environ(), "CCACHE_DIR="+filepath.Join(home, ".ccache"))
			if out, err := cmd.Output(); err == nil {
				limit = strings.TrimSpace(string(out))
			}
			s.ccache = fmt.Sprintf("ccache hit rate %.0f%%  (%.0f hits, %.0f misses, %.1f GiB of %s)", 100*hit/math.Max(1, hit+miss),
				hit, miss, st["cache_size_kibibyte"]/1048576, limit)
		}
	}
	var fsst syscall.Statfs_t
	disk := ""
	if syscall.Statfs(bdir, &fsst) == nil {
		disk = fmt.Sprintf("disk free for build/: %.0f GiB", float64(fsst.Bavail)*float64(fsst.Bsize)/(1<<30))
	}
	snap.Info = s.ccache + "     " + disk

	if len(logFiles) == 0 {
		newestLog := 0.0
		if es, err := os.ReadDir(bdir); err == nil {
			for _, e := range es {
				if !strings.HasSuffix(e.Name(), ".log") {
					continue
				}
				p := filepath.Join(bdir, e.Name())
				if t, ok := mtime(p); ok && t > newestLog {
					newestLog = t
					logFiles = []string{p}
				}
			}
		}
	}
	var names []string
	for _, p := range logFiles {
		snap.Logs = append(snap.Logs, tail(p, 80)...)
		names = append(names, rel(p))
	}
	if jobLog != "" {
		snap.Logs = append(snap.Logs, "", "--- "+rel(jobLog)+" ---")
		snap.Logs = append(snap.Logs, tail(jobLog, 12)...)
		names = append(names, rel(jobLog))
	}
	snap.LogName = strings.Join(names, ", ")
	for _, l := range snap.Logs {
		if strings.Contains(l, "failfast.py: stopping") || strings.Contains(l, "*** ") {
			snap.Failed = true
		}
	}
	if time.Since(s.imgAt) > 30*time.Second && s.imgBusy.CompareAndSwap(false, true) {
		s.imgAt = time.Now()
		go s.checkImages()
	}
	s.imgMu.Lock()
	state := s.imgState
	s.imgMu.Unlock()
	writing := map[string]bool{}
	for step := range steps {
		if after, ok := strings.CutPrefix(step, "writing the disk image "); ok {
			writing[after] = true
		}
	}
	for _, img := range imageNames {
		row := imageRow{Name: img, State: "checking"}
		if st, ok := state[img]; ok {
			row.State = st
		}
		if t, ok := mtime(filepath.Join(top, img)); ok {
			row.When = time.Unix(int64(t), 0).Format("Jan 2 15:04")
		}
		if writing[img] {
			row.State = "writing now"
		}
		snap.Images = append(snap.Images, row)
	}
	snap.CPU = append([]float64(nil), s.cpu...)
	snap.Mem = append([]float64(nil), s.mem...)
	snap.Load = append([]float64(nil), s.load...)
	return snap
}
