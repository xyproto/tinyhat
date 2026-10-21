// Talks to the boot splash: reads a frame number, draws it with hat() and writes the pixels back.
// Output from printf goes to the terminal when previewing.
#include <stdint.h>
#include <unistd.h>
#include "hat.h"

int main(void)
{
    int out = dup(1);
    int32_t tick;
    if (out < 0 || dup2(2, 1) < 0)
        return 1;
    for (;;) {
        size_t n = 0;
        while (n < sizeof(tick)) {
            ssize_t r = read(0, (char *)&tick + n, sizeof(tick) - n);
            if (r <= 0)
                return 0;
            n += (size_t)r;
        }
        hat(tick);
        for (n = 0; n < sizeof(low);) {
            ssize_t w = write(out, low + n, sizeof(low) - n);
            if (w <= 0)
                return 1;
            n += (size_t)w;
        }
    }
}
