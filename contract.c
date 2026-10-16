/*
 * Trees, contracts, fulfilments and what is judged from them (spec 2, 3, 5).
 * Contracts and fulfilments are stored facts; the rest is computed (rule 1).
 */
#include "ait.h"
#include <yaml.h>


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
	struct string_list records = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct tree *t;
	size_t alloc = 0;
	char oid[48], *expression = xstrfmt("%s^{tree}", treeish);

	if (!object_oid(expression, oid))
		reject(128, NULL, "git failed: 'git cat-file': not a tree: '%s'", treeish);
	free(expression);
	t = xcalloc(1, sizeof(*t));
	snprintf(t->oid, sizeof(t->oid), "%s", oid);
	git_records(&records, NULL, NULL, "ls-tree", "-r", "-t", "-z", "--full-tree", oid);
	for_each_string_list_item(item, &records) {
		struct entry *e;
		const char *p = item->string;

		ALLOC_GROW(t->e, t->nr + 1, alloc);
		e = &t->e[t->nr++];
		if (sscanf(p, "%7s %7s %47s", e->mode, e->type, e->oid) != 3)
			BUG("git ls-tree: '%s'", p);
		e->path = xstrdup(strchr(p, '\t') + 1);
	}
	QSORT(t->e, t->nr, cmp_entry);
	string_list_clear(&records, 0);
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
	struct tree *x, *h = NULL;
	struct strbuf info = STRBUF_INIT;
	struct string_list records = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	const char *last = "";
	char *p;

	git_records(&records, NULL, NULL, "ls-files", "-u", "-z");
	if (records.nr && head)
		h = tree_load(head);
	for_each_string_list_item(item, &records) {
		const char *path = strchr(item->string, '\t') + 1;
		struct entry *e = h ? tree_get(h, path) : NULL;

		/* the stages of a path come one after another */
		if (!strcmp(path, last))
			continue;
		last = path;
		warning("'%s' is unmerged; it is read as HEAD has it", path);
		strbuf_addf(&info, "0 %s\t%s%c", NULL_OID, path, 0);
		if (e)
			strbuf_addf(&info, "%s %s\t%s%c", e->mode, e->oid, path, 0);
	}
	if (!info.len) {
		p = git("write-tree");
		x = tree_load(line1(p));
	} else {
		p = git("ls-files", "-s", "-z");
		strbuf_splice(&info, 0, 0, p, git_out_len);
		free(p);
		p = tree_with(EMPTY_TREE, &info);
		x = tree_load(p);
	}
	free(p);
	tree_free(h);
	string_list_clear(&records, 0);
	strbuf_release(&info);
	return x;
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

void yaml_clear(struct string_list *kv)
{
	struct string_list_item *item;

	for_each_string_list_item(item, kv) {
		struct yval *v = item->util;

		free(v->s);
		free(v);
	}
	string_list_clear(kv, 0);
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
	if (frame->kind == 'l') {
		char index[32];

		snprintf(index, sizeof(index), "%d", frame->n++);
		return yaml_path(frame->path, index);
	}
	if (frame->key) {
		path = yaml_path(frame->path, frame->key);
		free(frame->key);
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

static const char *yaml_event(struct string_list *kv, struct yframe *stack, int *depth, yaml_event_t *e, struct strbuf *reason)
{
	const char *why = NULL;
	char *path;

	if (e->type == YAML_MAPPING_END_EVENT || e->type == YAML_SEQUENCE_END_EVENT) {
		(*depth)--;
		free(stack[*depth].path);
		free(stack[*depth].key);
		memset(&stack[*depth], 0, sizeof(stack[*depth]));
	}
	if (e->type != YAML_SCALAR_EVENT && e->type != YAML_MAPPING_START_EVENT && e->type != YAML_SEQUENCE_START_EVENT)
		return NULL;
	if (e->type == YAML_SCALAR_EVENT && strlen((char *)e->data.scalar.value) != e->data.scalar.length)
		return "a NUL character";
	path = yaml_place(*depth ? &stack[*depth - 1] : NULL, e, &why);
	if (why)
		return why;
	if (!*depth)
		path = xstrdup(ROOT);
	if (path && unsorted_string_list_has_string(kv, path)) {
		strbuf_addf(reason, "duplicate key '%s'", path);
		free(path);
		return reason->buf;
	}
	if (path) {
		struct yval *v = xcalloc(1, sizeof(*v));

		v->s = xstrdup(e->type == YAML_SCALAR_EVENT ? (char *)e->data.scalar.value : "");
		v->kind = yaml_kind(e);
		v->line = e->start_mark.line + 1;
		string_list_append(kv, path)->util = v;
	}
	if (e->type == YAML_SCALAR_EVENT) {
		free(path);
		return NULL;
	}
	if (*depth == YAML_DEPTH) {
		free(path);
		return "nested too deeply";
	}
	stack[*depth].path = strcmp(path, ROOT) ? path : NULL;
	if (!stack[*depth].path)
		free(path);
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

/* v14 2.5.1 permits LF only, even where YAML itself accepts other breaks. */
static const char *non_lf(const char *text)
{
	static const char *const breaks[] = { "\r", "\xc2\x85", "\xe2\x80\xa8", "\xe2\x80\xa9", NULL };
	const char *p, *first = NULL;
	int i;

	for (i = 0; breaks[i]; i++)
		if ((p = strstr(text, breaks[i])) && (!first || p < first))
			first = p;
	return first;
}

int yaml_parse(const char *text, size_t len, struct string_list *kv, char **err)
{
	struct yframe stack[YAML_DEPTH] = { 0 };
	struct strbuf reason = STRBUF_INIT;
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
	if ((why = non_lf(text))) {
		*err = xstrfmt("a line break other than LF in line %d", (int)line_at(text, why - text) + 1);
		return -1;
	}
	yaml_parser_initialize(&parser);
	yaml_parser_set_encoding(&parser, YAML_UTF8_ENCODING);
	yaml_parser_set_input_string(&parser, (const unsigned char *)text, len);
	while (!done && !why && yaml_parser_parse(&parser, &e)) {
		line = e.start_mark.line;
		docs += e.type == YAML_DOCUMENT_START_EVENT;
		why = yaml_outside(&e, docs > 1);
		if (!why)
			why = yaml_event(kv, stack, &depth, &e, &reason);
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
	strbuf_release(&reason);
	while (depth) {
		free(stack[--depth].path);
		free(stack[depth].key);
	}
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
	for (i = 0; ; i++) {
		char *child = xstrfmt("%s.%d", key, i), *why = NULL;
		const char *value = yget(kv, child);

		if (value && ykind(kv, child))
			why = yaml_refusal(kv, child, "'%s' is not a scalar", child);
		if (value && !why)
			string_list_append(out, value);
		free(child);
		if (why || !value)
			return why;
	}
}

static int yaml_out(void *sb, unsigned char *buf, size_t n)
{
	strbuf_add(sb, buf, n);
	return 1;
}

/*
 * v14 8.2: a scalar inside two block sequences gives the four-space
 * indentation used by both specs and tasks. libyaml chooses escaping and
 * chomping too;
 * discard the synthetic "- - " prefix and document terminator only.
 */
static void yaml_scalar(struct strbuf *sb, const char *s, yaml_scalar_style_t style)
{
	yaml_emitter_t emitter;
	yaml_event_t event;
	struct strbuf one = STRBUF_INIT;
	int ok;

	if (!yaml_emitter_initialize(&emitter))
		die("out of memory");
	yaml_emitter_set_output(&emitter, yaml_out, &one);
	yaml_emitter_set_unicode(&emitter, 1);
	yaml_emitter_set_width(&emitter, -1);
	yaml_emitter_set_indent(&emitter, 2);
	yaml_emitter_set_break(&emitter, YAML_LN_BREAK);
	ok = yaml_stream_start_event_initialize(&event, YAML_UTF8_ENCODING) && yaml_emitter_emit(&emitter, &event) &&
	     yaml_document_start_event_initialize(&event, NULL, NULL, NULL, 1) && yaml_emitter_emit(&emitter, &event) &&
	     yaml_sequence_start_event_initialize(&event, NULL, NULL, 1, YAML_BLOCK_SEQUENCE_STYLE) && yaml_emitter_emit(&emitter, &event) &&
	     yaml_sequence_start_event_initialize(&event, NULL, NULL, 1, YAML_BLOCK_SEQUENCE_STYLE) && yaml_emitter_emit(&emitter, &event) &&
	     yaml_scalar_event_initialize(&event, NULL, NULL, (yaml_char_t *)s, strlen(s), 1, 1, style) && yaml_emitter_emit(&emitter, &event) &&
	     yaml_sequence_end_event_initialize(&event) && yaml_emitter_emit(&emitter, &event) &&
	     yaml_sequence_end_event_initialize(&event) && yaml_emitter_emit(&emitter, &event) &&
	     yaml_document_end_event_initialize(&event, 1) && yaml_emitter_emit(&emitter, &event) &&
	     yaml_stream_end_event_initialize(&event) && yaml_emitter_emit(&emitter, &event);
	if (!ok)
		BUG("libyaml cannot write a scalar: %s", emitter.problem ? emitter.problem : "event initialization failed");
	yaml_emitter_delete(&emitter);
	if (!starts_with(one.buf, "- - ") || !ends_with(one.buf, "\n"))
		BUG("libyaml scalar has no wrapper");
	if (ends_with(one.buf, "\n...\n"))
		strbuf_setlen(&one, one.len - 4);
	if (style == YAML_DOUBLE_QUOTED_SCALAR_STYLE)
		strbuf_setlen(&one, one.len - 1);
	strbuf_add(sb, one.buf + 4, one.len - 4);
	strbuf_release(&one);
}

void yaml_out_scalar(struct strbuf *sb, const char *s)
{
	yaml_scalar(sb, s, YAML_DOUBLE_QUOTED_SCALAR_STYLE);
}

void yaml_out_body(struct strbuf *sb, const char *s)
{
	int quote = non_lf(s) != NULL;

	/* YAML would emit LS/PS literally; ait's raw input remains LF-only. */
	yaml_scalar(sb, s, quote ? YAML_DOUBLE_QUOTED_SCALAR_STYLE : YAML_LITERAL_SCALAR_STYLE);
	if (quote)
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

int oid_ok(const char *s)
{
	return strlen(s) == 40 && !s[strspn(s, "0123456789abcdef")];
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
		{
			char *component = xstrndup(k + plen, strcspn(k + plen, "."));
			int known = one_of(component, keys);

			free(component);
			if (!known)
				return yaml_refusal(kv, k, "unknown key '%s'", k);
		}
	}
	for (; required && *keys; keys++) {
		char *key = pk(prefix, *keys), *why = NULL;

		if (!unsorted_string_list_has_string(kv, key))
			why = yaml_refusal(kv, key, "'%s' is missing", *keys);
		free(key);
		if (why)
			return why;
	}
	return NULL;
}

/* v14 2.5.2 without inputs: a spec under PREFIX of KV; NULL with ERR */
static struct spec *spec_read(struct string_list *kv, const char *prefix, char **err,
			      struct spec *s, struct string_list *cmd, const char *const *fields)
{
	static const char *const keys[] = { "body", "engine", "command", "success", "environment", NULL };
	struct string_list_item *item;
	const char *body_key = fields[0], *engine_key = fields[1],
		   *command_key = fields[2], *success_key = fields[3],
		   *environment_key = fields[4], *engine = yget(kv, engine_key),
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
	if (yget(kv, command_key) && (*err = yaml_list(kv, command_key, cmd)))
		return NULL;
	if (success && ykind(kv, success_key) != 'l')
		return bad(err, yaml_refusal(kv, success_key, "'%s' is not a list", success_key));
	if (success && (*err = yaml_list(kv, success_key, &s->success)))
		return NULL;
	if (s->engine && !success)
		string_list_append(&s->success, "0");
	for_each_string_list_item(item, cmd)
		strvec_push(&s->command, item->string);
	for_each_string_list_item(item, &s->success)
		if (!*item->string || item->string[strspn(item->string, "0123456789")] ||
		    (*item->string == '0' && item->string[1])) {
			char *key = xstrfmt("%s.%d", success_key, (int)(item - s->success.items));

			*err = yaml_refusal(kv, key, "'success' holds '%s', not a decimal integer", item->string);
			free(key);
			return NULL;
		}
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

struct spec *spec_from(struct string_list *kv, const char *prefix, char **err)
{
	struct spec *s = xcalloc(1, sizeof(*s)), *parsed;
	struct string_list cmd = STRING_LIST_INIT_DUP;
	const char *fields[] = { pk(prefix, "body"), pk(prefix, "engine"), pk(prefix, "command"),
				 pk(prefix, "success"), pk(prefix, "environment") };
	size_t i;

	parsed = spec_read(kv, prefix, err, s, &cmd, fields);
	string_list_clear(&cmd, 0);
	for (i = 0; i < ARRAY_SIZE(fields); i++)
		free((char *)fields[i]);
	if (!parsed) {
		spec_clear(s);
		free(s);
	}
	return parsed;
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
	char *id, *group;
	int valid;

	if (!ends_with(base, ".yaml"))
		return NULL;
	id = xstrndup(base, strlen(base) - 5);
	group = group_of(path);
	valid = id_ok(id, "DOX") && group_ok(group);
	free(group);
	if (valid)
		return id;
	free(id);
	return NULL;
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
	char *dir, *expected = NULL, *parent = NULL, *stem = NULL;
	int valid = 0;
	size_t id_len = strlen(id);

	if (!slash)
		return 0;
	dir = xstrndup(path, slash - path);
	if (group) {
		expected = xstrfmt("%s%sfulfill", group, *group ? "/" : "");
		if (strcmp(dir, expected))
			goto out;
	}
	if (strcmp(dir, "fulfill")) {
		if (!ends_with(dir, "/fulfill"))
			goto out;
		parent = xstrndup(dir, strlen(dir) - 8);
		if (!group_ok(parent))
			goto out;
	}
	if (strncmp(slash + 1, id, id_len) || slash[1 + id_len] != '-')
		goto out;
	rid = slash + 2 + id_len;
	if (!ends_with(rid, ".md"))
		goto out;
	stem = xstrndup(rid, strlen(rid) - 3);
	valid = id_ok(stem, "R");
out:
	free(dir);
	free(expected);
	free(parent);
	free(stem);
	return valid;
}

static struct contract *contract_read(const char *text, size_t len, const char *path, int draft, char **err,
				      struct string_list *kv, struct contract **owned)
{
	static const char *const states_do[] = { "future", "cancelled", "withdrawn", NULL };
	static const char *const states_x[] = { "future", "cancelled", NULL };
	struct string_list_item *item;
	struct contract *c;
	const char *const *keys, *v;
	char *id = contract_path_id(path);
	int i, demand, offer, drop;

	if (yaml_parse(text, len, kv, err)) {
		free(id);
		return NULL;
	}
	*err = NULL;
	if (!id)
		return bad(err, yaml_refusal(kv, NULL, "not a contract path"));
	v = yget(kv, "kind");
	if (!v || !(keys = keys_of(v)) || kind_letter(v) != *id) {
		free(id);
		return bad(err, yaml_refusal(kv, "kind", "'kind' is missing or does not match the ID"));
	}
	{
		char *group = group_of(path);

		c = contract_new(v, group, id);
		free(group);
		free(id);
	}
	*owned = c;
	demand = !strcmp(v, "demand");
	offer = !strcmp(v, "offer");
	drop = !strcmp(v, "drop");
	if ((*err = keys_refusal(kv, "", keys, 1)))
		return NULL;
	replace_text(&c->condition_id, yget(kv, "condition-id"));
	replace_text(&c->change_id, yget(kv, "change-id"));
	replace_text(&c->creator, yget(kv, "creator"));
	replace_text(&c->state, yget(kv, "state"));
	if ((ykind(kv, "condition-id") != 0 && ykind(kv, "condition-id") != '?') ||
	    (draft && !*c->condition_id ? 0 : !id_ok(c->condition_id, "R")))
		return bad(err, yaml_refusal(kv, "condition-id", "bad 'condition-id'"));
	if ((ykind(kv, "change-id") != 0 && ykind(kv, "change-id") != '?') ||
	    (draft && !*c->change_id ? 0 : !id_ok(c->change_id, "R")))
		return bad(err, yaml_refusal(kv, "change-id", "bad 'change-id'"));
	if ((ykind(kv, "creator") != 0 && ykind(kv, "creator") != '?') ||
	    (draft && !*c->creator ? 0 : !agent_ok(c->creator)))
		return bad(err, yaml_refusal(kv, "creator", "bad 'creator'"));
	if (!one_of(c->state, drop ? states_x : states_do))
		return bad(err, yaml_refusal(kv, "state", "bad 'state'"));
	for (i = !demand; !drop && i < 2; i++) {
		const char *field = i ? "provider" : "consumer";
		char *why = scopes_parse(i ? &c->provider : &c->consumer, yget(kv, field), 1, i,
					draft ? SCOPE_DRAFT : SCOPE_STORED);

		if (why) {
			*err = yaml_refusal(kv, field, "bad scope in '%s': %s", field, why);
			free(why);
			return NULL;
		}
	}
	if (!drop && (!c->provider.nr || (demand && !c->consumer.nr)))
		return bad(err, yaml_refusal(kv, !c->provider.nr ? "provider" : "consumer", "a scope list is empty"));
	if (drop && !id_ok(c->target = xstrdup(yget(kv, "target")), "DO"))
		return bad(err, yaml_refusal(kv, "target", "bad 'target'"));
	if (offer && ((*err = yaml_list(kv, "undertakes", &c->undertakes)) ||
		      (*err = yaml_list(kv, "undertakes-partially", &c->partially))))
		return NULL;
	for_each_string_list_item(item, &c->undertakes) {
		char key[64];

		snprintf(key, sizeof(key), "undertakes.%d", (int)(item - c->undertakes.items));
		if (!id_ok(item->string, "D") || unsorted_string_list_has_string(&c->partially, item->string))
			return bad(err, yaml_refusal(kv, key, "bad ID '%s' in 'undertakes'", item->string));
	}
	for_each_string_list_item(item, &c->partially) {
		char key[64];

		snprintf(key, sizeof(key), "undertakes-partially.%d", (int)(item - c->partially.items));
		if (!id_ok(item->string, "D"))
			return bad(err, yaml_refusal(kv, key, "bad ID '%s' in 'undertakes-partially'", item->string));
	}
	if ((*err = yaml_list(kv, "fulfilled", &c->fulfilled)))
		return NULL;
	for_each_string_list_item(item, &c->fulfilled) {
		char key[64];
		int *source_line;

		snprintf(key, sizeof(key), "fulfilled.%d", (int)(item - c->fulfilled.items));

		if (!fulfil_path_ok(item->string, c->id, c->group))
			return bad(err, yaml_refusal(kv, key, "bad path '%s' in 'fulfilled'", item->string));
		source_line = xmalloc(sizeof(*source_line));
		*source_line = yline(kv, key);
		item->util = source_line;
	}
	if (drop)
		return c;
	if (ykind(kv, "specs") != 'l' || !yget(kv, "specs.0"))
		return bad(err, yaml_refusal(kv, "specs", "'specs' is no list of specs"));
	for (i = 0; ; i++) {
		char key[64];
		struct spec *s;

		snprintf(key, sizeof(key), "specs.%d", i);
		if (!yget(kv, key))
			break;
		s = spec_from(kv, key, err);

		if (!s)
			return NULL;
		REALLOC_ARRAY(c->specs, c->nspecs + 1);
		c->specs[c->nspecs++] = *s;
		free(s);
	}
	return c;
}

struct contract *contract_parse(const char *text, size_t len, const char *path, int draft, char **err)
{
	struct string_list kv = STRING_LIST_INIT_DUP;
	struct contract *owned = NULL;
	struct contract *c = contract_read(text, len, path, draft, err, &kv, &owned);

	yaml_clear(&kv);
	if (!c)
		contract_free(owned);
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
	string_list_clear(&sorted, 0);
}

static void scope_line(struct strbuf *sb, const char *key, const struct scope_list *l)
{
	char *text = scopes_join(l);

	strbuf_addf(sb, "%s: ", key);
	yaml_out_scalar(sb, text);
	strbuf_addch(sb, '\n');
	free(text);
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
			yaml_out_body(&sb, s->body);
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
static struct recorded_code *parse_code(const struct scope *scope, const char *text)
{
	struct string_list fields = STRING_LIST_INIT_DUP;
	struct recorded_code *code = NULL;
	const char *mode, *type, *oid, *end;
	int first = 0, last = 0, range;

	range = scope->kind == SCOPE_RANGE;
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
static struct fulfilment *fulfil_read(const char *text, const char *path, char **err,
				     struct string_list *kv, struct fulfilment **owned)
{
	static const char *const keys[] = { "contract", "condition-id", "by", "session", "commit", "outcome", "code", NULL };
	struct string_list_item *item;
	struct fulfilment *f = xcalloc(1, sizeof(*f));
	const char *end, *invalid;
	char *front;

	*owned = f;
	*err = NULL;
	if (!starts_with(text, "---\n") || !(end = strstr(text + 3, "\n---\n")))
		return bad(err, yaml_refusal(kv, NULL, "no front matter"));
	front = xstrndup(text, end + 1 - text);
	if (yaml_parse(front, end + 1 - text, kv, err)) {
		free(front);
		return NULL;
	}
	free(front);
	if ((*err = keys_refusal(kv, "", keys, 1)))
		return NULL;
	f->path = xstrdup(path);
	f->contract = xstrdup(yget(kv, "contract"));
	f->condition_id = xstrdup(yget(kv, "condition-id"));
	f->by = xstrdup(yget(kv, "by"));
	f->session = xstrdup(yget(kv, "session"));
	f->commit = xstrdup(yget(kv, "commit"));
	f->outcome = xstrdup(yget(kv, "outcome"));
	f->body = xstrdup(end + 5);
	invalid = !id_ok(f->contract, "DOX") ? "contract" : !id_ok(f->condition_id, "R") ? "condition-id" :
		!agent_ok(f->by) ? "by" : !id_ok(f->session, "S") ? "session" : !oid_ok(f->commit) ? "commit" :
		!one_of(f->outcome, (const char *const[]){ "pass", "forced", "withdrawn", NULL }) ? "outcome" :
		ykind(kv, "code") != 'm' ? "code" : NULL;
	if (invalid)
		return bad(err, yaml_refusal(kv, invalid, "bad '%s' in the front matter", invalid));
	if (!fulfil_path_ok(path, f->contract, NULL))
		return bad(err, yaml_refusal(kv, NULL, "not a fulfilment path"));
	if ((*f->contract == 'X') != !strcmp(f->outcome, "withdrawn"))
		return bad(err, yaml_refusal(kv, "outcome", "'outcome' does not match the contract kind"));
	for_each_string_list_item(item, kv) {
		const char *scope;

		if (skip_prefix(item->string, "code.", &scope)) {
			char *unescaped = yaml_key_decode(scope), *scope_error;
			struct recorded_code *code;
			struct scope key;

			if (((struct yval *)item->util)->kind) {
				free(unescaped);
				return bad(err, yaml_refusal(kv, item->string, "a code entry is not a string"));
			}
			scope_error = scope_parse(&key, unescaped, 0, 0, SCOPE_STORED);
			code = scope_error ? NULL : parse_code(&key, ((struct yval *)item->util)->s);
			if (!code) {
				char *why = yaml_refusal(kv, item->string, "bad code entry for '%s'", unescaped);

				free(scope_error);
				scope_clear(&key);
				free(unescaped);
				return bad(err, why);
			}
			code_add(&f->code, &key, code);
			scope_clear(&key);
			free(code);
			free(unescaped);
		}
	}
	if (*f->contract == 'X' && f->code.nr)
		return bad(err, yaml_refusal(kv, "code", "a drop's code is not empty"));
	return f;
}

static struct fulfilment *fulfil_parse(const char *text, const char *path, char **err)
{
	struct string_list kv = STRING_LIST_INIT_DUP;
	struct fulfilment *owned = NULL;
	struct fulfilment *f = fulfil_read(text, path, err, &kv, &owned);

	yaml_clear(&kv);
	if (!f)
		fulfilment_free(owned);
	return f;
}

char *fulfil_text(struct fulfilment *f)
{
	struct strbuf sb = STRBUF_INIT;
	size_t i;

	strbuf_addf(&sb, "---\ncontract: %s\ncondition-id: %s\nby: %s\nsession: %s\ncommit: %s\noutcome: %s\ncode:%s\n",
		    f->contract, f->condition_id, f->by, f->session, f->commit, f->outcome, f->code.nr ? "" : " {}");
	for (i = 0; i < f->code.nr; i++) {
		char *key = scope_text(&f->code.items[i].scope, 1);
		char *value = code_text(&f->code.items[i].value);

		strbuf_addstr(&sb, "  ");
		yaml_out_scalar(&sb, key);
		strbuf_addstr(&sb, ": ");
		yaml_out_scalar(&sb, value);
		strbuf_addch(&sb, '\n');
		free(key);
		free(value);
	}
	strbuf_addf(&sb, "---\n%s", f->body);
	strbuf_complete_line(&sb);
	return strbuf_detach(&sb, NULL);
}

/*
 * spec 2.1.3, 2.3, 2.5: the contracts and fulfilments of the branch NAME at
 * TIP (or of the tree TREEISH); each malformed file is one error
 */
static void branch_error(struct branch *b, const char *path, char *reason)
{
	struct string_list_item *slot = string_list_insert(&b->errors, path);

	free(slot->util);
	slot->util = reason;
}

struct branch *branch_load(const char *name, const char *tip, const char *treeish)
{
	struct string_list_item *item;
	struct string_list invalid = STRING_LIST_INIT_DUP;
	struct branch *b;
	char oid[48];
	size_t i;

	if (!treeish && !tip)
		tip = branch_tip(name, oid) ? oid : NULL;
	if (!treeish)
		treeish = tip ? tip : EMPTY_TREE;
	b = xcalloc(1, sizeof(*b));
	b->name = xstrdup(name);
	snprintf(b->tip, sizeof(b->tip), "%s", tip ? tip : "");
	b->tree = tree_load(treeish);
	string_list_init_dup(&b->contracts);
	string_list_init_dup(&b->fulfils);
	string_list_init_dup(&b->errors);
	for (i = 0; i < b->tree->nr; i++) {
		struct entry *e = &b->tree->e[i];
		char *text = NULL;
		const char *nul;
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

			if (c && string_list_lookup(&b->contracts, c->id)) {
				err = yaml_refusal_at(1, "'%s' is also at '%s'", c->id,
						      ((struct contract *)string_list_lookup(&b->contracts, c->id)->util)->path);
				contract_free(c);
			} else if (c)
				string_list_insert(&b->contracts, c->id)->util = c;
		}
		free(text);
		if (err)
			branch_error(b, e->path, err);
	}
	for_each_string_list_item(item, &b->contracts) {
		struct contract *c = item->util;
		struct string_list_item *f;

		for_each_string_list_item(f, &c->fulfilled) {
			struct string_list_item *got = string_list_lookup(&b->fulfils, f->string);

			if (!got || strcmp(((struct fulfilment *)got->util)->contract, c->id)) {
				branch_error(b, c->path,
					yaml_refusal_at(*(int *)f->util, "'%s' is not a fulfilment of '%s'", f->string, c->id));
				string_list_insert(&invalid, c->id);
			}
		}
	}
	for_each_string_list_item(item, &invalid) {
		contract_free(contract_get(b, item->string));
		string_list_remove(&b->contracts, item->string, 0);
	}
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
struct fulfilment *fulfilment_lookup(const struct contract_view *v, const char *path)
{
	struct string_list_item *item = v->fulfils ? string_list_lookup(v->fulfils, path) : NULL;

	if (!item)
		item = string_list_lookup(&v->at->fulfils, path);
	return item ? item->util : NULL;
}

static void valid_fulfilments(const struct contract_view *v, struct contract *c, struct string_list *out)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &c->fulfilled) {
		struct fulfilment *f = fulfilment_lookup(v, item->string);

		if (f && !strcmp(f->condition_id, c->condition_id))
			string_list_append(out, item->string)->util = f;
	}
}

int has_valid_fulfilment(struct branch *b, struct contract *c)
{
	struct string_list v = STRING_LIST_INIT_DUP;
	int found;

	{
		struct contract_view view = { b, NULL, NULL };

		valid_fulfilments(&view, c, &v);
	}
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

struct blob_hunk {
	int old_first;
	int old_length;
	int new_first;
	int new_length;
};
struct blob_diff {
	char base[48];
	char now[48];
	struct blob_hunk *hunks;
	size_t nr;
	size_t alloc;
};

void diff_cache_clear(struct diff_cache *cache)
{
	size_t i;

	for (i = 0; i < cache->nr; i++)
		free(cache->items[i].hunks);
	free(cache->items);
	memset(cache, 0, sizeof(*cache));
}

/* A hunk list belongs to this ordered blob pair, independent of scope coordinates. */
static struct blob_diff *blob_diff_get(struct diff_cache *cache, const char *base, const char *now)
{
	struct string_list lines = STRING_LIST_INIT_DUP;
	struct string_list_item *line;
	struct blob_diff *diff;
	char *text;
	size_t i;

	for (i = 0; i < cache->nr; i++)
		if (!strcmp(cache->items[i].base, base) && !strcmp(cache->items[i].now, now))
			return &cache->items[i];
	ALLOC_GROW(cache->items, cache->nr + 1, cache->alloc);
	diff = &cache->items[cache->nr++];
	memset(diff, 0, sizeof(*diff));
	snprintf(diff->base, sizeof(diff->base), "%s", base);
	snprintf(diff->now, sizeof(diff->now), "%s", now);
	text = diff0(base, now);
	string_list_split(&lines, text, "\n", -1);
	free(text);
	for_each_string_list_item(line, &lines) {
		const char *new_part;
		struct blob_hunk *hunk;

		if (!starts_with(line->string, "@@ -") || !(new_part = strstr(line->string, " +")))
			continue;
		ALLOC_GROW(diff->hunks, diff->nr + 1, diff->alloc);
		hunk = &diff->hunks[diff->nr++];
		hunk_part(line->string + 4, &hunk->old_first, &hunk->old_length);
		hunk_part(new_part + 2, &hunk->new_first, &hunk->new_length);
	}
	string_list_clear(&lines, 0);
	return diff;
}

/* v14 3.7/3.10: map coordinates through the shared, ordered hunk list. */
static enum scope_status map_blobs(const char *base, const char *now, int a, int b,
				   int *first, int *end, int *changed, struct diff_cache *cache)
{
	struct blob_diff *diff;
	int ma = -1, mb = -1, offset = 0;
	char *type;
	size_t i;

	*changed = 0;
	if (!now)
		return MISSING;
	type = base ? object(base, 0, NULL, NULL) : NULL;
	if (!type || strcmp(type, "blob")) {
		free(type);
		return NOBASE;
	}
	free(type);
	if (!strcmp(base, now)) {
		*first = a;
		*end = b;
		return FOUND;
	}
	diff = blob_diff_get(cache, base, now);
	for (i = 0; i < diff->nr; i++) {
		const struct blob_hunk *hunk = &diff->hunks[i];
		int os = hunk->old_first, ol = hunk->old_length;
		int ns = hunk->new_first, nl = hunk->new_length;

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

static enum scope_status map_range(const struct scope *scope, struct tree *t, int *first, int *end, int *changed, struct diff_cache *cache)
{
	return map_blobs(*scope->base ? scope->base : NULL, blob_at(t, scope->path),
			 scope->first, scope->last, first, end, changed, cache);
}

static int line_count(const char *text, size_t n)
{
	size_t i, lf = 0;

	for (i = 0; i < n; i++)
		lf += text[i] == '\n';
	return lf + (n && text[n - 1] != '\n');
}

/* spec 3.2 (v14 3.3): a scope argument fixed at T, a line range taking T's blob; NULL with ERR */
int scope_pin(struct scope *s, struct tree *t, char **err)
{
	struct entry *e;

	*err = NULL;
	if (s->kind == SCOPE_ROOT)
		return 0;
	if (s->kind == SCOPE_NEW) {
		if (tree_get(t, s->path))
			*err = xstrfmt("'%s' exists", s->path);
		return *err ? -1 : 0;
	}
	if (s->kind == SCOPE_DIR) {
		char *dir = xstrndup(s->path, strlen(s->path) - 1);

		e = tree_get(t, dir);
		free(dir);
		if (!e || strcmp(e->type, "tree"))
			*err = xstrfmt("'%s' is not a directory", s->path);
		return *err ? -1 : 0;
	}
	/* File scopes include gitlinks; line ranges require a text blob. */
	e = tree_get(t, s->path);
	if (!e || !strcmp(e->type, "tree")) {
		*err = xstrfmt("'%s' is not a file", s->path);
		return -1;
	}
	if (s->kind == SCOPE_RANGE) {
		size_t n;
		const char *oid = blob_at(t, s->path);
		char *text = oid ? object(oid, 1, NULL, &n) : NULL;
		int valid = text && strlen(text) == n && s->last <= line_count(text, n);

		free(text);
		if (!valid) {
			char *range = scope_text(s, 0);

			*err = xstrfmt("'%s' is not a range of '%s'", range, s->path);
			free(range);
			return -1;
		}
		snprintf(s->base, sizeof(s->base), "%s", oid);
	}
	return 0;
}

/* Draft input tuples keep their old pin; only new tuples read the code tree. */
int scopes_pin(struct scope_list *scopes, struct tree *t, const struct scope_list *was, char **err)
{
	struct scope *s;
	const struct scope *old;

	*err = NULL;
	for_each_scope(s, scopes) {
		int retained = 0;

		if (was)
			for_each_scope(old, was)
				if (scope_equal(s, old, 0)) {
					snprintf(s->base, sizeof(s->base), "%s", old->base);
					retained = 1;
					break;
				}
		if (!retained && scope_pin(s, t, err))
			return -1;
	}
	return 0;
}

/*
 * spec 6.7.4, v14 3.8: where SCOPE is in T, as one judgment its callers word:
 * FOUND with its path and mapped line endpoints, or the reason it is not
 */
static enum scope_status scope_locate_in(const struct scope *scope, struct tree *t, struct scope_location *where,
					 struct diff_cache *cache)
{
	struct entry *e = tree_get(t, scope->path);
	int changed;

	where->path = scope->path;
	where->first = where->last = 0;
	if (scope->kind == SCOPE_NEW || scope->kind == SCOPE_DIR || scope->kind == SCOPE_ROOT)
		return ABSTRACT;
	if (scope->kind != SCOPE_RANGE)
		return e && strcmp(e->type, "tree") ? FOUND : MISSING;
	return map_range(scope, t, &where->first, &where->last, &changed, cache);
}

enum scope_status scope_locate(const struct scope *scope, struct tree *t, struct scope_location *where)
{
	struct diff_cache diffs = { 0 };
	enum scope_status status = scope_locate_in(scope, t, where, &diffs);

	diff_cache_clear(&diffs);
	return status;
}

void scope_result_at(const struct scope *s, struct tree *t, struct diff_cache *diffs, struct scope_result *out)
{
	out->status = scope_locate_in(s, t, &out->at, diffs);
	out->exists = tree_get(t, s->path) != NULL;
}

/* spec 2.5: read the entry at a position already validated FOUND in this tree. */
struct recorded_code code_at(struct tree *t, const struct scope_location *where)
{
	struct entry *e = tree_get(t, where->path);
	struct recorded_code code;

	snprintf(code.mode, sizeof(code.mode), "%s", e->mode);
	snprintf(code.type, sizeof(code.type), "%s", e->type);
	snprintf(code.oid, sizeof(code.oid), "%s", e->oid);
	code.first = where->first;
	code.last = where->last;
	return code;
}

/* spec 5.3: the scope is in X as recorded; AT receives that verified entry in X. */
static int scope_same(const struct scope *scope, const struct recorded_code *code, struct tree *x, struct recorded_code *at, struct diff_cache *cache)
{
	struct entry *e = tree_get(x, scope->path);
	int first = 0, last = 0, changed;

	if (!code || !e || strcmp(e->mode, code->mode) || strcmp(e->type, code->type))
		return 0;
	if (code->first) {
		if (map_blobs(code->oid, e->oid, code->first, code->last, &first, &last, &changed, cache) != FOUND || changed)
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
static int recorded_same(struct fulfilment *f, const struct scope *scope, struct tree *x, struct recorded_code *at, struct diff_cache *cache)
{
	const struct recorded_code *code = code_lookup(&f->code, scope);

	return code && scope_same(scope, code, x, at, cache);
}

/* v14 3.8: formatting reads a prepared position and cannot invoke Git. */
char *scope_result_text(const struct scope *scope, const struct scope_result *at)
{
	static const char *const marks[] = { [NOBASE] = "?nobase", [MISSING] = "?missing", [DELETED] = "?deleted" };
	char *bare, *display;

	if (scope->kind == SCOPE_NEW)
		return xstrfmt("new:%s%s", scope->path, at->exists ? " ?exists" : "");
	if (at->status == ABSTRACT)
		return scope_text(scope, 0);
	if (at->status == FOUND)
		return at->at.first ? xstrfmt("%s:%d-%d", at->at.path, at->at.first, at->at.last) : xstrdup(at->at.path);
	bare = scope_text(scope, 0);
	display = xstrfmt("%s %s", bare, marks[at->status]);
	free(bare);
	return display;
}

char *scope_display(const struct scope *scope, struct tree *x)
{
	struct diff_cache diffs = { 0 };
	struct scope_result at;
	char *display;

	scope_result_at(scope, x, &diffs, &at);
	display = scope_result_text(scope, &at);
	diff_cache_clear(&diffs);
	return display;
}

char *scopes_bare(const struct scope_list *l)
{
	struct strbuf out = STRBUF_INIT;
	const struct scope **ordered = scope_order(l);
	size_t i;

	for (i = 0; i < l->nr; i++) {
		char *text = scope_text(ordered[i], 0);

		strbuf_addf(&out, "%s%s", i ? "," : "", text);
		free(text);
	}
	free(ordered);
	return strbuf_detach(&out, NULL);
}

char *scopes_display(const struct scope_list *l, struct tree *x)
{
	struct strbuf out = STRBUF_INIT;
	struct diff_cache diffs = { 0 };
	const struct scope **ordered = scope_order(l);
	size_t i;

	for (i = 0; i < l->nr; i++) {
		struct scope_result at;
		char *text;

		scope_result_at(ordered[i], x, &diffs, &at);
		text = scope_result_text(ordered[i], &at);

		strbuf_addf(&out, "%s%s", i ? "," : "", text);
		free(text);
	}
	diff_cache_clear(&diffs);
	free(ordered);
	return strbuf_detach(&out, NULL);
}

void geometry_init(struct contract_geometry *g, const struct contract *c, struct tree *t, struct diff_cache *diffs)
{
	size_t i;

	memset(g, 0, sizeof(*g));
	g->contract = c;
	g->consumer = xcalloc(c->consumer.nr, sizeof(*g->consumer));
	g->provider = xcalloc(c->provider.nr, sizeof(*g->provider));
	for (i = 0; i < c->consumer.nr; i++)
		scope_result_at(&c->consumer.items[i], t, diffs, &g->consumer[i]);
	for (i = 0; i < c->provider.nr; i++)
		scope_result_at(&c->provider.items[i], t, diffs, &g->provider[i]);
}

void geometry_clear(struct contract_geometry *g)
{
	free(g->consumer);
	free(g->provider);
	memset(g, 0, sizeof(*g));
}

/* Missing/collected/deleted pins overlap their whole file, as v14 3.9 requires. */
static int positions_overlap(const struct scope *q, const struct scope_result *qa,
			     const struct scope *s, const struct scope_result *sa)
{
	int a = 0, b = 0, c = 0, d = 0;

	if (q->kind == SCOPE_ROOT || s->kind == SCOPE_ROOT)
		return 1;
	if (q->kind == SCOPE_DIR || s->kind == SCOPE_DIR)
		return (q->kind == SCOPE_DIR && starts_with(s->path, q->path)) ||
		       (s->kind == SCOPE_DIR && starts_with(q->path, s->path));
	if (strcmp(q->path, s->path))
		return 0;
	if (q->kind == SCOPE_RANGE && (!*q->base || qa->status == FOUND)) {
		a = *q->base ? qa->at.first : q->first;
		b = *q->base ? qa->at.last : q->last;
	}
	if (s->kind == SCOPE_RANGE && (!*s->base || sa->status == FOUND)) {
		c = *s->base ? sa->at.first : s->first;
		d = *s->base ? sa->at.last : s->last;
	}
	return !a || !c || (a <= d && c <= b);
}

int geometry_overlaps(const struct contract_geometry *g, const struct scope *q, const struct scope_result *at, int depending)
{
	const struct contract *c = g->contract;
	size_t i;

	for (i = 0; i < c->consumer.nr; i++)
		if (positions_overlap(q, at, &c->consumer.items[i], &g->consumer[i]))
			return 1;
	for (i = 0; i < c->provider.nr; i++) {
		const struct scope *s = &c->provider.items[i];

		if ((!depending || (s->kind != SCOPE_DIR && s->kind != SCOPE_ROOT) ||
		     q->kind == SCOPE_DIR || q->kind == SCOPE_ROOT) && positions_overlap(q, at, s, &g->provider[i]))
			return 1;
	}
	return 0;
}

int contract_overlaps(struct contract *c, const struct scope *q, struct tree *t, int depending)
{
	struct diff_cache diffs = { 0 };
	struct contract_geometry geometry;
	struct scope_result at;
	int overlaps;

	geometry_init(&geometry, c, t, &diffs);
	scope_result_at(q, t, &diffs, &at);
	overlaps = geometry_overlaps(&geometry, q, &at, depending);
	geometry_clear(&geometry);
	diff_cache_clear(&diffs);
	return overlaps;
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

/* One completed result per contract; a back edge is never a completed result. */
void judgment_init(struct judgment *j, const struct contract_view *view, struct tree *t, struct diff_cache *diffs)
{
	memset(j, 0, sizeof(*j));
	j->view = *view;
	j->tree = t;
	j->diffs = diffs;
	string_list_init_dup(&j->done);
	string_list_init_dup(&j->visiting);
}

const struct judgment_result *judgment_get(struct judgment *j, struct contract *c)
{
	struct string_list_item *hit = string_list_lookup(&j->done, c->id), *item, *f;
	struct string_list valid = STRING_LIST_INIT_DUP, needs = STRING_LIST_INIT_DUP;
	struct judgment_result *r;
	int whole_ok = 1;
	size_t i;

	if (hit)
		return hit->util;
	if (string_list_has_string(&j->visiting, c->id))
		return NULL;
	r = xcalloc(1, sizeof(*r));
	geometry_init(&r->geometry, c, j->tree, j->diffs);
	r->matches_any = xcalloc(c->provider.nr, sizeof(*r->matches_any));
	if (!strcmp(c->state, "cancelled")) {
		r->verdict = V_CANCELLED;
		goto done;
	}
	if (!strcmp(c->state, "withdrawn")) {
		r->verdict = V_WITHDRAWN;
		goto done;
	}
	if (!strcmp(c->kind, "drop")) {
		r->verdict = c->fulfilled.nr ? V_FULFILLED : V_FUTURE;
		goto done;
	}
	valid_fulfilments(&j->view, c, &valid);
	if (!valid.nr) {
		r->verdict = V_FUTURE;
		goto done;
	}
	string_list_insert(&j->visiting, c->id);
	dependencies(&j->view, c, &needs);
	for_each_string_list_item(item, &needs) {
		const struct judgment_result *dependency = judgment_get(j, item->util);

		if (!dependency || dependency->verdict != V_CURRENT)
			whole_ok = 0;
	}
	string_list_remove(&j->visiting, c->id, 0);
	/* Per-scope matches can come from different fulfilments; a whole witness cannot. */
	for_each_string_list_item(f, &valid) {
		struct code_list code = { 0 };
		int all = 1;

		for (i = 0; i < c->provider.nr; i++) {
			const struct scope *s = &c->provider.items[i];
			struct recorded_code at;
			int same = recorded_same(f->util, s, j->tree, &at, j->diffs);

			r->matches_any[i] |= same;
			all &= same;
			if (same)
				code_add(&code, s, &at);
		}
		if (all) {
			r->code = code;
			r->witness = f->util;
			break;
		}
		code_clear(&code);
	}
	r->verdict = whole_ok && r->witness ? V_CURRENT : V_UNVERIFIED;
done:
	string_list_clear(&valid, 0);
	string_list_clear(&needs, 0);
	string_list_insert(&j->done, c->id)->util = r;
	return r;
}

void judgment_clear(struct judgment *j)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &j->done) {
		struct judgment_result *r = item->util;

		geometry_clear(&r->geometry);
		code_clear(&r->code);
		free(r->matches_any);
		free(r);
	}
	string_list_clear(&j->done, 0);
	string_list_clear(&j->visiting, 0);
}

enum verdict judge_view(const struct contract_view *v, struct contract *c, struct tree *x)
{
	struct diff_cache diffs = { 0 };
	struct judgment j;
	enum verdict verdict;

	judgment_init(&j, v, x, &diffs);
	verdict = judgment_get(&j, c)->verdict;
	judgment_clear(&j);
	diff_cache_clear(&diffs);
	return verdict;
}

enum verdict judge(struct branch *b, struct contract *c, struct tree *x)
{
	struct contract_view view = { b, NULL, NULL };

	return judge_view(&view, c, x);
}

/* spec 1.5: a promise is a live contract judged future or current ?unverified */
int promise(struct contract *c, enum verdict v)
{
	return live(c) && (v == V_FUTURE || v == V_UNVERIFIED);
}

const char *verdict_word(enum verdict v)
{
	static const char *const words[] = { "future", "current", "current ?unverified", "fulfilled", "cancelled", "withdrawn" };

	return words[v];
}

/* Printing consumes positions and matches; it cannot invoke Git or judge again. */
static char *geometry_display(const struct contract_geometry *g, int provider, const unsigned char *matches)
{
	const struct contract *c = g->contract;
	const struct scope_list *scopes = provider ? &c->provider : &c->consumer;
	const struct scope_result *positions = provider ? g->provider : g->consumer;
	const struct scope **ordered = scope_order(scopes);
	struct strbuf out = STRBUF_INIT;
	size_t i;

	for (i = 0; i < scopes->nr; i++) {
		size_t n = ordered[i] - scopes->items;
		char *display = scope_result_text(ordered[i], &positions[n]);
		int changed = matches && !matches[n] && !strchr(display, '?');

		strbuf_addf(&out, "%s%s%s", i ? "," : "", display, changed ? " ?changed" : "");
		free(display);
	}
	free(ordered);
	return strbuf_detach(&out, NULL);
}

char *geometry_scopes(const struct contract_geometry *g, int provider)
{
	return geometry_display(g, provider, NULL);
}

char *judgment_scopes(const struct judgment_result *r, int provider)
{
	const unsigned char *matches = provider && (r->verdict == V_CURRENT || r->verdict == V_UNVERIFIED) ? r->matches_any : NULL;

	return geometry_display(&r->geometry, provider, matches);
}

char *provider_display(struct branch *b, struct contract *c, struct tree *x)
{
	struct contract_view view = { b, NULL, NULL };
	struct diff_cache diffs = { 0 };
	struct judgment j;
	char *text;

	judgment_init(&j, &view, x, &diffs);
	text = judgment_scopes(judgment_get(&j, c), 1);
	judgment_clear(&j);
	diff_cache_clear(&diffs);
	return text;
}

/* Compare X with the single whole witness mapped to H, even when X is also current. */
int judgment_touched(const struct judgment_result *h, struct tree *x, struct diff_cache *diffs)
{
	size_t i;

	if (h->verdict != V_CURRENT)
		return 0;
	for (i = 0; i < h->code.nr; i++)
		if (!scope_same(&h->code.items[i].scope, &h->code.items[i].value, x, NULL, diffs))
			return 1;
	return 0;
}

int touched(struct branch *b, struct contract *c, struct tree *h, struct tree *x)
{
	struct contract_view view = { b, NULL, NULL };
	struct diff_cache diffs = { 0 };
	struct judgment j;
	int changed;

	judgment_init(&j, &view, h, &diffs);
	changed = judgment_touched(judgment_get(&j, c), x, &diffs);
	judgment_clear(&j);
	diff_cache_clear(&diffs);
	return changed;
}

void violations_clear(struct string_list *violations)
{
	struct string_list_item *item;

	for_each_string_list_item(item, violations) {
		struct violation *v = item->util;

		free((char *)v->a);
		free((char *)v->b);
		free(v);
	}
	string_list_clear(violations, 0);
}

/* spec 4.10: one violation under its key, which names it across trees */
static void violation(struct string_list *out, enum refusal kind, const char *a, const char *b)
{
	struct violation *v = xcalloc(1, sizeof(*v));
	char *key;

	v->kind = kind;
	v->a = xstrdup(a);
	v->b = xstrdup(b);
	key = kind == PAIR ? xstrfmt("pair:%s:%s", a, b) : xstrfmt("mix:%s", a);
	string_list_insert(out, key)->util = v;
	free(key);
}

/* spec 4.8: the words of a violation, chosen by its kind */
char *violation_words(struct violation *v)
{
	return refusal_words(v->kind, v->a, v->b, NULL);
}

/* Step zero checks the requested fulfilments before their files are written. */
static int fulfilled_in(const struct contract_view *v, struct contract *c, const struct string_list *fulfilling)
{
	struct string_list valid = STRING_LIST_INIT_DUP;
	int found;

	if (fulfilling && string_list_has_string(fulfilling, c->id))
		return 1;
	valid_fulfilments(v, c, &valid);
	found = valid.nr > 0;
	string_list_clear(&valid, 0);
	return found;
}

/*
 * spec 4.10 (b) and (c): the violations of B. A mix is told from WAS:
 * what held the demand there is what it is "already" held by.
 */
void invariants_view(const struct contract_view *v, struct branch *was, const struct string_list *fulfilling, struct string_list *out)
{
	struct contract_view old = { was, NULL, NULL };
	struct string_list contracts = STRING_LIST_INIT_DUP;
	struct string_list_item *item, *o;

	for_each_string_list_item(item, &v->at->contracts)
		string_list_insert(&contracts, item->string)->util = contract_lookup(v, item->string);
	if (v->changed)
		for_each_string_list_item(item, v->changed)
			string_list_insert(&contracts, item->string)->util = item->util;
	for_each_string_list_item(item, &contracts) {
		struct contract *d = item->util;
		struct string_list whole = STRING_LIST_INIT_DUP, part = STRING_LIST_INIT_DUP, before = STRING_LIST_INIT_DUP;

		if (!live(d) || strcmp(d->kind, "demand"))
			continue;
		undertakings(v, d, 'w', &whole);
		undertakings(v, d, 'p', &part);
		if (whole.nr && part.nr) {
			struct contract *previous = contract_get(was, d->id);

			if (previous)
				undertakings(&old, previous, 'w', &before);
			violation(out, before.nr ? MIX_WHOLLY : MIX_PARTIALLY, d->id,
				  before.nr ? before.items[0].string : part.items[0].string);
		}
		/* 1.8: what leans on an unfulfilled contract may not stand fulfilled on it */
		for_each_string_list_item(o, &whole)
			if (fulfilled_in(v, o->util, fulfilling) && !fulfilled_in(v, d, fulfilling))
				violation(out, PAIR, o->string, d->id);
		for_each_string_list_item(o, &part)
			if (fulfilled_in(v, d, fulfilling) && !fulfilled_in(v, o->util, fulfilling))
				violation(out, PAIR, d->id, o->string);
		string_list_clear(&whole, 0);
		string_list_clear(&part, 0);
		string_list_clear(&before, 0);
	}
	string_list_clear(&contracts, 0);
}

void invariants(struct branch *b, struct branch *was, const struct string_list *fulfilling, struct string_list *out)
{
	struct contract_view view = { b, NULL, NULL };

	invariants_view(&view, was, fulfilling, out);
}
