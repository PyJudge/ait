/*
 * Trees, contract records and the judgements computed from them (spec 2, 3, 1.5a).
 * Records are stored facts; everything else here is computed (rule 1).
 */
#include "ait.h"
#include <yaml.h>
#include "quote.h"
#include "strvec.h"
#include "utf8.h"
#include <regex.h>

static int cmp_entry(const void *a, const void *b)
{
	return strcmp(((const struct entry *)a)->path, ((const struct entry *)b)->path);
}

/* the entries of T from git's NUL-ended "... TAB PATH" lines, mode and object ID read by FMT */
static void listing(struct tree *t, char *p, const char *fmt)
{
	size_t alloc = 0;
	char *end;

	for (; *p; p = end + 1) {
		struct entry *e;

		end = p + strlen(p);
		ALLOC_GROW(t->e, t->nr + 1, alloc);
		e = &t->e[t->nr++];
		sscanf(p, fmt, e->mode, e->oid);
		e->path = strchr(p, '\t') + 1;
		e->ita = 0;
	}
	QSORT(t->e, t->nr, cmp_entry);
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
	char oid[48];

	if (hit)
		return hit->util;
	if (!object(xstrfmt("%s^{tree}", treeish), 0, oid, NULL))
		reject(128, NULL, "git failed: 'git cat-file': not a tree: '%s'", treeish);
	hit = string_list_lookup(&listed, oid);
	if (hit) {
		string_list_insert(&named, treeish)->util = hit->util;
		return hit->util;
	}
	t = xcalloc(1, sizeof(*t));
	strcpy(t->oid, oid);
	string_list_insert(&named, treeish)->util = t;
	string_list_insert(&listed, oid)->util = t;
	listing(t, git("ls-tree -r -t -z --full-tree %s", t->oid), "%7s %*s %47s");
	return t;
}

/*
 * The entries of the index as they are (spec 5.5-1), which git read-tree -m
 * compares with: intent-to-add ones too, listed by git with the empty blob
 * and marked here as what NOTED, the index's write-tree, leaves out. No tree ID.
 */
struct tree *index_load(struct tree *noted)
{
	struct tree *t = xcalloc(1, sizeof(*t));
	size_t i;

	listing(t, git("ls-files -s -z"), "%7s %47s");
	for (i = 0; i < t->nr; i++)
		t->e[i].ita = !tree_get(noted, t->e[i].path);
	return t;
}

struct entry *tree_get(struct tree *t, const char *path)
{
	struct entry key = { path };

	return bsearch(&key, t->e, t->nr, sizeof(*t->e), cmp_entry);
}

/* the version of the file PATH in T, a listing that holds trees too: no tree entry is a file's version */
struct entry *file_entry(struct tree *t, const char *path)
{
	struct entry *e = tree_get(t, path);

	return e && strcmp(e->mode, "040000") ? e : NULL;
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

/*
 * a line of git update-index --index-info, the path quoted as git reads it
 * back when it holds '"', a control character or more (git-update-index(1))
 */
void index_line(struct strbuf *sb, const char *mode, const char *oid, const char *path)
{
	strbuf_addf(sb, "%s %s\t", mode, oid);
	quote_c_style(path, sb, NULL, 0);
	strbuf_addch(sb, '\n');
}

/*
 * BASE in a throwaway index (spec 4.8), then INDEX_INFO (index_line()s), then
 * the work-tree files PATHS staged by git add as git stages them (modes,
 * symlinks, filters, a file becoming a directory; refusing paths in a
 * submodule or outside a sparse checkout); the tree, or NULL with git's
 * reason in git_error, for the caller to judge
 */
char *tree_with(const char *base, const char *index_info, struct string_list *paths)
{
	char *dir = xstrfmt("%s/ait-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
	struct strvec add = STRVEC_INIT;
	struct string_list_item *item;
	char *index, *tree = NULL;
	int rc;

	if (!paths && !*index_info)
		return xstrdup(base);
	/* 5.8 E_IO: the directory 4.8 names cannot be written */
	if (!mkdtemp(dir))
		reject(128, xstrfmt("make '%s' writable (free space, permissions), then rerun this command", xstrndup(dir, strrchr(dir, '/') - dir)),
		       "write error: '%s': %s", dir, strerror(errno));
	index = xstrfmt("%s/index", dir);
	git_run(NULL, index, &rc, "read-tree %s", base);
	if (!rc && *index_info)
		git_run(index_info, index, &rc, "update-index --add --index-info");
	if (!rc && paths) {
		strvec_pushl(&add, "add", "-f", "--", NULL);
		for_each_string_list_item(item, paths)
			strvec_push(&add, item->string);
		git_args(&add, NULL, index, &rc);
	}
	if (!rc)
		tree = line1(git_run(NULL, index, &rc, "write-tree"));
	unlink(index);
	rmdir(dir);
	return tree;
}

/* the files whose blob or mode differ between A and B, as git diff-tree lists them */
void tree_diff(struct tree *a, struct tree *b, struct string_list *paths)
{
	const char *p, *end;

	/* a submodule's commit is a change whatever submodule.<name>.ignore says */
	for (p = git("diff-tree -r -z --ignore-submodules=none --name-only %s %s", a->oid, b->oid), end = p + git_out_len;
	     p < end; p += strlen(p) + 1)
		string_list_append(paths, p);
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
		else if (strcmp(e->mode, "100644") || !id_ok(id)) {
			/* 2.1.3: no record was ever there to restore */
			r->bad = xstrdup("not a record path");
			r->mend = xstrfmt("remove '.ait/%s'", id);
		}
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
		if (!d || !o || *d->id != 'D' || *o->id != 'O') {
			/* the link is as it was; what went is its end */
			r->bad = r->old->bad = xstrdup("its demand or offer does not exist");
			r->mend = r->old->mend = xstrfmt("restore '.ait/%s' as in the last commit where it was there",
							 d && *d->id == 'D' ? r->id + 24 : xstrndup(r->id + 10, 13));
		}
	}
}

int oid_ok(const char *s)
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

/* 5.10: a malformed record stops with its error; the mend is the one chosen where it was found malformed */
static int bad_record(struct verdict *v, struct record *r, const char *why)
{
	return fail_with(v, 128, "format", xstrfmt("records are written only by ait; %s, then commit that alone as "
						   "spec/ait-mini.md says", r->mend ? r->mend :
						   xstrfmt("restore '.ait/%s' as in the last commit where it was valid", r->id)),
			 xstrfmt("bad format: '.ait/%s': %s", r->id, why));
}

static const char *const contract_states[] = { "future", "current", "cancelled", "withdrawn", NULL };
static const char *const link_states[] = { "undertaken", "released", NULL };

/* R means what its HEAD version means: the request did not change it (prev aside, 6.2) */
static int as_in_head(struct record *r)
{
	return r->old && !strcmp(record_text(r, MEANING_KEYS), record_text(r->old, MEANING_KEYS));
}

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
		/* 5.9a transition hint 1: an error HEAD already had is not this request's, so it is mended as 5.10 says */
		if (in_state(r, "undertaken") && is_current(o) && in_state(d, "future") && as_in_head(r) && as_in_head(d) &&
		    as_in_head(o))
			return fail_with(v, 1, "state", xstrfmt("records are written only by ait; restore '.ait/%s' '.ait/%s' "
								 "'.ait/%s' as in the last commit where they were valid, then "
								 "commit that alone as spec/ait-mini.md says, then rerun this "
								 "command", d->id, o->id, r->id),
					 xstrfmt("not allowed: current '%s' undertakes future '%s'", o->id, d->id));
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
							       "with them: 'ait %s -m <why>'",
							       s->r[i]->id, reopen_clauses(s, s->r[i], NULL)),
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

/*
 * the path of T that PATH names on a case-insensitive filesystem
 * (core.ignorecase) without being it, or NULL: git add stages nothing for
 * such a path, and git's history would get both
 */
const char *case_twin(struct tree *t, const char *path)
{
	static int ignorecase = -1;
	size_t i;

	if (ignorecase < 0)
		ignorecase = ait_config_bool("core.ignorecase");
	for (i = 0; ignorecase && i < t->nr; i++)
		if (!strcasecmp(t->e[i].path, path) && strcmp(t->e[i].path, path))
			return t->e[i].path;
	return NULL;
}

/* spec 3.3-3.4: fix a scope argument at tree T */
char *pin(const char *arg, struct tree *t, int allow_new, int allow_abs)
{
	char *dir = ends_with(arg, "/") ? xstrndup(arg, strlen(arg) - 1) : xstrdup(arg);
	const char *rest, *path = skip_prefix(arg, "new:", &rest) ? rest : dir;
	struct entry *e;

	/* the shape of ARG was checked before (scope_shape, 5.13-1); here is what T has */
	if (path == rest && allow_new && !tree_get(t, rest) && !case_twin(t, rest))
		return xstrdup(arg);
	if (!strcmp(arg, "/") && allow_abs)
		return xstrfmt("/ %s", t->oid);
	e = tree_get(t, dir);
	if (e && ends_with(arg, "/") && allow_abs && !strcmp(e->mode, "040000"))
		return xstrfmt("%s %s", arg, e->oid);
	if (e && !ends_with(arg, "/") && blob_at(t, arg))
		return xstrfmt("%s %s", arg, e->oid);
	if (case_twin(t, path))
		reject(128, xstrfmt("give it as git has it: '%s'", case_twin(t, path)), "bad scope: '%s'", arg);
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

/* the clause amending K to PROVIDER and FILES, "-" keeping what K has; a demand keeps its consumer (7.3.2) */
char *amend_clause(struct record *k, const char *provider, const char *files)
{
	return xstrfmt("amend %s %s%s %s", k->id, *k->id == 'D' ? "- " : "", provider, files);
}

/* "future amend K future amend O..." reopening K and what it takes along; K gets DEFS when given (5.9a) */
char *reopen_clauses(struct state *s, struct record *k, const char *defs)
{
	struct string_list set = STRING_LIST_INIT_DUP;
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;

	reopen_set(s, k, &set);
	for_each_string_list_item(item, &set)
		strbuf_addf(&sb, "%sfuture %s", sb.len ? " " : "", item->util == k && defs ? amend_clause(k, "-", defs) :
			    xstrfmt("amend %s", item->string));
	return strbuf_detach(&sb, NULL);
}

/* the reopen hint of 5.9a, run on its own */
char *reopen_hint(struct state *s, struct record *k)
{
	return xstrfmt("to change '%s' without a check, reopen it: 'ait %s -m <why>'", k->id, reopen_clauses(s, k, NULL));
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
static void body_lines(struct strbuf *sb, struct string_list *kv, const char *indent)
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

/*
 * every Ait-Check value in HEAD's history as the parser of 5.4 gives it (its
 * values as written, so a folded one is no check), with the commit that holds
 * it as util; read once.
 * "COMMIT NUL KEY \1 VALUE NUL ... NUL" per commit: the key is kept, since a
 * value alone cannot tell an empty value from none, and no value holds NUL
 */
static struct string_list *check_trailers(void)
{
	static struct string_list values = STRING_LIST_INIT_DUP;
	static int loaded;
	const char *p, *end, *commit = NULL;

	if (loaded++ || !head)
		return &values;
	p = git("log -z --format=%H%x00%(trailers:key=Ait-Check,separator=%x00,key_value_separator=%x01) %s", head);
	/* an item always holds \1, a commit's ID never; an empty field ends a commit without trailers */
	for (end = p + git_out_len; p < end; p += strlen(p) + 1)
		if (strchr(p, '\x01') && commit)
			string_list_append(&values, strchr(p, '\x01') + 1)->util = xstrdup(commit);
		else if (*p)
			commit = p;
	return &values;
}

/* an Ait-Check value that is exactly "ID reported|forced PINS" (5.4): "ID PINS", else NULL */
static char *check_of(const char *value)
{
	static const char *const how[] = { "reported", "forced", NULL };
	char id[48], word[16], p[48], more;

	if (sscanf(value, "%47s %15s %47s %c", id, word, p, &more) == 3 && one_of(word, how) && oid_ok(p) &&
	    !strcmp(value, xstrfmt("%s %s %s", id, word, p)))
		return xstrfmt("%s %s", id, p);
	return NULL;
}

/* the confirmed pins: "ID PINS" of every check in HEAD's history */
static struct string_list *checks(void)
{
	static struct string_list confirmed = STRING_LIST_INIT_DUP;
	static int loaded;
	struct string_list_item *item;

	if (loaded++)
		return &confirmed;
	for_each_string_list_item(item, check_trailers())
		if (check_of(item->string))
			string_list_insert(&confirmed, check_of(item->string));
	return &confirmed;
}

/*
 * the code or spec files of R in T are not the ones it pins (the second half
 * of 1.5a): each "file PATH MODE OID" and "provider PATH OID" line against
 * T's entry; nothing is parsed, so an old malformed blob is simply not the pin
 */
static int off_pins(struct record *r, struct tree *t)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &r->f[FILES]) {
		struct entry *now = file_entry(t, scope_path(item->string));

		if (!now || !starts_with(scope_oid(item->string), xstrfmt("%s %s ", now->mode, now->oid)))
			return 1;
	}
	for_each_string_list_item(item, &r->f[PROVIDER]) {
		const char *now = blob_at(t, scope_path(item->string));

		if (!now || !scope_oid(item->string) || strcmp(now, scope_oid(item->string)))
			return 1;
	}
	return 0;
}

/* spec 1.5a: not confirmed with these pins, or the code or spec files moved since */
int unverified(struct record *r, struct tree *t)
{
	return !string_list_has_string(checks(), xstrfmt("%s %s", r->id, pins(r))) || off_pins(r, t);
}

/* the record TEXT is R as a current contract with R's pins (the provider and file lines of 5.4) */
static int same_pins(const char *text, struct record *r)
{
	struct record x = { "" };
	int k;

	for (k = 0; k < NKEYS; k++)
		string_list_init_dup(&x.f[k]);
	if (text)
		record_parse(&x, text);
	return text && !x.bad && in_state(&x, "current") && !strcmp(record_text(&x, PIN_KEYS), record_text(r, PIN_KEYS));
}

/* the facts of 9.1 for R over HEAD's history, each commit's verdict remembered */
struct history {
	struct string_list confirmed;	/* commits a check of R's pins holds at: a check commit and its descendants */
	struct string_list changed;	/* commits that change R's record, code or spec files against a parent */
	struct string_list parent;	/* commit -> its first parent */
	struct string_list seen, off;	/* commit -> unverified there; commit -> off its pins there */
};

/*
 * R's code or spec files at C are off its pins: worked out where they change
 * (13.4: what is as in the parent is not worked out again), else as in C's
 * first parent, going down until one changes them
 */
static int off_at(struct record *r, const char *c, struct history *h)
{
	struct string_list chain = STRING_LIST_INIT_NODUP;
	struct string_list_item *hit, *up;
	const char *x = c;
	int off;

	/* down the first parents to a commit worked out already, or one that changes them */
	while (!(hit = string_list_lookup(&h->off, x)) && !string_list_has_string(&h->changed, x) &&
	       (up = string_list_lookup(&h->parent, x)) && up->util) {
		string_list_append(&chain, x);
		x = up->util;
	}
	off = hit ? hit->util != NULL : off_pins(r, tree_load(x));
	string_list_append(&chain, x);
	for_each_string_list_item(hit, &chain)
		string_list_insert(&h->off, hit->string)->util = off ? (void *)"" : NULL;
	return off;
}

/*
 * R is unverified at commit C with the pins it has now (1.5a): C holds R as a
 * current contract with these pins, and no check confirmed them at C or an
 * ancestor of C, or C's code or spec files are off them
 */
static int unverified_at(struct record *r, const char *c, struct history *h)
{
	struct string_list_item *hit = string_list_lookup(&h->seen, c);
	const char *record = blob_at(head_tree(), xstrfmt(".ait/%s", r->id));
	char there[48];
	int is;

	if (hit)
		return hit->util != NULL;
	is = object(xstrfmt("%s:.ait/%s", c, r->id), 0, there, NULL) &&
	     (!strcmp(there, record) || same_pins(blob_text(there), r)) &&
	     (!string_list_has_string(&h->confirmed, c) || off_at(r, c, h));
	string_list_insert(&h->seen, c)->util = is ? (void *)"" : NULL;
	return is;
}

/*
 * 9.1 for H8: the latest commit of HEAD's history where R newly became
 * unverified with the pins it has now: R is unverified there, and no parent
 * of it had R unverified with the same pins. Verified at C is what
 * unverified() says at HEAD: a commit with the check is C or an ancestor of C.
 */
const char *unverified_since(struct record *r)
{
	struct history h = { STRING_LIST_INIT_DUP, STRING_LIST_INIT_DUP, STRING_LIST_INIT_DUP, STRING_LIST_INIT_DUP,
			     STRING_LIST_INIT_DUP };
	struct string_list commits = STRING_LIST_INIT_DUP;
	struct strvec paths = STRVEC_INIT;
	struct string_list_item *item;
	char *want = xstrfmt("%s %s", r->id, pins(r));
	size_t i;

	if (!head)
		return NULL;
	for_each_string_list_item(item, check_trailers())
		if (check_of(item->string) && !strcmp(check_of(item->string), want)) {
			string_list_insert(&h.confirmed, item->util);
			string_list_split(&h.confirmed, git("rev-list --ancestry-path %s", xstrfmt("%s..%s", (char *)item->util, head)),
					  "\n", -1);
		}
	string_list_sort(&h.confirmed);
	/* the commits that change R's code or spec files against any parent, by git's full history */
	strvec_pushl(&paths, "log", "--full-history", "-m", "--format=%H", head, "--", NULL);
	for (i = 0; i < NKEYS; i++)
		if (i == PROVIDER || i == FILES)
			for_each_string_list_item(item, &r->f[i])
				strvec_push(&paths, scope_path(item->string));
	string_list_split(&h.changed, git_args(&paths, NULL, NULL, NULL), "\n", -1);
	string_list_sort(&h.changed);
	/* "C P1 P2 ..." newest first */
	string_list_split(&commits, git("rev-list --topo-order --parents %s", head), "\n", -1);
	for_each_string_list_item(item, &commits) {
		struct string_list ids = STRING_LIST_INIT_DUP;

		string_list_split(&ids, item->string, " ", -1);
		if (*item->string)
			string_list_insert(&h.parent, ids.items[0].string)->util = ids.nr > 1 ? ids.items[1].string : NULL;
	}
	for_each_string_list_item(item, &commits) {
		struct string_list ids = STRING_LIST_INIT_DUP;

		string_list_split(&ids, item->string, " ", -1);
		if (!*item->string || !unverified_at(r, ids.items[0].string, &h))
			continue;
		for (i = 1; i < ids.nr && !unverified_at(r, ids.items[i].string, &h); i++)
			;
		if (i == ids.nr)
			return ids.items[0].string;
	}
	return NULL;
}

/* spec 2.5.2: the inputs of the definition DEF at T into OUT, each a PATH of 3.1 */
void def_inputs(struct tree *t, const char *def, struct string_list *out)
{
	struct string_list *kv = def_get(t, def);
	struct string_list_item *in;

	for_each_string_list_item(in, kv) {
		if (!starts_with(in->string, "inputs."))
			continue;
		if (!path_ok(yget(kv, in->string)))
			reject(128, NULL, "bad format: '%s': bad input '%s'%s", def, yget(kv, in->string), yline(kv, in->string));
		if (!unsorted_string_list_has_string(out, yget(kv, in->string)))
			string_list_append(out, yget(kv, in->string));
	}
}

/* spec 2.4: the file lines are the defs and what the defs declare, pinned at T */
int expand_files(struct record *r, struct tree *t)
{
	struct string_list defs = STRING_LIST_INIT_DUP, out = STRING_LIST_INIT_DUP, inputs = STRING_LIST_INIT_DUP;
	struct string_list_item *item, *in;

	for_each_string_list_item(item, &r->f[FILES])
		if (is_def(item->string))
			string_list_insert(&defs, scope_path(item->string));
	for_each_string_list_item(item, &defs) {
		struct entry *e = tree_get(t, item->string);

		if (!blob_at(t, item->string))
			return -1;
		string_list_insert(&out, xstrfmt("%s %s %s def", e->path, e->mode, e->oid));
		def_inputs(t, e->path, &inputs);
	}
	for_each_string_list_item(in, &inputs) {
		if (string_list_has_string(&defs, in->string))
			continue;
		if (!blob_at(t, in->string))
			return -1;
		string_list_insert(&out, xstrfmt("%s %s %s input", in->string, tree_get(t, in->string)->mode,
						 blob_at(t, in->string)));
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
 * The YAML subset of spec 2.5.1, read by libyaml as YAML places it. What the
 * subset leaves out shows in libyaml's events and is refused here: aliases,
 * anchors, tags, directives, a second document, folded blocks, keys that are
 * not scalars, a root that is not a map. The markers "---" and "..." of the
 * one document are YAML's, and libyaml's emitter writes "..." itself. The values are flattened into
 * KV: "a: x" is "a", list items are "a.0", keys inside them "a.0.b"; '.' and
 * '\' in a key are escaped, so a path names one value. Every value keeps
 * where its text starts (a byte offset) and its kind.
 */
/* the path the root map is kept under: no key gives it, as yaml_path() escapes a '\\' of a key */
#define ROOT "\\"

struct yval {
	char *s;
	size_t start, at, end;	/* where the node starts, where the value's text starts, where the node ends */
	int line, literal;	/* LITERAL: the text from AT spells the value as it is */
	int plain, block;	/* a plain scalar; a "|" block */
	char kind;	/* 'l' a list, 'm' a map, '?' a key with nothing under it, 0 a scalar */
};

/* the path of KEY (a map key, or a list index) under PATH, NULL being the root */
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

/* the byte offset of the character MARK (libyaml counts characters, after a byte order mark) */
static size_t yaml_byte(const char *text, yaml_mark_t mark)
{
	const char *p = text + (starts_with(text, "\xef\xbb\xbf") ? 3 : 0);
	size_t n = 0;

	for (; *p && (n < mark.index || ((unsigned char)*p & 0xc0) == 0x80); p++)
		n += ((unsigned char)*p & 0xc0) != 0x80;
	return p - text;
}

/* an event the subset leaves out, or NULL */
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
		return e->data.scalar.anchor ? "an anchor" : e->data.scalar.tag ? "a tag" :
		       e->data.scalar.style == YAML_FOLDED_SCALAR_STYLE ? "a folded block" : NULL;
	case YAML_SEQUENCE_START_EVENT:
		return e->data.sequence_start.anchor ? "an anchor" : e->data.sequence_start.tag ? "a tag" : NULL;
	case YAML_MAPPING_START_EVENT:
		return e->data.mapping_start.anchor ? "an anchor" : e->data.mapping_start.tag ? "a tag" : NULL;
	default:
		return NULL;
	}
}

/* an open map or list: its path, the key read for its next value, the count of its items */
struct yframe {
	char *path, *key;
	int n;
	char kind;
};

/*
 * where the node of E goes under TOP, the innermost open collection (NULL at
 * the root): its path; NULL when it is the root map, or the key of TOP's next
 * value (kept in TOP), or with WHY set
 */
static char *yaml_place(struct yframe *top, yaml_event_t *e, const char **why)
{
	char *path;

	if (!top) {
		if (e->type != YAML_MAPPING_START_EVENT)
			*why = "no key";
		return NULL;
	}
	if (top->kind == 'l')
		return yaml_path(top->path, xstrfmt("%d", top->n++));
	if (top->key) {
		path = yaml_path(top->path, top->key);
		top->key = NULL;
		return path;
	}
	if (e->type == YAML_SCALAR_EVENT)
		top->key = xstrdup((char *)e->data.scalar.value);
	else
		*why = "a key that is not a scalar";
	return NULL;
}

/* the kind of the node of E, as struct yval keeps it */
static char yaml_kind(yaml_event_t *e)
{
	if (e->type == YAML_MAPPING_START_EVENT)
		return 'm';
	if (e->type == YAML_SEQUENCE_START_EVENT)
		return 'l';
	return e->data.scalar.style == YAML_PLAIN_SCALAR_STYLE && !e->data.scalar.length ? '?' : 0;
}

/* one event of the document into KV, with the open collections in STACK: NULL, or what is wrong */
static const char *yaml_event(const char *text, struct string_list *kv, struct yframe *stack, int *depth, yaml_event_t *e)
{
	const char *why = NULL;
	struct yval *v;
	char *path;

	if (e->type == YAML_MAPPING_END_EVENT || e->type == YAML_SEQUENCE_END_EVENT)
		(*depth)--;
	if (e->type != YAML_SCALAR_EVENT && e->type != YAML_MAPPING_START_EVENT && e->type != YAML_SEQUENCE_START_EVENT)
		return NULL;
	/* a value is text, as a file ait reads is (an escaped NUL would cut it) */
	if (e->type == YAML_SCALAR_EVENT && strlen((char *)e->data.scalar.value) != e->data.scalar.length)
		return "a NUL character";
	path = yaml_place(*depth ? &stack[*depth - 1] : NULL, e, &why);
	if (why)
		return why;
	/* the root map, kept under ROOT for the line of what it lacks */
	if (!*depth)
		path = xstrdup(ROOT);
	if (path && unsorted_string_list_has_string(kv, path))
		return xstrfmt("duplicate key '%s'", path);
	if (path) {
		v = xcalloc(1, sizeof(*v));
		v->s = xstrdup(e->type == YAML_SCALAR_EVENT ? (char *)e->data.scalar.value : "");
		v->kind = yaml_kind(e);
		v->plain = e->type == YAML_SCALAR_EVENT && e->data.scalar.style == YAML_PLAIN_SCALAR_STYLE;
		v->block = e->type == YAML_SCALAR_EVENT && e->data.scalar.style == YAML_LITERAL_SCALAR_STYLE;
		v->line = e->start_mark.line + 1;
		v->start = yaml_byte(text, e->start_mark);
		v->end = yaml_byte(text, e->end_mark);
		/* a quoted scalar's text starts after its quote */
		v->at = v->start + (e->type == YAML_SCALAR_EVENT && (e->data.scalar.style == YAML_SINGLE_QUOTED_SCALAR_STYLE ||
								      e->data.scalar.style == YAML_DOUBLE_QUOTED_SCALAR_STYLE));
		v->literal = !strncmp(text + v->at, v->s, strlen(v->s));
		string_list_append(kv, path)->util = v;
	}
	if (e->type == YAML_SCALAR_EVENT)
		return NULL;
	if (*depth == 16)
		return "nested too deeply";
	stack[*depth].path = strcmp(path, ROOT) ? path : NULL;
	stack[*depth].key = NULL;
	stack[*depth].n = 0;
	stack[*depth].kind = yaml_kind(e);
	(*depth)++;
	return NULL;
}

/* the line (from 0) of the byte at OFFSET in TEXT */
static size_t line_at(const char *text, size_t offset)
{
	size_t n = 0, i;

	for (i = 0; i < offset && text[i]; i++)
		n += text[i] == '\n';
	return n;
}

/* " in line N" of the first NUL of BUF, a file ait reads that holds one (5.8 E_FORMAT) */
char *nul_line(const char *buf)
{
	return xstrfmt(" in line %d", (int)line_at(buf, strlen(buf)) + 1);
}

/*
 * 2.5.1: UTF-8, and LF the only line break, as libyaml takes CR, NEL, LS and
 * PS as line breaks too (YAML 1.1); what breaks that and its line, or NULL.
 * Where a tab may stand is YAML's.
 */
static const char *not_text(const char *text, size_t *line)
{
	static const char *const breaks[] = { "\r", "\xc2\x85", "\xe2\x80\xa8", "\xe2\x80\xa9", NULL };
	const char *p, *first = NULL;
	int i;

	for (p = text, *line = 0; *p; p += strcspn(p, "\n") + !!p[strcspn(p, "\n")], ++*line)
		if (!is_utf8(xstrndup(p, strcspn(p, "\n"))))
			return "not UTF-8";
	for (i = 0; breaks[i]; i++)
		if ((p = strstr(text, breaks[i])) && (!first || p < first))
			first = p;
	*line = first ? line_at(text, first - text) : 0;
	return first ? "a line break other than LF" : NULL;
}

int yaml_parse(const char *text, struct string_list *kv, const char *what, char **err)
{
	struct yframe stack[16];
	yaml_parser_t parser;
	yaml_event_t e;
	const char *why = NULL;
	int depth = 0, docs = 0, done = 0;
	size_t line = 0;

	*err = NULL;
	/*
	 * 2.5.1: UTF-8, and LF the only line break; libyaml takes CR, NEL, LS and
	 * PS as line breaks too (YAML 1.1). Where a tab may stand is YAML's.
	 */
	if ((why = not_text(text, &line))) {
		*err = xstrfmt("bad format: '%s': %s in line %d", what, why, (int)line + 1);
		return -1;
	}
	yaml_parser_initialize(&parser);
	yaml_parser_set_input_string(&parser, (const unsigned char *)text, strlen(text));
	while (!done && !why && yaml_parser_parse(&parser, &e)) {
		line = e.start_mark.line;
		docs += e.type == YAML_DOCUMENT_START_EVENT;
		why = yaml_outside(&e, docs > 1);
		if (!why)
			why = yaml_event(text, kv, stack, &depth, &e);
		done = e.type == YAML_STREAM_END_EVENT;
		yaml_event_delete(&e);
	}
	if (!done && !why) {
		why = parser.problem;
		/* a reader error has its place as an offset only */
		line = parser.error == YAML_READER_ERROR ? line_at(text, parser.problem_offset) : parser.problem_mark.line;
	}
	if (why)
		*err = xstrfmt("bad format: '%s': %s in line %d", what, why, (int)line + 1);
	yaml_parser_delete(&parser);
	return why ? -1 : 0;
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

/* the output of libyaml's emitter into the strbuf SB */
int yaml_out(void *sb, unsigned char *buf, size_t n)
{
	strbuf_add(sb, buf, n);
	return 1;
}

/* S as one "..." scalar into SB, written by libyaml's emitter as the check files are (8.2) */
static void yaml_quoted(struct strbuf *sb, const char *s)
{
	yaml_emitter_t y;
	yaml_event_t e;
	int ok;

	yaml_emitter_initialize(&y);
	yaml_emitter_set_output(&y, yaml_out, sb);
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
	strbuf_rtrim(sb);
}

/*
 * In TEXT, which KV was parsed from, the first LEN bytes of KEY's value become
 * WITH: in place where the text spells the value as it is, else the scalar is
 * written anew as "..." (its line breaks after it kept). Replacing from the
 * last value back keeps the places of the others.
 */
void yreplace(struct strbuf *text, struct string_list *kv, const char *key, size_t len, const char *with)
{
	struct yval *v = unsorted_string_list_lookup(kv, key)->util;
	struct strbuf sb = STRBUF_INIT;
	size_t end = v->end;

	if (v->literal) {
		strbuf_splice(text, v->at, len, with, strlen(with));
		return;
	}
	while (end > v->start && text->buf[end - 1] == '\n')
		end--;
	yaml_quoted(&sb, xstrfmt("%s%s", with, v->s + len));
	strbuf_splice(text, v->start, end - v->start, sb.buf, sb.len);
}

/* where the last '.' of the path P stands, its length when it has none; keys escape theirs as "\\." */
static size_t last_dot(const char *p)
{
	size_t i, dot = strlen(p);

	for (i = 0; p[i]; i++)
		if (p[i] == '\\' && p[i + 1])
			i++;
		else if (p[i] == '.')
			dot = i;
	return dot;
}

/*
 * " in line N" of the value of KEY, for E_FORMAT (5.8: the file and the
 * line); for a KEY that is not there, of the nearest map above it that is,
 * the root at last, or line 1 of a file with none
 */
char *yline(struct string_list *kv, const char *key)
{
	char *up = xstrdup(key);
	struct string_list_item *item;
	size_t dot;

	while (!(item = unsorted_string_list_lookup(kv, up)) && *up) {
		dot = last_dot(up);
		up[dot < strlen(up) ? dot : 0] = 0;
	}
	if (!item)
		item = unsorted_string_list_lookup(kv, ROOT);
	return xstrfmt(" in line %d", item ? ((struct yval *)item->util)->line : 1);
}

/*
 * spec 8.4: the "|" blocks of TEXT, a YAML file, without their trailing blank
 * lines: whole lines of spaces at the end of a block as libyaml places it,
 * dropped only where the value reads the same after ("|+" keeps them as its
 * value); from the last block back, so the places of the others hold
 */
void yaml_trim_blocks(struct strbuf *text)
{
	struct string_list kv = STRING_LIST_INIT_DUP;
	char *err;
	size_t i;

	if (yaml_parse(text->buf, &kv, "", &err))
		return;
	for (i = kv.nr; i-- > 0;) {
		struct yval *v = kv.items[i].util;
		struct string_list after = STRING_LIST_INIT_DUP;
		struct strbuf trimmed = STRBUF_INIT;
		size_t head = v->start + strcspn(text->buf + v->start, "\n") + 1, e = v->end, p, line;

		if (!v->block)
			continue;
		/* the end of the block's last line; the next line keeps its indentation */
		while (e > head && text->buf[e - 1] != '\n')
			e--;
		/* back over the lines that hold spaces only */
		for (p = e; p > head; p = line) {
			line = p - 1;
			while (line > head && text->buf[line - 1] != '\n')
				line--;
			if (text->buf[line + strspn(text->buf + line, " ")] != '\n')
				break;
		}
		if (p == e)
			continue;
		strbuf_add(&trimmed, text->buf, text->len);
		strbuf_splice(&trimmed, p, e - p, "", 0);
		if (!yaml_parse(trimmed.buf, &after, "", &err) && yget(&after, kv.items[i].string) &&
		    !strcmp(yget(&after, kv.items[i].string), v->s))
			strbuf_splice(text, p, e - p, "", 0);
	}
}

/* 2.5.1: a decimal integer, as YAML 1.2 reads one: a plain scalar of digits, no leading zero */
static int decimal(struct yval *v)
{
	return v->plain && *v->s && !v->s[strspn(v->s, "0123456789")] && (*v->s != '0' || !v->s[1]);
}

/* the kind SCHEMA gives PATTERN, or 0 */
static char schema_kind(const char *const *schema, const char *pattern)
{
	for (; *schema; schema++)
		if (starts_with(*schema, pattern) && (*schema)[strlen(pattern)] == ' ')
			return (*schema)[strlen(pattern) + 1];
	return 0;
}

/*
 * Every key of KV against SCHEMA, entries "PATTERN KIND" where N in a pattern
 * stands for a list index and KIND is s (scalar), i (integer), l (list) or
 * m (map); a list or map may be written empty. The first mismatch is fatal.
 */
void yaml_check(struct string_list *kv, const char *const *schema, const char *what, const char *hint, int kinds)
{
	struct string_list_item *item;

	for_each_string_list_item(item, kv) {
		struct yval *v = item->util;
		struct strbuf pattern = STRBUF_INIT;
		struct string_list parts = STRING_LIST_INIT_DUP;
		struct string_list_item *part;
		char kind = 0;

		/* the root map is no key */
		if (!strcmp(item->string, ROOT))
			continue;
		string_list_split(&parts, item->string, ".", -1);
		for_each_string_list_item(part, &parts)
			strbuf_addf(&pattern, "%s%s", pattern.len ? "." : "",
				    *part->string && !part->string[strspn(part->string, "0123456789")] ? "N" : part->string);
		if (!(kind = schema_kind(schema, pattern.buf))) {
			/*
			 * a key the schema lacks, in a map the document and the schema both
			 * have as one (or the root), is unknown; under anything else it is
			 * its parent's kind that is wrong, and the parent's check says so
			 */
			size_t dot = last_dot(item->string);
			struct string_list_item *parent = unsorted_string_list_lookup(kv, xstrndup(item->string, dot));

			if (!item->string[dot] || (parent && ((struct yval *)parent->util)->kind == 'm' &&
						    schema_kind(schema, xstrndup(pattern.buf, last_dot(pattern.buf))) == 'm'))
				reject(128, hint, "bad format: '%s': unknown key '%s' in line %d", what, item->string, v->line);
			continue;
		}
		/* a key with nothing under it is an empty list, map or scalar; "" is a scalar only */
		if (kinds && (strchr("lm", kind) ? v->kind != kind && v->kind != '?' :
			      (v->kind && v->kind != '?') || (kind == 'i' && !decimal(v))))
			reject(128, hint, "bad format: '%s': bad value of '%s' in line %d", what, item->string, v->line);
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
		reject(128, NULL, "bad format: '%s': not a text file%s", path, nul_line(object(oid, 1, NULL, NULL)));
	kv = xcalloc(1, sizeof(*kv));
	string_list_init_dup(kv);
	yaml(blob_text(oid), kv, path);
	if (yget(kv, "environment"))
		unsupported("environment");
	engine = yget(kv, "engine") ? yget(kv, "engine") : "";
	if (*engine && strcmp(engine, "exec"))
		reject(128, NULL, "bad format: '%s': engine '%s': give exec, or leave it out for prompt%s", path, engine,
		       yline(kv, "engine"));
	yaml_check(kv, *engine ? exec : prompt, path, NULL, 1);
	if (!yget(kv, "body") || !*yget(kv, "body") || (*engine && !yget(kv, "command.0")))
		reject(128, NULL, "bad format: '%s': needs 'body', and 'command' with 'engine: exec'%s", path,
		       yline(kv, !yget(kv, "body") || !*yget(kv, "body") ? "body" : "command.0"));
	string_list_insert(&parsed, oid)->util = kv;
	return kv;
}
