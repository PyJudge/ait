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
 * One sorted listing per tree, its lookups a bsearch; a commit and its tree
 * share one listing
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

struct entry *tree_get(struct tree *t, const char *path)
{
	struct entry key = { path };

	return bsearch(&key, t->e, t->nr, sizeof(*t->e), cmp_entry);
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

/* the version of the file PATH in T, a listing that holds trees too: no tree entry is a file's version */
struct entry *file_entry(struct tree *t, const char *path)
{
	struct entry *e = tree_get(t, path);

	return e && strcmp(e->mode, "040000") ? e : NULL;
}

struct tree *head_tree(void)
{
	return tree_load(head ? head : EMPTY_TREE);
}

/* the blob of PATH in T: no tree, no submodule's commit */
const char *blob_at(struct tree *t, const char *path)
{
	struct entry *e = file_entry(t, path);

	return e && strcmp(e->mode, "160000") ? e->oid : NULL;
}

/* the text of a blob, or NULL when it holds a NUL and so is not text; each blob is read once */
static const char *blob_text(const char *oid)
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
 * back when it holds '"', a control character or more (git-update-index(1));
 * without MODE, the line that removes PATH
 */
void index_line(struct strbuf *sb, const char *mode, const char *oid, const char *path)
{
	strbuf_addf(sb, "%s %s\t", mode ? mode : "0", mode ? oid : "0000000000000000000000000000000000000000");
	quote_c_style(path, sb, NULL, 0);
	strbuf_addch(sb, '\n');
}

/*
 * BASE in a throwaway index (spec 4.8), or else a copy of the real index
 * with its flags (intent-to-add), then INDEX_INFO (index_line()s), then
 * the work-tree files PATHS staged by git add as git stages them (modes,
 * symlinks, filters, a file becoming a directory; refusing paths in a
 * submodule or outside a sparse checkout); the tree, or NULL with git's
 * reason in git_error, for the caller to judge
 */
char *tree_with(const char *base, const char *index_info, struct string_list *paths)
{
	const char *tmp = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
	char *dir = xstrfmt("%s/ait-XXXXXX", tmp);
	struct strvec add = STRVEC_INIT;
	struct strbuf real = STRBUF_INIT;
	struct string_list_item *item;
	char *index, *tree = NULL;
	int rc;

	if (base && !paths && !*index_info)
		return xstrdup(base);
	/* 5.8 E_IO: the directory 4.8 names cannot be written */
	if (!mkdtemp(dir))
		reject(128, xstrfmt("make '%s' writable (free space, permissions), then rerun this command", tmp),
		       "write error: '%s': %s", dir, strerror(errno));
	index = xstrfmt("%s/index", dir);
	if (base)
		git_run(NULL, index, &rc, "read-tree %s", base);
	else if (!(rc = strbuf_read_file(&real, line1(git("rev-parse --path-format=absolute --git-path index")), 0) < 0))
		write_file_buf(index, real.buf, real.len);
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
 * a file value is "PATH MODE OID def|input". Both start with their path;
 * scope_oid() gives what follows it, "MODE OID def|input" for a file value.
 */
static int range_of(const char *value, int *start, int *end)
{
	const char *colon;
	char *next;
	long a, b;

	skip_prefix(value, "new:", &value);
	colon = strchr(value, ':');
	if (!colon || colon > value + strcspn(value, " "))
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

static const char *scope_oid(const char *v)
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

/* S holds a control character */
static int has_control(const char *s)
{
	for (; *s; s++)
		if ((unsigned char)*s < 0x20 || *s == 0x7f)
			return 1;
	return 0;
}

int is_def(const char *file)
{
	return ends_with(file, " def");
}

/* the canonical key order of spec 2.2.2 */
static const char *keys[NKEYS] = { "role", "state", "consumer", "provider", "target", "file", "check", "creator", "prev" };

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

int is_current(struct record *r)
{
	return *r->id != 'u' && in_state(r, "current");
}

/* 1.3: R is a contract the agent ME made */
int made_by(struct record *r, const char *me)
{
	return me && r && !strcmp(scope_path(val(r, CREATOR)), me);
}

/* current in HEAD: only such a provider shows "?changed" (spec 3.8) */
int was_current(struct record *r)
{
	return r->old && is_current(r->old);
}

static int cmp_value(const void *a, const void *b)
{
	const char *x = ((const struct string_list_item *)a)->string;
	const char *y = ((const struct string_list_item *)b)->string;

	return strcmp(x, y);
}

/* spec 3.2a: path bytes, whole file, then numeric start and end */
static int cmp_scope(const void *a, const void *b)
{
	const char *x = ((const struct string_list_item *)a)->string;
	const char *y = ((const struct string_list_item *)b)->string;
	char *xp = scope_path(x), *yp = scope_path(y);
	int xs = 0, xe = 0, ys = 0, ye = 0;
	int order = strcmp(xp, yp);
	int xr = range_of(x, &xs, &xe), yr = range_of(y, &ys, &ye);

	free(xp);
	free(yp);
	if (order)
		return order;
	if (xr != yr)
		return xr < yr ? -1 : 1;
	if (xs != ys)
		return xs < ys ? -1 : 1;
	if (xe != ye)
		return xe < ye ? -1 : 1;
	return strcmp(x, y);
}

/* Mapping can collapse scopes; both repins and moved hints use the same list (3.2a). */
static void normalize_scopes(struct string_list *scopes)
{
	size_t src, dst = 0;

	QSORT(scopes->items, scopes->nr, cmp_scope);
	for (src = 0; src < scopes->nr; src++) {
		if (dst && !strcmp(scopes->items[dst - 1].string, scopes->items[src].string))
			free(scopes->items[src].string);
		else
			scopes->items[dst++] = scopes->items[src];
	}
	scopes->nr = dst;
}

static void record_sort(struct record *r)
{
	int k;

	for (k = 0; k < NKEYS; k++)
		QSORT(r->f[k].items, r->f[k].nr, k == CONSUMER || k == PROVIDER ? cmp_scope : cmp_value);
}

/* the canonical lines of R (2.2.1) for the keys in KEYS_WANTED, a bit per enum key; R's values are sorted first */
char *record_text(struct record *r, unsigned keys_wanted)
{
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;
	int k;

	record_sort(r);
	for (k = 0; k < NKEYS; k++) {
		if (!(keys_wanted & (1u << k)))
			continue;
		for_each_string_list_item(item, &r->f[k])
			strbuf_addf(&sb, "%s %s\n", keys[k], item->string);
	}
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

/* a record with no values yet */
static struct record *record_new(const char *id)
{
	struct record *r = xcalloc(1, sizeof(*r));
	int k;

	snprintf(r->id, sizeof(r->id), "%s", id);
	for (k = 0; k < NKEYS; k++)
		string_list_init_dup(&r->f[k]);
	return r;
}

/* a record a direct commit removes may go (2.1.5, 5.10), so the state it judges lacks it */
void state_drop(struct state *s, const char *id)
{
	size_t i;

	for (i = 0; i < s->nr; i++)
		if (!strcmp(s->r[i]->id, id)) {
			MOVE_ARRAY(s->r + i, s->r + i + 1, s->nr - i - 1);
			s->nr--;
			return;
		}
}

struct record *state_add(struct state *s, const char *id)
{
	struct record *r = record_new(id);

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

/* the ID of L's demand or offer: a link "undertake/D…-O…" names them */
const char *link_end_id(struct record *l, int offer)
{
	return offer ? l->id + 24 : xstrndup(l->id + 10, 13);
}

struct record *link_end(struct state *s, struct record *l, int offer)
{
	return state_get(s, link_end_id(l, offer));
}

/* the other end of L when L is a readable undertaken link with R at one end, else NULL */
struct record *partner(struct state *s, struct record *l, struct record *r)
{
	if (!l || !undertaken(l) || (link_end(s, l, 0) != r && link_end(s, l, 1) != r))
		return NULL;
	return link_end(s, l, link_end(s, l, 0) == r);
}

/*
 * spec 2.2.1: the lines of record TEXT into R by key, each "KEY VALUE"; WRITE
 * (2.2.3) keeps of the values what is written by hand and worked out again
 * by the gate (6.2): the scope arguments and the definitions. 0 when every
 * line reads.
 */
static int record_lines(struct record *r, const char *text, int written)
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
		if (k == NKEYS || !v[1] || isspace(v[1]) || isspace(l[strlen(l) - 1]) || has_control(l)) {
			r->bad = xstrfmt("bad line '%s'", l);
			return -1;
		}
		v++;
		if (!written)
			string_list_append(&r->f[k], v);
		else if (k == CONSUMER || k == PROVIDER)
			string_list_append(&r->f[k], xstrndup(v, strcspn(v, " ")));
		else if (k == FILES && !ends_with(v, " input"))
			string_list_append(&r->f[k], xstrfmt("%.*s - - def", (int)strcspn(v, " "), v));
		else if (k != FILES && k != CHECK && k != PREV)
			string_list_append(&r->f[k], v);
	}
	return 0;
}

/* spec 2.2.1: a stored record reads back as exactly its canonical text */
static void record_parse(struct record *r, const char *text)
{
	if (!record_lines(r, text, 0) && strcmp(record_text(r, ALL_KEYS), text))
		r->bad = xstrdup("not in canonical form");
}

/*
 * spec 2.2.3: record ID as written by hand in TEXT, in the authoring form:
 * keys in any order; the BLOBs, the file modes and kinds, check, creator and
 * prev may be left out, and what is written of them but the creator is
 * worked out again (6.2). Every line ends with LF, as a stored record's does.
 */
struct record *record_written(const char *id, const char *text)
{
	struct record *r = record_new(id);

	if (*text && !ends_with(text, "\n"))
		r->bad = xstrdup("its last line does not end with a line feed");
	else
		record_lines(r, text, 1);
	return r;
}

/* spec 1.9 and 2.1: "D…", "O…" or "undertake/D…-O…" */
static int id_ok(const char *id)
{
	regex_t re;
	int ok;

	regcomp(&re, "^([DOX][0-9a-f]{12}|undertake/D[0-9a-f]{12}-O[0-9a-f]{12})$", REG_EXTENDED | REG_NOSUB);
	ok = !regexec(&re, id, 0, NULL, 0);
	regfree(&re);
	return ok;
}

/* one of LETTERS and 12 hex digits: the shape of the IDs ait makes (1.9) */
int id_like(const char *s, const char *letters)
{
	return *s && strchr(letters, *s) && strlen(s) == 13 && strspn(s + 1, "0123456789abcdef") == 12;
}

/* spec 1.9: a session ID, S and 12 hex digits */
int sid_ok(const char *s)
{
	return s && id_like(s, "S");
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
	int ok = *p && *p != '/' && *p != '-' && strcmp(p, ".ait") && !starts_with(p, ".ait/") &&
		 !p[strcspn(p, " :,")] && !has_control(p);

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
	int start, end, range = range_of(v, &start, &end);

	if (range < 0 || (range && starts_with(v, "new:")))
		return 0;
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
		return n == 2 && !strcmp(v, xstrfmt("%s %s", a, b)) && (!strcmp(v, "unknown -") || (agent_ok(a) && sid_ok(b)));
	case PREV:
		return !strcmp(v, "-") || oid_ok(v);
	case TARGET:
		return id_like(v, "DO");
	case CHECK:
		return n == 2 && (!strcmp(a, "reported") || !strcmp(a, "forced")) && oid_ok(b) &&
		       !strcmp(v, xstrfmt("%s %s", a, b));
	default:
		return 1;
	}
}

static int fail_with(struct verdict *v, int code, const char *word, char *hint, char *msg)
{
	*v = (struct verdict){ code, word, hint, msg, NULL, NULL };
	return code;
}

/* 5.10: a malformed record stops with its error; the hint is where it is (head_repair() for HEAD's) */
static int bad_record(struct verdict *v, struct record *r, const char *why)
{
	fail_with(v, 128, "format", NULL, xstrfmt("bad format: '.ait/%s': %s", r->id, why));
	v->k = r;
	return 128;
}

static const char *const contract_states[] = { "future", "current", "cancelled", "withdrawn", NULL };
static const char *const link_states[] = { "undertaken", "released", NULL };
static const char *const drop_states[] = { "future", "cancelled", "fulfilled", NULL };

/* R means what its base version means (prev aside, 6.2): HEAD's, or GOOD's when entangled with HEAD's errors (5.10) */
int as_in_base(struct record *r)
{
	return r->old && !strcmp(record_text(r, MEANING_KEYS), record_text(r->old, MEANING_KEYS));
}

/* spec 5.9 a-c for one record: how many values each key has, each value, role and state; NULL when it holds */
static char *record_check(struct record *r)
{
	/* per kind: how many values each key takes ('1' one, '+' one or more, '0' none), its role and its states */
	static const struct kind { const char *counts, *role; const char *const *states; } kinds[] = {
		{ "11++0+?1+", "demand", contract_states },
		{ "110+0+?1+", "offer", contract_states },
		{ "010000001", "", link_states },
		{ "11001001+", "drop", drop_states },
	};
	const struct kind *kind = &kinds[*r->id == 'D' ? 0 : *r->id == 'O' ? 1 : *r->id == 'X' ? 3 : 2];
	const char *c = kind->counts;
	struct string_list_item *item;
	int k;

	for (k = 0; k < NKEYS; k++) {
		if (c[k] == '?' ? r->f[k].nr > 1 : c[k] == '+' ? !r->f[k].nr : r->f[k].nr != (size_t)(c[k] - '0'))
			return xstrfmt("wrong number of '%s' lines", keys[k]);
		for_each_string_list_item(item, &r->f[k])
			if (!value_ok(r, k, item->string))
				return xstrfmt("bad '%s' line '%s'", keys[k], item->string);
	}
	if (strcmp(val(r, ROLE), kind->role) || !one_of(val(r, STATE), kind->states))
		return xstrdup("role or state does not fit the record");
	if ((*r->id == 'D' || *r->id == 'O') && !strstr(record_text(r, 1u << FILES), " def\n"))
		return xstrdup("no spec definition");
	return NULL;
}

/* spec 2.3: the allowed transitions of a demand or offer ('x') and of a link ('u'), and who makes them */
static const struct move {
	char kind;
	const char *from, *to;
	unsigned gate : 1, fulfill : 1;
} moves[] = {
	{ 'x', "-", "future", 1, 0 }, { 'x', "-", "current", 1, 0 }, { 'x', "future", "future", 1, 0 },
	{ 'x', "future", "cancelled", 1, 0 }, { 'x', "future", "current", 0, 1 }, { 'x', "current", "current", 1, 1 },
	{ 'x', "current", "future", 1, 0 }, { 'u', "-", "undertaken", 1, 0 }, { 'u', "released", "undertaken", 1, 0 },
	{ 'u', "undertaken", "released", 1, 0 }, { 'x', "current", "withdrawn", 0, 1 },
	{ 'd', "-", "future", 1, 0 }, { 'd', "future", "future", 1, 0 },
	{ 'd', "future", "cancelled", 1, 0 }, { 'd', "future", "fulfilled", 0, 1 },
};

int record_step(const char *id, const char *from, const char *to, int gate)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(moves); i++)
		if (moves[i].kind == (*id == 'u' ? 'u' : *id == 'X' ? 'd' : 'x') && !strcmp(moves[i].from, from) &&
		    !strcmp(moves[i].to, to))
			return gate < 0 ? moves[i].gate || moves[i].fulfill : gate ? moves[i].gate : moves[i].fulfill;
	return 0;
}


/* 2.2: historical versions use the same canonical parser as stored records */
struct record *record_decode(const char *id, const char *oid)
{
	struct record *r = record_new(id);
	const char *text = blob_text(oid), *line, *next;

	if (!text) {
		r->bad = xstrdup("not a text file");
		return r;
	}
	record_parse(r, text);
	if (r->bad) {
		/* 7.12: a malformed record still declares its raw file rows */
		for (line = text; line && *line; line = next ? next + 1 : NULL) {
			char path[4096], mode[8], blob[48];

			next = strchr(line, '\n');
			if (sscanf(next ? xstrndup(line, next - line) : line, " file %4095s %7s %47s", path, mode, blob) == 3)
				string_list_append(&r->f[FILES], xstrfmt("%s %s %s input", path, mode, blob));
		}
		return r;
	}
	r->bad = record_check(r);
	if (r->f[PREV].nr > 1 && string_list_has_string(&r->f[PREV], "-"))
		r->bad = xstrdup("initial prev mixed with versions");
	return r;
}

/* Entry validity belongs to the tree, not the blob's version cache (2.1); WRITTEN reads it in the authoring form (2.2.3) */
struct record *record_from_entry(struct entry *e, const char *id, int written)
{
	struct record *r = record_new(id);
	const char *text = strcmp(e->mode, "100644") ? NULL : blob_text(e->oid);

	if (!strcmp(id, "init")) {
		r->bad = text && !strcmp(text, "version 2\n") ? NULL : xstrdup("must be the single line 'version 2'");
	} else if (strcmp(e->mode, "100644") || !id_ok(id)) {
		/* 7.12: malformed entries still declare changes that must be excluded. */
		if (id_ok(id))
			*r = *record_decode(id, e->oid);
		r->bad = xstrdup("not a record path");
		r->mend = xstrfmt("remove '.ait/%s'", id);
	} else if (!text) {
		r->bad = xstrdup("not a text file");
	} else {
		*r = *(written ? record_written(id, text) : record_decode(id, e->oid));
	}
	return r;
}

/* the records of T; a malformed one carries why in ->bad (spec 5.9 a-c, 5.10) */
void state_load(struct state *s, struct tree *t)
{
	size_t i;

	for (i = 0; i < t->nr; i++) {
		struct entry *e = &t->e[i];
		const char *id;
		struct record *r;

		if (!skip_prefix(e->path, ".ait/", &id) || !strcmp(e->mode, "040000"))
			continue;
		r = state_add(s, id);
		*r = *record_from_entry(e, id, 0);
		r->old = record_copy(r);
		r->base = xstrdup(e->oid);
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
		}
	}
}

static void hunk_part(const char *text, int *start, int *length)
{
	char *next;

	*start = strtol(text, &next, 10);
	*length = *next == ',' ? strtol(next + 1, NULL, 10) : 1;
}

enum scope_status {
	SCOPE_FOUND,
	SCOPE_NOBASE,
	SCOPE_MISSING,
	SCOPE_DELETED,
};

static enum scope_status map_range(const char *scope, struct tree *t, int *start, int *end, int *changed)
{
	const char *base = scope_oid(scope), *now = blob_at(t, scope_path(scope));
	struct string_list lines = STRING_LIST_INIT_DUP;
	struct string_list_item *line;
	int a, b, ma = -1, mb = -1, offset = 0;

	range_of(scope, &a, &b);
	*changed = 0;
	if (!now)
		return SCOPE_MISSING;
	if (!base || !object(base, 0, NULL, NULL))
		return SCOPE_NOBASE;
	string_list_split(&lines, git("diff -U0 --no-color --no-ext-diff --no-textconv --text --inter-hunk-context=0 --diff-algorithm=myers --no-indent-heuristic %s %s", base, now), "\n", -1);
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
	return *start > *end ? SCOPE_DELETED : SCOPE_FOUND;
}

/* A file's presence is not a range's presence (spec 1.5c, 3.7). */
struct scope_location {
	char *path;
	const char *oid;
	int range;
	int start;
	int end;
	int changed;
	enum scope_status status;
};

static struct scope_location locate_scope(const char *scope, struct tree *t)
{
	struct scope_location place = { .path = scope_path(scope) };

	place.oid = blob_at(t, place.path);
	place.range = range_of(scope, &place.start, &place.end) == 1;
	if (place.range)
		place.status = map_range(scope, t, &place.start, &place.end, &place.changed);
	else if (!ends_with(place.path, "/")) {
		place.status = place.oid ? SCOPE_FOUND : SCOPE_MISSING;
		place.changed = !place.oid || !scope_oid(scope) || strcmp(place.oid, scope_oid(scope));
	} else
		place.changed = 1;
	return place;
}

static char *location_arg(struct scope_location *place)
{
	return place->range ? xstrfmt("%s:%d-%d", place->path, place->start, place->end) : xstrdup(place->path);
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
		ignorecase = config_true("core.ignorecase");
	for (i = 0; ignorecase && i < t->nr; i++)
		if (!strcasecmp(t->e[i].path, path) && strcmp(t->e[i].path, path))
			return t->e[i].path;
	return NULL;
}

/* spec 3.3-3.4: a scope argument fixed at tree T, or NULL when T does not have it so */
char *pin(const char *arg, struct tree *t, int allow_new, int allow_abs)
{
	char *dir = ends_with(arg, "/") ? xstrndup(arg, strlen(arg) - 1) : scope_path(arg);
	const char *path = dir;
	int is_new = skip_prefix(arg, "new:", &path);
	struct entry *e;

	/* the shape of ARG was checked before (scope_shape, 5.13-1); here is what T has */
	if (is_new && allow_new && !tree_get(t, path) && !case_twin(t, path))
		return xstrdup(arg);
	if (!strcmp(arg, "/") && allow_abs)
		return xstrfmt("/ %s", t->oid);
	e = tree_get(t, dir);
	if (e && ends_with(arg, "/") && allow_abs && !strcmp(e->mode, "040000"))
		return xstrfmt("%s %s", arg, e->oid);
	if (!is_new && e && !ends_with(arg, "/") && blob_at(t, dir)) {
		int start, end, lines = 0;
		const char *text, *p;

		if (range_of(arg, &start, &end)) {
			text = blob_text(e->oid);
			if (!text)
				return NULL;
			for (p = text; *p; p++)
				lines += *p == '\n';
			lines += p > text && p[-1] != '\n';
			if (end > lines)
				return NULL;
		}
		return xstrfmt("%s %s", arg, e->oid);
	}
	return NULL;
}

/*
 * 5.9a: the scope VALUE as a hint's command takes it at T, where it runs: a
 * new file T has is its path, a range the lines it maps to there, and one T
 * cannot find "<scope>", as its old line numbers would pin other lines (3.3)
 */
char *input_scope(const char *value, struct tree *t)
{
	struct scope_location place = locate_scope(value, t);
	char *in = ends_with(place.path, "/") ? place.path : starts_with(value, "new:") ?
		   (place.oid ? place.path : xstrfmt("new:%s", place.path)) : place.status ? NULL : location_arg(&place);

	/* and only what T takes as that input (3.3) */
	return in && pin(in, t, 1, 1) ? in : xstrdup("<scope>");
}

/* R's scopes of KEY as one argument at T (5.9a), each quoted for the shell when it would read it; "<scope>" stays as it is */
char *input_scopes(struct record *r, enum key k, struct tree *t)
{
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;

	for_each_string_list_item(item, &r->f[k]) {
		char *in = input_scope(item->string, t);

		if (sb.len)
			strbuf_addch(&sb, ',');
		if (!strcmp(in, "<scope>"))
			strbuf_addstr(&sb, in);
		else
			sq_quote_buf_pretty(&sb, in);
	}
	return strbuf_detach(&sb, NULL);
}

/*
 * R's definitions joined by ',' as one helper argument, quoted for the shell
 * when it would read them (5.9a, git v2.53.0 quote.c sq_quote_buf_pretty())
 */
char *def_paths(struct record *r)
{
	struct strbuf sb = STRBUF_INIT, arg = STRBUF_INIT;
	struct string_list_item *item;

	for_each_string_list_item(item, &r->f[FILES])
		if (is_def(item->string))
			strbuf_addf(&sb, "%s%.*s", sb.len ? "," : "", (int)strcspn(item->string, " "), item->string);
	sq_quote_buf_pretty(&arg, sb.buf);
	strbuf_release(&sb);
	return strbuf_detach(&arg, NULL);
}

/* validate(state) of spec 5.9 a-d for record R of S, no parents involved; 0 when it holds */
static int record_error(struct state *s, struct record *r, struct verdict *v)
{
	struct record *d, *o;
	char *why;
	size_t j;

	if (r->bad)
		return bad_record(v, r, r->bad);
	if (!strcmp(r->id, "init"))
		return 0;
	if ((why = record_check(r)))
		return bad_record(v, r, why);
	/* 2.2.2: only a current contract has a check line; the version still reads, so the integration hook drops it (7.1.3-2) */
	if (r->f[CHECK].nr && !is_current(r))
		return bad_record(v, r, "check on a contract that is not current");
	if (*r->id == 'X') {
		struct record *target = state_get(s, val(r, TARGET));

		if (!target || (*target->id != 'D' && *target->id != 'O'))
			return bad_record(v, r, "its target does not exist");
		if (in_state(r, "fulfilled") && !in_state(target, "withdrawn")) {
			fail_with(v, 1, "state", NULL, xstrfmt("not allowed: fulfilled '%s' targets '%s' which is not withdrawn", r->id, target->id));
			v->k = target;
			v->o = r;
			return 1;
		}
	}
	if (in_state(r, "withdrawn")) {
		for (j = 0; j < s->nr; j++)
			if (*s->r[j]->id == 'X' && in_state(s->r[j], "fulfilled") && !strcmp(val(s->r[j], TARGET), r->id))
				break;
		if (j == s->nr) {
			fail_with(v, 1, "state", NULL, xstrfmt("not allowed: '%s' is withdrawn without a drop", r->id));
			v->k = r;
			return 1;
		}
	}
	if (*r->id != 'u')
		return 0;
	d = link_end(s, r, 0);
	o = link_end(s, r, 1);
	if (!in_state(r, "undertaken") || !is_current(o) || !in_state(d, "future"))
		return 0;
	fail_with(v, 1, "state", was_current(d) ?
		  xstrfmt("'%s' is undertaken by current '%s'; reopen them in the same request: "
			  "'ait future amend %s future amend %s -m <why>'", d->id, o->id, d->id, o->id) :
		  xstrfmt("undertake it with a future offer: 'ait future offer %s %s as o undertake %s o -m <why>'",
			  input_scopes(d, PROVIDER, head_tree()), def_paths(d), d->id),
		  xstrfmt("not allowed: current '%s' undertakes future '%s'", o->id, d->id));
	v->k = d;
	v->o = o;
	return 1;
}

/* validate(state) of spec 5.9 a-d: the first record of S with an error; 0 when there is none */
int check_records(struct state *s, struct verdict *v)
{
	size_t i;

	for (i = 0; i < s->nr; i++)
		if (record_error(s, s->r[i], v))
			return v->code;
	return 0;
}

/* why R cannot be shown from T: malformed, or a definition it declares is not a text file in T (NULL when it can) */
const char *unusable(struct record *r, struct tree *t)
{
	struct string_list_item *item;

	if (r->bad)
		return r->bad;
	if (!is_live(r))
		return NULL;
	for_each_string_list_item(item, &r->f[FILES]) {
		const char *oid = blob_at(t, scope_path(item->string));

		if (is_def(item->string) && (!oid || !blob_text(oid)))
			return xstrfmt("its definition '%s' is not there", scope_path(item->string));
	}
	return NULL;
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

/*
 * spec 3.1-3.2: the shape of a scope argument, judged before any tree is read
 * (5.13-1); new:PATH names a file, and '/' and 'DIR/' name none
 */
void scope_shape(const char *arg)
{
	const char *p = arg;
	int is_new = skip_prefix(arg, "new:", &p), start, end, range = range_of(arg, &start, &end);

	if (range < 0 || (range && is_new))
		reject(128, NULL, "bad scope: '%s'", arg);
	if (range)
		p = scope_path(arg);
	if ((is_new && ends_with(p, "/")) ||
	    (strcmp(p, "/") && !path_ok(ends_with(p, "/") ? xstrndup(p, strlen(p) - 1) : p)))
		reject(128, NULL, "bad scope: '%s'", arg);
}

/*
 * spec 3.9: the argument scope ARG, read at T, overlaps scope R, which is
 * mapped to T; a range R cannot map there is its whole file
 */
int overlap(const char *arg, const char *r, struct tree *t)
{
	const char *a = scope_path(arg), *b = scope_path(r);
	struct scope_location place;
	int start, end;

	if (!strcmp(a, "/") || !strcmp(b, "/"))
		return 1;
	if (ends_with(a, "/") || ends_with(b, "/"))
		return (ends_with(a, "/") && starts_with(b, a)) || (ends_with(b, "/") && starts_with(a, b));
	if (strcmp(a, b))
		return 0;
	place = locate_scope(r, t);
	if (range_of(arg, &start, &end) != 1 || !place.range || place.status != SCOPE_FOUND)
		return 1;
	return start <= place.end && place.start <= end;
}

int scope_changed(const char *scope, struct tree *t)
{
	struct scope_location place = locate_scope(scope, t);

	return place.status || place.changed;
}

/* spec 3.8: where a scope is in T; "?changed" only for the provider of a current contract */
char *show(const char *scope, struct tree *t, int current)
{
	struct scope_location place = locate_scope(scope, t);

	if (place.range) {
		if (place.status)
			return xstrfmt("%.*s ?%s", (int)strcspn(scope, " "), scope, place.status == SCOPE_NOBASE ? "nobase" : place.status == SCOPE_MISSING ? "missing" : "deleted");
		return xstrfmt("%s%s", location_arg(&place), current && place.changed ? " ?changed" : "");
	}
	if (starts_with(scope, "new:"))
		return xstrfmt("%s%s", scope, place.oid ? " ?exists" : "");
	if (ends_with(place.path, "/"))
		return place.path;
	if (place.status)
		return xstrfmt("%s ?missing", place.path);
	return current && place.changed ? xstrfmt("%s ?changed", place.path) : place.path;
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
	struct string_list scopes = STRING_LIST_INIT_DUP;
	struct string_list_item *item;

	for_each_string_list_item(item, &r->f[PROVIDER]) {
		struct scope_location place = locate_scope(item->string, t);

		if (!place.status && place.oid)
			string_list_append(&scopes, location_arg(&place));
	}
	normalize_scopes(&scopes);
	for_each_string_list_item(item, &scopes)
		strbuf_addf(&kept, "%s%s", kept.len ? "," : "", item->string);
	if (kept.len) {
		sq_quote_buf_pretty(&arg, kept.buf);
		strbuf_addch(&arg, ',');
	}
	strbuf_addstr(&arg, "<new scope>");
	return strbuf_detach(&arg, NULL);
}

/*
 * spec 5.9a and 5.9 d: what reopening K reopens with it, as S has them: K,
 * and when K is a current demand, each current offer that undertakes it
 */
void reopen_set(struct state *s, struct record *k, struct string_list *out)
{
	size_t i;

	if (!unsorted_string_list_has_string(out, k->id))
		string_list_append(out, k->id)->util = k;
	if (*k->id == 'D' && (was_current(k) || is_current(k)))
		for (i = 0; i < s->nr; i++) {
			struct record *o = partner(s, s->r[i], k);

			if (o && (was_current(o) || is_current(o)) && !unsorted_string_list_has_string(out, o->id))
				string_list_append(out, o->id)->util = o;
		}
}

/* the clause amending K to PROVIDER and FILES, "-" keeping what K has; a demand keeps its consumer (7.3.2) */
char *amend_clause(struct record *k, const char *provider, const char *files)
{
	return xstrfmt("amend %s %s%s %s", k->id, *k->id == 'D' ? "- " : "", provider, files);
}

/* "future amend K future amend O..." reopening K and what it takes along (5.9a, 5.9 d); K's clause ends in ARGS when given */
char *reopen_clauses(struct state *s, struct record *k, const char *args)
{
	struct string_list set = STRING_LIST_INIT_DUP;
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;

	reopen_set(s, k, &set);
	for_each_string_list_item(item, &set)
		strbuf_addf(&sb, "%sfuture amend %s%s", sb.len ? " " : "", item->string, item->util == k && args ? args : "");
	return strbuf_detach(&sb, NULL);
}

/* the reopen hint of 5.9a, run on its own */
char *reopen_hint(struct state *s, struct record *k)
{
	return xstrfmt("to change '%s' without a check, reopen it: 'ait %s -m <why>'", k->id, reopen_clauses(s, k, NULL));
}

/* spec 1.5c: absent files and unmappable ranges cannot be checked */
void missing_code(struct record *r, struct tree *t, struct string_list *out)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &r->f[PROVIDER]) {
		struct scope_location place = locate_scope(item->string, t);

		if (place.status)
			string_list_append(out, item->string);
	}
}

/*
 * 9.2, 1.5e: version B of a contract means what A means: they differ at most
 * in prev, check and pins, and each scope of B is A's pinned again: the same
 * path, and a range A's mapped onto B's blob (3.7)
 */
int same_meaning(struct record *a, struct record *b)
{
	static const enum key ks[] = { CONSUMER, PROVIDER };
	struct string_list_item *x, *y;
	size_t k;

	if (strcmp(record_text(a, MEANING_KEYS & ~((1u << CONSUMER) | (1u << PROVIDER))),
		   record_text(b, MEANING_KEYS & ~((1u << CONSUMER) | (1u << PROVIDER)))))
		return 0;
	for (k = 0; k < ARRAY_SIZE(ks); k++) {
		if (a->f[ks[k]].nr != b->f[ks[k]].nr)
			return 0;
		for (x = a->f[ks[k]].items, y = b->f[ks[k]].items; x < a->f[ks[k]].items + a->f[ks[k]].nr; x++, y++) {
			struct entry e = { .path = scope_path(y->string), .mode = "100644" };
			struct tree one = { "", 1, &e };
			struct scope_location place;
			int s, t;

			if (!strcmp(x->string, y->string))
				continue;
			if (strcmp(scope_path(x->string), scope_path(y->string)))
				return 0;
			if (!range_of(y->string, &s, &t))
				continue;
			snprintf(e.oid, sizeof(e.oid), "%s", scope_oid(y->string));
			place = locate_scope(x->string, &one);
			if (place.status || strcmp(location_arg(&place), xstrndup(y->string, strcspn(y->string, " "))))
				return 0;
		}
	}
	return 1;
}

/* the provider of R can be found in T: each scope that names a file is one there (spec 1.5c, 7.7.3-4) */
int code_found(struct record *r, struct tree *t)
{
	struct string_list missing = STRING_LIST_INIT_DUP;

	missing_code(r, t, &missing);
	return !missing.nr;
}

/* spec 3.11: R's code changed since its pins: one of its provider scopes changed, moved or is gone at T */
int code_changed(struct record *r, struct tree *t)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &r->f[PROVIDER])
		if (scope_changed(item->string, t))
			return 1;
	return 0;
}

/*
 * spec 1.5b: the change A to B touches current R, whose provider is found in
 * A: the diff touches a scope as A has it (3.10), or the code was as pinned
 * at A and is not at B (3.11), which catches what an alignment of A->B misses
 */
int touches(struct record *r, struct tree *a, struct tree *b)
{
	struct string_list_item *item;

	if (!r || !is_current(r) || !code_found(r, a))
		return 0;
	for_each_string_list_item(item, &r->f[PROVIDER]) {
		struct scope_location place = locate_scope(item->string, a);
		const char *now = blob_at(b, place.path);

		if ((!now || strcmp(place.oid, now)) &&
		    (!place.range || scope_changed(xstrfmt("%s %s", location_arg(&place), place.oid), b)))
			return 1;
	}
	return !code_changed(r, a) && code_changed(r, b);
}

/*
 * spec 7.7.4: pin the scopes of KEY at T where their file is; NEW_ONLY turns
 * only "new:PATH" into "PATH OID", otherwise pinned scopes take T's blob
 */
void repin(struct record *r, enum key k, struct tree *t, int new_only)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &r->f[k]) {
		struct scope_location place = locate_scope(item->string, t);

		if (!place.status && place.oid && starts_with(item->string, "new:") == new_only) {
			char *pinned = xstrfmt("%s %s", location_arg(&place), place.oid);

			free(item->string);
			item->string = pinned;
		}
	}
	normalize_scopes(&r->f[k]);
}

/*
 * The YAML subset of spec 2.5.1, read by libyaml as YAML places it. What the
 * subset leaves out shows in libyaml's events and is refused here: aliases,
 * anchors, tags, directives, a second document, folded blocks, keys that are
 * not plain or quoted scalars, a root that is not a map. The markers "---"
 * and "..." of the one document are YAML's; libyaml's emitter writes "..."
 * itself. The values are flattened into KV: "a: x" is "a", list items are
 * "a.0", keys inside them "a.0.b"; '.' and '\' in a key are escaped, so a
 * path names one value. Each value keeps where it stands in the text, its
 * kind and style, its pattern for the schemas and its parent.
 */

/* the path the root map is kept under: no key gives it, as yaml_path() escapes a '\\' of a key */
#define ROOT "\\"

/* how deep collections may nest */
#define YAML_DEPTH 16

struct yval {
	char *s;		/* the value; "" for a map or a list */
	char kind;		/* 'l' a list, 'm' a map, '?' a key with nothing under it, 0 a scalar */
	int plain, block;	/* a plain scalar; a "|" block */
	size_t start, at, end;	/* where the node starts, where the value's text starts, where the node ends */
	int line;		/* the line the node starts on, from 1 */
	int literal;		/* the text from AT spells the value as it is */
	char *pattern;		/* its path with "N" for each list index, as schemas name it (NULL: the root) */
	struct yval *parent;	/* the map or list it is in (NULL: the root) */
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

/* an anchor or a tag, which the subset leaves out, or NULL */
static const char *decorated(const yaml_char_t *anchor, const yaml_char_t *tag)
{
	return anchor ? "an anchor" : tag ? "a tag" : NULL;
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

/* an open map or list: its path and pattern, the key read for its next value, the count of its items, its node */
struct yframe {
	char *path, *pattern, *key;
	int n;
	char kind;
	struct yval *node;
};

/*
 * where the node of E goes under TOP, the innermost open collection (NULL at
 * the root): its path, and its PATTERN; NULL when it is the root map, or the
 * key of TOP's next value (kept in TOP), or with WHY set
 */
static char *yaml_place(struct yframe *top, yaml_event_t *e, char **pattern, const char **why)
{
	char *path;

	if (!top) {
		if (e->type != YAML_MAPPING_START_EVENT)
			*why = "no key";
		return NULL;
	}
	if (top->kind == 'l') {
		*pattern = yaml_path(top->pattern, "N");
		return yaml_path(top->path, xstrfmt("%d", top->n++));
	}
	if (top->key) {
		*pattern = yaml_path(top->pattern, top->key);
		path = yaml_path(top->path, top->key);
		top->key = NULL;
		return path;
	}
	/* 2.5.1: a key is a plain or quoted scalar, no "|" block */
	if (e->type == YAML_SCALAR_EVENT && e->data.scalar.style != YAML_LITERAL_SCALAR_STYLE)
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

/* the node of E in TEXT, with its PATTERN and PARENT */
static struct yval *yaml_value(const char *text, yaml_event_t *e, char *pattern, struct yval *parent)
{
	struct yval *v = xcalloc(1, sizeof(*v));
	int scalar = e->type == YAML_SCALAR_EVENT;

	v->s = xstrdup(scalar ? (char *)e->data.scalar.value : "");
	v->kind = yaml_kind(e);
	v->plain = scalar && e->data.scalar.style == YAML_PLAIN_SCALAR_STYLE;
	v->block = scalar && e->data.scalar.style == YAML_LITERAL_SCALAR_STYLE;
	v->start = yaml_byte(text, e->start_mark);
	v->end = yaml_byte(text, e->end_mark);
	/* a quoted scalar's text starts after its quote */
	v->at = v->start + (scalar && (e->data.scalar.style == YAML_SINGLE_QUOTED_SCALAR_STYLE ||
				       e->data.scalar.style == YAML_DOUBLE_QUOTED_SCALAR_STYLE));
	v->line = e->start_mark.line + 1;
	v->literal = !strncmp(text + v->at, v->s, strlen(v->s));
	v->pattern = pattern;
	v->parent = parent;
	return v;
}

/* one event of the document into KV, with the open collections in STACK: NULL, or what is wrong */
static const char *yaml_event(const char *text, struct string_list *kv, struct yframe *stack, int *depth,
			      yaml_event_t *e)
{
	const char *why = NULL;
	struct yval *v = NULL;
	char *path, *pattern = NULL;

	if (e->type == YAML_MAPPING_END_EVENT || e->type == YAML_SEQUENCE_END_EVENT)
		(*depth)--;
	if (e->type != YAML_SCALAR_EVENT && e->type != YAML_MAPPING_START_EVENT && e->type != YAML_SEQUENCE_START_EVENT)
		return NULL;
	/* a value is text, as a file ait reads is (an escaped NUL would cut it) */
	if (e->type == YAML_SCALAR_EVENT && strlen((char *)e->data.scalar.value) != e->data.scalar.length)
		return "a NUL character";
	path = yaml_place(*depth ? &stack[*depth - 1] : NULL, e, &pattern, &why);
	if (why)
		return why;
	/* the root map, kept under ROOT for the line of what it lacks */
	if (!*depth)
		path = xstrdup(ROOT);
	if (path && unsorted_string_list_has_string(kv, path))
		return xstrfmt("duplicate key '%s'", path);
	if (path) {
		v = yaml_value(text, e, pattern, *depth ? stack[*depth - 1].node : NULL);
		string_list_append(kv, path)->util = v;
	}
	if (e->type == YAML_SCALAR_EVENT)
		return NULL;
	if (*depth == YAML_DEPTH)
		return "nested too deeply";
	stack[*depth].path = strcmp(path, ROOT) ? path : NULL;
	stack[*depth].pattern = pattern;
	stack[*depth].node = v;
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

static void yaml(const char *text, struct string_list *kv, const char *what)
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
 * In TEXT, the value V of a scalar becomes WITH: in place where the text
 * spells the value as it is, else the scalar is written anew as "..." (its
 * line breaks after it kept)
 */
static void yreplace(struct strbuf *text, struct yval *v, const char *with)
{
	struct strbuf sb = STRBUF_INIT;
	size_t end = v->end;

	if (v->literal) {
		strbuf_splice(text, v->at, strlen(v->s), with, strlen(with));
		return;
	}
	while (end > v->start && text->buf[end - 1] == '\n')
		end--;
	yaml_quoted(&sb, with);
	strbuf_splice(text, v->start, end - v->start, sb.buf, sb.len);
}

/* where the last '.' of the path P stands, 0 when it has none; keys escape theirs as "\\." */
static size_t last_dot(const char *p)
{
	size_t i, dot = 0;

	for (i = 0; p[i]; i++)
		if (p[i] == '\\' && p[i + 1])
			i++;
		else if (p[i] == '.')
			dot = i;
	return dot;
}

/*
 * " in line N" of the value of KEY, for E_FORMAT (5.8: the file and the
 * line); for a KEY that is not there, of the nearest node on its path that
 * is, the root at last, or line 1 of a file with none
 */
char *yline(struct string_list *kv, const char *key)
{
	char *up = xstrdup(key);
	struct string_list_item *item;

	/* the path without its last key, until a node is there or nothing is left */
	while (!(item = unsorted_string_list_lookup(kv, up)) && *up)
		up[last_dot(up)] = '\0';
	if (!item)
		item = unsorted_string_list_lookup(kv, ROOT);
	return xstrfmt(" in line %d", item ? ((struct yval *)item->util)->line : 1);
}

/*
 * spec 8.4: the "|" block V of TEXT, the value of KEY, without its trailing
 * blank lines: whole lines of spaces at its end as libyaml places it, dropped
 * only where the value reads the same after ("|+" keeps them as its value)
 */
static void trim_block(struct strbuf *text, const char *key, struct yval *v)
{
	struct string_list after = STRING_LIST_INIT_DUP;
	struct strbuf trimmed = STRBUF_INIT;
	size_t first = v->start + strcspn(text->buf + v->start, "\n") + 1, end = v->end, p, start;
	char *err;

	/* the end of the block's last line; the next line keeps its indentation */
	while (end > first && text->buf[end - 1] != '\n')
		end--;
	/* back one line of spaces at a time, while the value reads the same without it */
	for (p = end; p > first; p = start) {
		start = p - 1;
		while (start > first && text->buf[start - 1] != '\n')
			start--;
		if (text->buf[start + strspn(text->buf + start, " ")] != '\n')
			break;
		strbuf_reset(&trimmed);
		strbuf_add(&trimmed, text->buf, text->len);
		strbuf_splice(&trimmed, start, p - start, "", 0);
		string_list_clear(&after, 0);
		if (yaml_parse(trimmed.buf, &after, "", &err) || !yget(&after, key) || strcmp(yget(&after, key), v->s))
			break;
		strbuf_splice(text, start, p - start, "", 0);
	}
}

/*
 * spec 8.4: the result file TEXT, parsed into KV, as the message carries it:
 * each value RENAMED names given its new text, each "|" block without its
 * trailing blank lines; from the last value back, so the places of the others hold
 */
void yaml_finish(struct strbuf *text, struct string_list *kv, struct string_list *renamed)
{
	size_t i;

	for (i = kv->nr; i-- > 0;) {
		struct string_list_item *to = unsorted_string_list_lookup(renamed, kv->items[i].string);
		struct yval *v = kv->items[i].util;

		if (to)
			yreplace(text, v, to->util);
		else if (v->block)
			trim_block(text, kv->items[i].string, v);
	}
}

/* 2.5.1: a decimal integer, as YAML 1.2 reads one: a plain scalar of digits, no leading zero */
static int decimal(struct yval *v)
{
	return v->plain && *v->s && !v->s[strspn(v->s, "0123456789")] && (*v->s != '0' || !v->s[1]);
}

/* V is of KIND: a key with nothing under it is an empty list, map or scalar; "" is a scalar only */
static int fits(struct yval *v, char kind)
{
	if (kind == 'l' || kind == 'm')
		return v->kind == kind || v->kind == '?';
	if (v->kind && v->kind != '?')
		return 0;
	return kind != 'i' || decimal(v);
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
		struct yval *v = item->util, *up = v->parent;
		char kind;

		/* the root map is no key */
		if (!v->pattern)
			continue;
		if (!(kind = schema_kind(schema, v->pattern))) {
			/*
			 * a key the schema lacks, in a map the document and the schema both
			 * have as one (or the root), is unknown; under anything else it is
			 * its parent's kind that is wrong, and the parent's check says so
			 */
			if (up->kind == 'm' && (!up->pattern || schema_kind(schema, up->pattern) == 'm'))
				reject(128, hint, "bad format: '%s': unknown key '%s' in line %d", what, item->string, v->line);
			continue;
		}
		if (kinds && !fits(v, kind))
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
	const char *oid = blob_at(t, path), *engine, *body, *missing = NULL;
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
	body = yget(kv, "body");
	if (!body || !*body)
		missing = "body";
	else if (*engine && !yget(kv, "command.0"))
		missing = "command.0";
	if (missing)
		reject(128, NULL, "bad format: '%s': needs 'body', and 'command' with 'engine: exec'%s", path,
		       yline(kv, missing));
	/* an empty success would let no exit code pass; left out, it is [0] (user decision) */
	if (yget(kv, "success") && !yget(kv, "success.0"))
		reject(128, NULL, "bad format: '%s': bad value of 'success'%s", path, yline(kv, "success"));
	string_list_insert(&parsed, oid)->util = kv;
	return kv;
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

/* spec 5.4: the PINS of a check line, the hash of R's provider and file lines; each text is hashed once */
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
 * spec 1.5a: current D or O R is unverified at T: its check line confirms its
 * pins and its code changed since (3.11). A check line that does not is an
 * accident (2.3.3), and spec files off their pins break the bundle (2.4).
 */
int unverified(struct record *r, struct tree *t)
{
	return is_current(r) && confirmed(r) && code_changed(r, t);
}

/*
 * spec 2.5.2: the inputs the definitions DEFS declare at T, each a PATH of 3.1,
 * into OUT as "input -> the definition declaring it", once per declaration
 */
void def_inputs(struct tree *t, struct string_list *defs, struct string_list *out)
{
	struct string_list_item *def, *in;

	for_each_string_list_item(def, defs) {
		struct string_list *kv;

		/* a definition T lacks declares nothing there: who needs it reports it missing (2.4, 7.5.2) */
		if (!blob_at(t, def->string))
			continue;
		kv = def_get(t, def->string);
		for_each_string_list_item(in, kv) {
			const char *p = yget(kv, in->string);

			if (!starts_with(in->string, "inputs."))
				continue;
			if (!path_ok(p))
				reject(128, NULL, "bad format: '%s': bad input '%s'%s", def->string, p, yline(kv, in->string));
			/* an input that is a definition of these is that definition */
			if (!unsorted_string_list_has_string(defs, p))
				string_list_append(out, p)->util = def->string;
		}
	}
}

/* the file line "PATH MODE OID WHAT" of PATH at T into OUT; -1 when T has no such file */
static int file_line(struct string_list *out, struct tree *t, const char *path, const char *what)
{
	struct entry *e = tree_get(t, path);

	if (!blob_at(t, path))
		return -1;
	string_list_insert(out, xstrfmt("%s %s %s %s", path, e->mode, e->oid, what));
	return 0;
}

/*
 * spec 2.4, 6.2: R's file lines again at T: its definitions and what they
 * declare, pinned at T; -1 when T lacks one. A path in CONFLICTS is unmerged
 * (7.12.6) and keeps R's line; while a definition is unmerged what it
 * declares cannot be read, so R's other lines stay too, pinned again.
 */
int expand_files_except(struct record *r, struct tree *t, struct string_list *conflicts)
{
	struct string_list defs = STRING_LIST_INIT_DUP, out = STRING_LIST_INIT_DUP, declared = STRING_LIST_INIT_DUP;
	struct string_list inputs = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	int held = 0;

	for_each_string_list_item(item, &r->f[FILES]) {
		const char *path = scope_path(item->string);

		if (conflicts && string_list_has_string(conflicts, path)) {
			string_list_insert(&out, item->string);
			held |= is_def(item->string);
		} else if (is_def(item->string)) {
			string_list_insert(&defs, path);
		}
	}
	for_each_string_list_item(item, &defs)
		if (file_line(&out, t, item->string, "def"))
			return -1;
	def_inputs(t, &defs, &declared);
	for_each_string_list_item(item, &declared)
		string_list_insert(&inputs, item->string);
	if (held)
		for_each_string_list_item(item, &r->f[FILES])
			if (!is_def(item->string))
				string_list_insert(&inputs, scope_path(item->string));
	for_each_string_list_item(item, &inputs)
		if (!(conflicts && string_list_has_string(conflicts, item->string)) && file_line(&out, t, item->string, "input"))
			return -1;
	r->f[FILES] = out;
	return 0;
}

int expand_files(struct record *r, struct tree *t)
{
	return expand_files_except(r, t, NULL);
}

static int files_match_except(struct record *r, struct tree *t, struct string_list *conflicts)
{
	struct record *x = record_copy(r);

	return !expand_files_except(x, t, conflicts) && !strcmp(record_text(x, 1u << FILES), record_text(r, 1u << FILES));
}

int files_match(struct record *r, struct tree *t)
{
	return files_match_except(r, t, NULL);
}

/* spec 5.9 e: live R of S keeps its bundle at T, its spec files as it pins them (2.4); 0 when it does */
static int bundle_error(struct state *s, struct record *r, struct tree *t, struct string_list *conflicts, struct verdict *v)
{
	if (!is_live(r) || files_match_except(r, t, conflicts))
		return 0;
	fail_with(v, 1, "incomplete", xstrfmt("make the spec files of '%s' match it, or reopen it with them: 'ait %s -m <why>'",
					      r->id, reopen_clauses(s, r, NULL)),
		  xstrfmt("spec files do not match the contract: '%s'", r->id));
	v->k = r;
	return 1;
}

int check_state_except(struct state *s, struct tree *t, struct verdict *v, struct string_list *conflicts)
{
	size_t i;

	if (check_records(s, v))
		return v->code;
	for (i = 0; i < s->nr; i++)
		if (bundle_error(s, s->r[i], t, conflicts, v))
			return 1;
	return 0;
}

int check_state(struct state *s, struct tree *t, struct verdict *v)
{
	return check_state_except(s, t, v, NULL);
}

/* 5.10, 7.1.8: every error of the state S at T, one per record, each message with its verdict */
void state_errors(struct state *s, struct tree *t, struct string_list *out)
{
	size_t i;

	for (i = 0; i < s->nr; i++) {
		struct verdict *v = xcalloc(1, sizeof(*v));

		if (record_error(s, s->r[i], v) || bundle_error(s, s->r[i], t, NULL, v))
			string_list_insert(out, v->msg)->util = v;
	}
}

/*
 * 5.9a transition hint 1, 7.1.3-7, 5.8: HEAD had the error V already, so the
 * state judged now did not make it: a record V points to is one that a
 * structure error of HEAD (5.9 a-d) points to, or, for a broken bundle, one
 * whose bundle HEAD breaks too
 */
int at_head(struct verdict *v)
{
	static struct string_list held = STRING_LIST_INIT_DUP, broken = STRING_LIST_INIT_DUP;
	static int loaded;
	struct string_list errors = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct state hs = { 0 };

	if (!loaded++) {
		state_load(&hs, head_tree());
		state_errors(&hs, head_tree(), &errors);
		for_each_string_list_item(item, &errors) {
			struct verdict *e = item->util;

			if (!strcmp(e->word, "incomplete"))
				string_list_insert(&broken, e->k->id);
			else
				string_list_insert(&held, e->k->id);
			if (e->o)
				string_list_insert(&held, e->o->id);
		}
	}
	if (!strcmp(v->word, "incomplete"))
		return v->k && string_list_has_string(&broken, v->k->id);
	return (v->k && string_list_has_string(&held, v->k->id)) || (v->o && string_list_has_string(&held, v->o->id));
}

/* R is a future or current demand or offer that has PATH as a spec file */
int declares(struct record *r, const char *path)
{
	struct string_list_item *item;

	if (!is_live(r))
		return 0;
	for_each_string_list_item(item, &r->f[FILES])
		if (!strcmp(scope_path(item->string), path))
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
	ssize_t n = strcmp(path, "-") ? strbuf_read_file(sb, path, 0) : strbuf_read(sb, 0, 0);

	if (n < 0)
		return -1;
	return strlen(sb->buf) != sb->len;
}
