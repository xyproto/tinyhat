#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>

extern char end[], __heap_limit[];

void *_sbrk(ptrdiff_t incr)
{
	static char *brk = end;
	char *prev = brk;

	if (incr > __heap_limit - brk || incr < end - brk) {
		errno = ENOMEM;
		return (void *)-1;
	}
	brk += incr;
	return prev;
}

int _write(int fd, const void *buf, size_t n)
{
	(void)fd;
	(void)buf;
	return (int)n;
}

int _read(int fd, void *buf, size_t n)
{
	(void)fd;
	(void)buf;
	(void)n;
	return 0;
}

int _close(int fd)
{
	(void)fd;
	errno = EBADF;
	return -1;
}

int _lseek(int fd, int offset, int whence)
{
	(void)fd;
	(void)offset;
	(void)whence;
	errno = ESPIPE;
	return -1;
}

int _fstat(int fd, struct stat *st)
{
	(void)fd;
	st->st_mode = S_IFCHR;
	return 0;
}

int _isatty(int fd)
{
	(void)fd;
	return 1;
}

int _getpid(void)
{
	return 1;
}

int _kill(int pid, int sig)
{
	(void)pid;
	(void)sig;
	errno = EINVAL;
	return -1;
}

void _exit(int status)
{
	(void)status;
	for (;;)
		;
}
