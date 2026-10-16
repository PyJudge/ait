#ifndef STRING_LIST_H
#define STRING_LIST_H

/* git v2.53.0 string-list.h:68-96 */
struct string_list_item {
	char *string;
	void *util;
};

typedef int (*compare_strings_fn)(const char *, const char *);

/**
 * Represents the list itself.
 *
 * . The array of items are available via the `items` member.
 * . The `nr` member contains the number of items stored in the list.
 * . The `alloc` member is used to avoid reallocating at every insertion.
 *   You should not tamper with it.
 * . Setting the `strdup_strings` member to 1 will strdup() the strings
 *   before adding them, see above.
 * . The `compare_strings_fn` member is used to specify a custom compare
 *   function, otherwise `strcmp()` is used as the default function.
 */
struct string_list {
	struct string_list_item *items;
	size_t nr;
	size_t alloc;
	unsigned int strdup_strings:1;
	compare_strings_fn cmp; /* NULL uses strcmp() */
};

#define STRING_LIST_INIT_NODUP { 0 }
#define STRING_LIST_INIT_DUP   { .strdup_strings = 1 }

/* git v2.53.0 string-list.h:160-163 */
#define for_each_string_list_item(item,list)            \
	for (item = (list)->items;                      \
	     item && item < (list)->items + (list)->nr; \
	     ++item)

enum {
	STRING_LIST_SPLIT_TRIM = (1 << 0),
	STRING_LIST_SPLIT_NONEMPTY = (1 << 1),
};
void string_list_init_dup(struct string_list *list);
struct string_list_item *string_list_insert(struct string_list *list, const char *string);
bool string_list_has_string(const struct string_list *list, const char *string);
void string_list_remove(struct string_list *list, const char *string, int free_util);
struct string_list_item *string_list_lookup(struct string_list *list, const char *string);
void string_list_sort(struct string_list *list);
void string_list_remove_duplicates(struct string_list *list, int free_util);
void string_list_clear(struct string_list *list, int free_util);
struct string_list_item *string_list_append_nodup(struct string_list *list, char *string);
struct string_list_item *string_list_append(struct string_list *list, const char *string);
struct string_list_item *unsorted_string_list_lookup(struct string_list *list, const char *string);
int unsorted_string_list_has_string(struct string_list *list, const char *string);
int string_list_split(struct string_list *list, const char *string, const char *delim, int maxsplit);
#endif