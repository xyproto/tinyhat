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
#define STAR 15
#define MOON 14
#define WINLIT 44
#define WINDARK 8
#define FUR 6
#define FACE 42
#define HATRED 40
#define HATBAND 4
#define TEXT 15
#define BUILD0 20
#define GRAV 9.8

static unsigned char far *vga = (unsigned char far *)MK_FP(0xA000, 0);
static int adlib;
static int skyline[W];
static int gx[2], gy[2];
static int score[2];
static double wind;
static int cpu;

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
	".rrrr.",
	".rrrr.",
	".bbbb.",
	"rrrrrr",
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
	if (!adlib)
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

static void quiet(void)
{
	if (adlib)
		adlibw(0xb0, 0);
}

static void palette(void)
{
	int i;
	setpal(SKY, 2, 4, 16);
	setpal(STAR, 63, 63, 63);
	setpal(MOON, 60, 60, 48);
	setpal(WINLIT, 63, 56, 20);
	setpal(WINDARK, 10, 10, 14);
	setpal(FUR, 30, 18, 6);
	setpal(FACE, 50, 36, 24);
	setpal(HATRED, 58, 8, 8);
	setpal(HATBAND, 20, 0, 0);
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
	for (i = 0; i < 70; i++)
		pset(rand() % W, rand() % 120, STAR);
	for (j = -6; j <= 6; j++)
		for (i = -6; i <= 6; i++)
			if (i * i + j * j <= 36 && (i + 3) * (i + 3) + (j - 1) * (j - 1) > 25)
				pset(280 + i, 22 + j, MOON);
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

static void showwind(void)
{
	int x, len = (int)(wind * 3);
	rect(100, H - 6, 120, 4, SKY);
	if (!len)
		return;
	for (x = 0; abs(x) < abs(len); x += len > 0 ? 1 : -1)
		pset(160 + x, H - 4, STAR);
	pset(160 + len - (len > 0 ? 1 : -1), H - 5, STAR);
	pset(160 + len - (len > 0 ? 1 : -1), H - 3, STAR);
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

static int readnum(int col, int row, const char *label, int max)
{
	char buf[4];
	int n = 0, k;
	cleartext(col, row, 16);
	text(col, row, label, TEXT);
	for (;;) {
		k = getch();
		if (k == 27)
			return -1;
		if (k == 0) {
			getch();
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
		if (iy >= 0 && !hits(p, ix, iy) && pget(ix, iy) != SKY && pget(ix, iy) != STAR && pget(ix, iy) != MOON)
			return 1;
	}
	return 0;
}

static void explode(int x, int y, int r)
{
	int k, i, j;
	tone(0x100, 2, 7, 3);
	for (k = 1; k <= r; k++) {
		for (j = -k; j <= k; j++)
			for (i = -k; i <= k; i++)
				if (i * i + j * j <= k * k)
					pset(x + i, y + j, 64 + (i * i + j * j) * 7 / (k * k + 1));
		delay(15);
	}
	delay(80);
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
	unsigned char under[4][6];
	int ox = -100, oy = -100, i, j, result = 0;
	if (p)
		vx = -vx;
	drawape(p, 1);
	tone(0x2ae, 4, 2, 6);
	for (t = 0; t < 40; t += 0.05) {
		int ix = (int)(x0 + vx * t + wind * t * t / 2), iy = (int)(y0 + vy * t + GRAV * t * t / 2);
		if (t > 0.3 && gy[p] >= 0)
			drawape(p, 0);
		if (ox > -100)
			for (j = 0; j < 4; j++)
				for (i = 0; i < 6; i++)
					if (oy + j >= 0)
						pset(ox + i, oy + j, under[j][i]);
		if (ix < -6 || ix >= W || iy >= H) {
			ox = -100;
			break;
		}
		if (hits(!p, ix, iy)) {
			result = 2;
			break;
		}
		if (iy >= 0 && !hits(p, ix, iy) && pget(ix, iy) != SKY && pget(ix, iy) != STAR && pget(ix, iy) != MOON) {
			explode(ix, iy, 6);
			result = 1;
			ox = -100;
			break;
		}
		for (j = 0; j < 4; j++)
			for (i = 0; i < 6; i++)
				under[j][i] = (unsigned char)pget(ix - 3 + i, iy - 2 + j);
		ox = ix - 3;
		oy = iy - 2;
		if (iy >= -4)
			drawsprite(hat, 4, ox, oy);
		delay(6);
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
	wind = (rand() % 21 - 10) / 2.0;
	showwind();
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
			delay(700);
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
			tone(0x241, 5, 0, 4);
			delay(400);
			quiet();
			delay(800);
			return p;
		}
		miss[p]++;
		p = !p;
	}
}

static int title(void)
{
	int k;
	rect(0, 0, W, H, SKY);
	city();
	rect(32, 24, 256, 128, SKY);
	text(12, 4, "H A T   T O S S", HATRED);
	drawbig(hat, 4, 145, 48, 5);
	text(8, 10, "Two gorillas, one city,", TEXT);
	text(8, 11, "and a lot of tiny hats.", TEXT);
	text(6, 13, "1    Play against the computer", TEXT);
	text(6, 14, "2    Two players", TEXT);
	text(6, 16, "Esc  Quit", TEXT);
	for (;;) {
		k = getch();
		if (k == '1' || k == '2')
			return k - '0';
		if (k == 27)
			return 0;
	}
}

int main(void)
{
	int winner, first = 0, mode_;
	srand((unsigned)time(NULL));
	adlib = adlibdetect();
	if (adlib)
		adlibinit();
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
				getch();
				break;
			}
		}
	}
	quiet();
	mode(3);
	return 0;
}
