/* Press Ctrl+Space in this editor to build, or press it twice in rapid succession to build and run it in Fuse. */
#include <arch/zx.h>
#include <conio.h>
#include <input.h>
#include <intrinsic.h>
#include <sound.h>

static const unsigned scale[] = {262, 294, 330, 349, 392, 440, 494, 523};
static const unsigned char rainbow[] = {INK_RED, INK_MAGENTA, INK_GREEN, INK_CYAN, INK_YELLOW, INK_WHITE};

static void block(unsigned char x, unsigned char y, unsigned char w, unsigned char h, unsigned char paper)
{
    unsigned char i, j;
    for (i = 0; i < h; ++i) {
        for (j = 0; j < w; ++j) {
            *zx_cxy2aaddr(x + j, y + i) = paper | BRIGHT;
        }
    }
}

int main(void)
{
    unsigned char i, frame = 0;

    zx_border(INK_BLACK);
    zx_cls_attr(PAPER_BLUE | INK_WHITE);

    gotoxy(12, 3);
    textbackground(BLUE);
    textcolor(WHITE);
    cputs("TINY HAT");

    block(12, 8, 8, 6, PAPER_RED);
    block(12, 12, 8, 1, PAPER_CYAN);
    block(9, 14, 14, 2, PAPER_RED);

    gotoxy(5, 21);
    textcolor(YELLOW);
    cputs("PRESS ANY KEY TO EXIT");

    for (i = 0; i < sizeof scale / sizeof scale[0]; ++i) {
        zx_border(rainbow[i % sizeof rainbow]);
        bit_beep(scale[i] / 5, (int)(437500L / scale[i]) - 30);
    }

    intrinsic_ei();
    in_WaitForNoKey();
    while (!in_Inkey()) {
        intrinsic_halt();
        zx_border(rainbow[(++frame >> 3) % sizeof rainbow]);
    }

    zx_border(INK_WHITE);
    zx_cls_attr(PAPER_WHITE | INK_BLACK);
    return 0;
}
