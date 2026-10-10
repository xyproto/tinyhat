// Hello GBA: a hat you can move around with the D-pad, drawn in bitmap mode 3.
// In Orbiton, press Ctrl+Space to build, or twice quickly to build and run it in mGBA.
// From a terminal, "make" builds it and "make run" runs it.
#include <tonc.h>

#define HAT_W 24
#define HAT_H 18
#define SKY RGB15(2, 3, 8)

static void draw_hat(int x, int y, COLOR hat, COLOR band)
{
    m3_rect(x + 4, y, x + 20, y + 14, hat);
    m3_rect(x + 4, y + 10, x + 20, y + 12, band);
    m3_rect(x, y + 14, x + HAT_W, y + HAT_H, hat);
}

int main(void)
{
    int x = (M3_WIDTH - HAT_W) / 2, y = 70;

    REG_DISPCNT = DCNT_MODE3 | DCNT_BG2;
    m3_fill(SKY);
    tte_init_bmp_default(3);
    tte_write("#{P:84,16}Hello GBA!");
    tte_write("#{P:48,140}D-pad moves the hat");

    while (1) {
        vid_vsync();
        key_poll();
        int nx = clamp(x + key_tri_horz() * 2, 0, M3_WIDTH - HAT_W);
        int ny = clamp(y + key_tri_vert() * 2, 32, 132 - HAT_H);
        if (nx != x || ny != y) {
            draw_hat(x, y, SKY, SKY);
            x = nx;
            y = ny;
        }
        draw_hat(x, y, RGB15(24, 7, 5), RGB15(3, 19, 18));
    }
}
