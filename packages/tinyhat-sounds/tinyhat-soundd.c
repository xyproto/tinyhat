#define _GNU_SOURCE
#include <SDL3/SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <fluidsynth.h>
#include <math.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum { RATE = 44100, VOICES = 6, EVENTS = 64, IDLE_MS = 2000 };

typedef struct {
	bool startup, typewriter, fart, shutdown;
} config;

typedef struct {
	Uint64 at;
	int chan, note, vel;
} event;

typedef struct {
	bool on;
	float t, dur, f0, f1, phase, wob, lp, seed;
} fart;

extern char **environ;

static fluid_settings_t *settings;
static fluid_synth_t *synth;
static SDL_AudioStream *stream;
static SDL_Mutex *lock;
static Uint64 frame;
static event events[EVENTS];
static int nevents;
static fart farts[VOICES];
static volatile sig_atomic_t reload, quit;
static char rundir[256], fifo[300], pidfile[300];

static void config_path(char *buf, size_t size)
{
	auto xdg = getenv("XDG_CONFIG_HOME");
	if (xdg && *xdg)
		snprintf(buf, size, "%s/tinyhat/sounds", xdg);
	else
		snprintf(buf, size, "%s/.config/tinyhat/sounds", getenv("HOME") ? getenv("HOME") : "/tmp");
}

static config load(void)
{
	config c = { 0 };
	char path[512], line[128], key[64];
	int v;
	config_path(path, sizeof path);
	auto f = fopen(path, "r");
	if (!f)
		return c;
	while (fgets(line, sizeof line, f))
		if (sscanf(line, "%63[a-z]=%d", key, &v) == 2) {
			if (!strcmp(key, "startup"))
				c.startup = v;
			else if (!strcmp(key, "typewriter"))
				c.typewriter = v;
			else if (!strcmp(key, "fart"))
				c.fart = v;
			else if (!strcmp(key, "shutdown"))
				c.shutdown = v;
		}
	fclose(f);
	return c;
}

static float rnd(float lo, float hi)
{
	return lo + (hi - lo) * (float)rand() / (float)RAND_MAX;
}

static void schedule(int ms, int chan, int note, int vel)
{
	SDL_LockMutex(lock);
	if (nevents < EVENTS)
		events[nevents++] = (event){ frame + (Uint64)ms * RATE / 1000, chan, note, vel };
	SDL_UnlockMutex(lock);
}

static void add_fart(bool epic)
{
	SDL_LockMutex(lock);
	int slot = 0;
	for (int i = 0; i < VOICES; i++) {
		if (!farts[i].on) {
			slot = i;
			break;
		}
		if (farts[i].t > farts[slot].t)
			slot = i;
	}
	float f0 = rnd(70, 150);
	farts[slot] = (fart){
		.on = true,
		.dur = epic ? rnd(1.0f, 1.8f) : rnd(0.15f, 0.55f),
		.f0 = f0,
		.f1 = f0 * rnd(0.55f, 1.25f),
		.wob = rnd(14, 40),
		.seed = rnd(0, 1000),
	};
	SDL_UnlockMutex(lock);
}

static float fart_sample(fart *v)
{
	float p = v->t / v->dur;
	float sputter = 0.75f + 0.25f * sinf(v->seed + v->t * 23.0f) * sinf(v->seed * 1.7f + v->t * 61.0f);
	float freq = (v->f0 + (v->f1 - v->f0) * p) * (1.0f + 0.08f * sinf(v->t * v->wob * 6.2832f)) * sputter;
	v->phase += freq / RATE;
	v->phase -= floorf(v->phase);
	float buzz = v->phase < 0.3f ? 1.0f : -0.45f;
	float noise = rnd(-1, 1) * 0.35f;
	float env = fminf(1.0f, v->t / 0.015f) * fminf(1.0f, (v->dur - v->t) / 0.08f);
	float x = (buzz + noise) * env * (0.65f + 0.35f * sinf(v->t * v->wob * 6.2832f));
	v->lp += 0.18f * (x - v->lp);
	v->t += 1.0f / RATE;
	if (v->t >= v->dur)
		v->on = false;
	return v->lp * 0.5f;
}

static void SDLCALL feed(void *data, SDL_AudioStream *s, int additional, int total)
{
	(void)data;
	(void)total;
	float buf[2 * 512];
	int frames = additional / (int)(2 * sizeof(float));
	while (frames > 0) {
		int n = frames < 512 ? frames : 512;
		SDL_LockMutex(lock);
		for (int i = 0; i < nevents;) {
			if (events[i].at <= frame) {
				if (events[i].vel)
					fluid_synth_noteon(synth, events[i].chan, events[i].note, events[i].vel);
				else
					fluid_synth_noteoff(synth, events[i].chan, events[i].note);
				events[i] = events[--nevents];
			} else {
				i++;
			}
		}
		fluid_synth_write_float(synth, n, buf, 0, 2, buf, 1, 2);
		for (int i = 0; i < n; i++)
			for (int j = 0; j < VOICES; j++)
				if (farts[j].on) {
					float x = fart_sample(&farts[j]);
					buf[2 * i] += x;
					buf[2 * i + 1] += x;
				}
		frame += n;
		SDL_UnlockMutex(lock);
		SDL_PutAudioStreamData(s, buf, n * 2 * (int)sizeof(float));
		frames -= n;
	}
}

static bool audio_open(void)
{
	SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, RATE };
	if (!SDL_Init(SDL_INIT_AUDIO))
		return false;
	lock = SDL_CreateMutex();
	settings = new_fluid_settings();
	fluid_settings_setnum(settings, "synth.sample-rate", RATE);
	fluid_settings_setnum(settings, "synth.gain", 0.6);
	fluid_settings_setint(settings, "synth.dynamic-sample-loading", 1);
	fluid_settings_setint(settings, "synth.polyphony", 32);
	fluid_settings_setint(settings, "synth.reverb.active", 0);
	fluid_settings_setint(settings, "synth.chorus.active", 0);
	synth = new_fluid_synth(settings);
	if (fluid_synth_sfload(synth, "/usr/share/soundfonts/default.sf2", 1) == FLUID_FAILED)
		return false;
	fluid_synth_program_change(synth, 0, 11);
	fluid_synth_program_change(synth, 1, 48);
	stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, NULL);
	return stream != NULL;
}

static void audio_close(void)
{
	SDL_DestroyAudioStream(stream);
	if (synth)
		delete_fluid_synth(synth);
	if (settings)
		delete_fluid_settings(settings);
	SDL_Quit();
}

static void note(int ms, int len, int chan, int key, int vel)
{
	schedule(ms, chan, key, vel);
	schedule(ms + len, chan, key, 0);
}

static int jingle(bool up)
{
	static const int melody[] = { 72, 76, 79, 84 };
	for (int i = 0; i < 4; i++)
		note(i * 130, i == 3 ? 900 : 260, 0, up ? melody[i] : melody[3 - i] - 5, 100);
	for (int i = 0; i < 3; i++)
		note(0, up ? 1400 : 1200, 1, (up ? 48 : 43) + (int[]){ 0, 4, 7 }[i], 55);
	return 2000;
}

static void typewriter(char c)
{
	switch (c) {
	case 'r':
		note(0, 100, 9, 37, 110);
		note(60, 900, 9, 81, 105);
		break;
	case 's':
		note(0, 100, 9, 41, 70);
		break;
	case 'b':
		note(0, 100, 9, 37, 60);
		break;
	default:
		note(0, 100, 9, 37, 90 + rand() % 30);
	}
}

static void key(char c, config cfg)
{
	if (cfg.typewriter)
		typewriter(c);
	if (cfg.fart)
		add_fart(c == 'r');
	SDL_ResumeAudioStreamDevice(stream);
}

static void play_and_wait(int ms)
{
	SDL_ResumeAudioStreamDevice(stream);
	SDL_Delay((Uint32)ms);
}

static int preview(const char *what)
{
	if (!strcmp(what, "startup") || !strcmp(what, "shutdown")) {
		play_and_wait(jingle(!strcmp(what, "startup")));
	} else if (!strcmp(what, "typewriter")) {
		static const char text[] = "Hi uncle!r";
		SDL_ResumeAudioStreamDevice(stream);
		for (const char *p = text; *p; p++) {
			config c = { .typewriter = true };
			key(*p == ' ' ? 's' : p[1] ? 'k' : 'r', c);
			SDL_Delay(p[1] ? 140 : 1200);
		}
	} else if (!strcmp(what, "fart")) {
		add_fart(false);
		SDL_ResumeAudioStreamDevice(stream);
		SDL_Delay(700);
		add_fart(true);
		SDL_Delay(2000);
	}
	return 0;
}

static int running_pid(void)
{
	char cmd[256] = "", path[64];
	int pid = 0;
	auto f = fopen(pidfile, "r");
	if (!f)
		return 0;
	if (fscanf(f, "%d", &pid) != 1)
		pid = 0;
	fclose(f);
	snprintf(path, sizeof path, "/proc/%d/cmdline", pid);
	f = pid > 0 ? fopen(path, "r") : nullptr;
	if (!f)
		return 0;
	size_t n = fread(cmd, 1, sizeof cmd - 1, f);
	fclose(f);
	cmd[n] = '\0';
	return strstr(cmd, "tinyhat-soundd") ? pid : 0;
}

static void on_signal(int sig)
{
	if (sig == SIGHUP)
		reload = 1;
	else
		quit = 1;
}

static int keys_daemon(config cfg)
{
	if (running_pid() || !(cfg.typewriter || cfg.fart))
		return 0;
	auto f = fopen(pidfile, "w");
	if (f) {
		fprintf(f, "%d\n", (int)getpid());
		fclose(f);
	}
	unlink(fifo);
	if (mkfifo(fifo, 0600)) {
		unlink(pidfile);
		return 1;
	}
	int fd = open(fifo, O_RDWR | O_CLOEXEC);
	struct sigaction sa = { .sa_handler = on_signal };
	sigaction(SIGHUP, &sa, nullptr);
	sigaction(SIGTERM, &sa, nullptr);
	sigaction(SIGINT, &sa, nullptr);
	Uint64 last = SDL_GetTicks();
	while (fd >= 0 && !quit) {
		struct timeval tv = { .tv_sec = IDLE_MS / 1000 };
		fd_set in;
		FD_ZERO(&in);
		FD_SET(fd, &in);
		int r = select(fd + 1, &in, nullptr, nullptr, &tv);
		if (reload) {
			reload = 0;
			cfg = load();
			if (!(cfg.typewriter || cfg.fart))
				break;
		}
		if (r > 0) {
			char buf[64];
			ssize_t n = read(fd, buf, sizeof buf);
			for (ssize_t i = 0; i < n; i++)
				key(buf[i], cfg);
			last = SDL_GetTicks();
		} else if (SDL_GetTicks() - last > IDLE_MS) {
			SDL_PauseAudioStreamDevice(stream);
		}
	}
	if (fd >= 0)
		close(fd);
	unlink(fifo);
	unlink(pidfile);
	return 0;
}

static int start_daemon(void)
{
	char *argv[] = { "tinyhat-soundd", "--keys", nullptr };
	pid_t pid;
	posix_spawnattr_t attr;
	posix_spawnattr_init(&attr);
	posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
	int err = posix_spawnp(&pid, argv[0], nullptr, &attr, argv, environ);
	posix_spawnattr_destroy(&attr);
	return err ? 1 : 0;
}

int main(int argc, char *argv[])
{
	auto dir = getenv("XDG_RUNTIME_DIR");
	snprintf(rundir, sizeof rundir, "%s", dir && *dir ? dir : "/tmp");
	snprintf(fifo, sizeof fifo, "%s/tinyhat-keys", rundir);
	snprintf(pidfile, sizeof pidfile, "%s/tinyhat-soundd.pid", rundir);
	srand((unsigned)time(nullptr) ^ (unsigned)getpid());
	auto cfg = load();
	auto mode = argc > 1 ? argv[1] : "";

	if (!strcmp(mode, "--reload")) {
		int pid = running_pid();
		if (pid)
			return kill(pid, SIGHUP) ? 1 : 0;
		return cfg.typewriter || cfg.fart ? start_daemon() : 0;
	}
	if (!strcmp(mode, "--play") && argc > 2) {
		bool up = !strcmp(argv[2], "startup");
		if (!(up ? cfg.startup : cfg.shutdown) || !audio_open())
			return 0;
		play_and_wait(jingle(up));
		audio_close();
		return 0;
	}
	if (!strcmp(mode, "--preview") && argc > 2) {
		if (!audio_open())
			return 1;
		preview(argv[2]);
		audio_close();
		return 0;
	}
	if (!strcmp(mode, "--keys")) {
		if (!(cfg.typewriter || cfg.fart) || running_pid() || !audio_open())
			return 0;
		int r = keys_daemon(cfg);
		audio_close();
		return r;
	}
	if (*mode) {
		fprintf(stderr, "usage: tinyhat-soundd [--keys | --reload | --play startup|shutdown | --preview startup|typewriter|fart|shutdown]\n");
		return 2;
	}
	if (!(cfg.startup || cfg.typewriter || cfg.fart) || !audio_open())
		return 0;
	if (cfg.startup) {
		SDL_ResumeAudioStreamDevice(stream);
		SDL_Delay((Uint32)jingle(true));
	}
	int r = keys_daemon(cfg);
	audio_close();
	return r;
}
