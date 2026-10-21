// Shows the boot animation in a window, by running ./hat the same way as the boot splash does. Esc quits.
// With --test, it only checks that ./hat answers quickly enough to be used at boot.
#include <SDL3/SDL.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "hat.h"

#define FRAME_MS 80
#define FIRST_TIMEOUT_MS 3000
#define TIMEOUT_MS 500
#define TEST_FRAMES 150
#define SCALE 8

unsigned char low[LOW * LOW];

static pid_t pid = -1;
static int to_hat = -1, from_hat = -1;

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static bool start(void)
{
    int in[2], out[2];
    if (pipe(in))
        return false;
    if (pipe(out)) {
        close(in[0]);
        close(in[1]);
        return false;
    }
    pid = fork();
    if (pid == 0) {
        dup2(in[0], 0);
        dup2(out[1], 1);
        close(in[0]);
        close(in[1]);
        close(out[0]);
        close(out[1]);
        execl("./hat", "hat", (char *)NULL);
        _exit(127);
    }
    close(in[0]);
    close(out[1]);
    to_hat = in[1];
    from_hat = out[0];
    return pid > 0;
}

static void stop(void)
{
    close(to_hat);
    close(from_hat);
    if (pid > 0) {
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
    }
}

static const char *frame(int32_t tick, int timeout_ms)
{
    long deadline = now_ms() + timeout_ms;
    size_t have = 0;
    if (write(to_hat, &tick, sizeof(tick)) != sizeof(tick))
        return "./hat stopped";
    while (have < sizeof(low)) {
        struct pollfd p = { from_hat, POLLIN, 0 };
        long left = deadline - now_ms();
        if (left <= 0 || poll(&p, 1, (int)left) <= 0)
            return "./hat took too long to draw a frame";
        ssize_t n = read(from_hat, low + have, sizeof(low) - have);
        if (n <= 0)
            return "./hat stopped or crashed";
        have += (size_t)n;
    }
    for (size_t i = 0; i < sizeof(low); i++)
        if (low[i] >= INKS)
            return "./hat drew with an ink that does not exist";
    return NULL;
}

static const char *show(void)
{
    static const Uint32 colors[INKS] = INK_COLORS;
    static Uint32 pixels[LOW * LOW];
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    const char *err = NULL;
    if (!SDL_Init(SDL_INIT_VIDEO))
        return "could not start SDL";
    if (!SDL_CreateWindowAndRenderer("Tiny Hat boot animation", LOW * SCALE, LOW * SCALE, SDL_WINDOW_RESIZABLE, &window,
                                     &renderer) ||
        !(texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, LOW, LOW))) {
        SDL_Quit();
        return "could not open a window";
    }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    SDL_SetRenderLogicalPresentation(renderer, LOW, LOW, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    for (bool first = true, running = true; running && !err; first = false) {
        long begin = now_ms();
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE))
                running = false;
        if ((err = frame((int32_t)(begin / FRAME_MS), first ? FIRST_TIMEOUT_MS : TIMEOUT_MS)))
            break;
        for (int i = 0; i < LOW * LOW; i++)
            pixels[i] = colors[low[i]];
        SDL_UpdateTexture(texture, NULL, pixels, LOW * sizeof(Uint32));
        SDL_SetRenderDrawColor(renderer, 0x1a, 0x1b, 0x26, 0xff);
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        long spent = now_ms() - begin;
        if (spent < FRAME_MS)
            SDL_Delay((Uint32)(FRAME_MS - spent));
    }
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return err;
}

int main(int argc, char **argv)
{
    bool test = argc > 1 && !strcmp(argv[1], "--test");
    const char *err = NULL;
    signal(SIGPIPE, SIG_IGN);
    if (!start()) {
        fprintf(stderr, "preview: could not start ./hat\n");
        return 1;
    }
    if (test) {
        for (int i = 0; i < TEST_FRAMES && !err; i++)
            err = frame(i, i ? TIMEOUT_MS : FIRST_TIMEOUT_MS);
    } else {
        err = show();
    }
    stop();
    if (err) {
        fprintf(stderr, "preview: %s\n", err);
        return 1;
    }
    return 0;
}
