#include <snes.h>

int main(void)
{
    u16 frame = 0;
    u8 level, shade;
    u8 greeted = 0;

    consoleInitDefaultText(0);
    bgSetGfxPtr(0, 0x3000);
    bgSetMapPtr(0, 0x6800, SC_32x32);
    setMode(BG_MODE1, 0);
    bgSetDisable(1);
    bgSetDisable(2);

    consoleDrawText(12, 6, "TINY HAT");
    consoleDrawText(6, 12, "HELLO FROM PVSNESLIB");
    consoleDrawText(10, 20, "PRESS START");

    setScreenOn();

    while (1) {
        WaitForVBlank();
        frame++;
        level = (frame >> 2) & 63;
        shade = level < 32 ? level : 63 - level;
        setPaletteColor(0, RGB5(0, shade >> 2, shade >> 1));
        if (padsDown(0) & KEY_START) {
            greeted = !greeted;
            consoleDrawText(10, 20, greeted ? "HELLO PLAYER" : "PRESS START ");
        }
    }
    return 0;
}
