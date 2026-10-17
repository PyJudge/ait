/*
 * Trees, contracts, fulfilments and what is judged from them (spec 2, 3, 5).
 * Contracts and fulfilments are stored facts; the rest is computed (rule 1).
 */
#include "ait.h"
#include <yaml.h>

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

/* the YAML subset of v14 2.5.1: libyaml's events composed into nodes */
#define YAML_DEPTH 16

void ynode_free(struct ynode *n)
{
	size_t i;

	if (!n)
		return;
	for (i = 0; i < n->nr; i++)
		ynode_free(n->kids[i]);
	free(n->kids);
	free(n->value);
	free(n);
}

static const char *decorated(const yaml_char_t *anchor, const yaml_char_t *tag)
{
	return anchor ? "an anchor" : tag ? "a tag" : NULL;
}

/* what v14 2.5.1 leaves out of the subset */
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

/*
 * As libyaml's loader.c composes (yaml_parser_load_node_add): the node of
 * event E joins the open node on STACK; a map holds key and value nodes in
 * turn, so an odd count waits for a value. The node takes the event's
 * scalar text. The root must be a map, and keys plain or quoted scalars
 * that a map holds once. NULL, or why not.
 */
static const char *yaml_add(struct ynode **stack, int *depth, struct ynode **root, yaml_event_t *e,
			    struct strbuf *reason)
{
	struct ynode *parent = *depth ? stack[*depth - 1] : NULL, *n;
	int key = parent && parent->kind == 'm' && !(parent->nr % 2);
	size_t i;

	if (e->type == YAML_SCALAR_EVENT && strlen((char *)e->data.scalar.value) != e->data.scalar.length)
		return "a NUL character";
	if (!parent && e->type != YAML_MAPPING_START_EVENT)
		return "no key";
	if (key && (e->type != YAML_SCALAR_EVENT || e->data.scalar.style == YAML_LITERAL_SCALAR_STYLE))
		return "a key that is not a scalar";
	for (i = 0; key && i < parent->nr; i += 2)
		if (!strcmp(parent->kids[i]->value, (char *)e->data.scalar.value)) {
			strbuf_addf(reason, "duplicate key '%s'", parent->kids[i]->value);
			return reason->buf;
		}
	n = xcalloc(1, sizeof(*n));
	n->line = e->start_mark.line + 1;
	if (e->type == YAML_MAPPING_START_EVENT) {
		n->kind = 'm';
	} else if (e->type == YAML_SEQUENCE_START_EVENT) {
		n->kind = 'l';
	} else {
		n->value = (char *)e->data.scalar.value;
		e->data.scalar.value = NULL;
		n->style = e->data.scalar.style == YAML_PLAIN_SCALAR_STYLE ? 'p' : '"';
		n->kind = n->style == 'p' && !*n->value ? '?' : 's';
	}
	if (!parent) {
		*root = n;
	} else {
		REALLOC_ARRAY(parent->kids, parent->nr + 1);
		parent->kids[parent->nr++] = n;
	}
	if (n->kind == 's' || n->kind == '?')
		return NULL;
	if (*depth == YAML_DEPTH)
		return "nested too deeply";
	stack[(*depth)++] = n;
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

/* the map TEXT holds (an empty one for no document), or NULL with ERR */
struct ynode *yaml_parse(const char *text, size_t len, char **err)
{
	struct ynode *stack[YAML_DEPTH] = { NULL }, *root = NULL;
	struct strbuf reason = STRBUF_INIT;
	yaml_parser_t parser;
	yaml_event_t e;
	const char *why = NULL, *nul = memchr(text, 0, len);
	int depth = 0, docs = 0, done = 0;
	size_t line = 0;

	*err = NULL;
	if (nul) {
		*err = xstrfmt("a NUL character in line %d", (int)line_at(text, nul - text) + 1);
		return NULL;
	}
	if ((why = non_lf(text))) {
		*err = xstrfmt("a line break other than LF in line %d", (int)line_at(text, why - text) + 1);
		return NULL;
	}
	yaml_parser_initialize(&parser);
	yaml_parser_set_encoding(&parser, YAML_UTF8_ENCODING);
	yaml_parser_set_input_string(&parser, (const unsigned char *)text, len);
	while (!done && !why && yaml_parser_parse(&parser, &e)) {
		line = e.start_mark.line;
		docs += e.type == YAML_DOCUMENT_START_EVENT;
		if (!(why = yaml_outside(&e, docs > 1))) {
			if (e.type == YAML_MAPPING_END_EVENT || e.type == YAML_SEQUENCE_END_EVENT)
				depth--;
			else if (e.type == YAML_SCALAR_EVENT || e.type == YAML_MAPPING_START_EVENT ||
				 e.type == YAML_SEQUENCE_START_EVENT)
				why = yaml_add(stack, &depth, &root, &e, &reason);
		}
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
	if (why) {
		ynode_free(root);
		return NULL;
	}
	if (!root) {
		root = xcalloc(1, sizeof(*root));
		root->kind = 'm';
		root->line = 1;
	}
	return root;
}

/* the value of KEY in the map MAP, or NULL */
struct ynode *yget(struct ynode *map, const char *key)
{
	size_t i;

	for (i = 0; map && map->kind == 'm' && i + 1 < map->nr; i += 2)
		if (!strcmp(map->kids[i]->value, key))
			return map->kids[i + 1];
	return NULL;
}

/* N's text when N is a string; YAML's null (an empty plain value) is none */
const char *ystr(struct ynode *n)
{
	return n && n->kind == 's' ? n->value : NULL;
}

/* N's text when N is a scalar, null as "": where the spec lets a value be empty */
const char *ytext(struct ynode *n)
{
	return n && (n->kind == 's' || n->kind == '?') ? n->value : NULL;
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

/* why the node N (a missing node's map) is refused, at N's line */
char *yaml_refusal(struct ynode *n, const char *fmt, ...)
{
	char *why;
	va_list ap;

	va_start(ap, fmt);
	why = yaml_vrefusal(n ? n->line : 1, fmt, ap);
	va_end(ap);
	return why;
}

/*
 * the strings of the list N, named NAME, into OUT (when given); NONEMPTY
 * wants one at least. OUT is filled only when the whole list is valid.
 */
char *yaml_list(struct ynode *n, const char *name, struct string_list *out, int nonempty)
{
	size_t i;

	if (!n || n->kind != 'l' || (nonempty && !n->nr))
		return yaml_refusal(n, "'%s' is not a %slist of strings", name, nonempty ? "nonempty " : "");
	for (i = 0; i < n->nr; i++)
		if (!ystr(n->kids[i]))
			return yaml_refusal(n->kids[i], "an item of '%s' is not a string", name);
	for (i = 0; out && i < n->nr; i++)
		string_list_append(out, n->kids[i]->value);
	return NULL;
}

static int yaml_out(void *sb, unsigned char *buf, size_t n)
{
	strbuf_add(sb, buf, n);
	return 1;
}

/* N's events; a map's keys are written as they were read */
static int yaml_node_events(yaml_emitter_t *emitter, struct ynode *n)
{
	yaml_event_t event;
	size_t i;
	int ok;

	if (n->kind == 's' || n->kind == '?')
		return yaml_scalar_event_initialize(&event, NULL, NULL, (yaml_char_t *)n->value, strlen(n->value), 1, 1,
						   n->style == 'p' ? YAML_ANY_SCALAR_STYLE :
						   n->style == '|' ? YAML_LITERAL_SCALAR_STYLE : YAML_DOUBLE_QUOTED_SCALAR_STYLE) &&
		       yaml_emitter_emit(emitter, &event);
	ok = n->kind == 'm' ? yaml_mapping_start_event_initialize(&event, NULL, NULL, 1, YAML_BLOCK_MAPPING_STYLE) :
		yaml_sequence_start_event_initialize(&event, NULL, NULL, 1, YAML_FLOW_SEQUENCE_STYLE);
	ok = ok && yaml_emitter_emit(emitter, &event);
	for (i = 0; ok && i < n->nr; i++)
		ok = yaml_node_events(emitter, n->kids[i]);
	ok = ok && (n->kind == 'm' ? yaml_mapping_end_event_initialize(&event) : yaml_sequence_end_event_initialize(&event));
	return ok && yaml_emitter_emit(emitter, &event);
}

/*
 * v14 8.2: libyaml writes N as the item of DEPTH block lists, which gives
 * it their indentation (two spaces each) and chooses escaping and chomping;
 * the lists' "- " give way to INDENT, and the document's end marker goes
 */
static void yaml_write(struct strbuf *sb, struct ynode *n, int depth, const char *indent)
{
	yaml_emitter_t emitter;
	yaml_event_t event;
	struct strbuf one = STRBUF_INIT;
	int i, ok;

	if (!yaml_emitter_initialize(&emitter))
		die("out of memory");
	yaml_emitter_set_output(&emitter, yaml_out, &one);
	yaml_emitter_set_unicode(&emitter, 1);
	yaml_emitter_set_width(&emitter, -1);
	yaml_emitter_set_indent(&emitter, 2);
	yaml_emitter_set_break(&emitter, YAML_LN_BREAK);
	ok = yaml_stream_start_event_initialize(&event, YAML_UTF8_ENCODING) && yaml_emitter_emit(&emitter, &event) &&
	     yaml_document_start_event_initialize(&event, NULL, NULL, NULL, 1) && yaml_emitter_emit(&emitter, &event);
	for (i = 0; ok && i < depth; i++)
		ok = yaml_sequence_start_event_initialize(&event, NULL, NULL, 1, YAML_BLOCK_SEQUENCE_STYLE) &&
		     yaml_emitter_emit(&emitter, &event);
	ok = ok && yaml_node_events(&emitter, n);
	for (i = 0; ok && i < depth; i++)
		ok = yaml_sequence_end_event_initialize(&event) && yaml_emitter_emit(&emitter, &event);
	ok = ok && yaml_document_end_event_initialize(&event, 1) && yaml_emitter_emit(&emitter, &event) &&
	     yaml_stream_end_event_initialize(&event) && yaml_emitter_emit(&emitter, &event);
	if (!ok)
		BUG("libyaml cannot write: %s", emitter.problem ? emitter.problem : "event initialization failed");
	yaml_emitter_delete(&emitter);
	if (one.len < 2 * (size_t)depth || strncmp(one.buf, "- - - - ", 2 * depth) || !ends_with(one.buf, "\n"))
		BUG("libyaml wrote no list wrapper");
	if (ends_with(one.buf, "\n...\n"))
		strbuf_setlen(&one, one.len - 4);
	strbuf_addstr(sb, indent);
	strbuf_add(sb, one.buf + 2 * depth, one.len - 2 * depth);
	strbuf_release(&one);
}

/* v14 8.2: S as one "..." scalar at four-space indentation, its line ended by the caller */
void yaml_out_scalar(struct strbuf *sb, const char *s)
{
	struct ynode n = { 's', '"', (char *)s };

	yaml_write(sb, &n, 2, "");
	strbuf_setlen(sb, sb->len - 1);
}

/* v14 8.2: a body as a "|" block, or quoted when it holds a break YAML would write literally */
void yaml_out_body(struct strbuf *sb, const char *s)
{
	struct ynode n = { 's', non_lf(s) ? '"' : '|', (char *)s };

	yaml_write(sb, &n, 2, "");
}

/*
 * v14 8.2, spec 7.2: "  KEY:" and the value N, in its order and with each
 * scalar's style, at the indentation of a task's keys
 */
void yaml_out_node(struct strbuf *sb, const char *key, struct ynode *n)
{
	struct ynode name = { 's', 'p', (char *)key }, *pair[] = { &name, n };
	struct ynode map = { 'm', 0, NULL, 0, ARRAY_SIZE(pair), pair };

	yaml_write(sb, &map, 1, "  ");
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
	char *pieces = xstrfmt("/%s/", group);
	int ok = !*group || (path_ok(group) && !strstr(pieces, "/fulfill/"));

	free(pieces);
	return ok;
}

static int one_of(const char *w, const char *const *set)
{
	for (; *set; set++)
		if (!strcmp(w, *set))
			return 1;
	return 0;
}

/*
 * spec 2.3.1: every key of the map MAP is one of KEYS, and all of KEYS are
 * there when REQUIRED; NULL, or why not
 */
char *keys_refusal(struct ynode *map, const char *const *keys, int required)
{
	size_t i;

	for (i = 0; i < map->nr; i += 2)
		if (!one_of(map->kids[i]->value, keys))
			return yaml_refusal(map->kids[i], "unknown key '%s'", map->kids[i]->value);
	for (; required && *keys; keys++)
		if (!yget(map, *keys))
			return yaml_refusal(map, "'%s' is missing", *keys);
	return NULL;
}

/* v14 2.5.2, inputs named fixtures (U60): the spec of the map MAP into S, its scopes in FORM; NULL, or why not */
static char *spec_read(struct ynode *map, struct spec *s, enum scope_form form)
{
	static const char *const keys[] = { "body", "engine", "command", "success", "environment", "fixtures", NULL };
	struct ynode *body = yget(map, "body"), *engine = yget(map, "engine"), *command = yget(map, "command"),
		     *success = yget(map, "success"), *environment = yget(map, "environment"),
		     *fixtures = yget(map, "fixtures");
	struct string_list cmd = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	char *why;
	size_t i;

	strvec_init(&s->command);
	string_list_init_dup(&s->success);
	if (map->kind != 'm')
		return yaml_refusal(map, "a spec is not a map");
	if ((why = keys_refusal(map, keys, 0)))
		return why;
	if (!body || body->kind != 's')
		return yaml_refusal(body ? body : map, "'body' is missing or not a string");
	s->body = xstrdup(body->value);
	/* 2.3.2: no engine, or an empty one, is prompt */
	if (engine && !ytext(engine))
		return yaml_refusal(engine, "'engine' is not a string");
	if (engine && *engine->value && strcmp(engine->value, "exec"))
		return yaml_refusal(engine, "unknown engine '%s'", engine->value);
	s->engine = engine && *engine->value ? xstrdup(engine->value) : NULL;
	if (command && (why = yaml_list(command, "command", &cmd, 0)))
		return why;
	for_each_string_list_item(item, &cmd)
		strvec_push(&s->command, item->string);
	string_list_clear(&cmd, 0);
	if (success && (why = yaml_list(success, "success", &s->success, 0)))
		return why;
	for (i = 0; success && i < success->nr; i++) {
		const char *code = success->kids[i]->value;

		if (!*code || code[strspn(code, "0123456789")] || (*code == '0' && code[1]))
			return yaml_refusal(success->kids[i], "'success' holds '%s', not a decimal integer", code);
	}
	if (s->engine && !success)
		string_list_append(&s->success, "0");
	if (environment && !ystr(environment))
		return yaml_refusal(environment, "'environment' is not a string");
	s->environment = environment ? xstrdup(environment->value) : NULL;
	if (s->engine && !s->command.nr)
		return yaml_refusal(command ? command : map, "an exec spec needs 'command'");
	if (!s->engine && (command || success || environment))
		return yaml_refusal(command ? command : success ? success : environment,
				    "a prompt spec takes only 'body' and 'fixtures'");
	if (fixtures && (why = yaml_list(fixtures, "fixtures", NULL, 0)))
		return why;
	/* 2.3.2: a fixture is a file or a line range, as a provider scope fixed in the code is */
	for (i = 0; fixtures && i < fixtures->nr; i++) {
		struct scope fixture;

		if ((why = scope_parse(&fixture, fixtures->kids[i]->value, 0, 0, form)))
			return yaml_refusal(fixtures->kids[i], "%s in 'fixtures'", why);
		scope_append(&s->fixtures, &fixture);
		scope_clear(&fixture);
	}
	return NULL;
}

struct spec *spec_from(struct ynode *map, char **err)
{
	struct spec *s = xcalloc(1, sizeof(*s));

	if (!(*err = spec_read(map, s, SCOPE_ARGUMENT)))
		return s;
	spec_clear(s);
	free(s);
	return NULL;
}

static const char *const demand_keys[] = { "condition-id", "change-id", "kind", "creator", "consumer", "provider",
					  "state", "specs", "fulfilled", NULL };
static const char *const offer_keys[] = { "condition-id", "change-id", "kind", "creator", "provider", "state",
					 "specs", "undertakes", "undertakes-partially", "fulfilled", NULL };
static const char *const drop_keys[] = { "condition-id", "change-id", "kind", "creator", "target", "state",
					"fulfilled", NULL };

static const struct {
	const char *word;
	int letter;
	const char *const *keys;
} kinds[] = {
	[DEMAND] = { "demand", 'D', demand_keys },
	[OFFER] = { "offer", 'O', offer_keys },
	[DROP] = { "drop", 'X', drop_keys },
};

const char *kind_word(enum kind kind)
{
	return kinds[kind].word;
}

/* spec 1.11: the letter of an ID of KIND */
int kind_letter(enum kind kind)
{
	return kinds[kind].letter;
}

/* the kind WORD names, or -1 */
int kind_of(const char *word)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(kinds); i++)
		if (!strcmp(word, kinds[i].word))
			return i;
	return -1;
}

/*
 * spec 2.1.3: where PATH lies on an ait branch: a contract file
 * (<group>/<ID>.yaml), a fulfilment file (an .md under fulfill/), or
 * neither; nonzero OK when its name and group are right (<group>/fulfill/
 * <ID>-<RID>.md), with ID, RID and GROUP filled
 */
enum placed { MISPLACED, CONTRACT_FILE, FULFIL_FILE };
struct placement {
	int ok;
	char id[16];
	char rid[16];
	char *group;
};

static enum placed place(const char *path, struct placement *at)
{
	const char *slash = strrchr(path, '/'), *base = slash ? slash + 1 : path;
	size_t len = strlen(base);
	enum placed kind;

	memset(at, 0, sizeof(*at));
	at->group = slash ? xstrndup(path, slash - path) : xstrdup("");
	if (ends_with(base, ".md") && (starts_with(path, "fulfill/") || strstr(path, "/fulfill/"))) {
		kind = FULFIL_FILE;
		/* the group above fulfill/, and "<ID>-<RID>.md" */
		if (!strcmp(at->group, "fulfill"))
			*at->group = 0;
		else if (ends_with(at->group, "/fulfill"))
			at->group[strlen(at->group) - 8] = 0;
		else
			return kind;
		if (len != 30 || base[13] != '-')
			return kind;
		snprintf(at->id, sizeof(at->id), "%.13s", base);
		snprintf(at->rid, sizeof(at->rid), "%.13s", base + 14);
		at->ok = id_ok(at->id, "DOX") && id_ok(at->rid, "R");
	} else if (ends_with(base, ".yaml")) {
		kind = CONTRACT_FILE;
		if (len == 18)
			snprintf(at->id, sizeof(at->id), "%.13s", base);
		at->ok = id_ok(at->id, "DOX");
	} else {
		return MISPLACED;
	}
	at->ok = at->ok && group_ok(at->group);
	return kind;
}

/* spec 2.1.3: the ID of the contract path "<group>/<ID>.yaml", or NULL */
char *contract_path_id(const char *path)
{
	struct placement at;
	char *id = place(path, &at) == CONTRACT_FILE && at.ok ? xstrdup(at.id) : NULL;

	free(at.group);
	return id;
}

/* a new contract of KIND in GROUP with ID, its fields empty (spec 6.4, 6.5.2) */
struct contract *contract_new(enum kind kind, const char *group, const char *id)
{
	struct contract *c = xcalloc(1, sizeof(*c));

	snprintf(c->id, sizeof(c->id), "%s", id);
	c->group = xstrdup(group);
	c->path = xstrfmt("%s%s%s.yaml", group, *group ? "/" : "", id);
	c->kind = kind;
	c->condition_id = xstrdup("");
	c->change_id = xstrdup("");
	c->creator = xstrdup("");
	c->state = xstrdup("future");
	string_list_init_dup(&c->undertakes);
	string_list_init_dup(&c->partially);
	string_list_init_dup(&c->fulfilled);
	return c;
}

/* spec 2.1.3: where the fulfilment of C by the request RID lives */
char *fulfil_path(struct contract *c, const char *rid)
{
	return xstrfmt("%s%sfulfill/%s-%s.md", c->group, *c->group ? "/" : "", c->id, rid);
}

/* spec 2.1.3: PATH is a fulfilment of ID, in GROUP when one is given */
static int fulfil_path_ok(const char *path, const char *id, const char *group)
{
	struct placement at;
	int ok = place(path, &at) == FULFIL_FILE && at.ok && !strcmp(at.id, id) &&
		 (!group || !strcmp(at.group, group));

	free(at.group);
	return ok;
}

/*
 * spec 2.3: the contract at PATH (<group>/<ID>.yaml) in the map MAP into C;
 * DRAFT allows the empty IDs and creator of a template (6.5.2). NULL, or why not.
 */
static char *contract_read(struct ynode *map, struct contract *c, int draft)
{
	static const char *const states_do[] = { "future", "cancelled", "withdrawn", NULL };
	static const char *const states_x[] = { "future", "cancelled", NULL };
	static const char *const ids[] = { "condition-id", "change-id", "creator" };
	struct string_list_item *item;
	struct ynode *n;
	char *why, **fields[] = { &c->condition_id, &c->change_id, &c->creator };
	int demand = c->kind == DEMAND, drop = c->kind == DROP;
	size_t i;

	if ((why = keys_refusal(map, kinds[c->kind].keys, 1)))
		return why;
	/* the IDs and the creator: plain values, empty only in a draft */
	for (i = 0; i < ARRAY_SIZE(ids); i++) {
		int valid;

		n = yget(map, ids[i]);
		if (!ytext(n))
			return yaml_refusal(n, "bad '%s'", ids[i]);
		replace_text(fields[i], n->value);
		if (draft && !*n->value)
			valid = 1;
		else
			valid = i < 2 ? id_ok(n->value, "R") : agent_ok(n->value);
		if (!valid)
			return yaml_refusal(n, "bad '%s'", ids[i]);
	}
	n = yget(map, "state");
	if (!ystr(n) || !one_of(n->value, drop ? states_x : states_do))
		return yaml_refusal(n, "bad 'state'");
	replace_text(&c->state, n->value);
	for (i = !demand; !drop && i < 2; i++) {
		const char *field = i ? "provider" : "consumer";
		struct scope_list *scopes = i ? &c->provider : &c->consumer;

		n = yget(map, field);
		if (!ystr(n))
			return yaml_refusal(n, "'%s' is not a string", field);
		if ((why = scopes_parse(scopes, n->value, 1, i, draft ? SCOPE_DRAFT : SCOPE_STORED))) {
			char *refusal = yaml_refusal(n, "bad scope in '%s': %s", field, why);

			free(why);
			return refusal;
		}
		if (!scopes->nr)
			return yaml_refusal(n, "a scope list is empty");
	}
	n = yget(map, "target");
	if (drop && (!ystr(n) || !id_ok(n->value, "DO")))
		return yaml_refusal(n, "bad 'target'");
	if (drop)
		c->target = xstrdup(n->value);
	/* 1.7: demands' IDs, the partial ones first, as a whole one is none of them */
	for (i = 0; c->kind == OFFER && i < 2; i++) {
		const char *key = i ? "undertakes" : "undertakes-partially";
		struct string_list *held = i ? &c->undertakes : &c->partially;

		if ((why = yaml_list(yget(map, key), key, held, 0)))
			return why;
		for_each_string_list_item(item, held)
			if (!id_ok(item->string, "D") || (i && unsorted_string_list_has_string(&c->partially, item->string)))
				return yaml_refusal(yget(map, key)->kids[item - held->items], "bad ID '%s' in '%s'",
						    item->string, key);
	}
	/* 2.3.2: no fulfilment is the key alone */
	n = yget(map, "fulfilled");
	if (n->kind != '?' && (why = yaml_list(n, "fulfilled", &c->fulfilled, 0)))
		return why;
	for_each_string_list_item(item, &c->fulfilled) {
		struct ynode *entry = n->kids[item - c->fulfilled.items];

		if (!fulfil_path_ok(item->string, c->id, c->group))
			return yaml_refusal(entry, "bad path '%s' in 'fulfilled'", item->string);
		/* its line, for branch_load to point at an entry with no file */
		item->util = xmemdupz(&entry->line, sizeof(entry->line));
	}
	if (drop)
		return NULL;
	n = yget(map, "specs");
	if (n->kind != 'l' || !n->nr)
		return yaml_refusal(n, "'specs' is no list of specs");
	c->specs = xcalloc(n->nr, sizeof(*c->specs));
	for (i = 0; i < n->nr; i++) {
		struct spec *spec = &c->specs[c->nspecs++];

		if ((why = spec_read(n->kids[i], spec, draft ? SCOPE_DRAFT : SCOPE_STORED)))
			return why;
	}
	return NULL;
}

struct contract *contract_parse(const char *text, size_t len, const char *path, int draft, char **err)
{
	struct ynode *map = yaml_parse(text, len, err), *kind = yget(map, "kind");
	struct placement at;
	int placed = place(path, &at) == CONTRACT_FILE && at.ok;
	struct contract *c = NULL;

	if (map && !placed)
		*err = yaml_refusal_at(1, "not a contract path");
	else if (map && (!ystr(kind) || kind_of(kind->value) < 0 || kind_letter(kind_of(kind->value)) != *at.id))
		*err = yaml_refusal(kind ? kind : map, "'kind' is missing or does not match the ID");
	else if (map)
		c = contract_new(kind_of(kind->value), at.group, at.id);
	if (c && (*err = contract_read(map, c, draft))) {
		contract_free(c);
		c = NULL;
	}
	ynode_free(map);
	free(at.group);
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
	char *text = scopes_text(l, 1);

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

/* spec 2.3.2, 7.2: a spec's fixtures, an exec or a prompt one's, as a flow list */
void spec_fixtures(struct strbuf *sb, struct spec *s)
{
	const struct scope *f;

	if (!s->fixtures.nr)
		return;
	strbuf_addstr(sb, "  fixtures: [");
	for_each_scope(f, &s->fixtures) {
		char *text = scope_text(f, 1);

		if (f != s->fixtures.items)
			strbuf_addstr(sb, ", ");
		yaml_out_scalar(sb, text);
		free(text);
	}
	strbuf_addstr(sb, "]\n");
}

/* spec 2.3.2: the contract written in its one form, every key on its own line */
char *contract_text(struct contract *c)
{
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;
	size_t i;

	strbuf_addf(&sb, "condition-id: %s\nchange-id: %s\nkind: %s\ncreator: %s\n",
		    *c->condition_id ? c->condition_id : "\"\"", *c->change_id ? c->change_id : "\"\"", kind_word(c->kind),
		    *c->creator ? c->creator : "\"\"");
	if (c->kind == DEMAND)
		scope_line(&sb, "consumer", &c->consumer);
	if (c->kind == DROP)
		strbuf_addf(&sb, "target: %s\n", c->target);
	else
		scope_line(&sb, "provider", &c->provider);
	strbuf_addf(&sb, "state: %s\n", c->state);
	if (c->kind != DROP) {
		strbuf_addstr(&sb, "specs:\n");
		for (i = 0; i < c->nspecs; i++) {
			struct spec *s = &c->specs[i];

			strbuf_addstr(&sb, "- body: ");
			yaml_out_body(&sb, s->body);
			if (s->engine) {
				strbuf_addstr(&sb, "  engine: exec\n");
				spec_lists(&sb, s);
				if (s->environment) {
					strbuf_addstr(&sb, "  environment: ");
					yaml_out_scalar(&sb, s->environment);
					strbuf_addch(&sb, '\n');
				}
			}
			spec_fixtures(&sb, s);
		}
	}
	if (c->kind == OFFER) {
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
static int parse_code(const struct scope *scope, const char *text, struct recorded_code *code)
{
	struct string_list fields = STRING_LIST_INIT_DUP;
	const char *mode, *type, *oid, *end;
	int first = 0, last = 0, range, parsed = 0;

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
	parsed = 1;
	snprintf(code->mode, sizeof(code->mode), "%s", mode);
	snprintf(code->type, sizeof(code->type), "%s", type);
	snprintf(code->oid, sizeof(code->oid), "%s", oid);
	code->first = range ? first : 0;
	code->last = range ? last : 0;
done:
	string_list_clear(&fields, 0);
	return parsed;
}

static char *code_text(struct recorded_code *code)
{
	return code->first ? xstrfmt("%s %s %s %d-%d", code->mode, code->type, code->oid, code->first, code->last) :
		xstrfmt("%s %s %s", code->mode, code->type, code->oid);
}

/* spec 2.5: the fulfilment at PATH from its front matter MAP and BODY into F; NULL, or why not */
static char *fulfil_read(struct ynode *map, const char *body, const char *path, struct fulfilment *f)
{
	static const char *const keys[] = { "contract", "condition-id", "by", "session", "commit", "outcome", "code", NULL };
	char **fields[] = { &f->contract, &f->condition_id, &f->by, &f->session, &f->commit, &f->outcome };
	struct ynode *code = yget(map, "code");
	const char *invalid;
	char *why;
	size_t i;

	if ((why = keys_refusal(map, keys, 1)))
		return why;
	for (i = 0; i < ARRAY_SIZE(fields); i++) {
		if (!ystr(yget(map, keys[i])))
			return yaml_refusal(yget(map, keys[i]), "bad '%s' in the front matter", keys[i]);
		*fields[i] = xstrdup(yget(map, keys[i])->value);
	}
	f->path = xstrdup(path);
	f->body = xstrdup(body);
	invalid = !id_ok(f->contract, "DOX") ? "contract" : !id_ok(f->condition_id, "R") ? "condition-id" :
		!agent_ok(f->by) ? "by" : !id_ok(f->session, "S") ? "session" : !oid_ok(f->commit) ? "commit" :
		!one_of(f->outcome, (const char *const[]){ "pass", "forced", "withdrawn", NULL }) ? "outcome" :
		code->kind != 'm' ? "code" : NULL;
	if (invalid)
		return yaml_refusal(yget(map, invalid), "bad '%s' in the front matter", invalid);
	if (!fulfil_path_ok(path, f->contract, NULL))
		return yaml_refusal_at(1, "not a fulfilment path");
	if ((*f->contract == 'X') != !strcmp(f->outcome, "withdrawn"))
		return yaml_refusal(yget(map, "outcome"), "'outcome' does not match the contract kind");
	for (i = 0; i < code->nr; i += 2) {
		struct ynode *scope = code->kids[i], *entry = code->kids[i + 1];
		struct recorded_code value;
		struct scope key;
		char *scope_error;

		if (!ystr(entry))
			return yaml_refusal(entry, "a code entry is not a string");
		scope_error = scope_parse(&key, scope->value, 0, 0, SCOPE_STORED);
		if (scope_error || !parse_code(&key, entry->value, &value)) {
			free(scope_error);
			scope_clear(&key);
			return yaml_refusal(entry, "bad code entry for '%s'", scope->value);
		}
		code_add(&f->code, &key, &value);
		scope_clear(&key);
	}
	if (*f->contract == 'X' && f->code.nr)
		return yaml_refusal(code, "a drop's code is not empty");
	return NULL;
}

static struct fulfilment *fulfil_parse(const char *text, const char *path, char **err)
{
	struct fulfilment *f = NULL;
	struct ynode *map = NULL;
	const char *end;
	char *front;

	if (!starts_with(text, "---\n") || !(end = strstr(text + 3, "\n---\n"))) {
		*err = yaml_refusal_at(1, "no front matter");
		return NULL;
	}
	front = xstrndup(text, end + 1 - text);
	if ((map = yaml_parse(front, end + 1 - text, err)))
		f = xcalloc(1, sizeof(*f));
	if (f && (*err = fulfil_read(map, end + 5, path, f))) {
		fulfilment_free(f);
		f = NULL;
	}
	ynode_free(map);
	free(front);
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

struct contract *contract_get(struct branch *b, const char *id)
{
	struct string_list_item *item = string_list_lookup(&b->contracts, id);

	return item ? item->util : NULL;
}

/* a malformed file at PATH: its one error */
static void branch_error(struct branch *b, const char *path, char *reason)
{
	struct string_list_item *slot = string_list_insert(&b->errors, path);

	free(slot->util);
	slot->util = reason;
}

/*
 * spec 2.1.3, 2.3, 2.5: the contracts and fulfilments of the branch NAME at
 * TIP (or of the tree TREEISH); each malformed file is one error
 */
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
		struct placement at;
		int fulfil_file = place(e->path, &at) == FULFIL_FILE;

		free(at.group);
		if (!strcmp(e->type, "tree"))
			continue;
		if (strcmp(e->type, "blob")) {
			err = yaml_refusal_at(1, "not a blob");
		} else if (!(text = object(e->oid, 1, NULL, &len))) {
			err = yaml_refusal_at(1, "could not read blob '%s'", e->oid);
		} else if ((nul = memchr(text, 0, len))) {
			err = yaml_refusal_at((int)line_at(text, nul - text) + 1, "a NUL character");
		} else if (fulfil_file) {
			struct fulfilment *f = fulfil_parse(text, e->path, &err);

			if (f)
				string_list_insert(&b->fulfils, e->path)->util = f;
		} else {
			struct contract *c = contract_parse(text, len, e->path, 0, &err);
			struct contract *had = c ? contract_get(b, c->id) : NULL;

			if (had) {
				err = yaml_refusal_at(1, "'%s' is also at '%s'", c->id, had->path);
				contract_free(c);
			} else if (c) {
				string_list_insert(&b->contracts, c->id)->util = c;
			}
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

			/* the path names the contract (contract_read) and its file the same (fulfil_read) */
			if (!got) {
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

/* spec 1.4: a contract that has not ended */
int live(struct contract *c)
{
	return c && !strcmp(c->state, "future");
}

/* spec 5.2: the fulfilment at PATH in B, when C made it with its condition-id */
struct fulfilment *valid_fulfilment(struct branch *b, struct contract *c, const char *path)
{
	struct string_list_item *item = string_list_lookup(&b->fulfils, path);
	struct fulfilment *f = item ? item->util : NULL;

	return f && !strcmp(f->condition_id, c->condition_id) ? f : NULL;
}

int has_valid_fulfilment(struct branch *b, struct contract *c)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &c->fulfilled)
		if (valid_fulfilment(b, c, item->string))
			return 1;
	return 0;
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
	int nobase;
	struct blob_hunk *hunks;
	size_t nr;
	size_t alloc;
};

/*
 * Two object names fix their diff, as an object name fixes its content: one
 * table serves every tree and view, and listen empties it between rounds.
 */
static struct blob_diff *blob_pairs;
static size_t blob_pairs_nr, blob_pairs_alloc;

void blob_pairs_clear(void)
{
	size_t i;

	for (i = 0; i < blob_pairs_nr; i++)
		free(blob_pairs[i].hunks);
	free(blob_pairs);
	blob_pairs = NULL;
	blob_pairs_nr = blob_pairs_alloc = 0;
}

/* The hunks from BASE to the blob NOW; NULL when BASE is no blob here (5.3, 6.7.4). */
static struct blob_diff *blob_diff_get(const char *base, const char *now)
{
	struct string_list lines = STRING_LIST_INIT_DUP;
	struct string_list_item *line;
	struct blob_diff *diff;
	char *text, *type;
	size_t i;

	for (i = 0; i < blob_pairs_nr; i++)
		if (!strcmp(blob_pairs[i].base, base) && !strcmp(blob_pairs[i].now, now))
			return blob_pairs[i].nobase ? NULL : &blob_pairs[i];
	ALLOC_GROW(blob_pairs, blob_pairs_nr + 1, blob_pairs_alloc);
	diff = &blob_pairs[blob_pairs_nr++];
	memset(diff, 0, sizeof(*diff));
	snprintf(diff->base, sizeof(diff->base), "%s", base);
	snprintf(diff->now, sizeof(diff->now), "%s", now);
	/* builtin/diff.c:577-602 diffs blobs only after reading the object's type */
	type = object(base, 0, NULL, NULL);
	diff->nobase = !type || strcmp(type, "blob");
	free(type);
	if (diff->nobase)
		return NULL;
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
				   int *first, int *end, int *changed)
{
	struct blob_diff *diff;
	int ma = -1, mb = -1, offset = 0;
	size_t i;

	*changed = 0;
	if (!now)
		return MISSING;
	if (!base)
		return NOBASE;
	/* NOW is a blob of the tree, so the same name is that blob */
	if (!strcmp(base, now)) {
		*first = a;
		*end = b;
		return FOUND;
	}
	if (!(diff = blob_diff_get(base, now)))
		return NOBASE;
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

/* the lines of TEXT, the last one counted even without its LF */
static int line_count(const char *text, size_t n)
{
	return line_at(text, n) + (n && text[n - 1] != '\n');
}

/* spec 3.2 (v14 3.3): a scope argument fixed at T, a line range taking T's blob, or E_SCOPE */
void scope_pin(struct scope *s, struct tree *t)
{
	struct entry *e;

	if (s->kind == SCOPE_ROOT)
		return;
	if (s->kind == SCOPE_NEW) {
		if (tree_get(t, s->path))
			reject(128, NULL, "bad scope: '%s' exists", s->path);
		return;
	}
	if (s->kind == SCOPE_DIR) {
		char *dir = xstrndup(s->path, strlen(s->path) - 1);

		e = tree_get(t, dir);
		if (!e || strcmp(e->type, "tree"))
			reject(128, NULL, "bad scope: '%s' is not a directory", s->path);
		free(dir);
		return;
	}
	/* File scopes include gitlinks; line ranges require a text blob. */
	e = tree_get(t, s->path);
	if (!e || !strcmp(e->type, "tree"))
		reject(128, NULL, "bad scope: '%s' is not a file", s->path);
	if (s->kind == SCOPE_RANGE) {
		size_t n;
		const char *oid = blob_at(t, s->path);
		char *text = oid ? object(oid, 1, NULL, &n) : NULL;

		if (!text || strlen(text) != n || s->last > line_count(text, n))
			reject(128, NULL, "bad scope: '%s' is not a range of '%s'", scope_text(s, 0), s->path);
		free(text);
		snprintf(s->base, sizeof(s->base), "%s", oid);
	}
}

/* Draft input tuples keep their old pin; only new tuples read the code tree. */
void scopes_pin(struct scope_list *scopes, struct tree *t, const struct scope_list *was)
{
	struct scope *s;
	const struct scope *old;

	for_each_scope(s, scopes) {
		int retained = 0;

		if (was)
			for_each_scope(old, was)
				if (scope_equal(s, old, 0)) {
					snprintf(s->base, sizeof(s->base), "%s", old->base);
					retained = 1;
					break;
				}
		if (!retained)
			scope_pin(s, t);
	}
}

/*
 * spec 6.7.4, v14 3.8: where SCOPE is in T, as one judgment its callers word:
 * FOUND with its path and mapped line endpoints, or the reason it is not
 */
void scope_result_at(const struct scope *s, struct tree *t, struct scope_result *out)
{
	struct entry *e = tree_get(t, s->path);
	int changed;

	out->at.path = s->path;
	out->at.first = out->at.last = 0;
	out->exists = e != NULL;
	if (s->kind == SCOPE_NEW || s->kind == SCOPE_DIR || s->kind == SCOPE_ROOT)
		out->status = ABSTRACT;
	else if (s->kind != SCOPE_RANGE)
		out->status = e && strcmp(e->type, "tree") ? FOUND : MISSING;
	else
		out->status = map_blobs(*s->base ? s->base : NULL, blob_at(t, s->path),
					s->first, s->last, &out->at.first, &out->at.last, &changed);
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
static int scope_same(const struct scope *scope, const struct recorded_code *code, struct tree *x,
		      struct recorded_code *at)
{
	struct entry *e = tree_get(x, scope->path);
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
	struct scope_result at;

	scope_result_at(scope, x, &at);
	return scope_result_text(scope, &at);
}

/* C39: the environment definition NAME is a file of the code */
char *environment_path(const char *name)
{
	return xstrfmt("specs/environments/%s.yaml", name);
}

/*
 * plan 3 (U60): what a fulfilment records of C and the judgment compares (rule
 * 4): the provider, then each spec's fixtures and environment definition file
 * that is not among them yet
 */
void witnessed_scopes(const struct contract *c, struct scope_list *out)
{
	const struct scope *f;
	size_t i;

	scopes_copy(out, &c->provider);
	for (i = 0; i < c->nspecs; i++) {
		for_each_scope(f, &c->specs[i].fixtures)
			if (!scopes_has(out, f, 1))
				scope_append(out, f);
		if (c->specs[i].environment) {
			struct scope file = { .kind = SCOPE_FILE, .path = environment_path(c->specs[i].environment) };

			if (!scopes_has(out, &file, 1))
				scope_append(out, &file);
			scope_clear(&file);
		}
	}
}

void geometry_init(struct contract_geometry *g, const struct contract *c, struct tree *t)
{
	size_t i;

	memset(g, 0, sizeof(*g));
	g->contract = c;
	g->consumer = xcalloc(c->consumer.nr, sizeof(*g->consumer));
	witnessed_scopes(c, &g->witnessed);
	g->witness = xcalloc(g->witnessed.nr, sizeof(*g->witness));
	for (i = 0; i < c->consumer.nr; i++)
		scope_result_at(&c->consumer.items[i], t, &g->consumer[i]);
	for (i = 0; i < g->witnessed.nr; i++)
		scope_result_at(&g->witnessed.items[i], t, &g->witness[i]);
}

void geometry_clear(struct contract_geometry *g)
{
	free(g->consumer);
	free(g->witness);
	scopes_clear(&g->witnessed);
	memset(g, 0, sizeof(*g));
}

/*
 * v14 3.9: the scope Q (found at QA in T) and S overlap; S is mapped in T only
 * when the paths meet. A pin not found there overlaps its whole file.
 */
static int scope_overlaps(const struct scope *q, const struct scope_result *qa, const struct scope *s, struct tree *t)
{
	struct scope_result sa;
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
	if (s->kind == SCOPE_RANGE && *s->base) {
		scope_result_at(s, t, &sa);
		if (sa.status == FOUND) {
			c = sa.at.first;
			d = sa.at.last;
		}
	} else if (s->kind == SCOPE_RANGE) {
		c = s->first;
		d = s->last;
	}
	return !a || !c || (a <= d && c <= b);
}

/* v14 3.9, 3.9a: C overlaps Q in T; DEPENDING leaves out what only a directory provider covers */
int contract_overlaps(const struct contract *c, struct tree *t, const struct scope *q, const struct scope_result *qa,
		      int depending)
{
	size_t i;

	for (i = 0; i < c->consumer.nr; i++)
		if (scope_overlaps(q, qa, &c->consumer.items[i], t))
			return 1;
	for (i = 0; i < c->provider.nr; i++) {
		const struct scope *s = &c->provider.items[i];

		if ((!depending || (s->kind != SCOPE_DIR && s->kind != SCOPE_ROOT) ||
		     q->kind == SCOPE_DIR || q->kind == SCOPE_ROOT) && scope_overlaps(q, qa, s, t))
			return 1;
	}
	return 0;
}

/* spec 1.7: how the offer O holds the demand ID: 'w' wholly, 'p' partially, 0 not (or O is no offer) */
static int holds(struct contract *o, const char *id)
{
	if (o->kind != OFFER)
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
	if (c->kind == OFFER) {
		for_each_string_list_item(item, how == 'w' ? &c->undertakes : &c->partially) {
			struct contract *d = contract_lookup(v, item->string);

			if (live(d) && d->kind == DEMAND)
				string_list_insert(out, d->id)->util = d;
		}
	} else if (c->kind == DEMAND) {
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
	undertakings(v, c, c->kind == OFFER ? 'w' : 'p', out);
}

/* One completed result per contract; a back edge is never a completed result. */
void judgment_init(struct judgment *j, const struct contract_view *view, struct tree *t)
{
	memset(j, 0, sizeof(*j));
	j->view = *view;
	j->tree = t;
	string_list_init_dup(&j->done);
	string_list_init_dup(&j->visiting);
}

const struct judgment_result *judgment_get(struct judgment *j, struct contract *c)
{
	struct string_list_item *hit = string_list_lookup(&j->done, c->id), *item, *f;
	struct string_list needs = STRING_LIST_INIT_DUP;
	struct judgment_result *r;
	int whole_ok = 1;
	size_t i;

	if (hit)
		return hit->util;
	if (string_list_has_string(&j->visiting, c->id))
		return NULL;
	r = xcalloc(1, sizeof(*r));
	if (!live(c)) {
		r->verdict = strcmp(c->state, "cancelled") ? V_WITHDRAWN : V_CANCELLED;
		goto done;
	}
	if (c->kind == DROP) {
		r->verdict = c->fulfilled.nr ? V_FULFILLED : V_FUTURE;
		goto done;
	}
	if (!has_valid_fulfilment(j->view.at, c)) {
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
	witnessed_scopes(c, &r->witnessed);
	r->matches_any = xcalloc(r->witnessed.nr, sizeof(*r->matches_any));
	for_each_string_list_item(f, &c->fulfilled) {
		struct fulfilment *valid = valid_fulfilment(j->view.at, c, f->string);
		struct recorded_code *code;
		int all = 1;

		if (!valid)
			continue;
		code = xcalloc(r->witnessed.nr, sizeof(*code));
		for (i = 0; i < r->witnessed.nr; i++) {
			const struct scope *s = &r->witnessed.items[i];
			int same = scope_same(s, code_lookup(&valid->code, s), j->tree, &code[i]);

			r->matches_any[i] |= same;
			all &= same;
		}
		if (all) {
			r->code = code;
			break;
		}
		free(code);
	}
	r->verdict = whole_ok && r->code ? V_CURRENT : V_UNVERIFIED;
done:
	string_list_clear(&needs, 0);
	string_list_insert(&j->done, c->id)->util = r;
	return r;
}

void judgment_clear(struct judgment *j)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &j->done) {
		struct judgment_result *r = item->util;

		free(r->code);
		free(r->matches_any);
		scopes_clear(&r->witnessed);
		free(r);
	}
	string_list_clear(&j->done, 0);
	string_list_clear(&j->visiting, 0);
}

/* spec 1.5: C was made by the agent of the session in use */
int is_mine(const struct contract *c)
{
	return *sid && !strcmp(c->creator, agent);
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

/*
 * v14 3.8: the scopes of G's contract as they are in its tree; MATCHES, a
 * judgment's per-scope comparison, marks a found scope that differs ?changed
 */
char *geometry_text(const struct contract_geometry *g, enum geometry_part part, const unsigned char *matches)
{
	const struct contract *c = g->contract;
	/* the provider heads the witnessed scopes and the fixtures follow it (witnessed_scopes()) */
	size_t from = part == GEOMETRY_FIXTURES ? c->provider.nr : 0;
	struct scope_list scopes = part == GEOMETRY_CONSUMER ? c->consumer : (struct scope_list){
		.items = g->witnessed.items + from,
		.nr = part == GEOMETRY_PROVIDER ? c->provider.nr : g->witnessed.nr - from };
	const struct scope_result *positions = part == GEOMETRY_CONSUMER ? g->consumer : g->witness + from;
	const struct scope **ordered = scope_order(&scopes);
	struct strbuf out = STRBUF_INIT;
	size_t i;

	if (matches)
		matches += from;
	for (i = 0; i < scopes.nr; i++) {
		size_t n = ordered[i] - scopes.items;
		char *display = scope_result_text(ordered[i], &positions[n]);
		int changed = matches && !matches[n] && positions[n].status == FOUND;

		strbuf_addf(&out, "%s%s%s", i ? "," : "", display, changed ? " ?changed" : "");
		free(display);
	}
	free(ordered);
	return strbuf_detach(&out, NULL);
}

/* spec 5.5: C is current in H's judgment, and X differs from the code that witnessed it */
int judgment_touched(struct judgment *h, struct contract *c, struct tree *x)
{
	const struct judgment_result *r = judgment_get(h, c);
	size_t i;

	if (r->verdict != V_CURRENT)
		return 0;
	for (i = 0; i < r->witnessed.nr; i++)
		if (!scope_same(&r->witnessed.items[i], &r->code[i], x, NULL))
			return 1;
	return 0;
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
	struct contract_view view = { b, NULL }, old = { was, NULL };
	const struct contract_view *v = &view;
	struct string_list_item *item, *o;

	for_each_string_list_item(item, &b->contracts) {
		struct contract *d = item->util;
		struct string_list whole = STRING_LIST_INIT_DUP, part = STRING_LIST_INIT_DUP, before = STRING_LIST_INIT_DUP;

		if (!live(d) || d->kind != DEMAND)
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
