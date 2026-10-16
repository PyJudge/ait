#include "ait.h"

#define BASE "0000000000000000000000000000000000000000"

static void check_scopes(int copied)
{
	const char *given[] = { "z.c", "a.c:10-12 " BASE, "a.c", "a.c:2-4 " BASE,
		"a.c:2-3 " BASE, "a.c:2-3 " BASE, "new:b.c", "dir/", "/" };
	const char *want = "/,a.c,a.c:2-3 " BASE ",a.c:2-3 " BASE ",a.c:2-4 " BASE
		",a.c:10-12 " BASE ",new:b.c,dir/,z.c";
	struct scope_list scopes = { 0 }, copy = { 0 };
	struct scope before[ARRAY_SIZE(given)], *items;
	size_t i, alloc;
	char *joined;

	for (i = 0; i < ARRAY_SIZE(given); i++) {
		struct scope value;
		char *err = scope_parse(&value, given[i], 1, 1, SCOPE_STORED);

		if (err)
			die("invalid scope fixture: %s", err);
		scope_append(&scopes, &value);
		scope_clear(&value);
	}
	if (copied) {
		scopes_copy(&copy, &scopes);
		for (i = 0; i < scopes.nr; i++)
			if (copy.items[i].path == scopes.items[i].path || copy.items[i].raw == scopes.items[i].raw)
				die("copy borrowed its source scope storage");
		scopes_clear(&scopes);
		scopes = copy;
	}
	items = scopes.items;
	alloc = scopes.alloc;
	memcpy(before, items, sizeof(before));
	joined = scopes_join(&scopes);
	if (strcmp(joined, want))
		die("scope output is not in canonical path and range order: %s", joined);
	if (scopes.items != items || scopes.nr != ARRAY_SIZE(given) || scopes.alloc != alloc)
		die("serializing scopes changed the original list storage");
	for (i = 0; i < ARRAY_SIZE(given); i++)
		if (memcmp(&scopes.items[i], &before[i], sizeof(before[i])) || strcmp(scopes.items[i].raw, given[i]))
			die("serializing scopes changed original item %d", (int)i);
	free(joined);
	joined = scopes_join(&scopes);
	if (strcmp(joined, want))
		die("serializing scopes twice changed the output");
	free(joined);
	scopes_clear(&scopes);
}

int main(int argc, const char **argv)
{
	if (argc != 2)
		return 2;
	if (!strcmp(argv[1], "empty")) {
		struct scope_list empty = { 0 };
		char *joined = scopes_join(&empty);

		if (*joined || empty.items || empty.nr || empty.alloc)
			die("serializing empty scopes changed the list or returned nonempty output");
		free(joined);
	} else {
		check_scopes(!strcmp(argv[1], "owned"));
	}
	return 0;
}
