#define _GNU_SOURCE
#include <SDL3/SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <fluidsynth.h>
#include <math.h>
#include <mpg123.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define SOUNDS "/usr/share/tinyhat/sounds"

enum { RATE = 44100, VOICES = 6, EVENTS = 64, IDLE_MS = 2000, SAMPLES = 7, HISTORY = 3, THROTTLE_MS = 700 };

typedef struct {
	bool startup, typewriter, fart, shutdown;
} config;

typedef struct {
	Uint64 at;
	int chan, note, vel;
} event;

typedef struct {
	float *data;
	size_t frames;
} sample;

typedef struct {
	const sample *s;
	size_t pos;
} fart;

typedef struct {
	const signed char *lead, *bass;
	const unsigned char (*chords)[3];
	int steps, step_ms;
} tune;

typedef struct {
	int note;
	bool gate;
	float phase, env, age;
} chip_voice;

extern char **environ;

static fluid_settings_t *settings;
static fluid_synth_t *synth;
static SDL_AudioStream *stream;
static SDL_Mutex *lock;
static Uint64 frame;
static event events[EVENTS];
static int nevents;
static fart farts[VOICES];
static sample samples[SAMPLES];
static int nsamples, history[HISTORY] = { -1, -1, -1 };
static Uint64 last_fart;
static const tune *playing;
static Uint64 tune_pos;
static chip_voice lead, bass;
static float arp_phase, lowpass[2];

static const signed char up_lead[] = {
	72, 0, 76, 79, 84, -1, 79, 76, 81, -1, 79, 76, 72, -1, 69, 72,
	77, -1, 81, 84, 89, -1, 84, 81, 83, 79, 86, -1, 91, -1, -1, -1,
	84, -1, -1, -1, -1, -1, -1, 0,
};
static const signed char up_bass[] = {
	48, 0, 60, 0, 48, 0, 55, 0, 45, 0, 57, 0, 45, 0, 52, 0,
	41, 0, 53, 0, 41, 0, 48, 0, 43, 0, 55, 0, 43, 0, 47, 0,
	36, -1, -1, -1, -1, -1, -1, 0,
};
static const unsigned char up_chords[][3] = { { 60, 64, 67 }, { 57, 60, 64 }, { 53, 57, 60 }, { 55, 59, 62 }, { 60, 64, 67 } };
static const signed char down_lead[] = {
	84, -1, 81, 77, 72, -1, 77, -1, 80, -1, 77, 72, 68, -1, 65, -1,
	76, -1, 72, 67, 64, -1, 67, 72, 71, 74, 67, -1, -1, -1, -1, -1,
	60, -1, -1, 0,
};
static const signed char down_bass[] = {
	41, 0, 53, 0, 41, 0, 48, 0, 41, 0, 53, 0, 41, 0, 44, 0,
	36, 0, 48, 0, 36, 0, 43, 0, 43, 0, 55, 0, 43, 0, 50, 0,
	36, -1, -1, 0,
};
static const unsigned char down_chords[][3] = { { 65, 69, 72 }, { 65, 68, 72 }, { 60, 64, 67 }, { 55, 59, 65 }, { 60, 64, 67 } };
static const tune tunes[] = {
	{ up_lead, up_bass, up_chords, (int)sizeof(up_lead), 110 },
	{ down_lead, down_bass, down_chords, (int)sizeof(down_lead), 140 },
};
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

static void schedule(int ms, int chan, int note, int vel)
{
	SDL_LockMutex(lock);
	if (nevents < EVENTS)
		events[nevents++] = (event){ frame + (Uint64)ms * RATE / 1000, chan, note, vel };
	SDL_UnlockMutex(lock);
}

static void load_samples(void)
{
	if (nsamples)
		return;
	mpg123_init();
	auto mh = mpg123_new(nullptr, nullptr);
	if (!mh)
		return;
	mpg123_param(mh, MPG123_FLAGS, MPG123_FORCE_STEREO | MPG123_QUIET, 0);
	for (int i = 1; i <= SAMPLES; i++) {
		char path[256];
		unsigned char buf[16384];
		float *data = nullptr;
		size_t have = 0, cap = 0, got;
		int r;
		snprintf(path, sizeof path, SOUNDS "/fart%d.mp3", i);
		mpg123_format_none(mh);
		mpg123_format(mh, RATE, MPG123_STEREO, MPG123_ENC_FLOAT_32);
		if (mpg123_open(mh, path) != MPG123_OK)
			continue;
		do {
			r = mpg123_read(mh, buf, sizeof buf, &got);
			size_t n = got / sizeof(float);
			if (have + n > cap) {
				cap = (have + n) * 2;
				auto grown = (float *)realloc(data, cap * sizeof(float));
				if (!grown)
					break;
				data = grown;
			}
			memcpy(data + have, buf, n * sizeof(float));
			have += n;
		} while (r == MPG123_OK || r == MPG123_NEW_FORMAT);
		mpg123_close(mh);
		if (have >= 2)
			samples[nsamples++] = (sample){ data, have / 2 };
		else
			free(data);
	}
	mpg123_delete(mh);
}

static int pick(bool epic)
{
	if (epic) {
		int a = 0, b = -1;
		for (int i = 1; i < nsamples; i++)
			if (samples[i].frames > samples[a].frames) {
				b = a;
				a = i;
			} else if (b < 0 || samples[i].frames > samples[b].frames) {
				b = i;
			}
		return b >= 0 && rand() % 2 ? b : a;
	}
	double weights[SAMPLES], total = 0;
	for (int i = 0; i < nsamples; i++) {
		weights[i] = 1.0 / (double)samples[i].frames;
		for (int h = 0; h < HISTORY; h++)
			if (history[h] == i)
				weights[i] = 0;
		total += weights[i];
	}
	if (total <= 0)
		return rand() % nsamples;
	double r = total * rand() / ((double)RAND_MAX + 1);
	for (int i = 0; i < nsamples; i++)
		if ((r -= weights[i]) < 0)
			return i;
	return nsamples - 1;
}

static void add_fart(bool epic)
{
	load_samples();
	if (!nsamples || (!epic && last_fart && SDL_GetTicks() - last_fart < THROTTLE_MS))
		return;
	last_fart = SDL_GetTicks();
	SDL_LockMutex(lock);
	int slot = 0;
	for (int i = 0; i < VOICES; i++) {
		if (!farts[i].s) {
			slot = i;
			break;
		}
		if (farts[i].pos * farts[slot].s->frames > farts[slot].pos * farts[i].s->frames)
			slot = i;
	}
	int i = pick(epic);
	memmove(history + 1, history, (HISTORY - 1) * sizeof(int));
	history[0] = i;
	farts[slot] = (fart){ &samples[i], 0 };
	SDL_UnlockMutex(lock);
}

static float hz(int note)
{
	return 440.0f * exp2f((float)(note - 69) / 12.0f);
}

static void step_voice(chip_voice *v, signed char n)
{
	if (n > 0) {
		v->note = n;
		v->gate = true;
		v->env = 0.0f;
		v->age = 0.0f;
	} else if (n == 0) {
		v->gate = false;
	}
}

static float envelope(chip_voice *v)
{
	if (v->gate)
		v->env = v->age < 0.004f ? v->age / 0.004f : 0.55f + (v->env - 0.55f) * 0.9997f;
	else
		v->env *= 0.9992f;
	v->age += 1.0f / RATE;
	return v->env;
}

static void chip_render(float *buf, int n)
{
	if (!playing)
		return;
	Uint64 step_len = (Uint64)playing->step_ms * RATE / 1000;
	for (int i = 0; i < n; i++, tune_pos++) {
		Uint64 step = tune_pos / step_len;
		if (tune_pos % step_len == 0 && step < (Uint64)playing->steps) {
			step_voice(&lead, playing->lead[step]);
			step_voice(&bass, playing->bass[step]);
		}
		if (step >= (Uint64)playing->steps && lead.env < 0.001f && bass.env < 0.001f) {
			playing = nullptr;
			return;
		}
		float t = (float)tune_pos / RATE, x = 0.0f;
		float le = envelope(&lead), be = envelope(&bass);
		float vib = lead.age > 0.15f ? 1.0f + 0.006f * sinf(6.2832f * 6.0f * t) : 1.0f;
		lead.phase += hz(lead.note) * vib / RATE;
		lead.phase -= floorf(lead.phase);
		x += 0.22f * le * (lead.phase < 0.25f + 0.15f * sinf(6.2832f * 0.7f * t) ? 1.0f : -1.0f);
		bass.phase += hz(bass.note) / RATE;
		bass.phase -= floorf(bass.phase);
		x += 0.30f * be * (bass.phase < 0.5f ? 4.0f * bass.phase - 1.0f : 3.0f - 4.0f * bass.phase);
		Uint64 bar = step / 8 < 4 ? step / 8 : 4;
		const unsigned char *chord = playing->chords[bar];
		float arp_env = step < (Uint64)playing->steps ? 1.0f : fmaxf(0.0f, 1.0f - (float)(tune_pos - playing->steps * step_len) / (RATE * 0.4f));
		arp_phase += hz(chord[(tune_pos / (RATE / 50)) % 3]) / RATE;
		arp_phase -= floorf(arp_phase);
		x += 0.10f * arp_env * (arp_phase < 0.5f ? 1.0f : -1.0f);
		for (int c = 0; c < 2; c++) {
			lowpass[c] += 0.45f * (x - lowpass[c]);
			buf[2 * i + c] += lowpass[c];
		}
	}
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
		chip_render(buf, n);
		for (int j = 0; j < VOICES; j++) {
			auto v = &farts[j];
			for (int i = 0; i < n && v->s; i++) {
				buf[2 * i] += 2.2f * v->s->data[2 * v->pos];
				buf[2 * i + 1] += 2.2f * v->s->data[2 * v->pos + 1];
				if (++v->pos >= v->s->frames)
					v->s = nullptr;
			}
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
	auto t = &tunes[up ? 0 : 1];
	SDL_LockMutex(lock);
	lead = bass = (chip_voice){ 0 };
	arp_phase = lowpass[0] = lowpass[1] = 0.0f;
	tune_pos = 0;
	playing = t;
	SDL_UnlockMutex(lock);
	return t->steps * t->step_ms + 900;
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

static int beep(int argc, char *argv[])
{
	double freq = 440, len = 200, delay = 100;
	int reps = 1;
	for (int c; (c = getopt(argc, argv, "f:l:r:d:D:")) != -1;) {
		if (c == 'f')
			freq = atof(optarg);
		else if (c == 'l')
			len = atof(optarg);
		else if (c == 'r')
			reps = atoi(optarg);
		else if (c == 'd' || c == 'D')
			delay = atof(optarg);
		else
			return 2;
	}
	if (freq <= 0 || len <= 0 || reps < 1)
		return 2;
	SDL_AudioSpec spec = { SDL_AUDIO_F32, 1, RATE };
	if (!SDL_Init(SDL_INIT_AUDIO))
		return 1;
	auto s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
	if (!s) {
		SDL_Quit();
		return 1;
	}
	int n = (int)(len * RATE / 1000), gap = (int)(delay * RATE / 1000), ramp = RATE / 200;
	auto buf = (float *)calloc((size_t)(n + gap), sizeof(float));
	for (int i = 0; i < n; i++) {
		float env = fminf(1.0f, fminf((float)i, (float)(n - i)) / (float)ramp);
		buf[i] = 0.25f * env * (sinf(6.2831853f * (float)freq * i / RATE) > 0 ? 1.0f : -1.0f) * 0.6f +
			 0.15f * env * sinf(6.2831853f * (float)freq * i / RATE);
	}
	for (int r = 0; r < reps; r++)
		SDL_PutAudioStreamData(s, buf, (int)((r + 1 < reps ? n + gap : n) * sizeof(float)));
	SDL_ResumeAudioStreamDevice(s);
	while (SDL_GetAudioStreamQueued(s) > 0)
		SDL_Delay(10);
	SDL_Delay(60);
	free(buf);
	SDL_DestroyAudioStream(s);
	SDL_Quit();
	return 0;
}

int main(int argc, char *argv[])
{
	auto name = strrchr(argv[0], '/');
	if (!strcmp(name ? name + 1 : argv[0], "beep"))
		return beep(argc, argv);
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
