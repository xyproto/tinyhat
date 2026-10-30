#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum { INTERVAL = 37 };

extern char **environ;

static char pidfile[512];

static bool running(const char *name)
{
	auto proc = opendir("/proc");
	if (!proc)
		return false;
	bool found = false;
	for (struct dirent *e; !found && (e = readdir(proc));) {
		if (!isdigit((unsigned char)e->d_name[0]))
			continue;
		char path[300], comm[64] = "";
		snprintf(path, sizeof path, "/proc/%s/comm", e->d_name);
		auto f = fopen(path, "r");
		if (!f)
			continue;
		if (fgets(comm, sizeof comm, f))
			comm[strcspn(comm, "\n")] = '\0';
		fclose(f);
		found = !strcmp(comm, name);
	}
	closedir(proc);
	return found;
}

static bool mounted(const char *dir)
{
	auto f = fopen("/proc/self/mountinfo", "r");
	if (!f)
		return false;
	char line[4096], target[4096];
	bool found = false;
	while (!found && fgets(line, sizeof line, f))
		found = sscanf(line, "%*s %*s %*s %*s %4095s", target) == 1 && !strcmp(target, dir);
	fclose(f);
	return found;
}

static bool run(char *out, size_t size, char *const argv[])
{
	int fd[2];
	if (pipe2(fd, O_CLOEXEC))
		return false;
	posix_spawn_file_actions_t fa;
	posix_spawnattr_t attr;
	sigset_t none;
	sigemptyset(&none);
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_adddup2(&fa, fd[1], STDOUT_FILENO);
	posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
	posix_spawnattr_init(&attr);
	posix_spawnattr_setsigmask(&attr, &none);
	posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK);
	pid_t pid;
	int err = posix_spawnp(&pid, argv[0], &fa, &attr, argv, environ);
	posix_spawn_file_actions_destroy(&fa);
	posix_spawnattr_destroy(&attr);
	close(fd[1]);
	size_t len = 0;
	if (!err) {
		for (ssize_t n; len + 1 < size && (n = read(fd[0], out + len, size - len - 1)) != 0;) {
			if (n > 0)
				len += n;
			else if (errno != EINTR)
				break;
		}
	}
	close(fd[0]);
	out[len] = '\0';
	int status = 0;
	if (!err)
		while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
			;
	return !err && WIFEXITED(status) && !WEXITSTATUS(status);
}

static void trim(char *s)
{
	auto start = s;
	while (isspace((unsigned char)*start))
		start++;
	memmove(s, start, strlen(start) + 1);
	for (auto end = s + strlen(s); end > s && isspace((unsigned char)end[-1]);)
		*--end = '\0';
}

static void strip_escapes(char *s)
{
	auto out = s;
	for (auto in = s; *in;) {
		if (*in == '\033') {
			for (in++; *in && !isalpha((unsigned char)*in); in++)
				;
			if (*in)
				in++;
		} else {
			*out++ = *in++;
		}
	}
	*out = '\0';
}

static void wifi(char *out, size_t size)
{
	*out = '\0';
	if (!running("iwd"))
		return;
	glob_t g;
	if (glob("/sys/class/net/*/wireless", 0, nullptr, &g))
		return;
	for (size_t i = 0; i < g.gl_pathc && !*out; i++) {
		char dev[256], text[8192];
		snprintf(dev, sizeof dev, "%.*s", (int)(strrchr(g.gl_pathv[i], '/') - g.gl_pathv[i]), g.gl_pathv[i]);
		char *argv[] = { "iwctl", "station", strrchr(dev, '/') + 1, "show", nullptr };
		if (!run(text, sizeof text, argv))
			continue;
		strip_escapes(text);
		for (auto line = strtok(text, "\n"); line; line = strtok(nullptr, "\n")) {
			auto key = strstr(line, "Connected network");
			if (key) {
				snprintf(out, size, "%s", key + strlen("Connected network"));
				trim(out);
				break;
			}
		}
	}
	globfree(&g);
}

static void volume(char *out, size_t size)
{
	char text[256];
	double v;
	char *argv[] = { "wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@", nullptr };
	*out = '\0';
	if (!run(text, sizeof text, argv) || sscanf(text, "Volume: %lf", &v) != 1)
		return;
	if (strstr(text, "[MUTED]"))
		snprintf(out, size, "muted");
	else
		snprintf(out, size, "%d%%", (int)(v * 100 + 0.5));
}

static void add(char *line, size_t size, const char *item)
{
	size_t len = strlen(line);
	snprintf(line + len, size - len, "%s%s", len ? " | " : "", item);
}

static void status(char *line, size_t size, const char *rundir)
{
	char item[512], text[256], path[512];
	*line = '\0';
	snprintf(path, sizeof path, "%s/tinyhat-share.pid", rundir);
	if (!access(path, F_OK)) {
		char *argv[] = { "tinyhat-addr", nullptr };
		run(text, sizeof text, argv);
		trim(text);
		snprintf(item, sizeof item, "S http://%s:3000", *text ? text : "this-computer");
		add(line, size, item);
	}
	if (running("dropbear"))
		add(line, size, "ssh on");
	wifi(text, sizeof text);
	if (*text) {
		snprintf(item, sizeof item, "W %s", text);
		add(line, size, item);
	}
	volume(text, sizeof text);
	if (*text) {
		snprintf(item, sizeof item, "V %s", text);
		add(line, size, item);
	}
	glob_t g;
	if (!glob("/sys/class/power_supply/BAT*/capacity", 0, nullptr, &g)) {
		for (size_t i = 0; i < g.gl_pathc; i++) {
			auto f = fopen(g.gl_pathv[i], "r");
			int cap;
			if (f && fscanf(f, "%d", &cap) == 1) {
				snprintf(item, sizeof item, "B %d%%", cap);
				add(line, size, item);
			}
			if (f)
				fclose(f);
		}
		globfree(&g);
	}
	auto now = time(nullptr);
	struct tm tm;
	strftime(text, sizeof text, "%Y-%m-%d %H:%M", localtime_r(&now, &tm));
	add(line, size, text);
}

static int refresh(void)
{
	char cmdline[256] = "";
	auto f = fopen(pidfile, "r");
	int pid = 0;
	if (!f)
		return 1;
	if (fscanf(f, "%d", &pid) != 1)
		pid = 0;
	fclose(f);
	char path[64];
	snprintf(path, sizeof path, "/proc/%d/cmdline", pid);
	f = pid > 0 ? fopen(path, "r") : nullptr;
	if (!f)
		return 1;
	size_t n = fread(cmdline, 1, sizeof cmdline - 1, f);
	fclose(f);
	cmdline[n] = '\0';
	return strstr(cmdline, "tinyhat-status") && !kill(pid, SIGUSR1) ? 0 : 1;
}

int main(int argc, char *argv[])
{
	auto rundir = getenv("XDG_RUNTIME_DIR");
	if (!rundir || !*rundir)
		rundir = "/tmp";
	snprintf(pidfile, sizeof pidfile, "%s/tinyhat-status.pid", rundir);
	if (argc > 1 && !strcmp(argv[1], "--refresh"))
		return refresh();

	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	sigaddset(&set, SIGTERM);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGHUP);
	sigprocmask(SIG_BLOCK, &set, nullptr);

	auto f = fopen(pidfile, "w");
	if (f) {
		fprintf(f, "%d\n", (int)getpid());
		fclose(f);
	}
	if (!access("/run/tinyhat/toram", F_OK) && !mounted("/persist"))
		puts("live 1");

	for (;;) {
		char line[2048];
		status(line, sizeof line, rundir);
		if (printf("%s\n", line) < 0 || fflush(stdout))
			break;
		struct timespec now, wait = { .tv_sec = INTERVAL };
		clock_gettime(CLOCK_REALTIME, &now);
		long long left = (60 - now.tv_sec % 60) * 1000000000LL - now.tv_nsec;
		if (left < INTERVAL * 1000000000LL) {
			wait.tv_sec = left / 1000000000;
			wait.tv_nsec = left % 1000000000;
		}
		int sig = sigtimedwait(&set, nullptr, &wait);
		if (sig == SIGTERM || sig == SIGINT || sig == SIGHUP)
			break;
	}
	unlink(pidfile);
	return 0;
}
