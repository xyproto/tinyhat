#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
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
#define FRAME_MS 100
#define IDLE_TIMEOUT_MS 120000

typedef struct {
	unsigned char r, g, b;
} Rgb;

static const Rgb BG = { 0x1a, 0x1b, 0x26 };
static const Rgb TEAL = { 0x1a, 0x9e, 0x96 };
static const Rgb ARCH = { 0x17, 0x93, 0xd1 };
static const Rgb HAT = { 0x92, 0x24, 0x24 };
static const Rgb PINK = { 0xf7, 0x76, 0x8e };
static const Rgb GRAY = { 0x49, 0x49, 0x49 };
static const Rgb CYAN = { 0x7d, 0xcf, 0xff };
static const Rgb LIGHT = { 0xc0, 0xca, 0xf5 };
static const Rgb SLATE = { 0xa9, 0xb1, 0xd6 };

static const struct {
	int y0, y1;
	const Rgb *color, *ink;
	const char *label;
} blocks[] = {
	{ 23, 33, &HAT, &LIGHT, "11-386" },
	{ 34, 43, &GRAY, &LIGHT, "04-486" },
	{ 44, 56, &CYAN, &BG, "07-586" },
	{ 57, 64, &PINK, &BG, "09-686" },
};

static struct {
	int fd;
	unsigned char *mem;
	size_t len;
	unsigned line, offset;
	char id[16];
	struct fb_var_screeninfo var;
} fb = { -1, NULL, 0, 0, 0, "", { 0 } };

static unsigned char *back;
static int W, H, BPP;
static int dx0, dy0, dx1, dy1;
static char message[128];
static int progress = -1;
static long tick;
static volatile sig_atomic_t stop;

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

static Rgb lighter(Rgb c)
{
	Rgb o = { (unsigned char)(c.r + (255 - c.r) / 2), (unsigned char)(c.g + (255 - c.g) / 2),
		  (unsigned char)(c.b + (255 - c.b) / 2) };
	return o;
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

static int X(int p)
{
	return p * W / 100;
}

static int Y(int p)
{
	return p * H / 100;
}

static int sq(int v)
{
	return v * v;
}

static void pill(int x0, int y0, int x1, int y1, int left, Rgb c)
{
	int r = (y1 - y0) / 2, cy = y0 + r;
	uint32_t v = pack(c);
	clip(&x0, &y0, &x1, &y1);
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++) {
			if (left && x < x0 + r && sq(x - x0 - r) + sq(y - cy) > r * r)
				continue;
			if (x >= x1 - r && sq(x - x1 + r) + sq(y - cy) > r * r)
				continue;
			put(x, y, v);
		}
	dirty(x0, y0, x1, y1);
}

static void elbow(int top, Rgb c)
{
	int x0 = X(2), x1 = X(14), xend = X(58);
	int b0 = top ? Y(3) : Y(92), b1 = top ? Y(8) : Y(97), yend = top ? Y(22) : Y(65);
	int r = (W < H ? W : H) * 8 / 100, ri = (W < H ? W : H) * 3 / 100;
	int ya = top ? b0 : yend, yb = top ? yend : b1;
	uint32_t v = pack(c);
	for (int y = ya; y < yb; y++)
		for (int x = x0; x < xend; x++) {
			int vert = x < x1;
			int horiz = y >= b0 && y < b1;
			int cx = x0 + r, cy = top ? b0 + r : b1 - r;
			int fx = x1 + ri, fy = top ? b1 + ri : b0 - ri;
			int in;
			if (vert || horiz)
				in = !(x < cx && (top ? y < cy : y >= cy)) || sq(x - cx) + sq(y - cy) <= r * r;
			else
				in = x < fx && (top ? y >= b1 && y < fy : y >= fy && y < b0) && sq(x - fx) + sq(y - fy) >= ri * ri;
			if (in)
				put(x, y, v);
		}
	dirty(x0, ya, xend, yb);
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

static int small_scale(void)
{
	return H >= 900 ? 2 : 1;
}

static void draw_block(int i, int lit)
{
	int s = small_scale();
	const char *l = blocks[i].label;
	fill(X(2), Y(blocks[i].y0), X(14), Y(blocks[i].y1), lit ? lighter(*blocks[i].color) : *blocks[i].color);
	text(X(13) - text_width(l, s), Y(blocks[i].y1) - 18 * s, s, l, *blocks[i].ink);
}

static void draw_blinkers(int phase)
{
	fill(X(59), Y(3), X(64), Y(8), phase ? lighter(SLATE) : SLATE);
	fill(X(59), Y(92), X(61), Y(97), phase ? CYAN : lighter(CYAN));
}

static void draw_status(void)
{
	int s = small_scale(), x0 = X(18), x1 = X(58), y0 = Y(78), y1 = y0 + 12 * s;
	fill(x0, Y(70), X(96), y1 + 2, BG);
	if (message[0])
		text(x0, Y(70), s, message, LIGHT);
	if (progress >= 0) {
		int fx = x0 + (x1 - x0) * progress / 100;
		pill(x0, y0, x1, y1, 1, GRAY);
		if (fx > x0 + (y1 - y0))
			pill(x0, y0, fx, y1, 1, TEAL);
	} else if (message[0]) {
		int len = (x1 - x0) / 4, span = x1 - x0 - len;
		int pos = (int)(tick % 40), off = (pos < 20 ? pos : 40 - pos) * span / 20;
		pill(x0, y0, x1, y1, 1, GRAY);
		pill(x0 + off, y0, x0 + off + len, y1, 1, TEAL);
	}
}

static void draw_all(void)
{
	int ts = (Y(8) - Y(3)) / 16;
	fill(0, 0, W, H, BG);
	elbow(1, TEAL);
	elbow(0, ARCH);
	pill(X(92), Y(3), X(98), Y(8), 0, PINK);
	pill(X(91), Y(92), X(98), Y(97), 0, HAT);
	if (ts < 1)
		ts = 1;
	text(X(91) - text_width("TINY HAT", ts), Y(3) + (Y(8) - Y(3) - 16 * ts) / 2, ts, "TINY HAT", PINK);
	text(X(13) - text_width("BOOT-1", small_scale()), Y(22) - 18 * small_scale(), small_scale(), "BOOT-1", BG);
	text(X(13) - text_width("32-BIT", small_scale()), Y(65) + 2, small_scale(), "32-BIT", BG);
	for (int i = 0; i < (int)(sizeof(blocks) / sizeof(blocks[0])); i++)
		draw_block(i, 0);
	draw_blinkers(0);
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
		snprintf(message, sizeof(message), "%s", t);
		for (char *p = message; *p; p++)
			if (*p >= 'a' && *p <= 'z')
				*p = (char)(*p - 'a' + 'A');
	} else if (!strncmp(line, "progress", 8)) {
		progress = atoi(line + 8);
		if (progress > 100)
			progress = 100;
	} else if (!strcmp(line, "quit")) {
		stop = 1;
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
	unlink(FIFO);
	if (mkfifo(FIFO, 0600) || (ff = open(FIFO, O_RDWR | O_NONBLOCK | O_CLOEXEC)) < 0)
		return 1;
	if (!map()) {
		unlink(FIFO);
		return 1;
	}
	tty = open(TTY, O_RDWR | O_CLOEXEC);
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
		for (int i = 0; i < (int)(sizeof(blocks) / sizeof(blocks[0])); i++)
			draw_block(i, (tick / 3) % 4 == i);
		draw_blinkers((tick / 6) % 2);
		if (progress < 0 && message[0])
			draw_status();
		if (t - last_full >= 2000) {
			last_full = t;
			dirty(0, 0, W, H);
		}
		flush();
	}
	unlink(FIFO);
	if (tty >= 0) {
		ioctl(tty, KDSETMODE, KD_TEXT);
		close(tty);
	}
	unmap();
	return 0;
}
