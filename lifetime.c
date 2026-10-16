/* Snapshot owners release their values, as Git object.c:557 does for pools. */
#include "ait.h"

void tree_free(struct tree *t)
{
	size_t i;

	if (!t)
		return;
	for (i = 0; i < t->nr; i++)
		free((char *)t->e[i].path);
	free(t->e);
	free(t);
}

void branch_free(struct branch *b)
{
	struct string_list_item *item;

	if (!b)
		return;
	for_each_string_list_item(item, &b->contracts)
		contract_free(item->util);
	for_each_string_list_item(item, &b->fulfils)
		fulfilment_free(item->util);
	string_list_clear(&b->contracts, 0);
	string_list_clear(&b->fulfils, 0);
	string_list_clear(&b->errors, 1);
	tree_free(b->tree);
	free((char *)b->name);
	free(b);
}
