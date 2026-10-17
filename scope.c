/* Scope input values and their spelling (spec 3); consumers never decode text. */
#include "ait.h"

void scope_clear(struct scope *s)
{
	free(s->path);
	memset(s, 0, sizeof(*s));
}

static void scope_copy(struct scope *out, const struct scope *s)
{
	*out = *s;
	out->path = xstrdup(s->path);
}

void scope_append(struct scope_list *out, const struct scope *s)
{
	ALLOC_GROW(out->items, out->nr + 1, out->alloc);
	scope_copy(&out->items[out->nr++], s);
}

void scopes_clear(struct scope_list *list)
{
	struct scope *s;

	for_each_scope(s, list)
		scope_clear(s);
	free(list->items);
	memset(list, 0, sizeof(*list));
}

void scopes_copy(struct scope_list *out, const struct scope_list *from)
{
	const struct scope *s;

	scopes_clear(out);
	for_each_scope(s, from)
		scope_append(out, s);
}

/* Input tuple identity ignores the pin; a recorded code key includes it. */
int scope_equal(const struct scope *a, const struct scope *b, int with_base)
{
	return a->kind == b->kind && !strcmp(a->path, b->path) && a->first == b->first && a->last == b->last &&
	       (!with_base || !strcmp(a->base, b->base));
}

/* LIST holds S (its base too, WITH_BASE) */
int scopes_has(const struct scope_list *list, const struct scope *s, int with_base)
{
	const struct scope *had;

	for_each_scope(had, list)
		if (scope_equal(had, s, with_base))
			return 1;
	return 0;
}

char *scope_text(const struct scope *s, int with_base)
{
	if (s->kind == SCOPE_RANGE)
		return xstrfmt("%s:%d-%d%s%s", s->path, s->first, s->last,
			      with_base && *s->base ? " " : "", with_base ? s->base : "");
	return xstrfmt("%s%s", s->kind == SCOPE_NEW ? "new:" : "", s->path);
}

/* Canonical positive line numbers, shared by scopes and stored code spans. */
const char *range_numbers(const char *text, int *first, int *last)
{
	char *next;
	long a, b;

	if (*text < '1' || *text > '9')
		return NULL;
	errno = 0;
	a = strtol(text, &next, 10);
	if (*next++ != '-' || *next < '1' || *next > '9')
		return NULL;
	b = strtol(next, &next, 10);
	if (errno || a > INT_MAX || b < a || b > INT_MAX)
		return NULL;
	*first = a;
	*last = b;
	return next;
}

/* Command, draft and stored permissions are decided only at this boundary. */
char *scope_parse(struct scope *s, const char *text, int allow_new, int allow_abs, enum scope_form form)
{
	const char *p = text, *base = strchr(text, ' '), *colon, *end;
	size_t len = base ? (size_t)(base - text) : strlen(text);
	int is_new = skip_prefix(p, "new:", &p), valid;

	memset(s, 0, sizeof(*s));
	len -= p - text;
	colon = memchr(p, ':', len);
	s->path = xstrndup(p, colon ? (size_t)(colon - p) : len);
	if (colon) {
		s->kind = SCOPE_RANGE;
		end = range_numbers(colon + 1, &s->first, &s->last);
		valid = !is_new && end == p + len && path_ok(s->path);
		if (base)
			valid &= form != SCOPE_ARGUMENT && oid_ok(base + 1);
		else
			valid &= form != SCOPE_STORED;
		if (valid && base)
			snprintf(s->base, sizeof(s->base), "%s", base + 1);
	} else {
		s->kind = is_new ? SCOPE_NEW : !strcmp(s->path, "/") ? SCOPE_ROOT :
			  ends_with(s->path, "/") ? SCOPE_DIR : SCOPE_FILE;
		valid = !base;
		if (s->kind == SCOPE_NEW) {
			valid &= allow_new && path_ok(s->path);
		} else if (s->kind == SCOPE_ROOT) {
			valid &= allow_abs;
		} else if (s->kind == SCOPE_DIR) {
			char *dir = xstrndup(s->path, strlen(s->path) - 1);

			valid &= allow_abs && path_ok(dir);
			free(dir);
		} else {
			valid &= path_ok(s->path);
		}
	}
	if (valid)
		return NULL;
	scope_clear(s);
	return xstrfmt("'%s' is not a scope", text);
}

/* v14 3.2a: a list of scopes joined by ","; the same scope twice is an error */
char *scopes_parse(struct scope_list *out, const char *text, int allow_new, int allow_abs, enum scope_form form)
{
	struct string_list words = STRING_LIST_INIT_DUP;
	struct string_list_item *word;
	char *err = NULL;

	if (*text)
		string_list_split(&words, text, ",", -1);
	for_each_string_list_item(word, &words) {
		struct scope s;

		if ((err = scope_parse(&s, word->string, allow_new, allow_abs, form)))
			break;
		if (scopes_has(out, &s, 0)) {
			char *key = scope_text(&s, 0);

			err = xstrfmt("'%s' is given twice", key);
			free(key);
			scope_clear(&s);
			break;
		}
		ALLOC_GROW(out->items, out->nr + 1, out->alloc);
		out->items[out->nr++] = s;
	}
	string_list_clear(&words, 0);
	if (err)
		scopes_clear(out);
	return err;
}

/* v14 3.2a: PATH byte order, whole file first, then numerical line coordinates. */
static int cmp_scope(const void *pa, const void *pb)
{
	const struct scope *a = *(const struct scope *const *)pa, *b = *(const struct scope *const *)pb;
	int path = strcmp(a->path, b->path), ar = a->kind == SCOPE_RANGE, br = b->kind == SCOPE_RANGE;

	if (path)
		return path;
	if (ar != br)
		return ar - br;
	return a->first != b->first ? (a->first > b->first ? 1 : -1) :
	       a->last != b->last ? (a->last > b->last ? 1 : -1) : 0;
}

const struct scope **scope_order(const struct scope_list *list)
{
	const struct scope **ordered = xcalloc(list->nr, sizeof(*ordered));
	size_t i;

	for (i = 0; i < list->nr; i++)
		ordered[i] = &list->items[i];
	QSORT(ordered, list->nr, cmp_scope);
	return ordered;
}

/* v14 3.2a: the scope list as stored (WITH_BASE) or as hints repeat it */
char *scopes_text(const struct scope_list *list, int with_base)
{
	const struct scope **ordered = scope_order(list);
	struct strbuf out = STRBUF_INIT;
	size_t i;

	for (i = 0; i < list->nr; i++) {
		char *text = scope_text(ordered[i], with_base);

		strbuf_addf(&out, "%s%s", i ? "," : "", text);
		free(text);
	}
	free(ordered);
	return strbuf_detach(&out, NULL);
}

void code_add(struct code_list *out, const struct scope *s, const struct recorded_code *value)
{
	struct scoped_code *entry;

	ALLOC_GROW(out->items, out->nr + 1, out->alloc);
	entry = &out->items[out->nr++];
	scope_copy(&entry->scope, s);
	entry->value = *value;
}

const struct recorded_code *code_lookup(const struct code_list *code, const struct scope *s)
{
	size_t i;

	for (i = 0; i < code->nr; i++)
		if (scope_equal(&code->items[i].scope, s, 1))
			return &code->items[i].value;
	return NULL;
}

void code_clear(struct code_list *code)
{
	size_t i;

	for (i = 0; i < code->nr; i++)
		scope_clear(&code->items[i].scope);
	free(code->items);
	memset(code, 0, sizeof(*code));
}
