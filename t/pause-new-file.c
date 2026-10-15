/* Pause scheduling before real exclusive creation or the old final rename. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifndef __APPLE__
#include <dlfcn.h>
#endif

static void before_new_file(const char *path)
{
	static volatile sig_atomic_t paused;
	const char *prefix = getenv("AIT_TEST_NEW_PREFIX");
	const char *ready = getenv("AIT_TEST_NEW_READY");
	const char *proceed = getenv("AIT_TEST_NEW_PROCEED");
	int fd, i, saved = errno;

	if (paused || !prefix || !ready || !proceed || strncmp(path, prefix, strlen(prefix)))
		return;
	paused = 1;
	fd = open(ready, O_CREAT | O_WRONLY | O_EXCL, 0666);
	if (fd >= 0) {
		write(fd, path, strlen(path));
		close(fd);
	}
	for (i = 0; i < 10000 && access(proceed, F_OK); i++)
		usleep(1000);
	errno = saved;
}

#ifdef __APPLE__
static int pause_new_open(const char *path, int flags, ...)
{
	va_list ap;
	mode_t mode = 0;

	if (flags & O_CREAT) {
		va_start(ap, flags);
		mode = va_arg(ap, int);
		va_end(ap);
	}
	if ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL))
		before_new_file(path);
	return open(path, flags, mode);
}

static int pause_new_rename(const char *from, const char *to)
{
	before_new_file(to);
	return rename(from, to);
}

__attribute__((used)) static struct {
	const void *replacement;
	const void *replacee;
} new_file_interpose[] __attribute__((section("__DATA,__interpose"))) = {
	{ pause_new_open, open },
	{ pause_new_rename, rename }
};
#else
int open(const char *path, int flags, ...)
{
	static int (*real_open)(const char *, int, ...);
	va_list ap;
	mode_t mode = 0;

	if (!real_open)
		real_open = dlsym(RTLD_NEXT, "open");
	if (!real_open)
		_exit(127);
	if (flags & O_CREAT) {
		va_start(ap, flags);
		mode = va_arg(ap, int);
		va_end(ap);
	}
	if ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL))
		before_new_file(path);
	return real_open(path, flags, mode);
}

int rename(const char *from, const char *to)
{
	static int (*real_rename)(const char *, const char *);

	if (!real_rename)
		real_rename = dlsym(RTLD_NEXT, "rename");
	if (!real_rename)
		_exit(127);
	before_new_file(to);
	return real_rename(from, to);
}
#endif
