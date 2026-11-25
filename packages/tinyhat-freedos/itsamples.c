#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PERIOD 64
#define C5 16744
#define DRUMRATE 22050

static void put32(unsigned char* p, uint32_t v)
{
    p[0] = v & 0xff;
    p[1] = (v >> 8) & 0xff;
    p[2] = (v >> 16) & 0xff;
    p[3] = (v >> 24) & 0xff;
}

static int save(const char* dir, const char* file, const char* name, const signed char* data, uint32_t len, int loop, uint32_t speed, int vol)
{
    unsigned char h[0x50];
    char path[512];
    FILE* f;
    memset(h, 0, sizeof h);
    memcpy(h, "IMPS", 4);
    memcpy(h + 4, file, strlen(file) < 12 ? strlen(file) : 12);
    h[0x11] = 64;
    h[0x12] = 1 | (loop ? 0x10 : 0);
    h[0x13] = (unsigned char)vol;
    memcpy(h + 0x14, name, strlen(name) < 25 ? strlen(name) : 25);
    h[0x2e] = 1;
    h[0x2f] = 32;
    put32(h + 0x30, len);
    put32(h + 0x34, 0);
    put32(h + 0x38, loop ? len : 0);
    put32(h + 0x3c, speed);
    put32(h + 0x48, sizeof h);
    snprintf(path, sizeof path, "%s/%s", dir, file);
    if (!(f = fopen(path, "wb")))
        return 1;
    if (fwrite(h, 1, sizeof h, f) != sizeof h || fwrite(data, 1, len, f) != len) {
        fclose(f);
        return 1;
    }
    return fclose(f) != 0;
}

static signed char clip(double v)
{
    long s = lround(v * 127.0);
    return (signed char)(s > 127 ? 127 : s < -127 ? -127 : s);
}

static uint16_t lfsr = 1;

static double noise(void)
{
    int bit = ((lfsr >> 0) ^ (lfsr >> 1)) & 1;
    lfsr = (uint16_t)((lfsr >> 1) | (bit << 14));
    return (lfsr & 1) ? 1.0 : -1.0;
}

int main(int argc, char** argv)
{
    static signed char buf[16384];
    const char* dir = argc > 1 ? argv[1] : ".";
    int err = 0;
    uint32_t i, n;

    for (i = 0; i < PERIOD; i++)
        buf[i] = clip(i < PERIOD / 8 ? 0.8 : -0.8);
    err |= save(dir, "PULSE12.ITS", "Pulse 12.5%", buf, PERIOD, 1, C5, 48);
    for (i = 0; i < PERIOD; i++)
        buf[i] = clip(i < PERIOD / 4 ? 0.8 : -0.8);
    err |= save(dir, "PULSE25.ITS", "Pulse 25%", buf, PERIOD, 1, C5, 48);
    for (i = 0; i < PERIOD; i++)
        buf[i] = clip(i < PERIOD / 2 ? 0.8 : -0.8);
    err |= save(dir, "SQUARE.ITS", "Square (pulse 50%)", buf, PERIOD, 1, C5, 48);
    for (i = 0; i < PERIOD; i++) {
        int step = (int)(i * 32 / PERIOD);
        int level = step < 16 ? 15 - step : step - 16;
        buf[i] = clip((level - 7.5) / 7.5 * 0.9);
    }
    err |= save(dir, "TRIANGLE.ITS", "Triangle (4-bit)", buf, PERIOD, 1, C5, 64);
    for (i = 0; i < PERIOD; i++)
        buf[i] = clip((2.0 * i / PERIOD - 1.0) * 0.8);
    err |= save(dir, "SAW.ITS", "Sawtooth", buf, PERIOD, 1, C5, 48);
    n = 8192;
    for (i = 0; i < n; i++)
        buf[i] = clip(noise() * 0.6);
    err |= save(dir, "NOISE.ITS", "Noise (LFSR)", buf, n, 1, C5, 40);

    n = DRUMRATE / 4;
    {
        double phase = 0;
        for (i = 0; i < n; i++) {
            double t = (double)i / DRUMRATE, f = 50 + 120 * exp(-t * 30);
            phase += 2 * M_PI * f / DRUMRATE;
            buf[i] = clip(sin(phase) * exp(-t * 12));
        }
    }
    err |= save(dir, "KICK.ITS", "Kick drum", buf, n, 0, DRUMRATE, 64);
    n = DRUMRATE / 5;
    lfsr = 1;
    for (i = 0; i < n; i++) {
        double t = (double)i / DRUMRATE;
        buf[i] = clip((noise() * 0.6 + sin(2 * M_PI * 180 * t) * 0.4) * exp(-t * 18));
    }
    err |= save(dir, "SNARE.ITS", "Snare drum", buf, n, 0, DRUMRATE, 56);
    n = DRUMRATE / 16;
    {
        double prev = 0;
        lfsr = 0x2b;
        for (i = 0; i < n; i++) {
            double t = (double)i / DRUMRATE, x = noise();
            buf[i] = clip((x - prev) * 0.45 * exp(-t * 60));
            prev = x;
        }
    }
    err |= save(dir, "HIHAT.ITS", "Hi-hat", buf, n, 0, DRUMRATE, 40);
    if (err) {
        fprintf(stderr, "itsamples: could not write the samples to %s\n", dir);
        return 1;
    }
    return 0;
}
