/*
 * Requests (spec 1.7): the helpers (7.3), fulfill (7.7) and commit (7.7a).
 * Each one edits the loaded records in place and names the tree the request
 * is judged in; finish() then runs one pipeline: transitions (2.3), the check
 * set (1.5b), validate (5.9), the code to check, results (8) and the commit (5.5).
 */
#include "ait.h"
#include <yaml.h>
#include "strvec.h"
#include "quote.h"
#include <sys/stat.h>

/* a helper clause of 7.3.1: what it names and what it gives ("-" keeps, NULL not given) */
struct clause {
	const char *verb, *k[2], *a, *b, *f, *alias;
	int future;
};

struct request {
	struct state s;		/* every record keeps its HEAD version in ->old */
	struct tree *ht, *v;	/* HEAD's tree and the tree judged */
	struct tree *noted;	/* the tree of the index when the request began: what git commit would commit (5.5-1) */
	struct tree *index;	/* the entries of the index then, intent-to-add ones marked, as git read-tree compares them */
	int gate, fulfill, force, seq;
	const char *result, *file;	/* -r and -F */
	int has_msg;			/* -m or -F was given */
	struct strbuf msg;
	const char *pull_hint;
	const char **pull_tree;
	struct strbuf *pull_out;
	struct string_list files;	/* spec files given to the helpers */
	struct string_list declared;	/* an input -> a definition of this request that declares it */
	struct string_list results;	/* the result file taken, as yaml_parse() read it */
	struct string_list renamed;	/* "results.N.task" -> its value with +SEQ made the new ID (8.4) */
	struct string_list scoped;	/* IDs given with =SCOPES */
	struct string_list selected;	/* the promises fulfill was asked to keep */
	struct string_list check_set;	/* the check set, ID -> record */
	struct string_list missing;	/* the check set without code to check */
	struct string_list tasks;	/* "CONTRACT:DEF" -> record */
	struct string_list hints;
	struct string_list carried;	/* rule 5: D -> " via O..." of the target offers that undertake it */
	struct string_list mixed;	/* the 7.7.6 "mixed PATH" lines */
	struct verdict invalid;		/* the 7.7.6 "invalid" line: the state after the request */
	struct verdict first;		/* the first condition that blocks fulfill or commit (5.13) */
	struct strvec args;		/* the arguments that are not options */
	char *after_option;		/* args.v[i] came right after an option */
	struct clause *cl;		/* the helper's clauses, as written */
	size_t ncl, alloc_cl;
};

#define REQUEST_INIT { .msg = STRBUF_INIT, .files = STRING_LIST_INIT_DUP, .declared = STRING_LIST_INIT_DUP, \
	.results = STRING_LIST_INIT_DUP, .renamed = STRING_LIST_INIT_DUP, .scoped = STRING_LIST_INIT_DUP, \
	.selected = STRING_LIST_INIT_DUP, .check_set = STRING_LIST_INIT_DUP, .missing = STRING_LIST_INIT_DUP, \
	.tasks = STRING_LIST_INIT_DUP, .hints = STRING_LIST_INIT_DUP, .carried = STRING_LIST_INIT_DUP, \
	.mixed = STRING_LIST_INIT_DUP, .args = STRVEC_INIT }

/*
 * spec 5.2-2: an integration (7.1.3), or a rebase, am or sequencer that has
 * not stopped at a commit (git v2.53.0 wt-status.c:1791)
 */
static int git_op_in_progress(void)
{
	static const char *const files[] = { "rebase-merge", "rebase-apply", "sequencer/todo", NULL };
	int i;

	for (i = 0; files[i]; i++)
		if (!access(xstrfmt("%s/%s", gitdir, files[i]), F_OK))
			return 1;
	return integrating();
}

/* spec 5.2 */
void precondition(int allow_unborn)
{
	if (git_op_in_progress())
		reject(128, "finish or abort it as 'git status' says, then rerun this command",
		       "cannot run now: a git operation is in progress");
	if (!headref)
		reject(128, "run this command with HEAD on a branch", "cannot run now: HEAD is detached");
	if (!head && !allow_unborn)
		reject(128, "run 'ait init' first", "cannot run now: the branch has no commit");
	if (*git("status --porcelain --untracked-files=all -- .ait"))
		reject(1, "contracts change only through ait; restore '.ait/' to HEAD's version and remove the new files there",
		       "uncommitted changes in .ait/");
}

/* HEAD is still the branch R, and R is the commit C */
static int at(const char *c)
{
	char oid[48];
	int rc;

	return !strcmp(line1(git_run(NULL, NULL, &rc, "symbolic-ref -q HEAD")), headref) &&
	       object(headref, 0, oid, NULL) && !strcmp(oid, c);
}

/* "'A' 'B'": the paths named in a message or a hint's prose, as git names them */
static char *quoted_paths(struct string_list *paths)
{
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;

	for_each_string_list_item(item, paths)
		strbuf_addf(&sb, "%s'%s'", sb.len ? " " : "", item->string);
	return strbuf_detach(&sb, NULL);
}

/* two versions of a path are the same: both absent, or the same mode and blob */
static int same_entry(struct entry *a, struct entry *b)
{
	if (!a || !b)
		return a == b;
	return !strcmp(a->mode, b->mode) && !strcmp(a->oid, b->oid);
}

/* this worktree is sparse as git takes it: core.sparseCheckout with its own patterns file (git-sparse-checkout(1)) */
static int sparse(void)
{
	int gone;
	char *patterns = line1(git_run(NULL, NULL, &gone, "rev-parse --path-format=absolute --git-path info/sparse-checkout"));

	return config_true("core.sparseCheckout") && !gone && !access(patterns, F_OK);
}

/*
 * spec 5.5-4, made before the commit is published so that nothing after it
 * can fail but git's own steps: the paths H→T changes, and O, which is T
 * with, on those paths, the entries the index has now (5.5-1). An
 * intent-to-add entry is git's "not staged": on a path T leaves out, O
 * leaves it out too, and git read-tree keeps it as git commit does.
 */
static char *index_before(struct tree *h, struct tree *t, struct tree *index, struct string_list *paths)
{
	struct string_list_item *item;
	struct strbuf info = STRBUF_INIT;
	char *o;

	tree_diff(h, t, paths);
	for_each_string_list_item(item, paths) {
		struct entry *was = tree_get(index, item->string);

		if (was && was->ita && !tree_get(t, item->string))
			was = NULL;
		index_line(&info, was ? was->mode : NULL, was ? was->oid : NULL, item->string);
	}
	if (paths->nr && !(o = tree_with(t->oid, info.buf, NULL)))
		reject(128, NULL, "%s", git_failed());
	return paths->nr ? o : NULL;
}

/*
 * spec 5.5-4/5 by git's own commands, after C is published; each step only
 * reports its failure. Pull uses git read-tree -m -u H C, retaining Git's
 * index and work tree protection; other requests update only .ait/ in the
 * work tree. git read-tree -i -m O C moves the index from O to C
 * under git's index lock in one step, taking C's entry only where the index
 * still has O's and refusing everything when one was staged otherwise (git
 * v2.53.0 unpack-trees.c twoway_merge). Then .ait/ follows in the work
 * tree, apart from skip-worktree entries, git refreshes what it wrote, and a
 * sparse worktree gets its patterns back. What is not done is warned about.
 */
static int follow(struct string_list *paths, const char *o, struct tree *ct, const char *c, int code, int held)
{
	struct string_list ait = STRING_LIST_INIT_DUP, rest = STRING_LIST_INIT_DUP;
	struct string_list skipped = STRING_LIST_INIT_DUP, left = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	int fd = held >= 0 ? held : lock_worktree(top), rc = fd < 0 || !at(c), failed;

	for_each_string_list_item(item, paths)
		string_list_append(starts_with(item->string, ".ait/") ? &ait : &rest, item->string);
	if (!rc && o)
		git_run(NULL, NULL, &rc, code ? "read-tree -m -u %s %s" : "read-tree -i -m %s %s", o, ct->oid);
	if (!rc && !code && ait.nr)
		string_list_split(&skipped, git_run(NULL, NULL, &failed, "ls-files -t -- .ait"), "\n", -1);
	for_each_string_list_item(item, &ait) {
		if (code)
			break;
		if (rc)
			break;
		if (unsorted_string_list_has_string(&skipped, xstrfmt("S %s", item->string)))
			continue;
		if (tree_get(ct, item->string))
			git_run(NULL, NULL, &failed, "checkout-index -f -q -- %s", item->string);
		else
			failed = unlink(xstrfmt("%s/%s", top, item->string)) && errno != ENOENT;
		if (failed)
			string_list_append(&left, item->string);
	}
	/* read-tree and checkout-index leave the entries without the files' stat data */
	if (!rc && !code && o) {
		git_run(NULL, NULL, &rc, "update-index --refresh");
		rc = rc && rc != 1;
	}
	/* the sparse patterns back over .ait/; when that fails, those files are left as they are */
	if (!rc && !code && ait.nr && sparse()) {
		git_run(NULL, NULL, &failed, "sparse-checkout reapply");
		for_each_string_list_item(item, &ait)
			if (failed && !unsorted_string_list_has_string(&left, item->string))
				string_list_append(&left, item->string);
	}
	if (!at(c))
		rc = 1;
	if (fd >= 0 && held < 0)
		close(fd);
	if (!rc && !left.nr)
		return 0;
	if (rc)
		warning("committed '%s' to '%s', but the index and work tree were not updated", c, headref);
	else
		warning("committed '%s' to '%s', but %s were not updated", c, headref, quoted_paths(&left));
	if (!at(c))
		return 1;
	if (rc && ait.nr)
		suggest("bring %s to '%s' in the index and work tree", quoted_paths(&ait), c);
	else if (left.nr)
		suggest("bring %s to '%s' in the work tree", quoted_paths(&left), c);
	if (rc && rest.nr)
		suggest("bring %s to '%s' in the %s", quoted_paths(&rest), c, code ? "index and work tree" : "index");
	return 1;
}

/* the commit the last publish() put on the branch (init prints it) */
const char *published;

/* the branch R is no longer H: another commit is on it, or it is gone where H was a commit */
static int moved(void)
{
	char oid[48];

	return object(headref, 0, oid, NULL) ? strcmp(oid, head ? head : "") != 0 : head != NULL;
}

/*
 * spec 5.5: C is TREE on HEAD with MSG, and git's update-ref moves the branch R
 * from HEAD to C or refuses (the CAS); then the index and work tree follow
 * from INDEX, the entries noted when the request began, and what fails there
 * is only warned about. 1 when R moved meanwhile; else C is PUBLISHED.
 */
int publish_pull(const char *tree, const char *msg, struct tree *index, struct string_list *ends, int ff)
{
	struct tree *t = tree_load(tree);
	struct string_list paths = STRING_LIST_INIT_DUP;
	char *o, *c;
	int rc;
	struct strvec args = STRVEC_INIT;
	struct string_list_item *item;

	if (ends) {
		tree_diff(head_tree(), t, &paths);
		o = head_tree()->oid;
	} else {
		o = index_before(head_tree(), t, index, &paths);
	}
	strvec_pushl(&args, "commit-tree", tree, NULL);
	if (head)
		strvec_pushl(&args, "-p", head, NULL);
	if (ends)
		for_each_string_list_item(item, ends)
			strvec_pushl(&args, "-p", item->string, NULL);
	c = ff ? ends->items[0].string : line1(git_args(&args, msg, NULL, NULL));
	git_run(NULL, NULL, &rc, "update-ref %s %s %s", headref, c, head ? head : "0000000000000000000000000000000000000000");
	if (rc) {
		char *why = git_failed();

		if (moved())
			return 1;
		reject(128, NULL, "%s", why);
	}
	published = c;
	follow(&paths, o, t, c, ends != NULL, -1);
	return 0;
}

int publish(const char *tree, const char *msg, struct tree *index)
{
	return publish_pull(tree, msg, index, NULL, 0);
}

/* 7.12.6: the ordinary Git merge files and index, without moving HEAD */
void pull_conflict(const char *tree, struct string_list *ends, const char *stages, const char *msg)
{
	struct string_list paths = STRING_LIST_INIT_DUP;
	struct tree *t = tree_load(tree);
	struct strbuf heads = STRBUF_INIT;
	struct string_list_item *item;
	char *before;
	int fd = lock_worktree(top);

	if (fd < 0)
		reject(128, NULL, "could not lock the working tree");
	if (moved())
		reject(1, "rerun this command", "HEAD moved while running; nothing changed");
	tree_diff(head_tree(), t, &paths);
	before = head_tree()->oid;
	if (follow(&paths, before, t, head, 1, fd))
		reject(1, "restore the index and working tree to HEAD, then rerun this command", "could not prepare the index and working tree for this pull");
	git_run(stages, NULL, NULL, "update-index --index-info");
	for_each_string_list_item(item, ends)
		strbuf_addf(&heads, "%s\n", item->string);
	write_state(xstrfmt("%s/MERGE_HEAD", gitdir), heads.buf);
	write_state(xstrfmt("%s/MERGE_MSG", gitdir), msg);
	write_state(xstrfmt("%s/MERGE_MODE", gitdir), "no-ff");
	git("update-ref ORIG_HEAD %s", head);
	close(fd);
}

/* a file given to ait is read as text (E_READ, or E_FORMAT when it holds a NUL) */
static void text_or_die(struct strbuf *sb, const char *path)
{
	int rc = read_text(sb, path);

	if (rc < 0)
		reject(128, NULL, "could not read '%s'", path);
	if (rc > 0)
		reject(128, NULL, "bad format: '%s': not a text file%s", path, nul_line(sb->buf));
}

static void load(struct request *q)
{
	precondition(0);
	q->ht = head_tree();
	/* spec 5.5-1: the index as the request found it */
	q->noted = tree_load(line1(git("write-tree")));
	q->index = index_load(q->noted);
	state_load(&q->s, q->ht);
}

/*
 * spec 5.3 and 7.3.1: -m, -F, -r and --force may stand anywhere; the other
 * arguments go to q->args in order. Usage comes first (5.13-1).
 */
static void parse_options(struct request *q, int argc, const char **argv)
{
	/*
	 * a positional argument never starts with '-' but for amend's lone "-"
	 * (7.3.1), so every other "-..." is one of these or a usage error;
	 * only -m may be repeated (5.3)
	 */
	static const struct option { const char *name, *as; int value; } options[] = {
		{ "-m", "-m", 1 }, { "-F", "-F", 1 }, { "-r", "-r", 1 }, { "--result", "-r", 1 }, { "--force", "--force", 0 },
	};
	struct string_list seen = STRING_LIST_INIT_DUP;
	const char *file = NULL;
	int i, option = 0;

	q->after_option = xcalloc(argc + 1, 1);
	for (i = 0; i < argc; i++) {
		const char *a = argv[i];
		const struct option *o = NULL;
		size_t k;

		if (*a != '-' || !strcmp(a, "-")) {
			q->after_option[q->args.nr] = option;
			strvec_push(&q->args, a);
			option = 0;
			continue;
		}
		for (k = 0; k < ARRAY_SIZE(options); k++)
			if (!strcmp(a, options[k].name))
				o = &options[k];
		if (!o)
			reject(129, NULL, "unknown option '%s'", a);
		if (o->value && i + 1 >= argc)
			reject(129, NULL, "option '%s' requires a value", a);
		if (strcmp(o->as, "-m") && string_list_has_string(&seen, o->as))
			reject(129, NULL, "option '%s' conflicts with another option", a);
		string_list_insert(&seen, o->as);
		option = 1;
		if (!strcmp(o->as, "--force"))
			q->force = 1;
		else if (!strcmp(o->as, "-r"))
			q->result = argv[++i];
		else if (!strcmp(o->as, "-F"))
			file = argv[++i];
		else
			strbuf_addf(&q->msg, "%s%s", q->msg.len ? "\n\n" : "", argv[++i]);
	}
	if (file && string_list_has_string(&seen, "-m"))
		reject(129, NULL, "options '-m' and '-F' cannot be used together");
	if (q->force && q->result)
		reject(129, NULL, "options '-r' and '--force' cannot be used together");
	q->file = file;
	q->has_msg = file || string_list_has_string(&seen, "-m");
}

/* 5.13-1: the files given as arguments are read once the grammar and the shapes of paths hold */
static void read_files(struct request *q)
{
	if (!q->has_msg)
		reject(129, NULL, "a message is required: give -m <msg> or -F <file>");
	if (q->file)
		text_or_die(&q->msg, q->file);
	if (q->result && access(q->result, R_OK))
		reject(128, "rerun without -r to get the check-result path", "could not read '%s'", q->result);
}

/* spec 5.3: git's scissors line would cut the trailers off the message */
static void hides(const char *text)
{
	if (strstr(text, "# ------------------------ >8 ------------------------"))
		reject(129, "remove that line from the message", "the message hides the trailers: "
		       "'# ------------------------ >8 ------------------------'");
}

/* spec 5.3 */
static void message(struct request *q, const char *prefix)
{
	struct strbuf sb = STRBUF_INIT;

	strbuf_stripspace(&q->msg, NULL);
	if (!q->msg.len)
		reject(129, NULL, "a message is required: give -m <msg> or -F <file>");
	hides(q->msg.buf);
	if (prefix && !starts_with(q->msg.buf, prefix))
		strbuf_addstr(&sb, prefix);
	strbuf_addstr(&sb, q->msg.buf);
	q->msg = sb;
}

char *pull_options(int argc, const char **argv, struct strvec *args, int *force, struct strbuf *msg)
{
	struct request q = REQUEST_INIT;

	parse_options(&q, argc, argv);
	if (q.result)
		reject(129, NULL, "unknown option '-r'");
	if (q.has_msg) {
		read_files(&q);
		message(&q, "[CONTRACT] ");
	}
	*args = q.args;
	*force = q.force;
	*msg = q.msg;
	return q.after_option;
}

/* the next argument of a clause; options stand only between clauses (7.3.1) */
static const char *arg(struct request *q, int *i)
{
	const char *a = (size_t)*i < q->args.nr ? q->args.v[*i] : NULL;

	if (!a || q->after_option[*i])
		reject(129, NULL, "missing argument after '%s'", q->args.v[*i - 1]);
	return q->args.v[(*i)++];
}

/* the request's own name, "contract" or "ct", which may come before its clauses (7.3.1) */
static int contract_word(const char *a)
{
	return !strcmp(a, "contract") || !strcmp(a, "ct");
}

/* the words of the helper grammar (7.3.1) and the reserved aliases (1.9) */
static int is_word(const char *a)
{
	static const char *const words[] = { "demand", "offer", "future", "amend", "undertake", "release", "as",
					     "drop", "cancel", "contract", "ct", "fulfill", NULL };

	return one_of(a, words);
}

static struct record *resolve(struct request *q, const char *k)
{
	size_t i;

	for (i = 0; i < q->s.nr; i++)
		if (q->s.r[i]->alias && !strcmp(q->s.r[i]->alias, k))
			return q->s.r[i];
	if (!state_get(&q->s, k) || !strcmp(k, "init"))
		reject(128, islower(*k) ? "aliases are local to one command; use the ID printed on its 'new' line" : NULL,
		       "no such contract: '%s'", k);
	return readable(state_get(&q->s, k));
}

/* spec 3.2a: a scope list "A,B", each pinned at T, its shape checked before (shapes()) */
static void scope_list(struct string_list *out, const char *list, struct tree *t, struct tree *index, int allow_new,
		       int allow_abs)
{
	struct string_list parts = STRING_LIST_INIT_DUP;
	struct string_list_item *item;

	string_list_split(&parts, list, ",", -1);
	for_each_string_list_item(item, &parts) {
		char *path = scope_path(item->string);

		/* 7.3.3: a case twin in the index refuses it too, unless the index spells it so */
		if (!tree_get(index, path) && case_twin(index, path))
			reject(128, xstrfmt("give it as git has it: '%s'", case_twin(index, path)), "bad scope: '%s'", item->string);
		string_list_append(out, pin(item->string, t, allow_new, allow_abs));
	}
}

/* V is the lone "-" that keeps what a clause's contract has (7.3.1) */
static int keeps(const char *v)
{
	return v && !strcmp(v, "-");
}

/* a helper's scope argument, pinned at HEAD (spec 3.3); "-" keeps the old one */
static void scopes(struct request *q, struct record *r, enum key k, const char *list)
{
	struct string_list vals = STRING_LIST_INIT_DUP;
	int future = in_state(r, "future");

	if (!keeps(list)) {
		scope_list(&vals, list, q->ht, q->index, future || k == CONSUMER, future && k == PROVIDER);
		r->f[k] = vals;
	}
}

/* the definitions given to a helper; expand_files() pins them and what they declare */
static void defs(struct request *q, struct record *r, const char *list)
{
	struct string_list parts = STRING_LIST_INIT_DUP;
	struct string_list_item *item;

	if (keeps(list))
		return;
	string_list_split(&parts, list, ",", -1);
	string_list_clear(&r->f[FILES], 0);
	for_each_string_list_item(item, &parts) {
		struct string_list_item *file = unsorted_string_list_lookup(&q->files, item->string);

		(file ? file : string_list_append(&q->files, item->string))->util = (void *)"def";
		string_list_append(&r->f[FILES], xstrfmt("%s - - def", item->string));
	}
}

static void new_contract(struct request *q, struct clause *c)
{
	char id[16];
	struct record *r;

	do
		newid(id, toupper(*c->verb));
	while (state_get(&q->s, id));
	r = state_add(&q->s, id);
	r->seq = ++q->seq;
	r->clause = c - q->cl + 1;
	r->alias = c->alias;
	put(r, ROLE, c->verb);
	put(r, STATE, c->future ? "future" : "current");
	if (c->a)
		scopes(q, r, CONSUMER, c->a);
	scopes(q, r, PROVIDER, c->b);
	defs(q, r, c->f);
	put(r, CREATOR, xstrfmt("%s %s", agent, sid));
	put(r, PREV, "-");
}

/* a clause of the request is named ALIAS */
static int alias_used(struct request *q, const char *alias)
{
	size_t i;

	for (i = 0; i < q->ncl; i++)
		if (q->cl[i].alias && !strcmp(q->cl[i].alias, alias))
			return 1;
	return 0;
}

/* the shapes of a clause's arguments (3.1-3.2), before any tree is read (5.13-1) */
static void shapes(const char *list, int defs, int keep)
{
	struct string_list parts = STRING_LIST_INIT_DUP, seen = STRING_LIST_INIT_DUP;
	struct string_list_item *item;

	/* "-" keeps a value in a clause; as the scopes of ID=SCOPES it is no PATH (3.1) */
	if (!list || (keep && keeps(list)))
		return;
	string_list_split(&parts, list, ",", -1);
	for_each_string_list_item(item, &parts) {
		if (defs && !path_ok(item->string))
			reject(128, NULL, "bad scope: '%s'", item->string);
		if (defs)
			continue;
		scope_shape(item->string);
		/* 3.2a: a scope once in a list; the spec files may repeat */
		if (unsorted_string_list_has_string(&seen, item->string))
			reject(128, NULL, "bad scope: '%s' is given twice", item->string);
		string_list_append(&seen, item->string);
	}
}

/*
 * spec 7.3.1: ["contract"|"ct"] clause+, from the arguments parse_options()
 * left; options stand between clauses, never among a clause's arguments
 * (after_option). Read before the repository is (5.13-1).
 */
static void parse_clauses(struct request *q)
{
	const char **v = q->args.v;
	int n = q->args.nr, i = n && contract_word(v[0]);

	if (i == n)
		reject(129, NULL, "a request needs a clause");
	while (i < n) {
		struct clause *c;
		int more;

		ALLOC_GROW(q->cl, q->ncl + 1, q->alloc_cl);
		c = memset(&q->cl[q->ncl++], 0, sizeof(*c));
		c->future = !strcmp(v[i], "future");
		if (c->future && i + 1 == n)
			reject(129, NULL, "missing argument after 'future'");
		c->verb = v[i + c->future];
		i += c->future + 1;
		if (c->future && !strcmp(c->verb, "drop"))
			unsupported("future drop");
		if (!strcmp(c->verb, "demand") || !strcmp(c->verb, "offer")) {
			c->a = *c->verb == 'd' ? arg(q, &i) : NULL;
			c->b = arg(q, &i);
			c->f = arg(q, &i);
			if (i < n && !strcmp(v[i], "as")) {
				const char *alias;

				i++;
				alias = arg(q, &i);
				if (!islower(*alias) || strlen(alias) > 32 || is_word(alias) ||
				    alias[strspn(alias, "abcdefghijklmnopqrstuvwxyz0123456789_")])
					reject(129, NULL, "bad alias '%s'", alias);
				if (alias_used(q, alias))
					reject(129, NULL, "alias '%s' is used twice in one request", alias);
				c->alias = alias;
			}
		} else if (!strcmp(c->verb, "amend")) {
			c->k[0] = arg(q, &i);
			/* a demand takes A B FILES, an offer B FILES; which one is checked once K is known */
			for (more = 0; i + more < n && !is_word(v[i + more]) && !q->after_option[i + more]; more++)
				;
			if (more == 3)
				c->a = arg(q, &i);
			if (more != 0 && more != 2 && more != 3)
				reject(129, NULL, "amend '%s' takes A B FILES (a demand) or B FILES (an offer)", c->k[0]);
			c->b = more ? arg(q, &i) : NULL;
			c->f = more ? arg(q, &i) : NULL;
		} else if (c->future && !strcmp(c->verb, "cancel")) {
			c->k[0] = arg(q, &i);
		} else if (!c->future && (!strcmp(c->verb, "undertake") || !strcmp(c->verb, "release"))) {
			c->k[0] = arg(q, &i);
			c->k[1] = arg(q, &i);
		} else {
			reject(129, NULL, "unknown clause '%s'", c->verb);
		}
		/* 7.3.1: a new contract has nothing to keep */
		if (strcmp(c->verb, "amend") && (keeps(c->a) || keeps(c->b) || keeps(c->f)))
			reject(129, NULL, "'-' keeps a value only in amend");
		shapes(c->a, 0, 1);
		shapes(c->b, 0, 1);
		shapes(c->f, 1, 1);
	}
}

static char *reopen_first(struct request *q, struct record *k);

/* spec 7.3.2: a clause applies to the records as the earlier clauses left them */
static void apply(struct request *q, struct clause *c)
{
	struct record *r, *o;
	char *id;

	if (!strcmp(c->verb, "demand") || !strcmp(c->verb, "offer")) {
		new_contract(q, c);
	} else if (!strcmp(c->verb, "amend")) {
		r = resolve(q, c->k[0]);
		if (*r->id == 'u' || !is_live(r))
			reject(1, NULL, "not allowed: '%s' is not a future or current contract", r->id);
		if (c->b && (*r->id == 'D') != (c->a != NULL))
			reject(129, NULL, "amend '%s' takes A B FILES (a demand) or B FILES (an offer)", c->k[0]);
		if (c->future)
			put(r, STATE, "future");
		if (c->future)
			string_list_clear(&r->f[CHECK], 0);
		/* 7.3.2: an amend that keeps everything checks K again */
		r->recheck = is_current(r) && (!c->b || ((!c->a || keeps(c->a)) && keeps(c->b) && keeps(c->f)));
		if (c->b) {
			scopes(q, r, CONSUMER, c->a ? c->a : "-");
			scopes(q, r, PROVIDER, c->b);
			defs(q, r, c->f);
		}
	} else if (!strcmp(c->verb, "cancel")) {
		/* 7.3.2: K is future; cancelled already is a noop */
		r = resolve(q, c->k[0]);
		if (!in_state(r, "future") && !in_state(r, "cancelled"))
			reject(1, was_current(r) ? reopen_first(q, r) : NULL, "not allowed: '%s' cannot go from %s to cancelled",
			       r->id, val(r, STATE));
		put(r, STATE, "cancelled");
	} else {
		r = resolve(q, c->k[0]);
		o = resolve(q, c->k[1]);
		if (*r->id != 'D' || *o->id != 'O')
			reject(128, NULL, "no such contract: '%s' must be a demand and '%s' an offer", r->id, o->id);
		id = xstrfmt("undertake/%s-%s", r->id, o->id);
		r = state_get(&q->s, id);
		if (!r && *c->verb == 'u') {
			r = state_add(&q->s, id);
			put(r, PREV, "-");
		}
		if (r)
			put(r, STATE, *c->verb == 'u' ? "undertaken" : "released");
	}
}

/* R does not change in this request: it means what HEAD has, or it is a new link released at once (7.3.5) */
static int unchanged(struct record *r)
{
	return r->old ? as_in_head(r) : *r->id == 'u' && in_state(r, "released");
}

/* rule 5 (1.6, 7.7.2): D is checked, and fulfilled if future, with a target offer that undertakes it */
static int carried(struct request *q, struct record *d)
{
	return string_list_has_string(&q->carried, d->id);
}

/* " via O..." for a demand fulfilled with the offers that undertake it (7.7.5) */
static const char *via(struct request *q, struct record *d)
{
	struct string_list_item *item = string_list_lookup(&q->carried, d->id);

	return item ? item->util : "";
}

static void add_hint(struct request *q, const char *h)
{
	if (!unsorted_string_list_has_string(&q->hints, h))
		string_list_append(&q->hints, h);
}

static char *hints(struct request *q)
{
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;

	for_each_string_list_item(item, &q->hints)
		strbuf_addf(&sb, "%s%s", sb.len ? "\n" : "", item->string);
	string_list_clear(&q->hints, 0);
	return strbuf_detach(&sb, NULL);
}

/* a word of a hint command, if there is one: quoted for the shell when it holds a character the shell reads (5.9a) */
static void word(struct strbuf *sb, const char *w)
{
	if (!w)
		return;
	strbuf_addch(sb, ' ');
	sq_quote_buf_pretty(sb, w);
}

/* the word W of a clause names R, by its ID or its alias */
static int is_named(const char *w, struct record *r)
{
	return w && (!strcmp(w, r->id) || (r->alias && !strcmp(w, r->alias)));
}

/* clause C names K: it made K, amends or cancels it, or, for a link, undertakes or releases its two ends */
static int names(struct request *q, struct clause *c, int i, struct record *k)
{
	if (*k->id == 'u')
		return (!strcmp(c->verb, "undertake") || !strcmp(c->verb, "release")) &&
		       is_named(c->k[0], link_end(&q->s, k, 0)) && is_named(c->k[1], link_end(&q->s, k, 1));
	return k->clause == i + 1 || ((!strcmp(c->verb, "amend") || !strcmp(c->verb, "cancel")) && is_named(c->k[0], k));
}

/* an alias the request does not use yet: BASE, or BASE and a number (5.9a) */
static const char *free_alias(struct request *q, const char *base)
{
	char *alias = xstrdup(base);
	int n = 0;

	while (alias_used(q, alias))
		alias = xstrfmt("%s%d", base, ++n);
	return alias;
}

/* the words of clause C as a helper takes them; MOVED, when given, is its provider argument as it stands */
static void clause_words(struct strbuf *sb, struct clause *c, const char *moved)
{
	strbuf_addf(sb, "%s %s", c->future ? " future" : "", c->verb);
	word(sb, c->k[0]);
	word(sb, c->k[1]);
	word(sb, c->a);
	if (moved)
		strbuf_addf(sb, " %s", moved);
	else
		word(sb, c->b);
	word(sb, c->f);
	if (c->alias)
		strbuf_addf(sb, " as %s", c->alias);
}

/*
 * 5.9a: the helper request again, with the clause of each contract in KS
 * changed: 'f' makes it a promise or reopens it ("future demand", "future
 * amend"), 'm' gives it KEPT,<new scope> as provider, 'u' puts ADD in place of
 * a link's clause; a contract no clause names gets one added, and so does the
 * clause ADD. Options give way to "-m <why>".
 */
static char *again(struct request *q, struct string_list *ks, char mode, const char *add)
{
	struct strbuf sb = STRBUF_INIT;
	struct string_list done = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	size_t i;

	strbuf_addstr(&sb, "ait");
	if (contract_word(q->args.v[0]))
		word(&sb, q->args.v[0]);
	for (i = 0; i < q->ncl; i++) {
		struct clause c = q->cl[i];
		struct record *k = NULL;

		for_each_string_list_item(item, ks)
			if (names(q, &q->cl[i], i, item->util))
				k = item->util;
		if (k)
			string_list_insert(&done, k->id);
		if (k && mode == 'f') {
			c.future = 1;
			c.verb = !strcmp(c.verb, "cancel") ? "amend" : c.verb;
		}
		if (k && mode == 'u') {
			strbuf_addf(&sb, " %s", add);
			continue;
		}
		if (k && mode == 'm' && !c.b) {
			c.a = *k->id == 'D' ? "-" : NULL;
			c.f = "-";
		}
		clause_words(&sb, &c, k && mode == 'm' ? moved_arg(k, q->ht) : NULL);
	}
	for_each_string_list_item(item, ks)
		if (!string_list_has_string(&done, item->string))
			strbuf_addf(&sb, " %s", mode == 'f' ? xstrfmt("future amend %s", item->string) :
				    amend_clause(item->util, moved_arg(item->util, q->ht), "-"));
	if (add && mode != 'u')
		strbuf_addf(&sb, " %s", add);
	strbuf_addstr(&sb, " -m <why>");
	return strbuf_detach(&sb, NULL);
}

/*
 * 5.9a: this fulfill or commit again, with each contract of IDS given SCOPES
 * ("ID=SCOPES"), or without its argument when SCOPES is NULL
 */
static char *rerun(struct request *q, struct string_list *ids, const char *scopes)
{
	struct strbuf sb = STRBUF_INIT;
	struct string_list given = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	size_t i;

	strbuf_addf(&sb, "ait %s", q->fulfill ? "fulfill" : "commit");
	for (i = 0; i < q->args.nr; i++) {
		char *id = xstrndup(q->args.v[i], strcspn(q->args.v[i], "="));

		if (!unsorted_string_list_has_string(ids, id))
			word(&sb, q->args.v[i]);
		else if (scopes)
			strbuf_addf(&sb, " %s=%s", id, scopes);
		string_list_insert(&given, id);
	}
	for_each_string_list_item(item, ids)
		if (scopes && !string_list_has_string(&given, item->string))
			strbuf_addf(&sb, " %s=%s", item->string, scopes);
	strbuf_addstr(&sb, " -m <why>");
	return strbuf_detach(&sb, NULL);
}

/* 5.9a transition hint 10: the request with the clause undertaking D by O turned into a future offer that does */
static char *undertake_hint(struct request *q, struct record *d, struct record *o)
{
	struct string_list set = STRING_LIST_INIT_DUP;
	const char *x = free_alias(q, "o");

	string_list_append(&set, xstrfmt("undertake/%s-%s", d->id, o->id))->util =
		state_get(&q->s, xstrfmt("undertake/%s-%s", d->id, o->id));
	return xstrfmt("undertake it with a future offer: '%s'",
		       again(q, &set, 'u', xstrfmt("future offer %s %s as %s undertake %s %s", scope_args(d, PROVIDER),
						   def_paths(d), x, d->alias ? d->alias : d->id, x)));
}

/* the hint of a verdict on the gate's candidate, as the same request with a clause changed (5.9a) */
static char *gate_hint(struct request *q, struct verdict *v)
{
	struct string_list set = STRING_LIST_INIT_DUP;

	if (!v->k)
		return v->hint;
	if (v->o && !v->o->old) {
		string_list_append(&set, v->o->id)->util = v->o;
		return xstrfmt("if it does not hold yet, make it a promise: '%s'", again(q, &set, 'f', NULL));
	}
	if (v->o && !was_current(v->k))
		return undertake_hint(q, v->k, v->o);
	reopen_set(&q->s, v->k, &set);
	if (v->o && !string_list_has_string(&set, v->o->id))
		string_list_append(&set, v->o->id)->util = v->o;
	if (v->o)
		return xstrfmt("'%s' is undertaken by current '%s'; reopen them in the same request: '%s'", v->k->id,
			       v->o->id, again(q, &set, 'f', NULL));
	return xstrfmt("make the spec files of '%s' match it, or reopen it with them: '%s'", v->k->id,
		       again(q, &set, 'f', NULL));
}

/*
 * 5.9a, code first for the gate: commit code that meets the condition with ait
 * commit, which would be E_MIXED while spec files are staged; so they go first
 */
static char *code_first(struct request *q, const char *code)
{
	struct string_list paths = STRING_LIST_INIT_DUP, staged = STRING_LIST_INIT_DUP;
	struct string_list_item *item;

	tree_diff(q->ht, q->noted, &paths);
	for_each_string_list_item(item, &paths)
		if (contract_path(&q->s, item->string))
			string_list_append(&staged, item->string);
	return xstrfmt("%scommit code that meets it first: stage %s and run 'ait commit -m <why>', then rerun this command",
		       staged.nr ? xstrfmt("unstage %s first; then ", quoted_paths(&staged)) : "", code);
}

/* "rerun this command", or the command without the ID=SCOPES of what SET reopens, which leaves the check set (5.9a) */
static char *then_rerun(struct request *q, struct string_list *set)
{
	struct string_list_item *item;

	for_each_string_list_item(item, set)
		if (unsorted_string_list_has_string(&q->scoped, item->string))
			return xstrfmt("rerun '%s'", rerun(q, set, NULL));
	return xstrdup("rerun this command");
}

/* the reopen hint of 5.9a: inside the helper's request, or run first before fulfill or commit again */
static char *reopen_first(struct request *q, struct record *k)
{
	struct string_list set = STRING_LIST_INIT_DUP;

	reopen_set(&q->s, k, &set);
	if (q->gate)
		return xstrfmt("to change '%s' without a check, reopen it: '%s'", k->id, again(q, &set, 'f', NULL));
	return xstrfmt("%s, then %s", reopen_hint(&q->s, k), then_rerun(q, &set));
}

/*
 * 5.9a, after the fix: a contract the helper registers as current can be made a
 * promise instead, and one current in HEAD can be reopened (except a demand
 * checked with the offer that undertakes it)
 */
static void next_hints(struct request *q, struct string_list *ids)
{
	struct string_list_item *item;

	for_each_string_list_item(item, ids) {
		struct record *r = item->util;

		if (q->gate && !r->old) {
			struct string_list set = STRING_LIST_INIT_DUP;
			size_t i;

			/* a demand this request has current offers undertake makes them promises too (5.9 d) */
			string_list_append(&set, r->id)->util = r;
			for (i = 0; i < q->s.nr; i++) {
				struct record *o = partner(&q->s, q->s.r[i], r);

				if (*r->id == 'D' && o && is_current(o))
					string_list_append(&set, o->id)->util = o;
			}
			add_hint(q, xstrfmt("if it does not hold yet, make it a promise: '%s'", again(q, &set, 'f', NULL)));
		} else if (was_current(r) && !carried(q, r)) {
			add_hint(q, reopen_first(q, r));
		}
	}
}

/* 5.9a, the fix for a failed check: the code, or the spec file that is the provider */
static void fix_hints(struct request *q, struct string_list *ids)
{
	struct string_list none = STRING_LIST_INIT_DUP;
	struct string_list_item *item, *sc;
	size_t k;

	for_each_string_list_item(item, ids) {
		for_each_string_list_item(sc, &((struct record *)item->util)->f[PROVIDER]) {
			char *path = scope_path(sc->string), *amend;
			struct record *owner = NULL;

			for (k = 0; k < q->s.nr && !owner; k++)
				if (q->s.r[k]->old && declares(q->s.r[k]->old, path))
					owner = q->s.r[k];
			amend = owner ? amend_clause(owner, "-", def_paths(owner->old)) : NULL;
			if (!owner)
				add_hint(q, q->gate ? code_first(q, "<your code>") : "make the code meet the failed task(s) and stage it, "
					 "then rerun without -r to get the new tasks");
			else if (q->gate && unsorted_string_list_has_string(&q->files, path))
				/* 7.3.3: a version staged apart from HEAD's would be partly staged once the file is fixed */
				add_hint(q, xstrfmt(same_entry(file_entry(q->noted, path), file_entry(q->ht, path)) ?
						    "fix '%s', then rerun this command" : "fix '%s' and stage it again, then rerun this command",
						    path));
			else if (q->gate)
				add_hint(q, xstrfmt("fix '%s' and carry it in this request: '%s'", path, again(q, &none, 0, amend)));
			else
				add_hint(q, xstrfmt("fix '%s' and commit it first: 'ait %s -m <why>', then rerun this command",
						    path, amend));
		}
	}
}

/*
 * 5.8 E_CHECK_FAILED: fulfill or commit may change the condition of K, current
 * in HEAD, by reopening it with new definitions, its offers of 5.9 d along
 */
static void condition_hint(struct request *q, struct record *k)
{
	struct string_list set = STRING_LIST_INIT_DUP;

	if (q->gate || !was_current(k))
		return;
	reopen_set(&q->s, k, &set);
	add_hint(q, xstrfmt("to change its condition, reopen it with the new one: 'ait %s -m <why>', then %s",
			    reopen_clauses(&q->s, k, "<new defs>"), then_rerun(q, &set)));
}

/* the first blocking condition wins (5.13); fulfill and commit keep computing their report */
static void block(struct request *q, struct verdict v)
{
	if (q->gate)
		reject(v.code, v.hint, "%s", v.msg);
	if (!q->first.code)
		q->first = v;
}

/*
 * the judged tree with the records this request changes, each with its prev
 * (2.2.4): the normalized candidate of the gate (6.2), or what fulfill and commit
 * write; CHANGED, when given, gets those records
 */
static char *judged_tree(struct request *q, struct string_list *changed)
{
	struct strbuf info = STRBUF_INIT;
	char *tree;
	size_t i;

	for (i = 0; i < q->s.nr; i++) {
		struct record *r = q->s.r[i];

		if (unchanged(r) && (!r->old || !strcmp(val(r, CHECK), val(r->old, CHECK))))
			continue;
		put(r, PREV, r->old ? blob_at(q->ht, xstrfmt(".ait/%s", r->id)) : "-");
		index_line(&info, "100644", line1(git_run(record_text(r, ALL_KEYS), NULL, NULL, "hash-object -w --stdin")),
			   xstrfmt(".ait/%s", r->id));
		if (changed)
			string_list_append(changed, r->id)->util = r;
	}
	if (!(tree = tree_with(q->v->oid, info.buf, NULL)))
		reject(128, NULL, "%s", git_failed());
	return tree;
}

/*
 * S as a YAML string: as it is when libyaml's reader reads it back, or,
 * holding other bytes (a path git keeps as bytes), quoted as git shows such
 * a path (core.quotePath). libyaml's emitter takes UTF-8 sequences its reader
 * refuses: surrogates (ED A0..BF) and code points past U+10FFFF (F4 90.., F5..)
 */
static const char *yaml_text(const char *s)
{
	struct strbuf sb = STRBUF_INIT;
	const unsigned char *p;
	yaml_event_t e;
	int reads = yaml_scalar_event_initialize(&e, NULL, NULL, (yaml_char_t *)s, strlen(s), 1, 1, YAML_ANY_SCALAR_STYLE);

	if (reads)
		yaml_event_delete(&e);
	for (p = (const unsigned char *)s; reads && *p; p++)
		if ((p[0] == 0xed && p[1] >= 0xa0) || (p[0] == 0xf4 && p[1] >= 0x90) || p[0] >= 0xf5)
			reads = 0;
	if (reads)
		return s;
	quote_c_style(s, &sb, NULL, 0);
	return strbuf_detach(&sb, NULL);
}

/* the task named T in a result file, its name written as the check files write it */
static struct string_list_item *task_of(struct request *q, const char *t)
{
	struct string_list_item *task;

	for_each_string_list_item(task, &q->tasks)
		if (!strcmp(yaml_text(task->string), t))
			return task;
	return NULL;
}

/*
 * spec 8.2: the check files are written by libyaml's emitter, so each value
 * reads back as it was: strings "...", a body as a "|" block where libyaml
 * can keep it so, escapes as YAML has them
 */
static void emit(yaml_emitter_t *y, yaml_event_t *e)
{
	if (!yaml_emitter_emit(y, e))
		BUG("libyaml cannot write the check files: %s", y->problem);
}

static void yscalar(yaml_emitter_t *y, const char *s, yaml_scalar_style_t style)
{
	yaml_event_t e;

	s = yaml_text(s);
	yaml_scalar_event_initialize(&e, NULL, NULL, (yaml_char_t *)s, strlen(s), 1, 1, style);
	emit(y, &e);
}

/* "KEY: VALUE", the value a "..." string */
static void ykey(yaml_emitter_t *y, const char *key, const char *value)
{
	yscalar(y, key, YAML_PLAIN_SCALAR_STYLE);
	yscalar(y, value, YAML_DOUBLE_QUOTED_SCALAR_STYLE);
}

/* a block map, or a list (block, or [FLOW]) */
static void yopen(yaml_emitter_t *y, int list, int flow)
{
	yaml_event_t e;

	if (list)
		yaml_sequence_start_event_initialize(&e, NULL, NULL, 1, flow ? YAML_FLOW_SEQUENCE_STYLE : YAML_BLOCK_SEQUENCE_STYLE);
	else
		yaml_mapping_start_event_initialize(&e, NULL, NULL, 1, YAML_BLOCK_MAPPING_STYLE);
	emit(y, &e);
}

static void yclose(yaml_emitter_t *y, int list)
{
	yaml_event_t e;

	if (list)
		yaml_sequence_end_event_initialize(&e);
	else
		yaml_mapping_end_event_initialize(&e);
	emit(y, &e);
}

/* a document of one map into SB; yend() closes it */
static void ybegin(yaml_emitter_t *y, struct strbuf *sb)
{
	yaml_event_t e;

	yaml_emitter_initialize(y);
	yaml_emitter_set_output(y, yaml_out, sb);
	yaml_emitter_set_unicode(y, 1);
	yaml_emitter_set_width(y, -1);
	yaml_stream_start_event_initialize(&e, YAML_UTF8_ENCODING);
	emit(y, &e);
	yaml_document_start_event_initialize(&e, NULL, NULL, NULL, 1);
	emit(y, &e);
	yopen(y, 0, 0);
}

static void yend(yaml_emitter_t *y)
{
	yaml_event_t e;

	yclose(y, 0);
	yaml_document_end_event_initialize(&e, 1);
	emit(y, &e);
	yaml_stream_end_event_initialize(&e);
	emit(y, &e);
	yaml_emitter_delete(y);
}

static char *task_def(struct string_list_item *task)
{
	return strchr(task->string, ':') + 1;
}

/* the style of a body in the check request: a "|" block, or "..." where libyaml's block keeps LS or PS raw (2.5.1) */
static yaml_scalar_style_t body_style(const char *body)
{
	return strstr(body, "\xe2\x80\xa8") || strstr(body, "\xe2\x80\xa9") ? YAML_DOUBLE_QUOTED_SCALAR_STYLE :
	       YAML_LITERAL_SCALAR_STYLE;
}

/* spec 8.2: the check request: what to check, in which tree, task by task */
static void write_request_file(struct request *q, const char *in, const char *req)
{
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *task;
	yaml_emitter_t y;

	/* the tree to check: T0 for fulfill and commit, the normalized candidate T for the gate */
	ybegin(&y, &sb);
	ykey(&y, "input", in);
	ykey(&y, "tree", q->gate ? judged_tree(q, NULL) : q->v->oid);
	yscalar(&y, "tasks", YAML_PLAIN_SCALAR_STYLE);
	yopen(&y, 1, 0);
	for_each_string_list_item(task, &q->tasks) {
		struct record *r = task->util;
		struct string_list *kv = def_get(q->v, task_def(task));
		const char *engine = engine_of(q->v, task_def(task)), *a;
		int k;

		yopen(&y, 0, 0);
		ykey(&y, "task", task->string);
		ykey(&y, "contract", xstrndup(task->string, strchr(task->string, ':') - task->string));
		ykey(&y, "role", val(r, ROLE));
		if (*r->id == 'D')
			ykey(&y, "consumer", shows(r, CONSUMER, q->v, 0));
		ykey(&y, "provider", shows(r, PROVIDER, q->v, was_current(r)));
		ykey(&y, "engine", engine);
		yscalar(&y, "body", YAML_PLAIN_SCALAR_STYLE);
		yscalar(&y, yget(kv, "body"), body_style(yget(kv, "body")));
		if (!strcmp(engine, "exec")) {
			yscalar(&y, "command", YAML_PLAIN_SCALAR_STYLE);
			yopen(&y, 1, 1);
			for (k = 0; (a = yget(kv, xstrfmt("command.%d", k))); k++)
				yscalar(&y, a, YAML_DOUBLE_QUOTED_SCALAR_STYLE);
			yclose(&y, 1);
			yscalar(&y, "success", YAML_PLAIN_SCALAR_STYLE);
			yopen(&y, 1, 1);
			for (k = 0; (a = yget(kv, xstrfmt("success.%d", k))) || !k; k++)
				yscalar(&y, a ? a : "0", YAML_PLAIN_SCALAR_STYLE);
			yclose(&y, 1);
		}
		yclose(&y, 0);
	}
	yclose(&y, 1);
	yend(&y);
	mkdir(xstrfmt("%s/ait", gitdir), 0777);
	write_state(req, sb.buf);
}

/* spec 4.7 and 8.4: the result template, kept when it already answers this input, so a filled one stays */
static void write_result_template(struct request *q, const char *in, const char *res)
{
	static const char *const fields[] = { "outcome", "basis", "reason", NULL };
	struct string_list kv = STRING_LIST_INIT_DUP;
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *task;
	yaml_emitter_t y;
	char *err;
	int i;

	if (!read_text(&sb, res) && !yaml_parse(sb.buf, &kv, res, &err) && yget(&kv, "input") &&
	    !strcmp(yget(&kv, "input"), in))
		return;
	strbuf_reset(&sb);
	strbuf_addstr(&sb, "# for each task: outcome is pass, fail or unknown; a pass needs a basis\n"
		      "# (what you ran or read, and what it showed); reason says why, when fail or unknown\n");
	ybegin(&y, &sb);
	ykey(&y, "input", in);
	yscalar(&y, "results", YAML_PLAIN_SCALAR_STYLE);
	yopen(&y, 1, 0);
	for_each_string_list_item(task, &q->tasks) {
		yopen(&y, 0, 0);
		ykey(&y, "task", task->string);
		for (i = 0; fields[i]; i++) {
			yscalar(&y, fields[i], YAML_PLAIN_SCALAR_STYLE);
			yscalar(&y, "", YAML_PLAIN_SCALAR_STYLE);
		}
		yclose(&y, 0);
	}
	yclose(&y, 1);
	yend(&y);
	write_state(res, sb.buf);
}

/*
 * spec 8.4: the results answer this state (0 when they do not), every task
 * once, each pass with a basis; FAILED and FAILING collect the tasks that did
 * not pass, FAILS those that failed. TEXT gets the file, for the message.
 */
static int read_results(struct request *q, const char *input, struct strbuf *text, struct strbuf *failed,
			struct string_list *failing, struct string_list *fails)
{
	static const char *const outcomes[] = { "pass", "fail", "unknown", NULL };
	static const char *const schema[] = { "input s", "results l", "results.N m", "results.N.task s",
					      "results.N.outcome s", "results.N.basis s", "results.N.reason s", NULL };
	struct string_list *kv = &q->results, seen = STRING_LIST_INIT_DUP;
	char *hint = xstrfmt("fix '%s': every task needs an outcome (pass, fail or unknown), and a pass needs a basis",
			     q->result);
	char *err;
	const char *t;
	size_t i;
	int n, rc;

	/* 5.8 E_FORMAT of a result file carries its hint on every path */
	if ((rc = read_text(text, q->result)) < 0)
		reject(128, "rerun without -r to get the check-result path", "could not read '%s'", q->result);
	if (rc)
		reject(128, hint, "bad format: '%s': not a text file%s", q->result, nul_line(text->buf));
	if (yaml_parse(text->buf, kv, q->result, &err))
		reject(128, hint, "%s", err);
	/* 8.4: the subset of 2.5.1, its unknown keys too, then the input, then the rest */
	yaml_check(kv, schema, q->result, hint, 0);
	if (!yget(kv, "input"))
		reject(128, hint, "bad format: '%s': no 'input'%s", q->result, yline(kv, "input"));
	if (strcmp(yget(kv, "input"), input))
		return 0;
	yaml_check(kv, schema, q->result, hint, 1);
	/* every item of results, each naming its task */
	for (n = 0; yget(kv, xstrfmt("results.%d", n)); n++) {
		const char *outcome = yget(kv, xstrfmt("results.%d.outcome", n));
		const char *basis = yget(kv, xstrfmt("results.%d.basis", n));
		struct string_list_item *task;

		if (!(t = yget(kv, xstrfmt("results.%d.task", n))))
			reject(128, hint, "bad format: '%s': a result without a task%s", q->result,
			       yline(kv, xstrfmt("results.%d", n)));
		task = task_of(q, t);
		if (!task || unsorted_string_list_has_string(&seen, task->string) || !outcome || !one_of(outcome, outcomes) ||
		    (!strcmp(outcome, "pass") && (!basis || !basis[strspn(basis, " \n")])))
			reject(128, hint, "bad format: '%s': task '%s'%s", q->result, t, yline(kv, xstrfmt("results.%d.task", n)));
		string_list_append(&seen, task->string);
		/* 8.4: a new contract's +SEQ becomes its ID in the message, the name written as the check files write it */
		for (i = 0; *task->string == '+' && i < q->s.nr; i++)
			if (!q->s.r[i]->old && q->s.r[i]->seq == atoi(task->string + 1))
				string_list_append(&q->renamed, xstrfmt("results.%d.task", n))->util =
					(void *)yaml_text(xstrfmt("%s%s", q->s.r[i]->id, strchr(task->string, ':')));
		if (!strcmp(outcome, "pass"))
			continue;
		if (!strcmp(outcome, "fail"))
			string_list_append(fails, task->string)->util = task->util;
		strbuf_addf(failed, "%s'%s'", failed->len ? ", " : "", t);
		string_list_append(failing, task->string)->util = task->util;
	}
	if (seen.nr != q->tasks.nr)
		reject(128, hint, "bad format: '%s': %d of %d tasks%s", q->result, (int)seen.nr, (int)q->tasks.nr,
		       yline(kv, "results"));
	return 1;
}

/* H4 (6.7a): my new link is not the only future offer on its demand */
static void also_undertaken(struct request *q, struct record *l)
{
	struct record *d, *o;
	size_t i;

	if (!undertaken(l) || (l->old && in_state(l->old, "undertaken")))
		return;
	d = link_end(&q->s, l, 0);
	o = link_end(&q->s, l, 1);
	for (i = 0; i < q->s.nr; i++) {
		struct record *o2 = q->s.r[i] == l ? NULL : partner(&q->s, q->s.r[i], d);

		if (!o2)
			continue;
		/* H4 is for my future offer, and the other one is someone else's (rule 7) */
		if (!in_state(o, "future") || strcmp(scope_path(val(o, CREATOR)), agent) || !in_state(o2, "future") ||
		    !strcmp(scope_path(val(o2, CREATOR)), agent))
			continue;
		warning("'%s' is also undertaken by '%s'", d->id, o2->id);
		suggest("if your work can go on without it, run 'ait future cancel %s -m <why>' (or 'ait release %s %s -m <why>' "
		       "to keep '%s' for its other conditions)", o->id, d->id, o->id, o->id);
	}
}

/* spec 1.2: a file that an exec command names but the contract does not pin */
static void unpinned(struct request *q, struct record *r)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &r->f[FILES]) {
		char *def = scope_path(item->string);
		const char *a;
		int k;

		if (!is_def(item->string))
			continue;
		for (k = 0; (a = yget(def_get(q->v, def), xstrfmt("command.%d", k))); k++) {
			while (starts_with(a, "./"))
				a += 2;
			if (path_ok(a) && (blob_at(q->v, a) || !access(xstrfmt("%s/%s", top, a), F_OK)) &&
			    !declares(r, a)) {
				warning("'%s' is not pinned", a);
				suggest("if it is test material, add it to 'inputs' in '%s'", def);
			}
		}
	}
}

/* spec 2.3: every changed record took an allowed step */
static void transitions(struct request *q)
{
	size_t i;
	int j;

	for (i = 0; i < q->s.nr; i++) {
		struct record *r = q->s.r[i];
		const char *from = r->old ? val(r->old, STATE) : "-", *to = val(r, STATE);

		if (unchanged(r))
			continue;
		for (j = 0; *r->id == 'u' && !strcmp(to, "undertaken") && j < 2; j++) {
			struct record *end = link_end(&q->s, r, j), *d = link_end(&q->s, r, 0);

			if (!is_live(end))
				reject(1, j ? undertake_hint(q, d, end) : NULL, "not allowed: '%s' is %s", end->id, val(end, STATE));
		}
		if (!record_step(r->id, from, to, q->gate))
			reject(1, was_current(r) && *r->id != 'u' ? reopen_first(q, r) : NULL,
			       "not allowed: '%s' cannot go from %s to %s", r->id, from, to);
	}
}

/*
 * spec 1.5b: current R is checked when it becomes current, is asked to be
 * checked again, has its code touched, is carried by a target offer, or has
 * pins this request changed without giving it scopes
 */
static int checked(struct request *q, struct record *r)
{
	if (!is_current(r))
		return 0;
	return !was_current(r) || r->recheck || touches(r->old, q->ht, q->v) || carried(q, r) ||
	       (!unsorted_string_list_has_string(&q->scoped, r->id) &&
		strcmp(record_text(r, PIN_KEYS), record_text(r->old, PIN_KEYS)));
}

/* spec 1.5b: the contracts this request checks, decided before anything is pinned again */
static void check_set(struct request *q)
{
	struct string_list_item *item;
	size_t i;

	for (i = 0; i < q->s.nr; i++) {
		struct record *r = q->s.r[i];

		if (r->bad)
			continue;
		if (is_current(r) && !was_current(r)) {
			repin(r, CONSUMER, q->v, 1);
			repin(r, PROVIDER, q->v, 1);
		}
		if (checked(q, r))
			string_list_append(&q->check_set, r->id)->util = r;
	}
	string_list_sort(&q->check_set);
	for_each_string_list_item(item, &q->scoped)
		if (!unsorted_string_list_has_string(&q->check_set, item->string))
			reject(128, NULL, "bad scope: '%s' is not checked by this command", item->string);
}

/* spec 7.7.3-4: the code to check is there; abstract contracts need a scope first */
static void code_present(struct request *q)
{
	struct string_list_item *item, *sc;

	for_each_string_list_item(item, &q->check_set) {
		struct string_list write = STRING_LIST_INIT_DUP, stage = STRING_LIST_INIT_DUP, missing = STRING_LIST_INIT_DUP;
		struct string_list one = STRING_LIST_INIT_DUP;
		struct record *k = item->util;
		int moved = 0;

		string_list_append(&one, k->id)->util = k;
		if (is_abstract(k))
			reject(128, xstrfmt("give it the scope you implemented: '%s'", rerun(q, &one, "<scope>")),
			       "bad scope: '%s' becomes current without a scope", k->id);
		missing_code(k, q->v, &missing);
		for_each_string_list_item(sc, &missing) {
			char *file = scope_path(sc->string);
			int written = !access(xstrfmt("%s/%s", top, file), F_OK), new = starts_with(sc->string, "new:");

			/* 7.3.3: the file came in another case; git add keeps that spelling */
			if (case_twin(q->v, file))
				reject(128, xstrfmt("give it as git has it: '%s'", case_twin(q->v, file)), "bad scope: '%s'", sc->string);
			/* An existing file with an unmappable range needs a scope, not staging. */
			if (blob_at(q->v, file)) {
				moved = 1;
				continue;
			}
			if (!written && new)
				string_list_append(&write, file);
			if (written || new)
				string_list_append(&stage, file);
			else
				moved = 1;
		}
		/* 5.8 E_CODE_REQUIRED, a hint per kind with all its paths: write, stage (the gate commits code first), or moved */
		if (write.nr)
			add_hint(q, xstrfmt("write %s", quoted_paths(&write)));
		if (stage.nr)
			add_hint(q, q->gate ? code_first(q, quoted_paths(&stage)) :
				 xstrfmt("stage %s, then rerun this command", quoted_paths(&stage)));
		if (moved && k->old)
			add_hint(q, q->gate ? xstrfmt("if it was moved, run '%s'", again(q, &one, 'm', NULL)) :
				 xstrfmt("if it was moved, rerun with '%s=%s'", k->id, moved_arg(k, q->v)));
		if (write.nr || stage.nr || moved)
			string_list_append(&q->missing, item->string)->util = item->util;
	}
	if (!q->missing.nr)
		return;
	next_hints(q, &q->missing);
	block(q, (struct verdict){ 1, "code-required", hints(q),
				   xstrfmt("no code under the contract's scope: '%s'", q->missing.items[0].string) });
}

/* spec 8.1 and 8.3: a task per definition of the check set, and the input that names this state */
static char *check_input(struct request *q)
{
	struct string_list paths = STRING_LIST_INIT_DUP;
	struct string_list_item *item, *f;
	struct strbuf in = STRBUF_INIT;

	for_each_string_list_item(item, &q->check_set) {
		struct record *r = item->util;

		for_each_string_list_item(f, &r->f[FILES])
			if (is_def(f->string))
				string_list_insert(&q->tasks, xstrfmt("%s:%s", r->old ? r->id : xstrfmt("+%d", r->seq),
								      scope_path(f->string)))->util = r;
	}
	strbuf_addf(&in, "head %s\n", head);
	tree_diff(q->ht, q->v, &paths);
	for_each_string_list_item(item, &paths) {
		struct entry *e = tree_get(q->v, item->string);

		if (!starts_with(item->string, ".ait/"))
			strbuf_addf(&in, "path %s %s %s\n", item->string, e ? e->mode : "-", e ? e->oid : "-");
	}
	for_each_string_list_item(item, &q->tasks)
		strbuf_addf(&in, "task %s %s %s\n", item->string, blob_at(q->v, task_def(item)),
			    shows(item->util, PROVIDER, q->v, was_current(item->util)));
	return line1(git_run(in.buf, NULL, NULL, "hash-object --stdin"));
}

/* spec 7.7.6: what is missing and what to check; fulfill and commit write the state lines too */
static void report(struct request *q, const char *req, const char *res, int with_tasks)
{
	struct string_list_item *item, *sc;

	for_each_string_list_item(item, &q->selected)
		printf("id %s\n", item->string);
	if (!q->gate) {
		for_each_string_list_item(item, &q->check_set)
			for_each_string_list_item(sc, &((struct record *)item->util)->f[PROVIDER])
				printf("provider %s %s\n", item->string, show(sc->string, q->v, was_current(item->util)));
		for_each_string_list_item(sc, &q->mixed)
			printf("mixed %s\n", sc->string);
		if (q->invalid.code)
			printf("invalid %s %s\n", q->invalid.word, q->invalid.msg);
		for_each_string_list_item(item, &q->missing)
			printf("code-required %s\n", item->string);
		for_each_string_list_item(item, &q->check_set)
			if (touches(((struct record *)item->util)->old, q->ht, q->v))
				printf("touch %s\n", item->string);
		putchar('\n');
	}
	if (with_tasks) {
		for_each_string_list_item(item, &q->tasks)
			printf("task %s %s\n", item->string, engine_of(q->v, task_def(item)));
		printf("check-request %s\ncheck-result %s\n", req, res);
	}
	if (!q->gate)
		puts(q->missing.nr || q->mixed.nr || q->invalid.code ? "ready no" : "ready yes");
}

/* the kind of output line a changed record of this request gets (6.7, 7.7.5); "" when none */
static const char *kind_of(struct request *q, struct record *r)
{
	if (*r->id == 'u')
		return val(r, STATE);
	if (!r->old)
		return "new";
	if (q->gate)
		return "changed";
	return is_current(r) && in_state(r->old, "future") ? "fulfilled" : "";
}

static char *change_line(struct request *q, struct record *r)
{
	if (*r->id == 'u')
		return xstrfmt("%s %.13s %s\n", val(r, STATE), r->id + 10, r->id + 24);
	else if (!r->old)
		return xstrfmt("new %s%s%s\n", r->id, r->alias ? " " : "", r->alias ? r->alias : "");
	else
		return xstrfmt("%s %s%s\n", kind_of(q, r), r->id, q->gate ? "" : via(q, r));
}

/* spec 7.7.4 and 6.5: pin the check set at the judged tree, add what changed to it and commit */
static void commit_request(struct request *q, struct strbuf *results)
{
	static const char *const kinds[] = { "new", "changed", "fulfilled", "undertaken", "released", NULL };
	struct string_list changed = STRING_LIST_INIT_DUP, sorted = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct strbuf trailers = STRBUF_INIT;
	char rid[16], *tree;
	size_t i;
	int kind, full = blob_at(q->ht, ".ait/init") && !strcmp(object(blob_at(q->ht, ".ait/init"), 1, NULL, NULL), "version 2\n");

	newid(rid, 'R');
	for_each_string_list_item(item, &q->check_set) {
		struct record *r = item->util;

		repin(r, PROVIDER, q->v, 0);
		if (full)
			put(r, CHECK, xstrfmt("%s %s", !q->force || (r->old && starts_with(val(r->old, CHECK), "reported ") && !strcmp(pins(r), pins(r->old))) ? "reported" : "forced", pins(r)));
	}
	tree = judged_tree(q, &changed);
	if (q->pull_tree) {
		*q->pull_tree = tree;
		for_each_string_list_item(item, &changed)
			strbuf_addstr(q->pull_out, change_line(q, item->util));
		for_each_string_list_item(item, &q->check_set)
			strbuf_addf(q->pull_out, "check %s %s\n", item->string, starts_with(val(item->util, CHECK), "reported ") ? "reported" : "forced");
		return;
	}
	if (!strcmp(tree, q->ht->oid) && !q->check_set.nr) {
		puts("noop");
		return;
	}
	if (results->len) {
		yaml_finish(results, &q->results, &q->renamed);
		strbuf_complete_line(results);
		hides(results->buf);
		strbuf_addf(&q->msg, "\n%s", results->buf);
	}
	strbuf_addf(&trailers, "Ait-Request: %s\nAit-Agent: %s\nAit-Session: %s\n", rid, agent, sid);
	for_each_string_list_item(item, &q->check_set)
		if (!full)
			strbuf_addf(&trailers, "Ait-Check: %s %s %s\n", item->string, q->force ? "forced" : "reported", pins(item->util));
	strbuf_addf(&q->msg, "\n%s", trailers.buf);
	/*
	 * spec 5.3: the message reads back to exactly these lines with the parser of
	 * 5.4, which takes no "---" divider (git v2.53.0 pretty.c:1767)
	 */
	if (strcmp(git_run(q->msg.buf, NULL, NULL, "interpret-trailers --parse --no-divider"), trailers.buf))
		reject(129, NULL, "the message hides the trailers");
	if (publish(tree, q->msg.buf, q->index))
		reject(1, "rerun this command", "HEAD moved while running; nothing changed");
	printf("request %s\n", rid);
	/* 6.7 and 7.7.5: one kind after another, by ID within a kind, but new in the order made */
	for (i = 0; i < changed.nr; i++)
		string_list_insert(&sorted, changed.items[i].string)->util = changed.items[i].util;
	for (kind = 0; kinds[kind]; kind++)
		for_each_string_list_item(item, kind ? &sorted : &changed)
			if (!strcmp(kind_of(q, item->util), kinds[kind]))
				fputs(change_line(q, item->util), stdout);
	for_each_string_list_item(item, &q->check_set)
		printf("check %s %s\n", item->string, q->force ? "forced" : "reported");
	for_each_string_list_item(item, &changed)
		also_undertaken(q, item->util);
}

static int finish(struct request *q)
{
	char *req = xstrfmt("%s/ait/check-request.yaml", gitdir), *res = xstrfmt("%s/ait/check-result.yaml", gitdir);
	struct string_list failing = STRING_LIST_INIT_DUP, fails = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct strbuf failed = STRBUF_INIT, results = STRBUF_INIT;
	struct verdict v;
	char *input = NULL;
	size_t k;

	transitions(q);
	check_set(q);
	if (!q->gate && !q->fulfill && !q->check_set.nr && !q->first.code) {
		/* 7.7a.1: nothing to check is a plain commit, without validate, session or trailers (5.10) */
		for (k = 0; k < q->s.nr; k++)
			if (q->s.r[k]->bad)
				warning("skipping malformed '.ait/%s' at 'index'", q->s.r[k]->id);
		if (!strcmp(q->v->oid, q->ht->oid))
			puts("noop");
		else if (publish(q->v->oid, q->msg.buf, q->index))
			reject(1, "rerun this command", "HEAD moved while running; nothing changed");
		return 0;
	}
	/* 5.13-6: the state is valid; fulfill and commit judge the records they found (5.10) */
	if (q->gate && (q->pull_tree ? check_records(&q->s, &v) : check_state(&q->s, q->v, &v)))
		reject(v.code, gate_hint(q, &v), "%s", v.msg);
	if (!q->gate && malformed(&q->s, &v))
		block(q, v);
	code_present(q);
	if (!q->gate && !q->missing.nr && check_state(&q->s, q->v, &q->invalid))
		block(q, q->invalid);
	/* 5.13-8: commit asks for its session right before the results */
	if (!q->gate && !q->fulfill && !q->first.code)
		session(1);
	if (!q->missing.nr && !q->force)
		input = check_input(q);
	if (input && q->tasks.nr && q->result && !q->first.code &&
	    !read_results(q, input, &results, &failed, &failing, &fails))
		strbuf_reset(&results);
	if (q->first.code || (q->tasks.nr && !q->force && !results.len)) {
		if (input && q->tasks.nr) {
			write_request_file(q, input, req);
			write_result_template(q, input, res);
		}
		report(q, req, res, input && q->tasks.nr);
		if (q->first.code)
			reject(q->first.code, q->first.hint, "%s", q->first.msg);
		if (q->pull_hint)
			reject(1, q->pull_hint, "check results required: %d task(s)", (int)q->tasks.nr);
		add_hint(q, xstrfmt("check each task, fill '%s', and rerun with -r %s", res, quoted(res)));
		add_hint(q, "if it is trivial, add --force instead (recorded as forced)");
		next_hints(q, &q->check_set);
		reject(1, hints(q), "check results required: %d task(s)", (int)q->tasks.nr);
	}
	if (failed.len) {
		/* 5.8 E_CHECK_FAILED: an unknown task is checked again elsewhere, a failed one is fixed */
		if (failing.nr > fails.nr) {
			add_hint(q, xstrfmt("check it where its environment is available (see '%s') and rerun with -r %s", req,
					    quoted(res)));
			add_hint(q, "if it is trivial, add --force (recorded as forced)");
		}
		fix_hints(q, &fails);
		next_hints(q, &failing);
		for_each_string_list_item(item, &failing)
			condition_hint(q, item->util);
		reject(1, hints(q), "check failed: %s", failed.buf);
	}
	commit_request(q, &results);
	return 0;
}

/* spec 7.7.1: ID=SCOPES moves the provider to scopes found in the staged tree, inside an abstract one */
static void given_scopes(struct request *q, struct record *r, const char *list)
{
	struct string_list parts = STRING_LIST_INIT_DUP, vals = STRING_LIST_INIT_DUP;
	struct string_list_item *item, *old;

	string_list_split(&parts, list, ",", -1);
	for_each_string_list_item(item, &parts) {
		int inside = 0;

		for_each_string_list_item(old, &r->f[PROVIDER]) {
			char *path = scope_path(old->string);

			inside |= !strcmp(path, "/") || (ends_with(path, "/") && starts_with(item->string, path));
		}
		if (is_abstract(r) && !inside)
			reject(128, NULL, "bad scope: '%s' is outside the scope of '%s'", item->string, r->id);
	}
	scope_list(&vals, list, q->v, q->index, 0, 0);
	r->f[PROVIDER] = vals;
}

static void select_promise(struct request *q, struct record *r)
{
	if (!in_state(r, "future"))
		reject(1, is_current(r) ? xstrfmt("'%s' is already current; to check it again, run 'ait amend %s -m <why>' "
						  "to get the tasks", r->id, r->id) : NULL,
		       "not allowed: '%s' is %s", r->id, val(r, STATE));
	put(r, STATE, "current");
	string_list_append(&q->selected, r->id);
}

/* rule 5: the future demands undertaken by an offer among the targets, from HEAD (7.7.2) */
static void carry(struct request *q, struct string_list *ids)
{
	size_t i;

	for (i = 0; i < q->s.nr; i++) {
		struct record *l = q->s.r[i], *o = undertaken(l) ? link_end(&q->s, l, 1) : NULL;
		struct string_list_item *d;

		if (!o || !unsorted_string_list_has_string(ids, o->id) || !in_state(o, "future"))
			continue;
		d = string_list_insert(&q->carried, partner(&q->s, l, o)->id);
		d->util = xstrfmt("%s %s", d->util ? (char *)d->util : " via", o->id);
	}
}

/* spec 7.7 and 7.7a: fulfill keeps the selected promises; commit only checks what the change touches */
int cmd_fulfill(int argc, const char **argv)
{
	struct request q = REQUEST_INIT;
	struct string_list paths = STRING_LIST_INIT_DUP, ids = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	size_t k;

	q.fulfill = !strcmp(argv[0], "fulfill");
	parse_options(&q, argc - 1, argv + 1);
	if (q.fulfill && !q.args.nr)
		reject(129, NULL, "nothing to fulfill: give the IDs of future contracts");
	/* spec 7.7.1: IDs once each; aliases live only inside one helper request */
	for (k = 0; k < q.args.nr; k++) {
		const char *a = q.args.v[k], *eq = strchr(a, '=');
		char *id = eq ? xstrndup(a, eq - a) : xstrdup(a);

		if (*a == '-' || unsorted_string_list_has_string(&ids, id) || (!q.fulfill && !eq) || islower(*id))
			reject(129, islower(*id) ? "aliases are local to one command; use the ID printed on its 'new' line" : NULL,
			       "bad argument '%s'", a);
		string_list_append(&ids, id)->util = eq ? xstrdup(eq + 1) : NULL;
		shapes(eq ? eq + 1 : NULL, 0, 0);
	}
	read_files(&q);
	message(&q, q.fulfill ? "[FULFILL] " : NULL);
	if (q.fulfill)
		session(1);
	load(&q);
	q.v = q.noted;
	/* each target's record, kept with its ID=SCOPES */
	for_each_string_list_item(item, &ids) {
		struct record *r = state_get(&q.s, item->string);

		if (!r || *item->string == 'u')
			reject(128, NULL, "no such contract: '%s'", item->string);
		readable(r);
	}
	if (q.fulfill)
		carry(&q, &ids);
	/* the selected targets: plain IDs, and ID=SCOPES of a future contract that no target offer carries */
	for_each_string_list_item(item, &ids) {
		struct record *r = state_get(&q.s, item->string);

		if (q.fulfill && (!item->util || (in_state(r, "future") && !carried(&q, r))))
			select_promise(&q, r);
	}
	for_each_string_list_item(item, &q.carried) {
		struct record *d = state_get(&q.s, item->string);

		if (in_state(d, "future"))
			put(d, STATE, "current");
	}
	for_each_string_list_item(item, &ids) {
		struct record *r = state_get(&q.s, item->string);

		if (!item->util)
			continue;
		string_list_append(&q.scoped, item->string);
		/* 7.7.1: only what becomes or stays current is checked, so only it takes scopes (5.13-4) */
		if (!is_current(r))
			reject(128, NULL, "bad scope: '%s' is not checked by this command", item->string);
		given_scopes(&q, r, item->util);
	}
	if (q.fulfill && !q.selected.nr)
		reject(129, NULL, "nothing to fulfill: give the IDs of future contracts");
	tree_diff(q.ht, q.v, &paths);
	for_each_string_list_item(item, &paths)
		if (contract_path(&q.s, item->string))
			string_list_append(&q.mixed, item->string);
	if (q.mixed.nr)
		block(&q, (struct verdict){ 1, "mixed", xstrfmt("unstage the spec files (%s), then rerun this command",
								  quoted_paths(&q.mixed)),
					    xstrfmt("contract changes and code changes are mixed: '%s'", q.mixed.items[0].string) });
	return finish(&q);
}

/*
 * why PATH, there in the work tree, is not a file of it (7.3.3), or NULL: it is
 * reached through a symlink, or is a directory, a FIFO or the like
 */
static const char *not_a_file(const char *path)
{
	const char *slash;
	struct stat st;

	for (slash = strchr(path, '/'); slash; slash = strchr(slash + 1, '/'))
		if (!lstat(xstrfmt("%s/%.*s", top, (int)(slash - path), path), &st) && S_ISLNK(st.st_mode))
			return "is beyond a symbolic link";
	/* a symlink is staged as git stages it, and judged by its mode then */
	return !lstat(xstrfmt("%s/%s", top, path), &st) && !S_ISREG(st.st_mode) && !S_ISLNK(st.st_mode) ?
	       "is not a regular file" : NULL;
}

/*
 * 5.8 E_INCOMPLETE "missing": restore PATH from HEAD, or create it, or stop
 * declaring it in DECL, the definitions of this request and the records of
 * HEAD that declare it
 */
static char *missing_hint(struct request *q, const char *path)
{
	struct string_list decl = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct strbuf in = STRBUF_INIT;
	size_t i;

	for_each_string_list_item(item, &q->declared)
		if (!strcmp(item->string, path))
			string_list_append(&decl, item->util);
	for (i = 0; i < q->s.nr; i++)
		if (q->s.r[i]->old && declares(q->s.r[i]->old, path))
			string_list_append(&decl, xstrfmt(".ait/%s", q->s.r[i]->id));
	if (decl.nr)
		strbuf_addf(&in, " in %s", quoted_paths(&decl));
	return blob_at(q->ht, path) ? xstrfmt("restore '%s' from HEAD, or stop declaring it%s", path, in.buf) :
	       xstrfmt("create it or stop declaring it%s", in.buf);
}

/*
 * spec 7.3.3: PATH is a spec file git can stage as it is in the work tree: no
 * case twin in HEAD or the index, a file there and not reached through a
 * symlink, and not under a file git has (rule 3: a contract commit changes no code)
 */
static void judge_file(struct request *q, const char *path)
{
	const char *why = not_a_file(path), *in_head = case_twin(q->ht, path), *in_index = case_twin(q->index, path);
	const char *twin = in_head ? in_head : in_index, *slash;
	struct stat st;

	/* git add stages nothing for a path that differs in case only from one git has */
	if (in_head && in_index && strcmp(in_head, in_index))
		reject(1, "spell it the same in HEAD and the index, then rerun this command",
		       "spec files do not match the contract: '%s' is '%s' and '%s' in git", path, in_head, in_index);
	if (twin && (tree_get(q->ht, path) || tree_get(q->index, path)))
		reject(1, xstrfmt("spell it the same in HEAD and the index ('%s' there), then rerun this command", twin),
		       "spec files do not match the contract: '%s' is '%s' in git", path, twin);
	if (twin)
		reject(1, xstrfmt("give it as git has it: '%s'", twin),
		       "spec files do not match the contract: '%s' is '%s' in git", path, twin);
	if (why)
		reject(1, NULL, "spec files do not match the contract: '%s' %s", path, why);
	for (slash = strchr(path, '/'); slash; slash = strchr(slash + 1, '/')) {
		char *up = xstrndup(path, slash - path);

		/* git add would put the directory in that file's place */
		if (file_entry(q->ht, up) || file_entry(q->noted, up))
			reject(1, xstrfmt("remove '%s' and run 'ait commit -m <why>' first, then rerun this command", up),
			       "spec files do not match the contract: '%s' is under the file '%s' in git", path, up);
	}
	if (lstat(xstrfmt("%s/%s", top, path), &st))
		reject(1, missing_hint(q, path), "spec files do not match the contract: missing '%s'", path);
}

/*
 * spec 7.3.3: the files of the request from the work tree on HEAD, staged by
 * git as git stages them; each a regular file, and the index holding HEAD's
 * version or the work tree's (no partial staging); git failing is E_GIT
 */
static struct tree *stage_files(struct request *q)
{
	struct string_list_item *item;
	struct strbuf info = STRBUF_INIT;
	struct tree *u;
	char *tree;

	for_each_string_list_item(item, &q->files)
		judge_file(q, item->string);
	/* on HEAD, the index's own versions of the files, so git add starts where the index is (core.fileMode) */
	for_each_string_list_item(item, &q->files) {
		struct entry *was = file_entry(q->noted, item->string);

		index_line(&info, was ? was->mode : NULL, was ? was->oid : NULL, item->string);
	}
	if (!(tree = tree_with(q->ht->oid, info.buf, &q->files)))
		reject(128, NULL, "%s", git_failed());
	u = tree_load(tree);
	for_each_string_list_item(item, &q->files) {
		/* the index as git commit takes it: an intent-to-add entry is not staged */
		struct entry *e = file_entry(u, item->string), *staged = file_entry(q->noted, item->string);

		/* 2.1.4: a spec file is a regular file */
		if (!e || (strcmp(e->mode, "100644") && strcmp(e->mode, "100755")))
			reject(1, NULL, "spec files do not match the contract: '%s' is not a regular file", item->string);
		if (!same_entry(staged, e) && !same_entry(staged, file_entry(q->ht, item->string)))
			reject(1, xstrfmt("stage all of '%s' or none of it", item->string),
			       "spec files do not match the contract: '%s' is partly staged", item->string);
	}
	return u;
}

/* the candidate U: the given definitions, read as git stages them, then what they declare (7.3.3) */
static struct tree *candidate(struct request *q)
{
	struct string_list defs = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct tree *u = stage_files(q);
	size_t n = q->files.nr;

	for_each_string_list_item(item, &q->files)
		if (item->util)
			string_list_append(&defs, item->string);
	def_inputs(u, &defs, &q->declared);
	for_each_string_list_item(item, &q->declared)
		if (!unsorted_string_list_has_string(&q->files, item->string))
			string_list_append(&q->files, item->string);
	return q->files.nr > n ? stage_files(q) : u;
}

/* spec 7.3: clauses apply in order to the records loaded from HEAD */
const char *pull_helpers(const char *tree, struct strvec *clauses, char *after_option, int force, struct string_list *conflicts, const char *hint, struct strbuf *out)
{
	struct request q = REQUEST_INIT;
	size_t i;

	q.gate = 1;
	q.pull_hint = hint;
	q.pull_tree = &tree;
	q.pull_out = out;
	q.force = force;
	q.ht = q.v = q.noted = q.index = tree_load(tree);
	state_load(&q.s, q.ht);
	strvec_pushv(&q.args, clauses->v);
	q.after_option = after_option;
	parse_clauses(&q);
	for (i = 0; i < q.ncl; i++)
		apply(&q, &q.cl[i]);
	for (i = 0; i < q.s.nr; i++)
		if (is_live(q.s.r[i]) && !is_current(q.s.r[i]))
			expand_files_except(q.s.r[i], q.v, conflicts);
	finish(&q);
	return tree;
}

/* spec 7.3: clauses apply in order to the records loaded from HEAD */
int cmd_request(int argc, const char **argv)
{
	static const char *const direct[] = { "new", "add", "commit", "push", "pull", NULL };
	struct request q = REQUEST_INIT;
	size_t k;

	if (argc > 1 && contract_word(argv[0]) && !strcmp(argv[1], "pull")) {
#ifdef AIT_MINI
		unsupported("contract pull");
#else
		return cmd_pull(argc - 2, argv + 2);
#endif
	}
	q.gate = 1;
	parse_options(&q, argc, argv);
	if (q.args.nr > 1 && contract_word(q.args.v[0]) && one_of(q.args.v[1], direct))
		unsupported(xstrfmt("contract %s", q.args.v[1]));
	parse_clauses(&q);
	read_files(&q);
	message(&q, "[CONTRACT] ");
	session(1);
	load(&q);
	for (k = 0; k < q.ncl; k++)
		apply(&q, &q.cl[k]);
	q.v = candidate(&q);
	for (k = 0; k < q.s.nr; k++)
		if (is_live(q.s.r[k]) && expand_files(q.s.r[k], q.v))
			reject(1, "create the spec files it declares, or stop declaring them",
			       "spec files do not match the contract: '%s'", q.s.r[k]->id);
	for (k = 0; k < q.s.nr; k++)
		if (is_live(q.s.r[k]) && !unchanged(q.s.r[k]))
			unpinned(&q, q.s.r[k]);
	return finish(&q);
}
