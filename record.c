/*
 * Trees, contract records and the judgements computed from them (spec 2, 3, 1.5a).
 * Records are stored facts; everything else here is computed (rule 1).
 */
#include "ait.h"
#include "quote.h"
#include "utf8.h"
#include <regex.h>

static int cmp_entry(const void *a, const void *b)
{
	return strcmp(((const struct entry *)a)->path, ((const struct entry *)b)->path);
}

/*
 * One sorted listing per tree: lookups are a bsearch, a diff is a merge walk.
 * Callers name trees by object ID; a commit and its tree share one listing.
 */
struct tree *tree_load(const char *treeish)
{
	static struct string_list named = STRING_LIST_INIT_DUP, listed = STRING_LIST_INIT_DUP;
	struct string_list_item *hit = string_list_lookup(&named, treeish);
	struct tree *t;
	size_t alloc = 0;
	char *p, *end, oid[48];

	if (hit)
		return hit->util;
	if (!object(xstrfmt("%s^{tree}", treeish), 0, oid, NULL))
		reject(128, NULL, "git failed: not a tree: '%s'", treeish);
	hit = string_list_lookup(&listed, oid);
	if (hit) {
		string_list_insert(&named, treeish)->util = hit->util;
		return hit->util;
	}
	t = xcalloc(1, sizeof(*t));
	strcpy(t->oid, oid);
	string_list_insert(&named, treeish)->util = t;
	string_list_insert(&listed, oid)->util = t;
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

struct tree *head_tree(void)
{
	return tree_load(head ? head : "4b825dc642cb6eb9a060e54bf8d69288fbee4904");	/* the empty tree */
}

const char *blob_at(struct tree *t, const char *path)
{
	struct entry *e = tree_get(t, path);

	return e && strcmp(e->mode, "040000") && strcmp(e->mode, "160000") ? e->oid : NULL;
}

/* the text of a blob, or NULL when it holds a NUL and so is not text; each blob is read once */
const char *blob_text(const char *oid)
{
	static struct string_list blobs = STRING_LIST_INIT_DUP;
	struct string_list_item *hit = string_list_lookup(&blobs, oid);
	size_t size;
	char *text;

	if (!hit) {
		text = object(oid, 1, NULL, &size);
		hit = string_list_insert(&blobs, oid);
		hit->util = text && strlen(text) == size ? text : NULL;
	}
	return hit->util;
}

/* BASE plus INDEX_INFO ("MODE OID\tPATH" lines) in a throwaway index (spec 4.8) */
char *tree_with(const char *base, const char *index_info)
{
	char *dir = xstrfmt("%s/ait-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
	char *index, *tree = NULL;
	int rc;

	if (!*index_info)
		return xstrdup(base);
	if (!mkdtemp(dir))
		die_errno("could not create '%s'", dir);
	index = xstrfmt("%s/index", dir);
	git_run(NULL, index, &rc, "read-tree %s", base);
	if (!rc)
		git_run(index_info, index, &rc, "update-index --add --index-info");
	if (!rc)
		tree = line1(git_run(NULL, index, &rc, "write-tree"));
	unlink(index);
	rmdir(dir);
	if (rc)
		reject(128, NULL, "git failed: %s", git_error);
	return tree;
}

/* the files whose blob or mode differ between A and B; a file that became a directory is gone */
void tree_diff(struct tree *a, struct tree *b, struct string_list *paths)
{
	size_t i = 0, j = 0;

	while (i < a->nr || j < b->nr) {
		int c = i == a->nr ? 1 : j == b->nr ? -1 : strcmp(a->e[i].path, b->e[j].path);
		struct entry *x = NULL, *y = NULL;

		if (c <= 0)
			x = &a->e[i++];
		if (c >= 0)
			y = &b->e[j++];
		if (x && !strcmp(x->mode, "040000"))
			x = NULL;
		if (y && !strcmp(y->mode, "040000"))
			y = NULL;
		if ((x || y) && (!x || !y || strcmp(x->mode, y->mode) || strcmp(x->oid, y->oid)))
			string_list_append(paths, (x ? x : y)->path);
	}
}

/*
 * A scope value is "new:PATH", "PATH OID", "DIR/ OID" or "/ OID" (spec 2.2.2);
 * a file value is "PATH MODE OID def|input". Both start with their path.
 */
char *scope_path(const char *v)
{
	skip_prefix(v, "new:", &v);
	return xstrndup(v, strcspn(v, " "));
}

const char *scope_oid(const char *v)
{
	return strchr(v, ' ') ? strchr(v, ' ') + 1 : NULL;
}

/* WORD is one of the words of SET (NULL-terminated) */
int one_of(const char *word, const char *const *set)
{
	for (; *set; set++)
		if (!strcmp(word, *set))
			return 1;
	return 0;
}

int is_def(const char *file)
{
	return ends_with(file, " def");
}

/* the canonical key order of spec 2.2.2 */
static const char *keys[NKEYS] = { "role", "state", "consumer", "provider", "target", "file", "creator", "prev" };

const char *val(struct record *r, enum key k)
{
	return r->f[k].nr ? r->f[k].items[0].string : "";
}

void put(struct record *r, enum key k, const char *v)
{
	string_list_clear(&r->f[k], 0);
	string_list_append(&r->f[k], v);
}

int in_state(struct record *r, const char *state)
{
	return !strcmp(val(r, STATE), state);
}

/* spec 1.5c: a provider of "/" or "DIR/" */
int is_abstract(struct record *r)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &r->f[PROVIDER])
		if (ends_with(scope_path(item->string), "/"))
			return 1;
	return 0;
}

int is_live(struct record *r)
{
	return *r->id != 'u' && (in_state(r, "future") || in_state(r, "current"));
}

/* a readable link that is undertaken: its demand and offer exist (state_load marks it bad otherwise) */
int undertaken(struct record *l)
{
	return *l->id == 'u' && !l->bad && in_state(l, "undertaken");
}

/* the other end of L when L is a readable undertaken link with R at one end, else NULL */
struct record *partner(struct state *s, struct record *l, struct record *r)
{
	if (!l || !undertaken(l) || (link_end(s, l, 0) != r && link_end(s, l, 1) != r))
		return NULL;
	return link_end(s, l, link_end(s, l, 0) == r);
}

int is_current(struct record *r)
{
	return *r->id != 'u' && in_state(r, "current");
}

/* R was current in one of its earlier versions, which its prev chain names (2.2.4): it was reopened (1.5d) */
int reopened(struct record *r)
{
	const char *prev = val(r, PREV), *text;

	while (strcmp(prev, "-") && (text = blob_text(prev))) {
		if (strstr(text, "\nstate current\n"))
			return 1;
		prev = strstr(text, "\nprev ") ? line1(xstrdup(strstr(text, "\nprev ") + 6)) : "-";
	}
	return 0;
}

/* current in HEAD: only such a provider shows "?changed" (spec 3.8) */
int was_current(struct record *r)
{
	return r->old && is_current(r->old);
}

/* within a key, values sort bytewise with "new:PATH" as PATH (spec 3.2a) */
static int cmp_value(const void *a, const void *b)
{
	const char *x = ((const struct string_list_item *)a)->string;
	const char *y = ((const struct string_list_item *)b)->string;

	skip_prefix(x, "new:", &x);
	skip_prefix(y, "new:", &y);
	return strcmp(x, y);
}

static void record_sort(struct record *r)
{
	int k;

	for (k = 0; k < NKEYS; k++)
		QSORT(r->f[k].items, r->f[k].nr, cmp_value);
}

/* the canonical lines of R (2.2.1) for the keys in KEYS (a bit per enum key) */
char *record_text(struct record *r, unsigned keys_wanted)
{
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;
	int k;

	record_sort(r);
	for (k = 0; k < NKEYS; k++)
		for_each_string_list_item(item, &r->f[k])
			if (keys_wanted & (1u << k))
				strbuf_addf(&sb, "%s %s\n", keys[k], item->string);
	return strbuf_detach(&sb, NULL);
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
	int k;

	snprintf(r->id, sizeof(r->id), "%s", id);
	for (k = 0; k < NKEYS; k++)
		string_list_init_dup(&r->f[k]);
	ALLOC_GROW(s->r, s->nr + 1, s->alloc);
	s->r[s->nr++] = r;
	return r;
}

struct record *record_copy(struct record *r)
{
	struct record *c = xmalloc(sizeof(*c));
	struct string_list_item *item;
	int k;

	*c = *r;
	for (k = 0; k < NKEYS; k++) {
		string_list_init_dup(&c->f[k]);
		for_each_string_list_item(item, &r->f[k])
			string_list_append(&c->f[k], item->string);
	}
	c->old = NULL;
	return c;
}

/* a link "undertake/D…-O…" names its demand and its offer */
struct record *link_end(struct state *s, struct record *l, int offer)
{
	return state_get(s, offer ? l->id + 24 : xstrndup(l->id + 10, 13));
}

/* spec 2.2.1: a stored record reads back as exactly its canonical text */
static void record_parse(struct record *r, const char *text)
{
	struct string_list lines = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	int k;

	string_list_split(&lines, text, "\n", -1);
	for_each_string_list_item(item, &lines) {
		const char *l = item->string, *v;

		if (!*l && item == lines.items + lines.nr - 1)
			break;
		for (k = 0; k < NKEYS && !(skip_prefix(l, keys[k], &v) && *v == ' '); k++)
			;
		if (k == NKEYS || !v[1] || isspace(v[1]) || isspace(l[strlen(l) - 1]) ||
		    l[strcspn(l, "\r\t\x7f\x01\x02\x03\x04\x05\x06\x07\x08\x0b\x0c\x0e\x0f\x10\x11\x12"
			     "\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f")]) {
			r->bad = xstrfmt("bad line '%s'", l);
			return;
		}
		string_list_append(&r->f[k], v + 1);
	}
	if (strcmp(record_text(r, ALL_KEYS), text))
		r->bad = xstrdup("not in canonical form");
}

/* spec 1.9 and 2.1: "D…", "O…" or "undertake/D…-O…" */
static int id_ok(const char *id)
{
	regex_t re;
	int ok;

	regcomp(&re, "^([DO][0-9a-f]{12}|undertake/D[0-9a-f]{12}-O[0-9a-f]{12})$", REG_EXTENDED | REG_NOSUB);
	ok = !regexec(&re, id, 0, NULL, 0);
	regfree(&re);
	return ok;
}

static char *record_check(struct record *r);

/* the records of T; a malformed one carries why in ->bad (spec 5.9 a-c, 5.10) */
void state_load(struct state *s, struct tree *t)
{
	size_t i;

	for (i = 0; i < t->nr; i++) {
		struct entry *e = &t->e[i];
		const char *id, *text;
		struct record *r;

		if (!skip_prefix(e->path, ".ait/", &id) || !strcmp(e->mode, "040000"))
			continue;
		if (*id == 'X')
			unsupported("drop record");
		text = strcmp(e->mode, "100644") ? NULL : blob_text(e->oid);
		r = state_add(s, id);
		/* 2.1.2a: the init file is no record; only its one line is checked, and it stays as T has it */
		if (!strcmp(id, "init"))
			r->bad = text && !strcmp(text, "version 1\n") ? NULL : xstrdup("must be the single line 'version 1'");
		else if (strcmp(e->mode, "100644") || !id_ok(id))
			r->bad = xstrdup("not a record path");
		else if (!text)
			r->bad = xstrdup("not a text file");
		else
			record_parse(r, text);
		if (!r->bad && strcmp(id, "init"))
			r->bad = record_check(r);
		r->old = record_copy(r);
	}
	for (i = 0; i < s->nr; i++) {
		struct record *r = s->r[i], *d, *o;

		if (*r->id != 'u' || r->bad)
			continue;
		d = link_end(s, r, 0);
		o = link_end(s, r, 1);
		if (!d || !o || *d->id != 'D' || *o->id != 'O')
			r->bad = r->old->bad = xstrdup("its demand or offer does not exist");
	}
}

static int oid_ok(const char *s)
{
	return s && strlen(s) == 40 && strspn(s, "0123456789abcdef") == 40;
}

/* spec 3.1 */
int path_ok(const char *p)
{
	struct string_list parts = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	int ok = *p && *p != '/' && *p != '-' && strcmp(p, ".ait") && !starts_with(p, ".ait/");
	const char *c;

	for (c = p; *c; c++)
		ok &= *c != ' ' && *c != ':' && *c != ',' && (unsigned char)*c >= 0x20 && *c != 0x7f;
	string_list_split(&parts, p, "/", -1);
	for_each_string_list_item(item, &parts)
		ok &= *item->string && strcmp(item->string, ".") && strcmp(item->string, "..");
	string_list_clear(&parts, 0);
	return ok;
}

/* a stored scope value of spec 2.2.2; abstract and new scopes only where the caller allows them */
static int scope_ok(const char *v, int allow_new, int allow_abs)
{
	char *path = scope_path(v);

	if (starts_with(v, "new:"))
		return allow_new && path_ok(path) && !scope_oid(v);
	if (!oid_ok(scope_oid(v)))
		return 0;
	if (ends_with(path, "/"))
		return allow_abs && (!strcmp(path, "/") || path_ok(xstrndup(path, strlen(path) - 1)));
	return path_ok(path);
}

/* spec 7.2.1: an agent name, [A-Za-z0-9._-]{1,64} and not "unknown" */
int agent_ok(const char *name)
{
	return *name && strlen(name) <= 64 && strcmp(name, "unknown") &&
	       !name[strspn(name, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-")];
}

static int value_ok(struct record *r, enum key k, const char *v)
{
	char a[4096], b[4096], c[4096], d[4096];
	int n = sscanf(v, "%4095s %4095s %4095s %4095s", a, b, c, d);

	switch (k) {
	case CONSUMER:
		return scope_ok(v, 1, 0);
	case PROVIDER:
		return scope_ok(v, !is_current(r), !is_current(r));
	case FILES:
		return n == 4 && path_ok(a) && (!strcmp(b, "100644") || !strcmp(b, "100755")) && oid_ok(c) &&
		       (!strcmp(d, "def") || !strcmp(d, "input")) && !strcmp(v, xstrfmt("%s %s %s %s", a, b, c, d));
	case CREATOR:
		/* 2.2.2: "AGENT SID", or "unknown -" for a record an integration made without a session */
		return n == 2 && !strcmp(v, xstrfmt("%s %s", a, b)) && (!strcmp(v, "unknown -") ||
		       (agent_ok(a) && *b == 'S' && strlen(b) == 13 && strspn(b + 1, "0123456789abcdef") == 12));
	case PREV:
		return !strcmp(v, "-") || oid_ok(v);
	default:
		return 1;
	}
}

static int fail_with(struct verdict *v, int code, const char *word, char *hint, char *msg)
{
	*v = (struct verdict){ code, word, hint, msg, NULL, NULL };
	return code;
}

static int bad_record(struct verdict *v, struct record *r, const char *why)
{
	return fail_with(v, 128, "format", xstrfmt("records are written only by ait; restore '.ait/%s' as in the "
						   "last commit where it was valid, then commit that alone as "
						   "spec/ait-mini.md says", r->id),
			 xstrfmt("bad format: '.ait/%s': %s", r->id, why));
}

static const char *const contract_states[] = { "future", "current", "cancelled", "withdrawn", NULL };
static const char *const link_states[] = { "undertaken", "released", NULL };

/* spec 5.9 a-c for one record: how many values each key has, each value, role and state; NULL when it holds */
static char *record_check(struct record *r)
{
	/* how many values each key takes, per kind: '1' one, '+' one or more, '0' none */
	static const char *counts[] = { "11++0+1+", "110+0+1+", "01000001" };
	const char *c = counts[*r->id == 'D' ? 0 : *r->id == 'O' ? 1 : 2];
	struct string_list_item *item;
	int k;

	for (k = 0; k < NKEYS; k++) {
		if (c[k] == '+' ? !r->f[k].nr : r->f[k].nr != (size_t)(c[k] - '0'))
			return xstrfmt("wrong number of '%s' lines", keys[k]);
		for_each_string_list_item(item, &r->f[k])
			if (!value_ok(r, k, item->string))
				return xstrfmt("bad '%s' line '%s'", keys[k], item->string);
	}
	if (strcmp(val(r, ROLE), *r->id == 'D' ? "demand" : *r->id == 'O' ? "offer" : "") ||
	    !one_of(val(r, STATE), *r->id == 'u' ? link_states : contract_states))
		return xstrdup("role or state does not fit the record");
	if (*r->id != 'u' && !strstr(record_text(r, 1u << FILES), " def\n"))
		return xstrdup("no spec definition");
	return NULL;
}

/* validate(state) of spec 5.9 a-e: invariants of one state, no parents involved; 0 when it holds */
int check_state(struct state *s, struct tree *t, struct verdict *v)
{
	size_t i;

	for (i = 0; i < s->nr; i++) {
		struct record *r = s->r[i], *d, *o;
		char *why = r->bad ? r->bad : strcmp(r->id, "init") ? record_check(r) : NULL;

		if (why)
			return bad_record(v, r, why);
		if (!strcmp(r->id, "init"))
			continue;
		if (in_state(r, "withdrawn"))
			return fail_with(v, 1, "state", NULL, xstrfmt("not allowed: '%s' is withdrawn without a drop", r->id));
		if (*r->id != 'u')
			continue;
		d = link_end(s, r, 0);
		o = link_end(s, r, 1);
		if (in_state(r, "undertaken") && is_current(o) && in_state(d, "future")) {
			fail_with(v, 1, "state", was_current(d) ?
				  xstrfmt("'%s' is undertaken by current '%s'; reopen them in the same request: "
					  "'ait future amend %s future amend %s -m <why>'", d->id, o->id, d->id, o->id) :
				  xstrfmt("undertake it with a future offer: 'ait future offer %s %s as o undertake %s o -m <why>'",
					  scope_args(d, PROVIDER), def_paths(d), d->id),
				  xstrfmt("not allowed: current '%s' undertakes future '%s'", o->id, d->id));
			v->k = d;
			v->o = o;
			return 1;
		}
	}
	for (i = 0; i < s->nr; i++)
		if (is_live(s->r[i]) && !files_match(s->r[i], t)) {
			fail_with(v, 1, "incomplete", xstrfmt("make the spec files of '%s' match it, or reopen it "
							       "with them: 'ait future amend %s -m <why>'",
							       s->r[i]->id, reopen_ids(s, s->r[i])),
				  xstrfmt("spec files do not match the contract: '%s'", s->r[i]->id));
			v->k = s->r[i];
			return 1;
		}
	return 0;
}

/* why R cannot be shown from T: malformed, or a definition it declares is not a text file in T (NULL when it can) */
const char *unusable(struct record *r, struct tree *t)
{
	struct string_list_item *item;

	if (r->bad)
		return r->bad;
	for_each_string_list_item(item, &r->f[FILES])
		if (is_live(r) && is_def(item->string) && (!blob_at(t, scope_path(item->string)) ||
							   !blob_text(blob_at(t, scope_path(item->string)))))
			return xstrfmt("its definition '%s' is not there", scope_path(item->string));
	return NULL;
}

/* spec 5.10: a malformed record cannot be read; whoever needs it stops with its error */
struct record *readable(struct record *r)
{
	struct verdict v;

	if (r->bad && bad_record(&v, r, r->bad))
		reject(v.code, v.hint, "%s", v.msg);
	return r;
}

/* the first malformed record of S, as a verdict (5.10) */
int malformed(struct state *s, struct verdict *v)
{
	size_t i;

	for (i = 0; i < s->nr; i++)
		if (s->r[i]->bad)
			return bad_record(v, s->r[i], s->r[i]->bad);
	return 0;
}

void validate(struct state *s, struct tree *t)
{
	struct verdict v;

	if (check_state(s, t, &v))
		reject(v.code, v.hint, "%s", v.msg);
}

/* the state that T holds is valid (spec 5.9) */
void validate_at(struct tree *t)
{
	struct state s = { 0 };

	state_load(&s, t);
	validate(&s, t);
}

/*
 * The paths of KEY joined by ',' as one helper argument, quoted for the shell
 * when it would read them (5.9a, git v2.53.0 quote.c sq_quote_buf_pretty());
 * DEFS_ONLY keeps only the definitions of FILES.
 */
static char *join_arg(struct record *r, enum key k, int defs_only)
{
	struct strbuf sb = STRBUF_INIT, arg = STRBUF_INIT;
	struct string_list_item *item;

	for_each_string_list_item(item, &r->f[k])
		if (!defs_only || is_def(item->string))
			strbuf_addf(&sb, "%s%.*s", sb.len ? "," : "", (int)strcspn(item->string, " "), item->string);
	sq_quote_buf_pretty(&arg, sb.buf);
	strbuf_release(&sb);
	return strbuf_detach(&arg, NULL);
}

char *scope_args(struct record *r, enum key k)
{
	return r->f[k].nr ? join_arg(r, k, 0) : xstrdup("");
}

char *def_paths(struct record *r)
{
	return join_arg(r, FILES, 1);
}

/* spec 3.2: a well-formed PATH:S-E, the line range ait-mini leaves out; a malformed one is a bad scope */
void no_range(const char *arg)
{
	regex_t re;
	int range;

	regcomp(&re, "^[^:]+:([1-9][0-9]*)-([1-9][0-9]*)$", REG_EXTENDED | REG_NOSUB);
	range = !starts_with(arg, "new:") && !regexec(&re, arg, 0, NULL, 0) &&
		path_ok(xstrndup(arg, strchr(arg, ':') - arg)) &&
		atoi(strchr(arg, ':') + 1) <= atoi(strchr(arg, '-') + 1);
	regfree(&re);
	if (range)
		unsupported("line ranges");
}

/* spec 3.1-3.2: the shape of a scope argument, judged before any tree is read (5.13-1) */
void scope_shape(const char *arg)
{
	const char *p = arg;

	skip_prefix(arg, "new:", &p);
	no_range(arg);
	if (strcmp(p, "/") && !path_ok(ends_with(p, "/") ? xstrndup(p, strlen(p) - 1) : p))
		reject(128, NULL, "bad scope: '%s'", arg);
}

/* spec 3.3-3.4: fix a scope argument at tree T */
char *pin(const char *arg, struct tree *t, int allow_new, int allow_abs)
{
	char *dir = ends_with(arg, "/") ? xstrndup(arg, strlen(arg) - 1) : xstrdup(arg);
	const char *rest;
	struct entry *e;

	if (skip_prefix(arg, "new:", &rest) && allow_new && path_ok(rest) && !tree_get(t, rest))
		return xstrdup(arg);
	if (!strcmp(arg, "/") && allow_abs)
		return xstrfmt("/ %s", t->oid);
	no_range(arg);
	e = path_ok(dir) ? tree_get(t, dir) : NULL;
	if (e && ends_with(arg, "/") && allow_abs && !strcmp(e->mode, "040000"))
		return xstrfmt("%s %s", arg, e->oid);
	if (e && !ends_with(arg, "/") && blob_at(t, arg))
		return xstrfmt("%s %s", arg, e->oid);
	reject(128, e || starts_with(arg, "new:") || !allow_new ? NULL :
	       xstrfmt("if it does not exist yet, use 'new:%s'", arg), "bad scope: '%s'", arg);
}

/* spec 3.8: where a scope is in T; "?changed" only for the provider of a current contract */
char *show(const char *scope, struct tree *t, int current)
{
	char *path = scope_path(scope);
	const char *now = blob_at(t, path);

	if (starts_with(scope, "new:"))
		return xstrfmt("%s%s", scope, now ? " ?exists" : "");
	if (ends_with(path, "/"))
		return path;
	if (!now)
		return xstrfmt("%s ?missing", path);
	return current && strcmp(now, scope_oid(scope)) ? xstrfmt("%s ?changed", path) : path;
}

char *shows(struct record *r, enum key k, struct tree *t, int current)
{
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;

	for_each_string_list_item(item, &r->f[k])
		strbuf_addf(&sb, "%s%s", sb.len ? "," : "", show(item->string, t, current));
	return strbuf_detach(&sb, NULL);
}

/* KEPT,<new scope> of 5.9a: the pieces of R's provider that T still has, then the new place */
char *moved_arg(struct record *r, struct tree *t)
{
	struct strbuf kept = STRBUF_INIT, arg = STRBUF_INIT;
	struct string_list_item *item;

	for_each_string_list_item(item, &r->f[PROVIDER])
		if (blob_at(t, scope_path(item->string)))
			strbuf_addf(&kept, "%s%s", kept.len ? "," : "", scope_path(item->string));
	if (kept.len) {
		sq_quote_buf_pretty(&arg, kept.buf);
		strbuf_addch(&arg, ',');
	}
	strbuf_addstr(&arg, "<new scope>");
	return strbuf_detach(&arg, NULL);
}

/*
 * spec 5.9a and 5.9 d: what reopening K reopens with it, as HEAD has them: K,
 * and when K is a current demand, each current offer that undertakes it
 */
void reopen_set(struct state *s, struct record *k, struct string_list *out)
{
	size_t i;

	string_list_append(out, k->id)->util = k;
	for (i = 0; i < s->nr; i++) {
		struct record *o = partner(s, s->r[i]->old, k);

		if (*k->id == 'D' && k->old && is_current(k->old) && o && o->old && is_current(o->old))
			string_list_append(out, o->id)->util = o;
	}
}

/* "K future amend O..." of the reopen command run on its own */
char *reopen_ids(struct state *s, struct record *k)
{
	struct string_list set = STRING_LIST_INIT_DUP;
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;

	reopen_set(s, k, &set);
	for_each_string_list_item(item, &set)
		strbuf_addf(&sb, "%s%s", sb.len ? " future amend " : "", item->string);
	return strbuf_detach(&sb, NULL);
}

/* the provider of R can be found in T: each scope is a file there (spec 1.5c, 7.7.3-4) */
int code_found(struct record *r, struct tree *t)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &r->f[PROVIDER])
		if (!blob_at(t, scope_path(item->string)))
			return 0;
	return 1;
}

/* spec 1.5b: the change A to B touches the provider of current R, found in A */
int touches(struct record *r, struct tree *a, struct tree *b)
{
	struct string_list_item *item;

	if (!r || !is_current(r))
		return 0;
	for_each_string_list_item(item, &r->f[PROVIDER]) {
		const char *x = blob_at(a, scope_path(item->string)), *y = blob_at(b, scope_path(item->string));

		if (x && (!y || strcmp(x, y)))
			return 1;
	}
	return 0;
}

/*
 * spec 7.7.4: pin the scopes of KEY at T where their file is; NEW_ONLY turns
 * only "new:PATH" into "PATH OID", otherwise pinned scopes take T's blob
 */
void repin(struct record *r, enum key k, struct tree *t, int new_only)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &r->f[k]) {
		char *path = scope_path(item->string);

		if (blob_at(t, path) && starts_with(item->string, "new:") == new_only)
			item->string = xstrfmt("%s %s", path, blob_at(t, path));
	}
}

/* the body lines of a definition, each after INDENT */
void body_lines(struct strbuf *sb, struct string_list *kv, const char *indent)
{
	struct string_list body = STRING_LIST_INIT_DUP;
	struct string_list_item *item;

	string_list_split(&body, yget(kv, "body"), "\n", -1);
	for_each_string_list_item(item, &body)
		if (*item->string || item != body.items + body.nr - 1)
			strbuf_addf(sb, "%s%s\n", indent, item->string);
}

/* the body lines of the definition PATH and its "$ ARG..." line, each after INDENT (spec 7.8.2) */
char *def_text(struct tree *t, const char *path, const char *indent)
{
	struct string_list *kv = def_get(t, path);
	struct strbuf sb = STRBUF_INIT;
	const char *a;
	int k;

	body_lines(&sb, kv, indent);
	for (k = 0; (a = yget(kv, xstrfmt("command.%d", k))); k++)
		strbuf_addf(&sb, "%s%s", k ? " " : xstrfmt("%s$ ", indent), a);
	if (k)
		strbuf_addch(&sb, '\n');
	return strbuf_detach(&sb, NULL);
}

const char *engine_of(struct tree *t, const char *def)
{
	struct string_list *kv = def_get(t, def);

	return yget(kv, "engine") && *yget(kv, "engine") ? "exec" : "prompt";
}

/* spec 5.4: their hash, the PINS of an Ait-Check line; each text is hashed once */
char *pins(struct record *r)
{
	static struct string_list hashed = STRING_LIST_INIT_DUP;
	char *text = record_text(r, PIN_KEYS);
	struct string_list_item *hit = string_list_lookup(&hashed, text);

	if (!hit) {
		hit = string_list_insert(&hashed, text);
		hit->util = line1(git_run(text, NULL, NULL, "hash-object --stdin"));
	}
	return hit->util;
}

/* the confirmed pins: every "Ait-Check: ID reported|forced PINS" in HEAD's history, read when first asked */
static struct string_list *checks(void)
{
	static struct string_list confirmed = STRING_LIST_INIT_DUP;
	static int loaded;
	struct string_list lines = STRING_LIST_INIT_DUP;
	struct string_list_item *item;

	if (loaded++ || !head)
		return &confirmed;
	string_list_split(&lines, git("log --format=%(trailers:key=Ait-Check,valueonly,separator=%x0a) %s", head),
			  "\n", -1);
	/* spec 5.4: exactly "ID reported|forced PINS" */
	for_each_string_list_item(item, &lines) {
		static const char *const how[] = { "reported", "forced", NULL };
		char id[48], word[16], p[48], more;

		if (sscanf(item->string, "%47s %15s %47s %c", id, word, p, &more) == 3 && one_of(word, how) && oid_ok(p) &&
		    !strcmp(item->string, xstrfmt("%s %s %s", id, word, p)))
			string_list_insert(&confirmed, xstrfmt("%s %s", id, p));
	}
	return &confirmed;
}

/* spec 1.5a: not confirmed with these pins, or the code or spec files moved since */
int unverified(struct record *r, struct tree *t)
{
	struct string_list_item *item;

	if (!string_list_has_string(checks(), xstrfmt("%s %s", r->id, pins(r))) || !files_match(r, t))
		return 1;
	for_each_string_list_item(item, &r->f[PROVIDER]) {
		const char *now = blob_at(t, scope_path(item->string));

		if (!now || !scope_oid(item->string) || strcmp(now, scope_oid(item->string)))
			return 1;
	}
	return 0;
}

/* spec 2.5.2: an input of the definition DEF is a PATH of 3.1 */
const char *input_path(const char *def, const char *p)
{
	if (!path_ok(p))
		reject(128, NULL, "bad format: '%s': bad input '%s'", def, p);
	return p;
}

/* spec 2.4: the file lines are the defs and what the defs declare, pinned at T */
int expand_files(struct record *r, struct tree *t)
{
	struct string_list defs = STRING_LIST_INIT_DUP, out = STRING_LIST_INIT_DUP;
	struct string_list_item *item, *in;

	for_each_string_list_item(item, &r->f[FILES])
		if (is_def(item->string))
			string_list_insert(&defs, scope_path(item->string));
	for_each_string_list_item(item, &defs) {
		struct entry *e = tree_get(t, item->string);

		if (!blob_at(t, item->string))
			return -1;
		string_list_insert(&out, xstrfmt("%s %s %s def", e->path, e->mode, e->oid));
		for_each_string_list_item(in, def_get(t, e->path)) {
			const char *p = yget(def_get(t, e->path), in->string);

			if (!starts_with(in->string, "inputs.") || string_list_has_string(&defs, input_path(item->string, p)))
				continue;
			if (!blob_at(t, p))
				return -1;
			string_list_insert(&out, xstrfmt("%s %s %s input", p, tree_get(t, p)->mode, blob_at(t, p)));
		}
	}
	r->f[FILES] = out;
	return 0;
}

/* the file lines of R are what its definitions at T declare (spec 2.4) */
int files_match(struct record *r, struct tree *t)
{
	struct record *x = record_copy(r);

	return !expand_files(x, t) && !strcmp(record_text(x, 1u << FILES), record_text(r, 1u << FILES));
}

/* R is a future or current demand or offer that has PATH as a spec file */
int declares(struct record *r, const char *path)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &r->f[FILES])
		if (is_live(r) && !strcmp(scope_path(item->string), path))
			return 1;
	return 0;
}

/* the paths of the contract: records and the spec files of live contracts (spec 6.1) */
int contract_path(struct state *s, const char *path)
{
	size_t i;

	for (i = 0; i < s->nr; i++)
		if (declares(s->r[i], path))
			return 1;
	return starts_with(path, ".ait/");
}

/* a file ait reads as text: 0, or -1 when it cannot be read, 1 when it holds a NUL */
int read_text(struct strbuf *sb, const char *path)
{
	if (!strcmp(path, "-") ? strbuf_read(sb, 0, 0) < 0 : strbuf_read_file(sb, path, 0) < 0)
		return -1;
	return strlen(sb->buf) != sb->len;
}

/*
 * The YAML subset of spec 2.5.1, flattened into KV: "a: x" is "a", list items
 * are "a.0", "a.1", map keys inside them "a.0.b". Every value keeps where it
 * starts in the text and whether it holds a list or map.
 */
struct yval {
	char *s;
	size_t at;
	char kind;	/* 'l' a list, 'm' a map, '?' a key with nothing under it yet, 0 a scalar */
};

struct yaml {
	const char *text, *what;
	struct string_list *kv;
	char *err;
};

static int yaml_fail(struct yaml *y, const char *fmt, ...)
{
	struct strbuf sb = STRBUF_INIT;
	va_list ap;

	strbuf_addf(&sb, "bad format: '%s': ", y->what);
	va_start(ap, fmt);
	strbuf_vaddf(&sb, fmt, ap);
	va_end(ap);
	if (!y->err)
		y->err = strbuf_detach(&sb, NULL);
	return -1;
}

/* V holds a list ('l') or a map ('m'), never both */
static int yaml_kind(struct yaml *y, struct yval *v, char kind, const char *key)
{
	if (v->kind != '?' && v->kind != kind)
		return yaml_fail(y, "'%s' is both a list and a map", key);
	v->kind = kind;
	return 0;
}

static struct yval *yaml_put(struct yaml *y, char *key, char *s, const char *at)
{
	struct yval *v;

	if (unsorted_string_list_has_string(y->kv, key)) {
		yaml_fail(y, "duplicate key '%s'", key);
		return NULL;
	}
	v = xcalloc(1, sizeof(*v));
	v->s = s;
	v->at = at - y->text;
	string_list_append(y->kv, key)->util = v;
	return v;
}

/* a scalar at *P, quoted or plain up to a character of STOP, a comment or the line's end */
static int yaml_scalar(struct yaml *y, const char **p, const char *stop, char *key)
{
	struct strbuf sb = STRBUF_INIT;
	const char *v = *p, *at = v;
	char q = *v;

	if (q && strchr("&*!|>%@`", q))
		return yaml_fail(y, "unsupported YAML in '%s'", key);
	if (q != '"' && q != '\'') {
		while (*v && *v != '\n' && !strchr(stop, *v) && !(*v == '#' && v > at && v[-1] == ' '))
			strbuf_addch(&sb, *v++);
		strbuf_rtrim(&sb);
	} else {
		for (at = ++v; *v && *v != '\n' && (*v != q || (q == '\'' && v[1] == '\'')); v++) {
			if (q == '"' && *v == '\\' && v[1] != '"' && v[1] != '\\')
				return yaml_fail(y, "an escape other than \\\" or \\\\ in '%s'", key);
			if ((q == '"' && *v == '\\') || (q == '\'' && *v == '\''))
				v++;
			strbuf_addch(&sb, *v);
		}
		if (*v != q)
			return yaml_fail(y, "unterminated quote in '%s'", key);
		v++;
	}
	*p = v;
	return yaml_put(y, key, strbuf_detach(&sb, NULL), at) ? 0 : -1;
}

/* the blanks, line ends and comments between the items of a flow collection */
static const char *flow_gap(const char *p)
{
	for (;;) {
		p += strspn(p, " \n");
		if (*p != '#')
			return p;
		p = strchrnul(p, '\n');
	}
}

/* a flow list "[a, b]" or flow map "{k: v}" at *P, over as many lines as written, nested as deep */
static int yaml_flow(struct yaml *y, const char **p, char *key, int depth)
{
	char close = **p == '[' ? ']' : '}';
	struct yval *v = yaml_put(y, key, xstrdup(""), *p);
	int n;

	if (!v)
		return -1;
	if (depth > 15)
		return yaml_fail(y, "'%s' is nested too deeply", key);
	v->kind = close == ']' ? 'l' : 'm';
	*p = flow_gap(*p + 1);
	for (n = 0; **p != close; n++) {
		char *sub = xstrfmt("%s.%d", key, n);

		if (close == '}') {
			size_t len = strcspn(*p, ":,]}\n");

			if (!len || (*p)[len] != ':')
				return yaml_fail(y, "not a key in '%s'", key);
			sub = xstrfmt("%s.%.*s", key, (int)len, *p);
			*p = flow_gap(*p + len + 1);
		}
		if (**p == '[' || **p == '{' ? yaml_flow(y, p, sub, depth + 1) : yaml_scalar(y, p, ",]}", sub))
			return -1;
		*p = flow_gap(*p);
		if (**p == ',')
			*p = flow_gap(*p + 1);
		else if (**p != close)
			return yaml_fail(y, "expected ',' or '%c' in '%s'", close, key);
	}
	(*p)++;
	return 0;
}

/* the line after the one P stands on, when a flow collection went past NEXT; NO counts the lines */
static const char *flow_end(const char *p, const char *next, int *no)
{
	const char *c;

	if (p < next)
		return next;
	for (c = next; c < p; c++)
		*no += *c == '\n';
	c = strchrnul(p, '\n');
	return c + (*c == '\n');
}

/* blanks, then the line's end or a comment */
static int yaml_end(struct yaml *y, const char *p, int no)
{
	p += strspn(p, " ");
	return *p && *p != '\n' && *p != '#' ? yaml_fail(y, "trailing text in line %d", no) : 0;
}

int yaml_parse(const char *text, struct string_list *kv, const char *what, char **err)
{
	struct yaml y = { text, what, kv, NULL };
	struct { int indent, items, map_only; char *prefix; struct yval *v; } st[16] = { { -1, 0, 1, NULL, NULL } };
	const char *line, *next;
	int sp = 0, no = 0;

	if (!is_utf8(text))
		yaml_fail(&y, "not UTF-8");
	if (strpbrk(text, "\t\r"))
		yaml_fail(&y, "a tab or CR");
	for (line = text; *line && !y.err; line = next) {
		const char *p = line, *colon, *e = strchrnul(line, '\n');
		int indent = strspn(line, " "), item;
		char *key, *prefix;

		next = e + (*e == '\n');
		no++;
		p += indent;
		if (p == e || *p == '#')
			continue;
		item = *p == '-' && (p + 1 == e || p[1] == ' ');
		while (sp && (indent < st[sp].indent || (indent == st[sp].indent && (!item || st[sp].map_only))))
			sp--;
		if (st[sp].v && yaml_kind(&y, st[sp].v, item ? 'l' : 'm', st[sp].prefix))
			break;
		prefix = st[sp].prefix;
		if (item) {
			prefix = xstrfmt("%s.%d", prefix, st[sp].items++);
			p += p + 1 == e ? 1 : 2;
			indent += 2;
			if (p[strspn(p, "abcdefghijklmnopqrstuvwxyz_")] != ':') {
				if (!(*p == '[' || *p == '{' ? yaml_flow(&y, &p, prefix, sp) : yaml_scalar(&y, &p, "", prefix)))
					yaml_end(&y, p, no);
				next = flow_end(p, next, &no);
				continue;
			}
			if (sp >= 15) {
				yaml_fail(&y, "nested too deeply in line %d", no);
				break;
			}
			sp++;
			st[sp].indent = indent - 2;
			st[sp].items = 0;
			st[sp].map_only = 1;
			st[sp].prefix = prefix;
			st[sp].v = yaml_put(&y, prefix, xstrdup(""), p);
			if (!st[sp].v)
				break;
			st[sp].v->kind = 'm';
		}
		colon = p + strspn(p, "abcdefghijklmnopqrstuvwxyz_");
		if (colon == p || *colon != ':' || (colon[1] != ' ' && colon + 1 != e)) {
			yaml_fail(&y, "not a key in line %d", no);
			break;
		}
		key = prefix ? xstrfmt("%s.%.*s", prefix, (int)(colon - p), p) : xstrndup(p, colon - p);
		p = colon + 1 + strspn(colon + 1, " ");
		if (p == e || *p == '#') {
			if (sp >= 15) {
				yaml_fail(&y, "nested too deeply in line %d", no);
				break;
			}
			sp++;
			st[sp].indent = indent;
			st[sp].items = 0;
			st[sp].map_only = 0;
			st[sp].prefix = key;
			st[sp].v = yaml_put(&y, key, xstrdup(""), p);
			if (st[sp].v)
				st[sp].v->kind = '?';
		} else if (*p == '|') {
			/* a literal block: the more indented lines below, clipped to one final newline */
			struct strbuf sb = STRBUF_INIT;
			const char *l = next, *at = next;
			int block = -1;

			if (yaml_end(&y, p + 1, no))
				break;
			while (*l) {
				const char *le = strchrnul(l, '\n');
				int in = strspn(l, " "), blank = l + in == le;

				if (!blank && in <= indent)
					break;
				if (!blank && block < 0) {
					block = in;
					at = l + in;
				}
				if (!blank && in < block) {
					yaml_fail(&y, "a line of '%s' is less indented than its first", key);
					break;
				}
				strbuf_add(&sb, l + (blank ? le - l : block), blank ? 0 : le - l - block);
				strbuf_addch(&sb, '\n');
				l = le + (*le == '\n');
				no++;
			}
			while (sb.len > 1 && sb.buf[sb.len - 2] == '\n')
				strbuf_setlen(&sb, sb.len - 1);
			yaml_put(&y, key, block < 0 ? xstrdup("") : strbuf_detach(&sb, NULL), at);
			next = l;
		} else if (*p == '[' || *p == '{') {
			if (!yaml_flow(&y, &p, key, sp))
				yaml_end(&y, p, no);
			next = flow_end(p, next, &no);
		} else if (!yaml_scalar(&y, &p, "", key)) {
			yaml_end(&y, p, no);
		}
	}
	*err = y.err;
	return y.err ? -1 : 0;
}

void yaml(const char *text, struct string_list *kv, const char *what)
{
	char *err;

	if (yaml_parse(text, kv, what, &err))
		reject(128, NULL, "%s", err);
}

const char *yget(struct string_list *kv, const char *key)
{
	struct string_list_item *item = unsorted_string_list_lookup(kv, key);

	return item ? ((struct yval *)item->util)->s : NULL;
}

/* where the value of KEY starts in the parsed text */
size_t yat(struct string_list *kv, const char *key)
{
	return ((struct yval *)unsorted_string_list_lookup(kv, key)->util)->at;
}

/*
 * Every key of KV against SCHEMA, entries "PATTERN KIND" where N in a pattern
 * stands for a list index and KIND is s (scalar), i (integer), l (list) or
 * m (map); a list or map may be written empty. The first mismatch is fatal.
 */
void yaml_check(struct string_list *kv, const char *const *schema, const char *what)
{
	struct string_list_item *item;
	const char *const *e;

	for_each_string_list_item(item, kv) {
		struct yval *v = item->util;
		struct strbuf pattern = STRBUF_INIT;
		struct string_list parts = STRING_LIST_INIT_DUP;
		struct string_list_item *part;
		char kind = 0;

		string_list_split(&parts, item->string, ".", -1);
		for_each_string_list_item(part, &parts)
			strbuf_addf(&pattern, "%s%s", pattern.len ? "." : "",
				    *part->string && !part->string[strspn(part->string, "0123456789")] ? "N" : part->string);
		for (e = schema; *e && !kind; e++)
			if (starts_with(*e, pattern.buf) && (*e)[pattern.len] == ' ')
				kind = (*e)[pattern.len + 1];
		if (!kind)
			reject(128, NULL, "bad format: '%s': unknown key '%s'", what, item->string);
		/* a key with nothing under it is an empty list, map or scalar; "" is a scalar only */
		if (strchr("lm", kind) ? v->kind != kind && v->kind != '?' :
		    (v->kind && v->kind != '?') || (kind == 'i' && (!*v->s || v->s[strspn(v->s, "0123456789")])))
			reject(128, NULL, "bad format: '%s': bad value of '%s'", what, item->string);
	}
}

/* spec 2.5.2, 2.5.4, 2.5.6: a spec definition; each one is read and checked once per blob */
struct string_list *def_get(struct tree *t, const char *path)
{
	static const char *const exec[] = { "body s", "engine s", "command l", "command.N s", "success l",
					    "success.N i", "inputs l", "inputs.N s", NULL };
	static const char *const prompt[] = { "body s", "engine s", "inputs l", "inputs.N s", NULL };
	static struct string_list parsed = STRING_LIST_INIT_DUP;
	const char *oid = blob_at(t, path), *engine;
	struct string_list_item *hit = oid ? string_list_lookup(&parsed, oid) : NULL;
	struct string_list *kv;

	if (!oid)
		reject(1, NULL, "spec files do not match the contract: missing '%s'", path);
	if (hit)
		return hit->util;
	if (!blob_text(oid))
		reject(128, NULL, "bad format: '%s': not a text file", path);
	kv = xcalloc(1, sizeof(*kv));
	string_list_init_dup(kv);
	yaml(blob_text(oid), kv, path);
	if (yget(kv, "environment"))
		unsupported("environment");
	engine = yget(kv, "engine") ? yget(kv, "engine") : "";
	if (*engine && strcmp(engine, "exec"))
		reject(128, NULL, "bad format: '%s': engine '%s' is not exec or prompt", path, engine);
	yaml_check(kv, *engine ? exec : prompt, path);
	if (!yget(kv, "body") || !*yget(kv, "body") || (*engine && !yget(kv, "command.0")))
		reject(128, NULL, "bad format: '%s': needs 'body', and 'command' with 'engine: exec'", path);
	string_list_insert(&parsed, oid)->util = kv;
	return kv;
}
