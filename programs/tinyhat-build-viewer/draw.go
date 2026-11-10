package main

import (
	"fmt"
	"math"
	"slices"
	"strings"
	"sync/atomic"

	"github.com/Zyko0/go-sdl3/sdl"
	"github.com/Zyko0/go-sdl3/ttf"
)

type text struct {
	tex  *sdl.Texture
	w, h float32
	used int
}

type part struct {
	tex    *sdl.Texture
	w, h   float32
	ascent float32
}

type entry struct {
	it            item
	summary       string
	parts         []part
	width         float32
	passes, limit int
}

type viewer struct {
	r        *sdl.Renderer
	win      *sdl.Window
	fontPath string
	k        float32
	fonts    map[int]*ttf.Font
	cache    map[string]*text
	frame    int
	snap     atomic.Pointer[snapshot]
	news     *news
	ring     []*entry
	x0       float32
	seen     map[string]bool
	served   int
	angle    float64
}

func (v *viewer) font(size int) *ttf.Font {
	if f, ok := v.fonts[size]; ok {
		return f
	}
	f, err := ttf.OpenFont(v.fontPath, float32(size)*v.k)
	if err != nil {
		panic(err)
	}
	v.fonts[size] = f
	return f
}

func (v *viewer) setScale(k float32) {
	if math.Abs(float64(k-v.k)) < 0.01 {
		return
	}
	v.k = k
	for _, f := range v.fonts {
		f.Close()
	}
	v.fonts = map[int]*ttf.Font{}
	for _, t := range v.cache {
		t.tex.Destroy()
	}
	v.cache = map[string]*text{}
	for _, e := range v.ring {
		v.layout(e)
	}
}

func (v *viewer) render(s string, size int, c rgb) (*sdl.Texture, float32, float32) {
	if s == "" {
		return nil, 0, 0
	}
	surf, err := v.font(size).RenderTextBlended(s, sdl.Color{R: uint8(c.r * 255), G: uint8(c.g * 255), B: uint8(c.b * 255), A: 255})
	if err != nil {
		return nil, 0, 0
	}
	defer surf.Destroy()
	tex, err := v.r.CreateTextureFromSurface(surf)
	if err != nil {
		return nil, 0, 0
	}
	w, h, _ := tex.Size()
	return tex, w, h
}

func (v *viewer) text(x, y float32, s string, size int, c rgb) {
	key := fmt.Sprintf("%d|%v|%s", size, c, s)
	t, ok := v.cache[key]
	if !ok {
		tex, w, h := v.render(s, size, c)
		if tex == nil {
			return
		}
		t = &text{tex, w, h, 0}
		v.cache[key] = t
	}
	t.used = v.frame
	asc := float32(v.font(size).Ascent())
	v.r.RenderTexture(t.tex, nil, &sdl.FRect{X: x * v.k, Y: y*v.k - asc, W: t.w, H: t.h})
}

func (v *viewer) sweep() {
	for key, t := range v.cache {
		if v.frame-t.used > 600 {
			t.tex.Destroy()
			delete(v.cache, key)
		}
	}
}

func (v *viewer) fill(x, y, w, h float32, c rgb, a float32) {
	v.r.SetDrawColorFloat(c.r, c.g, c.b, a)
	v.r.RenderFillRect(&sdl.FRect{X: x * v.k, Y: y * v.k, W: w * v.k, H: h * v.k})
}

func (v *viewer) bar(x, y, w float32, frac float64, c rgb, label string) {
	v.fill(x, y, w, 18, rgb{0.2, 0.22, 0.28}, 1)
	v.fill(x, y, w*float32(math.Max(0, math.Min(1, frac))), 18, c, 1)
	fg := rgb{0.95, 0.95, 0.95}
	if frac > 0.5 {
		fg = rgb{0.05, 0.05, 0.05}
	}
	v.text(x+6, y+14, label, 12, fg)
}

func (v *viewer) graph(x, y, w, h float32, data []float64, c rgb, title string, scale float64) {
	v.fill(x, y, w, h, rgb{0.13, 0.14, 0.19}, 1)
	v.text(x+6, y+16, title, 12, c)
	if len(data) > 1 {
		pts := make([]sdl.FPoint, len(data))
		for i, d := range data {
			pts[i] = sdl.FPoint{X: (x + w*float32(i)/299) * v.k, Y: (y + h - 4 - (h-26)*float32(math.Min(1, d/scale))) * v.k}
		}
		v.r.SetDrawColorFloat(c.r, c.g, c.b, 1)
		v.r.RenderLines(pts)
	}
}

func chunks(s string, n int) []string {
	var out []string
	for len(s) > n {
		i := strings.LastIndexByte(s[:n], ' ')
		if i <= 0 {
			i = n
		}
		out = append(out, s[:i])
		s = s[i:]
	}
	return append(out, s)
}

func (v *viewer) layout(e *entry) {
	for _, p := range e.parts {
		p.tex.Destroy()
	}
	e.parts, e.width = nil, 0
	type span struct {
		s    string
		size int
		c    rgb
	}
	fc, ok := feedColors[e.it.Name]
	if !ok {
		fc = rgb{0.8, 0.8, 0.8}
	}
	spans := []span{{e.it.Name + ": ", 15, fc}, {e.it.Title, 15, rgb{0.94, 0.94, 0.94}}}
	if e.it.Extra != "" {
		spans = append(spans, span{"  (" + e.it.Extra + ")", 12, rgb{0.6, 0.62, 0.7}})
	}
	if e.summary != "" {
		for _, c := range chunks("   — "+e.summary, 90) {
			spans = append(spans, span{c, 14, rgb{0.78, 0.8, 0.86}})
		}
	}
	spans = append(spans, span{"     •     ", 15, rgb{0.4, 0.4, 0.45}})
	for _, sp := range spans {
		tex, w, h := v.render(sp.s, sp.size, sp.c)
		if tex == nil {
			continue
		}
		e.parts = append(e.parts, part{tex, w / v.k, h / v.k, float32(v.font(sp.size).Ascent()) / v.k})
		e.width += w / v.k
	}
}

func (v *viewer) newEntry(it item, passes, limit int) *entry {
	v.news.mu.Lock()
	summary := v.news.summaries[it.URL]
	v.news.mu.Unlock()
	e := &entry{it: it, summary: summary, passes: passes, limit: 3}
	if summary != "" {
		e.limit = 2
	}
	if limit > 0 && limit < e.limit {
		e.limit = limit
	}
	v.layout(e)
	return e
}

func (v *viewer) nextItem() item {
	v.news.mu.Lock()
	items := append([]item(nil), v.news.items...)
	digest := v.news.summaries["gemini:digest"]
	v.news.mu.Unlock()
	if len(items) == 0 {
		return item{Name: "news", Title: "loading headlines..."}
	}
	inRing := map[string]bool{}
	for _, e := range v.ring {
		inRing[e.it.URL] = true
	}
	v.served++
	if v.served%8 == 0 && digest != "" && !inRing["gemini:digest"] {
		return item{Name: "Gemini", Title: "digest of the news", Extra: "hot, 24h, week and month", URL: "gemini:digest"}
	}
	for range 2 {
		for _, it := range items {
			if !v.seen[it.URL] && !inRing[it.URL] {
				v.seen[it.URL] = true
				return it
			}
		}
		v.seen = map[string]bool{}
	}
	return items[0]
}

func (v *viewer) ticker(w float32, dt float32) {
	v.fill(0, 0, w, 32, rgb{0.12, 0.13, 0.18}, 1)
	v.news.mu.Lock()
	haveNews := len(v.news.items) > 0
	v.news.mu.Unlock()
	if haveNews && len(v.ring) > 0 && v.ring[0].it.Name == "news" {
		for _, e := range v.ring {
			for _, p := range e.parts {
				p.tex.Destroy()
			}
		}
		v.ring = nil
	}
	for len(v.ring) < 6 {
		v.ring = append(v.ring, v.newEntry(v.nextItem(), 0, 0))
	}
	v.x0 -= 85 * dt
	for v.x0+v.ring[0].width < 0 {
		e := v.ring[0]
		v.ring = v.ring[1:]
		v.x0 += e.width
		e.passes++
		v.news.mu.Lock()
		still := e.it.URL == "gemini:digest"
		if slices.Contains(v.news.items, e.it) {
			still = true
		}
		v.news.mu.Unlock()
		for _, p := range e.parts {
			p.tex.Destroy()
		}
		if e.passes < e.limit && still {
			v.ring = append(v.ring, v.newEntry(e.it, e.passes, e.limit))
		} else {
			v.ring = append(v.ring, v.newEntry(v.nextItem(), 0, 0))
		}
	}
	x := v.x0
	for _, e := range v.ring {
		for _, p := range e.parts {
			if x+p.w > 0 && x < w {
				v.r.RenderTexture(p.tex, nil, &sdl.FRect{X: x * v.k, Y: 22*v.k - p.ascent*v.k, W: p.w * v.k, H: p.h * v.k})
			}
			x += p.w
		}
		if x > w {
			break
		}
	}
}

func rpi4Line(snap *snapshot, i int) string {
	state := "not checked yet"
	for _, im := range snap.Images {
		if im.Name == "tinyhat-rpi4.img" {
			state = im.State
		}
	}
	left := snap.Total[i] - snap.Done[i]
	pkgs := fmt.Sprintf("all %d aarch64 packages up to date", snap.Total[i])
	if left > 0 {
		pkgs = fmt.Sprintf("waits for %d of %d aarch64 packages", left, snap.Total[i])
	}
	return fmt.Sprintf("rpi4: %s, plus the Raspberry Pi firmware and bootloader  |  tinyhat-rpi4.img %s", pkgs, state)
}

func archLine(a string, snap *snapshot, i int) string {
	todo := snap.Todo[i]
	if len(todo) == 0 {
		return fmt.Sprintf("%s: all %d packages up to date  (%d built this run)", a, snap.Total[i], snap.Run[i])
	}
	busy := map[string]bool{}
	for _, j := range snap.Jobs {
		if j.Arch == a {
			busy[j.Pkg] = true
		}
	}
	var now, next []string
	for _, p := range todo {
		if busy[p] {
			now = append(now, p)
		} else {
			next = append(next, p)
		}
	}
	line := fmt.Sprintf("%s: %d of %d to build, %d built this run", a, len(todo), snap.Total[i], snap.Run[i])
	if len(now) > 0 {
		line += "  |  building " + strings.Join(now, ", ")
	}
	if len(next) > 0 {
		shown := next
		if len(shown) > 6 {
			shown = shown[:6]
		}
		line += "  |  next: " + strings.Join(shown, ", ")
		if len(next) > len(shown) {
			line += fmt.Sprintf(" and %d more", len(next)-len(shown))
		}
	}
	return line
}

func (v *viewer) hat(ox, oy, size float32, snap *snapshot, t, dt float64) {
	load := 0.0
	if len(snap.CPU) > 0 {
		load = snap.CPU[len(snap.CPU)-1]
	}
	v.angle += dt * (0.4 + 2.2*load)
	p := snap.Progress
	band := rgb{float32(1 - p), float32(0.35 + 0.6*p), 0.3}
	gold := rgb{0.92, 0.75, 0.3}
	cx, cy, s := float64(ox+size/2), float64(oy+size*0.47), float64(size)*0.25
	tilt := 0.45 + 0.12*math.Sin(t*0.7)
	ca, sa, ct, st := math.Cos(v.angle), math.Sin(v.angle), math.Cos(tilt), math.Sin(tilt)
	type pt struct{ x, y, z float64 }
	proj := func(x, y, z float64) pt {
		x, z = x*ca+z*sa, -x*sa+z*ca
		y, z = y*ct-z*st, y*st+z*ct
		f := 3.2 / (3.2 + z)
		return pt{cx + x*s*f, cy + y*s*f, z}
	}
	ring := func(r, y float64) []pt {
		out := make([]pt, 40)
		for i := range out {
			a := 2 * math.Pi * float64(i) / 40
			out[i] = proj(r*math.Cos(a), y, r*math.Sin(a))
		}
		return out
	}
	k := v.k
	line := func(a, b pt, c rgb, alpha float64, width int) {
		v.r.SetDrawColorFloat(c.r, c.g, c.b, float32(alpha))
		for o := range width {
			off := float32(o) - float32(width-1)/2
			v.r.RenderLine(float32(a.x)*k, float32(a.y)*k+off, float32(b.x)*k, float32(b.y)*k+off)
		}
	}
	strokeRing := func(pts []pt, c rgb, width int) {
		for i := range pts {
			a, b := pts[i], pts[(i+1)%len(pts)]
			line(a, b, c, 0.35+0.65*(0.5-(a.z+b.z)/4.8), width)
		}
	}
	brimOuter, brimInner := ring(1.15, 0.55), ring(0.62, 0.55)
	bottom, bandTop, crown := ring(0.6, 0.5), ring(0.6, 0.3), ring(0.55, -0.75)
	for i := 0; i < 40; i += 4 {
		line(bottom[i], crown[i], gold, 0.25+0.5*(0.5-(bottom[i].z+crown[i].z)/4.8), 1)
		line(brimInner[i], brimOuter[i], gold, 0.25+0.5*(0.5-(brimInner[i].z+brimOuter[i].z)/4.8), 1)
	}
	strokeRing(brimOuter, gold, 2)
	strokeRing(brimInner, gold, 1)
	strokeRing(bottom, band, 3)
	strokeRing(bandTop, band, 3)
	strokeRing(crown, gold, 2)
	var colors []rgb
	for i, j := range snap.Jobs {
		if i == 12 {
			break
		}
		c := rgb{0.9, 0.9, 0.9}
		for n, a := range pkgArches {
			if a == j.Arch {
				c = pkgColors[n]
			}
		}
		colors = append(colors, c)
	}
	for _, kr := range snap.Kernels {
		if kr.Running {
			colors = append(colors, kernelColor)
		}
	}
	for i, c := range colors {
		a := t*(0.9+0.13*float64(i)) + float64(i)*2.1
		q := proj(1.3*math.Cos(a), 0.15*math.Sin(a*1.7)-0.1, 1.3*math.Sin(a))
		r := float32(2.5+2.5*(0.5-q.z/2.4)) * k
		fc := sdl.FColor{R: c.r, G: c.g, B: c.b, A: 0.9}
		verts := []sdl.Vertex{{Position: sdl.FPoint{X: float32(q.x) * k, Y: float32(q.y) * k}, Color: fc}}
		var idx []int32
		for n := 0; n <= 12; n++ {
			ang := 2 * math.Pi * float64(n) / 12
			verts = append(verts, sdl.Vertex{Position: sdl.FPoint{X: float32(q.x)*k + r*float32(math.Cos(ang)), Y: float32(q.y)*k + r*float32(math.Sin(ang))}, Color: fc})
			if n > 0 {
				idx = append(idx, 0, int32(n), int32(n+1))
			}
		}
		v.r.RenderGeometry(nil, verts, idx)
	}
	label := fmt.Sprintf("%.0f%% up to date", 100*p)
	v.text(ox+size/2-float32(len(label))*3.6, oy+size-10, label, 12, rgb{0.85, 0.85, 0.9})
}

func (v *viewer) draw(w, h float32, t, dt float64) {
	snap := v.snap.Load()
	v.r.SetDrawColorFloat(0.08, 0.09, 0.12, 1)
	v.r.Clear()
	v.ticker(w, float32(dt))
	oy := float32(32)
	el := 0.0
	if snap.Start > 0 {
		el = now() - snap.Start
	}
	status, sc := "idle", rgb{0.7, 0.9, 0.7}
	if snap.Failed {
		status, sc = "BUILD STOPPED AT AN ERROR", rgb{1, 0.35, 0.35}
	} else if snap.Start > 0 {
		status = "building"
	} else {
		for _, st := range snap.Steps {
			if strings.HasPrefix(st, "QEMU test") {
				status = "testing in QEMU"
			}
		}
	}
	v.text(20, oy+34, "Tiny Hat build", 24, accent)
	v.text(260, oy+34, fmt.Sprintf("%s   elapsed %d:%02d:%02d", status, int(el)/3600, int(el)%3600/60, int(el)%60), 16, sc)
	built, left := 0, 0
	for i := range pkgArches {
		built += snap.Run[i]
		left += snap.Total[i] - snap.Done[i]
	}
	line := fmt.Sprintf("packages built this run: %d", built)
	if el > 60 && built >= 5 {
		rate := float64(built) / (el / 3600)
		line += fmt.Sprintf("   (%.0f/h)", rate)
		if left > 0 {
			line += fmt.Sprintf("   about %.1f h left at this pace", float64(left)/rate)
		}
	}
	y := oy + 70
	v.text(20, y, line, 14, white)
	y += 16
	for i, a := range pkgArches {
		if snap.Total[i] == 0 {
			if a == "x32" && snap.X32Image[1] > 0 {
				y += 26
				v.bar(20, y, w-40, 0, pkgColors[i], fmt.Sprintf("x32: no x32 packages built yet, the x32 image uses %d i686 packages", snap.X32Image[1]))
			}
			continue
		}
		y += 26
		label := archLine(a, snap, i)
		if a == "x32" {
			label += fmt.Sprintf("  |  x32 image: %d x32 + %d i686 packages", snap.X32Image[0], snap.X32Image[1])
		}
		v.bar(20, y, w-40, float64(snap.Done[i])/float64(snap.Total[i]), pkgColors[i], label)
		if a == "aarch64" {
			y += 26
			v.bar(20, y, w-40, float64(snap.Done[i])/float64(snap.Total[i]), rpi4Color, rpi4Line(snap, i))
		}
	}
	for i, a := range arches {
		y += 26
		kr := snap.Kernels[i]
		switch a {
		case "x86_64":
			a = "x86_64 and x32"
		case "aarch64":
			a = "aarch64 and rpi4"
		}
		switch {
		case kr.Running && kr.Total >= kr.Objects && kr.Objects > 0:
			v.bar(20, y, w-40, float64(kr.Objects)/float64(kr.Total), kernelColor,
				fmt.Sprintf("%s kernel  %d/%d objects  %d min", a, kr.Objects, kr.Total, int(kr.Secs)/60))
		case kr.Running:
			v.bar(20, y, w-40, 0, kernelColor, fmt.Sprintf("%s kernel  %d objects so far (no total yet)  %d min", a, kr.Objects, int(kr.Secs)/60))
		case kr.Built != "":
			v.bar(20, y, w-40, 1, rgb{0.45, 0.3, 0.5}, fmt.Sprintf("%s kernel  built %s", a, kr.Built))
		default:
			v.bar(20, y, w-40, 0, kernelColor, a+" kernel  not built yet")
		}
	}
	y += 44
	v.text(20, y, "running now", 15, accent)
	for i, st := range snap.Steps {
		if i == 4 {
			break
		}
		y += 22
		v.text(30, y, st, 13, rgb{0.95, 0.85, 0.5})
	}
	for i, j := range snap.Jobs {
		if i == 8 {
			break
		}
		y += 22
		c := rgb{0.9, 0.9, 0.9}
		for n, a := range pkgArches {
			if a == j.Arch {
				c = pkgColors[n]
			}
		}
		v.text(30, y, fmt.Sprintf("%-8s %-28s %3d min %02d s", j.Arch, j.Pkg, int(j.Secs)/60, int(j.Secs)%60), 13, c)
		v.fill(470, y-11, float32(math.Min(float64(w-490), j.Secs/6)), 10, c, 1)
	}
	if len(snap.Jobs) == 0 && len(snap.Steps) == 0 {
		y += 22
		v.text(30, y, "nothing", 13, rgb{0.6, 0.6, 0.6})
	}
	y += 30
	gw := (w - 60) / 3
	last := func(d []float64) float64 {
		if len(d) == 0 {
			return 0
		}
		return d[len(d)-1]
	}
	v.graph(20, y, gw, 110, snap.CPU, rgb{1, 0.5, 0.4}, fmt.Sprintf("CPU %.0f%%", 100*last(snap.CPU)), 1)
	v.graph(30+gw, y, gw, 110, snap.Mem, rgb{0.4, 0.8, 1}, fmt.Sprintf("memory %.0f%%", 100*last(snap.Mem)), 1)
	cores := float64(numCPU)
	topLoad := 2 * cores
	for _, l := range snap.Load {
		topLoad = math.Max(topLoad, l*1.1)
	}
	v.graph(40+2*gw, y, gw, 110, snap.Load, rgb{0.6, 0.95, 0.5}, fmt.Sprintf("load %.1f on %d cores", last(snap.Load), numCPU), topLoad)
	ly := y + 110 - 4 - 84*float32(cores/topLoad)
	v.r.SetDrawColorFloat(0.9, 0.9, 0.9, 0.35)
	for x := 40 + 2*gw; x < 40+3*gw; x += 8 {
		v.r.RenderLine(x*v.k, ly*v.k, (x+4)*v.k, ly*v.k)
	}
	y += 134
	v.text(20, y, snap.Info, 13, rgb{0.8, 0.8, 0.6})
	y += 30
	v.text(20, y, "images", 15, accent)
	for _, im := range snap.Images {
		y += 20
		c, ok := map[string]rgb{"up to date": {0.5, 0.95, 0.5}, "outdated": {0.85, 0.65, 0.4}, "missing": {0.5, 0.5, 0.5},
			"writing now": {0.95, 0.85, 0.5}}[im.State]
		if !ok {
			c = rgb{0.6, 0.6, 0.6}
		}
		v.text(30, y, fmt.Sprintf("%-22s %-12s %s", im.Name, im.State, im.When), 13, c)
	}
	y += 30
	v.text(20, y, "log", 15, accent)
	if snap.LogName != "" {
		v.text(60, y, snap.LogName, 12, rgb{0.6, 0.6, 0.7})
	}
	cw := float32(6.6)
	rows := int((h - y - 12) / 16)
	logs := snap.Logs
	if rows < 3 {
		rows = 3
	}
	if len(logs) > rows {
		logs = logs[len(logs)-rows:]
	}
	for _, l := range logs {
		y += 16
		room := w - 40
		if y > h-300 {
			room -= 300
		}
		if n := int(room / cw); len(l) > n && n > 10 {
			l = l[:n]
		}
		c := rgb{0.65, 0.65, 0.7}
		if strings.Contains(l, "rror") || strings.Contains(l, "***") {
			c = rgb{1, 0.5, 0.5}
		}
		v.text(30, y, l, 11, c)
	}
	v.hat(w-280, h-280, 280, snap, t, dt)
	v.r.Present()
}
