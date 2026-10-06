/* Hello DOS: draws in 320x200 VGA mode 13h, like many DOS games and demos did.
   Build with "make", run it in DOSBox-X with "make run". */
#include <conio.h>
#include <i86.h>

#define W 320
#define H 200

static unsigned char far *vga = (unsigned char far *)MK_FP(0xA000, 0);

static void set_mode(unsigned char mode)
{
    union REGS r;
    r.h.ah = 0x00;
    r.h.al = mode;
    int86(0x10, &r, &r);
}

static void print_at(int col, int row, const char *s, unsigned char color)
{
    union REGS r;
    r.h.ah = 0x02;
    r.h.bh = 0;
    r.h.dl = col;
    r.h.dh = row;
    int86(0x10, &r, &r);
    for (; *s; s++) {
        r.h.ah = 0x0E;
        r.h.al = *s;
        r.h.bl = color;
        int86(0x10, &r, &r);
    }
}

static void fill_rect(int x, int y, int w, int h, unsigned char color)
{
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            vga[j * W + i] = color;
        }
    }
}

int main(void)
{
    set_mode(0x13);

    /* Colours 32-55 of the standard VGA palette go around the colour wheel */
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            vga[y * W + x] = 32 + ((x + y) / 8) % 24;
        }
    }

    fill_rect(130, 70, 60, 60, 4);   /* crown */
    fill_rect(130, 112, 60, 8, 3);   /* band */
    fill_rect(110, 130, 100, 12, 4); /* brim */

    print_at(15, 2, "TINY HAT", 15);
    print_at(9, 22, "Press any key to exit", 15);
    getch();

    set_mode(0x03);
    return 0;
}
