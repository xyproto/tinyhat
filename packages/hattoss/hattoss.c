#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define W 320
#define H 200
#define SKY 0
#define SUN 14
#define SUNFACE 12
#define POLE 7
#define SOCKW 15
#define WINLIT 44
#define WINDARK 8
#define FUR 6
#define FACE 42
#define HATRED 40
#define HATBAND 4
#define TEXT 15
#define BANANA 50
#define BANTIP 51
#define GHOST 52
#define PUPIL 53
#define BARREL 54
#define HOOP 55
#define BUILD0 20
#define GRAV 9.8

static unsigned char far *vga = (unsigned char far *)MK_FP(0xA000, 0);
static int adlib;
static int skyline[W];
static int gx[2], gy[2];
static int score[2];
static double wind;
static int cpu;
static int sunhappy;
static int soundon = 1;
static int sockx, socky;

#define SUNX 160
#define SUNY 20

static void beep(int freq);
static void nobeep(void);
static void togglesound(void);

static int pendingkey;

static void pollkeys(void)
{
	int k;
	while (kbhit()) {
		k = getch();
		if (k == 0)
			getch();
		else if (k == 's' || k == 'S')
			togglesound();
		else
			pendingkey = k;
	}
}

static int getkey(void)
{
	int k;
	pollkeys();
	if (pendingkey) {
		k = pendingkey;
		pendingkey = 0;
		return k;
	}
	return getch();
}

static void vsync(void)
{
	while (inp(0x3da) & 8)
		;
	while (!(inp(0x3da) & 8))
		;
}

#define PIT_HZ 1193182UL

static void timerstart(void)
{
	outp(0x43, 0x34);
	outp(0x40, 0);
	outp(0x40, 0);
}

static void timerstop(void)
{
	outp(0x43, 0x36);
	outp(0x40, 0);
	outp(0x40, 0);
}

/* The time in PIT clocks (1193182 per second), from the BIOS tick count and the PIT counter */
static unsigned long now(void)
{
	volatile unsigned long far *ticks = (volatile unsigned long far *)MK_FP(0x40, 0x6c);
	unsigned long t1, t2;
	unsigned c;
	do {
		t1 = *ticks;
		_disable();
		outp(0x43, 0);
		c = inp(0x40);
		c |= inp(0x40) << 8;
		_enable();
		t2 = *ticks;
	} while (t1 != t2);
	return (t1 << 16) + (0xffffu - c);
}

static void rest(int ms)
{
	unsigned long start = now(), span = (unsigned long)ms * (PIT_HZ / 1000);
	while (now() - start < span)
		pollkeys();
}

static void frame(void)
{
	static unsigned long last;
	while (now() - last < PIT_HZ / 100)
		pollkeys();
	vsync();
	last = now();
}

static const char *ape[] = {
	"....aaaaa....",
	"...aaaaaaa...",
	"...afffffa...",
	"...af.f.fa...",
	"...afffffa...",
	"....aaaaa....",
	".aaaaaaaaaaa.",
	"aaaaaaaaaaaaa",
	"aa.aaaaaaa.aa",
	"aa.aaaaaaa.aa",
	"...aaaaaaa...",
	"...aaa.aaa...",
	"..aaa...aaa..",
	"..aa.....aa..",
};

static const char *apeup[] = {
	"aa..aaaaa..aa",
	"aa.aaaaaaa.aa",
	"aa.afffffa.aa",
	"aa.af.f.fa.aa",
	".a.afffffa.a.",
	".aa.aaaaa.aa.",
	"..aaaaaaaaa..",
	"..aaaaaaaaa..",
	"...aaaaaaa...",
	"...aaaaaaa...",
	"...aaaaaaa...",
	"...aaa.aaa...",
	"..aaa...aaa..",
	"..aa.....aa..",
};

static const char *hat[] = {
	".rrrrr.",
	".rrrrr.",
	".bbbbb.",
	"rrrrrrr",
};

static void mode(int m)
{
	union REGS r;
	r.w.ax = m;
	int86(0x10, &r, &r);
}

static void setpal(int i, int r, int g, int b)
{
	outp(0x3c8, i);
	outp(0x3c9, r);
	outp(0x3c9, g);
	outp(0x3c9, b);
}

static void pset(int x, int y, int c)
{
	if (x >= 0 && x < W && y >= 0 && y < H)
		vga[y * W + x] = c;
}

static int pget(int x, int y)
{
	if (x < 0 || x >= W || y < 0 || y >= H)
		return SKY;
	return vga[y * W + x];
}

static void rect(int x, int y, int w, int h, int c)
{
	int i, j;
	for (j = y; j < y + h; j++)
		for (i = x; i < x + w; i++)
			pset(i, j, c);
}

static void gotoxy(int col, int row)
{
	union REGS r;
	r.h.ah = 2;
	r.h.bh = 0;
	r.h.dh = row;
	r.h.dl = col;
	int86(0x10, &r, &r);
}

static void putch13(char ch, int color)
{
	union REGS r;
	r.h.ah = 0x0e;
	r.h.al = ch;
	r.h.bh = 0;
	r.h.bl = color;
	int86(0x10, &r, &r);
}

static void text(int col, int row, const char *s, int color)
{
	gotoxy(col, row);
	while (*s)
		putch13(*s++, color);
}

static void cleartext(int col, int row, int len)
{
	rect(col * 8, row * 8, len * 8, 8, SKY);
}

static void adlibw(int reg, int val)
{
	int i;
	outp(0x388, reg);
	for (i = 0; i < 6; i++)
		inp(0x388);
	outp(0x389, val);
	for (i = 0; i < 35; i++)
		inp(0x388);
}

static int adlibdetect(void)
{
	int a, b;
	adlibw(4, 0x60);
	adlibw(4, 0x80);
	a = inp(0x388);
	adlibw(2, 0xff);
	adlibw(4, 0x21);
	delay(1);
	b = inp(0x388);
	adlibw(4, 0x60);
	adlibw(4, 0x80);
	return (a & 0xe0) == 0 && (b & 0xe0) == 0xc0;
}

static void adlibinit(void)
{
	int r;
	for (r = 1; r < 0xf6; r++)
		adlibw(r, 0);
	adlibw(0x01, 0x20);
}

static void tone(int fnum, int block, int feedback, int decay)
{
	if (!adlib || !soundon)
		return;
	adlibw(0xb0, 0);
	adlibw(0x20, 0x01);
	adlibw(0x23, 0x01);
	adlibw(0x40, 0x10);
	adlibw(0x43, 0x00);
	adlibw(0x60, 0xf0 | decay);
	adlibw(0x63, 0xf0 | decay);
	adlibw(0x80, 0x77);
	adlibw(0x83, 0x77);
	adlibw(0xc0, feedback << 1);
	adlibw(0xa0, fnum & 0xff);
	adlibw(0xb0, 0x20 | (block << 2) | ((fnum >> 8) & 3));
}

static void sfx(int kind)
{
	int i;
	if (!soundon)
		return;
	if (adlib) {
		switch (kind) {
		case 0:
			tone(0x2ae, 4, 2, 6);
			break;
		case 1:
			tone(0x100, 2, 7, 3);
			break;
		case 2:
			tone(0x241, 5, 0, 4);
			break;
		case 3:
			tone(0x2ae, 6, 0, 5);
			break;
		}
		return;
	}
	switch (kind) {
	case 0:
		for (i = 400; i < 1000; i += 60) {
			beep(i);
			rest(4);
		}
		nobeep();
		break;
	case 2:
		beep(523);
		rest(120);
		beep(659);
		rest(120);
		beep(784);
		rest(220);
		nobeep();
		break;
	case 3:
		beep(1319);
		rest(50);
		beep(1760);
		rest(90);
		nobeep();
		break;
	}
}

static void quiet(void)
{
	if (adlib)
		adlibw(0xb0, 0);
}

static void palette(void)
{
	int i;
	setpal(SKY, 18, 36, 58);
	setpal(SUN, 63, 56, 12);
	setpal(SUNFACE, 40, 20, 0);
	setpal(POLE, 30, 30, 34);
	setpal(SOCKW, 63, 63, 63);
	setpal(WINLIT, 63, 56, 20);
	setpal(WINDARK, 10, 10, 14);
	setpal(FUR, 30, 18, 6);
	setpal(FACE, 50, 36, 24);
	setpal(HATRED, 58, 8, 8);
	setpal(HATBAND, 20, 0, 0);
	setpal(BANANA, 63, 58, 10);
	setpal(BANTIP, 30, 20, 4);
	setpal(GHOST, 63, 30, 46);
	setpal(PUPIL, 8, 12, 50);
	setpal(BARREL, 40, 24, 10);
	setpal(HOOP, 18, 12, 6);
	setpal(BUILD0, 22, 24, 30);
	setpal(BUILD0 + 1, 28, 18, 22);
	setpal(BUILD0 + 2, 16, 26, 26);
	for (i = 0; i < 8; i++)
		setpal(64 + i, 63, 20 + i * 5, i * 4);
}

static void drawsprite(const char **s, int rows, int x, int y)
{
	int i, j;
	for (j = 0; j < rows; j++)
		for (i = 0; s[j][i]; i++) {
			char c = s[j][i];
			if (c == 'a')
				pset(x + i, y + j, FUR);
			else if (c == 'f')
				pset(x + i, y + j, FACE);
			else if (c == 'r')
				pset(x + i, y + j, HATRED);
			else if (c == 'b')
				pset(x + i, y + j, HATBAND);
		}
}

static const char *things[4][6] = {
	{ "......", ".rrrr.", ".rrrr.", ".bbbb.", "rrrrrr", "......" },
	{ "......", "k....k", "y....y", "yy..yy", ".yyyy.", "......" },
	{ ".gggg.", "gggggg", "gwegwe", "gggggg", "gggggg", "g.gg.g" },
	{ ".oooo.", "hhhhhh", "oooooo", "oooooo", "hhhhhh", ".oooo." },
};

static int thing;

static void drawspin(int x, int y, int frame)
{
	int i, j, si, sj;
	for (j = 0; j < 6; j++)
		for (i = 0; i < 6; i++) {
			char c;
			switch (frame & 3) {
			case 0:
				si = i, sj = j;
				break;
			case 1:
				si = j, sj = 5 - i;
				break;
			case 2:
				si = 5 - i, sj = 5 - j;
				break;
			default:
				si = 5 - j, sj = i;
				break;
			}
			c = things[thing][sj][si];
			switch (c) {
			case 'r':
				pset(x + i, y + j, HATRED);
				break;
			case 'b':
				pset(x + i, y + j, HATBAND);
				break;
			case 'y':
				pset(x + i, y + j, BANANA);
				break;
			case 'k':
				pset(x + i, y + j, BANTIP);
				break;
			case 'g':
				pset(x + i, y + j, GHOST);
				break;
			case 'w':
				pset(x + i, y + j, SOCKW);
				break;
			case 'e':
				pset(x + i, y + j, PUPIL);
				break;
			case 'o':
				pset(x + i, y + j, BARREL);
				break;
			case 'h':
				pset(x + i, y + j, HOOP);
				break;
			}
		}
}

static void drawbig(const char **s, int rows, int x, int y, int k)
{
	int i, j;
	for (j = 0; j < rows; j++)
		for (i = 0; s[j][i]; i++) {
			char c = s[j][i];
			if (c == 'r')
				rect(x + i * k, y + j * k, k, k, HATRED);
			else if (c == 'b')
				rect(x + i * k, y + j * k, k, k, HATBAND);
		}
}

static void drawape(int p, int up)
{
	rect(gx[p], gy[p], 13, 14, SKY);
	drawsprite(up ? apeup : ape, 14, gx[p], gy[p]);
}

static void city(void)
{
	int x = 0, i, j, k;
	rect(0, 0, W, H, SKY);
	while (x < W) {
		int w = 24 + rand() % 22, h = 40 + rand() % 90, c = BUILD0 + rand() % 3;
		if (x + w > W)
			w = W - x;
		rect(x, H - h, w - 1, h, c);
		for (j = H - h + 4; j < H - 4; j += 7)
			for (i = x + 3; i < x + w - 4; i += 5)
				rect(i, j, 2, 3, rand() % 4 ? WINLIT : WINDARK);
		for (k = x; k < x + w && k < W; k++)
			skyline[k] = H - h;
		x += w;
	}
}

static void placeapes(void)
{
	int p;
	for (p = 0; p < 2; p++) {
		int x = p == 0 ? 20 + rand() % 50 : W - 33 - rand() % 50, top = H, i;
		for (i = x; i < x + 13; i++)
			if (skyline[i] < top)
				top = skyline[i];
		gx[p] = x;
		gy[p] = top - 14;
		drawape(p, 0);
	}
}

static int solid(int c)
{
	return c == WINLIT || c == WINDARK || (c >= BUILD0 && c <= BUILD0 + 2);
}

static void beep(int freq)
{
	unsigned div = (unsigned)(1193182L / freq);
	if (!soundon)
		return;
	outp(0x43, 0xb6);
	outp(0x42, div & 0xff);
	outp(0x42, div >> 8);
	outp(0x61, inp(0x61) | 3);
}

static void nobeep(void)
{
	outp(0x61, inp(0x61) & 0xfc);
}

static void drawsun(void)
{
	int i, j, k;
	static const int rays[8][2] = { { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 } };
	rect(SUNX - 16, SUNY - 16, 33, 33, SKY);
	for (j = -9; j <= 9; j++)
		for (i = -9; i <= 9; i++)
			if (i * i + j * j <= 81)
				pset(SUNX + i, SUNY + j, SUN);
	for (k = 0; k < 8; k++)
		for (i = 11; i <= 14; i++)
			pset(SUNX + rays[k][0] * (rays[k][0] && rays[k][1] ? i * 3 / 4 : i), SUNY + rays[k][1] * (rays[k][0] && rays[k][1] ? i * 3 / 4 : i), SUN);
	if (sunhappy) {
		for (k = -1; k <= 1; k++) {
			pset(SUNX - 4 + k, SUNY - 3 + k * k, SUNFACE);
			pset(SUNX + 4 + k, SUNY - 3 + k * k, SUNFACE);
		}
		for (i = -4; i <= 4; i++)
			for (j = 3 - (i * i) / 8; j <= 3 - (i * i) / 8 + (i * i <= 4); j++)
				pset(SUNX + i, SUNY + j, SUNFACE);
		drawsprite(hat, 4, SUNX - 3, SUNY - 12);
	} else {
		rect(SUNX - 4, SUNY - 3, 2, 2, SUNFACE);
		rect(SUNX + 3, SUNY - 3, 2, 2, SUNFACE);
		for (i = -3; i <= 3; i++)
			pset(SUNX + i, SUNY + 3 - (i * i) / 9, SUNFACE);
	}
}

static void drawsock(void)
{
	int len, i, j, dir;
	for (j = 0; j < 12; j++)
		pset(sockx, socky - j, POLE);
	if (wind == 0) {
		for (j = 0; j < 7; j++)
			for (i = 1; i <= 2; i++)
				pset(sockx + i, socky - 11 + j, (j / 2) % 2 ? SOCKW : HATRED);
		return;
	}
	dir = wind > 0 ? 1 : -1;
	len = 4 + (int)(fabs(wind) * 4);
	for (i = 1; i <= len; i++) {
		int h = i < len * 2 / 3 ? 3 : 2, c = ((i - 1) / 3) % 2 ? SOCKW : HATRED;
		int droop = i * (4 - (int)fabs(wind)) / 8;
		for (j = 0; j < h; j++)
			pset(sockx + dir * i, socky - 11 + j + droop, c);
	}
}

static void showscore(void)
{
	char s[16];
	s[0] = '0' + score[0];
	strcpy(s + 1, ">Score<");
	s[8] = '0' + score[1];
	s[9] = 0;
	cleartext(15, 24, 10);
	text(15, 24, s, TEXT);
}

static void togglesound(void)
{
	soundon = !soundon;
	quiet();
	nobeep();
}

static int sunhit(int x, int y)
{
	int cx = SUNX < x - 3 ? x - 3 : SUNX > x + 2 ? x + 2 : SUNX;
	int cy = SUNY < y - 3 ? y - 3 : SUNY > y + 2 ? y + 2 : SUNY;
	return (cx - SUNX) * (cx - SUNX) + (cy - SUNY) * (cy - SUNY) <= 81;
}

static int readnum(int col, int row, const char *label, int max)
{
	char buf[4];
	int n = 0, k;
	cleartext(col, row, 16);
	text(col, row, label, TEXT);
	for (;;) {
		k = getkey();
		if (k == 27)
			return -1;
		if (k == 0) {
			getch();
			continue;
		}
		if (k == 's' || k == 'S') {
			togglesound();
			continue;
		}
		if (k == 13 && n > 0)
			break;
		if (k == 8 && n > 0) {
			n--;
			cleartext(col + (int)strlen(label), row, 4);
			buf[n] = 0;
			text(col + (int)strlen(label), row, buf, TEXT);
		} else if (k >= '0' && k <= '9' && n < 3) {
			buf[n++] = (char)k;
			buf[n] = 0;
			text(col + (int)strlen(label), row, buf, TEXT);
		}
	}
	buf[n] = 0;
	k = atoi(buf);
	return k > max ? max : k;
}

static int hits(int p, int x, int y)
{
	return x >= gx[p] && x < gx[p] + 13 && y >= gy[p] && y < gy[p] + 14;
}

static int simulate(int p, double angle, double speed, int *tx)
{
	double a = angle * 3.14159265 / 180, vx = cos(a) * speed, vy = -sin(a) * speed;
	double x = gx[p] + (p ? 0 : 12), y = gy[p] - 4, t;
	if (p)
		vx = -vx;
	for (t = 0; t < 40; t += 0.05) {
		int ix = (int)(x + vx * t + wind * t * t / 2), iy = (int)(y + vy * t + GRAV * t * t / 2);
		*tx = ix;
		if (ix < 0 || ix >= W || iy >= H)
			return 0;
		if (hits(!p, ix, iy))
			return 2;
		if (iy >= 0 && !hits(p, ix, iy) && solid(pget(ix, iy)))
			return 1;
	}
	return 0;
}

static void explode(int x, int y, int r)
{
	int k, i, j;
	sfx(1);
	for (k = 1; k <= r; k++) {
		if (!adlib)
			beep(60 + rand() % 240);
		for (j = -k; j <= k; j++)
			for (i = -k; i <= k; i++)
				if (i * i + j * j <= k * k)
					pset(x + i, y + j, 64 + (i * i + j * j) * 7 / (k * k + 1));
		rest(15);
	}
	nobeep();
	rest(80);
	for (j = -r; j <= r; j++)
		for (i = -r; i <= r; i++)
			if (i * i + j * j <= r * r)
				pset(x + i, y + j, SKY);
	quiet();
}

static int throwhat(int p, double angle, double speed)
{
	double a = angle * 3.14159265 / 180, vx = cos(a) * speed, vy = -sin(a) * speed;
	double x0 = gx[p] + (p ? 0 : 12), y0 = gy[p] - 4, t;
	unsigned char under[6][6];
	int ox = -100, oy = -100, i, j, ix = 0, iy = 0, idle = 0, done = 0, result = 0;
	unsigned long prev = now();
	double acc = 0.05;
	if (p)
		vx = -vx;
	thing = rand() % 4;
	drawape(p, 1);
	sfx(0);
	for (t = 0; t < 40 && !done;) {
		unsigned long cur;
		pollkeys();
		frame();
		cur = now();
		acc += (double)(cur - prev) * 3.0 / PIT_HZ;
		prev = cur;
		if (acc > 0.3)
			acc = 0.3;
		if (ox > -100)
			for (j = 0; j < 6; j++)
				for (i = 0; i < 6; i++)
					if (oy + j >= 0)
						pset(ox + i, oy + j, under[j][i]);
		ox = -100;
		for (; acc >= 0.05 && t < 40 && !done; acc -= 0.05, t += 0.05) {
			ix = (int)(x0 + vx * t + wind * t * t / 2);
			iy = (int)(y0 + vy * t + GRAV * t * t / 2);
			if (t > 0.3 && !idle && gy[p] >= 0) {
				drawape(p, 0);
				idle = 1;
			}
			if (ix < -6 || ix >= W || iy >= H) {
				done = 1;
			} else if (hits(!p, ix, iy)) {
				result = 2;
				done = 1;
			} else {
				if (!sunhappy && sunhit(ix, iy)) {
					sunhappy = 1;
					drawsun();
					sfx(3);
				}
				if (iy >= 0 && !hits(p, ix, iy) && solid(pget(ix, iy))) {
					explode(ix, iy, 6);
					result = 1;
					done = 1;
				}
			}
		}
		if (done)
			break;
		for (j = 0; j < 6; j++)
			for (i = 0; i < 6; i++)
				under[j][i] = (unsigned char)pget(ix - 3 + i, iy - 3 + j);
		ox = ix - 3;
		oy = iy - 3;
		if (iy >= -6)
			drawspin(ox, oy, p ? 3 - (int)(t * 10) % 4 : (int)(t * 10) % 4);
	}
	quiet();
	if (result == 2) {
		explode(gx[!p] + 6, gy[!p] + 7, 14);
		return 1;
	}
	return 0;
}

static void cpuaim(int p, int miss, int *angle, int *speed)
{
	int a, s, tx, best = 9999, err = miss < 4 ? 14 - miss * 4 : 2;
	*angle = 45;
	*speed = 60;
	for (a = 25; a <= 75; a += 5)
		for (s = 20; s <= 150; s += 2) {
			int r = simulate(p, a, s, &tx), d = abs(tx - (gx[!p] + 6));
			if (r == 2) {
				*angle = a;
				*speed = s;
				goto found;
			}
			if (d < best) {
				best = d;
				*angle = a;
				*speed = s;
			}
		}
found:
	*speed += rand() % (2 * err + 1) - err;
	if (*speed < 10)
		*speed = 10;
}

static int round_(int first)
{
	int p = first, miss[2] = { 0, 0 };
	city();
	placeapes();
	wind = (rand() % 13 - 6) / 2.0;
	sunhappy = 0;
	drawsun();
	sockx = 150 + rand() % 21;
	socky = skyline[sockx] - 1;
	drawsock();
	showscore();
	text(0, 0, cpu ? "You" : "Player 1", TEXT);
	text(32, 0, cpu ? "Computer" : "Player 2", TEXT);
	for (;;) {
		int angle, speed, col = p ? 24 : 0;
		if (cpu && p == 1) {
			char s[20];
			cpuaim(p, miss[p], &angle, &speed);
			cleartext(col, 1, 16);
			cleartext(col, 2, 16);
			s[0] = 0;
			text(col, 1, "Angle: ", TEXT);
			itoa(angle, s, 10);
			text(col + 7, 1, s, TEXT);
			text(col, 2, "Speed: ", TEXT);
			itoa(speed, s, 10);
			text(col + 7, 2, s, TEXT);
			rest(700);
		} else {
			cleartext(col, 2, 16);
			angle = readnum(col, 1, "Angle: ", 90);
			if (angle < 0)
				return -1;
			speed = readnum(col, 2, "Speed: ", 200);
			if (speed < 0)
				return -1;
		}
		cleartext(col, 1, 16);
		cleartext(col, 2, 16);
		if (throwhat(p, angle, speed)) {
			score[p]++;
			showscore();
			sfx(2);
			rest(400);
			quiet();
			rest(800);
			return p;
		}
		miss[p]++;
		p = !p;
	}
}

static void soundline(void)
{
	cleartext(13, 16, 14);
	text(13, 16, soundon ? "Sound: on" : "Sound: off", TEXT);
}

static int title(void)
{
	int k;
	rect(0, 0, W, H, SKY);
	city();
	sunhappy = 1;
	drawsun();
	rect(36, 46, 248, 132, HATBAND);
	rect(38, 48, 244, 128, HATRED);
	rect(40, 50, 240, 124, SKY);
	text(12, 8, "H A T   T O S S", HATRED);
	drawsprite(ape, 14, 104, 86);
	drawbig(hat, 4, 143, 82, 5);
	drawsprite(apeup, 14, 203, 86);
	text(9, 13, "1", HATRED);
	text(13, 13, "Play the computer", TEXT);
	text(9, 14, "2", HATRED);
	text(13, 14, "Two players", TEXT);
	text(9, 16, "S", HATRED);
	soundline();
	text(9, 18, "Esc", HATRED);
	text(13, 18, "Quit", TEXT);
	for (;;) {
		k = getkey();
		if (k == '1' || k == '2')
			return k - '0';
		if (k == 's' || k == 'S') {
			togglesound();
			soundline();
		}
		if (k == 27)
			return 0;
	}
}

int main(int argc, char **argv)
{
	int winner, first = 0, mode_;
	srand((unsigned)time(NULL));
	adlib = (argc > 1 && !stricmp(argv[1], "/ADLIB")) || adlibdetect();
	if (adlib)
		adlibinit();
	timerstart();
	mode(0x13);
	palette();
	while ((mode_ = title()) != 0) {
		cpu = mode_ == 1;
		score[0] = score[1] = 0;
		for (;;) {
			winner = round_(first);
			if (winner < 0)
				break;
			first = !first;
			if (score[winner] >= 3) {
				rect(72, 80, 176, 24, SKY);
				text(11, 11, winner ? (cpu ? "The computer wins!" : "Player 2 wins!") : (cpu ? "You win!" : "Player 1 wins!"), TEXT);
				getkey();
				break;
			}
		}
	}
	quiet();
	timerstop();
	mode(3);
	return 0;
}
