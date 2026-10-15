/* Pause only scheduling: unlink and replacement-lock creation remain real. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifndef __APPLE__
#include <dlfcn.h>
#endif

static int after_unlink(const char *path, int result)
{
	static volatile sig_atomic_t paused;
	const char *target, *ready, *proceed;
	int fd, i, saved = errno;

	if (result || paused)
		return result;
	target = getenv("AIT_TEST_RELEASE_LOCK");
	ready = getenv("AIT_TEST_RELEASE_READY");
	proceed = getenv("AIT_TEST_RELEASE_PROCEED");
	if (!target || !ready || !proceed || strcmp(path, target)) {
		errno = saved;
		return result;
	}
	paused = 1;
	fd = open(ready, O_CREAT | O_WRONLY, 0666);
	if (fd >= 0)
		close(fd);
	for (i = 0; i < 10000 && access(proceed, F_OK); i++)
		usleep(1000);
	if (!access(proceed, F_OK))
		raise(SIGTERM);
	errno = saved;
	return result;
}

#ifdef __APPLE__
static int pause_stage_unlink(const char *path)
{
	return after_unlink(path, unlink(path));
}

__attribute__((used)) static struct {
	const void *replacement;
	const void *replacee;
} stage_unlink_interpose __attribute__((section("__DATA,__interpose"))) = {
	pause_stage_unlink, unlink
};
#else
int unlink(const char *path)
{
	static int (*real_unlink)(const char *);

	if (!real_unlink)
		real_unlink = dlsym(RTLD_NEXT, "unlink");
	if (!real_unlink)
		_exit(127);
	return after_unlink(path, real_unlink(path));
}
#endif
