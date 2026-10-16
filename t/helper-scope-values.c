/* Count real coordinate parses in the read consumers after all input was loaded. */
#include "../contract.c"

static unsigned long coordinate_reads;
void __cyg_profile_func_enter(void *fn, void *caller) __attribute__((no_instrument_function));
void __cyg_profile_func_exit(void *fn, void *caller) __attribute__((no_instrument_function));
void __cyg_profile_func_enter(void *fn, void *caller UNUSED)
{
	if (fn == (void *)range_numbers)
		coordinate_reads++;
}
void __cyg_profile_func_exit(void *fn UNUSED, void *caller UNUSED)
{
}

int main(void)
{
	struct tree *t = tree_load("HEAD");
	struct branch *b = branch_load("ait/master", NULL, NULL);
	struct contract *c = b->contracts.items[0].util;
	const struct scope *item;
	struct scope query;
	char *err;
	unsigned long before;
	int i;

	if ((err = scope_parse(&query, "src/stock.c:2-4", 1, 1, SCOPE_ARGUMENT)) || scope_pin(&query, t, &err))
		die("fixture scope pin failed: %s", err);
	before = coordinate_reads;
	for (i = 0; i < 10; i++) {
		struct scope_location where;

		free(scopes_join(&c->consumer));
		free(scopes_join(&c->provider));
		free(scopes_bare(&c->provider));
		free(scopes_display(&c->consumer, t));
		free(provider_display(b, c, t));
		if (judge(b, c, t) != V_CURRENT || !contract_overlaps(c, &query, t, 0) || touched(b, c, t, t))
			die("read consumers changed the verified scope judgment");
		for_each_scope(item, &c->provider)
			if (scope_locate(item, t, &where) != FOUND)
				die("a loaded provider no longer locates");
	}
	if (coordinate_reads != before)
		die("read consumers reparsed coordinates %lu times", coordinate_reads - before);
	puts("no coordinate reparsing after input");
	return 0;
}
