/*
 * Trees, contract records and the judgements computed from them (spec 2, 3, 1.5a).
 * Records are stored facts; everything else here is computed (rule 1).
 */
#include "ait.h"
#include "quote.h"
#include "strvec.h"
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

/*
 * BASE in a throwaway index (spec 4.8), then INDEX_INFO ("MODE OID\tPATH"
 * lines), or the work-tree files PATHS staged by git update-index --add as git
 * stages them (core.fileMode, core.symlinks, paths beyond a symlink, filters);
 * the tree, or NULL with git's reason in git_error when git refuses the paths
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
	if (!mkdtemp(dir))
		die_errno("could not create '%s'", dir);
	index = xstrfmt("%s/index", dir);
	git_run(NULL, index, &rc, "read-tree %s", base);
	if (!rc && paths) {
		strvec_pushl(&add, "update-index", "--add", "--", NULL);
		for_each_string_list_item(item, paths)
			strvec_push(&add, item->string);
		git_args(&add, NULL, index, &rc);
	} else if (!rc) {
		git_run(index_info, index, &rc, "update-index --add --index-info");
	}
	if (!rc)
		tree = line1(git_run(NULL, index, &rc, "write-tree"));
	unlink(index);
	rmdir(dir);
	if (rc && !paths)
		reject(128, NULL, "git failed: %s", git_error);
	return tree;
}

/* the files whose blob or mode differ between A and B, as git diff-tree lists them */
void tree_diff(struct tree *a, struct tree *b, struct string_list *paths)
{
	const char *p, *end;

	for (p = git("diff-tree -r -z --name-only %s %s", a->oid, b->oid), end = p + git_out_len; p < end; p += strlen(p) + 1)
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

	/* the shape of ARG was checked before (scope_shape, 5.13-1); here is what T has */
	if (skip_prefix(arg, "new:", &rest) && allow_new && !tree_get(t, rest))
		return xstrdup(arg);
	if (!strcmp(arg, "/") && allow_abs)
		return xstrfmt("/ %s", t->oid);
	e = tree_get(t, dir);
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

/*
 * every Ait-Check value in HEAD's history as git's trailer parser gives it (5.4),
 * a folded one unfolded, with the commit that holds it as util; read once.
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
	p = git("log -z --format=%H%x00%(trailers:key=Ait-Check,unfold,separator=%x00,key_value_separator=%x01) %s", head);
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

/* the code or spec files of R in T are not the ones it pins (the second half of 1.5a) */
static int off_pins(struct record *r, struct tree *t)
{
	struct string_list_item *item;

	if (!files_match(r, t))
		return 1;
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
	struct strbuf pinned = STRBUF_INIT;
	struct string_list lines = STRING_LIST_INIT_DUP;
	struct string_list_item *item;

	if (!text || !strstr(text, "\nstate current\n"))
		return 0;
	string_list_split(&lines, text, "\n", -1);
	for_each_string_list_item(item, &lines)
		if (starts_with(item->string, "provider ") || starts_with(item->string, "file "))
			strbuf_addf(&pinned, "%s\n", item->string);
	return !strcmp(pinned.buf, record_text(r, PIN_KEYS));
}

/*
 * 9.1 for H8: the commit of HEAD's first-parent history where R became unverified
 * with the pins it has now: going back from HEAD while the commit holds R current
 * with these pins and R is unverified there, the last such commit. Verified at C
 * is what unverified() says at HEAD: a commit with the check is C or an ancestor
 * of C, and C's tree has what the pins name. NULL when the 100 commits ait looks
 * at end before that.
 */
const char *unverified_since(struct record *r)
{
	struct string_list confirmed = STRING_LIST_INIT_DUP, walk = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	const char *record = blob_at(head_tree(), xstrfmt(".ait/%s", r->id)), *since = NULL;
	char *want = xstrfmt("%s %s", r->id, pins(r));
	size_t i;

	if (!head)
		return NULL;
	for_each_string_list_item(item, check_trailers())
		if (check_of(item->string) && !strcmp(check_of(item->string), want)) {
			string_list_insert(&confirmed, item->util);
			string_list_split(&confirmed, git("rev-list --ancestry-path %s", xstrfmt("%s..%s", (char *)item->util, head)),
					  "\n", -1);
		}
	string_list_sort(&confirmed);
	string_list_split(&walk, git("log --first-parent -n 100 --format=%H %s", head), "\n", -1);
	for (i = 0; i < walk.nr && *walk.items[i].string; i++) {
		struct tree *t = tree_load(walk.items[i].string);
		const char *there = blob_at(t, xstrfmt(".ait/%s", r->id));

		/* 9.1: the same pins (provider and file lines) of a current contract; prev alone is no change */
		if (!there || (strcmp(there, record) && !same_pins(blob_text(there), r)) ||
		    (string_list_has_string(&confirmed, walk.items[i].string) && !off_pins(r, t)))
			return since;
		since = walk.items[i].string;
	}
	return walk.nr > 100 ? NULL : since;
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
 * The YAML subset of spec 2.5.1, read by recursive descent as YAML 1.2 places
 * its forms: block maps and lists by indentation, flow collections, plain and
 * quoted scalars, literal blocks and comments. One function reads each form,
 * and the boundaries of tokens are decided in scalar() and line_end() alone.
 * The values are flattened into KV: "a: x" is "a", list items are "a.0",
 * keys inside them "a.0.b"; '.' and '\' in a key are escaped, so a path names
 * one value. Every value keeps where it starts in the text and its kind.
 */
struct yval {
	char *s;
	size_t at;
	char kind;	/* 'l' a list, 'm' a map, '?' a key with nothing under it, 0 a scalar */
};

struct yaml {
	const char *text, *p, *what;
	struct string_list *kv;
	char *err;
	int depth;
};

static int yaml_fail(struct yaml *y, const char *fmt, ...)
{
	struct strbuf sb = STRBUF_INIT;
	const char *c;
	va_list ap;
	int line = 1;

	for (c = y->text; c < y->p; c++)
		line += *c == '\n';
	strbuf_addf(&sb, "bad format: '%s': ", y->what);
	va_start(ap, fmt);
	strbuf_vaddf(&sb, fmt, ap);
	va_end(ap);
	strbuf_addf(&sb, " in line %d", line);
	if (!y->err)
		y->err = strbuf_detach(&sb, NULL);
	return -1;
}

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

static int yaml_put(struct yaml *y, const char *path, char *s, size_t at, char kind)
{
	struct yval *v;

	if (!path)
		return 0;
	if (unsorted_string_list_has_string(y->kv, path))
		return yaml_fail(y, "duplicate key '%s'", path);
	v = xcalloc(1, sizeof(*v));
	*v = (struct yval){ s, at, kind };
	string_list_append(y->kv, path)->util = v;
	return 0;
}

/* a comment: '#' at the start of a line or after a blank */
static int comment(struct yaml *y)
{
	return *y->p == '#' && (y->p == y->text || y->p[-1] == ' ' || y->p[-1] == '\n');
}

/* the rest of the line holds blanks and maybe a comment; then past its end */
static int line_end(struct yaml *y)
{
	y->p += strspn(y->p, " ");
	if (comment(y))
		y->p = strchrnul(y->p, '\n');
	if (*y->p && *y->p != '\n')
		return yaml_fail(y, "unexpected '%c'", *y->p);
	y->p += !!*y->p;
	return 0;
}

/* from a line's start, past blank and comment lines to the next content: its indentation, or -1 at the end */
static int next_line(struct yaml *y)
{
	for (;;) {
		int n = strspn(y->p, " ");

		y->p += n;
		if (comment(y))
			y->p = strchrnul(y->p, '\n');
		if (*y->p != '\n')
			return *y->p ? n : -1;
		y->p++;
	}
}

/* in a flow collection, past blanks, line ends and comments */
static void flow_gap(struct yaml *y)
{
	for (;;) {
		y->p += strspn(y->p, " \n");
		if (!comment(y))
			return;
		y->p = strchrnul(y->p, '\n');
	}
}

/*
 * a scalar: '...' (with '' for '), "..." (escaping only \" and \\), or plain;
 * a plain scalar starts with no indicator and ends at ": ", " #", the line's
 * end, and in FLOW also at ",[]{}"; AT gets where its text starts
 */
static char *scalar(struct yaml *y, int flow, size_t *at)
{
	struct strbuf sb = STRBUF_INIT;
	const char *v = y->p, *stop = flow ? " \n,[]{}" : " \n";
	char q = *v;

	if (q == '\'' || q == '"') {
		*at = v + 1 - y->text;
		for (v++; *v && *v != '\n' && (*v != q || (q == '\'' && v[1] == '\'')); v++) {
			if (q == '"' && *v == '\\' && v[1] != '"' && v[1] != '\\') {
				y->p = v;
				yaml_fail(y, "an escape other than \\\" or \\\\");
				return NULL;
			}
			if ((q == '"' && *v == '\\') || (q == '\'' && *v == '\''))
				v++;
			strbuf_addch(&sb, *v);
		}
		if (*v != q) {
			yaml_fail(y, "an unterminated quote");
			return NULL;
		}
		y->p = v + 1;
		return strbuf_detach(&sb, NULL);
	}
	if (!*v || strchr(",[]{}#&*!|>'\"%@`", *v) || (strchr("-?:", *v) && strchr(stop, v[1] ? v[1] : ' '))) {
		yaml_fail(y, "no value at '%c'", *v && *v != '\n' ? *v : ' ');
		return NULL;
	}
	*at = v - y->text;
	while (*v && *v != '\n' && !(*v == ':' && strchr(stop, v[1] ? v[1] : ' ')) && !(*v == '#' && v[-1] == ' ') &&
	       !(flow && strchr(",[]{}", *v)))
		strbuf_addch(&sb, *v++);
	strbuf_rtrim(&sb);
	y->p = v;
	return strbuf_detach(&sb, NULL);
}

/* a map key and its ':' at the point: the key, or NULL with the point kept when there is none */
static char *key(struct yaml *y, int flow)
{
	const char *start = y->p;
	char *err = y->err, *k;
	size_t at;

	k = scalar(y, flow, &at);
	y->p += strspn(y->p, " ");
	if (k && *y->p == ':') {
		y->p++;
		return k;
	}
	y->p = start;
	y->err = err;
	return NULL;
}

/* where the point stands in its line */
static int column(struct yaml *y)
{
	const char *l = y->p;

	while (l > y->text && l[-1] != '\n')
		l--;
	return y->p - l;
}

static int dash(struct yaml *y)
{
	return *y->p == '-' && (!y->p[1] || y->p[1] == ' ' || y->p[1] == '\n');
}

static int block_map(struct yaml *y, const char *path, int indent);
static int block_list(struct yaml *y, const char *path, int indent);

static int deeper(struct yaml *y)
{
	return ++y->depth > 16 ? yaml_fail(y, "nested too deeply") : 0;
}

/* a flow list "[a, b]" or map "{k: v}" at the point, over as many lines as written */
static int flow(struct yaml *y, const char *path)
{
	char close = *y->p == '[' ? ']' : '}', *k, *s;
	size_t at = y->p - y->text;
	int n;

	if (deeper(y) || yaml_put(y, path, xstrdup(""), at, close == ']' ? 'l' : 'm'))
		return -1;
	y->p++;
	flow_gap(y);
	for (n = 0; *y->p != close; n++) {
		char *sub = yaml_path(path, xstrfmt("%d", n));

		if (close == '}') {
			if (!(k = key(y, 1)))
				return yaml_fail(y, "no key");
			sub = yaml_path(path, k);
			flow_gap(y);
		}
		if (*y->p == '[' || *y->p == '{') {
			if (flow(y, sub))
				return -1;
		} else if (!(s = scalar(y, 1, &at)) || yaml_put(y, sub, s, at, 0)) {
			return -1;
		}
		flow_gap(y);
		if (*y->p == ',') {
			y->p++;
			flow_gap(y);
		} else if (*y->p != close) {
			return yaml_fail(y, "expected ',' or '%c'", close);
		}
	}
	y->p++;
	y->depth--;
	return 0;
}

/* a literal block "|": the lines below more indented than INDENT, clipped to one final newline */
static int literal(struct yaml *y, const char *path, int indent)
{
	struct strbuf sb = STRBUF_INIT;
	size_t at = y->p - y->text;
	int block = -1;

	while (*y->p) {
		const char *e = strchrnul(y->p, '\n');
		int in = strspn(y->p, " "), blank = y->p + in == e;

		if (!blank && in <= indent)
			break;
		if (!blank && block < 0) {
			block = in;
			at = y->p + in - y->text;
		}
		if (!blank && in < block)
			return yaml_fail(y, "a line less indented than the block's first");
		strbuf_add(&sb, y->p + (blank ? e - y->p : block), blank ? 0 : e - y->p - block);
		strbuf_addch(&sb, '\n');
		y->p = e + !!*e;
	}
	while (sb.len > 1 && sb.buf[sb.len - 2] == '\n')
		strbuf_setlen(&sb, sb.len - 1);
	return yaml_put(y, path, block < 0 ? xstrdup("") : strbuf_detach(&sb, NULL), at, 0);
}

/*
 * what stands under a key or dash whose line is done: a block map or list more
 * indented than INDENT, a list at INDENT under a key (UNDER_KEY), or nothing
 */
static int block(struct yaml *y, const char *path, int indent, int under_key)
{
	int n = next_line(y);
	size_t at = y->p - y->text;

	if (n < 0 || n < indent || (n == indent && !(under_key && dash(y)))) {
		y->p -= n < 0 ? 0 : n;
		return yaml_put(y, path, xstrdup(""), at, '?');
	}
	if (dash(y))
		return yaml_put(y, path, xstrdup(""), at, 'l') || block_list(y, path, n);
	if (!key(y, 0))
		return yaml_fail(y, "no key");
	y->p = y->text + at;
	return yaml_put(y, path, xstrdup(""), at, 'm') || block_map(y, path, n);
}

/* what follows ": " or "- " on its line, in the map or list at INDENT */
static int value(struct yaml *y, const char *path, int indent, int under_key)
{
	size_t at;
	char *s;

	y->p += strspn(y->p, " ");
	if (!*y->p || *y->p == '\n' || comment(y))
		return line_end(y) || block(y, path, indent, under_key);
	if (*y->p == '|') {
		y->p++;
		return line_end(y) || literal(y, path, indent);
	}
	if (*y->p == '[' || *y->p == '{')
		return flow(y, path) || line_end(y);
	if (!(s = scalar(y, 0, &at)))
		return -1;
	return yaml_put(y, path, s, at, 0) || line_end(y);
}

/* after a block's entry, the next line at INDENT: 1 to go on, 0 when the block ended, -1 on a bad indentation */
static int next_entry(struct yaml *y, int indent)
{
	int n = next_line(y);

	if (n < 0)
		return 0;
	if (n > indent)
		return yaml_fail(y, "bad indentation");
	y->p -= n < indent ? n : 0;
	return n == indent;
}

/* the entries "KEY: VALUE" of a block map standing at INDENT */
static int block_map(struct yaml *y, const char *path, int indent)
{
	int more;
	char *k;

	if (deeper(y))
		return -1;
	do {
		if (!(k = key(y, 0)))
			return yaml_fail(y, "no key");
		if (*y->p && *y->p != ' ' && *y->p != '\n')
			return yaml_fail(y, "no blank after ':'");
		if (value(y, yaml_path(path, k), indent, 1) || (more = next_entry(y, indent)) < 0)
			return -1;
		if (more && dash(y))
			return yaml_fail(y, "a list item among the entries of a map");
	} while (more);
	y->depth--;
	return 0;
}

/* the items "- VALUE" of a block list standing at INDENT; an item may open a map on its own line */
static int block_list(struct yaml *y, const char *path, int indent)
{
	int n = 0, more;

	if (deeper(y))
		return -1;
	do {
		char *item = yaml_path(path, xstrfmt("%d", n++));
		size_t at;

		y->p++;
		y->p += strspn(y->p, " ");
		at = y->p - y->text;
		if (*y->p && *y->p != '\n' && !comment(y) && key(y, 0)) {
			/* "- k: v": a map whose keys stand where its first one does */
			y->p = y->text + at;
			if (yaml_put(y, item, xstrdup(""), at, 'm') || block_map(y, item, column(y)))
				return -1;
		} else if (value(y, item, indent, 0)) {
			return -1;
		}
		if ((more = next_entry(y, indent)) < 0)
			return -1;
		if (more && !dash(y)) {
			y->p -= indent;
			break;
		}
	} while (more);
	y->depth--;
	return 0;
}

/* a document is a block map, or one flow map (YAML 1.2), and nothing after it */
static void document(struct yaml *y, int indent)
{
	const char *start = y->p;

	if (*y->p == '{') {
		if (flow(y, NULL) || line_end(y))
			return;
	} else if (!key(y, 0)) {
		yaml_fail(y, "no key");
		return;
	} else {
		y->p = start;
		if (block_map(y, NULL, indent))
			return;
	}
	if (next_line(y) >= 0)
		yaml_fail(y, "text after the document");
}

int yaml_parse(const char *text, struct string_list *kv, const char *what, char **err)
{
	struct yaml y = { text, text, what, kv, NULL, 0 };
	int n;

	if (!is_utf8(text))
		yaml_fail(&y, "not UTF-8");
	else if (strpbrk(text, "\t\r"))
		yaml_fail(&y, "a tab or CR");
	else if ((n = next_line(&y)) >= 0)
		document(&y, n);
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

/* spec 2.5.2, 2.5.4, 2.5.6: a spec definition, from a blob or a work-tree file; each text is read and checked once */
struct string_list *def_parse(const char *text, const char *path)
{
	static const char *const exec[] = { "body s", "engine s", "command l", "command.N s", "success l",
					    "success.N i", "inputs l", "inputs.N s", NULL };
	static const char *const prompt[] = { "body s", "engine s", "inputs l", "inputs.N s", NULL };
	static struct string_list parsed = STRING_LIST_INIT_DUP;
	struct string_list_item *hit = string_list_lookup(&parsed, text);
	struct string_list *kv;
	const char *engine;

	if (hit)
		return hit->util;
	kv = xcalloc(1, sizeof(*kv));
	string_list_init_dup(kv);
	yaml(text, kv, path);
	if (yget(kv, "environment"))
		unsupported("environment");
	engine = yget(kv, "engine") ? yget(kv, "engine") : "";
	if (*engine && strcmp(engine, "exec"))
		reject(128, NULL, "bad format: '%s': engine '%s' is not exec or prompt", path, engine);
	yaml_check(kv, *engine ? exec : prompt, path);
	if (!yget(kv, "body") || !*yget(kv, "body") || (*engine && !yget(kv, "command.0")))
		reject(128, NULL, "bad format: '%s': needs 'body', and 'command' with 'engine: exec'", path);
	string_list_insert(&parsed, text)->util = kv;
	return kv;
}

struct string_list *def_get(struct tree *t, const char *path)
{
	const char *oid = blob_at(t, path);

	if (!oid)
		reject(1, NULL, "spec files do not match the contract: missing '%s'", path);
	if (!blob_text(oid))
		reject(128, NULL, "bad format: '%s': not a text file", path);
	return def_parse(blob_text(oid), path);
}
