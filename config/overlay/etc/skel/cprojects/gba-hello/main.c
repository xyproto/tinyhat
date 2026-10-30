// Hello GBA: a hat sprite you can move around with the D-pad, on a bitmap mode 3 background, with a little square-wave tune.
// Press Ctrl+Space in this editor to build, or press it twice in rapid succession to build and run it in mGBA.
#include <tonc.h>

#define HAT_W 24
#define HAT_H 18
#define HAT_TILE 512
#define SKY RGB15(3, 3, 5)

static const int tune[][2] = {
    { NOTE_C, 0 }, { NOTE_E, 0 }, { NOTE_G, 0 }, { NOTE_C, 1 },
    { NOTE_G, 0 }, { NOTE_E, 0 }, { NOTE_D, 0 }, { NOTE_G, -1 },
};

static OBJ_ATTR obj_buffer[128];

static int hat_pixel(int x, int y)
{
    if (x >= 4 && x < 20 && y >= 10 && y < 12)
        return 2;
    if ((x >= 4 && x < 20 && y < 14) || (x < HAT_W && y >= 14 && y < HAT_H))
        return 1;
    return 0;
}

static void make_hat(void)
{
    TILE *tiles = &tile_mem[5][HAT_TILE - 512];
    for (int ty = 0; ty < 4; ty++)
        for (int tx = 0; tx < 4; tx++)
            for (int row = 0; row < 8; row++) {
                u32 bits = 0;
                for (int col = 0; col < 8; col++)
                    bits |= (u32)hat_pixel(tx * 8 + col, ty * 8 + row) << (col * 4);
                tiles[ty * 4 + tx].data[row] = bits;
            }
    pal_obj_mem[1] = RGB15(24, 7, 5);
    pal_obj_mem[2] = RGB15(3, 19, 18);
}

int main(void)
{
    int x = (M3_WIDTH - HAT_W) / 2, y = 70, frame = 0;

    REG_DISPCNT = DCNT_MODE3 | DCNT_BG2 | DCNT_OBJ | DCNT_OBJ_1D;
    m3_fill(SKY);
    tte_init_bmp_default(3);
    tte_write("#{P:84,16}Hello GBA!");
    tte_write("#{P:48,140}D-pad moves the hat");

    make_hat();
    oam_init(obj_buffer, 128);
    OBJ_ATTR *hat = &obj_buffer[0];
    obj_set_attr(hat, ATTR0_SQUARE, ATTR1_SIZE_32, ATTR2_PALBANK(0) | HAT_TILE);

    REG_SNDSTAT = SSTAT_ENABLE;
    REG_SNDDMGCNT = SDMG_BUILD_LR(SDMG_SQR1, 7);
    REG_SNDDSCNT = SDS_DMG100;
    REG_SND1SWEEP = SSW_OFF;
    REG_SND1CNT = SSQR_ENV_BUILD(12, 0, 2) | SSQR_DUTY1_2;

    while (1) {
        vid_vsync();
        obj_set_pos(hat, x, y);
        oam_copy(oam_mem, obj_buffer, 1);
        key_poll();
        if (frame % 12 == 0) {
            const int *note = tune[frame / 12 % 8];
            REG_SND1FREQ = SFREQ_RESET | SND_RATE(note[0], note[1]);
        }
        frame++;
        x = clamp(x + key_tri_horz() * 2, 0, M3_WIDTH - HAT_W);
        y = clamp(y + key_tri_vert() * 2, 32, 132 - HAT_H);
    }
}
