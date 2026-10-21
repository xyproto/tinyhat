// The boot animation. hat() draws frame number "tick" (12.5 frames per second) into the LOW x LOW pixels in "low".
// Press Ctrl+Space in the editor to build it and use it from the next boot, or press it twice quickly to also preview it.
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "hat.h"

#define SEGMENTS 16

typedef struct {
    int x, y;
    double z;
} Point;

unsigned char low[LOW * LOW];

static double tilt = 0.38;

void line(int x0, int y0, int x1, int y1, unsigned char ink)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        if (x0 >= 0 && y0 >= 0 && x0 < LOW && y0 < LOW)
            low[y0 * LOW + x0] = ink;
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * e;
        if (e2 >= dy)
            e += dy, x0 += sx;
        if (e2 <= dx)
            e += dx, y0 += sy;
    }
}

static Point project(double angle, double x, double y, double z)
{
    double x1 = x * cos(angle) + z * sin(angle), z1 = -x * sin(angle) + z * cos(angle);
    double y1 = y - 0.45;
    double y2 = y1 * cos(tilt) - z1 * sin(tilt), z2 = y1 * sin(tilt) + z1 * cos(tilt);
    double f = 3.4 / (z2 + 3.4);
    Point p = { (int)lround(LOW / 2 + x1 * f * 25), (int)lround(LOW / 2 - y2 * f * 25), z2 };
    return p;
}

static void edge(int pass, Point a, Point b, unsigned char front, unsigned char rear)
{
    int is_front = a.z + b.z < 0;
    if (is_front == pass)
        line(a.x, a.y, b.x, b.y, is_front ? front : rear);
}

static void ring(int pass, double angle, double y, double r, unsigned char front, unsigned char rear)
{
    for (int i = 0; i < SEGMENTS; i++) {
        double a0 = 2 * M_PI * i / SEGMENTS, a1 = 2 * M_PI * (i + 1) / SEGMENTS;
        edge(pass, project(angle, r * cos(a0), y, r * sin(a0)), project(angle, r * cos(a1), y, r * sin(a1)), front,
             rear);
    }
}

static Point at(double angle, double r, double y, double a, double t)
{
    return project(angle, r * cos(a) - t * sin(a), y, r * sin(a) + t * cos(a));
}

static Point quill(double angle, double t, double spread)
{
    double w = spread * sin(M_PI * t);
    return at(angle, 0.58 + 0.45 * t - 0.15 * t * t + (w != 0 ? 0.04 : 0), 0.2 + 0.95 * t - (w != 0 ? 0.08 : 0),
              -0.45 - 0.35 * t, w);
}

void hat(long tick)
{
    double angle = 2 * M_PI * (double)(tick % 75) / 75;
    memset(low, INK_BG, sizeof(low));
    tilt = 0.36 + 0.1 * sin(2 * angle);
    for (int pass = 0; pass < 2; pass++) {
        for (int side = -1; side <= 1; side += 2) {
            Point knot = at(angle, 0.56, 0.19, 0, 0);
            Point top = at(angle, 0.6, 0.27, 0, side * 0.15), bottom = at(angle, 0.6, 0.11, 0, side * 0.15);
            edge(pass, knot, top, INK_LIGHT, INK_GRAY);
            edge(pass, top, bottom, INK_LIGHT, INK_GRAY);
            edge(pass, bottom, knot, INK_LIGHT, INK_GRAY);
        }
        ring(pass, angle, 0, 1.0, INK_HIGHLIGHT, INK_HAT);
        ring(pass, angle, 0, 0.55, INK_HIGHLIGHT, INK_HAT);
        ring(pass, angle, 0.12, 0.555, INK_LIGHT, INK_GRAY);
        ring(pass, angle, 0.26, 0.56, INK_LIGHT, INK_GRAY);
        ring(pass, angle, 0.95, 0.6, INK_HIGHLIGHT, INK_HAT);
        for (int i = 0; i < SEGMENTS; i += 2) {
            double a = 2 * M_PI * i / SEGMENTS;
            edge(pass, project(angle, 0.55 * cos(a), 0, 0.55 * sin(a)),
                 project(angle, 0.6 * cos(a), 0.95, 0.6 * sin(a)), INK_HIGHLIGHT, INK_HAT);
            edge(pass, project(angle, 0.55 * cos(a), 0, 0.55 * sin(a)), project(angle, cos(a), 0, sin(a)),
                 INK_HIGHLIGHT, INK_HAT);
        }
        for (int k = 0; k < 6; k++)
            edge(pass, quill(angle, k / 6.0, 0), quill(angle, (k + 1) / 6.0, 0), INK_CYAN, INK_TEAL);
        for (int k = 1; k < 6; k++)
            for (int side = -1; side <= 1; side += 2)
                edge(pass, quill(angle, k / 6.0, 0), quill(angle, k / 6.0, side * 0.17), INK_CYAN, INK_TEAL);
    }
}
