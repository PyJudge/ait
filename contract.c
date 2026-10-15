/*
 * Trees, contracts, fulfilments and what is judged from them (spec 2, 3, 5).
 * Contracts and fulfilments are stored facts; the rest is computed (rule 1).
 */
#include "ait.h"
#include <yaml.h>
#include "utf8.h"

static char *xstrdup_null(const char *s)
{
	return s ? xstrdup(s) : NULL;
}

/* git's "return error(...)" for the parsers: ERR says why, NULL is returned */
static void *bad(char **err, char *why)
{
	*err = why;
	return NULL;
}

static int cmp_entry(const void *a, const void *b)
{
	return strcmp(((const struct entry *)a)->path, ((const struct entry *)b)->path);
}

/* one sorted listing per tree (git ls-tree -r -t -z) */
struct tree *tree_load(const char *treeish)
{
	static struct string_list listed = STRING_LIST_INIT_DUP;
	struct string_list_item *hit;
	struct string_list records = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct tree *t;
	size_t alloc = 0;
	char oid[48];

	if (!object(xstrfmt("%s^{tree}", treeish), 0, oid, NULL))
		reject(128, NULL, "git failed: 'git cat-file': not a tree: '%s'", treeish);
	if ((hit = string_list_lookup(&listed, oid)))
		return hit->util;
	t = xcalloc(1, sizeof(*t));
	snprintf(t->oid, sizeof(t->oid), "%s", oid);
	string_list_insert(&listed, oid)->util = t;
	git_records(&records, NULL, NULL, "ls-tree", "-r", "-t", "-z", "--full-tree", oid);
	for_each_string_list_item(item, &records) {
		struct entry *e;
		const char *p = item->string;

		ALLOC_GROW(t->e, t->nr + 1, alloc);
		e = &t->e[t->nr++];
		if (sscanf(p, "%7s %7s %47s", e->mode, e->type, e->oid) != 3)
			BUG("git ls-tree: '%s'", p);
		e->path = strchr(p, '\t') + 1;
	}
	QSORT(t->e, t->nr, cmp_entry);
	return t;
}

static struct entry *tree_get(struct tree *t, const char *path)
{
	struct entry key = { path };

	return bsearch(&key, t->e, t->nr, sizeof(*t->e), cmp_entry);
}

const char *blob_at(struct tree *t, const char *path)
{
	struct entry *e = tree_get(t, path);

	return e && !strcmp(e->type, "blob") ? e->oid : NULL;
}

/*
 * spec 5.1: the tree X of the index; an unmerged path is read as HEAD has
 * it, as git write-tree cannot write an index with one
 */
struct tree *index_tree(void)
{
	static struct tree *x;
	struct strbuf info = STRBUF_INIT;
	struct string_list records = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	const char *last = "";
	char *p;

	if (x)
		return x;
	git_records(&records, NULL, NULL, "ls-files", "-u", "-z");
	for_each_string_list_item(item, &records) {
		const char *path = strchr(item->string, '\t') + 1;
		struct entry *e = head ? tree_get(tree_load(head), path) : NULL;

		/* the stages of a path come one after another */
		if (!strcmp(path, last))
			continue;
		last = path;
		warning("'%s' is unmerged; it is read as HEAD has it", path);
		strbuf_addf(&info, "0 %s\t%s%c", NULL_OID, path, 0);
		if (e)
			strbuf_addf(&info, "%s %s\t%s%c", e->mode, e->oid, path, 0);
	}
	if (!info.len)
		return x = tree_load(line1(git("write-tree")));
	p = git("ls-files", "-s", "-z");
	strbuf_splice(&info, 0, 0, p, git_out_len);
	return x = tree_load(tree_with(EMPTY_TREE, &info));
}

/* the YAML subset of v14 2.5.1, read by libyaml (moved from v14 record.c) */
#define ROOT "\\"
#define YAML_DEPTH 16

struct yval {
	char *s;
	char kind;		/* 'l' a list, 'm' a map, '?' a key with nothing under it, 0 a scalar */
	int line;
};

static char *yaml_path(const char *path, const char *key)
{
	struct strbuf sb = STRBUF_INIT;

	if (path)
		strbuf_addf(&sb, "%s.", path);
	for (; *key; key++) {
		if (*key == '.' || *key == '\\')
			strbuf_addch(&sb, '\\');
		strbuf_addch(&sb, *key);
	}
	return strbuf_detach(&sb, NULL);
}

/* Decode one scalar key component escaped by yaml_path(). */
char *yaml_key_decode(const char *key)
{
	struct strbuf sb = STRBUF_INIT;

	for (; *key; key++)
		if (*key != '\\' || !key[1])
			strbuf_addch(&sb, *key);
		else
			strbuf_addch(&sb, *++key);
	return strbuf_detach(&sb, NULL);
}

static const char *decorated(const yaml_char_t *anchor, const yaml_char_t *tag)
{
	return anchor ? "an anchor" : tag ? "a tag" : NULL;
}

static const char *yaml_outside(yaml_event_t *e, int docs)
{
	switch (e->type) {
	case YAML_ALIAS_EVENT:
		return "an alias";
	case YAML_DOCUMENT_START_EVENT:
		if (docs)
			return "more than one document";
		return e->data.document_start.version_directive ||
		       e->data.document_start.tag_directives.start != e->data.document_start.tag_directives.end ?
		       "a directive" : NULL;
	case YAML_SCALAR_EVENT:
		if (e->data.scalar.style == YAML_FOLDED_SCALAR_STYLE && !decorated(e->data.scalar.anchor, e->data.scalar.tag))
			return "a folded block";
		return decorated(e->data.scalar.anchor, e->data.scalar.tag);
	case YAML_SEQUENCE_START_EVENT:
		return decorated(e->data.sequence_start.anchor, e->data.sequence_start.tag);
	case YAML_MAPPING_START_EVENT:
		return decorated(e->data.mapping_start.anchor, e->data.mapping_start.tag);
	default:
		return NULL;
	}
}

struct yframe {
	char *path, *key;
	int n;
	char kind;
};

static char *yaml_place(struct yframe *frame, yaml_event_t *e, const char **why)
{
	char *path;

	if (!frame) {
		if (e->type != YAML_MAPPING_START_EVENT)
			*why = "no key";
		return NULL;
	}
	if (frame->kind == 'l')
		return yaml_path(frame->path, xstrfmt("%d", frame->n++));
	if (frame->key) {
		path = yaml_path(frame->path, frame->key);
		frame->key = NULL;
		return path;
	}
	if (e->type == YAML_SCALAR_EVENT && e->data.scalar.style != YAML_LITERAL_SCALAR_STYLE)
		frame->key = xstrdup((char *)e->data.scalar.value);
	else
		*why = "a key that is not a scalar";
	return NULL;
}

static char yaml_kind(yaml_event_t *e)
{
	if (e->type == YAML_MAPPING_START_EVENT)
		return 'm';
	if (e->type == YAML_SEQUENCE_START_EVENT)
		return 'l';
	return e->data.scalar.style == YAML_PLAIN_SCALAR_STYLE && !e->data.scalar.length ? '?' : 0;
}

static const char *yaml_event(struct string_list *kv, struct yframe *stack, int *depth, yaml_event_t *e)
{
	const char *why = NULL;
	char *path;

	if (e->type == YAML_MAPPING_END_EVENT || e->type == YAML_SEQUENCE_END_EVENT)
		(*depth)--;
	if (e->type != YAML_SCALAR_EVENT && e->type != YAML_MAPPING_START_EVENT && e->type != YAML_SEQUENCE_START_EVENT)
		return NULL;
	if (e->type == YAML_SCALAR_EVENT && strlen((char *)e->data.scalar.value) != e->data.scalar.length)
		return "a NUL character";
	path = yaml_place(*depth ? &stack[*depth - 1] : NULL, e, &why);
	if (why)
		return why;
	if (!*depth)
		path = xstrdup(ROOT);
	if (path && unsorted_string_list_has_string(kv, path))
		return xstrfmt("duplicate key '%s'", path);
	if (path) {
		struct yval *v = xcalloc(1, sizeof(*v));

		v->s = xstrdup(e->type == YAML_SCALAR_EVENT ? (char *)e->data.scalar.value : "");
		v->kind = yaml_kind(e);
		v->line = e->start_mark.line + 1;
		string_list_append(kv, path)->util = v;
	}
	if (e->type == YAML_SCALAR_EVENT)
		return NULL;
	if (*depth == YAML_DEPTH)
		return "nested too deeply";
	stack[*depth].path = strcmp(path, ROOT) ? path : NULL;
	stack[*depth].key = NULL;
	stack[*depth].n = 0;
	stack[*depth].kind = yaml_kind(e);
	(*depth)++;
	return NULL;
}

static size_t line_at(const char *text, size_t offset)
{
	size_t n = 0, i;

	for (i = 0; i < offset && text[i]; i++)
		n += text[i] == '\n';
	return n;
}

/* v14 2.5.1: UTF-8, and LF the only line break */
static const char *not_text(const char *text, size_t *line)
{
	static const char *const breaks[] = { "\r", "\xc2\x85", "\xe2\x80\xa8", "\xe2\x80\xa9", NULL };
	struct string_list lines = STRING_LIST_INIT_DUP;
	const char *p, *first = NULL;
	size_t n;
	int i;

	string_list_split(&lines, text, "\n", -1);
	for (n = 0; n < lines.nr; n++)
		if (!is_utf8(lines.items[n].string)) {
			*line = n;
			return "not UTF-8";
		}
	for (i = 0; breaks[i]; i++)
		if ((p = strstr(text, breaks[i])) && (!first || p < first))
			first = p;
	*line = first ? line_at(text, first - text) : 0;
	return first ? "a line break other than LF" : NULL;
}

int yaml_parse(const char *text, size_t len, struct string_list *kv, char **err)
{
	struct yframe stack[YAML_DEPTH];
	yaml_parser_t parser;
	yaml_event_t e;
	const char *why = NULL;
	int depth = 0, docs = 0, done = 0;
	size_t line = 0;
	const char *nul = memchr(text, 0, len);

	*err = NULL;
	if (nul) {
		*err = xstrfmt("a NUL character in line %d", (int)line_at(text, nul - text) + 1);
		return -1;
	}
	if ((why = not_text(text, &line))) {
		*err = xstrfmt("%s in line %d", why, (int)line + 1);
		return -1;
	}
	yaml_parser_initialize(&parser);
	yaml_parser_set_input_string(&parser, (const unsigned char *)text, len);
	while (!done && !why && yaml_parser_parse(&parser, &e)) {
		line = e.start_mark.line;
		docs += e.type == YAML_DOCUMENT_START_EVENT;
		why = yaml_outside(&e, docs > 1);
		if (!why)
			why = yaml_event(kv, stack, &depth, &e);
		done = e.type == YAML_STREAM_END_EVENT;
		yaml_event_delete(&e);
	}
	if (!done && !why) {
		why = parser.problem;
		line = parser.error == YAML_READER_ERROR ? line_at(text, parser.problem_offset) : parser.problem_mark.line;
	}
	if (why)
		*err = xstrfmt("%s in line %d", why, (int)line + 1);
	yaml_parser_delete(&parser);
	return why ? -1 : 0;
}

const char *yget(struct string_list *kv, const char *key)
{
	struct string_list_item *item = unsorted_string_list_lookup(kv, key);

	return item ? ((struct yval *)item->util)->s : NULL;
}

char ykind(struct string_list *kv, const char *key)
{
	struct string_list_item *item = unsorted_string_list_lookup(kv, key);

	return item ? ((struct yval *)item->util)->kind : -1;
}

int yline(struct string_list *kv, const char *key)
{
	struct string_list_item *item = NULL;
	char *parent, *p, *dot;

	if (!key)
		return 1;
	parent = xstrdup(key);
	while (!(item = unsorted_string_list_lookup(kv, parent))) {
		dot = NULL;
		for (p = parent; *p; p++)
			if (*p == '\\' && p[1])
				p++;
			else if (*p == '.')
				dot = p;
		if (!dot)
			break;
		*dot = '\0';
	}
	free(parent);

	if (!item)
		item = unsorted_string_list_lookup(kv, ROOT);
	return item ? ((struct yval *)item->util)->line : 1;
}

static char *yaml_vrefusal(int line, const char *fmt, va_list ap)
{
	struct strbuf sb = STRBUF_INIT;

	strbuf_vaddf(&sb, fmt, ap);
	strbuf_addf(&sb, " in line %d", line);
	return strbuf_detach(&sb, NULL);
}

char *yaml_refusal_at(int line, const char *fmt, ...)
{
	char *why;
	va_list ap;

	va_start(ap, fmt);
	why = yaml_vrefusal(line, fmt, ap);
	va_end(ap);
	return why;
}

/* Schema callers know the node; syntax errors already carry their parser mark. */
char *yaml_refusal(struct string_list *kv, const char *key, const char *fmt, ...)
{
	char *why;
	va_list ap;

	va_start(ap, fmt);
	why = yaml_vrefusal(yline(kv, key), fmt, ap);
	va_end(ap);
	return why;
}

/* The scalars KEY.0, KEY.1, ...; a child refusal keeps its own position. */
static char *yaml_list(struct string_list *kv, const char *key, struct string_list *out)
{
	char k = ykind(kv, key);
	int i;

	if (k != 'l' && k != '?')
		return yaml_refusal(kv, key, "'%s' is not a list", key);
	for (i = 0; yget(kv, xstrfmt("%s.%d", key, i)); i++) {
		char *child = xstrfmt("%s.%d", key, i);

		if (ykind(kv, child))
			return yaml_refusal(kv, child, "'%s' is not a scalar", child);
		string_list_append(out, yget(kv, child));
	}
	return NULL;
}

static int yaml_out(void *sb, unsigned char *buf, size_t n)
{
	strbuf_add(sb, buf, n);
	return 1;
}

/* S as one "..." scalar into SB, written by libyaml's emitter (v14 8.2) */
void yaml_out_scalar(struct strbuf *sb, const char *s)
{
	yaml_emitter_t y;
	yaml_event_t e;
	struct strbuf one = STRBUF_INIT;
	int ok;

	yaml_emitter_initialize(&y);
	yaml_emitter_set_output(&y, yaml_out, &one);
	yaml_emitter_set_unicode(&y, 1);
	yaml_emitter_set_width(&y, -1);
	ok = yaml_stream_start_event_initialize(&e, YAML_UTF8_ENCODING) && yaml_emitter_emit(&y, &e) &&
	     yaml_document_start_event_initialize(&e, NULL, NULL, NULL, 1) && yaml_emitter_emit(&y, &e) &&
	     yaml_scalar_event_initialize(&e, NULL, NULL, (yaml_char_t *)s, strlen(s), 1, 1,
					  YAML_DOUBLE_QUOTED_SCALAR_STYLE) && yaml_emitter_emit(&y, &e) &&
	     yaml_document_end_event_initialize(&e, 1) && yaml_emitter_emit(&y, &e) &&
	     yaml_stream_end_event_initialize(&e) && yaml_emitter_emit(&y, &e);
	yaml_emitter_delete(&y);
	if (!ok)
		BUG("libyaml cannot write a scalar: %s", y.problem);
	strbuf_rtrim(&one);
	strbuf_addstr(sb, one.buf);
}

/*
 * v14 8.2: a "|" block of S under INDENT where its lines allow one (no
 * leading space, no trailing blanks, printable), with "-" or "+" for its
 * end; else a "..." scalar
 */
void yaml_out_body(struct strbuf *sb, const char *s, const char *indent)
{
	struct string_list lines = STRING_LIST_INIT_DUP;
	struct string_list_item *line;
	size_t len = strlen(s), ends = 0;
	int block = len && *s != ' ' && *s != '\n';

	while (ends < len && s[len - 1 - ends] == '\n')
		ends++;
	string_list_split(&lines, xstrndup(s, len - ends), "\n", -1);
	for_each_string_list_item(line, &lines) {
		const char *p;

		for (p = line->string; *p; p++)
			/* These decoded scalars need escapes even though their UTF-8 bytes exceed 0x20. */
			block &= (((unsigned char)*p >= 0x20 && (unsigned char)*p != 0x7f) || *p == '\t') &&
				!((unsigned char)*p == 0xc2 && (unsigned char)p[1] < 0xa0) &&
				!starts_with(p, "\xe2\x80\xa8") && !starts_with(p, "\xe2\x80\xa9") &&
				!starts_with(p, "\xef\xbf\xbe") && !starts_with(p, "\xef\xbf\xbf");
		block &= !*line->string || !isspace(line->string[strlen(line->string) - 1]);
	}
	if (!block) {
		yaml_out_scalar(sb, s);
		strbuf_addch(sb, '\n');
		return;
	}
	strbuf_addf(sb, "|%s\n", ends == 0 ? "-" : ends == 1 ? "" : "+");
	for_each_string_list_item(line, &lines)
		strbuf_addf(sb, "%s%s\n", *line->string ? indent : "", line->string);
	for (; ends > 1; ends--)
		strbuf_addch(sb, '\n');
}

/* v14 3.1: repository-root path pieces; spec 3.1 permits .ait for condition files */
int worktree_path_ok(const char *p, int allow_ait)
{
	struct string_list parts = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	const char *q;
	int ok = *p && *p != '/' && *p != '-' && strcmp(p, ".ait") && !p[strcspn(p, " :,")] &&
		 (allow_ait || !starts_with(p, ".ait/"));

	for (q = p; *q; q++)
		ok &= (unsigned char)*q >= 0x20 && *q != 0x7f;
	string_list_split(&parts, p, "/", -1);
	for_each_string_list_item(item, &parts)
		ok &= *item->string && strcmp(item->string, ".") && strcmp(item->string, "..");
	string_list_clear(&parts, 0);
	return ok;
}

int path_ok(const char *p)
{
	return worktree_path_ok(p, 0);
}

/* spec 2.1.3: a group is path pieces, none of them "fulfill"; "" is the root */
int group_ok(const char *group)
{
	return !*group || (path_ok(group) && strcmp(group, "fulfill") && !starts_with(group, "fulfill/") &&
			   !strstr(group, "/fulfill/") && !ends_with(group, "/fulfill"));
}

static char *group_of(const char *path)
{
	const char *slash = strrchr(path, '/');

	return slash ? xstrndup(path, slash - path) : xstrdup("");
}

/* Canonical positive line numbers, shared by scopes and stored code spans. */
static const char *range_numbers(const char *text, int *first, int *last)
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

int range_of(const char *v, int *first, int *last)
{
	const char *colon, *end;

	skip_prefix(v, "new:", &v);
	colon = strchr(v, ':');
	if (!colon || colon > v + strcspn(v, " "))
		return 0;
	end = range_numbers(colon + 1, first, last);
	return end && (!*end || *end == ' ') ? 1 : -1;
}

char *scope_path(const char *v)
{
	skip_prefix(v, "new:", &v);
	return xstrndup(v, strcspn(v, " :"));
}

const char *scope_blob(const char *v)
{
	return strchr(v, ' ') ? strchr(v, ' ') + 1 : NULL;
}

static int oid_ok(const char *s)
{
	return strlen(s) == 40 && !s[strspn(s, "0123456789abcdef")];
}

/* spec 2.3.2, 3: a stored scope; a line range carries its base blob, nothing else does */
static int scope_valid(const char *v, int allow_new, int allow_abs, int stored)
{
	char *path = scope_path(v);
	const char *blob = scope_blob(v);
	int first, end, range = range_of(v, &first, &end);

	if (range < 0 || (range && starts_with(v, "new:")))
		return 0;
	if (range)
		return path_ok(path) && (blob ? oid_ok(blob) : !stored);
	if (blob)
		return 0;
	if (starts_with(v, "new:"))
		return allow_new && path_ok(path);
	if (!strcmp(v, "/"))
		return allow_abs;
	if (ends_with(v, "/"))
		return allow_abs && path_ok(xstrndup(v, strlen(v) - 1));
	return path_ok(v);
}

int scope_ok(const char *v, int allow_new, int allow_abs)
{
	return scope_valid(v, allow_new, allow_abs, 1);
}

int scope_arg_ok(const char *v, int allow_new, int allow_abs)
{
	return !scope_blob(v) && scope_valid(v, allow_new, allow_abs, 0);
}

/* v14 3.2a: input identity is new/PATH/S/E, before any fixing changes a row. */
char *scopes_refusal(struct string_list *scopes, int allow_new, int allow_abs, enum scope_form form)
{
	struct string_list seen = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	char *err = NULL;

	for_each_string_list_item(item, scopes) {
		char *bare = xstrndup(item->string, strcspn(item->string, " "));

		if (!scope_valid(item->string, allow_new, allow_abs, form == SCOPE_STORED) ||
		    (form == SCOPE_ARGUMENT && scope_blob(item->string)))
			err = xstrfmt("'%s' is not a scope", item->string);
		else if (string_list_has_string(&seen, bare))
			err = xstrfmt("'%s' is given twice", bare);
		else
			string_list_insert(&seen, bare);
		free(bare);
		if (err)
			break;
	}
	string_list_clear(&seen, 0);
	return err;
}

static void scopes_split(const char *list, struct string_list *out)
{
	if (*list)
		string_list_split(out, list, ",", -1);
}

/* v14 3.2a: PATH byte order, the whole file before its ranges, ranges by start and end */
static int cmp_scope(const void *pa, const void *pb)
{
	const char *a = (*(const struct string_list_item *)pa).string, *b = (*(const struct string_list_item *)pb).string;
	char *x = scope_path(a), *y = scope_path(b);
	int c = strcmp(x, y), sa = 0, ea = 0, sb = 0, eb = 0, ra = range_of(a, &sa, &ea), rb = range_of(b, &sb, &eb);

	if (c)
		return c;
	if (ra != rb)
		return ra - rb;
	return sa != sb ? sa - sb : ea - eb;
}

char *scopes_join(struct string_list *l)
{
	struct strbuf sb = STRBUF_INIT;
	struct string_list sorted = STRING_LIST_INIT_NODUP;
	struct string_list_item *item;

	for_each_string_list_item(item, l)
		string_list_append(&sorted, item->string);
	QSORT(sorted.items, sorted.nr, cmp_scope);
	for_each_string_list_item(item, &sorted)
		strbuf_addf(&sb, "%s%s", sb.len ? "," : "", item->string);
	string_list_clear(&sorted, 0);
	return strbuf_detach(&sb, NULL);
}

/* the key NAME under PREFIX ("specs.N"), or NAME itself at the root of a condition file ("") */
static char *pk(const char *prefix, const char *name)
{
	return *prefix ? xstrfmt("%s.%s", prefix, name) : xstrdup(name);
}

static int one_of(const char *w, const char *const *set)
{
	for (; *set; set++)
		if (!strcmp(w, *set))
			return 1;
	return 0;
}

/*
 * spec 2.3.1: every key of KV under PREFIX ("" the root) is one of KEYS, and
 * all of KEYS are there when REQUIRED; NULL, or why not
 */
char *keys_refusal(struct string_list *kv, const char *prefix, const char *const *keys, int required)
{
	struct string_list_item *item;
	size_t plen = *prefix ? strlen(prefix) + 1 : 0;

	for_each_string_list_item(item, kv) {
		const char *k = item->string;

		if (!strcmp(k, ROOT) || (plen && (strncmp(k, prefix, plen - 1) || k[plen - 1] != '.')))
			continue;
		if (!one_of(xstrndup(k + plen, strcspn(k + plen, ".")), keys))
			return yaml_refusal(kv, k, "unknown key '%s'", k);
	}
	for (; required && *keys; keys++)
		if (!unsorted_string_list_has_string(kv, pk(prefix, *keys)))
			return yaml_refusal(kv, pk(prefix, *keys), "'%s' is missing", *keys);
	return NULL;
}

/* v14 2.5.2 without inputs: a spec under PREFIX of KV; NULL with ERR */
struct spec *spec_from(struct string_list *kv, const char *prefix, char **err)
{
	static const char *const keys[] = { "body", "engine", "command", "success", "environment", NULL };
	struct spec *s = xcalloc(1, sizeof(*s));
	struct string_list cmd = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	const char *body_key = pk(prefix, "body"), *engine_key = pk(prefix, "engine"),
		   *command_key = pk(prefix, "command"), *success_key = pk(prefix, "success"),
		   *environment_key = pk(prefix, "environment"), *engine = yget(kv, engine_key),
		   *success = yget(kv, success_key);

	strvec_init(&s->command);
	string_list_init_dup(&s->success);
	if ((*err = keys_refusal(kv, prefix, keys, 0)))
		return NULL;
	s->body = xstrdup_null(yget(kv, body_key));
	if (!s->body || ykind(kv, body_key))
		return bad(err, yaml_refusal(kv, body_key, "'%s' is missing or not a string", body_key));
	if (engine && ykind(kv, engine_key) != 0 && ykind(kv, engine_key) != '?')
		return bad(err, yaml_refusal(kv, engine_key, "'%s' is not a string", engine_key));
	if (engine && *engine && strcmp(engine, "exec"))
		return bad(err, yaml_refusal(kv, engine_key, "unknown engine '%s'", engine));
	s->engine = engine && *engine ? xstrdup(engine) : NULL;
	if (yget(kv, command_key) && (*err = yaml_list(kv, command_key, &cmd)))
		return NULL;
	if (success && ykind(kv, success_key) != 'l')
		return bad(err, yaml_refusal(kv, success_key, "'%s' is not a list", success_key));
	if (success && (*err = yaml_list(kv, success_key, &s->success)))
		return NULL;
	if (s->engine && !success)
		string_list_append(&s->success, "0");
	for_each_string_list_item(item, &cmd)
		strvec_push(&s->command, item->string);
	for_each_string_list_item(item, &s->success)
		if (!*item->string || item->string[strspn(item->string, "0123456789")] ||
		    (*item->string == '0' && item->string[1]))
			return bad(err, yaml_refusal(kv, xstrfmt("%s.%d", success_key, (int)(item - s->success.items)),
					     "'success' holds '%s', not a decimal integer", item->string));
	s->environment = xstrdup_null(yget(kv, environment_key));
	if (s->environment && ykind(kv, environment_key) != 0 && ykind(kv, environment_key) != '?')
		return bad(err, yaml_refusal(kv, environment_key, "'%s' is not a string", environment_key));
	if (s->engine && !s->command.nr)
		return bad(err, yaml_refusal(kv, command_key, "an exec spec needs 'command'"));
	if (!s->engine && (yget(kv, command_key) || success || s->environment))
		return bad(err, yaml_refusal(kv, yget(kv, command_key) ? command_key : success ? success_key : environment_key,
					     "a prompt spec takes only 'body'"));
	return s;
}

static const char *const demand_keys[] = { "condition-id", "change-id", "kind", "creator", "consumer", "provider",
					  "state", "specs", "fulfilled", NULL };
static const char *const offer_keys[] = { "condition-id", "change-id", "kind", "creator", "provider", "state",
					 "specs", "undertakes", "undertakes-partially", "fulfilled", NULL };
static const char *const drop_keys[] = { "condition-id", "change-id", "kind", "creator", "target", "state",
					"fulfilled", NULL };

static const char *const *keys_of(const char *kind)
{
	return !strcmp(kind, "demand") ? demand_keys : !strcmp(kind, "offer") ? offer_keys :
	       !strcmp(kind, "drop") ? drop_keys : NULL;
}

/* spec 1.11: the letter of an ID of KIND */
int kind_letter(const char *kind)
{
	return !strcmp(kind, "demand") ? 'D' : !strcmp(kind, "offer") ? 'O' : 'X';
}

/* spec 2.1.3: the ID of the contract path "<group>/<ID>.yaml", or NULL */
char *contract_path_id(const char *path)
{
	const char *base = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
	char *id = xstrndup(base, strcspn(base, "."));

	return !strcmp(base, xstrfmt("%s.yaml", id)) && id_ok(id, "DOX") && group_ok(group_of(path)) ? id : NULL;
}

/* a new contract of KIND in GROUP with ID, its fields empty (spec 6.4, 6.5.2) */
struct contract *contract_new(const char *kind, const char *group, const char *id)
{
	struct contract *c = xcalloc(1, sizeof(*c));

	snprintf(c->id, sizeof(c->id), "%s", id);
	c->group = xstrdup(group);
	c->path = xstrfmt("%s%s%s.yaml", group, *group ? "/" : "", id);
	c->kind = xstrdup(kind);
	c->condition_id = xstrdup("");
	c->change_id = xstrdup("");
	c->creator = xstrdup("");
	c->state = xstrdup("future");
	string_list_init_dup(&c->consumer);
	string_list_init_dup(&c->provider);
	string_list_init_dup(&c->undertakes);
	string_list_init_dup(&c->partially);
	string_list_init_dup(&c->fulfilled);
	return c;
}

/*
 * spec 2.3: the contract at PATH (<group>/<ID>.yaml) from TEXT; DRAFT allows
 * the empty IDs and creator of a template (6.5.2). NULL with ERR when malformed.
 */
char *fulfil_path(struct contract *c, const char *rid)
{
	return xstrfmt("%s%sfulfill/%s-%s.md", c->group, *c->group ? "/" : "", c->id, rid);
}

static int fulfil_path_ok(const char *path, const char *id, const char *group)
{
	const char *slash = strrchr(path, '/'), *rid;
	char *dir;

	if (!slash)
		return 0;
	dir = xstrndup(path, slash - path);
	if (group && strcmp(dir, xstrfmt("%s%sfulfill", group, *group ? "/" : "")))
		return 0;
	if (strcmp(dir, "fulfill") &&
	    (!ends_with(dir, "/fulfill") || !group_ok(xstrndup(dir, strlen(dir) - 8))))
		return 0;
	return skip_prefix(slash + 1, xstrfmt("%s-", id), &rid) && ends_with(rid, ".md") &&
		id_ok(xstrndup(rid, strlen(rid) - 3), "R");
}

struct contract *contract_parse(const char *text, size_t len, const char *path, int draft, char **err)
{
	static const char *const states_do[] = { "future", "cancelled", "withdrawn", NULL };
	static const char *const states_x[] = { "future", "cancelled", NULL };
	struct string_list kv = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct contract *c;
	const char *const *keys, *v;
	char *id = contract_path_id(path);
	int i, demand, offer, drop;

	if (yaml_parse(text, len, &kv, err))
		return NULL;
	*err = NULL;
	if (!id)
		return bad(err, yaml_refusal(&kv, NULL, "not a contract path"));
	v = yget(&kv, "kind");
	if (!v || !(keys = keys_of(v)) || kind_letter(v) != *id)
		return bad(err, yaml_refusal(&kv, "kind", "'kind' is missing or does not match the ID"));
	c = contract_new(v, group_of(path), id);
	demand = !strcmp(v, "demand");
	offer = !strcmp(v, "offer");
	drop = !strcmp(v, "drop");
	if ((*err = keys_refusal(&kv, "", keys, 1)))
		return NULL;
	c->condition_id = xstrdup(yget(&kv, "condition-id"));
	c->change_id = xstrdup(yget(&kv, "change-id"));
	c->creator = xstrdup(yget(&kv, "creator"));
	c->state = xstrdup(yget(&kv, "state"));
	if ((ykind(&kv, "condition-id") != 0 && ykind(&kv, "condition-id") != '?') ||
	    (draft && !*c->condition_id ? 0 : !id_ok(c->condition_id, "R")))
		return bad(err, yaml_refusal(&kv, "condition-id", "bad 'condition-id'"));
	if ((ykind(&kv, "change-id") != 0 && ykind(&kv, "change-id") != '?') ||
	    (draft && !*c->change_id ? 0 : !id_ok(c->change_id, "R")))
		return bad(err, yaml_refusal(&kv, "change-id", "bad 'change-id'"));
	if ((ykind(&kv, "creator") != 0 && ykind(&kv, "creator") != '?') ||
	    (draft && !*c->creator ? 0 : !agent_ok(c->creator)))
		return bad(err, yaml_refusal(&kv, "creator", "bad 'creator'"));
	if (!one_of(c->state, drop ? states_x : states_do))
		return bad(err, yaml_refusal(&kv, "state", "bad 'state'"));
	if (demand)
		scopes_split(yget(&kv, "consumer"), &c->consumer);
	if (!drop)
		scopes_split(yget(&kv, "provider"), &c->provider);
	for (i = !demand; !drop && i < 2; i++) {
		char *why = scopes_refusal(i ? &c->provider : &c->consumer, 1, i,
					  draft ? SCOPE_DRAFT : SCOPE_STORED);

		if (why)
			return bad(err, yaml_refusal(&kv, i ? "provider" : "consumer", "bad scope in '%s': %s",
						 i ? "provider" : "consumer", why));
	}
	if (!drop && (!c->provider.nr || (demand && !c->consumer.nr)))
		return bad(err, yaml_refusal(&kv, !c->provider.nr ? "provider" : "consumer", "a scope list is empty"));
	if (drop && !id_ok(c->target = xstrdup(yget(&kv, "target")), "DO"))
		return bad(err, yaml_refusal(&kv, "target", "bad 'target'"));
	if (offer && ((*err = yaml_list(&kv, "undertakes", &c->undertakes)) ||
		      (*err = yaml_list(&kv, "undertakes-partially", &c->partially))))
		return NULL;
	for_each_string_list_item(item, &c->undertakes)
		if (!id_ok(item->string, "D") || unsorted_string_list_has_string(&c->partially, item->string))
			return bad(err, yaml_refusal(&kv, xstrfmt("undertakes.%d", (int)(item - c->undertakes.items)),
						 "bad ID '%s' in 'undertakes'", item->string));
	for_each_string_list_item(item, &c->partially)
		if (!id_ok(item->string, "D"))
			return bad(err, yaml_refusal(&kv, xstrfmt("undertakes-partially.%d", (int)(item - c->partially.items)),
						 "bad ID '%s' in 'undertakes-partially'", item->string));
	if ((*err = yaml_list(&kv, "fulfilled", &c->fulfilled)))
		return NULL;
	for_each_string_list_item(item, &c->fulfilled) {
		char *key = xstrfmt("fulfilled.%d", (int)(item - c->fulfilled.items));
		int *source_line;

		if (!fulfil_path_ok(item->string, c->id, c->group))
			return bad(err, yaml_refusal(&kv, key, "bad path '%s' in 'fulfilled'", item->string));
		source_line = xmalloc(sizeof(*source_line));
		*source_line = yline(&kv, key);
		item->util = source_line;
	}
	if (drop)
		return c;
	if (ykind(&kv, "specs") != 'l' || !yget(&kv, "specs.0"))
		return bad(err, yaml_refusal(&kv, "specs", "'specs' is no list of specs"));
	for (i = 0; yget(&kv, xstrfmt("specs.%d", i)); i++) {
		struct spec *s = spec_from(&kv, xstrfmt("specs.%d", i), err);

		if (!s)
			return NULL;
		REALLOC_ARRAY(c->specs, c->nspecs + 1);
		c->specs[c->nspecs++] = *s;
	}
	return c;
}

static void ids_out(struct strbuf *sb, const char *key, struct string_list *l)
{
	struct string_list sorted = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	int first = 1;

	for_each_string_list_item(item, l)
		string_list_insert(&sorted, item->string);
	strbuf_addf(sb, "%s: [", key);
	for_each_string_list_item(item, &sorted) {
		strbuf_addf(sb, "%s%s", first ? "" : ", ", item->string);
		first = 0;
	}
	strbuf_addstr(sb, "]\n");
}

static void scope_line(struct strbuf *sb, const char *key, struct string_list *l)
{
	strbuf_addf(sb, "%s: ", key);
	yaml_out_scalar(sb, scopes_join(l));
	strbuf_addch(sb, '\n');
}

/* spec 2.3.2, 7.2: both outputs use the parsed command and success values. */
void spec_lists(struct strbuf *sb, struct spec *s)
{
	size_t j;

	strbuf_addstr(sb, "  command: [");
	for (j = 0; j < s->command.nr; j++) {
		if (j)
			strbuf_addstr(sb, ", ");
		yaml_out_scalar(sb, s->command.v[j]);
	}
	strbuf_addstr(sb, "]\n");
	strbuf_addstr(sb, "  success: [");
	for (j = 0; j < s->success.nr; j++)
		strbuf_addf(sb, "%s%s", j ? ", " : "", s->success.items[j].string);
	strbuf_addstr(sb, "]\n");
}

/* spec 2.3.2: the contract written in its one form, every key on its own line */
char *contract_text(struct contract *c)
{
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;
	size_t i;

	strbuf_addf(&sb, "condition-id: %s\nchange-id: %s\nkind: %s\ncreator: %s\n",
		    *c->condition_id ? c->condition_id : "\"\"", *c->change_id ? c->change_id : "\"\"", c->kind,
		    *c->creator ? c->creator : "\"\"");
	if (!strcmp(c->kind, "demand"))
		scope_line(&sb, "consumer", &c->consumer);
	if (!strcmp(c->kind, "drop"))
		strbuf_addf(&sb, "target: %s\n", c->target);
	else
		scope_line(&sb, "provider", &c->provider);
	strbuf_addf(&sb, "state: %s\n", c->state);
	if (strcmp(c->kind, "drop")) {
		strbuf_addstr(&sb, "specs:\n");
		for (i = 0; i < c->nspecs; i++) {
			struct spec *s = &c->specs[i];

			strbuf_addstr(&sb, "- body: ");
			yaml_out_body(&sb, s->body, "    ");
			if (!s->engine)
				continue;
			strbuf_addstr(&sb, "  engine: exec\n");
			spec_lists(&sb, s);
			if (s->environment) {
				strbuf_addstr(&sb, "  environment: ");
				yaml_out_scalar(&sb, s->environment);
				strbuf_addch(&sb, '\n');
			}
		}
	}
	if (!strcmp(c->kind, "offer")) {
		ids_out(&sb, "undertakes", &c->undertakes);
		ids_out(&sb, "undertakes-partially", &c->partially);
	}
	/* an empty list is the key alone: the first fulfilment then leaves the key's line as it is (drafts 9.31) */
	strbuf_addstr(&sb, "fulfilled:\n");
	for_each_string_list_item(item, &c->fulfilled) {
		strbuf_addstr(&sb, "- ");
		yaml_out_scalar(&sb, item->string);
		strbuf_addch(&sb, '\n');
	}
	return strbuf_detach(&sb, NULL);
}

/* A recorded tree entry is parsed once at the stored-fact boundary (2.5). */
static struct recorded_code *parse_code(const char *scope, const char *text)
{
	struct string_list fields = STRING_LIST_INIT_DUP;
	struct recorded_code *code = NULL;
	const char *mode, *type, *oid, *end;
	int first = 0, last = 0, range;

	if (!scope_ok(scope, 0, 0))
		return NULL;
	range = range_of(scope, &first, &last) == 1;
	string_list_split(&fields, text, " ", -1);
	if (fields.nr != (range ? 4 : 3))
		goto done;
	mode = fields.items[0].string;
	type = fields.items[1].string;
	oid = fields.items[2].string;
	if (!one_of(mode, (const char *const[]){ "100644", "100755", "120000", "160000", NULL }) ||
	    strcmp(type, !strcmp(mode, "160000") ? "commit" : "blob") || !oid_ok(oid))
		goto done;
	if (range && (strcmp(type, "blob") ||
		      !(end = range_numbers(fields.items[3].string, &first, &last)) || *end))
		goto done;
	code = xcalloc(1, sizeof(*code));
	snprintf(code->mode, sizeof(code->mode), "%s", mode);
	snprintf(code->type, sizeof(code->type), "%s", type);
	snprintf(code->oid, sizeof(code->oid), "%s", oid);
	code->first = range ? first : 0;
	code->last = range ? last : 0;
done:
	string_list_clear(&fields, 0);
	return code;
}

static char *code_text(struct recorded_code *code)
{
	return code->first ? xstrfmt("%s %s %s %d-%d", code->mode, code->type, code->oid, code->first, code->last) :
		xstrfmt("%s %s %s", code->mode, code->type, code->oid);
}

/* spec 2.5: the fulfilment at PATH from TEXT, or NULL with ERR */
static struct fulfilment *fulfil_parse(const char *text, const char *path, char **err)
{
	static const char *const keys[] = { "contract", "condition-id", "by", "session", "commit", "outcome", "code", NULL };
	struct string_list kv = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct fulfilment *f = xcalloc(1, sizeof(*f));
	const char *end, *invalid;
	char *front;

	*err = NULL;
	if (!starts_with(text, "---\n") || !(end = strstr(text + 3, "\n---\n")))
		return bad(err, yaml_refusal(&kv, NULL, "no front matter"));
	front = xstrndup(text, end + 1 - text);
	if (yaml_parse(front, end + 1 - text, &kv, err))
		return NULL;
	if ((*err = keys_refusal(&kv, "", keys, 1)))
		return NULL;
	f->path = xstrdup(path);
	f->contract = xstrdup(yget(&kv, "contract"));
	f->condition_id = xstrdup(yget(&kv, "condition-id"));
	f->by = xstrdup(yget(&kv, "by"));
	f->session = xstrdup(yget(&kv, "session"));
	f->commit = xstrdup(yget(&kv, "commit"));
	f->outcome = xstrdup(yget(&kv, "outcome"));
	f->body = xstrdup(end + 5);
	string_list_init_dup(&f->code);
	invalid = !id_ok(f->contract, "DOX") ? "contract" : !id_ok(f->condition_id, "R") ? "condition-id" :
		!agent_ok(f->by) ? "by" : !id_ok(f->session, "S") ? "session" : !oid_ok(f->commit) ? "commit" :
		!one_of(f->outcome, (const char *const[]){ "pass", "forced", "withdrawn", NULL }) ? "outcome" :
		ykind(&kv, "code") != 'm' ? "code" : NULL;
	if (invalid)
		return bad(err, yaml_refusal(&kv, invalid, "bad '%s' in the front matter", invalid));
	if (!fulfil_path_ok(path, f->contract, NULL))
		return bad(err, yaml_refusal(&kv, NULL, "not a fulfilment path"));
	if ((*f->contract == 'X') != !strcmp(f->outcome, "withdrawn"))
		return bad(err, yaml_refusal(&kv, "outcome", "'outcome' does not match the contract kind"));
	for_each_string_list_item(item, &kv) {
		const char *scope;

		if (skip_prefix(item->string, "code.", &scope)) {
			char *unescaped = yaml_key_decode(scope);
			struct recorded_code *code;

			if (((struct yval *)item->util)->kind)
				return bad(err, yaml_refusal(&kv, item->string, "a code entry is not a string"));
			code = parse_code(unescaped, ((struct yval *)item->util)->s);
			if (!code)
				return bad(err, yaml_refusal(&kv, item->string, "bad code entry for '%s'", unescaped));
			string_list_insert(&f->code, unescaped)->util = code;
		}
	}
	if (*f->contract == 'X' && f->code.nr)
		return bad(err, yaml_refusal(&kv, "code", "a drop's code is not empty"));
	return f;
}

char *fulfil_text(struct fulfilment *f)
{
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;

	strbuf_addf(&sb, "---\ncontract: %s\ncondition-id: %s\nby: %s\nsession: %s\ncommit: %s\noutcome: %s\ncode:%s\n",
		    f->contract, f->condition_id, f->by, f->session, f->commit, f->outcome, f->code.nr ? "" : " {}");
	for_each_string_list_item(item, &f->code) {
		strbuf_addstr(&sb, "  ");
		yaml_out_scalar(&sb, item->string);
		strbuf_addstr(&sb, ": ");
		yaml_out_scalar(&sb, code_text(item->util));
		strbuf_addch(&sb, '\n');
	}
	strbuf_addf(&sb, "---\n%s", f->body);
	strbuf_complete_line(&sb);
	return strbuf_detach(&sb, NULL);
}

/*
 * spec 2.1.3, 2.3, 2.5: the contracts and fulfilments of the branch NAME at
 * TIP (or of the tree TREEISH); each malformed file is one error
 */
struct branch *branch_load(const char *name, const char *tip, const char *treeish)
{
	static struct string_list loaded = STRING_LIST_INIT_DUP;
	struct string_list_item *hit, *item;
	struct string_list invalid = STRING_LIST_INIT_DUP;
	struct branch *b;
	char oid[48];
	size_t i;

	if (!treeish && !tip)
		tip = branch_tip(name, oid) ? xstrdup(oid) : NULL;
	if (!treeish)
		treeish = tip ? tip : EMPTY_TREE;
	if ((hit = string_list_lookup(&loaded, xstrfmt("%s %s", name, treeish))))
		return hit->util;
	b = xcalloc(1, sizeof(*b));
	b->name = name;
	snprintf(b->tip, sizeof(b->tip), "%s", tip ? tip : "");
	b->tree = tree_load(treeish);
	string_list_init_dup(&b->contracts);
	string_list_init_dup(&b->fulfils);
	string_list_init_dup(&b->errors);
	string_list_insert(&loaded, xstrfmt("%s %s", name, treeish))->util = b;
	for (i = 0; i < b->tree->nr; i++) {
		struct entry *e = &b->tree->e[i];
		const char *text, *nul;
		char *err = NULL;
		size_t len;

		if (!strcmp(e->type, "tree"))
			continue;
		if (strcmp(e->type, "blob"))
			err = yaml_refusal_at(1, "not a blob");
		else if (!(text = object(e->oid, 1, NULL, &len)))
			err = yaml_refusal_at(1, "could not read blob '%s'", e->oid);
		else if ((nul = memchr(text, 0, len)))
			err = yaml_refusal_at((int)line_at(text, nul - text) + 1, "a NUL character");
		else if (ends_with(e->path, ".md") && (starts_with(e->path, "fulfill/") || strstr(e->path, "/fulfill/"))) {
			struct fulfilment *f = fulfil_parse(text, e->path, &err);

			if (f)
				string_list_insert(&b->fulfils, e->path)->util = f;
		} else {
			struct contract *c = contract_parse(text, len, e->path, 0, &err);

			if (c && string_list_lookup(&b->contracts, c->id))
				err = yaml_refusal_at(1, "'%s' is also at '%s'", c->id,
						      ((struct contract *)string_list_lookup(&b->contracts, c->id)->util)->path);
			else if (c)
				string_list_insert(&b->contracts, c->id)->util = c;
		}
		if (err)
			string_list_insert(&b->errors, e->path)->util = err;
	}
	for_each_string_list_item(item, &b->contracts) {
		struct contract *c = item->util;
		struct string_list_item *f;

		for_each_string_list_item(f, &c->fulfilled) {
			struct string_list_item *got = string_list_lookup(&b->fulfils, f->string);

			if (!got || strcmp(((struct fulfilment *)got->util)->contract, c->id)) {
				string_list_insert(&b->errors, c->path)->util =
					yaml_refusal_at(*(int *)f->util, "'%s' is not a fulfilment of '%s'", f->string, c->id);
				string_list_insert(&invalid, c->id);
			}
		}
	}
	for_each_string_list_item(item, &invalid)
		string_list_remove(&b->contracts, item->string, 0);
	string_list_clear(&invalid, 0);
	return b;
}

struct contract *contract_get(struct branch *b, const char *id)
{
	struct string_list_item *item = string_list_lookup(&b->contracts, id);

	return item ? item->util : NULL;
}

/* spec 1.4: a contract that has not ended */
int live(struct contract *c)
{
	return c && !strcmp(c->state, "future");
}

/* spec 5.2: the fulfilments of C made with its condition-id */
static void valid_fulfilments(struct branch *b, struct contract *c, struct string_list *out)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &c->fulfilled) {
		struct string_list_item *f = string_list_lookup(&b->fulfils, item->string);

		if (f && !strcmp(((struct fulfilment *)f->util)->condition_id, c->condition_id))
			string_list_append(out, item->string)->util = f->util;
	}
}

int has_valid_fulfilment(struct branch *b, struct contract *c)
{
	struct string_list v = STRING_LIST_INIT_DUP;
	int found;

	valid_fulfilments(b, c, &v);
	found = v.nr > 0;
	string_list_clear(&v, 0);
	return found;
}

static void hunk_part(const char *text, int *first, int *length)
{
	char *next;

	*first = strtol(text, &next, 10);
	*length = *next == ',' ? strtol(next + 1, NULL, 10) : 1;
}

/* v14 3.6: the diff of two blobs, as every range is judged */
static char *diff0(const char *a, const char *b)
{
	return git("diff", "-U0", "--no-color", "--no-ext-diff", "--no-textconv", "--text",
		"--inter-hunk-context=0", "--diff-algorithm=myers", "--no-indent-heuristic", a, b);
}

/*
 * v14 3.7 and 3.10: the range S-E of the blob BASE mapped to the blob NOW;
 * CHANGED when a hunk's touch span (an insertion [os, os+1]) meets S-E
 */
static enum scope_status map_blobs(const char *base, const char *now, int a, int b, int *first, int *end, int *changed)
{
	struct string_list lines = STRING_LIST_INIT_DUP;
	struct string_list_item *line;
	int ma = -1, mb = -1, offset = 0;

	*changed = 0;
	if (!now)
		return MISSING;
	if (!base || !object(base, 0, NULL, NULL))
		return NOBASE;
	if (!strcmp(base, now)) {
		*first = a;
		*end = b;
		return FOUND;
	}
	string_list_split(&lines, diff0(base, now), "\n", -1);
	for_each_string_list_item(line, &lines) {
		int os, ol, ns, nl;
		const char *new_part;

		if (!starts_with(line->string, "@@ -") || !(new_part = strstr(line->string, " +")))
			continue;
		hunk_part(line->string + 4, &os, &ol);
		hunk_part(new_part + 2, &ns, &nl);
		*changed |= a <= os + (ol ? ol - 1 : 1) && b >= os;
		if (ma < 0) {
			if (ol ? a < os : a <= os)
				ma = a + offset;
			else if (ol && a < os + ol)
				ma = nl ? ns : ns + 1;
		}
		if (mb < 0) {
			if (ol ? b < os : b <= os)
				mb = b + offset;
			else if (ol && b < os + ol)
				mb = nl ? ns + nl - 1 : ns;
		}
		offset += nl - ol;
	}
	*first = ma < 0 ? a + offset : ma;
	*end = mb < 0 ? b + offset : mb;
	return *first > *end ? DELETED : FOUND;
}

static enum scope_status map_range(const char *scope, struct tree *t, int *first, int *end, int *changed)
{
	int a, b;

	range_of(scope, &a, &b);
	return map_blobs(scope_blob(scope), blob_at(t, scope_path(scope)), a, b, first, end, changed);
}

static int line_count(const char *text, size_t n)
{
	size_t i, lf = 0;

	for (i = 0; i < n; i++)
		lf += text[i] == '\n';
	return lf + (n && text[n - 1] != '\n');
}

/* spec 3.2 (v14 3.3): a scope argument fixed at T, a line range taking T's blob; NULL with ERR */
char *scope_pin(const char *arg, struct tree *t, char **err)
{
	char *path = scope_path(arg);
	struct entry *e;
	int first, end;

	*err = NULL;
	if (!scope_arg_ok(arg, 1, 1))
		return bad(err, xstrfmt("'%s' is not a scope", arg));
	if (starts_with(arg, "new:")) {
		if (tree_get(t, path))
			*err = xstrfmt("'%s' exists", path);
		return *err ? NULL : xstrdup(arg);
	}
	if (!strcmp(arg, "/"))
		return xstrdup(arg);
	if (ends_with(arg, "/")) {
		e = tree_get(t, xstrndup(arg, strlen(arg) - 1));
		if (!e || strcmp(e->type, "tree"))
			*err = xstrfmt("'%s' is not a directory", arg);
		return *err ? NULL : xstrdup(arg);
	}
	/* a file scope is any entry but a tree, a gitlink too (2.5); a line range is in a blob */
	if (!(e = tree_get(t, path)) || !strcmp(e->type, "tree"))
		return bad(err, xstrfmt("'%s' is not a file", path));
	if (range_of(arg, &first, &end) == 1) {
		size_t n;
		char *text = blob_at(t, path) ? object(blob_at(t, path), 1, NULL, &n) : NULL;

		if (!text || strlen(text) != n || end > line_count(text, n))
			return bad(err, xstrfmt("'%s' is not a range of '%s'", arg, path));
		return xstrfmt("%s:%d-%d %s", path, first, end, blob_at(t, path));
	}
	return xstrdup(path);
}

/* v14 3.2a, 3.3: unchanged draft tuples keep T's pin; supplied bases are ignored. */
int scopes_pin(struct string_list *scopes, struct tree *t, struct string_list *was, char **err)
{
	struct string_list fixed = STRING_LIST_INIT_DUP;
	struct string_list_item *item, *w;

	*err = NULL;
	for_each_string_list_item(item, scopes) {
		size_t len = strcspn(item->string, " ");
		char *bare = xstrndup(item->string, len), *pinned = NULL;

		if (was)
			for_each_string_list_item(w, was)
				if (strcspn(w->string, " ") == len && !strncmp(w->string, bare, len))
					pinned = xstrdup(w->string);
		if (!pinned)
			pinned = scope_pin(bare, t, err);
		free(bare);
		if (!pinned) {
			string_list_clear(&fixed, 0);
			return -1;
		}
		string_list_insert(&fixed, pinned);
		free(pinned);
	}
	string_list_clear(scopes, 0);
	*scopes = fixed;
	return 0;
}

/*
 * spec 6.7.4, v14 3.8: where SCOPE is in T, as one judgment its callers word:
 * FOUND with its path and mapped line endpoints, or the reason it is not
 */
enum scope_status scope_locate(const char *scope, struct tree *t, struct scope_location *where)
{
	char *path = scope_path(scope);
	struct entry *e = tree_get(t, path);
	int changed;

	where->path = path;
	where->first = where->last = 0;
	if (starts_with(scope, "new:") || ends_with(scope, "/"))
		return ABSTRACT;
	if (range_of(scope, &where->first, &where->last) != 1)
		return e && strcmp(e->type, "tree") ? FOUND : MISSING;
	return map_range(scope, t, &where->first, &where->last, &changed);
}

/* spec 2.5: the recorded tree entry of SCOPE at T; NULL when it is not found. */
struct recorded_code *code_entry(const char *scope, struct tree *t)
{
	struct scope_location where;
	struct entry *e;
	struct recorded_code *code;

	if (scope_locate(scope, t, &where) != FOUND)
		return NULL;
	e = tree_get(t, where.path);
	code = xcalloc(1, sizeof(*code));
	snprintf(code->mode, sizeof(code->mode), "%s", e->mode);
	snprintf(code->type, sizeof(code->type), "%s", e->type);
	snprintf(code->oid, sizeof(code->oid), "%s", e->oid);
	code->first = where.first;
	code->last = where.last;
	return code;
}

/* spec 5.3: the scope is in X as recorded; AT receives that verified entry in X. */
static int scope_same(const char *scope, struct recorded_code *code, struct tree *x, struct recorded_code *at)
{
	struct entry *e = tree_get(x, scope_path(scope));
	int first = 0, last = 0, changed;

	if (!code || !e || strcmp(e->mode, code->mode) || strcmp(e->type, code->type))
		return 0;
	if (code->first) {
		if (map_blobs(code->oid, e->oid, code->first, code->last, &first, &last, &changed) != FOUND || changed)
			return 0;
	} else if (strcmp(e->oid, code->oid)) {
		return 0;
	}
	if (at) {
		*at = *code;
		snprintf(at->oid, sizeof(at->oid), "%s", e->oid);
		at->first = first;
		at->last = last;
	}
	return 1;
}

/* spec 5.3: the fulfilment F recorded SCOPE as X has it */
static int recorded_same(struct fulfilment *f, const char *scope, struct tree *x, struct recorded_code *at)
{
	struct string_list_item *code = unsorted_string_list_lookup(&f->code, scope);

	return code && scope_same(scope, code->util, x, at);
}

/* spec 5.4: every provider must match the same fulfilment; retain its verified code when requested. */
static int provider_same(struct contract *c, struct fulfilment *f, struct tree *x, struct string_list *code)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &c->provider) {
		struct recorded_code at;

		if (!recorded_same(f, item->string, x, code ? &at : NULL)) {
			if (code)
				string_list_clear(code, 1);
			return 0;
		}
		if (code) {
			struct recorded_code *saved = xmalloc(sizeof(*saved));

			*saved = at;
			string_list_append(code, item->string)->util = saved;
		}
	}
	return 1;
}

/* v14 3.8: the scope as shown, located in X */
char *scope_display(const char *scope, struct tree *x)
{
	static const char *const marks[] = { [NOBASE] = "?nobase", [MISSING] = "?missing", [DELETED] = "?deleted" };
	struct scope_location where;
	enum scope_status found = scope_locate(scope, x, &where);

	if (starts_with(scope, "new:"))
		return xstrfmt("%s%s", scope, tree_get(x, scope_path(scope)) ? " ?exists" : "");
	if (found == ABSTRACT)
		return xstrdup(scope);
	if (found == FOUND)
		return where.first ? xstrfmt("%s:%d-%d", where.path, where.first, where.last) : xstrdup(where.path);
	return xstrfmt("%.*s %s", (int)strcspn(scope, " "), scope, marks[found]);
}

/* a scope list as a request takes it: each scope without its base blob, which the request fixes again (3.2) */
char *scopes_bare(struct string_list *l)
{
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;

	for_each_string_list_item(item, l)
		strbuf_addf(&sb, "%s%.*s", sb.len ? "," : "", (int)strcspn(item->string, " "), item->string);
	return strbuf_detach(&sb, NULL);
}

/* a scope list as shown in X, joined with "," */
char *scopes_display(struct string_list *l, struct tree *x)
{
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;

	for_each_string_list_item(item, l)
		strbuf_addf(&sb, "%s%s", sb.len ? "," : "", scope_display(item->string, x));
	return strbuf_detach(&sb, NULL);
}

/* v14 3.9: two scopes overlap in T */
static int scope_overlap(const char *q, const char *r, struct tree *t)
{
	char *pq = scope_path(q), *pr = scope_path(r);
	int a, b, c, d, cq, cr;

	if (!strcmp(q, "/") || !strcmp(r, "/"))
		return 1;
	if (ends_with(q, "/") || ends_with(r, "/"))
		return starts_with(pr, ends_with(q, "/") ? q : "\x01") || starts_with(pq, ends_with(r, "/") ? r : "\x01");
	if (strcmp(pq, pr))
		return 0;
	if (range_of(q, &a, &b) != 1 || range_of(r, &c, &d) != 1)
		return 1;
	if (scope_blob(q) && map_range(q, t, &a, &b, &cq) != FOUND)
		return 1;
	if (scope_blob(r) && map_range(r, t, &c, &d, &cr) != FOUND)
		return 1;
	return a <= d && c <= b;
}

/* v14 3.9a: the scope Q overlaps C's consumer or provider in T */
int contract_overlaps(struct contract *c, const char *q, struct tree *t, int depending)
{
	struct string_list_item *s;

	for_each_string_list_item(s, &c->consumer)
		if (scope_overlap(q, s->string, t))
			return 1;
	for_each_string_list_item(s, &c->provider)
		if ((!depending || !ends_with(s->string, "/") || ends_with(q, "/")) && scope_overlap(q, s->string, t))
			return 1;
	return 0;
}

/* spec 1.7: how the offer O holds the demand ID: 'w' wholly, 'p' partially, 0 not (or O is no offer) */
static int holds(struct contract *o, const char *id)
{
	if (strcmp(o->kind, "offer"))
		return 0;
	return unsorted_string_list_has_string(&o->undertakes, id) ? 'w' :
	       unsorted_string_list_has_string(&o->partially, id) ? 'p' : 0;
}

struct contract *contract_lookup(const struct contract_view *v, const char *id)
{
	struct string_list_item *item = v->changed ? string_list_lookup(v->changed, id) : NULL;

	return item ? item->util : contract_get(v->at, id);
}

/* spec 1.7: an undertaking exists only while both its offer and demand are live. */
static void undertaking(const struct contract_view *v, struct contract *d, const char *id,
			int how, struct string_list *out)
{
	struct contract *o = contract_lookup(v, id);

	if (live(o) && holds(o, d->id) == how)
		string_list_insert(out, o->id)->util = o;
}

/* The live counterparts of C's whole ('w') or partial ('p') undertakings. */
void undertakings(const struct contract_view *v, struct contract *c, int how, struct string_list *out)
{
	struct string_list_item *item;

	if (!live(c))
		return;
	if (!strcmp(c->kind, "offer")) {
		for_each_string_list_item(item, how == 'w' ? &c->undertakes : &c->partially) {
			struct contract *d = contract_lookup(v, item->string);

			if (live(d) && !strcmp(d->kind, "demand"))
				string_list_insert(out, d->id)->util = d;
		}
	} else if (!strcmp(c->kind, "demand")) {
		for_each_string_list_item(item, &v->at->contracts)
			undertaking(v, c, item->string, how, out);
		if (v->changed)
			for_each_string_list_item(item, v->changed)
				undertaking(v, c, item->string, how, out);
	}
}

/* spec 1.8: whole offers lean on demands; partially held demands lean on offers. */
void dependencies(const struct contract_view *v, struct contract *c, struct string_list *out)
{
	undertakings(v, c, !strcmp(c->kind, "offer") ? 'w' : 'p', out);
}

/* spec 5.4: one judgment on one view and tree, with the current visiting path. */
static enum verdict judge_inner(const struct contract_view *v, struct contract *c, struct tree *x,
			       struct string_list *judging, struct string_list *code)
{
	struct string_list valid = STRING_LIST_INIT_DUP;
	struct string_list needs = STRING_LIST_INIT_DUP;
	struct string_list_item *f, *item;
	int whole_ok = 1;

	if (!strcmp(c->state, "cancelled"))
		return V_CANCELLED;
	if (!strcmp(c->state, "withdrawn"))
		return V_WITHDRAWN;
	valid_fulfilments(v->at, c, &valid);
	if (!strcmp(c->kind, "drop"))
		return c->fulfilled.nr ? V_FULFILLED : V_FUTURE;
	if (!valid.nr)
		return V_FUTURE;
	/*
	 * the undertakings it leans on (1.7): an offer on what it holds wholly,
	 * a demand on its partial offers. 5.4 knows no cycle, but a git merge
	 * that mixed undertakings (10.4) can make one: a contract met again
	 * while it is being judged is not current.
	 */
	if (string_list_has_string(judging, c->id))
		return V_UNVERIFIED;
	string_list_insert(judging, c->id);
	dependencies(v, c, &needs);
	for_each_string_list_item(item, &needs)
		if (judge_inner(v, item->util, x, judging, NULL) != V_CURRENT)
			whole_ok = 0;
	string_list_clear(&needs, 0);
	string_list_remove(judging, c->id, 0);
	for_each_string_list_item(f, &valid)
		if (whole_ok && provider_same(c, f->util, x, code))
			return V_CURRENT;
	return V_UNVERIFIED;
}

enum verdict judge_view(const struct contract_view *v, struct contract *c, struct tree *x)
{
	struct string_list judging = STRING_LIST_INIT_DUP;
	enum verdict result = judge_inner(v, c, x, &judging, NULL);

	string_list_clear(&judging, 0);
	return result;
}

enum verdict judge(struct branch *b, struct contract *c, struct tree *x)
{
	struct contract_view v = { b, NULL };

	return judge_view(&v, c, x);
}

/* spec 1.5: a promise is a live contract judged future or current ?unverified */
int promise(struct contract *c, enum verdict v)
{
	return live(c) && (v == V_FUTURE || v == V_UNVERIFIED);
}

const char *verdict_word(enum verdict v)
{
	static const char *const words[] = { "future", "current", "current ?unverified", "fulfilled", "cancelled",
					    "withdrawn" };

	return words[v];
}

/* spec 3.4, v14 3.8: C's provider as shown in X; a current C's scope no valid fulfilment has as X does is ?changed */
char *provider_display(struct branch *b, struct contract *c, struct tree *x)
{
	struct string_list valid = STRING_LIST_INIT_DUP;
	struct string_list_item *item, *f;
	struct strbuf sb = STRBUF_INIT;
	enum verdict v = judge(b, c, x);

	valid_fulfilments(b, c, &valid);
	for_each_string_list_item(item, &c->provider) {
		char *shown = scope_display(item->string, x);
		int same = 0;

		for_each_string_list_item(f, &valid)
			same |= recorded_same(f->util, item->string, x, NULL);
		strbuf_addf(&sb, "%s%s%s", sb.len ? "," : "", shown,
			    (v == V_CURRENT || v == V_UNVERIFIED) && !same && !strchr(shown, '?') ? " ?changed" : "");
	}
	return strbuf_detach(&sb, NULL);
}

/* spec 5.5: C, current at H, has a provider scope X changed from H */
int touched(struct branch *b, struct contract *c, struct tree *h, struct tree *x)
{
	struct contract_view v = { b, NULL };
	struct string_list judging = STRING_LIST_INIT_DUP, code = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	int changed = 0;

	if (judge_inner(&v, c, h, &judging, &code) == V_CURRENT)
		for_each_string_list_item(item, &code)
			changed |= !scope_same(item->string, item->util, x, NULL);
	string_list_clear(&judging, 0);
	string_list_clear(&code, 1);
	return changed;
}

/* spec 4.10: one violation under its key, which names it across trees */
static void violation(struct string_list *out, enum refusal kind, const char *a, const char *b)
{
	struct violation *v = xcalloc(1, sizeof(*v));

	v->kind = kind;
	v->a = xstrdup(a);
	v->b = xstrdup(b);
	string_list_insert(out, kind == PAIR ? xstrfmt("pair:%s:%s", a, b) : xstrfmt("mix:%s", a))->util = v;
}

/* spec 4.8: the words of a violation, chosen by its kind */
char *violation_words(struct violation *v)
{
	return refusal_words(v->kind, v->a, v->b, NULL);
}

/* Step zero checks the requested fulfilments before their files are written. */
static int fulfilled_in(struct branch *b, struct contract *c, const struct string_list *fulfilling)
{
	return (fulfilling && string_list_has_string(fulfilling, c->id)) || has_valid_fulfilment(b, c);
}

/*
 * spec 4.10 (b) and (c): the violations of B. A mix is told from WAS:
 * what held the demand there is what it is "already" held by.
 */
void invariants(struct branch *b, struct branch *was, const struct string_list *fulfilling, struct string_list *out)
{
	struct contract_view now = { b, NULL }, old = { was, NULL };
	struct string_list_item *item, *o;

	for_each_string_list_item(item, &b->contracts) {
		struct contract *d = item->util;
		struct string_list whole = STRING_LIST_INIT_DUP, part = STRING_LIST_INIT_DUP, before = STRING_LIST_INIT_DUP;

		if (!live(d) || strcmp(d->kind, "demand"))
			continue;
		undertakings(&now, d, 'w', &whole);
		undertakings(&now, d, 'p', &part);
		if (whole.nr && part.nr) {
			struct contract *previous = contract_get(was, d->id);

			if (previous)
				undertakings(&old, previous, 'w', &before);
			violation(out, before.nr ? MIX_WHOLLY : MIX_PARTIALLY, d->id,
				  before.nr ? before.items[0].string : part.items[0].string);
		}
		/* 1.8: what leans on an unfulfilled contract may not stand fulfilled on it */
		for_each_string_list_item(o, &whole)
			if (fulfilled_in(b, o->util, fulfilling) && !fulfilled_in(b, d, fulfilling))
				violation(out, PAIR, o->string, d->id);
		for_each_string_list_item(o, &part)
			if (fulfilled_in(b, d, fulfilling) && !fulfilled_in(b, o->util, fulfilling))
				violation(out, PAIR, d->id, o->string);
		string_list_clear(&whole, 0);
		string_list_clear(&part, 0);
		string_list_clear(&before, 0);
	}
}
