#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	const char *name = strrchr(argv[0], '/');
	char path[512];
	char **args = malloc((size_t)(argc + 8) * sizeof(*args));
	int n = 0;
	name = name ? name + 1 : argv[0];
	snprintf(path, sizeof(path), "/host/usr/bin/%s", name);
	unsetenv("LD_PRELOAD");
	args[n++] = "/lib64/ld-linux-x86-64.so.2";
	args[n++] = "--library-path";
	args[n++] = "/host/usr/lib";
	args[n++] = "--argv0";
	args[n++] = argv[0];
	args[n++] = path;
	for (int i = 1; i < argc; i++)
		args[n++] = argv[i];
	args[n] = NULL;
	execv(args[0], args);
	perror(path);
	return 127;
}
