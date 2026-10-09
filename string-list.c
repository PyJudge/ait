#include "git-compat-util.h"
#include "string-list.h"

/* git v2.53.0 string-list.c:10 */
void string_list_init_dup(struct string_list *list)
{
	struct string_list blank = STRING_LIST_INIT_DUP;
	memcpy(list, &blank, sizeof(*list));
}

/* git v2.53.0 string-list.c:18 */
static size_t get_entry_index(const struct string_list *list, const char *string,
			      bool *exact_match)
{
	size_t left = 0, right = list->nr;
	compare_strings_fn cmp = list->cmp ? list->cmp : strcmp;

	while (left < right) {
		size_t middle = left + (right - left) / 2;
		int compare = cmp(string, list->items[middle].string);
		if (compare < 0)
			right = middle;
		else if (compare > 0)
			left = middle + 1;
		else {
			if (exact_match)
				*exact_match = true;
			return middle;
		}
	}

	if (exact_match)
		*exact_match = false;
	return right;
}

/* git v2.53.0 string-list.c:43 */
static size_t add_entry(struct string_list *list, const char *string)
{
	bool exact_match;
	size_t index = get_entry_index(list, string, &exact_match);

	if (exact_match)
		return index;

	ALLOC_GROW(list->items, list->nr+1, list->alloc);
	if (index < list->nr)
		MOVE_ARRAY(list->items + index + 1, list->items + index,
			   list->nr - index);
	list->items[index].string = list->strdup_strings ?
		xstrdup(string) : (char *)string;
	list->items[index].util = NULL;
	list->nr++;

	return index;
}

/* git v2.53.0 string-list.c:63 */
struct string_list_item *string_list_insert(struct string_list *list, const char *string)
{
	size_t index = add_entry(list, string);

	return list->items + index;
}

/* git v2.53.0 string-list.c:70 */
void string_list_remove(struct string_list *list, const char *string,
			int free_util)
{
	bool exact_match;
	int i = get_entry_index(list, string, &exact_match);

	if (exact_match) {
		if (list->strdup_strings)
			free(list->items[i].string);
		if (free_util)
			free(list->items[i].util);

		list->nr--;
		MOVE_ARRAY(list->items + i, list->items + i + 1, list->nr - i);
	}
}

/* git v2.53.0 string-list.c:87 */
bool string_list_has_string(const struct string_list *list, const char *string)
{
	bool exact_match;
	get_entry_index(list, string, &exact_match);
	return exact_match;
}

/* git v2.53.0 string-list.c:100 */
struct string_list_item *string_list_lookup(struct string_list *list, const char *string)
{
	bool exact_match;
	size_t i = get_entry_index(list, string, &exact_match);
	if (!exact_match)
		return NULL;
	return list->items + i;
}

/* git v2.53.0 string-list.c:164 */
void string_list_clear(struct string_list *list, int free_util)
{
	if (list->items) {
		if (list->strdup_strings) {
			for (size_t i = 0; i < list->nr; i++)
				free(list->items[i].string);
		}
		if (free_util) {
			for (size_t i = 0; i < list->nr; i++)
				free(list->items[i].util);
		}
		free(list->items);
	}
	list->items = NULL;
	list->nr = list->alloc = 0;
}

/* git v2.53.0 string-list.c:207 */
struct string_list_item *string_list_append_nodup(struct string_list *list,
						  char *string)
{
	struct string_list_item *retval;
	ALLOC_GROW(list->items, list->nr + 1, list->alloc);
	retval = &list->items[list->nr++];
	retval->string = string;
	retval->util = NULL;
	return retval;
}

/* git v2.53.0 string-list.c:218 */
struct string_list_item *string_list_append(struct string_list *list,
					    const char *string)
{
	return string_list_append_nodup(
			list,
			list->strdup_strings ? xstrdup(string) : (char *)string);
}

/* git v2.53.0 string-list.c:226-241 */
/*
 * Encapsulate the compare function pointer because ISO C99 forbids
 * casting from void * to a function pointer and vice versa.
 */
struct string_list_sort_ctx
{
	compare_strings_fn cmp;
};

static int cmp_items(const void *a, const void *b, void *ctx)
{
	struct string_list_sort_ctx *sort_ctx = ctx;
	const struct string_list_item *one = a;
	const struct string_list_item *two = b;
	return sort_ctx->cmp(one->string, two->string);
}

/* git v2.53.0 string-list.c:243 */
void string_list_sort(struct string_list *list)
{
	struct string_list_sort_ctx sort_ctx = {list->cmp ? list->cmp : strcmp};

	QSORT_S(list->items, list->nr, cmp_items, &sort_ctx);
}

/* git v2.53.0 string-list.c:250 */
struct string_list_item *unsorted_string_list_lookup(struct string_list *list,
						     const char *string)
{
	struct string_list_item *item;
	compare_strings_fn cmp = list->cmp ? list->cmp : strcmp;

	for_each_string_list_item(item, list)
		if (!cmp(string, item->string))
			return item;
	return NULL;
}

/* git v2.53.0 string-list.c:262 */
int unsorted_string_list_has_string(struct string_list *list,
				    const char *string)
{
	return unsorted_string_list_lookup(list, string) != NULL;
}

/* git v2.53.0 string-list.c:282 */
static int append_one(struct string_list *list,
		      const char *p, const char *end,
		      int in_place, unsigned flags)
{
	if (!end)
		end = p + strlen(p);

	if ((flags & STRING_LIST_SPLIT_TRIM)) {
		/* rtrim */
		for (; p < end; end--)
			if (!isspace(end[-1]))
				break;
	}

	if ((flags & STRING_LIST_SPLIT_NONEMPTY) && (end <= p))
		return 0;

	if (in_place) {
		*((char *)end) = '\0';
		string_list_append(list, p);
	} else {
		string_list_append_nodup(list, xmemdupz(p, end - p));
	}
	return 1;
}

/* git v2.53.0 string-list.c:318 */
static int split_string(struct string_list *list, const char *string, const char *delim,
			int maxsplit, int in_place, unsigned flags)
{
	int count = 0;
	const char *p = string;

	if (in_place && list->strdup_strings)
		BUG("string_list_split_in_place() called with strdup_strings");
	else if (!in_place && !list->strdup_strings)
		BUG("string_list_split() called without strdup_strings");

	for (;;) {
		char *end;

		if (flags & STRING_LIST_SPLIT_TRIM) {
			/* ltrim */
			while (*p && isspace(*p))
				p++;
		}

		if (0 <= maxsplit && maxsplit <= count)
			end = NULL;
		else
			end = strpbrk(p, delim);

		count += append_one(list, p, end, in_place, flags);

		if (!end)
			return count;
		p = end + 1;
	}
}

/* git v2.53.0 string-list.c:351 */
int string_list_split(struct string_list *list, const char *string,
		      const char *delim, int maxsplit)
{
	return split_string(list, string, delim, maxsplit, 0, 0);
}
