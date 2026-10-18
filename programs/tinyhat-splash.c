#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "font.h"

#define FIFO "/run/tinyhat-splash"
#define FBDEV "/dev/fb0"
#define TTY "/dev/tty1"
#define FRAME_MS 80
#define IDLE_TIMEOUT_MS 120000
#define LOW 64
#define MAXSTARS 160
#define SEGMENTS 16

typedef struct {
	unsigned char r, g, b;
} Rgb;

static const Rgb BG = { 0x1a, 0x1b, 0x26 };
static const Rgb TEAL = { 0x1a, 0x9e, 0x96 };
static const Rgb HAT = { 0x3d, 0x7a, 0x2a };
static const Rgb GREEN = { 0x9e, 0xce, 0x6a };
static const Rgb GRAY = { 0x49, 0x49, 0x49 };
static const Rgb LIGHT = { 0xc0, 0xca, 0xf5 };
static const Rgb CYAN = { 0x7d, 0xcf, 0xff };
static const Rgb WHITE = { 0xff, 0xff, 0xff };

static const Rgb *const ink[] = { &BG, &HAT, &GREEN, &GRAY, &LIGHT, &CYAN, &TEAL };
static const Rgb STAR[] = { { 0x2f, 0x34, 0x4b }, { 0x56, 0x5f, 0x89 }, { 0xa9, 0xb1, 0xd6 }, { 0xff, 0xff, 0xff } };

static struct {
	int fd;
	unsigned char *mem;
	size_t len;
	unsigned line, offset;
	char id[16];
	struct fb_var_screeninfo var;
} fb = { -1, NULL, 0, 0, 0, "", { 0 } };

static unsigned char *back;
static unsigned char low[LOW * LOW];
static int W, H, BPP;
static int dx0, dy0, dx1, dy1;
static char message[128];
static int progress = -1;
static long tick;
static volatile sig_atomic_t stop;
static struct {
	int x, y, base, period, phase, shade;
} stars[MAXSTARS];
static int nstars, starsize;
static int handoff;

static long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static uint32_t pack(Rgb c)
{
	const struct fb_var_screeninfo *v = &fb.var;
	return ((uint32_t)(c.r >> (8 - v->red.length)) << v->red.offset) |
	       ((uint32_t)(c.g >> (8 - v->green.length)) << v->green.offset) |
	       ((uint32_t)(c.b >> (8 - v->blue.length)) << v->blue.offset);
}

static void dirty(int x0, int y0, int x1, int y1)
{
	if (dx1 <= dx0 || dy1 <= dy0) {
		dx0 = x0, dy0 = y0, dx1 = x1, dy1 = y1;
		return;
	}
	if (x0 < dx0)
		dx0 = x0;
	if (y0 < dy0)
		dy0 = y0;
	if (x1 > dx1)
		dx1 = x1;
	if (y1 > dy1)
		dy1 = y1;
}

static void put(int x, int y, uint32_t c)
{
	unsigned char *p = back + ((size_t)y * W + x) * BPP;
	if (BPP == 4)
		memcpy(p, &c, 4);
	else if (BPP == 2) {
		uint16_t s = (uint16_t)c;
		memcpy(p, &s, 2);
	} else {
		p[0] = (unsigned char)c;
		p[1] = (unsigned char)(c >> 8);
		p[2] = (unsigned char)(c >> 16);
	}
}

static void clip(int *x0, int *y0, int *x1, int *y1)
{
	if (*x0 < 0)
		*x0 = 0;
	if (*y0 < 0)
		*y0 = 0;
	if (*x1 > W)
		*x1 = W;
	if (*y1 > H)
		*y1 = H;
}

static void fill(int x0, int y0, int x1, int y1, Rgb c)
{
	uint32_t v = pack(c);
	clip(&x0, &y0, &x1, &y1);
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++)
			put(x, y, v);
	dirty(x0, y0, x1, y1);
}

static int sq(int v)
{
	return v * v;
}

static void pill(int x0, int y0, int x1, int y1, Rgb c)
{
	int r = (y1 - y0) / 2, cy = y0 + r;
	uint32_t v = pack(c);
	clip(&x0, &y0, &x1, &y1);
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++) {
			if (x < x0 + r && sq(x - x0 - r) + sq(y - cy) > r * r)
				continue;
			if (x >= x1 - r && sq(x - x1 + r) + sq(y - cy) > r * r)
				continue;
			put(x, y, v);
		}
	dirty(x0, y0, x1, y1);
}

static int text_width(const char *s, int scale)
{
	return (int)strlen(s) * 8 * scale;
}

static void text(int x, int y, int scale, const char *s, Rgb c)
{
	uint32_t v = pack(c);
	int x0 = x;
	for (; *s; s++, x += 8 * scale) {
		const unsigned char *g = font8x16 + (unsigned char)*s * 16;
		for (int gy = 0; gy < 16; gy++)
			for (int gx = 0; gx < 8; gx++)
				if (g[gy] & (0x80 >> gx))
					for (int sy = 0; sy < scale; sy++)
						for (int sx = 0; sx < scale; sx++) {
							int px = x + gx * scale + sx, py = y + gy * scale + sy;
							if (px >= 0 && py >= 0 && px < W && py < H)
								put(px, py, v);
						}
	}
	dirty(x0, y, x, y + 16 * scale);
}

static int text_scale(void)
{
	return H >= 900 ? 2 : 1;
}

static int hat_scale(void)
{
	int s = (W < H ? W : H) / 240;
	return s < 2 ? 2 : s;
}

static int hat_x(void)
{
	return W / 2 - LOW * hat_scale() / 2;
}

static int hat_y(void)
{
	return H * 2 / 5 - LOW * hat_scale() / 2;
}

static void line(int x0, int y0, int x1, int y1, unsigned char c)
{
	int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, e = dx + dy;
	for (;;) {
		if (x0 >= 0 && y0 >= 0 && x0 < LOW && y0 < LOW)
			low[y0 * LOW + x0] = c;
		if (x0 == x1 && y0 == y1)
			break;
		int e2 = 2 * e;
		if (e2 >= dy)
			e += dy, x0 += sx;
		if (e2 <= dx)
			e += dx, y0 += sy;
	}
}

typedef struct {
	int x, y;
	double z;
} Point;

static double tilt = 0.38;

static Point project(double angle, double x, double y, double z)
{
	double x1 = x * cos(angle) + z * sin(angle), z1 = -x * sin(angle) + z * cos(angle);
	double y1 = y - 0.45;
	double y2 = y1 * cos(tilt) - z1 * sin(tilt), z2 = y1 * sin(tilt) + z1 * cos(tilt);
	double f = 3.4 / (z2 + 3.4);
	Point p = { (int)lround(LOW / 2 + x1 * f * 25), (int)lround(LOW / 2 - y2 * f * 25), z2 };
	return p;
}

static void edge(int pass, Point a, Point b, unsigned char front, unsigned char rear)
{
	int is_front = a.z + b.z < 0;
	if (is_front == pass)
		line(a.x, a.y, b.x, b.y, is_front ? front : rear);
}

static void ring(int pass, double angle, double y, double r, unsigned char front, unsigned char rear)
{
	for (int i = 0; i < SEGMENTS; i++) {
		double a0 = 2 * M_PI * i / SEGMENTS, a1 = 2 * M_PI * (i + 1) / SEGMENTS;
		edge(pass, project(angle, r * cos(a0), y, r * sin(a0)), project(angle, r * cos(a1), y, r * sin(a1)), front,
		     rear);
	}
}

static Point at(double angle, double r, double y, double a, double t)
{
	return project(angle, r * cos(a) - t * sin(a), y, r * sin(a) + t * cos(a));
}

static Point quill(double angle, double t, double spread)
{
	double w = spread * sin(M_PI * t);
	return at(angle, 0.58 + 0.45 * t - 0.15 * t * t + (w != 0 ? 0.04 : 0), 0.2 + 0.95 * t - (w != 0 ? 0.08 : 0),
		  -0.45 - 0.35 * t, w);
}

static void hat(double angle)
{
	memset(low, 0, sizeof(low));
	tilt = 0.36 + 0.1 * sin(2 * angle);
	for (int pass = 0; pass < 2; pass++) {
		for (int side = -1; side <= 1; side += 2) {
			Point knot = at(angle, 0.56, 0.19, 0, 0);
			Point top = at(angle, 0.6, 0.27, 0, side * 0.15), bottom = at(angle, 0.6, 0.11, 0, side * 0.15);
			edge(pass, knot, top, 4, 3);
			edge(pass, top, bottom, 4, 3);
			edge(pass, bottom, knot, 4, 3);
		}
		ring(pass, angle, 0, 1.0, 2, 1);
		ring(pass, angle, 0, 0.55, 2, 1);
		ring(pass, angle, 0.12, 0.555, 4, 3);
		ring(pass, angle, 0.26, 0.56, 4, 3);
		ring(pass, angle, 0.95, 0.6, 2, 1);
		for (int i = 0; i < SEGMENTS; i += 2) {
			double a = 2 * M_PI * i / SEGMENTS;
			edge(pass, project(angle, 0.55 * cos(a), 0, 0.55 * sin(a)),
			     project(angle, 0.6 * cos(a), 0.95, 0.6 * sin(a)), 2, 1);
			edge(pass, project(angle, 0.55 * cos(a), 0, 0.55 * sin(a)), project(angle, cos(a), 0, sin(a)), 2, 1);
		}
		for (int k = 0; k < 6; k++)
			edge(pass, quill(angle, k / 6.0, 0), quill(angle, (k + 1) / 6.0, 0), 5, 6);
		for (int k = 1; k < 6; k++)
			for (int side = -1; side <= 1; side += 2)
				edge(pass, quill(angle, k / 6.0, 0), quill(angle, k / 6.0, side * 0.17), 5, 6);
	}
}

static void draw_hat(void)
{
	int s = hat_scale(), x0 = hat_x(), y0 = hat_y();
	uint32_t c[sizeof(ink) / sizeof(ink[0])];
	for (size_t i = 0; i < sizeof(ink) / sizeof(ink[0]); i++)
		c[i] = pack(*ink[i]);
	hat(2 * M_PI * (double)(tick % 75) / 75);
	for (int y = 0; y < LOW * s; y++)
		for (int x = 0; x < LOW * s; x++) {
			int px = x0 + x, py = y0 + y;
			if (px >= 0 && py >= 0 && px < W && py < H)
				put(px, py, c[low[(y / s) * LOW + x / s]]);
		}
	dirty(x0, y0, x0 + LOW * s, y0 + LOW * s);
}

static int title_y(void)
{
	return hat_y() + LOW * hat_scale() + 4 * text_scale();
}

static void draw_title(void)
{
	int s = 2 * text_scale();
	text((W - text_width("TINY HAT", s)) / 2, title_y(), s, "TINY HAT", WHITE);
}

static void draw_status(void)
{
	int s = text_scale(), top = title_y() + 32 * s + 24 * s;
	int x0 = W * 3 / 10, x1 = W * 7 / 10, by = top + 40 * s;
	fill(0, top, W, by + 12 * s + 2, BG);
	if (message[0])
		text((W - text_width(message, s)) / 2, top, s, message, LIGHT);
	if (progress >= 0) {
		int fx = x0 + (x1 - x0) * progress / 100;
		pill(x0, by, x1, by + 12 * s, GRAY);
		if (fx > x0 + 12 * s)
			pill(x0, by, fx, by + 12 * s, TEAL);
	} else if (message[0]) {
		int len = (x1 - x0) / 4, span = x1 - x0 - len;
		int pos = (int)(tick % 40), off = (pos < 20 ? pos : 40 - pos) * span / 20;
		pill(x0, by, x1, by + 12 * s, GRAY);
		pill(x0 + off, by, x0 + off + len, by + 12 * s, TEAL);
	}
}

static void blit(int x0, int y0, int x1, int y1)
{
	if (!fb.mem)
		return;
	clip(&x0, &y0, &x1, &y1);
	for (int y = y0; y < y1; y++)
		memcpy(fb.mem + fb.offset + (size_t)y * fb.line + (size_t)x0 * BPP, back + ((size_t)y * W + x0) * BPP,
		       (size_t)(x1 - x0) * BPP);
}

static int star_ok(int x, int y)
{
	int m = 4 * starsize + 8, s = hat_scale(), ts = text_scale();
	int bottom = title_y() + 32 * ts + 24 * ts + 40 * ts + 12 * ts + 2;
	if (x >= hat_x() - m && x < hat_x() + LOW * s + m && y >= hat_y() - m && y < hat_y() + LOW * s + m)
		return 0;
	return !(x >= W / 8 - m && x < W * 7 / 8 + m && y >= title_y() - m && y < bottom + m);
}

static void setup_stars(void)
{
	uint32_t r = 0x7417a700;
	int want = W / 30 * (H / 300);
	starsize = H >= 900 ? 2 : 1;
	nstars = 0;
	if (want > MAXSTARS)
		want = MAXSTARS;
	for (int tries = 0; nstars < want && tries < want * 20; tries++) {
		int x, y;
		r ^= r << 13, r ^= r >> 17, r ^= r << 5;
		x = (int)(r % (uint32_t)W);
		r ^= r << 13, r ^= r >> 17, r ^= r << 5;
		y = (int)(r % (uint32_t)H);
		if (!star_ok(x, y))
			continue;
		r ^= r << 13, r ^= r >> 17, r ^= r << 5;
		stars[nstars].x = x;
		stars[nstars].y = y;
		stars[nstars].base = (int)(r % 2);
		stars[nstars].period = 20 + (int)((r >> 8) % 41);
		stars[nstars].phase = (int)((r >> 16) % 60);
		stars[nstars].shade = -1;
		nstars++;
	}
}

static void draw_stars(int force)
{
	for (int i = 0; i < nstars; i++) {
		int t = (int)((tick + stars[i].phase) % stars[i].period), shade = stars[i].base;
		int x0 = stars[i].x, y0 = stars[i].y, x1 = x0 + starsize, y1 = y0 + starsize;
		uint32_t c;
		if (t < 2)
			shade = 3;
		else if (t < 4)
			shade = stars[i].base + 1;
		if (!force && shade == stars[i].shade)
			continue;
		stars[i].shade = shade;
		c = pack(STAR[shade]);
		clip(&x0, &y0, &x1, &y1);
		for (int y = y0; y < y1; y++)
			for (int x = x0; x < x1; x++)
				put(x, y, c);
		if (!force)
			blit(x0, y0, x1, y1);
	}
}

static void draw_all(void)
{
	fill(0, 0, W, H, BG);
	setup_stars();
	draw_stars(1);
	draw_hat();
	draw_title();
	draw_status();
}

static void flush(void)
{
	if (!fb.mem || dx1 <= dx0 || dy1 <= dy0)
		return;
	for (int y = dy0; y < dy1; y++)
		memcpy(fb.mem + fb.offset + (size_t)y * fb.line + (size_t)dx0 * BPP, back + ((size_t)y * W + dx0) * BPP,
		       (size_t)(dx1 - dx0) * BPP);
	dx0 = dy0 = dx1 = dy1 = 0;
}

static void unmap(void)
{
	if (fb.mem)
		munmap(fb.mem, fb.len);
	if (fb.fd >= 0)
		close(fb.fd);
	fb.mem = NULL;
	fb.fd = -1;
}

static int map(void)
{
	struct fb_fix_screeninfo fix;
	int fd = open(FBDEV, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return 0;
	if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) || ioctl(fd, FBIOGET_VSCREENINFO, &fb.var) ||
	    (fb.var.bits_per_pixel != 16 && fb.var.bits_per_pixel != 24 && fb.var.bits_per_pixel != 32)) {
		close(fd);
		return 0;
	}
	unmap();
	fb.fd = fd;
	fb.len = fix.smem_len;
	fb.line = fix.line_length;
	memcpy(fb.id, fix.id, sizeof(fb.id));
	fb.mem = mmap(NULL, fb.len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (fb.mem == MAP_FAILED) {
		fb.mem = NULL;
		unmap();
		return 0;
	}
	BPP = (int)fb.var.bits_per_pixel / 8;
	fb.offset = fb.var.yoffset * fb.line + fb.var.xoffset * (unsigned)BPP;
	if ((int)fb.var.xres != W || (int)fb.var.yres != H || !back) {
		W = (int)fb.var.xres;
		H = (int)fb.var.yres;
		free(back);
		back = malloc((size_t)W * H * 4);
		if (!back)
			return 0;
	}
	draw_all();
	return 1;
}

static int fb_changed(void)
{
	struct fb_fix_screeninfo fix;
	struct fb_var_screeninfo var;
	int changed = 0, fd = open(FBDEV, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return 0;
	if (!ioctl(fd, FBIOGET_FSCREENINFO, &fix) && !ioctl(fd, FBIOGET_VSCREENINFO, &var))
		changed = memcmp(fix.id, fb.id, sizeof(fb.id)) || var.xres != fb.var.xres || var.yres != fb.var.yres ||
			  var.bits_per_pixel != fb.var.bits_per_pixel;
	close(fd);
	return changed;
}

static void command(char *line)
{
	if (!strncmp(line, "msg", 3)) {
		const char *t = line[3] == ' ' ? line + 4 : "";
		size_t n = strlen(t);
		if (n >= sizeof(message))
			n = sizeof(message) - 1;
		memcpy(message, t, n);
		message[n] = '\0';
		for (char *p = message; *p; p++)
			if (*p >= 'a' && *p <= 'z')
				*p = (char)(*p - 'a' + 'A');
	} else if (!strncmp(line, "progress", 8)) {
		progress = atoi(line + 8);
		if (progress > 100)
			progress = 100;
	} else if (!strcmp(line, "quit")) {
		stop = 1;
	} else if (!strcmp(line, "handoff")) {
		stop = 1;
		handoff = 1;
	}
	draw_status();
}

static void on_signal(int sig)
{
	(void)sig;
	stop = 1;
}

int main(void)
{
	char buf[512];
	size_t have = 0;
	long last_cmd, last_check = 0, last_full = 0;
	int ff, tty;
	if (fork() > 0)
		return 0;
	setsid();
	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, SIG_IGN);
	unlink(FIFO);
	if (mkfifo(FIFO, 0600) || (ff = open(FIFO, O_RDWR | O_NONBLOCK | O_CLOEXEC)) < 0)
		return 1;
	if (!map()) {
		unlink(FIFO);
		return 1;
	}
	tty = open(TTY, O_RDWR | O_NOCTTY | O_CLOEXEC);
	if (tty >= 0)
		ioctl(tty, KDSETMODE, KD_GRAPHICS);
	flush();
	last_cmd = now_ms();
	while (!stop) {
		struct pollfd p = { ff, POLLIN, 0 };
		long t;
		if (poll(&p, 1, FRAME_MS) > 0 && (p.revents & POLLIN)) {
			ssize_t n = read(ff, buf + have, sizeof(buf) - 1 - have);
			if (n > 0) {
				char *nl;
				have += (size_t)n;
				buf[have] = '\0';
				while ((nl = strchr(buf, '\n'))) {
					*nl = '\0';
					command(buf);
					have -= (size_t)(nl + 1 - buf);
					memmove(buf, nl + 1, have + 1);
				}
				if (have == sizeof(buf) - 1)
					have = 0;
				last_cmd = now_ms();
			}
		}
		if (stop)
			break;
		t = now_ms();
		tick = t / FRAME_MS;
		if (t - last_cmd > IDLE_TIMEOUT_MS)
			break;
		if (t - last_check >= 500) {
			last_check = t;
			if (fb_changed())
				map();
		}
		draw_hat();
		draw_stars(0);
		if (progress < 0 && message[0])
			draw_status();
		if (tty >= 0) {
			int mode;
			if (!ioctl(tty, KDGETMODE, &mode) && mode != KD_GRAPHICS) {
				ioctl(tty, KDSETMODE, KD_GRAPHICS);
				dirty(0, 0, W, H);
			}
		}
		if (t - last_full >= 2000) {
			last_full = t;
			dirty(0, 0, W, H);
		}
		flush();
	}
	unlink(FIFO);
	if (tty >= 0) {
		if (!handoff)
			ioctl(tty, KDSETMODE, KD_TEXT);
		close(tty);
	}
	unmap();
	return 0;
}
