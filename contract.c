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
static void *bad(char **err, const char *fmt, ...)
{
	struct strbuf sb = STRBUF_INIT;
	va_list ap;

	va_start(ap, fmt);
	strbuf_vaddf(&sb, fmt, ap);
	va_end(ap);
	*err = strbuf_detach(&sb, NULL);
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
	struct tree *t;
	size_t alloc = 0;
	char oid[48], *p, *end;

	if (!object(xstrfmt("%s^{tree}", treeish), 0, oid, NULL))
		reject(128, NULL, "git failed: 'git cat-file': not a tree: '%s'", treeish);
	if ((hit = string_list_lookup(&listed, oid)))
		return hit->util;
	t = xcalloc(1, sizeof(*t));
	snprintf(t->oid, sizeof(t->oid), "%s", oid);
	string_list_insert(&listed, oid)->util = t;
	for (p = git("ls-tree -r -t -z --full-tree %s", oid); *p; p = end + 1) {
		struct entry *e;

		end = p + strlen(p);
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
	const char *last = "";
	char *p, *end;

	if (x)
		return x;
	for (p = git("ls-files -u -z"), end = p + git_out_len; p < end; p += strlen(p) + 1) {
		const char *path = strchr(p, '\t') + 1;
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
	p = git("ls-files -s -z");
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

int yaml_parse(const char *text, struct string_list *kv, const char *what, char **err)
{
	struct yframe stack[YAML_DEPTH];
	yaml_parser_t parser;
	yaml_event_t e;
	const char *why = NULL;
	int depth = 0, docs = 0, done = 0;
	size_t line = 0;

	*err = NULL;
	if ((why = not_text(text, &line))) {
		*err = xstrfmt("'%s': %s in line %d", what, why, (int)line + 1);
		return -1;
	}
	yaml_parser_initialize(&parser);
	yaml_parser_set_input_string(&parser, (const unsigned char *)text, strlen(text));
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
		*err = xstrfmt("'%s': %s in line %d", what, why, (int)line + 1);
	yaml_parser_delete(&parser);
	return why ? -1 : 0;
}

const char *yget(struct string_list *kv, const char *key)
{
	struct string_list_item *item = unsorted_string_list_lookup(kv, key);

	return item ? ((struct yval *)item->util)->s : NULL;
}

static char ykind(struct string_list *kv, const char *key)
{
	struct string_list_item *item = unsorted_string_list_lookup(kv, key);

	return item ? ((struct yval *)item->util)->kind : -1;
}

static int yline(struct string_list *kv, const char *key)
{
	struct string_list_item *item = unsorted_string_list_lookup(kv, key);

	if (!item)
		item = unsorted_string_list_lookup(kv, ROOT);
	return item ? ((struct yval *)item->util)->line : 1;
}

/* the scalars KEY.0, KEY.1, ... of a list KEY (or of a key with nothing under it): -1 when KEY is no list */
static int yaml_list(struct string_list *kv, const char *key, struct string_list *out)
{
	char k = ykind(kv, key);
	int i;

	if (k != 'l' && k != '?')
		return -1;
	for (i = 0; yget(kv, xstrfmt("%s.%d", key, i)); i++) {
		if (ykind(kv, xstrfmt("%s.%d", key, i)))
			return -1;
		string_list_append(out, yget(kv, xstrfmt("%s.%d", key, i)));
	}
	return 0;
}

static int yaml_out(void *sb, unsigned char *buf, size_t n)
{
	strbuf_add(sb, buf, n);
	return 1;
}

/* S as one "..." scalar into SB, written by libyaml's emitter (v14 8.2) */
static void yaml_out_scalar(struct strbuf *sb, const char *s)
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
			block &= (unsigned char)*p >= 0x20 || *p == '\t';
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

/* v14 3.1: a repository path */
int path_ok(const char *p)
{
	struct string_list parts = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	const char *q;
	int ok = *p && *p != '/' && *p != '-' && strcmp(p, ".ait") && !starts_with(p, ".ait/") && !p[strcspn(p, " :,")];

	for (q = p; *q; q++)
		ok &= (unsigned char)*q >= 0x20 && *q != 0x7f;
	string_list_split(&parts, p, "/", -1);
	for_each_string_list_item(item, &parts)
		ok &= *item->string && strcmp(item->string, ".") && strcmp(item->string, "..");
	string_list_clear(&parts, 0);
	return ok;
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

int range_of(const char *v, int *start, int *end)
{
	const char *colon;
	char *next;
	long a, b;

	skip_prefix(v, "new:", &v);
	colon = strchr(v, ':');
	if (!colon || colon > v + strcspn(v, " "))
		return 0;
	a = strtol(colon + 1, &next, 10);
	if (*next++ != '-')
		return -1;
	b = strtol(next, &next, 10);
	if (a < 1 || b < a || b > INT_MAX || (*next && *next != ' ') ||
	    strcmp(xstrndup(colon + 1, next - colon - 1), xstrfmt("%ld-%ld", a, b)))
		return -1;
	*start = a;
	*end = b;
	return 1;
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

/* spec 2.3.2, 3: a stored scope; a line range carries its base blob, nothing else does */
int scope_ok(const char *v, int allow_new, int allow_abs)
{
	char *path = scope_path(v);
	const char *blob = scope_blob(v);
	int start, end, range = range_of(v, &start, &end);

	if (range < 0 || (range && starts_with(v, "new:")))
		return 0;
	if (range)
		return path_ok(path) && blob && strlen(blob) == 40 && !blob[strspn(blob, "0123456789abcdef")];
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
	struct string_list_item *item;

	QSORT(l->items, l->nr, cmp_scope);
	for_each_string_list_item(item, l)
		strbuf_addf(&sb, "%s%s", sb.len ? "," : "", item->string);
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
static char *keys_refusal(struct string_list *kv, const char *prefix, const char *const *keys, int required)
{
	struct string_list_item *item;
	size_t plen = *prefix ? strlen(prefix) + 1 : 0;

	for_each_string_list_item(item, kv) {
		const char *k = item->string;

		if (!strcmp(k, ROOT) || (plen && (strncmp(k, prefix, plen - 1) || k[plen - 1] != '.')))
			continue;
		if (!one_of(xstrndup(k + plen, strcspn(k + plen, ".")), keys))
			return xstrfmt("unknown key '%s' in line %d", k, ((struct yval *)item->util)->line);
	}
	for (; required && *keys; keys++)
		if (!unsorted_string_list_has_string(kv, pk(prefix, *keys)))
			return xstrfmt("'%s' is missing", *keys);
	return NULL;
}

/* v14 2.5.2 without inputs: a spec under PREFIX of KV; NULL with ERR */
struct spec *spec_from(struct string_list *kv, const char *prefix, char **err)
{
	static const char *const keys[] = { "body", "engine", "command", "success", "environment", NULL };
	struct spec *s = xcalloc(1, sizeof(*s));
	struct string_list cmd = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	const char *engine = yget(kv, pk(prefix, "engine"));

	strvec_init(&s->command);
	string_list_init_dup(&s->success);
	if ((*err = keys_refusal(kv, prefix, keys, 0)))
		return NULL;
	s->body = xstrdup_null(yget(kv, pk(prefix, "body")));
	if (!s->body || ykind(kv, pk(prefix, "body")))
		return bad(err, "'%s' is missing or not a string", pk(prefix, "body"));
	if (engine && *engine && strcmp(engine, "exec"))
		return bad(err, "unknown engine '%s'", engine);
	s->engine = engine && *engine ? xstrdup(engine) : NULL;
	if ((yget(kv, pk(prefix, "command")) && yaml_list(kv, pk(prefix, "command"), &cmd)) ||
	    (yget(kv, pk(prefix, "success")) && yaml_list(kv, pk(prefix, "success"), &s->success)))
		return bad(err, "'command' and 'success' are lists");
	for_each_string_list_item(item, &cmd)
		strvec_push(&s->command, item->string);
	for_each_string_list_item(item, &s->success)
		if (!*item->string || item->string[strspn(item->string, "0123456789")] ||
		    (*item->string == '0' && item->string[1]))
			return bad(err, "'success' holds '%s', not a decimal integer", item->string);
	s->environment = xstrdup_null(yget(kv, pk(prefix, "environment")));
	if (s->engine && !s->command.nr)
		return bad(err, "an exec spec needs 'command'");
	if (!s->engine && (s->command.nr || s->success.nr || s->environment))
		return bad(err, "a prompt spec takes only 'body'");
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
struct contract *contract_parse(const char *text, const char *path, int draft, char **err)
{
	static const char *const states_do[] = { "future", "cancelled", "withdrawn", NULL };
	static const char *const states_x[] = { "future", "cancelled", NULL };
	struct string_list kv = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct contract *c;
	const char *const *keys, *v;
	char *id = contract_path_id(path);
	int i, demand, offer, drop;

	if (yaml_parse(text, &kv, path, err))
		return NULL;
	*err = NULL;
	if (!id)
		return bad(err, "'%s': not a contract path", path);
	v = yget(&kv, "kind");
	if (!v || !(keys = keys_of(v)) || "DOX"[!strcmp(v, "demand") ? 0 : !strcmp(v, "offer") ? 1 : 2] != *id)
		return bad(err, "'%s': 'kind' is missing or does not match the ID in line %d", path, yline(&kv, "kind"));
	c = contract_new(v, group_of(path), id);
	demand = !strcmp(v, "demand");
	offer = !strcmp(v, "offer");
	drop = !strcmp(v, "drop");
	if ((v = keys_refusal(&kv, "", keys, 1)))
		return bad(err, "'%s': %s", path, v);
	c->condition_id = xstrdup(yget(&kv, "condition-id"));
	c->change_id = xstrdup(yget(&kv, "change-id"));
	c->creator = xstrdup(yget(&kv, "creator"));
	c->state = xstrdup(yget(&kv, "state"));
	if ((draft && !*c->condition_id ? 0 : !id_ok(c->condition_id, "R")) ||
	    (draft && !*c->change_id ? 0 : !id_ok(c->change_id, "R")) ||
	    (draft && !*c->creator ? 0 : !agent_ok(c->creator)) ||
	    !one_of(c->state, drop ? states_x : states_do))
		return bad(err, "'%s': bad 'condition-id', 'change-id', 'creator' or 'state'", path);
	if (demand)
		scopes_split(yget(&kv, "consumer"), &c->consumer);
	if (!drop)
		scopes_split(yget(&kv, "provider"), &c->provider);
	/* a draft's line ranges are written without their base blob; contract commit fixes them at HEAD (v14 3.3) */
	for (i = 0; i < 2; i++)
		for_each_string_list_item(item, i ? &c->provider : &c->consumer)
			if (!scope_ok(item->string, 1, i) &&
			    !(draft && !scope_blob(item->string) && range_of(item->string, &(int){ 0 }, &(int){ 0 }) == 1))
				return bad(err, "'%s': bad scope '%s' in '%s'", path, item->string, i ? "provider" : "consumer");
	if (!drop && (!c->provider.nr || (demand && !c->consumer.nr)))
		return bad(err, "'%s': a scope list is empty", path);
	if (drop && !id_ok(c->target = xstrdup(yget(&kv, "target")), "DO"))
		return bad(err, "'%s': bad 'target'", path);
	if (offer && (yaml_list(&kv, "undertakes", &c->undertakes) || yaml_list(&kv, "undertakes-partially", &c->partially)))
		return bad(err, "'%s': 'undertakes' and 'undertakes-partially' are lists", path);
	for_each_string_list_item(item, &c->undertakes)
		if (!id_ok(item->string, "D") || unsorted_string_list_has_string(&c->partially, item->string))
			return bad(err, "'%s': bad ID '%s' in 'undertakes'", path, item->string);
	for_each_string_list_item(item, &c->partially)
		if (!id_ok(item->string, "D"))
			return bad(err, "'%s': bad ID '%s' in 'undertakes-partially'", path, item->string);
	if (yaml_list(&kv, "fulfilled", &c->fulfilled))
		return bad(err, "'%s': 'fulfilled' is a list", path);
	for_each_string_list_item(item, &c->fulfilled) {
		const char *rest;
		char *want = xstrfmt("%s%sfulfill/%s-", c->group, *c->group ? "/" : "", c->id);

		if (!skip_prefix(item->string, want, &rest) || !ends_with(rest, ".md") ||
		    !id_ok(xstrndup(rest, strlen(rest) - 3), "R"))
			return bad(err, "'%s': bad path '%s' in 'fulfilled'", path, item->string);
	}
	if (drop)
		return c;
	if (ykind(&kv, "specs") != 'l' || !yget(&kv, "specs.0"))
		return bad(err, "'%s': 'specs' is no list of specs in line %d", path, yline(&kv, "specs"));
	for (i = 0; yget(&kv, xstrfmt("specs.%d", i)); i++) {
		struct spec *s = spec_from(&kv, xstrfmt("specs.%d", i), err);

		if (!s)
			return bad(err, "'%s': %s", path, *err);
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

/* an exec spec's "command" and "success" lists under "  "; WHOLE writes the default success too (7.2) */
void spec_lists(struct strbuf *sb, struct spec *s, int whole)
{
	size_t j;

	strbuf_addstr(sb, "  command: [");
	for (j = 0; j < s->command.nr; j++) {
		if (j)
			strbuf_addstr(sb, ", ");
		yaml_out_scalar(sb, s->command.v[j]);
	}
	strbuf_addstr(sb, "]\n");
	if (!s->success.nr && !whole)
		return;
	strbuf_addstr(sb, "  success: [");
	for (j = 0; j < s->success.nr; j++)
		strbuf_addf(sb, "%s%s", j ? ", " : "", s->success.items[j].string);
	strbuf_addstr(sb, s->success.nr ? "]\n" : "0]\n");
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
			spec_lists(&sb, s, 0);
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

/* spec 2.5: the fulfilment at PATH from TEXT, or NULL with ERR */
static struct fulfilment *fulfil_parse(const char *text, const char *path, char **err)
{
	static const char *const keys[] = { "contract", "condition-id", "by", "session", "commit", "outcome", "code", NULL };
	struct string_list kv = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct fulfilment *f = xcalloc(1, sizeof(*f));
	const char *end;
	char *front;

	*err = NULL;
	if (!starts_with(text, "---\n") || !(end = strstr(text + 3, "\n---\n")))
		return bad(err, "'%s': no front matter", path);
	front = xstrndup(text + 4, end + 1 - (text + 4));
	if (yaml_parse(front, &kv, path, err))
		return NULL;
	if ((front = keys_refusal(&kv, "", keys, 1)))
		return bad(err, "'%s': %s", path, front);
	f->path = xstrdup(path);
	f->contract = xstrdup(yget(&kv, "contract"));
	f->condition_id = xstrdup(yget(&kv, "condition-id"));
	f->by = xstrdup(yget(&kv, "by"));
	f->session = xstrdup(yget(&kv, "session"));
	f->commit = xstrdup(yget(&kv, "commit"));
	f->outcome = xstrdup(yget(&kv, "outcome"));
	f->body = xstrdup(end + 5);
	string_list_init_dup(&f->code);
	if (!id_ok(f->contract, "DOX") || !id_ok(f->condition_id, "R") || !agent_ok(f->by) || !id_ok(f->session, "S") ||
	    strlen(f->commit) != 40 || !one_of(f->outcome, (const char *const[]){ "pass", "forced", "withdrawn", NULL }) ||
	    (ykind(&kv, "code") != 'm' && ykind(&kv, "code") != '?' && strcmp(yget(&kv, "code"), "{}")))
		return bad(err, "'%s': a bad value in the front matter", path);
	for_each_string_list_item(item, &kv) {
		const char *scope;

		if (skip_prefix(item->string, "code.", &scope)) {
			struct strbuf unescaped = STRBUF_INIT;

			for (; *scope; scope++)
				if (*scope != '\\' || !scope[1])
					strbuf_addch(&unescaped, *scope);
				else
					strbuf_addch(&unescaped, *++scope);
			string_list_insert(&f->code, unescaped.buf)->util = ((struct yval *)item->util)->s;
		}
	}
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
		yaml_out_scalar(&sb, item->util);
		strbuf_addch(&sb, '\n');
	}
	strbuf_addf(&sb, "---\n%s", f->body);
	strbuf_complete_line(&sb);
	return strbuf_detach(&sb, NULL);
}

/* the blob OID as text; NULL when it holds a NUL, which no contract or fulfilment does */
static const char *blob_text(const char *oid)
{
	size_t n;
	char *text = object(oid, 1, NULL, &n);

	return text && strlen(text) == n ? text : NULL;
}

/*
 * spec 2.1.3, 2.3, 2.5: the contracts and fulfilments of the branch NAME at
 * TIP (or of the tree TREEISH); each malformed file is one error
 */
struct branch *branch_load(const char *name, const char *tip, const char *treeish)
{
	static struct string_list loaded = STRING_LIST_INIT_DUP;
	struct string_list_item *hit, *item;
	struct branch *b;
	char oid[48];
	size_t i;

	if (!treeish && !tip)
		tip = object(xstrfmt("refs/heads/%s^{commit}", name), 0, oid, NULL) ? xstrdup(oid) : NULL;
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
		const char *text;
		char *err = NULL;

		if (strcmp(e->type, "blob"))
			continue;
		if (!(text = blob_text(e->oid)))
			err = xstrfmt("'%s': a NUL character", e->path);
		else if (ends_with(e->path, ".md") && (starts_with(e->path, "fulfill/") || strstr(e->path, "/fulfill/"))) {
			struct fulfilment *f = fulfil_parse(text, e->path, &err);

			if (f)
				string_list_insert(&b->fulfils, e->path)->util = f;
		} else {
			struct contract *c = contract_parse(text, e->path, 0, &err);

			if (c && string_list_lookup(&b->contracts, c->id))
				err = xstrfmt("'%s': '%s' is also at '%s'", e->path, c->id,
					      ((struct contract *)string_list_lookup(&b->contracts, c->id)->util)->path);
			else if (c)
				string_list_insert(&b->contracts, c->id)->util = c;
		}
		if (err)
			string_list_append(&b->errors, err);
	}
	for_each_string_list_item(item, &b->contracts) {
		struct contract *c = item->util;
		struct string_list_item *f;

		for_each_string_list_item(f, &c->fulfilled) {
			struct string_list_item *got = string_list_lookup(&b->fulfils, f->string);

			if (!got || strcmp(((struct fulfilment *)got->util)->contract, c->id))
				string_list_append(&b->errors, xstrfmt("'%s': '%s' is not a fulfilment of '%s'",
								       c->path, f->string, c->id));
		}
	}
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

static int has_valid(struct branch *b, struct contract *c)
{
	struct string_list v = STRING_LIST_INIT_DUP;

	valid_fulfilments(b, c, &v);
	return v.nr > 0;
}

enum scope_status { FOUND, NOBASE, MISSING, DELETED };

static void hunk_part(const char *text, int *start, int *length)
{
	char *next;

	*start = strtol(text, &next, 10);
	*length = *next == ',' ? strtol(next + 1, NULL, 10) : 1;
}

/* v14 3.6: the diff of two blobs, as every range is judged */
static char *diff0(const char *a, const char *b)
{
	return git("diff -U0 --no-color --no-ext-diff --no-textconv --text --inter-hunk-context=0 "
		   "--diff-algorithm=myers --no-indent-heuristic %s %s", a, b);
}

/*
 * v14 3.7 and 3.10: the range S-E of the blob BASE mapped to the blob NOW;
 * CHANGED when a hunk's touch span (an insertion [os, os+1]) meets S-E
 */
static enum scope_status map_blobs(const char *base, const char *now, int a, int b, int *start, int *end, int *changed)
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
		*start = a;
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
	*start = ma < 0 ? a + offset : ma;
	*end = mb < 0 ? b + offset : mb;
	return *start > *end ? DELETED : FOUND;
}

static enum scope_status map_range(const char *scope, struct tree *t, int *start, int *end, int *changed)
{
	int a, b;

	range_of(scope, &a, &b);
	return map_blobs(scope_blob(scope), blob_at(t, scope_path(scope)), a, b, start, end, changed);
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
	int start, end;

	*err = NULL;
	if (!scope_ok(arg, 1, 1) && !(range_of(arg, &start, &end) == 1 && !scope_blob(arg)))
		return bad(err, "'%s' is not a scope", arg);
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
		return bad(err, "'%s' is not a file", path);
	if (range_of(arg, &start, &end) == 1) {
		size_t n;
		char *text = blob_at(t, path) ? object(blob_at(t, path), 1, NULL, &n) : NULL;

		if (!text || strlen(text) != n || end > line_count(text, n))
			return bad(err, "'%s' is not a range of '%s'", arg, path);
		return xstrfmt("%s:%d-%d %s", path, start, end, blob_at(t, path));
	}
	return xstrdup(path);
}

/* spec 6.7.4: where the concrete scope SCOPE is in T ("PATH" or "PATH:s-e"), or NULL with ERR */
char *scope_locate(const char *scope, struct tree *t, char **err)
{
	char *path = scope_path(scope);
	int start, end, changed;

	*err = NULL;
	if (starts_with(scope, "new:") || ends_with(scope, "/"))
		return bad(err, "'%s' is not a concrete scope", scope);
	if (range_of(scope, &start, &end) != 1)
		return tree_get(t, path) && strcmp(tree_get(t, path)->type, "tree") ? path :
		       bad(err, "'%s' is not in the code", scope);
	switch (map_range(scope, t, &start, &end, &changed)) {
	case NOBASE:
		return bad(err, "repository lacks the base blob of '%s'", path);
	case MISSING:
	case DELETED:
		return bad(err, "'%s' is not in the code", scope);
	default:
		return xstrfmt("%s:%d-%d", path, start, end);
	}
}

/* spec 2.5: the code entry of SCOPE at T: "mode type oid", and "s-e" for a range */
char *code_entry(const char *scope, struct tree *t, char **err)
{
	char *where = scope_locate(scope, t, err), *colon;
	struct entry *e;

	if (!where)
		return NULL;
	colon = strchr(where, ':');
	e = tree_get(t, scope_path(scope));
	return colon ? xstrfmt("%s %s %s %s", e->mode, e->type, e->oid, colon + 1) :
		       xstrfmt("%s %s %s", e->mode, e->type, e->oid);
}

/* spec 5.3: the scope is in X as the code entry CODE recorded it */
static int scope_same(const char *scope, const char *code, struct tree *x)
{
	char mode[8], type[8], oid[48], span[32] = "";
	struct entry *e = tree_get(x, scope_path(scope));
	int s, end, a, b, changed;

	if (!code || sscanf(code, "%7s %7s %47s %31s", mode, type, oid, span) < 3 || !e || strcmp(e->mode, mode))
		return 0;
	if (!*span)
		return !strcmp(e->oid, oid);
	if (sscanf(span, "%d-%d", &a, &b) != 2)
		return 0;
	return map_blobs(oid, e->oid, a, b, &s, &end, &changed) == FOUND && !changed;
}

/* spec 5.3: the fulfilment F recorded SCOPE as X has it */
static int recorded_same(struct fulfilment *f, const char *scope, struct tree *x)
{
	struct string_list_item *code = unsorted_string_list_lookup(&f->code, scope);

	return code && scope_same(scope, code->util, x);
}

/* v14 3.8: the scope as shown, located in X */
static char *scope_display(const char *scope, struct tree *x)
{
	char *err, *where;

	if (starts_with(scope, "new:"))
		return xstrfmt("%s%s", scope, tree_get(x, scope_path(scope)) ? " ?exists" : "");
	if (ends_with(scope, "/"))
		return xstrdup(scope);
	if ((where = scope_locate(scope, x, &err)))
		return where;
	return xstrfmt("%s %s", xstrndup(scope, strcspn(scope, " ")),
		       starts_with(err, "repository lacks") ? "?nobase" : blob_at(x, scope_path(scope)) ? "?deleted" : "?missing");
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
int contract_overlaps(struct contract *c, const char *q, struct tree *t)
{
	struct string_list_item *s;

	for_each_string_list_item(s, &c->consumer)
		if (scope_overlap(q, s->string, t))
			return 1;
	for_each_string_list_item(s, &c->provider)
		if (scope_overlap(q, s->string, t))
			return 1;
	return 0;
}

/* spec 1.7: how the offer O holds the demand ID: 'w' wholly, 'p' partially, 0 not (or O is no offer) */
int holds(struct contract *o, const char *id)
{
	if (!o || strcmp(o->kind, "offer"))
		return 0;
	return unsorted_string_list_has_string(&o->undertakes, id) ? 'w' :
	       unsorted_string_list_has_string(&o->partially, id) ? 'p' : 0;
}

/* spec 5.4: the judgment of C on branch B in tree X; one definition */
enum verdict judge(struct branch *b, struct contract *c, struct tree *x)
{
	struct string_list valid = STRING_LIST_INIT_DUP;
	struct string_list_item *f, *item;
	int whole_ok = 1;

	if (!strcmp(c->state, "cancelled"))
		return V_CANCELLED;
	if (!strcmp(c->state, "withdrawn"))
		return V_WITHDRAWN;
	valid_fulfilments(b, c, &valid);
	if (!strcmp(c->kind, "drop"))
		return c->fulfilled.nr ? V_FULFILLED : V_FUTURE;
	if (!valid.nr)
		return V_FUTURE;
	/* the undertakings it leans on (1.7): an offer on what it holds wholly, a demand on its partial offers */
	if (!strcmp(c->kind, "offer")) {
		for_each_string_list_item(item, &c->undertakes) {
			struct contract *d = contract_get(b, item->string);

			if (live(d) && judge(b, d, x) != V_CURRENT)
				whole_ok = 0;
		}
	} else {
		for_each_string_list_item(item, &b->contracts) {
			struct contract *o = item->util;

			if (live(o) && holds(o, c->id) == 'p' && judge(b, o, x) != V_CURRENT)
				whole_ok = 0;
		}
	}
	for_each_string_list_item(f, &valid) {
		int same = 1;

		for_each_string_list_item(item, &c->provider)
			same &= recorded_same(f->util, item->string, x);
		if (same && whole_ok)
			return V_CURRENT;
	}
	return V_UNVERIFIED;
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
			same |= recorded_same(f->util, item->string, x);
		strbuf_addf(&sb, "%s%s%s", sb.len ? "," : "", shown,
			    (v == V_CURRENT || v == V_UNVERIFIED) && !same && !strchr(shown, '?') ? " ?changed" : "");
	}
	return strbuf_detach(&sb, NULL);
}

/* spec 5.5: C, current at H, has a provider scope X changed from H */
int touched(struct branch *b, struct contract *c, struct tree *h, struct tree *x)
{
	struct string_list_item *item;
	char *err, *code;

	if (strcmp(c->kind, "drop") && judge(b, c, h) == V_CURRENT)
		for_each_string_list_item(item, &c->provider)
			if ((code = code_entry(item->string, h, &err)) && !scope_same(item->string, code, x))
				return 1;
	return 0;
}

/* the live offers that hold the demand ID in the way HOW ('w' or 'p') */
static void holders(struct branch *b, const char *id, int how, struct string_list *out)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &b->contracts)
		if (live(item->util) && holds(item->util, id) == how)
			string_list_append(out, item->string);
}

static void pair(struct string_list *out, struct contract *upper, struct contract *lower)
{
	string_list_insert(out, xstrfmt("pair:%s:%s changing '%s' would leave '%s' fulfilled on top of an "
					"unfulfilled '%s'", upper->id, lower->id, lower->id, upper->id, lower->id));
}

/*
 * spec 4.10 (b) and (c): each violation of B as "KEY message", KEY naming it
 * across trees, so a new tree's violations compare with the old one's. A mix
 * is told from WAS: what held the demand there is what it is "already" held by.
 */
void invariants(struct branch *b, struct branch *was, struct string_list *out)
{
	struct string_list_item *item, *o;

	for_each_string_list_item(item, &b->contracts) {
		struct contract *d = item->util;
		struct string_list whole = STRING_LIST_INIT_DUP, part = STRING_LIST_INIT_DUP, before = STRING_LIST_INIT_DUP;

		if (!live(d) || strcmp(d->kind, "demand"))
			continue;
		holders(b, d->id, 'w', &whole);
		holders(b, d->id, 'p', &part);
		if (whole.nr && part.nr) {
			holders(was, d->id, 'w', &before);
			string_list_insert(out, xstrfmt("mix:%s '%s' is already undertaken %s by '%s'", d->id, d->id,
							before.nr ? "wholly" : "partially",
							before.nr ? before.items[0].string : part.items[0].string));
		}
		/* 1.8: what leans on an unfulfilled contract may not stand fulfilled on it */
		for_each_string_list_item(o, &whole)
			if (has_valid(b, contract_get(b, o->string)) && !has_valid(b, d))
				pair(out, contract_get(b, o->string), d);
		for_each_string_list_item(o, &part)
			if (has_valid(b, d) && !has_valid(b, contract_get(b, o->string)))
				pair(out, d, contract_get(b, o->string));
	}
}
