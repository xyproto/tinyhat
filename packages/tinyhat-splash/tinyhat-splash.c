#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
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
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "font.h"
#include "hat.h"

#define FIFO "/run/tinyhat-splash"
#define HAT_ACK "/run/tinyhat-splash.hat"
#define FBDEV "/dev/fb0"
#define TTY "/dev/tty1"
#define FRAME_MS 80
#define IDLE_TIMEOUT_MS 120000
#define HAT_FIRST_MS 3000
#define HAT_FRAME_MS 500
#define MAXSTARS 160

typedef struct {
	unsigned char r, g, b;
} Rgb;

static const Rgb BG = { 0x1a, 0x1b, 0x26 };
static const Rgb TEAL = { 0x1a, 0x9e, 0x96 };
#define RGB(c) { ((c) >> 16) & 0xff, ((c) >> 8) & 0xff, (c) & 0xff }
static const Rgb GRAY = { 0x49, 0x49, 0x49 };
static const Rgb LIGHT = { 0xc0, 0xca, 0xf5 };
static const Rgb WHITE = { 0xff, 0xff, 0xff };

static const uint32_t ink[INKS] = INK_COLORS;
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
static struct {
	pid_t pid;
	int in, out, shown;
	size_t have;
	long deadline;
	unsigned char frame[LOW * LOW];
} remote = { -1, -1, -1, 0, 0, 0, { 0 } };

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

static void hat_stop(void)
{
	if (remote.pid > 0) {
		kill(remote.pid, SIGKILL);
		waitpid(remote.pid, NULL, 0);
	}
	if (remote.in >= 0)
		close(remote.in);
	if (remote.out >= 0)
		close(remote.out);
	remote.pid = remote.in = remote.out = -1;
}

static void hat_child(int dir, int exe, int in, int out, int null)
{
	static char *const argv[] = { "hat", NULL }, *const envp[] = { "PATH=/usr/bin", NULL };
	struct rlimit mem = { 64 << 20, 64 << 20 }, none = { 0, 0 };
	signal(SIGPIPE, SIG_DFL);
	signal(SIGHUP, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	signal(SIGINT, SIG_DFL);
	if (dup2(in, 0) < 0 || dup2(out, 1) < 0 || dup2(null, 2) < 0 || fchdir(dir) || chroot(".") || chdir("/") ||
	    setgroups(0, NULL) || setgid(65534) || setuid(65534) || prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) ||
	    setrlimit(RLIMIT_AS, &mem) || setrlimit(RLIMIT_FSIZE, &none) || setrlimit(RLIMIT_CORE, &none))
		_exit(127);
	setpriority(PRIO_PROCESS, 0, 10);
	fexecve(exe, argv, envp);
	_exit(127);
}

static int hat_send(void)
{
	int32_t t = (int32_t)tick;
	remote.have = 0;
	return write(remote.in, &t, sizeof(t)) == sizeof(t);
}

static void hat_start(char *args)
{
	char *path = strchr(args, ' ');
	int dir = -1, exe = -1, null = -1, in[2] = { -1, -1 }, out[2] = { -1, -1 };
	if (remote.pid > 0 || !path)
		return;
	*path++ = '\0';
	if ((dir = open(args, O_RDONLY | O_DIRECTORY | O_CLOEXEC)) >= 0 && (exe = open(path, O_RDONLY | O_CLOEXEC)) >= 0 &&
	    (null = open("/dev/null", O_RDWR | O_CLOEXEC)) >= 0 && !pipe2(in, O_CLOEXEC) && !pipe2(out, O_CLOEXEC)) {
		remote.pid = fork();
		if (remote.pid == 0)
			hat_child(dir, exe, in[0], out[1], null);
		if (remote.pid > 0) {
			remote.in = in[1];
			remote.out = out[0];
			in[1] = out[0] = -1;
			fcntl(remote.in, F_SETFL, O_NONBLOCK);
			fcntl(remote.out, F_SETFL, O_NONBLOCK);
			remote.shown = 0;
			remote.deadline = now_ms() + HAT_FIRST_MS;
			if (!hat_send())
				hat_stop();
		}
	}
	int fds[] = { dir, exe, null, in[0], in[1], out[0], out[1] };
	for (size_t i = 0; i < sizeof(fds) / sizeof(fds[0]); i++)
		if (fds[i] >= 0)
			close(fds[i]);
}

static int hat_remote(void)
{
	ssize_t n = -1;
	if (remote.pid <= 0)
		return 0;
	while (remote.have < sizeof(remote.frame) &&
	       (n = read(remote.out, remote.frame + remote.have, sizeof(remote.frame) - remote.have)) > 0)
		remote.have += (size_t)n;
	if (remote.have == sizeof(remote.frame)) {
		for (size_t i = 0; i < sizeof(low); i++)
			low[i] = remote.frame[i] < INKS ? remote.frame[i] : INK_BG;
		remote.shown = 1;
		remote.deadline = now_ms() + HAT_FRAME_MS;
		if (hat_send())
			return 1;
	} else if (n < 0 && (errno == EAGAIN || errno == EINTR) && now_ms() < remote.deadline) {
		return remote.shown;
	}
	hat_stop();
	return 0;
}

static void draw_hat(void)
{
	int s = hat_scale(), x0 = hat_x(), y0 = hat_y();
	uint32_t c[sizeof(ink) / sizeof(ink[0])];
	for (size_t i = 0; i < sizeof(ink) / sizeof(ink[0]); i++)
		c[i] = pack((Rgb)RGB(ink[i]));
	if (!hat_remote())
		hat(tick);
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
	starsize = H >= 960 ? 3 : 2;
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
		stars[nstars].base = 1 + (int)(r % 2);
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
			shade = stars[i].base < 2 ? stars[i].base + 1 : 3;
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
	} else if (!strncmp(line, "hat ", 4)) {
		hat_start(line + 4);
		close(open(HAT_ACK, O_WRONLY | O_CREAT | O_CLOEXEC, 0644));
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
	unlink(HAT_ACK);
	if (mkfifo(FIFO, 0600) || chmod(FIFO, 0622) || (ff = open(FIFO, O_RDWR | O_NONBLOCK | O_CLOEXEC)) < 0)
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
	hat_stop();
	unlink(FIFO);
	unlink(HAT_ACK);
	if (tty >= 0) {
		if (!handoff)
			ioctl(tty, KDSETMODE, KD_TEXT);
		close(tty);
	}
	unmap();
	return 0;
}
