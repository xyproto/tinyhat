#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <termios.h>
#include <unistd.h>

#include "qrcodegen.h"

#define REPO "https://github.com/xyproto/tinyhat/issues/new"
#define MAXLOG 12
#define LINE 160

static char logs[MAXLOG][LINE];
static int nlogs;
static struct termios saved;
static int raw;

static uint32_t crc32(const char* s)
{
    uint32_t c = 0xffffffffu;
    for (; *s; s++) {
        c ^= (unsigned char)*s;
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return ~c;
}

static void read_logs(const char* path)
{
    static char ring[64][LINE];
    int n = 0;
    FILE* f = path && *path ? fopen(path, "r") : NULL;
    if (!f)
        return;
    while (fgets(ring[n % 64], LINE, f)) {
        ring[n % 64][strcspn(ring[n % 64], "\r\n")] = '\0';
        if (ring[n % 64][0])
            n++;
    }
    fclose(f);
    int first = n > MAXLOG ? n - MAXLOG : 0;
    for (int i = first; i < n; i++)
        snprintf(logs[nlogs++], LINE, "%s", ring[i % 64]);
}

static void os_field(const char* key, char* out, size_t len)
{
    char line[256];
    size_t k = strlen(key);
    FILE* f = fopen("/etc/os-release", "r");
    snprintf(out, len, "?");
    if (!f)
        return;
    while (fgets(line, sizeof(line), f)) {
        if (!strncmp(line, key, k) && line[k] == '=') {
            char* v = line + k + 1;
            v[strcspn(v, "\r\n")] = '\0';
            if (*v == '"') {
                v++;
                v[strcspn(v, "\"")] = '\0';
            }
            snprintf(out, len, "%s", v);
            break;
        }
    }
    fclose(f);
}

static void urlenc(char* out, size_t len, const char* in)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = strlen(out);
    for (; *in && o + 4 < len; in++) {
        unsigned char c = (unsigned char)*in;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("-_.~", c)) {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        }
    }
    out[o] = '\0';
}

static void restore(void)
{
    if (raw)
        tcsetattr(0, TCSANOW, &saved);
    printf("\033[0m\033[?25h");
    fflush(stdout);
}

static void border(int cols, int on)
{
    printf("\033[1;1H\033[%sm%*s\033[0m", on ? "41" : "40", cols, "");
    printf("\033[6;1H\033[%sm%*s\033[0m", on ? "41" : "40", cols, "");
    for (int r = 2; r <= 5; r++) {
        printf("\033[%d;1H\033[%sm  \033[0m", r, on ? "41" : "40");
        printf("\033[%d;%dH\033[%sm  \033[0m", r, cols - 1, on ? "41" : "40");
    }
    fflush(stdout);
}

static void centered(int row, int cols, const char* s)
{
    int len = (int)strlen(s);
    printf("\033[%d;%dH%s", row, len < cols ? (cols - len) / 2 + 1 : 1, s);
}

static int draw_qr(const char* url, int top, int left, int rows, int cols)
{
    static uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
    static uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
    if (!qrcodegen_encodeText(url, tmp, qr, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
            qrcodegen_Mask_AUTO, true))
        return 0;
    int size = qrcodegen_getSize(qr);
    int quiet = 2;
    int w = size + 2 * quiet;
    if (w > cols - left || (w + 1) / 2 > rows - top)
        return 0;
    for (int y = -quiet, r = 0; y < size + quiet; y += 2, r++) {
        printf("\033[%d;%dH\033[30;47m", top + r, left);
        for (int x = -quiet; x < size + quiet; x++) {
            int a = qrcodegen_getModule(qr, x, y);
            int b = qrcodegen_getModule(qr, x, y + 1);
            printf("%s", a && b ? "█" : a ? "▀" : b ? "▄" : " ");
        }
        printf("\033[0m");
    }
    return w;
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s WHAT CLASS [LOGFILE] [KEYS]\n", argv[0]);
        return 2;
    }
    const char* what = argv[1];
    unsigned long cls = strtoul(argv[2], NULL, 16);
    read_logs(argc > 3 ? argv[3] : NULL);
    const char* keys = argc > 4 ? argv[4] : "Enter=Continue";

    char joined[MAXLOG * LINE] = "";
    for (int i = 0; i < nlogs; i++) {
        strcat(joined, logs[i]);
        strcat(joined, "\n");
    }
    uint32_t sum = crc32(joined[0] ? joined : what);
    char code[40];
    snprintf(code, sizeof(code), "%08lX.%08X", cls & 0xffffffffUL, sum);

    struct utsname u;
    uname(&u);
    char version[64];
    os_field("VERSION_ID", version, sizeof(version));

    struct winsize ws = { 0 };
    int rows = 25, cols = 80;
    if (!ioctl(1, TIOCGWINSZ, &ws) && ws.ws_row && ws.ws_col) {
        rows = ws.ws_row;
        cols = ws.ws_col;
    }

    if (isatty(0) && !tcgetattr(0, &saved)) {
        struct termios t = saved;
        t.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
        t.c_cc[VMIN] = 1;
        t.c_cc[VTIME] = 0;
        raw = !tcsetattr(0, TCSANOW, &t);
    }
    atexit(restore);

    printf("\033[0m\033[40m\033[2J\033[?25l\033[31;40m");
    centered(3, cols, "Software Failure.   Press a key below to continue.");
    char guru[64];
    snprintf(guru, sizeof(guru), "Guru Meditation #%s", code);
    centered(4, cols, guru);
    printf("\033[0;37;40m");
    printf("\033[8;3H\033[1m%s\033[0;37;40m", what);
    printf("\033[9;3HTiny Hat %s, Linux %s %s", version, u.release, u.machine);

    int row = 11;
    printf("\033[%d;3H\033[1mKeys\033[0;37;40m", row++);
    char kbuf[256];
    snprintf(kbuf, sizeof(kbuf), "%s", keys);
    for (char* k = strtok(kbuf, ","); k; k = strtok(NULL, ",")) {
        char* eq = strchr(k, '=');
        if (eq) {
            *eq = '\0';
            printf("\033[%d;3H  \033[36m%-8s\033[37m %s", row++, k, eq + 1);
        }
    }

    int wide = cols >= 120;
    int qrtop = wide ? 8 : row + 1;
    int qrleft = wide ? cols / 2 + 2 : 3;
    char body[2048] = "";
    char url[3072];
    int shown = nlogs < 4 ? nlogs : 4;
    int qrw = 0;
    for (;;) {
        char text[1600];
        snprintf(text, sizeof(text), "Tiny Hat %s, Linux %s %s\n%s\n\n", version, u.release, u.machine, what);
        for (int i = nlogs - shown; i < nlogs; i++) {
            strncat(text, logs[i], sizeof(text) - strlen(text) - 2);
            strcat(text, "\n");
        }
        snprintf(url, sizeof(url), "%s?title=", REPO);
        char title[160];
        snprintf(title, sizeof(title), "Guru Meditation #%s: %s", code, what);
        urlenc(url, sizeof(url), title);
        strncat(url, "&body=", sizeof(url) - strlen(url) - 1);
        body[0] = '\0';
        urlenc(body, sizeof(body), text);
        strncat(url, body, sizeof(url) - strlen(url) - 1);
        qrw = draw_qr(url, qrtop, qrleft, rows - 1, cols);
        if (qrw || shown == 0)
            break;
        shown--;
    }
    if (nlogs && (wide || !qrw) && row + 3 < rows) {
        int w = (wide ? cols / 2 - 2 : cols) - 4;
        printf("\033[%d;3H\033[1mLast messages\033[0;90;40m", row + 1);
        for (int i = 0; i < nlogs && row + 2 + i < rows; i++)
            printf("\033[%d;3H%.*s", row + 2 + i, w > 10 ? w : 10, logs[i]);
        printf("\033[37;40m");
    }
    if (qrw)
        printf("\033[%d;%dH\033[37;40mScan to report this on GitHub", qrtop - 1, qrleft);
    else
        printf("\033[%d;3H\033[37;40mReport it: %s", rows, REPO);
    fflush(stdout);

    int on = 1;
    for (;;) {
        border(cols, on);
        on = !on;
        struct pollfd p = { .fd = 0, .events = POLLIN };
        int r = poll(&p, 1, 800);
        if (r > 0) {
            char c = 0;
            if (read(0, &c, 1) == 1) {
                printf("\033[0m\033[2J\033[H");
                fflush(stdout);
                restore();
                raw = 0;
                if (c == '\r')
                    c = '\n';
                if (c >= 'A' && c <= 'Z')
                    c = (char)(c - 'A' + 'a');
                return (unsigned char)c;
            }
        } else if (r < 0) {
            return 1;
        }
    }
}
