/* A fixed receiving state must not accumulate per-round helper buffers. */
#define main ait_command_main
#include "../ait.c"
#undef main
#include "../notify.c"
#include <sys/resource.h>
#ifdef __APPLE__
#include <malloc/malloc.h>
#endif

int main(void)
{
	struct rusage usage;
	long initial;
	int i, warned = 0;
#ifdef __APPLE__
	malloc_statistics_t statistics;
	size_t initial_live;
#endif

	setup();
	session(1);
	deliver(&warned);
	getrusage(RUSAGE_SELF, &usage);
	initial = usage.ru_maxrss;
#ifdef __APPLE__
	malloc_zone_statistics(NULL, &statistics);
	initial_live = statistics.size_in_use;
#endif
	for (i = 0; i < 200; i++)
		deliver(&warned);
#ifdef __APPLE__
	malloc_zone_statistics(NULL, &statistics);
	printf("empty-round live growth %zu\n", statistics.size_in_use > initial_live ? statistics.size_in_use - initial_live : 0);
	if (!getenv("ASAN_OPTIONS") && statistics.size_in_use > initial_live + 32768)
		die("fixed rounds retained %zu bytes", statistics.size_in_use - initial_live);
#endif
	getrusage(RUSAGE_SELF, &usage);
	initial = usage.ru_maxrss - initial;
#ifndef __APPLE__
	initial *= 1024;
#endif
	if (!getenv("ASAN_OPTIONS") && initial > 8 * 1024 * 1024)
		die("fixed rounds retained %ld RSS bytes", initial);
	return 0;
}
