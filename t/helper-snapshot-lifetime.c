/* Snapshot owners must not alias a process-global mutable cache. */
#include "ait.h"
#include <sys/resource.h>
#ifdef __APPLE__
#include <malloc/malloc.h>
#endif

int main(int argc UNUSED, const char **argv)
{
	if (!strcmp(argv[1], "repeat") || !strcmp(argv[1], "many")) {
		struct rusage usage;
		long initial, peak;
		int i, expected = !strcmp(argv[1], "many") ? 100 : 1;
#ifdef __APPLE__
		malloc_statistics_t statistics;
		size_t initial_live = 0;
#endif

		getrusage(RUSAGE_SELF, &usage);
		initial = usage.ru_maxrss;
		for (i = 1; i <= 200; i++) {
			struct branch *snapshot = branch_load("ait/master", NULL, NULL);

			if (snapshot->contracts.nr != expected)
				die("a repeated snapshot lost its contract");
			branch_free(snapshot);
#ifdef __APPLE__
			if (i == 1) {
				malloc_zone_statistics(NULL, &statistics);
				initial_live = statistics.size_in_use;
			}
#endif
			if (i == 10 || i == 40 || i == 200) {
				getrusage(RUSAGE_SELF, &usage);
				printf("rss %d %ld\n", i, usage.ru_maxrss);
			}
		}
#ifdef __APPLE__
		malloc_zone_statistics(NULL, &statistics);
		printf("live growth %zu\n", statistics.size_in_use > initial_live ? statistics.size_in_use - initial_live : 0);
		if (!getenv("ASAN_OPTIONS") && statistics.size_in_use > initial_live + 32768)
			die("fixed snapshot parser retained %zu bytes", statistics.size_in_use - initial_live);
#endif
		getrusage(RUSAGE_SELF, &usage);
		peak = usage.ru_maxrss - initial;
#ifndef __APPLE__
		peak *= 1024;
#endif
		if (!getenv("ASAN_OPTIONS") && peak > 8 * 1024 * 1024)
			die("fixed snapshot reads retained %ld bytes", peak);
	} else if (!strcmp(argv[1], "branch")) {
		struct branch *a = branch_load("ait/master", NULL, NULL);
		struct branch *b = branch_load("ait/master", NULL, NULL);
		struct contract *ca = a->contracts.items[0].util;
		struct contract *cb = b->contracts.items[0].util;

		ca->creator[0] = 'X';
		if (a == b || cb->creator[0] == 'X')
			die("independent branch owners share a process cache");
		branch_free(a);
		if (cb->creator[0] == 'X' || !*cb->specs[0].body)
			die("releasing one branch invalidated another");
		branch_free(b);
	} else {
		struct tree *a = tree_load("HEAD"), *b = tree_load("HEAD");

		a->e[0].mode[0] = 'X';
		if (a == b || b->e[0].mode[0] == 'X')
			die("independent tree owners share a process cache");
		tree_free(a);
		if (!*b->e[0].path || b->e[0].mode[0] == 'X')
			die("releasing one tree invalidated another");
		tree_free(b);
	}
	return 0;
}
