#include "ait.h"

#define BASE "0000000000000000000000000000000000000000"

static void check_scopes(int owned)
{
	const char *given[] = { "z.c", "a.c:10-12 " BASE, "a.c", "a.c:2-4 " BASE,
				"a.c:2-3 " BASE, "a.c:2-3 " BASE };
	const char *want = "a.c,a.c:2-3 " BASE ",a.c:2-3 " BASE ",a.c:2-4 " BASE ",a.c:10-12 " BASE ",z.c";
	struct string_list scopes = STRING_LIST_INIT_NODUP;
	struct string_list_item before[ARRAY_SIZE(given)], *items;
	size_t i, alloc;
	int markers[ARRAY_SIZE(given)];
	char *joined;

	scopes.strdup_strings = owned;
	for (i = 0; i < ARRAY_SIZE(given); i++)
		string_list_append(&scopes, given[i])->util = &markers[i];
	items = scopes.items;
	alloc = scopes.alloc;
	memcpy(before, items, sizeof(before));
	joined = scopes_join(&scopes);
	if (strcmp(joined, want))
		die("scope output is not in canonical path and range order: %s", joined);
	if (scopes.items != items || scopes.nr != ARRAY_SIZE(given) || scopes.alloc != alloc ||
	    scopes.strdup_strings != owned)
		die("serializing scopes changed the original list's storage or ownership");
	for (i = 0; i < ARRAY_SIZE(given); i++)
		if (scopes.items[i].string != before[i].string || scopes.items[i].util != before[i].util ||
		    strcmp(scopes.items[i].string, given[i]))
			die("serializing scopes changed original item %d", (int)i);
	free(joined);
	joined = scopes_join(&scopes);
	if (strcmp(joined, want))
		die("serializing scopes twice changed the output");
	free(joined);
	string_list_clear(&scopes, 0);
}

int main(int argc, const char **argv)
{
	if (argc != 2)
		return 2;
	if (!strcmp(argv[1], "empty")) {
		struct string_list empty = STRING_LIST_INIT_NODUP;
		char *joined = scopes_join(&empty);

		if (*joined || empty.items || empty.nr || empty.alloc || empty.strdup_strings)
			die("serializing empty scopes changed the list or returned nonempty output");
		free(joined);
	} else {
		check_scopes(!strcmp(argv[1], "owned"));
	}
	return 0;
}
