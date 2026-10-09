/*
 * Trees, contract records and the judgements computed from them (spec 2, 3, 1.5a).
 * Records are stored facts; everything else here is computed (rule 1).
 */
#include "ait.h"

static int cmp_entry(const void *a, const void *b)
{
	return strcmp(((const struct entry *)a)->path, ((const struct entry *)b)->path);
}

/* one sorted listing per tree: lookups are a bsearch, a diff is a merge walk */
struct tree *tree_load(const char *treeish)
{
	struct tree *t = xcalloc(1, sizeof(*t));
	size_t alloc = 0;
	char *p, *end;

	snprintf(t->oid, sizeof(t->oid), "%s",
		 line1(git("rev-parse --verify %s", xstrfmt("%s^{tree}", treeish))));
	for (p = git("ls-tree -r -t -z --full-tree %s", t->oid); *p; p = end + 1) {
		struct entry *e;

		end = p + strlen(p);
		ALLOC_GROW(t->e, t->nr + 1, alloc);
		e = &t->e[t->nr++];
		sscanf(p, "%7s %*s %47s", e->mode, e->oid);
		e->path = strchr(p, '\t') + 1;
	}
	QSORT(t->e, t->nr, cmp_entry);
	return t;
}

struct entry *tree_get(struct tree *t, const char *path)
{
	struct entry key = { path };

	return bsearch(&key, t->e, t->nr, sizeof(*t->e), cmp_entry);
}

const char *blob_at(struct tree *t, const char *path)
{
	struct entry *e = tree_get(t, path);

	return e && strcmp(e->mode, "040000") && strcmp(e->mode, "160000") ? e->oid : NULL;
}

/* BASE plus INDEX_INFO ("MODE OID\tPATH" lines) in a throwaway index (spec 4.8) */
char *tree_with(const char *base, const char *index_info)
{
	char *dir = xstrfmt("%s/ait-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
	char *index, *tree;

	if (!mkdtemp(dir))
		die_errno("could not create '%s'", dir);
	index = xstrfmt("%s/index", dir);
	git_run(NULL, index, NULL, "read-tree %s", base);
	git_run(index_info, index, NULL, "update-index --add --index-info");
	tree = line1(git_run(NULL, index, NULL, "write-tree"));
	unlink(index);
	rmdir(dir);
	return tree;
}

/* paths whose blob or mode differ between A and B */
void tree_diff(struct tree *a, struct tree *b, struct string_list *paths)
{
	size_t i = 0, j = 0;

	while (i < a->nr || j < b->nr) {
		struct entry *x = i < a->nr ? &a->e[i] : NULL;
		struct entry *y = j < b->nr ? &b->e[j] : NULL;
		int c = !x ? 1 : !y ? -1 : strcmp(x->path, y->path);
		struct entry *e = c < 0 ? x : y;

		i += c <= 0;
		j += c >= 0;
		if (!strcmp(e->mode, "040000"))
			continue;
		if (c || strcmp(x->mode, y->mode) || strcmp(x->oid, y->oid))
			string_list_append(paths, e->path);
	}
}

/* record lines are "key value"; this is also the canonical key order (spec 2.2.2) */
static const char *keys[] = { "role", "state", "consumer", "provider", "target", "file", "creator", "prev", NULL };

static int key_rank(const char *line)
{
	int i;

	for (i = 0; keys[i]; i++)
		if (starts_with(line, keys[i]) && line[strlen(keys[i])] == ' ')
			return i;
	return -1;
}

/* within a key, lines sort by value; "new:PATH" sorts as PATH (spec 3.2a) */
static int cmp_line(const void *a, const void *b)
{
	const char *x = ((const struct string_list_item *)a)->string;
	const char *y = ((const struct string_list_item *)b)->string;
	int d = key_rank(x) - key_rank(y);

	x = strchr(x, ' ') + 1;
	y = strchr(y, ' ') + 1;
	skip_prefix(x, "new:", &x);
	skip_prefix(y, "new:", &y);
	return d ? d : strcmp(x, y);
}

char *record_text(struct record *r, int with_prev)
{
	struct strbuf sb = STRBUF_INIT;
	size_t i;

	QSORT(r->lines.items, r->lines.nr, cmp_line);
	for (i = 0; i < r->lines.nr; i++)
		if (with_prev || !starts_with(r->lines.items[i].string, "prev "))
			strbuf_addf(&sb, "%s\n", r->lines.items[i].string);
	return strbuf_detach(&sb, NULL);
}

const char *rget(struct record *r, const char *key)
{
	struct string_list v = STRING_LIST_INIT_DUP;

	rvals(r, key, &v);
	return v.nr ? v.items[0].string : "";
}

void rvals(struct record *r, const char *key, struct string_list *out)
{
	size_t i;

	for (i = 0; i < r->lines.nr; i++)
		if (key_rank(r->lines.items[i].string) >= 0 &&
		    !strcmp(keys[key_rank(r->lines.items[i].string)], key))
			string_list_append(out, strchr(r->lines.items[i].string, ' ') + 1);
}

void rset(struct record *r, const char *key, struct string_list *vals)
{
	struct string_list lines = STRING_LIST_INIT_DUP;
	size_t i;

	for (i = 0; i < r->lines.nr; i++)
		if (strcmp(keys[key_rank(r->lines.items[i].string)], key))
			string_list_append(&lines, r->lines.items[i].string);
	for (i = 0; i < vals->nr; i++)
		string_list_append_nodup(&lines, xstrfmt("%s %s", key, vals->items[i].string));
	r->lines = lines;
}

int is_live(struct record *r)
{
	return *r->id != 'u' && (!strcmp(rget(r, "state"), "future") || is_current(r));
}

int is_current(struct record *r)
{
	return *r->id != 'u' && !strcmp(rget(r, "state"), "current");
}

/* current in HEAD: only such a provider shows "?changed" (spec 3.8) */
int was_current(struct record *r)
{
	return r->old && is_current(r->old);
}

struct record *state_get(struct state *s, const char *id)
{
	size_t i;

	for (i = 0; i < s->nr; i++)
		if (!strcmp(s->r[i]->id, id))
			return s->r[i];
	return NULL;
}

struct record *state_add(struct state *s, const char *id)
{
	struct record *r = xcalloc(1, sizeof(*r));

	snprintf(r->id, sizeof(r->id), "%s", id);
	r->lines.strdup_strings = 1;
	ALLOC_GROW(s->r, s->nr + 1, s->alloc);
	s->r[s->nr++] = r;
	return r;
}

struct record *record_copy(struct record *r)
{
	struct record *c = xmalloc(sizeof(*c));
	size_t i;

	*c = *r;
	string_list_init_dup(&c->lines);
	for (i = 0; i < r->lines.nr; i++)
		string_list_append(&c->lines, r->lines.items[i].string);
	c->old = r;
	return c;
}

/* spec 2.2.1: a stored record must read back as exactly its canonical text */
static void record_parse(struct record *r, const char *text)
{
	struct string_list lines = STRING_LIST_INIT_DUP;
	size_t i;

	string_list_split(&lines, text, "\n", -1);
	for (i = 0; i + 1 < lines.nr; i++) {
		const char *l = lines.items[i].string;
		const char *v = strchr(l, ' ');

		if (key_rank(l) < 0 || !v[1] || isspace(v[1]) || isspace(l[strlen(l) - 1]) ||
		    l[strcspn(l, "\r\t\x7f\x01\x02\x03\x04\x05\x06\x07\x08\x0b\x0c\x0e\x0f\x10\x11\x12"
			     "\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f")])
			r->bad = xstrfmt("bad line '%s'", l);
		string_list_append(&r->lines, l);
	}
	if (!r->bad && strcmp(record_text(r, 1), text))
		r->bad = xstrdup("not in canonical form");
}

static int hex12(const char *s)
{
	return strspn(s, "0123456789abcdef") == 12;
}

/* "D…", "O…" or "undertake/D…-O…" (spec 1.9, 2.1) */
static int id_ok(const char *id)
{
	if (strlen(id) == 13)
		return strchr("DO", *id) && hex12(id + 1);
	return strlen(id) == 37 && starts_with(id, "undertake/D") && id[23] == '-' && id[24] == 'O' &&
	       strspn(id + 11, "0123456789abcdef") == 12 && hex12(id + 25);
}

void state_load(struct state *s, struct tree *t)
{
	size_t i;

	for (i = 0; i < t->nr; i++) {
		struct entry *e = &t->e[i];
		const char *id, *text;
		struct record *r;

		if (!skip_prefix(e->path, ".ait/", &id) || !strcmp(e->mode, "040000"))
			continue;
		text = git("cat-file blob %s", e->oid);
		if (!strcmp(id, "init")) {
			if (strcmp(e->mode, "100644") || strcmp(text, "version 1\n"))
				state_add(s, id)->bad = xstrdup("must be the single line 'version 1'");
			continue;
		}
		r = state_add(s, id);
		if (*id == 'X')
			unsupported("drop record");
		if (strcmp(e->mode, "100644") || !id_ok(id))
			r->bad = xstrdup("not a record path");
		else
			record_parse(r, text);
		r->old = record_copy(r);
	}
}

static int oid_ok(const char *s)
{
	return strlen(s) == 40 && strspn(s, "0123456789abcdef") == 40;
}

/* spec 3.1 */
int path_ok(const char *p)
{
	struct string_list parts = STRING_LIST_INIT_DUP;
	int ok = *p && *p != '/' && *p != '-' && !p[strcspn(p, " :,\t\r\n")] &&
		 strcmp(p, ".ait") && !starts_with(p, ".ait/");
	size_t i;

	string_list_split(&parts, p, "/", -1);
	for (i = 0; ok && i < parts.nr; i++)
		ok = *parts.items[i].string && strcmp(parts.items[i].string, ".") &&
		     strcmp(parts.items[i].string, "..");
	string_list_clear(&parts, 0);
	return ok;
}

/* "new:PATH", "PATH OID", "DIR/ OID" or "/ OID" (spec 2.2.2) */
static int scope_ok(const char *v, int allow_new, int allow_abs)
{
	const char *sp = strchr(v, ' '), *rest;
	char *path;

	if (skip_prefix(v, "new:", &rest))
		return allow_new && path_ok(rest);
	if (!sp || !oid_ok(sp + 1))
		return 0;
	path = xstrndup(v, sp - v);
	if (!strcmp(path, "/") || (path[strlen(path) - 1] == '/' && (path[strlen(path) - 1] = 0, 1)))
		return allow_abs && (!*path || !strcmp(path, "/") || path_ok(path));
	return path_ok(path);
}

static int line_ok(struct record *r, const char *key, const char *v)
{
	char a[4096], b[4096], c[4096], d[4096];
	int n = sscanf(v, "%4095s %4095s %4095s %4095s", a, b, c, d);

	if (!strcmp(key, "consumer"))
		return scope_ok(v, 1, 0);
	if (!strcmp(key, "provider"))
		return scope_ok(v, !is_current(r), !is_current(r));
	if (!strcmp(key, "file"))
		return n == 4 && path_ok(a) && (!strcmp(b, "100644") || !strcmp(b, "100755")) &&
		       oid_ok(c) && (!strcmp(d, "def") || !strcmp(d, "input")) &&
		       strlen(v) == strlen(a) + strlen(b) + strlen(c) + strlen(d) + 3;
	if (!strcmp(key, "creator"))
		return n == 2 && (!strcmp(b, "-") || (strlen(b) == 13 && *b == 'S' &&
						      strspn(b + 1, "0123456789abcdef") == 12));
	if (!strcmp(key, "prev"))
		return !strcmp(v, "-") || oid_ok(v);
	return 1;
}

static int fail_with(struct verdict *v, int code, const char *word, char *hint, char *msg)
{
	v->code = code;
	v->word = word;
	v->hint = hint;
	v->msg = msg;
	return code;
}

static int bad_record(struct verdict *v, struct record *r, const char *why)
{
	return fail_with(v, 128, "format", xstrfmt("records are written only by ait; restore '.ait/%s' as in the "
						   "last commit where it was valid", r->id),
			 xstrfmt("bad format: '.ait/%s': %s", r->id, why));
}

/* validate(state) of spec 5.9 a-e: invariants of one state, no parents involved; 0 when it holds */
int check_state(struct state *s, struct tree *t, struct verdict *v)
{
	static const char *shape[] = { "D role state consumer+ provider+ file+ creator prev+",
				       "O role state provider+ file+ creator prev+", "u state prev+" };
	size_t i, k;

	for (i = 0; i < s->nr; i++) {
		struct record *r = s->r[i], *d, *o;
		const char *sh = shape[*r->id == 'D' ? 0 : *r->id == 'O' ? 1 : 2];
		struct string_list vals = STRING_LIST_INIT_DUP;

		if (r->bad)
			return bad_record(v, r, r->bad);
		if (!strcmp(r->id, "init"))
			continue;
		for (k = 0; keys[k]; k++) {
			const char *at = strstr(sh, xstrfmt(" %s", keys[k]));
			size_t j;

			string_list_clear(&vals, 0);
			rvals(r, keys[k], &vals);
			if (at ? at[strlen(keys[k]) + 1] == '+' ? !vals.nr : vals.nr != 1 : vals.nr)
				return bad_record(v, r, xstrfmt("wrong number of '%s' lines", keys[k]));
			for (j = 0; j < vals.nr; j++)
				if (!line_ok(r, keys[k], vals.items[j].string))
					return bad_record(v, r, xstrfmt("bad '%s' line '%s'", keys[k], vals.items[j].string));
		}
		if (strcmp(rget(r, "role"), *r->id == 'D' ? "demand" : *r->id == 'O' ? "offer" : "") ||
		    !strstr(*r->id == 'u' ? " undertaken released " : " future current cancelled withdrawn ",
			    xstrfmt(" %s ", rget(r, "state"))))
			return bad_record(v, r, "role or state does not fit the record");
		if (*r->id != 'u' && !strstr(record_text(r, 1), " def\n"))
			return bad_record(v, r, "no spec definition");
		if (!strcmp(rget(r, "state"), "withdrawn"))
			return fail_with(v, 1, "state", NULL, xstrfmt("not allowed: '%s' is withdrawn without a drop", r->id));
		if (*r->id != 'u')
			continue;
		d = state_get(s, xstrndup(r->id + 10, 13));
		o = state_get(s, r->id + 24);
		if (!d || !o || *d->id != 'D' || *o->id != 'O')
			return bad_record(v, r, "its demand or offer does not exist");
		if (!strcmp(rget(r, "state"), "undertaken") && is_current(o) && !strcmp(rget(d, "state"), "future"))
			return fail_with(v, 1, "state", was_current(d) ?
					 xstrfmt("'%s' is undertaken by current '%s'; reopen them in the same request: "
						 "'ait future amend %s future amend %s -m <why>'", d->id, o->id, d->id, o->id) :
					 xstrfmt("undertake it with a future offer: 'ait future offer %s %s as o undertake %s o -m <why>'",
						 rjoin(d, "provider", ","), def_paths(d), d->id),
					 xstrfmt("not allowed: current '%s' undertakes future '%s'", o->id, d->id));
	}
	for (i = 0; i < s->nr; i++)
		if (is_live(s->r[i]) && !files_match(s->r[i], t))
			return fail_with(v, 1, "incomplete", xstrfmt("make the spec files of '%s' match it, or reopen it "
								      "with them: 'ait future amend %s -m <why>'",
								      s->r[i]->id, s->r[i]->id),
					 xstrfmt("spec files do not match the contract: '%s'", s->r[i]->id));
	return 0;
}

void validate(struct state *s, struct tree *t)
{
	struct verdict v;

	if (check_state(s, t, &v))
		reject(v.code, v.hint, "%s", v.msg);
}

/* the file lines of R are what its definitions at T declare (spec 2.4) */
int files_match(struct record *r, struct tree *t)
{
	struct record *x = record_copy(r);

	return !expand_files(x, t) && !strcmp(record_text(x, 1), record_text(r, 1));
}

/* the definitions of R as a FILES argument */
char *def_paths(struct record *r)
{
	struct string_list v = STRING_LIST_INIT_DUP;
	struct strbuf sb = STRBUF_INIT;
	size_t i;

	rvals(r, "file", &v);
	for (i = 0; i < v.nr; i++)
		if (ends_with(v.items[i].string, " def"))
			strbuf_addf(&sb, "%s%.*s", sb.len ? "," : "", (int)strcspn(v.items[i].string, " "), v.items[i].string);
	return strbuf_detach(&sb, NULL);
}

/* the values of KEY, without the pinned object, joined by SEP: the scope as an argument */
char *rjoin(struct record *r, const char *key, const char *sep)
{
	struct string_list v = STRING_LIST_INIT_DUP;
	struct strbuf sb = STRBUF_INIT;
	size_t i;

	rvals(r, key, &v);
	for (i = 0; i < v.nr; i++)
		strbuf_addf(&sb, "%s%.*s", i ? sep : "", (int)strcspn(v.items[i].string, " "), v.items[i].string);
	return strbuf_detach(&sb, NULL);
}

/* spec 3.2: PATH:S-E with S and E as [1-9][0-9]* and S <= E */
static int is_range(const char *arg)
{
	const char *colon = strrchr(arg, ':');
	char *path = xstrndup(arg, colon - arg);
	int s, e, n = 0;

	return path_ok(path) && colon[1] != '0' && sscanf(colon + 1, "%d-%d%n", &s, &e, &n) == 2 &&
	       !colon[1 + n] && s > 0 && s <= e && colon[1 + strcspn(colon + 1, "-") + 1] != '0' &&
	       strspn(colon + 1, "0123456789-") == strlen(colon + 1);
}

/* spec 3.3-3.4: fix a scope argument at tree T */
char *pin(const char *arg, struct tree *t, int allow_new, int allow_abs)
{
	char *dir = xstrndup(arg, strlen(arg) - (*arg && arg[strlen(arg) - 1] == '/'));
	const char *rest;
	struct entry *e;

	if (skip_prefix(arg, "new:", &rest) && allow_new && path_ok(rest) && !tree_get(t, rest))
		return xstrdup(arg);
	if (!strcmp(arg, "/") && allow_abs)
		return xstrfmt("/ %s", t->oid);
	if (strchr(arg, ':') && !starts_with(arg, "new:") && is_range(arg))
		unsupported("line ranges");
	e = path_ok(dir) ? tree_get(t, dir) : NULL;
	if (e && strcmp(dir, arg) && allow_abs && !strcmp(e->mode, "040000"))
		return xstrfmt("%s %s", arg, e->oid);
	if (e && !strcmp(dir, arg) && blob_at(t, arg))
		return xstrfmt("%s %s", arg, e->oid);
	reject(128, e || starts_with(arg, "new:") || !allow_new ? NULL :
	       xstrfmt("if it does not exist yet, use 'new:%s'", arg), "bad scope: '%s'", arg);
}

/* spec 3.8 */
char *show(const char *scope, struct tree *t, int current)
{
	const char *rest, *oid = strchr(scope, ' ');
	char *path;

	if (skip_prefix(scope, "new:", &rest))
		return xstrfmt("%s%s", scope, blob_at(t, rest) ? " ?exists" : "");
	path = xstrndup(scope, oid - scope);
	if (path[strlen(path) - 1] == '/')
		return path;
	if (!blob_at(t, path))
		return xstrfmt("%s ?missing", path);
	return current && strcmp(blob_at(t, path), oid + 1) ? xstrfmt("%s ?changed", path) : path;
}

char *shows(struct record *r, const char *key, struct tree *t, int current)
{
	struct string_list v = STRING_LIST_INIT_DUP;
	struct strbuf sb = STRBUF_INIT;
	size_t i;

	rvals(r, key, &v);
	for (i = 0; i < v.nr; i++)
		strbuf_addf(&sb, "%s%s", i ? "," : "", show(v.items[i].string, t, current));
	return strbuf_detach(&sb, NULL);
}

/* the provider of R can be found in T: each scope is a file there (spec 1.5c, 7.7.3-4) */
int code_found(struct record *r, struct tree *t)
{
	struct string_list v = STRING_LIST_INIT_DUP;
	size_t i;

	rvals(r, "provider", &v);
	for (i = 0; i < v.nr; i++) {
		const char *p = v.items[i].string;

		skip_prefix(p, "new:", &p);
		if (p[strcspn(p, " ") - 1] == '/' || !blob_at(t, xstrndup(p, strcspn(p, " "))))
			return 0;
	}
	return 1;
}

/* spec 1.5b: the change A to B touches the provider of current R, found in A */
int touches(struct record *r, struct tree *a, struct tree *b)
{
	struct string_list v = STRING_LIST_INIT_DUP;
	size_t i;

	if (r && is_current(r))
		rvals(r, "provider", &v);
	for (i = 0; i < v.nr; i++) {
		char *path = xstrndup(v.items[i].string, strcspn(v.items[i].string, " "));
		const char *x = blob_at(a, path), *y = blob_at(b, path);

		if (x && (!y || strcmp(x, y)))
			return 1;
	}
	return 0;
}

/* the body lines of the definition PATH and its "$ ARG..." line, each after INDENT (spec 7.8.2) */
char *def_text(struct tree *t, const char *path, const char *indent)
{
	struct string_list kv = STRING_LIST_INIT_DUP, body = STRING_LIST_INIT_DUP;
	struct strbuf sb = STRBUF_INIT;
	size_t k;

	def_load(t, path, &kv);
	string_list_split(&body, yget(&kv, "body"), "\n", -1);
	for (k = 0; k < body.nr; k++)
		if (k + 1 < body.nr || *body.items[k].string)
			strbuf_addf(&sb, "%s%s\n", indent, body.items[k].string);
	for (k = 0; yget(&kv, xstrfmt("command.%d", (int)k)); k++)
		strbuf_addf(&sb, "%s%s", k ? " " : xstrfmt("%s$ ", indent), yget(&kv, xstrfmt("command.%d", (int)k)));
	if (k)
		strbuf_addch(&sb, '\n');
	return strbuf_detach(&sb, NULL);
}

/* spec 5.4: hash of the provider and file lines in record order */
char *pins(struct record *r)
{
	struct strbuf sb = STRBUF_INIT;
	size_t i;

	record_text(r, 1);
	for (i = 0; i < r->lines.nr; i++)
		if (starts_with(r->lines.items[i].string, "provider ") ||
		    starts_with(r->lines.items[i].string, "file "))
			strbuf_addf(&sb, "%s\n", r->lines.items[i].string);
	return line1(git_run(sb.buf, NULL, NULL, "hash-object --stdin"));
}

/* spec 1.5a */
int unverified(struct record *r, struct tree *t, struct string_list *checks)
{
	struct string_list v = STRING_LIST_INIT_DUP;
	size_t i;

	if (!string_list_has_string(checks, xstrfmt("%s %s", r->id, pins(r))) || !files_match(r, t))
		return 1;
	rvals(r, "provider", &v);
	for (i = 0; i < v.nr; i++) {
		const char *oid = strchr(v.items[i].string, ' ');
		const char *now = oid ? blob_at(t, xstrndup(v.items[i].string, oid - v.items[i].string)) : NULL;

		if (!now || strcmp(now, oid + 1))
			return 1;
	}
	return 0;
}

/* the confirmed pins: every "Ait-Check: ID reported|forced PINS" in HEAD's history */
void checks_load(struct string_list *checks)
{
	struct string_list lines = STRING_LIST_INIT_DUP;
	size_t i;

	if (!head)
		return;
	string_list_split(&lines, git("log --format=%(trailers:key=Ait-Check,valueonly,separator=%x0a) %s", head),
			  "\n", -1);
	for (i = 0; i < lines.nr; i++) {
		char id[48], how[16], p[48];

		if (sscanf(lines.items[i].string, "%47s %15s %47s", id, how, p) == 3)
			string_list_insert(checks, xstrfmt("%s %s", id, p));
	}
}

/* spec 2.4: the file lines are the defs and what the defs declare, pinned at T */
int expand_files(struct record *r, struct tree *t)
{
	struct string_list files = STRING_LIST_INIT_DUP, defs = STRING_LIST_INIT_DUP;
	struct string_list out = STRING_LIST_INIT_DUP;
	size_t i, j;

	rvals(r, "file", &files);
	for (i = 0; i < files.nr; i++)
		if (ends_with(files.items[i].string, " def"))
			string_list_insert(&defs, xstrndup(files.items[i].string,
							   strcspn(files.items[i].string, " ")));
	for (i = 0; i < defs.nr; i++) {
		struct string_list kv = STRING_LIST_INIT_DUP;
		struct entry *e = tree_get(t, defs.items[i].string);

		if (!blob_at(t, defs.items[i].string))
			return -1;
		string_list_append_nodup(&out, xstrfmt("%s %s %s def", e->path, e->mode, e->oid));
		def_load(t, e->path, &kv);
		for (j = 0; j < kv.nr; j++) {
			const char *in = kv.items[j].util;

			if (!starts_with(kv.items[j].string, "inputs.") || string_list_has_string(&defs, in))
				continue;
			if (!path_ok(in))
				reject(128, NULL, "bad format: '%s': bad input '%s'", e->path, in);
			if (!blob_at(t, in))
				return -1;
			e = tree_get(t, in);
			string_list_append_nodup(&out, xstrfmt("%s %s %s input", e->path, e->mode, e->oid));
			e = tree_get(t, defs.items[i].string);
		}
	}
	rset(r, "file", &out);
	record_text(r, 1);
	for (i = 1; i < r->lines.nr; i++)
		if (!strcmp(r->lines.items[i].string, r->lines.items[i - 1].string))
			return -1;
	return 0;
}

/* the paths of the contract: records and the spec files of live contracts (spec 6.1) */
int contract_path(struct state *s, const char *path)
{
	size_t i;

	if (starts_with(path, ".ait/"))
		return 1;
	for (i = 0; i < s->nr; i++) {
		struct string_list f = STRING_LIST_INIT_DUP;
		size_t j;

		if (is_live(s->r[i]))
			rvals(s->r[i], "file", &f);
		for (j = 0; j < f.nr; j++)
			if (starts_with(f.items[j].string, path) && f.items[j].string[strlen(path)] == ' ')
				return 1;
	}
	return 0;
}

static char *scalar(const char *v, const char *what)
{
	struct strbuf sb = STRBUF_INIT;
	char q = *v;

	if (q != '"' && q != '\'') {
		strbuf_add(&sb, v, strstr(v, " #") ? (size_t)(strstr(v, " #") - v) : strlen(v));
		strbuf_rtrim(&sb);
		return strbuf_detach(&sb, NULL);
	}
	for (v++; *v && (*v != q || (q == '\'' && v[1] == '\'')); v++) {
		if ((q == '"' && *v == '\\' && (v[1] == '"' || v[1] == '\\')) || (q == '\'' && *v == '\''))
			v++;
		strbuf_addch(&sb, *v);
	}
	if (*v != q || (v[1] && v[strspn(v + 1, " ") + 1] != '#'))
		reject(128, NULL, "bad format: '%s': unterminated or trailing text in '%s'", what, v);
	return strbuf_detach(&sb, NULL);
}

static void put(struct string_list *kv, char *key, char *val, const char *what)
{
	if (unsorted_string_list_has_string(kv, key))
		reject(128, NULL, "bad format: '%s': duplicate key '%s'", what, key);
	string_list_append(kv, key)->util = val;
}

/*
 * The YAML subset of spec 2.5.1, flattened into KV: "a: x" is "a", list items
 * are "a.0", "a.1", and maps inside list items "a.0.b". Values are scalars.
 */
void yaml(const char *text, struct string_list *kv, const char *what)
{
	struct string_list lines = STRING_LIST_INIT_DUP;
	struct { int indent, items, map_only; char *prefix; } stack[16] = { { -1, 0, 1, NULL } };
	int sp = 0;
	size_t i;

	string_list_split(&lines, text, "\n", -1);
	for (i = 0; i < lines.nr; i++) {
		char *line = lines.items[i].string, *key, *val, *prefix;
		int indent = strspn(line, " "), item = line[indent] == '-' && (!line[indent + 1] || line[indent + 1] == ' ');

		if (strchr(line, '\t') || strchr(line, '\r'))
			reject(128, NULL, "bad format: '%s': tab or CR in line %d", what, (int)i + 1);
		if (!line[indent] || line[indent] == '#')
			continue;
		while (sp && (indent < stack[sp].indent || (indent == stack[sp].indent && !(item && !stack[sp].map_only))))
			sp--;
		prefix = stack[sp].prefix;
		line += indent;
		if (item) {
			prefix = xstrfmt("%s.%d", prefix, stack[sp].items++);
			line += line[1] ? 2 : 1;
			indent += 2;
			if (!line[strspn(line, "abcdefghijklmnopqrstuvwxyz_")] || line[strspn(line, "abcdefghijklmnopqrstuvwxyz_")] != ':') {
				put(kv, prefix, scalar(line, what), what);
				continue;
			}
			stack[++sp].indent = indent - 2;
			stack[sp].map_only = 1;
			stack[sp].prefix = prefix;
			stack[sp].items = 0;
		}
		val = strchr(line, ':');
		if (!val || (val[1] && val[1] != ' ') || val == line || sp >= 15)
			reject(128, NULL, "bad format: '%s': not a key in line %d", what, (int)i + 1);
		key = prefix ? xstrfmt("%s.%.*s", prefix, (int)(val - line), line) : xstrndup(line, val - line);
		val += 1 + strspn(val + 1, " ");
		if (!*val || *val == '#') {
			put(kv, key, xstrdup(""), what);
			stack[++sp].indent = indent;
			stack[sp].map_only = 0;
			stack[sp].prefix = key;
			stack[sp].items = 0;
		} else if (!strcmp(val, "|")) {
			struct strbuf sb = STRBUF_INIT;
			int block = -1;

			for (; i + 1 < lines.nr; i++) {
				char *l = lines.items[i + 1].string;
				int in = strspn(l, " ");

				if (l[in] && in <= indent)
					break;
				if (l[in] && block < 0)
					block = in;
				strbuf_addf(&sb, "%s\n", (int)strlen(l) > block && block >= 0 ? l + block : "");
			}
			while (sb.len > 1 && sb.buf[sb.len - 2] == '\n')
				strbuf_setlen(&sb, sb.len - 1);
			put(kv, key, block < 0 ? xstrdup("") : strbuf_detach(&sb, NULL), what);
		} else if (*val == '[' && val[strlen(val) - 1] == ']') {
			struct string_list items = STRING_LIST_INIT_DUP;
			size_t j;

			if (val[1 + strspn(val + 1, " ")] != ']')
				string_list_split(&items, xstrndup(val + 1, strlen(val) - 2), ",", -1);
			for (j = 0; j < items.nr; j++)
				put(kv, xstrfmt("%s.%d", key, (int)j),
				    scalar(items.items[j].string + strspn(items.items[j].string, " "), what), what);
		} else if (strchr("[{&*!|>", *val)) {
			reject(128, NULL, "bad format: '%s': unsupported YAML in line %d", what, (int)i + 1);
		} else {
			put(kv, key, scalar(val, what), what);
		}
	}
}

const char *yget(struct string_list *kv, const char *key)
{
	struct string_list_item *item = unsorted_string_list_lookup(kv, key);

	return item ? item->util : NULL;
}

/* spec 2.5.2, 2.5.4, 2.5.6: a spec definition and the keys it may use */
void def_load(struct tree *t, const char *path, struct string_list *kv)
{
	const char *engine;
	size_t i;

	yaml(git("cat-file blob %s", blob_at(t, path)), kv, path);
	engine = yget(kv, "engine") ? yget(kv, "engine") : "";
	if (yget(kv, "environment"))
		unsupported("environment");
	if (!yget(kv, "body") || !*yget(kv, "body") || (*engine && strcmp(engine, "exec")) ||
	    (*engine && !yget(kv, "command.0")))
		reject(128, NULL, "bad format: '%s': needs 'body', and 'command' with 'engine: exec'", path);
	for (i = 0; i < kv->nr; i++) {
		char *k = xstrndup(kv->items[i].string, strcspn(kv->items[i].string, "."));

		if (!strstr(*engine ? " body engine command success inputs " : " body engine inputs ",
			    xstrfmt(" %s ", k)))
			reject(128, NULL, "bad format: '%s': unknown key '%s'", path, k);
	}
}
