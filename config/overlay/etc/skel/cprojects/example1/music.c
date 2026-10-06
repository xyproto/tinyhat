// Plays a little tune with FluidSynth and the General MIDI soundfont, the same way ScummVM and DOSBox-X make music.
#include <SDL3/SDL.h>
#include <fluidsynth.h>
#include "music.h"

#define RATE 44100
#define TEMPO 132
#define STEP_FRAMES (RATE * 60 / TEMPO / 2)
#define STEPS 32
#define CHUNK 512

// One step is an eighth note. 0 means: keep playing the previous note.
static const unsigned char melody[STEPS] = {
    76, 79, 84, 79, 76, 79, 72, 74,
    76, 81, 84, 81, 76, 72, 69, 72,
    77, 81, 84, 81, 77, 76, 74, 72,
    74, 79, 83, 79, 74, 71, 74, 79,
};

static const unsigned char bass[STEPS] = {
    36, 0, 43, 0, 36, 0, 43, 0,
    33, 0, 40, 0, 33, 0, 40, 0,
    41, 0, 48, 0, 41, 0, 48, 0,
    43, 0, 50, 0, 43, 0, 47, 0,
};

static fluid_settings_t *settings;
static fluid_synth_t *synth;
static SDL_AudioStream *stream;
static int step, frames_left, melody_note, bass_note;

static void play_step(void)
{
    if (melody[step]) {
        fluid_synth_noteoff(synth, 0, melody_note);
        melody_note = melody[step];
        fluid_synth_noteon(synth, 0, melody_note, 80);
    }
    if (bass[step]) {
        fluid_synth_noteoff(synth, 1, bass_note);
        bass_note = bass[step];
        fluid_synth_noteon(synth, 1, bass_note, 90);
    }
    fluid_synth_noteon(synth, 9, 42, 35);
    if (step % 4 == 0) {
        fluid_synth_noteon(synth, 9, 36, 90);
    } else if (step % 8 == 6) {
        fluid_synth_noteon(synth, 9, 38, 70);
    }
    step = (step + 1) % STEPS;
}

// SDL calls this from its audio thread whenever it needs more sound.
static void SDLCALL feed(void *userdata, SDL_AudioStream *s, int additional, int total)
{
    float buf[CHUNK * 2];
    int frames = additional / (int)(2 * sizeof(float));

    while (frames > 0) {
        if (frames_left == 0) {
            play_step();
            frames_left = STEP_FRAMES;
        }
        int n = SDL_min(frames, SDL_min(frames_left, CHUNK));
        fluid_synth_write_float(synth, n, buf, 0, 2, buf, 1, 2);
        SDL_PutAudioStreamData(s, buf, n * 2 * (int)sizeof(float));
        frames -= n;
        frames_left -= n;
    }
}

bool music_start(void)
{
    SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, RATE };

    settings = new_fluid_settings();
    fluid_settings_setnum(settings, "synth.sample-rate", RATE);
    fluid_settings_setnum(settings, "synth.gain", 0.5);
    fluid_settings_setint(settings, "synth.dynamic-sample-loading", 1);
    fluid_settings_setint(settings, "synth.polyphony", 32);
    fluid_settings_setint(settings, "synth.reverb.active", 0);
    fluid_settings_setint(settings, "synth.chorus.active", 0);
    synth = new_fluid_synth(settings);
    if (fluid_synth_sfload(synth, "/usr/share/soundfonts/default.sf2", 1) == FLUID_FAILED) {
        SDL_Log("Could not load the soundfont, playing without music");
        music_stop();
        return false;
    }
    fluid_synth_program_change(synth, 0, 10);
    fluid_synth_program_change(synth, 1, 33);

    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, NULL);
    if (!stream) {
        SDL_Log("Could not open the audio device: %s", SDL_GetError());
        music_stop();
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream);
    return true;
}

void music_bounce(void)
{
    if (stream) {
        fluid_synth_noteon(synth, 9, 76, 110);
    }
}

void music_stop(void)
{
    SDL_DestroyAudioStream(stream);
    stream = NULL;
    if (synth) {
        delete_fluid_synth(synth);
        synth = NULL;
    }
    if (settings) {
        delete_fluid_settings(settings);
        settings = NULL;
    }
}
