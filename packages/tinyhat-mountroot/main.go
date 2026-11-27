// tinyhat-mountroot is the /init of Tiny Hat's initramfs. It finds the boot medium (an XFS partition labeled TINYHAT,
// or a CD with tinyhat/root.sfs on it), mounts the squashfs root file system (copied to RAM when there is room),
// sets up persistence (a THDATA partition on the boot disk, or a save file on another disk) under an overlay, and
// switches to the real root and its init. When something fails, it shows the guru meditation screen
// (tinyhat-guru). Run as /run/initramfs/shutdown, it is the shutdown helper that systemd runs at power off.
//
// Kernel command line: tinyhat.label=LABEL (TINYHAT), init=PATH (/usr/lib/systemd/systemd),
// tinyhat.toram[=0|1] (by default when there is room and no hypervisor), tinyhat.nopersist, tinyhat.reset,
// tinyhat.persist=stick|machine, quiet, tinyhat.nosplash.
package main

import (
	"bufio"
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"syscall"
	"time"
	"unsafe"
)

const (
	systemd     = "/usr/lib/systemd/systemd"
	splashFifo  = "/run/tinyhat-splash"
	tinyhatDisk = 0x7417a700 // the MBR disk id of a Tiny Hat image
	mib         = 1 << 20
)

var (
	quiet = false
	// the boot medium, the disk it is on, and how persistence is set up ("disk", "file" or "")
	dev, disk, where string
)

func main() {
	if filepath.Base(os.Args[0]) == "shutdown" {
		shutdown()
		return
	}
	os.Setenv("PATH", "/usr/bin:/bin")
	mount("dev", "/dev", "devtmpfs", "mode=0755,nosuid")
	mount("proc", "/proc", "proc", "nosuid,nodev,noexec")
	mount("sys", "/sys", "sysfs", "nosuid,nodev,noexec")
	mount("run", "/run", "tmpfs", "mode=0755,nosuid,nodev")

	label, init, toram, persist, reset, mode, splash := "TINYHAT", systemd, "auto", true, false, "stick", true
	for _, arg := range strings.Fields(readString("/proc/cmdline")) {
		k, v, _ := strings.Cut(arg, "=")
		switch {
		case k == "tinyhat.label":
			label = v
		case k == "init":
			init = v
		case arg == "tinyhat.toram" || arg == "tinyhat.toram=1":
			toram = "1"
		case arg == "tinyhat.toram=0":
			toram = "0"
		case arg == "tinyhat.nopersist":
			persist = false
		case arg == "tinyhat.reset":
			reset = true
		case k == "tinyhat.persist":
			mode = v
		case arg == "quiet":
			quiet = true
		case arg == "tinyhat.nosplash":
			splash = false
		}
	}
	loadModules()
	if quiet {
		writeFile("/dev/tty1", "\033[?25l")
		if splash && exists("/dev/fb0") {
			run("/bin/tinyhat-splash")
		}
	}
	if init != systemd {
		writeFile("/proc/sys/kernel/ctrl-alt-del", "1\n")
	}

	// The boot medium: wait up to 30 seconds for it, then tell, and wait again
	for deadline := uptime() + 30; ; time.Sleep(200 * time.Millisecond) {
		if dev = findLabel(label, allBlockDevices()); dev != "" {
			break
		}
		if dev = findCD(); dev != "" {
			break
		}
		if uptime() >= deadline {
			guru("no bootable partition, USB stick or CDROM labeled "+label+" found", true)
			deadline = uptime() + 30
		}
	}
	for _, d := range []string{"/run/tinyhat/boot", "/run/tinyhat/lower", "/run/tinyhat/rw", "/run/tinyhat/data", "/run/tinyhat/host", "/newroot"} {
		os.MkdirAll(d, 0o755)
	}
	if !exists("/run/tinyhat/boot/tinyhat/root.sfs") {
		check(mount(dev, "/run/tinyhat/boot", "xfs", "ro,noatime"), "could not mount "+dev)
	}
	sfs := "/run/tinyhat/boot/tinyhat/root.sfs"
	if toram == "auto" {
		toram = "0"
		if st, err := os.Stat(sfs); err == nil && memTotal() >= st.Size()+250*mib && !hypervisor() {
			toram = "1"
		}
	}
	if toram == "1" {
		tell("Tiny Hat: copying the system to RAM, this takes a moment...")
		mount("ram", "/run/tinyhat/rw", "tmpfs", "size=90%,mode=0755")
		say("msg Copying Tiny Hat to RAM")
		if err := copyFile(sfs, "/run/tinyhat/rw/root.sfs"); err == nil {
			sfs = "/run/tinyhat/rw/root.sfs"
			writeFile("/run/tinyhat/toram", "")
		} else {
			os.Remove("/run/tinyhat/rw/root.sfs")
		}
		say("progress -1")
		say("msg")
	} else {
		mount("rw", "/run/tinyhat/rw", "tmpfs", "size=75%,mode=0755")
	}
	check(mountLoop(sfs, "/run/tinyhat/lower", "squashfs", "ro"), "could not mount "+sfs)

	// Persistence: on the boot disk (a THDATA partition), or in a save file on another disk
	name := filepath.Base(dev)
	disk = filepath.Base(resolve("/sys/class/block/" + name + "/.."))
	if !exists("/sys/class/block/"+name+"/partition") || !exists("/sys/block/"+disk) {
		persist = false
	}
	switch mode {
	case "", "stick":
		if removable(disk) || isTinyhatDisk(disk) {
			where = "disk"
		}
	case "machine":
		where = "disk"
		if removable(disk) {
			where = "file"
		}
	}
	if !persist {
		where = ""
	}
	if where == "disk" && findLabel("THDATA", partitions(disk)) == "" {
		if err := makeData(); err != nil {
			tell("Tiny Hat: no persistent storage: " + err.Error())
		}
		say("progress -1")
		say("msg")
	}
	if data := findLabel("THDATA", partitions(disk)); where == "disk" && data != "" && data != dev {
		if mount(data, "/run/tinyhat/data", "xfs", "noatime,lazytime") != nil {
			where = ""
		}
	} else if where == "file" && findSaveFile() {
		installShutdown()
	} else {
		where = ""
	}
	if reset && where != "" && mounted("/run/tinyhat/data") {
		say("msg Erasing the saved packages, files and settings")
		entries, _ := os.ReadDir("/run/tinyhat/data")
		for _, e := range entries {
			os.RemoveAll("/run/tinyhat/data/" + e.Name())
		}
		writeFile("/run/tinyhat/data/.tinyhat-seed", "")
		say("msg")
	}

	// The boot animation that was chosen, from the persistent data
	anim := "/run/tinyhat/data/bootsplash/active"
	if !exists(anim) {
		anim = "/run/tinyhat/data/state/bootanim/hat"
	}
	if where != "" && exists(anim) {
		if b, err := os.ReadFile(anim); err == nil {
			os.WriteFile("/run/tinyhat/bootsplash-active", b, 0o644)
		}
		if isFifo(splashFifo) {
			say("hat /run/tinyhat/lower " + anim)
			for i := 0; i < 20 && !exists(splashFifo+".hat"); i++ {
				time.Sleep(100 * time.Millisecond)
			}
		}
	}

	// The root: the squashfs under an overlay, with the changes kept in the persistent data (in a directory for this
	// build of the root file system, by its squashfs creation time) or else in RAM
	if where != "" {
		root := "root-" + strconv.FormatUint(uint64(sfsTime(sfs)), 10)
		up, work := "/run/tinyhat/data/"+root+"/upper", "/run/tinyhat/data/"+root+"/work"
		os.MkdirAll(up, 0o755)
		os.MkdirAll(work, 0o755)
		if mount("overlay", "/newroot", "overlay", "lowerdir=/run/tinyhat/lower,upperdir="+up+",workdir="+work) == nil {
			writeFile("/run/tinyhat/persistroot", root+"\n")
		}
	}
	if !exists("/run/tinyhat/persistroot") {
		os.MkdirAll("/run/tinyhat/rw/upper", 0o755)
		os.MkdirAll("/run/tinyhat/rw/work", 0o755)
		check(mount("overlay", "/newroot", "overlay", "lowerdir=/run/tinyhat/lower,upperdir=/run/tinyhat/rw/upper,workdir=/run/tinyhat/rw/work"),
			"could not mount the overlay")
	}
	for _, m := range []string{"/run/tinyhat/lower", "/run/tinyhat/rw", "/run/tinyhat/boot"} {
		syscall.Unmount(m, syscall.MNT_DETACH)
	}
	if where == "disk" {
		syscall.Unmount("/run/tinyhat/data", syscall.MNT_DETACH)
	}
	if where == "file" && !exists("/run/tinyhat/persistroot") {
		syscall.Unmount("/run/tinyhat/data", 0)
		syscall.Unmount("/run/tinyhat/host", 0)
		os.RemoveAll("/run/initramfs")
	}
	writeFile("/run/tinyhat/bootdev", dev+"\n")
	if init != systemd {
		os.Stdout.WriteString("\033[?25h")
		say("quit")
		time.Sleep(300 * time.Millisecond)
	}
	switchRoot(init)
}

// switchRoot moves /run to the new root, frees the initramfs and runs init in the new root, as switch_root does
func switchRoot(init string) {
	os.MkdirAll("/newroot/run", 0o755)
	check(syscall.Mount("/run", "/newroot/run", "", syscall.MS_MOVE, ""), "could not move /run")
	syscall.Unmount("/sys", 0)
	syscall.Unmount("/proc", 0)
	var st syscall.Stat_t
	if syscall.Stat("/", &st) == nil {
		removeTree("/", uint64(st.Dev))
	}
	check(syscall.Chdir("/newroot"), "could not enter the new root")
	check(syscall.Mount(".", "/", "", syscall.MS_MOVE, ""), "could not move the new root")
	check(syscall.Chroot("."), "could not enter the new root")
	syscall.Chdir("/")
	err := syscall.Exec(init, []string{init}, os.Environ())
	guru("could not start "+init+": "+err.Error(), false)
}

// removeTree removes what is in dir on the device dev (the initramfs), but not other file systems, like /newroot
func removeTree(dir string, dev uint64) {
	entries, _ := os.ReadDir(dir)
	for _, e := range entries {
		p := filepath.Join(dir, e.Name())
		var st syscall.Stat_t
		if syscall.Lstat(p, &st) != nil || uint64(st.Dev) != dev {
			continue
		}
		if e.IsDir() {
			removeTree(p, dev)
			syscall.Rmdir(p)
		} else {
			syscall.Unlink(p)
		}
	}
}

// guru shows the guru meditation screen with the error and the last kernel messages, and restarts or powers off.
// With retry, any other key returns, to look for the boot medium again.
func guru(what string, retry bool) {
	say("quit")
	buf := make([]byte, 1<<16)
	n, _ := syscall.Klogctl(3, buf)
	lines := strings.Split(strings.TrimRight(string(buf[:max(n, 0)]), "\n"), "\n")
	if len(lines) > 30 {
		lines = lines[len(lines)-30:]
	}
	writeFile("/run/guru.log", strings.Join(append(lines, what), "\n")+"\n")
	for {
		code := -1
		if exists("/bin/tinyhat-guru") {
			cmd := exec.Command("/bin/tinyhat-guru", "Tiny Hat could not start: "+what, "2", "/run/guru.log", "")
			cmd.Stdin, cmd.Stdout, cmd.Stderr = os.Stdin, os.Stdout, os.Stderr
			err := cmd.Run()
			code = 0
			var exit *exec.ExitError
			if errors.As(err, &exit) {
				code = exit.ExitCode()
			}
		} else {
			fmt.Println("Tiny Hat could not start:", what)
			time.Sleep(10 * time.Second)
		}
		switch code {
		case 'r':
			reboot(syscall.LINUX_REBOOT_CMD_RESTART)
		case 'p':
			reboot(syscall.LINUX_REBOOT_CMD_POWER_OFF)
		}
		if retry {
			return
		}
	}
}

func check(err error, what string) {
	if err != nil {
		guru(what+": "+err.Error(), false)
	}
}

// tell writes a line on the console, unless the boot is quiet
func tell(s string) {
	if !quiet {
		fmt.Println(s)
	}
}

// say sends a command to the boot splash (msg TEXT, progress N, hat DIR FILE, quit), if it runs, waiting at most a
// second for it
func say(s string) {
	if !isFifo(splashFifo) {
		return
	}
	for end := time.Now().Add(time.Second); time.Now().Before(end); time.Sleep(20 * time.Millisecond) {
		fd, err := syscall.Open(splashFifo, syscall.O_WRONLY|syscall.O_NONBLOCK|syscall.O_CLOEXEC, 0)
		if err == syscall.ENXIO {
			continue
		}
		if err == nil {
			syscall.Write(fd, []byte(s+"\n"))
			syscall.Close(fd)
		}
		return
	}
}

// mount mounts with mount(8)-style options: the flags among them, and the rest as file system data
func mount(src, target, fstype, opts string) error {
	var flags uintptr
	var data []string
	for _, o := range strings.Split(opts, ",") {
		switch o {
		case "":
		case "ro":
			flags |= syscall.MS_RDONLY
		case "noatime":
			flags |= syscall.MS_NOATIME
		case "lazytime":
			flags |= 1 << 25 // MS_LAZYTIME
		case "nosuid":
			flags |= syscall.MS_NOSUID
		case "nodev":
			flags |= syscall.MS_NODEV
		case "noexec":
			flags |= syscall.MS_NOEXEC
		default:
			data = append(data, o)
		}
	}
	return syscall.Mount(src, target, fstype, flags, strings.Join(data, ","))
}

func mounted(target string) bool {
	for _, line := range strings.Split(readString("/proc/mounts"), "\n") {
		if f := strings.Fields(line); len(f) > 1 && f[1] == target {
			return true
		}
	}
	return false
}

const (
	loopCtlGetFree = 0x4c82
	loopConfigure  = 0x4c0a
	loopClrFd      = 0x4c01
	loFlagsRO      = 1
	loFlagsClear   = 4
)

// loopConfig is struct loop_config: the backing file, the block size and struct loop_info64
type loopConfig struct {
	Fd, BlockSize                              uint32
	Device, Inode, Rdevice, Offset, SizeLimit  uint64
	Number, EncryptType, EncryptKeySize, Flags uint32
	FileName, CryptName                        [64]byte
	EncryptKey                                 [32]byte
	Init                                       [2]uint64
	Reserved                                   [8]uint64
}

// mountLoop mounts a file through a free loop device, read-only when opts start with "ro". The loop device goes away
// when it is unmounted (LO_FLAGS_AUTOCLEAR), so it is kept open until the mount holds it: closing it first would
// detach it, and the mount would read from a loop device that is being taken down (EIO)
func mountLoop(file, dir, fstype, opts string) error {
	ro := opts == "ro" || strings.HasPrefix(opts, "ro,")
	l, err := loopSetup(file, ro)
	if err != nil {
		return err
	}
	defer l.Close()
	return mount(l.Name(), dir, fstype, opts)
}

// loopSetup attaches a file to a free loop device, and returns the loop device, open
func loopSetup(file string, ro bool) (*os.File, error) {
	ctl, err := os.OpenFile("/dev/loop-control", os.O_RDWR, 0)
	if err != nil {
		return nil, err
	}
	defer ctl.Close()
	n, _, errno := syscall.Syscall(syscall.SYS_IOCTL, ctl.Fd(), loopCtlGetFree, 0)
	if errno != 0 {
		return nil, errno
	}
	name := "/dev/loop" + strconv.Itoa(int(n))
	flag := os.O_RDWR
	if ro {
		flag = os.O_RDONLY
	}
	f, err := os.OpenFile(file, flag, 0)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	var l *os.File
	for i := 0; i < 50; i++ {
		if l, err = os.OpenFile(name, flag, 0); err == nil {
			break
		}
		time.Sleep(20 * time.Millisecond)
	}
	if err != nil {
		return nil, err
	}
	cfg := loopConfig{Fd: uint32(f.Fd()), Flags: loFlagsClear}
	if ro {
		cfg.Flags |= loFlagsRO
	}
	copy(cfg.FileName[:], file)
	if _, _, errno := syscall.Syscall(syscall.SYS_IOCTL, l.Fd(), loopConfigure, uintptr(unsafe.Pointer(&cfg))); errno != 0 {
		l.Close()
		return nil, errno
	}
	return l, nil
}

// readAt reads n bytes at off of a file or device, or nil
func readAt(path string, off int64, n int) []byte {
	f, err := os.Open(path)
	if err != nil {
		return nil
	}
	defer f.Close()
	b := make([]byte, n)
	if _, err := f.ReadAt(b, off); err != nil {
		return nil
	}
	return b
}

// xfsLabel is the label of an XFS file system, or ""
func xfsLabel(path string) string {
	b := readAt(path, 0, 120)
	if b == nil || string(b[:4]) != "XFSB" {
		return ""
	}
	return string(bytes.TrimRight(b[108:120], "\x00"))
}

// fsType tells the file system on a device from its superblock: ext4 (ext2, ext3 and ext4), xfs, vfat, exfat, ntfs
func fsType(path string) string {
	b := readAt(path, 0, 2048)
	switch {
	case b == nil:
		return ""
	case string(b[:4]) == "XFSB":
		return "xfs"
	case binary.LittleEndian.Uint16(b[0x438:]) == 0xef53:
		return "ext4"
	case string(b[3:11]) == "NTFS    ":
		return "ntfs"
	case string(b[3:11]) == "EXFAT   ":
		return "exfat"
	case string(b[0x36:0x39]) == "FAT" || string(b[0x52:0x57]) == "FAT32":
		return "vfat"
	}
	return ""
}

// allBlockDevices are the block devices and partitions, in name order
func allBlockDevices() []string {
	entries, _ := os.ReadDir("/sys/class/block")
	var out []string
	for _, e := range entries {
		out = append(out, "/dev/"+e.Name())
	}
	return out
}

// partitions are the partitions of a disk, in name order
func partitions(disk string) []string {
	entries, _ := os.ReadDir("/sys/block/" + disk)
	var out []string
	for _, e := range entries {
		if strings.HasPrefix(e.Name(), disk) && exists("/sys/block/"+disk+"/"+e.Name()+"/partition") {
			out = append(out, "/dev/"+e.Name())
		}
	}
	return out
}

func findLabel(label string, devices []string) string {
	for _, d := range devices {
		if isBlock(d) && xfsLabel(d) == label {
			return d
		}
	}
	return ""
}

// findCD finds a CD (an ISO 9660 file system) with tinyhat/root.sfs on it, and leaves it mounted at /run/tinyhat/boot
func findCD() string {
	entries, _ := os.ReadDir("/sys/block")
	sort.SliceStable(entries, func(i, j int) bool {
		return strings.HasPrefix(entries[i].Name(), "sr") && !strings.HasPrefix(entries[j].Name(), "sr")
	})
	for _, e := range entries {
		n := e.Name()
		if strings.HasPrefix(n, "loop") || strings.HasPrefix(n, "ram") || strings.HasPrefix(n, "zram") || strings.HasPrefix(n, "fd") {
			continue
		}
		d := "/dev/" + n
		if !isBlock(d) || string(readAt(d, 32769, 5)) != "CD001" {
			continue
		}
		os.MkdirAll("/run/tinyhat/boot", 0o755)
		if mount(d, "/run/tinyhat/boot", "iso9660", "ro") != nil {
			continue
		}
		if exists("/run/tinyhat/boot/tinyhat/root.sfs") {
			return d
		}
		syscall.Unmount("/run/tinyhat/boot", 0)
	}
	return ""
}

func removable(disk string) bool {
	return strings.TrimSpace(readString("/sys/block/"+disk+"/removable")) == "1" ||
		strings.Contains(resolve("/sys/block/"+disk+"/device"), "/usb") ||
		strings.TrimSpace(readString("/sys/block/"+disk+"/device/type")) == "SD"
}

// isTinyhatDisk tells if the disk has the MBR disk id of a Tiny Hat image
func isTinyhatDisk(disk string) bool {
	b := readAt("/dev/"+disk, 440, 4)
	return b != nil && binary.LittleEndian.Uint32(b) == tinyhatDisk
}

// partType is the MBR partition type of partition n (1 to 4) of a disk, in hex like sfdisk --part-type, or ""
func partType(disk string, n int) string {
	b := readAt("/dev/"+disk, 446+16*int64(n-1), 16)
	if b == nil || b[4] == 0 {
		return ""
	}
	return strconv.FormatUint(uint64(b[4]), 16)
}

// partNum is the device of partition n of the disk, or ""
func partNum(n int) string {
	for _, p := range partitions(disk) {
		if strings.TrimSpace(readString("/sys/class/block/"+filepath.Base(p)+"/partition")) == strconv.Itoa(n) {
			return p
		}
	}
	return ""
}

// makeData makes the THDATA partition for persistence on a Tiny Hat stick: partition 3, on the rest of the drive,
// only when the boot partition is partition 2 and partition 1 is the EFI or FAT partition of a Tiny Hat image
func makeData() error {
	if !exists("/usr/bin/mkfs.xfs") || partNum(2) != dev || !isTinyhatDisk(disk) {
		return errors.New("not a Tiny Hat drive")
	}
	if t := partType(disk, 1); t != "ef" && t != "c" {
		return errors.New("not a Tiny Hat drive")
	}
	count := len(partitions(disk))
	tell("Tiny Hat: creating persistent storage on the rest of the drive (one time only)")
	say("msg Creating persistent storage (one time only)")
	say("progress 5")
	switch count {
	case 2:
		out, _ := exec.Command("/usr/bin/sfdisk", "-F", "-q", "/dev/"+disk).Output()
		lines := strings.Split(strings.TrimSpace(string(out)), "\n")
		f := strings.Fields(lines[len(lines)-1])
		if len(f) < 3 {
			return errors.New("no free space")
		}
		if free, err := strconv.ParseInt(f[2], 10, 64); err != nil || free/2048 < 64 {
			return errors.New("less than 64 MiB free")
		}
		cmd := exec.Command("/usr/bin/sfdisk", "--append", "--no-reread", "-q", "/dev/"+disk)
		cmd.Stdin = strings.NewReader(",,L\n")
		if err := cmd.Run(); err != nil {
			return fmt.Errorf("sfdisk: %w", err)
		}
		if exec.Command("/usr/bin/partx", "-a", "/dev/"+disk).Run() != nil {
			exec.Command("/usr/bin/partx", "-u", "/dev/"+disk).Run()
		}
	case 3:
	default:
		return errors.New("unexpected partitions")
	}
	part := ""
	for i := 0; i < 50; i++ {
		if part = partNum(3); part != "" && isBlock(part) {
			break
		}
		time.Sleep(100 * time.Millisecond)
	}
	if part == "" || !isBlock(part) {
		return errors.New("partition 3 did not appear")
	}
	if partType(disk, 3) != "83" {
		return errors.New("partition 3 is not a Linux partition")
	}
	if out, _ := exec.Command("/usr/bin/wipefs", "-n", part).CombinedOutput(); len(out) > 0 {
		return errors.New("partition 3 is not empty")
	}
	say("progress 35")
	say("msg Formatting persistent storage")
	cmd := exec.Command("/usr/bin/mkfs.xfs", "-q", "-f", "-K", "-l", "size=16m", "-L", "THDATA", part)
	cmd.Env = append(os.Environ(), "TEST_DIR=1", "TEST_DEV=1", "QA_CHECK_FS=xfs")
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("mkfs.xfs: %w", err)
	}
	if err := mount(part, "/run/tinyhat/data", "xfs", "noatime"); err != nil {
		return err
	}
	writeFile("/run/tinyhat/data/.tinyhat-seed", "")
	syscall.Unmount("/run/tinyhat/data", 0)
	say("progress 100")
	return nil
}

// hostMount mounts a partition of another operating system at /run/tinyhat/host
func hostMount(d, opts string) bool {
	t := fsType(d)
	switch t {
	case "vfat":
		opts += ",flush"
	case "ntfs":
		t = "ntfs3"
	case "ext4", "xfs", "exfat":
	default:
		return false
	}
	return mount(d, "/run/tinyhat/host", t, opts) == nil
}

// hibernated tells if the operating system on a partition is hibernated, so that it must not be written to
func hibernated(d string) bool {
	switch fsType(d) {
	case "ext4":
		b := readAt(d, 1120, 4) // s_feature_incompat: EXT3_FEATURE_INCOMPAT_RECOVER
		return b != nil && binary.LittleEndian.Uint32(b)&4 != 0
	case "ntfs":
		b := readAt("/run/tinyhat/host/hiberfil.sys", 0, 4)
		switch string(b) {
		case "hibr", "HIBR", "wake", "WAKE":
			return true
		}
	}
	return false
}

// findSaveFile finds tinyhat/config.img (XFS, labeled THCONFIG) on a fixed disk that is not the boot disk, and mounts
// it at /run/tinyhat/data, with its partition mounted read-write at /run/tinyhat/host
func findSaveFile() bool {
	entries, _ := os.ReadDir("/sys/block")
	for _, e := range entries {
		n := e.Name()
		if n == disk || removable(n) {
			continue
		}
		skip := false
		for _, p := range []string{"loop", "ram", "zram", "sr", "fd"} {
			skip = skip || strings.HasPrefix(n, p)
		}
		if skip {
			continue
		}
		for _, d := range partitions(n) {
			if !isBlock(d) {
				continue
			}
			opts := "ro"
			switch fsType(d) {
			case "ext4":
				opts = "ro,noload"
			case "xfs":
				opts = "ro,norecovery"
			case "vfat", "exfat", "ntfs":
			default:
				continue
			}
			if !hostMount(d, opts) {
				continue
			}
			f := "/run/tinyhat/host/tinyhat/config.img"
			if exists(f) && xfsLabel(f) == "THCONFIG" && !hibernated(d) {
				syscall.Unmount("/run/tinyhat/host", 0)
				if !hostMount(d, "noatime") {
					return false
				}
				if hostWritable() {
					if mountLoop(f, "/run/tinyhat/data", "xfs", "noatime,lazytime") == nil {
						return true
					}
				}
				syscall.Unmount("/run/tinyhat/host", 0)
				return false
			}
			syscall.Unmount("/run/tinyhat/host", 0)
		}
	}
	return false
}

func hostWritable() bool {
	for _, line := range strings.Split(readString("/proc/mounts"), "\n") {
		if f := strings.Fields(line); len(f) > 3 && f[1] == "/run/tinyhat/host" {
			return strings.HasPrefix(f[3], "rw")
		}
	}
	return false
}

// installShutdown puts this program at /run/initramfs/shutdown, which systemd runs at power off, to unmount the
// save file and the partition it is on
func installShutdown() {
	self, err := os.ReadFile("/proc/self/exe")
	if err != nil {
		return
	}
	os.MkdirAll("/run/initramfs", 0o755)
	os.WriteFile("/run/initramfs/shutdown", self, 0o755)
}

// shutdown unmounts what is left of the old root (/oldroot), deepest first, and its loop devices, and then reboots,
// halts or powers off (argv[1]), like systemd-shutdown's own final step
func shutdown() {
	for _, d := range []string{"/proc", "/sys", "/dev"} {
		os.MkdirAll(d, 0o755)
	}
	if !exists("/proc/mounts") {
		mount("proc", "/proc", "proc", "")
	}
	if !exists("/dev/null") {
		mount("dev", "/dev", "devtmpfs", "")
	}
	if !exists("/sys/block") {
		mount("sys", "/sys", "sysfs", "")
	}
	syscall.Sync()
	for i := 0; i < 6; i++ {
		var old []string
		for _, line := range strings.Split(readString("/proc/mounts"), "\n") {
			if f := strings.Fields(line); len(f) > 1 && strings.HasPrefix(f[1], "/oldroot") {
				old = append(old, f[1])
			}
		}
		if len(old) == 0 {
			break
		}
		sort.Sort(sort.Reverse(sort.StringSlice(old)))
		for _, m := range old {
			if syscall.Unmount(m, 0) != nil {
				syscall.Mount("", m, "", syscall.MS_REMOUNT|syscall.MS_RDONLY, "")
			}
		}
		entries, _ := os.ReadDir("/sys/block")
		for _, e := range entries {
			if strings.HasPrefix(e.Name(), "loop") && exists("/sys/block/"+e.Name()+"/loop/backing_file") {
				if f, err := os.Open("/dev/" + e.Name()); err == nil {
					syscall.Syscall(syscall.SYS_IOCTL, f.Fd(), loopClrFd, 0)
					f.Close()
				}
			}
		}
	}
	var cmd uint32 = syscall.LINUX_REBOOT_CMD_POWER_OFF
	if len(os.Args) > 1 {
		switch os.Args[1] {
		case "reboot":
			cmd = syscall.LINUX_REBOOT_CMD_RESTART
		case "halt":
			cmd = syscall.LINUX_REBOOT_CMD_HALT
		}
	}
	reboot(cmd)
}

// reboot syncs and reboots, halts or powers off. Not syscall.Reboot: on 386 its int cannot hold the halt command
func reboot(cmd uint32) {
	syscall.Sync()
	syscall.RawSyscall6(syscall.SYS_REBOOT, syscall.LINUX_REBOOT_MAGIC1, syscall.LINUX_REBOOT_MAGIC2, uintptr(cmd), 0, 0, 0)
}

// loadModules loads the kernel modules in /modules/order, in that order
func loadModules() {
	f, err := os.Open("/modules/order")
	if err != nil {
		return
	}
	defer f.Close()
	s := bufio.NewScanner(f)
	for s.Scan() {
		m := strings.TrimSpace(s.Text())
		if m == "" {
			continue
		}
		mf, err := os.Open("/modules/" + m + ".ko")
		if err != nil {
			continue
		}
		param, _ := syscall.BytePtrFromString("")
		syscall.Syscall(sysFinitModule, mf.Fd(), uintptr(unsafe.Pointer(param)), 0)
		mf.Close()
	}
}

// copyFile copies the squashfs to RAM in 8 MiB pieces, telling the splash how far it got
func copyFile(src, dst string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()
	st, err := in.Stat()
	if err != nil {
		return err
	}
	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	buf := make([]byte, 8*mib)
	chunks := (st.Size() + 8*mib - 1) / (8 * mib)
	for i := int64(1); ; i++ {
		n, rerr := io.ReadFull(in, buf)
		if n > 0 {
			if _, err := out.Write(buf[:n]); err != nil {
				out.Close()
				return err
			}
			say("progress " + strconv.FormatInt(min(i*100/max(chunks, 1), 100), 10))
		}
		if rerr == io.EOF || rerr == io.ErrUnexpectedEOF {
			break
		}
		if rerr != nil {
			out.Close()
			return rerr
		}
	}
	return out.Close()
}

// sfsTime is the creation time in the squashfs superblock (offset 8), which names the persistent root directory
func sfsTime(sfs string) uint32 {
	b := readAt(sfs, 8, 4)
	if b == nil {
		return 0
	}
	return binary.LittleEndian.Uint32(b)
}

func memTotal() int64 {
	for _, line := range strings.Split(readString("/proc/meminfo"), "\n") {
		if f := strings.Fields(line); len(f) > 1 && f[0] == "MemTotal:" {
			kb, _ := strconv.ParseInt(f[1], 10, 64)
			return kb * 1024
		}
	}
	return 0
}

// hypervisor tells if this is a virtual machine, where the disk is a file the host already caches, so that a copy to
// RAM would only cost memory and boot time
func hypervisor() bool {
	for _, line := range strings.Split(readString("/proc/cpuinfo"), "\n") {
		if strings.HasPrefix(line, "flags") {
			for _, f := range strings.Fields(line) {
				if f == "hypervisor" {
					return true
				}
			}
		}
	}
	return false
}

func uptime() int64 {
	f := strings.Fields(readString("/proc/uptime"))
	if len(f) == 0 {
		return 0
	}
	s, _ := strconv.ParseFloat(f[0], 64)
	return int64(s)
}

func run(path string, args ...string) error {
	cmd := exec.Command(path, args...)
	cmd.Stdin, cmd.Stdout, cmd.Stderr = os.Stdin, os.Stdout, os.Stderr
	return cmd.Run()
}

func readString(path string) string {
	b, _ := os.ReadFile(path)
	return string(b)
}

func writeFile(path, s string) {
	os.WriteFile(path, []byte(s), 0o644)
}

func exists(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}

func resolve(path string) string {
	p, err := filepath.EvalSymlinks(path)
	if err != nil {
		return path
	}
	return p
}

func isBlock(path string) bool {
	st, err := os.Stat(path)
	return err == nil && st.Mode()&os.ModeDevice != 0 && st.Mode()&os.ModeCharDevice == 0
}

func isFifo(path string) bool {
	st, err := os.Stat(path)
	return err == nil && st.Mode()&os.ModeNamedPipe != 0
}
