/* Internal copy and comparison must operate on values, without YAML IO. */
#include "../request.c"

static unsigned long yaml_calls;
void __cyg_profile_func_enter(void *fn, void *caller) __attribute__((no_instrument_function));
void __cyg_profile_func_exit(void *fn, void *caller) __attribute__((no_instrument_function));
void __cyg_profile_func_enter(void *fn, void *caller UNUSED)
{
	if (fn == (void *)yaml_parse || fn == (void *)contract_text)
		yaml_calls++;
}
void __cyg_profile_func_exit(void *fn UNUSED, void *caller UNUSED)
{
}

int main(int argc UNUSED, const char **argv)
{
	struct branch *b = branch_load("ait/master", NULL, NULL);
	struct contract *c = b->contracts.items[0].util;
	struct request req;
	const char *words[] = { "future", NULL };
	int i;

	top = getcwd(NULL, 0);
	request_init(&req, words);
	req.input.rid = "R000000000000";
	if (!strcmp(argv[1], "copy")) {
		yaml_calls = 0;
		for (i = 0; i < 5; i++) {
			struct contract *copy = contract_copy(c);

			stamp(&req.input, copy, c, 0);
			if (strcmp(copy->condition_id, c->condition_id) || strcmp(copy->change_id, c->change_id))
				die("an equal value acquired new IDs");
		}
		if (yaml_calls)
			die("internal copy/comparison performed %lu YAML operations", yaml_calls);
		puts("no YAML IO inside copy/comparison");
	} else if (!strcmp(argv[1], "sets")) {
		struct contract *old = contract_new("offer", "", "O000000000001");
		struct contract *copy;

		string_list_append(&old->undertakes, "D000000000001");
		copy = contract_copy(old);
		string_list_append(&copy->undertakes, "D000000000001");
		stamp(&req.input, copy, old, 0);
		if (strcmp(copy->condition_id, old->condition_id) || strcmp(copy->change_id, old->change_id))
			die("repeated set members changed contract identity");
	} else {
		struct contract *a = contract_new("offer", "", "O000000000001");
		struct contract *other = contract_new("offer", "", "O000000000002");

		specs_from_files(&req.input, a, "cond.yaml");
		specs_from_files(&req.input, other, "cond.yaml");
		a->specs[0].body[0] = 'X';
		if (other->specs[0].body[0] == 'X' || ((struct spec *)req.input.files.items[0].util)->body[0] == 'X')
			die("a candidate mutated another candidate or the fixed condition input");
		puts("candidate specs own independent values");
	}
	return 0;
}
