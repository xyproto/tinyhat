#include <fluidsynth.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SAMPLE_LEN 2048
#define SAMPLE_PERIOD 64
#define MEASURE_VOICES 32
#define MEASURE_CPU_SECONDS 0.1
#define MEASURE_MAX_FRAMES (4 * 44100)
#define BLOCK 64

static unsigned char *sf;
static size_t sf_len, sf_cap;

static void put(const void *p, size_t n)
{
	if (sf_len + n > sf_cap) {
		sf_cap = (sf_len + n) * 2;
		sf = realloc(sf, sf_cap);
		if (!sf) {
			perror("realloc");
			exit(1);
		}
	}
	memcpy(sf + sf_len, p, n);
	sf_len += n;
}

static void put8(unsigned v)
{
	unsigned char b = v;
	put(&b, 1);
}

static void put16(unsigned v)
{
	put8(v & 0xff);
	put8(v >> 8);
}

static void put32(uint32_t v)
{
	put16(v & 0xffff);
	put16(v >> 16);
}

static void putname(const char *s, size_t n)
{
	char b[20] = {0};
	memcpy(b, s, strlen(s));
	put(b, n);
}

static size_t chunk_begin(const char *id)
{
	put(id, 4);
	put32(0);
	return sf_len;
}

static void chunk_end(size_t start)
{
	uint32_t n = sf_len - start;
	sf[start - 4] = n;
	sf[start - 3] = n >> 8;
	sf[start - 2] = n >> 16;
	sf[start - 1] = n >> 24;
}

static size_t list_begin(const char *id, const char *type)
{
	size_t start = chunk_begin(id);
	put(type, 4);
	return start;
}

static void build_soundfont(void)
{
	size_t riff = list_begin("RIFF", "sfbk");

	size_t info = list_begin("LIST", "INFO");
	size_t c = chunk_begin("ifil");
	put16(2);
	put16(1);
	chunk_end(c);
	c = chunk_begin("isng");
	put("EMU8000", 8);
	chunk_end(c);
	c = chunk_begin("INAM");
	put("bench", 6);
	chunk_end(c);
	chunk_end(info);

	size_t sdta = list_begin("LIST", "sdta");
	c = chunk_begin("smpl");
	for (int i = 0; i < SAMPLE_LEN; i++)
		put16((uint16_t)(int16_t)lrint(16000.0 * sin(2.0 * M_PI * i / SAMPLE_PERIOD)));
	for (int i = 0; i < 46; i++)
		put16(0);
	chunk_end(c);
	chunk_end(sdta);

	size_t pdta = list_begin("LIST", "pdta");

	c = chunk_begin("phdr");
	putname("bench", 20);
	put16(0);
	put16(0);
	put16(0);
	put32(0);
	put32(0);
	put32(0);
	putname("EOP", 20);
	put16(0);
	put16(0);
	put16(1);
	put32(0);
	put32(0);
	put32(0);
	chunk_end(c);

	c = chunk_begin("pbag");
	put16(0);
	put16(0);
	put16(1);
	put16(0);
	chunk_end(c);

	c = chunk_begin("pmod");
	for (int i = 0; i < 10; i++)
		put8(0);
	chunk_end(c);

	c = chunk_begin("pgen");
	put16(41);
	put16(0);
	put16(0);
	put16(0);
	chunk_end(c);

	c = chunk_begin("inst");
	putname("sine", 20);
	put16(0);
	putname("EOI", 20);
	put16(1);
	chunk_end(c);

	c = chunk_begin("ibag");
	put16(0);
	put16(0);
	put16(4);
	put16(0);
	chunk_end(c);

	c = chunk_begin("imod");
	for (int i = 0; i < 10; i++)
		put8(0);
	chunk_end(c);

	c = chunk_begin("igen");
	put16(8);
	put16(9000);
	put16(9);
	put16(100);
	put16(54);
	put16(1);
	put16(53);
	put16(0);
	put16(0);
	put16(0);
	chunk_end(c);

	c = chunk_begin("shdr");
	putname("sine", 20);
	put32(0);
	put32(SAMPLE_LEN);
	put32(SAMPLE_PERIOD);
	put32(SAMPLE_LEN - SAMPLE_PERIOD);
	put32(44100);
	put8(60);
	put8(0);
	put16(0);
	put16(1);
	putname("EOS", 20);
	for (int i = 0; i < 26; i++)
		put8(0);
	chunk_end(c);

	chunk_end(pdta);
	chunk_end(riff);
}

static void *mem_open(const char *filename)
{
	if (strcmp(filename, "bench.sf2") != 0)
		return NULL;
	return calloc(1, sizeof(size_t));
}

static int mem_read(void *buf, fluid_long_long_t count, void *handle)
{
	size_t *pos = handle;
	if (count < 0 || *pos + (size_t)count > sf_len)
		return FLUID_FAILED;
	memcpy(buf, sf + *pos, count);
	*pos += count;
	return FLUID_OK;
}

static int mem_seek(void *handle, fluid_long_long_t offset, int origin)
{
	size_t *pos = handle;
	fluid_long_long_t base = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? (fluid_long_long_t)*pos : (fluid_long_long_t)sf_len;
	if (base + offset < 0 || base + offset > (fluid_long_long_t)sf_len)
		return FLUID_FAILED;
	*pos = base + offset;
	return FLUID_OK;
}

static fluid_long_long_t mem_tell(void *handle)
{
	return *(size_t *)handle;
}

static int mem_close(void *handle)
{
	free(handle);
	return FLUID_OK;
}

static double cpu_seconds(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void quiet_log(int level, const char *message, void *data)
{
	(void)level;
	(void)message;
	(void)data;
}

static double measure(void)
{
	for (int l = FLUID_PANIC; l <= FLUID_DBG; l++)
		fluid_set_log_function(l, quiet_log, NULL);

	fluid_settings_t *settings = new_fluid_settings();
	fluid_settings_setnum(settings, "synth.sample-rate", 44100.0);
	fluid_settings_setint(settings, "synth.polyphony", MEASURE_VOICES);
	fluid_settings_setint(settings, "synth.reverb.active", 0);
	fluid_settings_setint(settings, "synth.chorus.active", 0);
	fluid_settings_setint(settings, "synth.cpu-cores", 1);
	fluid_settings_setint(settings, "synth.lock-memory", 0);
	fluid_synth_t *synth = new_fluid_synth(settings);
	if (!synth)
		return 0;

	fluid_sfloader_t *loader = new_fluid_defsfloader(settings);
	fluid_sfloader_set_callbacks(loader, mem_open, mem_read, mem_seek, mem_tell, mem_close);
	fluid_synth_add_sfloader(synth, loader);
	if (fluid_synth_sfload(synth, "bench.sf2", 1) == FLUID_FAILED) {
		delete_fluid_synth(synth);
		delete_fluid_settings(settings);
		return 0;
	}

	for (int ch = 0; ch < 16; ch++)
		fluid_synth_program_select(synth, ch, 1, 0, 0);
	for (int i = 0; i < MEASURE_VOICES; i++) {
		int ch = i % 15;
		if (ch >= 9)
			ch++;
		fluid_synth_noteon(synth, ch, 40 + i, 100);
	}

	float left[BLOCK], right[BLOCK];
	for (int i = 0; i < 16; i++)
		fluid_synth_write_float(synth, BLOCK, left, 0, 1, right, 0, 1);

	long frames = 0;
	double start = cpu_seconds(), elapsed = 0;
	while (elapsed < MEASURE_CPU_SECONDS && frames < MEASURE_MAX_FRAMES) {
		fluid_synth_write_float(synth, BLOCK, left, 0, 1, right, 0, 1);
		frames += BLOCK;
		elapsed = cpu_seconds() - start;
	}
	int active = fluid_synth_get_active_voice_count(synth);

	delete_fluid_synth(synth);
	delete_fluid_settings(settings);
	if (elapsed <= 0 || active < MEASURE_VOICES)
		return 0;
	return (double)active * frames / elapsed;
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s OUTPUT.conf\n", argv[0]);
		return 2;
	}

	build_soundfont();
	double capacity = measure();
	if (capacity <= 0) {
		fprintf(stderr, "%s: the benchmark failed\n", argv[0]);
		return 1;
	}

	long cores = sysconf(_SC_NPROCESSORS_ONLN);
	double budget = capacity * (cores > 1 ? 0.5 : 0.2);

	static const int rates[] = {44100, 32000, 22050};
	static const int polys[] = {32, 24, 18, 16, 12, 10, 8, 6, 4};
	int rate = 22050, poly = 4;
	int found = 0;
	for (size_t i = 0; i < sizeof rates / sizeof *rates && !found; i++) {
		if ((double)rates[i] * polys[0] <= budget) {
			rate = rates[i];
			poly = polys[0];
			found = 1;
		}
	}
	for (size_t i = 1; i < sizeof polys / sizeof *polys && !found; i++) {
		if ((double)rate * polys[i] <= budget) {
			poly = polys[i];
			found = 1;
		}
	}

	size_t n = strlen(argv[1]) + 5;
	char *tmp = malloc(n);
	if (!tmp) {
		perror("malloc");
		return 1;
	}
	snprintf(tmp, n, "%s.tmp", argv[1]);
	FILE *f = fopen(tmp, "w");
	if (!f) {
		perror(tmp);
		return 1;
	}
	fprintf(f, "[mixer]\nrate=%d\nblocksize=%d\n\n[midi]\nfluid.samplerate=%d\nfluid.polyphony=%d\n",
		rate, rate < 44100 ? 1024 : 2048, rate, poly);
	if (fclose(f) != 0 || rename(tmp, argv[1]) != 0) {
		perror(argv[1]);
		unlink(tmp);
		return 1;
	}

	printf("capacity %.0f voice-samples/s, %ld cores: %d Hz, %d voices\n", capacity, cores, rate, poly);
	return 0;
}
