// tinyhat-retro: a small SDL3 frontend for libretro emulator cores.
// Usage: tinyhat-retro core.so game
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "libretro.h"

#define PORTS 2

static void (*core_init)(void);
static void (*core_deinit)(void);
static void (*core_get_system_info)(struct retro_system_info *);
static void (*core_get_system_av_info)(struct retro_system_av_info *);
static void (*core_set_environment)(retro_environment_t);
static void (*core_set_video_refresh)(retro_video_refresh_t);
static void (*core_set_audio_sample)(retro_audio_sample_t);
static void (*core_set_audio_sample_batch)(retro_audio_sample_batch_t);
static void (*core_set_input_poll)(retro_input_poll_t);
static void (*core_set_input_state)(retro_input_state_t);
static void (*core_set_controller_port_device)(unsigned, unsigned);
static void (*core_run)(void);
static void (*core_reset)(void);
static bool (*core_load_game)(const struct retro_game_info *);
static void (*core_unload_game)(void);
static size_t (*core_serialize_size)(void);
static bool (*core_serialize)(void *, size_t);
static bool (*core_unserialize)(const void *, size_t);
static void *(*core_get_memory_data)(unsigned);
static size_t (*core_get_memory_size)(unsigned);

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static SDL_AudioStream *audio;
static SDL_Gamepad *pads[PORTS];
static SDL_PixelFormat format = SDL_PIXELFORMAT_XRGB1555;
static int bytes_per_pixel = 2;
static int frame_w, frame_h;
static float aspect = 4.0f / 3.0f;
static char data_dir[1024];
static char save_base[2048];

static void core_log(enum retro_log_level level, const char *fmt, ...)
{
    if (level < RETRO_LOG_WARN) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static bool environment(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = core_log;
        return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *(bool *)data = true;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        switch (*(enum retro_pixel_format *)data) {
        case RETRO_PIXEL_FORMAT_0RGB1555:
            format = SDL_PIXELFORMAT_XRGB1555;
            bytes_per_pixel = 2;
            return true;
        case RETRO_PIXEL_FORMAT_RGB565:
            format = SDL_PIXELFORMAT_RGB565;
            bytes_per_pixel = 2;
            return true;
        case RETRO_PIXEL_FORMAT_XRGB8888:
            format = SDL_PIXELFORMAT_XRGB8888;
            bytes_per_pixel = 4;
            return true;
        default:
            return false;
        }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = data_dir;
        return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY: {
        const struct retro_game_geometry *g = data;
        if (g->aspect_ratio > 0) {
            aspect = g->aspect_ratio;
        }
        return true;
    }
    default:
        return false;
    }
}

static void video_refresh(const void *data, unsigned width, unsigned height, size_t pitch)
{
    if (!data) {
        return;
    }
    if (!texture || (int)width != frame_w || (int)height != frame_h || texture->format != format) {
        SDL_DestroyTexture(texture);
        texture = SDL_CreateTexture(renderer, format, SDL_TEXTUREACCESS_STREAMING, width, height);
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
        frame_w = width;
        frame_h = height;
    }
    SDL_UpdateTexture(texture, NULL, data, (int)pitch);
}

static size_t audio_batch(const int16_t *data, size_t frames)
{
    if (audio) {
        SDL_PutAudioStreamData(audio, data, (int)(frames * 4));
    }
    return frames;
}

static void audio_sample(int16_t left, int16_t right)
{
    int16_t frame[2] = { left, right };
    if (audio) {
        SDL_PutAudioStreamData(audio, frame, 4);
    }
}

static void input_poll(void)
{
}

static bool key(SDL_Scancode a, SDL_Scancode b)
{
    const bool *keys = SDL_GetKeyboardState(NULL);
    return keys[a] || (b != SDL_SCANCODE_UNKNOWN && keys[b]);
}

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)index;
    if (port >= PORTS || (device & RETRO_DEVICE_MASK) != RETRO_DEVICE_JOYPAD) {
        return 0;
    }
    SDL_Gamepad *pad = pads[port];
    if (pad) {
        switch (id) {
        case RETRO_DEVICE_ID_JOYPAD_UP:
            if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP) || SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) < -16000) return 1;
            break;
        case RETRO_DEVICE_ID_JOYPAD_DOWN:
            if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) > 16000) return 1;
            break;
        case RETRO_DEVICE_ID_JOYPAD_LEFT:
            if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT) || SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) < -16000) return 1;
            break;
        case RETRO_DEVICE_ID_JOYPAD_RIGHT:
            if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) > 16000) return 1;
            break;
        case RETRO_DEVICE_ID_JOYPAD_B: if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH)) return 1; break;
        case RETRO_DEVICE_ID_JOYPAD_A: if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST)) return 1; break;
        case RETRO_DEVICE_ID_JOYPAD_Y: if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST)) return 1; break;
        case RETRO_DEVICE_ID_JOYPAD_X: if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_NORTH)) return 1; break;
        case RETRO_DEVICE_ID_JOYPAD_L: if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) return 1; break;
        case RETRO_DEVICE_ID_JOYPAD_R: if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) return 1; break;
        case RETRO_DEVICE_ID_JOYPAD_START: if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START)) return 1; break;
        case RETRO_DEVICE_ID_JOYPAD_SELECT: if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK)) return 1; break;
        }
    }
    if (port != 0) {
        return 0;
    }
    switch (id) {
    case RETRO_DEVICE_ID_JOYPAD_UP: return key(SDL_SCANCODE_UP, SDL_SCANCODE_UNKNOWN);
    case RETRO_DEVICE_ID_JOYPAD_DOWN: return key(SDL_SCANCODE_DOWN, SDL_SCANCODE_UNKNOWN);
    case RETRO_DEVICE_ID_JOYPAD_LEFT: return key(SDL_SCANCODE_LEFT, SDL_SCANCODE_UNKNOWN);
    case RETRO_DEVICE_ID_JOYPAD_RIGHT: return key(SDL_SCANCODE_RIGHT, SDL_SCANCODE_UNKNOWN);
    case RETRO_DEVICE_ID_JOYPAD_B: return key(SDL_SCANCODE_Z, SDL_SCANCODE_UNKNOWN);
    case RETRO_DEVICE_ID_JOYPAD_A: return key(SDL_SCANCODE_X, SDL_SCANCODE_UNKNOWN);
    case RETRO_DEVICE_ID_JOYPAD_Y: return key(SDL_SCANCODE_A, SDL_SCANCODE_UNKNOWN);
    case RETRO_DEVICE_ID_JOYPAD_X: return key(SDL_SCANCODE_S, SDL_SCANCODE_UNKNOWN);
    case RETRO_DEVICE_ID_JOYPAD_L: return key(SDL_SCANCODE_Q, SDL_SCANCODE_UNKNOWN);
    case RETRO_DEVICE_ID_JOYPAD_R: return key(SDL_SCANCODE_W, SDL_SCANCODE_UNKNOWN);
    case RETRO_DEVICE_ID_JOYPAD_START: return key(SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_ENTER);
    case RETRO_DEVICE_ID_JOYPAD_SELECT: return key(SDL_SCANCODE_RSHIFT, SDL_SCANCODE_BACKSPACE);
    }
    return 0;
}

static void *load_symbol(void *core, const char *name)
{
    void *sym = dlsym(core, name);
    if (!sym) {
        fprintf(stderr, "tinyhat-retro: the core has no %s\n", name);
        exit(1);
    }
    return sym;
}

#define LOAD(var, name) *(void **)&var = load_symbol(core, name)

static void open_pads(void)
{
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    for (int port = 0; port < PORTS; port++) {
        if (pads[port] && !SDL_GamepadConnected(pads[port])) {
            SDL_CloseGamepad(pads[port]);
            pads[port] = NULL;
        }
    }
    for (int i = 0; ids && i < count; i++) {
        bool used = false;
        for (int port = 0; port < PORTS; port++) {
            if (pads[port] && SDL_GetGamepadID(pads[port]) == ids[i]) {
                used = true;
            }
        }
        for (int port = 0; !used && port < PORTS; port++) {
            if (!pads[port]) {
                pads[port] = SDL_OpenGamepad(ids[i]);
                used = true;
            }
        }
    }
    SDL_free(ids);
}

static bool write_file(const char *path, const void *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        return false;
    }
    bool ok = fwrite(data, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}

static void save_memory(void)
{
    void *sram = core_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t size = core_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (sram && size) {
        char path[2100];
        snprintf(path, sizeof(path), "%s.srm", save_base);
        write_file(path, sram, size);
    }
}

static void load_memory(void)
{
    void *sram = core_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t size = core_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    char path[2100];
    snprintf(path, sizeof(path), "%s.srm", save_base);
    size_t got = 0;
    void *data = sram && size ? SDL_LoadFile(path, &got) : NULL;
    if (data && got == size) {
        memcpy(sram, data, size);
    }
    SDL_free(data);
}

static void state(bool save)
{
    char path[2100];
    snprintf(path, sizeof(path), "%s.state", save_base);
    if (save) {
        size_t size = core_serialize_size();
        void *buf = size ? malloc(size) : NULL;
        if (buf && core_serialize(buf, size) && write_file(path, buf, size)) {
            SDL_SetWindowTitle(window, "Saved the game state (F9 loads it)");
        }
        free(buf);
        return;
    }
    size_t size = 0;
    void *buf = SDL_LoadFile(path, &size);
    if (buf && core_unserialize(buf, size)) {
        SDL_SetWindowTitle(window, "Loaded the game state");
    }
    SDL_free(buf);
}

static void draw(void)
{
    int w, h;
    SDL_GetRenderOutputSize(renderer, &w, &h);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    if (texture) {
        SDL_FRect dst;
        dst.h = (float)h;
        dst.w = dst.h * aspect;
        if (dst.w > w) {
            dst.w = (float)w;
            dst.h = dst.w / aspect;
        }
        dst.x = (w - dst.w) / 2;
        dst.y = (h - dst.h) / 2;
        SDL_RenderTexture(renderer, texture, NULL, &dst);
    }
    SDL_RenderPresent(renderer);
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: tinyhat-retro core.so game\n");
        return 1;
    }
    void *core = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!core) {
        fprintf(stderr, "tinyhat-retro: %s\n", dlerror());
        return 1;
    }
    LOAD(core_init, "retro_init");
    LOAD(core_deinit, "retro_deinit");
    LOAD(core_get_system_info, "retro_get_system_info");
    LOAD(core_get_system_av_info, "retro_get_system_av_info");
    LOAD(core_set_environment, "retro_set_environment");
    LOAD(core_set_video_refresh, "retro_set_video_refresh");
    LOAD(core_set_audio_sample, "retro_set_audio_sample");
    LOAD(core_set_audio_sample_batch, "retro_set_audio_sample_batch");
    LOAD(core_set_input_poll, "retro_set_input_poll");
    LOAD(core_set_input_state, "retro_set_input_state");
    LOAD(core_set_controller_port_device, "retro_set_controller_port_device");
    LOAD(core_run, "retro_run");
    LOAD(core_reset, "retro_reset");
    LOAD(core_load_game, "retro_load_game");
    LOAD(core_unload_game, "retro_unload_game");
    LOAD(core_serialize_size, "retro_serialize_size");
    LOAD(core_serialize, "retro_serialize");
    LOAD(core_unserialize, "retro_unserialize");
    LOAD(core_get_memory_data, "retro_get_memory_data");
    LOAD(core_get_memory_size, "retro_get_memory_size");

    const char *home = getenv("HOME");
    snprintf(data_dir, sizeof(data_dir), "%s/.local/share/tinyhat/retro", home ? home : ".");
    for (char *p = data_dir + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(data_dir, 0755);
            *p = '/';
        }
    }
    mkdir(data_dir, 0755);
    const char *base = strrchr(argv[2], '/');
    snprintf(save_base, sizeof(save_base), "%s/%s", data_dir, base ? base + 1 : argv[2]);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "tinyhat-retro: %s\n", SDL_GetError());
        return 1;
    }

    core_set_environment(environment);
    core_init();
    core_set_video_refresh(video_refresh);
    core_set_audio_sample(audio_sample);
    core_set_audio_sample_batch(audio_batch);
    core_set_input_poll(input_poll);
    core_set_input_state(input_state);

    struct retro_system_info info = { 0 };
    core_get_system_info(&info);
    struct retro_game_info game = { .path = argv[2] };
    void *rom = NULL;
    if (!info.need_fullpath) {
        size_t size = 0;
        rom = SDL_LoadFile(argv[2], &size);
        if (!rom) {
            fprintf(stderr, "tinyhat-retro: %s\n", SDL_GetError());
            return 1;
        }
        game.data = rom;
        game.size = size;
    }
    if (!core_load_game(&game)) {
        fprintf(stderr, "tinyhat-retro: %s could not load %s\n", info.library_name, argv[2]);
        return 1;
    }
    for (unsigned port = 0; port < PORTS; port++) {
        core_set_controller_port_device(port, RETRO_DEVICE_JOYPAD);
    }

    struct retro_system_av_info av = { 0 };
    core_get_system_av_info(&av);
    if (av.geometry.aspect_ratio > 0) {
        aspect = av.geometry.aspect_ratio;
    } else if (av.geometry.base_height) {
        aspect = (float)av.geometry.base_width / av.geometry.base_height;
    }
    char title[256];
    snprintf(title, sizeof(title), "%s - %s", base ? base + 1 : argv[2], info.library_name);
    window = SDL_CreateWindow(title, (int)(av.geometry.base_height * aspect * 3), (int)av.geometry.base_height * 3, SDL_WINDOW_RESIZABLE);
    renderer = window ? SDL_CreateRenderer(window, NULL) : NULL;
    if (!renderer) {
        fprintf(stderr, "tinyhat-retro: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    SDL_AudioSpec spec = { SDL_AUDIO_S16, 2, (int)av.timing.sample_rate };
    audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (audio) {
        SDL_ResumeAudioStreamDevice(audio);
    }
    load_memory();
    open_pads();

    const double fps = av.timing.fps > 0 ? av.timing.fps : 60.0;
    const Uint64 frame_ns = (Uint64)(1e9 / fps);
    const int max_queued = (int)(av.timing.sample_rate * 4 / 10);
    Uint64 next = SDL_GetTicksNS();
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (e.type == SDL_EVENT_GAMEPAD_ADDED || e.type == SDL_EVENT_GAMEPAD_REMOVED) {
                open_pads();
            } else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                switch (e.key.key) {
                case SDLK_ESCAPE: running = false; break;
                case SDLK_F11: SDL_SetWindowFullscreen(window, !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)); break;
                case SDLK_RETURN:
                    if (e.key.mod & SDL_KMOD_ALT) {
                        SDL_SetWindowFullscreen(window, !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN));
                    }
                    break;
                case SDLK_F1: core_reset(); break;
                case SDLK_F5: state(true); break;
                case SDLK_F9: state(false); break;
                }
            }
        }
        if (audio && SDL_GetAudioStreamQueued(audio) > max_queued) {
            SDL_Delay(1);
            continue;
        }
        core_run();
        draw();
        next += frame_ns;
        Uint64 now = SDL_GetTicksNS();
        if (next > now) {
            SDL_DelayPrecise(next - now);
        } else if (now - next > frame_ns * 4) {
            next = now;
        }
    }

    save_memory();
    core_unload_game();
    core_deinit();
    SDL_free(rom);
    SDL_Quit();
    return 0;
}
