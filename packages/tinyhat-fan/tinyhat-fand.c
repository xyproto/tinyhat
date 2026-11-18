#define _GNU_SOURCE
#include <dirent.h>
#include <glob.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum { POLL = 2, REAPPLY = 15, MAXCPU = 256, MAXFAN = 16 };

static const char *modefile = "/var/lib/tinyhat-fan/mode";
static const char *rapl = "/sys/class/powercap/intel-rapl:0";
static const char *pstate = "/sys/devices/system/cpu/intel_pstate";

static volatile sig_atomic_t stop;

static char temppath[512];
static bool haverapl;
static long pl1, pl2;
static int noturbo = -1, boost = -1;
static char epp[64];
static int ncpu;
static long maxfreq[MAXCPU], minfreq[MAXCPU], hwmaxfreq[MAXCPU];
static int nfan;
static char fanpath[MAXFAN][512];
static int fanenable[MAXFAN];

static bool readlong(const char *path, long *out)
{
	auto f = fopen(path, "r");
	if (!f)
		return false;
	bool ok = fscanf(f, "%ld", out) == 1;
	fclose(f);
	return ok;
}

static bool readstr(const char *path, char *out, size_t size)
{
	auto f = fopen(path, "r");
	if (!f)
		return false;
	bool ok = fgets(out, size, f) != nullptr;
	fclose(f);
	if (ok)
		out[strcspn(out, "\n")] = '\0';
	return ok;
}

static void writestr(const char *path, const char *value)
{
	auto f = fopen(path, "w");
	if (!f)
		return;
	fputs(value, f);
	fclose(f);
}

static void writelong(const char *path, long value)
{
	char text[32];
	snprintf(text, sizeof text, "%ld", value);
	writestr(path, text);
}

static void findtemp(void)
{
	static const char *names[] = { "coretemp", "k10temp", "zenpower", "cpu_thermal", "acpitz" };
	glob_t g;
	if (glob("/sys/class/hwmon/hwmon*/name", 0, nullptr, &g))
		return;
	for (size_t n = 0; n < sizeof names / sizeof *names && !*temppath; n++)
		for (size_t i = 0; i < g.gl_pathc && !*temppath; i++) {
			char name[64], path[512];
			long t;
			if (!readstr(g.gl_pathv[i], name, sizeof name) || strcmp(name, names[n]))
				continue;
			snprintf(path, sizeof path, "%.*stemp1_input", (int)(strlen(g.gl_pathv[i]) - strlen("name")), g.gl_pathv[i]);
			if (readlong(path, &t))
				snprintf(temppath, sizeof temppath, "%s", path);
		}
	globfree(&g);
	if (!*temppath)
		snprintf(temppath, sizeof temppath, "/sys/class/thermal/thermal_zone0/temp");
}

static int temperature(void)
{
	long t;
	return readlong(temppath, &t) ? (int)(t / 1000) : -1;
}

static void findfans(void)
{
	glob_t g;
	if (glob("/sys/class/hwmon/hwmon*/pwm[0-9]", 0, nullptr, &g))
		return;
	for (size_t i = 0; i < g.gl_pathc && nfan < MAXFAN; i++) {
		char path[520];
		long e, p;
		snprintf(path, sizeof path, "%.511s_enable", g.gl_pathv[i]);
		if (access(g.gl_pathv[i], W_OK) || access(path, W_OK) || !readlong(path, &e) || !readlong(g.gl_pathv[i], &p))
			continue;
		snprintf(fanpath[nfan], sizeof fanpath[0], "%s", g.gl_pathv[i]);
		fanenable[nfan] = (int)e;
		nfan++;
	}
	globfree(&g);
}

static void setup(void)
{
	char path[512];
	findtemp();
	findfans();
	snprintf(path, sizeof path, "%s/constraint_0_power_limit_uw", rapl);
	haverapl = !access(path, W_OK) && readlong(path, &pl1) && pl1 > 0;
	snprintf(path, sizeof path, "%s/constraint_1_power_limit_uw", rapl);
	if (haverapl && (!readlong(path, &pl2) || pl2 < pl1))
		pl2 = pl1;
	long v;
	snprintf(path, sizeof path, "%s/no_turbo", pstate);
	if (readlong(path, &v))
		noturbo = (int)v;
	if (readlong("/sys/devices/system/cpu/cpufreq/boost", &v))
		boost = (int)v;
	readstr("/sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference", epp, sizeof epp);
	for (ncpu = 0; ncpu < MAXCPU; ncpu++) {
		snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", ncpu);
		if (!readlong(path, &maxfreq[ncpu]))
			break;
		snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", ncpu);
		if (!readlong(path, &hwmaxfreq[ncpu]))
			hwmaxfreq[ncpu] = maxfreq[ncpu];
		snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_min_freq", ncpu);
		if (!readlong(path, &minfreq[ncpu]))
			minfreq[ncpu] = hwmaxfreq[ncpu] / 3;
	}
}

static void setturbo(bool on)
{
	char path[512];
	if (noturbo >= 0) {
		snprintf(path, sizeof path, "%s/no_turbo", pstate);
		writelong(path, on ? 0 : 1);
	}
	if (boost >= 0)
		writelong("/sys/devices/system/cpu/cpufreq/boost", on ? 1 : 0);
}

static void setepp(const char *value)
{
	char path[512];
	if (!*epp)
		return;
	for (int i = 0; i < ncpu; i++) {
		snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/energy_performance_preference", i);
		writestr(path, value);
	}
}

static void setlimit(int percent)
{
	char path[512];
	if (haverapl) {
		long l1 = pl1 * percent / 100, l2 = l1 + 10000000;
		snprintf(path, sizeof path, "%s/constraint_0_power_limit_uw", rapl);
		writelong(path, l1);
		snprintf(path, sizeof path, "%s/constraint_1_power_limit_uw", rapl);
		writelong(path, l2 < pl2 ? l2 : pl2);
		return;
	}
	for (int i = 0; i < ncpu; i++) {
		long f = minfreq[i] + (hwmaxfreq[i] - minfreq[i]) * percent / 100;
		snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", i);
		writelong(path, f);
	}
}

static void setfans(int pwm)
{
	char path[520];
	for (int i = 0; i < nfan; i++) {
		snprintf(path, sizeof path, "%.511s_enable", fanpath[i]);
		if (pwm < 0) {
			writelong(path, fanenable[i] == 1 ? 2 : fanenable[i]);
		} else {
			writelong(path, 1);
			writelong(fanpath[i], pwm);
		}
	}
}

static void restore(void)
{
	char path[512];
	setfans(-1);
	if (haverapl) {
		snprintf(path, sizeof path, "%s/constraint_0_power_limit_uw", rapl);
		writelong(path, pl1);
		snprintf(path, sizeof path, "%s/constraint_1_power_limit_uw", rapl);
		writelong(path, pl2);
	}
	for (int i = 0; i < ncpu; i++) {
		snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", i);
		writelong(path, maxfreq[i]);
	}
	if (noturbo >= 0) {
		snprintf(path, sizeof path, "%s/no_turbo", pstate);
		writelong(path, noturbo);
	}
	if (boost >= 0)
		writelong("/sys/devices/system/cpu/cpufreq/boost", boost);
	setepp(epp);
}

static bool laptop(void)
{
	long type;
	glob_t g;
	if (readlong("/sys/class/dmi/id/chassis_type", &type))
		switch (type) {
		case 8:
		case 9:
		case 10:
		case 14:
		case 30:
		case 31:
		case 32:
			return true;
		}
	bool battery = !glob("/sys/class/power_supply/BAT*", 0, nullptr, &g);
	if (battery)
		globfree(&g);
	return battery;
}

static void writedefault(void)
{
	char text[32] = "";
	if (readstr(modefile, text, sizeof text) && (!strcmp(text, "full") || !strcmp(text, "medium") || !strcmp(text, "chill")))
		return;
	mkdir("/var/lib/tinyhat-fan", 0755);
	writestr(modefile, laptop() ? "chill\n" : "full\n");
	chmod(modefile, 0666);
}

static char readmode(void)
{
	char text[32] = "";
	readstr(modefile, text, sizeof text);
	if (!strcmp(text, "full"))
		return 'f';
	if (!strcmp(text, "chill"))
		return 'c';
	return 'm';
}

static void onsignal(int) { stop = 1; }

int main(void)
{
	struct sigaction sa = { .sa_handler = onsignal };
	sigaction(SIGTERM, &sa, nullptr);
	sigaction(SIGINT, &sa, nullptr);
	sigaction(SIGHUP, &sa, nullptr);
	setup();
	writedefault();

	char mode = 0;
	int percent = 100, count = 0;
	bool fanfull = false;
	while (!stop) {
		char want = readmode();
		int t = temperature();
		if (want != mode || count % REAPPLY == 0) {
			if (want != mode) {
				percent = want == 'f' ? 100 : want == 'm' ? 70 : 40;
				fanfull = false;
				setfans(-1);
			}
			mode = want;
			setturbo(mode != 'c');
			setepp(mode == 'f' ? "performance" : mode == 'm' ? "balance_performance" : "power");
			setlimit(percent);
		}
		if (mode != 'f' && t >= 0) {
			int target = mode == 'm' ? 82 : 68, lo = mode == 'm' ? 30 : 20, next = percent;
			if (t >= target + 7)
				next -= 10;
			else if (t >= target + 3)
				next -= 4;
			else if (t <= target - 3)
				next += 2;
			next = next < lo ? lo : next > 100 ? 100 : next;
			if (next != percent) {
				percent = next;
				setlimit(percent);
			}
		}
		if (mode == 'f' && t >= 0) {
			bool hot = t >= 55 || (fanfull && t >= 48);
			if (hot != fanfull) {
				fanfull = hot;
				setfans(hot ? 255 : -1);
			}
		}
		count++;
		struct timespec wait = { .tv_sec = POLL };
		nanosleep(&wait, nullptr);
	}
	restore();
	return 0;
}
