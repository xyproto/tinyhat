/* Press Ctrl+Space in this editor to build, or press it twice in rapid succession to build and run it in VICE. */
#include <c64.h>
#include <conio.h>

static const unsigned scale[] = {4455, 5001, 5613, 5947, 6676, 7493, 8411, 8911};
static const unsigned char rainbow[] = {
    COLOR_RED, COLOR_ORANGE, COLOR_YELLOW, COLOR_LIGHTGREEN,
    COLOR_CYAN, COLOR_LIGHTBLUE, COLOR_PURPLE,
};

static void wait_frame(void)
{
    while (VIC.rasterline != 250) {
    }
    while (VIC.rasterline == 250) {
    }
}

static void block(unsigned char x, unsigned char y, unsigned char w, unsigned char h, unsigned char color)
{
    unsigned char i;
    textcolor(color);
    revers(1);
    for (i = 0; i < h; ++i) {
        cclearxy(x, y + i, w);
    }
    revers(0);
}

static void play(unsigned freq, unsigned char frames)
{
    SID.v1.freq = freq;
    SID.v1.ctrl = 0x11;
    while (frames--) {
        wait_frame();
    }
    SID.v1.ctrl = 0x10;
}

int main(void)
{
    unsigned char i, frame = 0;

    bordercolor(COLOR_BLACK);
    bgcolor(COLOR_BLUE);
    clrscr();

    textcolor(COLOR_WHITE);
    cputsxy(16, 3, "TINY HAT");

    block(16, 8, 8, 6, COLOR_RED);
    block(16, 12, 8, 1, COLOR_CYAN);
    block(13, 14, 14, 2, COLOR_RED);

    textcolor(COLOR_YELLOW);
    cputsxy(9, 22, "PRESS ANY KEY TO EXIT");

    SID.amp = 0x0F;
    SID.v1.ad = 0x09;
    SID.v1.sr = 0x00;
    for (i = 0; i < sizeof scale / sizeof scale[0]; ++i) {
        VIC.bordercolor = rainbow[i % sizeof rainbow];
        play(scale[i], 12);
    }

    while (!kbhit()) {
        wait_frame();
        VIC.bordercolor = rainbow[(++frame >> 3) % sizeof rainbow];
    }
    cgetc();

    SID.amp = 0;
    bordercolor(COLOR_LIGHTBLUE);
    bgcolor(COLOR_BLUE);
    textcolor(COLOR_LIGHTBLUE);
    clrscr();
    VIC.addr = 0x15;
    return 0;
}
