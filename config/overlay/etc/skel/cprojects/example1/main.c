// Press Ctrl+Space in the editor to build, or press it twice quickly to build and run. Esc quits.
#include <SDL3/SDL.h>
#include "music.h"

#define W 320
#define H 200
#define STARS 60

typedef struct {
    float x, y, phase;
} Star;

// Calls fn with a pointer to the variable when it goes out of scope, a bit like "defer" in Go.
// Variables are cleaned up in the opposite order of how they were declared.
#define CLEANUP(fn) __attribute__((cleanup(fn)))

static void quit_sdl(bool *initialized)
{
    if (*initialized) {
        SDL_Quit();
    }
}

static void destroy_window(SDL_Window **window)
{
    if (*window) {
        SDL_DestroyWindow(*window);
    }
}

static void destroy_renderer(SDL_Renderer **renderer)
{
    if (*renderer) {
        SDL_DestroyRenderer(*renderer);
    }
}

static void stop_music(bool *playing)
{
    if (*playing) {
        music_stop();
    }
}

static void draw_sky(SDL_Renderer *renderer)
{
    for (int y = 0; y < H; y++) {
        float t = (float)y / H;
        SDL_SetRenderDrawColor(renderer, (Uint8)(10 + 20 * t), (Uint8)(20 + 90 * t), (Uint8)(40 + 90 * t), 0xff);
        SDL_RenderLine(renderer, 0, (float)y, W, (float)y);
    }
}

static void draw_stars(SDL_Renderer *renderer, const Star *stars, float time)
{
    for (int i = 0; i < STARS; i++) {
        Uint8 v = (Uint8)(150 + 105 * SDL_sinf(time * 3 + stars[i].phase));
        SDL_SetRenderDrawColor(renderer, v, v, v, 0xff);
        SDL_RenderPoint(renderer, stars[i].x, stars[i].y);
    }
}

static void draw_hat(SDL_Renderer *renderer, float x, float y)
{
    SDL_FRect shadow = { x + 4, H - 14, 52, 4 };
    SDL_FRect crown = { x + 12, y, 28, 30 };
    SDL_FRect band = { x + 12, y + 22, 28, 4 };
    SDL_FRect brim = { x, y + 30, 52, 6 };

    SDL_SetRenderDrawColor(renderer, 0x08, 0x10, 0x18, 0xff);
    SDL_RenderFillRect(renderer, &shadow);
    SDL_SetRenderDrawColor(renderer, 0xc0, 0x39, 0x2b, 0xff);
    SDL_RenderFillRect(renderer, &crown);
    SDL_RenderFillRect(renderer, &brim);
    SDL_SetRenderDrawColor(renderer, 0x1a, 0x9e, 0x96, 0xff);
    SDL_RenderFillRect(renderer, &band);
}

static void draw_centered_text(SDL_Renderer *renderer, const char *text, float y)
{
    float x = (W - SDL_strlen(text) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE) / 2.0f;
    SDL_RenderDebugText(renderer, x, y, text);
}

int main(int argc, char *argv[])
{
    Star stars[STARS];
    float x = 40, y = 40, dx = 1.5f, dy = 0;
    bool running = true;

    // A larger audio buffer gives a slow CPU more time to make the music
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "2048");
    CLEANUP(quit_sdl) bool initialized = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    if (!initialized) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    CLEANUP(destroy_window) SDL_Window *window = NULL;
    CLEANUP(destroy_renderer) SDL_Renderer *renderer = NULL;
    if (!SDL_CreateWindowAndRenderer("Tiny Hat", 640, 400, SDL_WINDOW_FULLSCREEN, &window, &renderer)) {
        SDL_Log("Could not create a window: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderLogicalPresentation(renderer, W, H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderVSync(renderer, 1);
    SDL_HideCursor();
    CLEANUP(stop_music) bool playing = music_start();

    for (int i = 0; i < STARS; i++) {
        stars[i].x = (float)SDL_rand(W);
        stars[i].y = (float)SDL_rand(H / 2);
        stars[i].phase = SDL_randf() * 6.28f;
    }

    while (running) {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                switch (event.key.key) {
                case SDLK_ESCAPE:
                    running = false;
                    break;
                case SDLK_LEFT:
                    dx -= 1.5f;
                    break;
                case SDLK_RIGHT:
                    dx += 1.5f;
                    break;
                case SDLK_UP:
                case SDLK_SPACE:
                    dy = -5;
                    break;
                }
            }
        }

        dy += 0.15f;
        x += dx;
        y += dy;
        if (x < 0 || x > W - 52) {
            dx = -dx;
            x = x < 0 ? 0 : W - 52;
        }
        if (y > H - 50) {
            y = H - 50;
            dy = -dy * 0.85f;
            music_bounce();
            if (dy > -2) {
                dy = -5;
            }
        }

        draw_sky(renderer);
        draw_stars(renderer, stars, SDL_GetTicks() / 1000.0f);
        SDL_SetRenderDrawColor(renderer, 0x14, 0x40, 0x30, 0xff);
        SDL_RenderFillRect(renderer, &(SDL_FRect){ 0, H - 12, W, 12 });
        draw_hat(renderer, x, y);
        SDL_SetRenderDrawColor(renderer, 0xff, 0xff, 0xff, 0xff);
        draw_centered_text(renderer, "TINY HAT", 16);
        SDL_SetRenderDrawColor(renderer, 0xb0, 0xc8, 0xc8, 0xff);
        draw_centered_text(renderer, "Arrows/Space: kick the hat  Esc: quit", H - 9);
        SDL_RenderPresent(renderer);
    }

    return 0;
}
