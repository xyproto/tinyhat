package main

import (
	"math"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/Zyko0/go-sdl3/sdl"
	"github.com/Zyko0/go-sdl3/ttf"
)

type rgb struct {
	r, g, b float32
}

var (
	top, bdir   string
	arches      = []string{"i686", "x86_64", "aarch64"}
	pkgArches   = []string{"i686", "x32", "x86_64", "aarch64"}
	pkgColors   = []rgb{{0.95, 0.55, 0.2}, {0.85, 0.45, 0.85}, {0.3, 0.7, 1}, {0.5, 0.85, 0.4}}
	kernelDirs  = []string{"linux-7.3.1", "linux-7.3.1-x86_64", "linux-7.3.1-arm64"}
	kernelOuts  = []string{"vmlinuz", "vmlinuz-x32", "Image-arm64"}
	imageNames  = []string{"tinyhat32.img", "tinyhat64x32.img", "tinyhat64.img", "tinyhat-aarch64.img", "tinyhat-rpi4.img"}
	rpi4Color   = rgb{0.85, 0.3, 0.4}
	kernelColor = rgb{0.85, 0.45, 0.9}
	feedColors  = map[string]rgb{
		"lobste.rs": {0.9, 0.3, 0.3}, "HN": {1, 0.6, 0.2}, "r/programming": {0.4, 0.75, 1}, "r/linux": {1, 0.85, 0.3},
		"r/archlinux": {0.3, 0.6, 1}, "r/golang": {0.35, 0.85, 0.85}, "r/retrogamedev": {1, 0.5, 0.8},
		"r/worldnews": {0.75, 0.6, 1}, "r/europe": {0.45, 0.55, 1}, "Phoronix": {0.5, 0.9, 0.5},
		"OSNews": {0.95, 0.75, 0.45}, "Gemini": {0.55, 0.75, 1},
	}
	white  = rgb{0.92, 0.92, 0.92}
	accent = rgb{1, 0.8, 0.3}
)

func findTop() string {
	var starts []string
	if wd, err := os.Getwd(); err == nil {
		starts = append(starts, wd)
	}
	if exe, err := os.Executable(); err == nil {
		starts = append(starts, filepath.Dir(exe))
	}
	for _, d := range starts {
		for ; ; d = filepath.Dir(d) {
			if _, err := os.Stat(filepath.Join(d, "distro.toml")); err == nil {
				return d
			}
			if filepath.Dir(d) == d {
				break
			}
		}
	}
	return "."
}

func readFile(path string) string {
	b, _ := os.ReadFile(path)
	return string(b)
}

func mtime(path string) (float64, bool) {
	fi, err := os.Stat(path)
	if err != nil {
		return 0, false
	}
	return float64(fi.ModTime().UnixNano()) / 1e9, true
}

func now() float64 { return float64(time.Now().UnixNano()) / 1e9 }

var numCPU = 1

func main() {
	top = findTop()
	bdir = filepath.Join(top, "build")
	if n, err := strconv.Atoi(strings.TrimSpace(func() string { out, _ := exec.Command("nproc").Output(); return string(out) }())); err == nil {
		numCPU = n
	}
	if err := sdl.LoadLibrary(sdl.Path()); err != nil {
		panic(err)
	}
	if err := ttf.LoadLibrary(ttf.Path()); err != nil {
		panic(err)
	}
	if err := sdl.Init(sdl.INIT_VIDEO); err != nil {
		panic(err)
	}
	defer sdl.Quit()
	if err := ttf.Init(); err != nil {
		panic(err)
	}
	fontPath := "/usr/share/fonts/noto/NotoSansMono-Regular.ttf"
	if out, err := exec.Command("fc-match", "-f", "%{file}", "monospace").Output(); err == nil && len(out) > 0 {
		fontPath = string(out)
	}
	win, r, err := sdl.CreateWindowAndRenderer("Tiny Hat build", 1000, 1032, sdl.WINDOW_RESIZABLE)
	if err != nil {
		panic(err)
	}
	r.SetVSync(1)
	r.SetDrawBlendMode(sdl.BLENDMODE_BLEND)
	v := &viewer{r: r, win: win, fontPath: fontPath, k: 1, fonts: map[int]*ttf.Font{}, cache: map[string]*text{},
		news: &news{summaries: map[string]string{}, inline: map[string]string{}}, seen: map[string]bool{}}
	s := &sampler{src: map[string]float64{}}
	for i, a := range arches {
		s.kmax[i], _ = strconv.Atoi(strings.TrimSpace(readFile(filepath.Join(bdir, ".tinyhat-build-viewer-kernel-"+a))))
	}
	s.sample()
	v.snap.Store(s.sample())
	go func() {
		for {
			time.Sleep(2 * time.Second)
			v.snap.Store(s.sample())
		}
	}()
	go v.news.fetchLoop()
	go v.news.summarizeLoop()
	start := time.Now()
	last := start
	for {
		var ev sdl.Event
		for sdl.PollEvent(&ev) {
			switch ev.Type {
			case sdl.EVENT_QUIT:
				return
			case sdl.EVENT_KEY_DOWN:
				if k := ev.KeyboardEvent(); k.Key == sdl.K_RETURN && k.Mod&sdl.KMOD_ALT != 0 {
					win.SetFullscreen(win.Flags()&sdl.WINDOW_FULLSCREEN == 0)
				}
			}
		}
		pw, ph, _ := r.CurrentOutputSize()
		v.setScale(float32(math.Max(0.5, math.Min(float64(ph)/1032, float64(pw)/900))))
		nowT := time.Now()
		dt := math.Min(0.1, nowT.Sub(last).Seconds())
		last = nowT
		v.frame++
		if v.frame%300 == 0 {
			v.sweep()
		}
		v.draw(float32(pw)/v.k, float32(ph)/v.k, nowT.Sub(start).Seconds(), dt)
	}
}
