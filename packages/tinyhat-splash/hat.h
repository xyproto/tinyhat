#define LOW 64

#ifndef HAT_COLOR
#define HAT_COLOR 0x922424
#endif
#ifndef HAT_HIGHLIGHT_COLOR
#define HAT_HIGHLIGHT_COLOR 0xf7768e
#endif
#define INK_COLORS { 0x1a1b26, HAT_COLOR, HAT_HIGHLIGHT_COLOR, 0x494949, 0xc0caf5, 0x7dcfff, 0x1a9e96 }

enum { INK_BG, INK_HAT, INK_HIGHLIGHT, INK_GRAY, INK_LIGHT, INK_CYAN, INK_TEAL, INKS };

extern unsigned char low[LOW * LOW];

void line(int x0, int y0, int x1, int y1, unsigned char ink);
void hat(long tick);
