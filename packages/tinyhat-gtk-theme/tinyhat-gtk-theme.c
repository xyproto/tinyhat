#include <ctype.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void tohsl(double r, double g, double b, double *h, double *s, double *l)
{
	double mx = fmax(r, fmax(g, b)), mn = fmin(r, fmin(g, b)), d = mx - mn;
	*l = (mx + mn) / 2;
	*h = 0;
	*s = d == 0 ? 0 : d / (1 - fabs(2 * *l - 1));
	if (d == 0)
		return;
	if (mx == r)
		*h = 60 * fmod((g - b) / d, 6);
	else if (mx == g)
		*h = 60 * ((b - r) / d + 2);
	else
		*h = 60 * ((r - g) / d + 4);
	if (*h < 0)
		*h += 360;
}

static void fromhsl(double h, double s, double l, double *r, double *g, double *b)
{
	double c = (1 - fabs(2 * l - 1)) * s, x = c * (1 - fabs(fmod(h / 60, 2) - 1)), m = l - c / 2;
	double rr = 0, gg = 0, bb = 0;
	if (h < 60)
		rr = c, gg = x;
	else if (h < 120)
		rr = x, gg = c;
	else if (h < 180)
		gg = c, bb = x;
	else if (h < 240)
		gg = x, bb = c;
	else if (h < 300)
		rr = x, bb = c;
	else
		rr = c, bb = x;
	*r = rr + m;
	*g = gg + m;
	*b = bb + m;
}

static void remap(int *r, int *g, int *b)
{
	double h, s, l, nr, ng, nb;
	tohsl(*r / 255.0, *g / 255.0, *b / 255.0, &h, &s, &l);
	if (s < 0.12) {
		if (l > 0.97 || l < 0.01)
			return;
		if (l < 0.5) {
			l = l * 0.62;
			s = 0.18;
		} else {
			l = 0.5 + (l - 0.5) * 0.82;
			s = 0.2 + 0.55 * pow((l - 0.5) / 0.4, 2);
		}
		h = 232;
	} else if (h >= 195 && h <= 235) {
		h = 176;
		s = fmin(1, s * 1.05);
	} else {
		return;
	}
	fromhsl(h, fmin(1, s), fmin(1, l), &nr, &ng, &nb);
	*r = (int)lround(nr * 255);
	*g = (int)lround(ng * 255);
	*b = (int)lround(nb * 255);
}

static int hexval(const char *p, int n)
{
	char buf[3] = { p[0], n == 2 ? p[1] : p[0], 0 };
	return (int)strtol(buf, NULL, 16);
}

int main(void)
{
	GBytes *bytes;
	const char *p, *end;
	gsize len;

	gtk_init_check();
	bytes = g_resources_lookup_data("/org/gtk/libgtk/theme/Default/Default-dark.css", 0, NULL);
	if (!bytes) {
		fputs("tinyhat-gtk-theme: GTK's Default-dark.css was not found\n", stderr);
		return 1;
	}
	p = g_bytes_get_data(bytes, &len);
	end = p + len;
	while (p < end) {
		int r, g, b, n = 0;
		double a;
		if (*p == '#' && (end - p > 6) && isxdigit((unsigned char)p[1])) {
			const char *q = p + 1;
			while (q < end && isxdigit((unsigned char)*q))
				q++;
			if ((q - p == 7 || q - p == 4) && (q == end || !(isalnum((unsigned char)*q) || *q == '-' || *q == '_'))) {
				int two = q - p == 7;
				r = hexval(p + 1, two ? 2 : 1);
				g = hexval(p + (two ? 3 : 2), two ? 2 : 1);
				b = hexval(p + (two ? 5 : 3), two ? 2 : 1);
				remap(&r, &g, &b);
				printf("#%02x%02x%02x", r, g, b);
				p = q;
				continue;
			}
		}
		if (!strncmp(p, "rgba(", 5) && sscanf(p, "rgba(%d, %d, %d, %lf)%n", &r, &g, &b, &a, &n) == 4 && n > 0) {
			remap(&r, &g, &b);
			printf("rgba(%d, %d, %d, %g)", r, g, b, a);
			p += n;
			continue;
		}
		if (!strncmp(p, "rgb(", 4) && sscanf(p, "rgb(%d, %d, %d)%n", &r, &g, &b, &n) == 3 && n > 0) {
			remap(&r, &g, &b);
			printf("rgb(%d, %d, %d)", r, g, b);
			p += n;
			continue;
		}
		putchar(*p++);
	}
	g_bytes_unref(bytes);
	return 0;
}
