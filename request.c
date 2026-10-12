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
	int direct;		/* contract commit: the records are the index's, written by hand (7.6) */
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
 * spec 5.2-2 and 5.8 E_BUSY: the git operation in progress by its name: am,
 * rebase, merge, cherry-pick or revert (git v2.53.0 wt-status.c:1791); NULL
 * when there is none
 */
const char *git_op(void)
{
	struct strbuf todo = STRBUF_INIT;

	if (!access(xstrfmt("%s/rebase-apply/applying", gitdir), F_OK))
		return "am";
	if (!access(xstrfmt("%s/rebase-merge", gitdir), F_OK) || !access(xstrfmt("%s/rebase-apply", gitdir), F_OK))
		return "rebase";
	if (!access(xstrfmt("%s/MERGE_HEAD", gitdir), F_OK))
		return "merge";
	if (object("CHERRY_PICK_HEAD", 0, NULL, NULL))
		return "cherry-pick";
	if (object("REVERT_HEAD", 0, NULL, NULL))
		return "revert";
	/* picking several commits, between two of them: the sequencer's next command names it */
	if (strbuf_read_file(&todo, xstrfmt("%s/sequencer/todo", gitdir), 0) >= 0)
		return starts_with(todo.buf, "revert") ? "revert" : "cherry-pick";
	return NULL;
}

/* 7.1.3: the finishing commit, which leaves the resolution as a commit the hooks run for */
const char *finishing_commit(void)
{
	return strcmp(git_op() ?: "", "rebase") ? "git commit --allow-empty --no-edit --cleanup=strip" :
	       "git commit --allow-empty --allow-empty-message -C REBASE_HEAD";
}

/*
 * 5.9a: HINT, then the guidance for the git operation in progress: in an
 * integration its finishing hint (7.1.3); and when a contract path outside
 * the conflicts has changes git would commit without the hooks (unstaged
 * ones in an integration, else in the index or work tree), to keep them out
 */
char *op_guidance(const char *hint)
{
	struct string_list changed = STRING_LIST_INIT_DUP, conflicts = STRING_LIST_INIT_DUP;
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;
	struct state hs = { 0 };
	int in = !!integration_head(), keep = 0;

	strbuf_addstr(&sb, hint);
	if (in)
		strbuf_addf(&sb, "\ncommit the resolution first: '%s', then continue as git says", finishing_commit());
	z_paths(git("diff --name-only -z --diff-filter=U"), &conflicts);
	z_paths(git("diff --name-only -z"), &changed);
	z_paths(git("ls-files --others --exclude-standard -z"), &changed);
	if (!in && head)
		z_paths(git("diff --cached --name-only -z HEAD"), &changed);
	state_load(&hs, head_tree());
	for_each_string_list_item(item, &changed)
		keep |= contract_path(&hs, item->string) && !string_list_has_string(&conflicts, item->string);
	if (keep)
		strbuf_addstr(&sb, "; keep your contract changes out of it, since only ait commits contract files");
	return strbuf_detach(&sb, NULL);
}

/* PATH is in the work tree */
static int in_work_tree(const char *path)
{
	return !access(xstrfmt("%s/%s", top, path), F_OK);
}

/* the paths of a "-z" listing of git status --porcelain OUT that git has just given, without the status */
static void status_paths(const char *out, struct string_list *paths)
{
	const char *p, *end = out + git_out_len;

	for (p = out; p < end; p += strlen(p) + 1)
		if (strlen(p) > 3)
			string_list_insert(paths, p + 3);
}

/*
 * spec 5.2; CONTRACTS_STAGED for contract add and commit, whose request is
 * the changes under .ait/ (5.2-3)
 */
void precondition(int allow_unborn, int contracts_staged)
{
	struct string_list paths = STRING_LIST_INIT_DUP, code = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct state hs = { 0 };
	const char *op = git_op();

	if (op)
		reject(128, op_guidance(xstrfmt("a git %s is in progress; finish or abort it as 'git status' says, "
						"then rerun this command", op)),
		       "cannot run now: a git %s is in progress", op);
	if (!headref)
		reject(128, "run this command with HEAD on a branch", "cannot run now: HEAD is detached");
	if (!head && !allow_unborn)
		reject(128, "run 'ait init' first", "cannot run now: the branch has no commit");
	if (contracts_staged)
		return;
	status_paths(git("status --porcelain -z --untracked-files=all -- .ait"), &paths);
	if (!paths.nr)
		return;
	/* 5.9a, local changes: contract commit takes no staged code, so it is unstaged first */
	state_load(&hs, head_tree());
	z_paths(git("diff --cached --name-only -z HEAD"), &code);
	for (item = code.items; item < code.items + code.nr; )
		if (contract_path(&hs, item->string))
			string_list_remove(&code, item->string, 0);
		else
			item++;
	reject(1, xstrfmt("%scommit them through ait: %s\nor drop them: restore '.ait/' to HEAD's version and remove the new files there",
			  code.nr ? xstrfmt("unstage %s first; then ", quoted_paths(&code)) : "", through_ait(&paths)),
	       "uncommitted changes in .ait/");
}

/*
 * 5.10 for fulfill and commit: HEAD's error V is mended first by contract
 * commit, which takes no code, so the staged code is set aside for it and
 * staged again for this command to check (5.9a local changes)
 */
static char *mend_first(struct request *q, struct verdict *v)
{
	struct string_list paths = STRING_LIST_INIT_DUP, code = STRING_LIST_INIT_DUP;
	struct string_list_item *item;

	tree_diff(q->ht, q->noted, &paths);
	for_each_string_list_item(item, &paths)
		if (!contract_path(&q->s, item->string))
			string_list_append(&code, item->string);
	return head_repair(v, &code, 1);
}

/* 5.10: a malformed record cannot be read; the request stops with its error, mended as from this request */
static struct record *readable(struct request *q, struct record *r)
{
	struct state one = { 1, 1, &r };
	struct verdict v;

	if (malformed(&one, &v))
		reject(v.code, q->gate ? head_repair(&v, NULL, 0) : mend_first(q, &v), "%s", v.msg);
	return r;
}

/* HEAD is still the branch R, and R is the commit C */
static int at(const char *c)
{
	char oid[48];
	int rc;

	return !strcmp(line1(git_run(NULL, NULL, &rc, "symbolic-ref -q HEAD")), headref) &&
	       object(headref, 0, oid, NULL) && !strcmp(oid, c);
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
		/* a path under what is no directory is not there either (git v2.53.0 entry.c:603, wrapper.c:403) */
		if (tree_get(ct, item->string))
			git_run(NULL, NULL, &failed, "checkout-index -f -q -- %s", item->string);
		else
			failed = unlink(xstrfmt("%s/%s", top, item->string)) && errno != ENOENT && errno != ENOTDIR;
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
	precondition(0, 0);
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

/* spec 5.3; the message and result files are read first (5.12, 5.13-1) */
static void message(struct request *q, const char *prefix)
{
	struct strbuf sb = STRBUF_INIT;

	read_files(q);
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
	if (q.has_msg)
		message(&q, "[CONTRACT] ");
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
	return readable(q, state_get(&q->s, k));
}

/* a word of a hint command, if there is one: quoted for the shell when it holds a character the shell reads (5.9a) */
static void word(struct strbuf *sb, const char *w)
{
	if (!w)
		return;
	strbuf_addch(sb, ' ');
	sq_quote_buf_pretty(sb, w);
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
	if (!q->direct)
		return xstrfmt("%scommit code that meets it first: stage %s and run 'ait commit -m <why>', then rerun this command",
			       staged.nr ? xstrfmt("unstage %s first; then ", quoted_paths(&staged)) : "", code);
	/* contract commit: ait commit takes no change under .ait/ (5.2-3), so only the spec files step aside */
	if (*git("status --porcelain -- .ait"))
		return NULL;
	return xstrfmt("unstage %s first; then commit code that meets it first: stage %s and run 'ait commit -m <why>', then %s",
		       quoted_paths(&staged), code, through_ait(&staged));
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

/* V is the lone "-" that keeps what a clause's contract has (7.3.1) */
static int keeps(const char *v)
{
	return v && !strcmp(v, "-");
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
			/* 7.3.1: a demand takes A B FILES, an offer B FILES, a drop K; which one is checked once K is known */
			for (more = 0; i + more < n && !is_word(v[i + more]) && !q->after_option[i + more]; more++)
				;
			if (more > 3)
				reject(129, NULL, "amend '%s' takes A B FILES (a demand), B FILES (an offer) or K (a drop)", c->k[0]);
			c->a = more == 3 ? arg(q, &i) : NULL;
			c->b = more ? arg(q, &i) : NULL;
			c->f = more > 1 ? arg(q, &i) : NULL;
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

/*
 * 5.9a: the helper request again, with the clause of each contract in KS
 * changed: 'f' makes it a promise or reopens it ("future demand", "future
 * amend"), 'm' gives it KEPT,<new scope> as provider, 'u' puts ADD in place of
 * a link's clause; a contract no clause names gets one added, and so does the
 * clause ADD. Options give way to "-m <why>", and the command is quoted.
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
	return xstrfmt("'%s -m <why>'", sb.buf);
}

/* 5.9a: reopening SET inside this request: the helper again with "future amend", or contract commit's files set so */
static char *reopen_inside(struct request *q, struct string_list *set)
{
	struct string_list paths = STRING_LIST_INIT_DUP;
	struct string_list_item *item;

	if (!q->direct)
		return again(q, set, 'f', NULL);
	for_each_string_list_item(item, set)
		string_list_append(&paths, xstrfmt(".ait/%s", item->string));
	return xstrfmt("set 'state future' in %s, then %s", quoted_paths(&paths), through_ait(&paths));
}

/*
 * 5.8 E_SCOPE: this helper again with the scope PART of LIST as new:PART,
 * the contracts of SET made promises as 5.9a's promise makes them; a demand
 * among them that a later undertake clause gives to a current offer takes
 * that offer along (5.9 d), whose clauses are not applied yet
 */
static char *with_new(struct request *q, const char *list, const char *part, struct string_list *set)
{
	struct string_list parts = STRING_LIST_INIT_DUP, along = STRING_LIST_INIT_DUP;
	struct string_list_item *item, *d;
	struct clause *was = q->cl, *cl = xmemdupz(q->cl, q->ncl * sizeof(*q->cl));
	struct strbuf to = STRBUF_INIT;
	char *command, *path = scope_path(part);
	size_t i, j;
	int given = 0;

	string_list_split(&parts, list, ",", -1);
	/* new:PATH names a whole file (3.2), so it stands once for all the scopes of that file (3.2a) */
	for_each_string_list_item(item, &parts)
		if (strcmp(scope_path(item->string), path))
			strbuf_addf(&to, "%s%s", to.len ? "," : "", item->string);
		else if (!given++)
			strbuf_addf(&to, "%snew:%s", to.len ? "," : "", path);
	for (i = 0; i < q->ncl; i++) {
		if (cl[i].a == list)
			cl[i].a = to.buf;
		if (cl[i].b == list)
			cl[i].b = to.buf;
		for_each_string_list_item(d, set) {
			struct record *o;

			if (strcmp(cl[i].verb, "undertake") || *d->string != 'D' || !is_named(cl[i].k[0], d->util))
				continue;
			/* HEAD's current offer is reopened with it; one this request makes becomes a promise in its clause */
			if ((o = state_get(&q->s, cl[i].k[1])) && is_current(o))
				string_list_append(&along, o->id)->util = o;
			for (j = 0; !o && j < q->ncl; j++)
				if (!strcmp(cl[j].verb, "offer") && cl[j].alias && !strcmp(cl[j].alias, cl[i].k[1]))
					cl[j].future = 1;
		}
	}
	for_each_string_list_item(item, &along)
		if (!unsorted_string_list_has_string(set, item->string))
			string_list_append(set, item->string)->util = item->util;
	q->cl = cl;
	command = again(q, set, 'f', NULL);
	q->cl = was;
	free(cl);
	return command;
}

/*
 * 5.8 E_SCOPE for PART of LIST, a scope of R whose PATH the tree lacks: where
 * new:PATH may stand, the same command with it; for a contract becoming
 * current, the code first, or staged for fulfill and commit, when the work
 * tree has it, else the promise of it. Contract commit's record lines have
 * no such form here.
 */
static char *scope_hint(struct request *q, struct record *r, const char *list, const char *part, const char *path,
			int allow_new)
{
	struct string_list set = STRING_LIST_INIT_DUP;
	int there = !access(xstrfmt("%s/%s", top, path), F_OK);

	if (q->direct)
		return NULL;
	/* a promise, or a reopen of a current one, takes the current offers of 5.9 d along */
	if (!allow_new || (in_state(r, "future") && was_current(r)))
		reopen_set(&q->s, r, &set);
	if (allow_new)
		return xstrfmt("if it does not exist yet, run %s", with_new(q, list, part, &set));
	if (!q->gate)
		return there ? xstrfmt("stage %s, then rerun this command", quoted_paths(single(path))) : NULL;
	if (there)
		return code_first(q, quoted_paths(single(path)));
	return xstrfmt("if it does not exist yet, make it future: %s", with_new(q, list, part, &set));
}

/* spec 3.2a: a scope list "A,B", each pinned at T, its shape checked before (shapes()) */
static void scope_list(struct request *q, struct record *r, struct string_list *out, const char *list, struct tree *t,
		       int allow_new, int allow_abs)
{
	struct string_list parts = STRING_LIST_INIT_DUP;
	struct string_list_item *item;

	string_list_split(&parts, list, ",", -1);
	for_each_string_list_item(item, &parts) {
		char *path = scope_path(item->string), *pinned = pin(item->string, t, allow_new, allow_abs);

		/* 7.3.3: a case twin in the index refuses it too, unless the index spells it so */
		if (!tree_get(q->index, path) && case_twin(q->index, path))
			reject(128, xstrfmt("give it as git has it: '%s'", case_twin(q->index, path)), "bad scope: '%s'", item->string);
		if (!starts_with(item->string, "new:") && !ends_with(path, "/") && !tree_get(t, path) && !case_twin(t, path))
			reject(128, scope_hint(q, r, list, item->string, path, allow_new), "bad scope: '%s'", item->string);
		if (!pinned)
			reject(128, case_twin(t, path) ? xstrfmt("give it as git has it: '%s'", case_twin(t, path)) : NULL,
			       "bad scope: '%s'", item->string);
		string_list_append(out, pinned);
	}
}

/* a helper's scope argument, pinned at HEAD (spec 3.3); "-" keeps the old one */
static void scopes(struct request *q, struct record *r, enum key k, const char *list)
{
	struct string_list vals = STRING_LIST_INIT_DUP;
	int future = in_state(r, "future");

	if (!keeps(list)) {
		scope_list(q, r, &vals, list, q->ht, future || k == CONSUMER, future && k == PROVIDER);
		r->f[k] = vals;
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
	put(r, PREV, "-");
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
		return xstrfmt("to change '%s' without a check, reopen it: %s", k->id, reopen_inside(q, &set));
	return xstrfmt("%s, then %s", reopen_hint(&q->s, k), then_rerun(q, &set));
}

/* 5.9a transition hint 1: K is HEAD's accident (5.10), whose repair comes first; NULL when it is not */
static char *head_first(struct request *q, struct record *k)
{
	struct verdict v;

	return accident_at_head(k, &v) ? mend_first(q, &v) : NULL;
}

/*
 * 5.9a transition hint 3 in a helper's form: an ended K stays as it is, so
 * what is needed again is a new contract, as H6 makes it; a drop's withdraw
 * form is not written, as future drop is not supported here. Hint 1 comes first.
 */
static char *closed_hint(struct request *q, struct record *k)
{
	char *first = head_first(q, k);

	if (first)
		return first;
	if (*k->id == 'X')
		return xstrfmt("'%s' is closed", k->id);
	return xstrfmt("'%s' is closed\nto need it again, make a new contract: 'ait future %s%s %s %s -m <why>'", k->id,
		       val(k, ROLE), *k->id == 'D' ? " <your code>" : "", input_scopes(k, PROVIDER, q->ht), def_paths(k));
}

/* R does not change in this request: it means what HEAD has, or it is a new link released at once (7.3.5) */
static int unchanged(struct record *r)
{
	return r->old ? as_in_base(r) && !r->rebased : !r->rebased && *r->id == 'u' && in_state(r, "released");
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

/* H joins this request's hints, unless there is no such hint (NULL); suggest() writes each once */
static void add_hint(struct request *q, const char *h)
{
	if (h)
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

/* an alias the request does not use yet: BASE, or BASE and a number (5.9a) */
static const char *free_alias(struct request *q, const char *base)
{
	char *alias = xstrdup(base);
	int n = 0;

	while (alias_used(q, alias))
		alias = xstrfmt("%s%d", base, ++n);
	return alias;
}

/* 5.9a, moving K's scope in contract commit: its provider lines set to what is found at T and the new scope */
static char *moved_inside(struct record *k, struct tree *t)
{
	struct string_list parts = STRING_LIST_INIT_DUP;
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;

	string_list_split(&parts, moved_arg(k, t), ",", -1);
	for_each_string_list_item(item, &parts)
		strbuf_addf(&sb, "%s%s", sb.len ? " and " : "", item->string);
	return xstrfmt("if it was moved, set the 'provider' lines of '.ait/%s' to %s, then %s", k->id, sb.buf,
		       through_ait(single(xstrfmt(".ait/%s", k->id))));
}

/* 5.9a LINES: R's scopes as input at T and its definitions, as written by hand (2.2.3), each quoted, joined by ", " */
static char *written_lines(struct record *r, struct tree *t)
{
	static const enum key ks[] = { CONSUMER, PROVIDER, FILES };
	static const char *const words[] = { "consumer", "provider", "file" };
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;
	size_t k;

	for (k = 0; k < ARRAY_SIZE(ks); k++)
		for_each_string_list_item(item, &r->f[ks[k]])
			if (ks[k] != FILES || is_def(item->string))
				strbuf_addf(&sb, "%s'%s %s'", sb.len ? ", " : "", words[k],
					    ks[k] == FILES ? scope_path(item->string) : input_scope(item->string, t));
	return strbuf_detach(&sb, NULL);
}

/*
 * 5.9a "restore it": R back as its base has it, HEAD's or GOOD's (5.10),
 * or gone when it has no base
 */
static char *restore_it(struct record *r)
{
	if (!r->old)
		return xstrfmt("remove '.ait/%s'", r->id);
	return xstrfmt("restore '.ait/%s' as in '%s'", r->id, r->base_at ? r->base_at : "HEAD");
}

/* 5.9a transition hint 10: the request with the clause undertaking D by O turned into a future offer that does */
static char *undertake_hint(struct request *q, struct record *d, struct record *o)
{
	struct string_list set = STRING_LIST_INIT_DUP;
	const char *x = free_alias(q, "o"), *link = xstrfmt("undertake/%s-%s", d->id, o->id);
	struct record *l = state_get(&q->s, link);

	if (q->direct)
		return xstrfmt("%s\nundertake it with a future offer in this request: 'ait contract new offer', fill it "
			       "with %s, write 'state undertaken' to '.ait/undertake/%s-<O>', then 'ait contract add' them",
			       restore_it(l), written_lines(d, q->ht), d->id);
	string_list_append(&set, link)->util = l;
	return xstrfmt("undertake it with a future offer: %s",
		       again(q, &set, 'u', xstrfmt("future offer %s %s as %s undertake %s %s", input_scopes(d, PROVIDER, q->ht),
						   def_paths(d), x, d->alias ? d->alias : d->id, x)));
}

/*
 * 5.9a transition hints 2-8 and 12 in contract commit's form, for record R
 * written from state FROM to TO: edits of its files in this request; "restore
 * it" brings the record back as HEAD has it, or removes a new one
 */
static char *direct_hint(struct request *q, struct record *r, const char *from, const char *to)
{
	struct string_list set = STRING_LIST_INIT_DUP;
	char *path = xstrfmt(".ait/%s", r->id);
	char *restore = restore_it(r);
	struct record *as_was = xmemdupz(r, sizeof(*r));
	int mine = made_by(r->old ? r->old : r, agent);

	as_was->f[STATE] = r->old ? r->old->f[STATE] : r->f[STATE];
	/* 2: another role, or a new record neither future nor current */
	if ((r->old && strcmp(val(r->old, ROLE), val(r, ROLE))) || (!r->old && strcmp(to, "future") && strcmp(to, "current")))
		return restore;
	/* 3: an ended contract changed; the change goes to a new contract before the record is restored */
	if (!strcmp(from, "cancelled") || !strcmp(from, "withdrawn") || !strcmp(from, "fulfilled"))
		return *r->id == 'X' ? xstrfmt("'%s' is closed\n%s", r->id, restore) :
		       xstrfmt("'%s' is closed\nto need it again, move your change to a new contract in this request: 'ait "
			       "contract new %s', fill it with %s, then 'ait contract add' it\n%s", r->id, val(r, ROLE),
			       written_lines(r, q->ht), restore);
	/* 4: only a fulfil makes a promise current */
	if (!strcmp(from, "future") && (!strcmp(to, "current") || !strcmp(to, "fulfilled")))
		return xstrfmt("fulfill it instead: %s, then 'ait fulfill %s -m <why>'",
			       as_in_base(as_was) ? restore : xstrfmt("set 'state future' back in '%s', then %s", path, through_ait(single(path))),
			       r->id);
	/* 5: current to ended: reopen it */
	if (!strcmp(from, "current")) {
		reopen_set(&q->s, r, &set);
		return xstrfmt("to change '%s' without a check, reopen it: %s", r->id, reopen_inside(q, &set));
	}
	/* 6: a promise is withdrawn only when current; its maker cancels it */
	if (!strcmp(to, "withdrawn") && mine)
		return xstrfmt("set 'state cancelled' in '%s', then %s", path, through_ait(single(path)));
	return restore;
}

/* the hint of a verdict on the gate's candidate, as the same request with a clause changed (5.9a) */
static char *gate_hint(struct request *q, struct verdict *v)
{
	struct string_list set = STRING_LIST_INIT_DUP;

	if (at_head(v))
		return head_repair(v, NULL, !strcmp(v->word, "state"));
	/* 5.10: a malformed record that only this request has is fixed in it; what is no record goes */
	if (!strcmp(v->word, "format") && v->k)
		return v->k->mend || !strcmp(v->k->id, "init") ? head_repair(v, NULL, 0) :
		       xstrfmt("fix it in this request (see the error line), then %s", through_ait(single(xstrfmt(".ait/%s", v->k->id))));
	if (!v->k)
		return v->hint;
	if (v->o && !v->o->old) {
		string_list_append(&set, v->o->id)->util = v->o;
		return xstrfmt("if it does not hold yet, make it future: %s", reopen_inside(q, &set));
	}
	if (v->o && !was_current(v->k))
		return undertake_hint(q, v->k, v->o);
	reopen_set(&q->s, v->k, &set);
	if (v->o && !string_list_has_string(&set, v->o->id))
		string_list_append(&set, v->o->id)->util = v->o;
	if (v->o)
		return xstrfmt("'%s' is undertaken by current '%s'; reopen them in the same request: %s", v->k->id,
			       v->o->id, reopen_inside(q, &set));
	return xstrfmt("make the spec files of '%s' match it, or reopen it with them: %s", v->k->id, reopen_inside(q, &set));
}

/*
 * 5.9a, after the fix: a contract the helper registers as current can be made a
 * promise instead when PROMISE (5.8 gives it to E_CODE_REQUIRED and
 * E_CHECK_FAILED), and one current in HEAD can be reopened (except a demand
 * checked with the offer that undertakes it)
 */
static void next_hints(struct request *q, struct string_list *ids, int promise)
{
	struct string_list_item *item;

	for_each_string_list_item(item, ids) {
		struct record *r = item->util;

		if (q->gate && !r->old && promise) {
			struct string_list set = STRING_LIST_INIT_DUP;

			/* a demand this request has current offers undertake makes them promises too (5.9 d) */
			reopen_set(&q->s, r, &set);
			add_hint(q, xstrfmt("if it does not hold yet, make it future: %s", reopen_inside(q, &set)));
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
			else if (q->direct)
				add_hint(q, xstrfmt("fix '%s', then %s", path, through_ait(single(path))));
			else if (q->gate && unsorted_string_list_has_string(&q->files, path))
				/* 7.3.3: a version staged apart from HEAD's would be partly staged once the file is fixed */
				add_hint(q, xstrfmt(same_entry(file_entry(q->noted, path), file_entry(q->ht, path)) ?
						    "fix '%s', then rerun this command" : "fix '%s' and stage it again, then rerun this command",
						    path));
			else if (q->gate)
				add_hint(q, xstrfmt("fix '%s' and carry it in this request: %s", path, again(q, &none, 0, amend)));
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
	struct state hs = { 0 };

	if (q->gate || !was_current(k))
		return;
	/* the reopen runs on HEAD, where the offers that take K along are the current ones there (5.9 d) */
	state_load(&hs, q->ht);
	k = state_get(&hs, k->id);
	reopen_set(&hs, k, &set);
	add_hint(q, xstrfmt("to change its condition, reopen it with the new one: 'ait %s -m <why>', then %s",
			    reopen_clauses(&hs, k, *k->id == 'D' ? " - - <new defs>" : " - <new defs>"), then_rerun(q, &set)));
}

/*
 * 5.9a, release: a demand K that the one target offer of mine carries goes
 * out of this fulfill when released from it, unless the change touches K
 * (then it is checked anyway) or another target offer carries it too
 */
static void release_hint(struct request *q, struct record *k)
{
	struct string_list_item *carrier = string_list_lookup(&q->carried, k->id);
	const char *o = carrier ? (char *)carrier->util + strlen(" via ") : NULL;

	if (o && !strchr(o, ' ') && made_by(state_get(&q->s, o), agent) && !touches(k->old, q->ht, q->v))
		add_hint(q, xstrfmt("to fulfill '%s' without '%s', release it first: 'ait release %s %s -m <why>', then rerun "
				    "this command", o, k->id, k->id, o));
}

/* the first blocking condition wins (5.13); fulfill and commit keep computing their report */
static void block(struct request *q, struct verdict v)
{
	if (q->gate)
		reject(v.code, v.hint, "%s", v.msg);
	if (!q->first.code)
		q->first = v;
}

/* 5.13-6, 7.7.6: the first failure of validate refuses the request and is the report's 'invalid' line */
static void invalid(struct request *q, struct verdict v)
{
	if (!q->invalid.code)
		q->invalid = v;
	block(q, v);
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

		/*
		 * what means what the base means is the base, however the index spells
		 * it (contract commit, 2.2.3); the init file is no record, kept as it is (2.1.2a)
		 */
		if (!strcmp(r->id, "init") || (unchanged(r) && (!r->old || !strcmp(val(r, CHECK), val(r->old, CHECK))))) {
			if (r->base && strcmp(r->id, "init") && strcmp(blob_at(q->v, xstrfmt(".ait/%s", r->id)) ?: "", r->base))
				index_line(&info, "100644", r->base, xstrfmt(".ait/%s", r->id));
			continue;
		}
		put(r, PREV, r->base ? r->base : "-");
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
			if (q->s.r[i]->seq && q->s.r[i]->seq == atoi(task->string + 1))
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

/* 7.7.5: how R's check line says it was checked; reported PINS stay reported however they are checked again (2.2.2) */
static const char *checked_as(struct record *r)
{
	return starts_with(val(r, CHECK), "reported ") ? "reported" : "forced";
}

/*
 * H4: my link, made now with offer O, comes after the link L2 of offer O2: in
 * this request's state (REF NULL), one HEAD has undertaken is an ancestor's
 * and one made with mine has the same commit; on commit REF, by author time;
 * the same time, by O ID
 */
static int later_link(const char *ref, struct record *l2, struct record *o, struct record *o2)
{
	struct string_list ident = STRING_LIST_INIT_DUP;
	long theirs, mine;

	if (!ref && l2->old && undertaken(l2->old))
		return 1;
	if (!ref)
		return strcmp(o->id, o2->id) > 0;
	theirs = atol(line1(git("log -1 --format=%at %s -- %s", ref, xstrfmt(".ait/%s", l2->id))));
	string_list_split(&ident, line1(git("var GIT_AUTHOR_IDENT")), " ", -1);
	mine = ident.nr > 1 ? atol(ident.items[ident.nr - 2].string) : 0;
	return mine != theirs ? mine > theirs : strcmp(o->id, o2->id) > 0;
}

/* H4 (6.7a): my new link is not the only future offer on its demand */
static void also_undertaken(struct request *q, struct record *l)
{
	struct string_list refs = STRING_LIST_INIT_DUP, others = STRING_LIST_INIT_DUP;
	struct string_list_item *ref, *item;
	struct record *d, *o;
	size_t i;

	if (!undertaken(l) || (l->old && in_state(l->old, "undertaken")))
		return;
	d = link_end(&q->s, l, 0);
	o = link_end(&q->s, l, 1);
	/* H4 is for my future offer, and the other one is someone else's (rule 7) */
	if (!in_state(o, "future") || !made_by(o, agent))
		return;
	for (i = 0; i < q->s.nr; i++) {
		struct record *o2 = q->s.r[i] == l ? NULL : partner(&q->s, q->s.r[i], d);

		if (o2 && in_state(o2, "future") && !made_by(o2, agent) && later_link(NULL, q->s.r[i], o, o2))
			string_list_insert(&others, o2->id);
	}
	/* a link only another ref has (5.11) */
	ref_list(&refs);
	for_each_string_list_item(ref, &refs) {
		struct state rs = { 0 };

		state_load(&rs, tree_load(ref->util));
		for (i = 0; i < rs.nr; i++) {
			struct record *o2 = partner(&rs, rs.r[i], state_get(&rs, d->id));

			if (o2 && strcmp(o2->id, o->id) && in_state(o2, "future") && !made_by(o2, agent) &&
			    !contains(version_in(head, rs.r[i]->id), version_in(ref->util, rs.r[i]->id)) &&
			    later_link(ref->util, rs.r[i], o, o2))
				string_list_insert(&others, o2->id);
		}
	}
	for_each_string_list_item(item, &others) {
		warning("'%s' is also undertaken by '%s'", d->id, item->string);
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
			if (path_ok(a) && (blob_at(q->v, a) || in_work_tree(a)) &&
			    !declares(r, a)) {
				warning("'%s' is not pinned", a);
				suggest("if it is test material, add it to 'inputs' in '%s'", def);
			}
		}
	}
}

/* 5.9a: the transition hint for R's refused step FROM to TO: contract commit's form, an ended contract, or a reopen first */
static char *step_hint(struct request *q, struct record *r, const char *from, const char *to)
{
	if (q->direct)
		return direct_hint(q, r, from, to);
	if (*r->id == 'u')
		return NULL;
	if (r->old && !is_live(r->old))
		return closed_hint(q, r->old);
	return head_first(q, r) ?: was_current(r) ? reopen_first(q, r) : NULL;
}

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
			reject(1, *r->id == 'u' ? NULL : closed_hint(q, r), "not allowed: '%s' is not a future or current contract", r->id);
		if (c->b && ((*r->id == 'D') != !!c->a || (*r->id == 'X') != !c->f))
			reject(129, NULL, "amend '%s' takes A B FILES (a demand), B FILES (an offer) or K (a drop)", c->k[0]);
		if (c->future)
			put(r, STATE, "future");
		if (c->future)
			string_list_clear(&r->f[CHECK], 0);
		/* 7.3.2: an amend that keeps everything checks K again */
		r->recheck = is_current(r) && (!c->b || ((!c->a || keeps(c->a)) && keeps(c->b) && keeps(c->f)));
		if (c->f) {
			scopes(q, r, CONSUMER, c->a ? c->a : "-");
			scopes(q, r, PROVIDER, c->b);
			defs(q, r, c->f);
		} else if (c->b && !keeps(c->b)) {
			put(r, TARGET, resolve(q, c->b)->id);
		}
	} else if (!strcmp(c->verb, "cancel")) {
		/* 7.3.2: K is future; cancelled already is a noop */
		r = resolve(q, c->k[0]);
		if (!in_state(r, "future") && !in_state(r, "cancelled"))
			reject(1, step_hint(q, r, val(r, STATE), "cancelled"), "not allowed: '%s' cannot go from %s to cancelled",
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

/* spec 2.3: every changed record took an allowed step */
static void transitions(struct request *q)
{
	size_t i;
	int j;

	for (i = 0; i < q->s.nr; i++) {
		struct record *r = q->s.r[i];
		const char *from = r->old ? val(r->old, STATE) : "-", *to = val(r, STATE);

		/*
		 * a malformed record has no state to judge, and validate refuses it (5.9 a); the init file
		 * is no contract; what reads as its base version makes no transition, an ended one too (5.10)
		 */
		if (r->bad || !strcmp(r->id, "init") || (r->old ? as_in_base(r) : unchanged(r)))
			continue;
		/*
		 * 2.3: a drop is made, moved and fulfilled with future drop, which is not
		 * supported here (choice 161), so a live one only goes to cancelled
		 */
		if (*r->id == 'X' && (!r->old || (is_live(r->old) && (strcmp(to, "cancelled") ||
							       strcmp(val(r, TARGET), val(r->old, TARGET))))))
			unsupported("drop");
		for (j = 0; *r->id == 'u' && !strcmp(to, "undertaken") && j < 2; j++) {
			struct record *end = link_end(&q->s, r, j), *d = link_end(&q->s, r, 0);

			if (!is_live(end))
				reject(1, j ? undertake_hint(q, d, end) : closed_hint(q, end), "not allowed: '%s' is %s", end->id,
				       val(end, STATE));
		}
		if (!record_step(r->id, from, to, q->gate))
			reject(1, step_hint(q, r, from, to), "not allowed: '%s' cannot go from %s to %s", r->id, from, to);
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
			int written = in_work_tree(file), new = starts_with(sc->string, "new:");

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
			/* 5.8: a contract HEAD has may have moved its code anyway */
			moved |= !(written || new) || k->old;
		}
		/* 5.8 E_CODE_REQUIRED, a hint per kind with all its paths: write, stage (the gate commits code first), or moved */
		if (write.nr)
			add_hint(q, xstrfmt("write %s", quoted_paths(&write)));
		if (stage.nr)
			add_hint(q, q->gate ? code_first(q, quoted_paths(&stage)) :
				 xstrfmt("stage %s, then rerun this command", quoted_paths(&stage)));
		if (moved && k->old)
			add_hint(q, q->direct ? moved_inside(k, q->v) :
				 q->gate ? xstrfmt("if it was moved, run %s", again(q, &one, 'm', NULL)) :
				 xstrfmt("if it was moved, rerun with '%s=%s'", k->id, moved_arg(k, q->v)));
		if (!write.nr && !stage.nr && !moved)
			continue;
		string_list_append(&q->missing, item->string)->util = item->util;
		/* 5.8: each contract's hints in turn: then a promise or a reopen, a carried demand released, my chosen promise gone */
		next_hints(q, &one, 1);
		release_hint(q, k);
		if (unsorted_string_list_has_string(&q->selected, k->id) && made_by(k, agent))
			add_hint(q, xstrfmt("if it is gone for good, run 'ait future cancel %s -m <why>'", k->id));
	}
	if (!q->missing.nr)
		return;
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

		/* 8.1: a record a helper makes here has no ID until it commits, so its task names its sequence number */
		for_each_string_list_item(f, &r->f[FILES])
			if (is_def(f->string))
				string_list_insert(&q->tasks, xstrfmt("%s:%s", r->seq ? xstrfmt("+%d", r->seq) : r->id,
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
/* spec 6.2: the spec files of every live contract of the request, expanded and pinned at its tree */
static void expand_request(struct request *q)
{
	size_t i;

	for (i = 0; i < q->s.nr; i++)
		if (!q->s.r[i]->bad && is_live(q->s.r[i]) && expand_files(q->s.r[i], q->v))
			reject(1, "create the spec files it declares, or stop declaring them",
			       "spec files do not match the contract: '%s'", q->s.r[i]->id);
}

/*
 * spec 7.5.1-7.5.2: the records the work tree has under .ait/, as written by
 * hand (2.2.3), into WS; and W, HEAD with those files, the definitions they
 * declare and what those declare, as git add stages them from the work tree
 */
static struct tree *worktree_state(struct request *q, struct state *ws)
{
	struct string_list files = STRING_LIST_INIT_DUP;
	struct strbuf gone = STRBUF_INIT;
	struct string_list_item *item, *f;
	const char *p, *end, *id;
	struct tree *w;
	size_t i;

	for (p = git("ls-files -z --cached --others --exclude-standard -- .ait"), end = p + git_out_len; p < end; p += strlen(p) + 1) {
		struct strbuf sb = STRBUF_INIT;

		if (skip_prefix(p, ".ait/", &id) && !read_text(&sb, xstrfmt("%s/%s", top, p))) {
			string_list_insert(&files, p);
			*state_add(ws, id) = *record_written(id, sb.buf);
		}
	}
	/* a written record names its definitions only (2.2.3); one the work tree lacks is gone from W too */
	for (i = 0; i < ws->nr; i++)
		for_each_string_list_item(f, &ws->r[i]->f[FILES])
			if (is_live(ws->r[i]) && in_work_tree(scope_path(f->string)))
				string_list_insert(&files, scope_path(f->string));
			else if (is_live(ws->r[i]))
				index_line(&gone, NULL, NULL, scope_path(f->string));
	w = tree_load(tree_with(q->ht->oid, gone.buf, &files));
	/* each declares what its definitions declare in the work tree, there or not (6.1) */
	for (i = 0; i < ws->nr; i++) {
		struct string_list defs = STRING_LIST_INIT_DUP, declared = STRING_LIST_INIT_DUP;

		if (!is_live(ws->r[i]))
			continue;
		for_each_string_list_item(f, &ws->r[i]->f[FILES])
			string_list_insert(&defs, scope_path(f->string));
		def_inputs(w, &defs, &declared);
		for_each_string_list_item(item, &declared) {
			string_list_append(&ws->r[i]->f[FILES], xstrfmt("%s - - input", item->string));
			if (in_work_tree(item->string))
				string_list_insert(&files, item->string);
			else
				index_line(&gone, NULL, NULL, item->string);
		}
	}
	return tree_load(tree_with(q->ht->oid, gone.buf, &files));
}

/*
 * 7.6.1: what contract commit's noop leaves: the contract paths (7.5.1) the
 * work tree has otherwise than the index, as what was fixed is not committed,
 * which is this command's result, not a risk (rule 6); once none is left,
 * records the index spells otherwise than HEAD, which mean what HEAD's do as
 * the noop shows (user decision 44), as restoring them first would throw
 * away what is to be staged
 */
static void noop_left(struct request *q)
{
	struct string_list differ = STRING_LIST_INIT_DUP, paths = STRING_LIST_INIT_DUP, spelled = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct state hs = { 0 }, ws = { 0 };

	state_load(&hs, q->ht);
	worktree_state(q, &ws);
	z_paths(git("diff --name-only -z"), &differ);
	z_paths(git("ls-files --others --exclude-standard -z -- .ait"), &differ);
	for_each_string_list_item(item, &differ)
		if (contract_path(&hs, item->string) || contract_path(&ws, item->string)) {
			printf("unstaged %s\n", item->string);
			string_list_append(&paths, item->string);
		}
	tree_diff(q->ht, q->v, &spelled);
	if (paths.nr)
		suggest("stage it: 'ait contract add %s', then rerun this command", shell_words(&paths));
	else if (spelled.nr)
		suggest("restore %s as in 'HEAD': it means what HEAD's version means", quoted_paths(&spelled));
}

/* 6.7a H9's contracts, judged as this request ends (5.9a): on its commit, else on HEAD as it is then, before a refusal's error too */
static struct string_list h9_ids = STRING_LIST_INIT_DUP;

static void h9(void)
{
	char oid[48];

	before_error = NULL;
	if (!published)
		head = object("HEAD^{commit}", 0, oid, NULL) ? xstrdup(oid) : NULL;
	changes_not_in_head(&h9_ids);
	string_list_clear(&h9_ids, 0);
}

static void commit_request(struct request *q, struct strbuf *results)
{
	static const char *const kinds[] = { "new", "changed", "fulfilled", "undertaken", "released", NULL };
	struct string_list changed = STRING_LIST_INIT_DUP, sorted = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct strbuf trailers = STRBUF_INIT;
	char rid[16], *tree;
	size_t i;
	int kind;

	newid(rid, 'R');
	/* 2.2.2: the starting version is the one this request starts from */
	for_each_string_list_item(item, &q->check_set) {
		struct record *r = item->util;
		struct string_list from = STRING_LIST_INIT_DUP;
		struct version *was = version_of(r->id, blob_at(q->ht, xstrfmt(".ait/%s", r->id)));

		repin(r, PROVIDER, q->v, 0);
		if (was && !was->r->bad)
			string_list_append(&from, was->oid)->util = was;
		put(r, CHECK, check_line(r, !q->force, &from));
	}
	tree = judged_tree(q, &changed);
	if (q->pull_tree) {
		*q->pull_tree = tree;
		for_each_string_list_item(item, &changed)
			strbuf_addstr(q->pull_out, change_line(q, item->util));
		for_each_string_list_item(item, &q->check_set)
			strbuf_addf(q->pull_out, "check %s %s\n", item->string, checked_as(item->util));
		return;
	}
	/* 6.6: checking the same content the same way leaves the record as it was */
	if (!strcmp(tree, q->ht->oid)) {
		h9();
		puts("noop");
		if (q->direct)
			noop_left(q);
		return;
	}
	if (results->len) {
		yaml_finish(results, &q->results, &q->renamed);
		strbuf_complete_line(results);
		hides(results->buf);
		strbuf_addf(&q->msg, "\n%s", results->buf);
	}
	strbuf_addf(&trailers, "Ait-Request: %s\nAit-Agent: %s\nAit-Session: %s\n", rid, agent, sid);
	strbuf_addf(&q->msg, "\n%s", trailers.buf);
	/*
	 * spec 5.3: the message reads back to exactly these lines with the parser of
	 * 5.4, which takes no "---" divider (git v2.53.0 pretty.c:1767)
	 */
	if (strcmp(git_run(q->msg.buf, NULL, NULL, "interpret-trailers --parse --no-divider"), trailers.buf))
		reject(129, NULL, "the message hides the trailers");
	if (publish(tree, q->msg.buf, q->index))
		reject(1, "rerun this command", "HEAD moved while running; nothing changed");
	head = published;
	printf("request %s\n", rid);
	/* 6.7 and 7.7.5: one kind after another, by ID within a kind, but new in the order made */
	for (i = 0; i < changed.nr; i++)
		string_list_insert(&sorted, changed.items[i].string)->util = changed.items[i].util;
	for (kind = 0; kinds[kind]; kind++)
		for_each_string_list_item(item, kind ? &sorted : &changed)
			if (!strcmp(kind_of(q, item->util), kinds[kind]))
				fputs(change_line(q, item->util), stdout);
	for_each_string_list_item(item, &q->check_set)
		printf("check %s %s\n", item->string, checked_as(item->util));
	for_each_string_list_item(item, &changed)
		also_undertaken(q, item->util);
	h9();
}

/*
 * 6.7a H9: the contracts HEAD has that this request changes or checks: 9.2's
 * R, where a changed link changes what its D and O mean, and the check set
 */
static void elsewhere(struct request *q)
{
	size_t i;
	int end;

	if (q->pull_tree)
		return;
	for (i = 0; i < q->s.nr; i++) {
		struct record *r = q->s.r[i];

		if (r->bad || (unchanged(r) && !string_list_has_string(&q->check_set, r->id)))
			continue;
		if (*r->id != 'u')
			string_list_insert(&h9_ids, r->id);
		for (end = 0; *r->id == 'u' && end < 2; end++)
			string_list_insert(&h9_ids, link_end_id(r, end));
	}
	for (i = 0; i < h9_ids.nr; )
		if (blob_at(q->ht, xstrfmt(".ait/%s", h9_ids.items[i].string)))
			i++;
		else
			string_list_remove(&h9_ids, h9_ids.items[i].string, 0);
	before_error = h9;
}

static int finish(struct request *q)
{
	char *req = xstrfmt("%s/ait/check-request.yaml", gitdir), *res = xstrfmt("%s/ait/check-result.yaml", gitdir);
	struct string_list failing = STRING_LIST_INIT_DUP, fails = STRING_LIST_INIT_DUP, written = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct strbuf failed = STRBUF_INIT, results = STRBUF_INIT;
	struct verdict v;
	char *input = NULL;
	size_t k;

	/* 5.10: what the request writes is judged from its base version, GOOD's when entangled with HEAD's errors */
	for (k = 0; k < q->s.nr; k++)
		if (!q->s.r[k]->bad && !unchanged(q->s.r[k]))
			string_list_insert(&written, q->s.r[k]->id);
	if (!q->pull_tree)
		base_versions(&q->s, &written);
	/* 2.2.5, 6.2: a contract's creator never changes, so it is its base's; without a base it is new, made by this session */
	for_each_string_list_item(item, &written) {
		struct record *r = state_get(&q->s, item->string);

		if (r->f[ROLE].nr)
			r->f[CREATOR] = r->old ? r->old->f[CREATOR] : *single(xstrfmt("%s %s", agent, sid));
	}
	/* 6.2, 2.4.1: an ended contract's files are not expanded or pinned again, so its base's definitions keep their lines */
	for (k = 0; k < q->s.nr; k++) {
		struct record *r = q->s.r[k];

		if (!r->bad && r->old && !is_live(r) && !strcmp(def_paths(r), def_paths(r->old)))
			r->f[FILES] = r->old->f[FILES];
	}
	transitions(q);
	check_set(q);
	elsewhere(q);
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
	/* 5.13-6: the state is valid and HEAD holds no accident (5.10); fulfill and commit judge the records they found */
	if (q->gate && (q->pull_tree ? check_records(&q->s, &v) : check_state(&q->s, q->v, &v)))
		reject(v.code, gate_hint(q, &v), "%s", v.msg);
	if (!q->gate && malformed(&q->s, &v)) {
		v.hint = mend_first(q, &v);
		invalid(q, v);
	}
	/* an accident the request leaves is HEAD's; what it writes was judged from its base (5.10) */
	for (k = 0; k < q->s.nr && !q->pull_tree; k++)
		if (unchanged(q->s.r[k]) && accident_at_head(q->s.r[k], &v)) {
			if (!q->gate)
				v.hint = mend_first(q, &v);
			invalid(q, v);
		}
	code_present(q);
	if (!q->gate && !q->missing.nr && check_state(&q->s, q->v, &v)) {
		if (at_head(&v))
			v.hint = mend_first(q, &v);
		invalid(q, v);
	}
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
		/* pull takes no -r (7.12.5): its own hint, or none */
		if (q->pull_tree)
			reject(1, q->pull_hint, "check results required: %d task(s)", (int)q->tasks.nr);
		add_hint(q, xstrfmt("check each task, fill '%s', and rerun with -r %s", res, quoted(res)));
		add_hint(q, "if it is trivial, add --force instead (recorded as forced)");
		next_hints(q, &q->check_set, 0);
		reject(1, hints(q), "check results required: %d task(s)", (int)q->tasks.nr);
	}
	if (failed.len) {
		/* 5.8 E_CHECK_FAILED: each task in turn: a failed one is fixed, an unknown one checked elsewhere, then its contract */
		for_each_string_list_item(item, &failing) {
			struct string_list one = STRING_LIST_INIT_DUP;

			string_list_append(&one, item->string)->util = item->util;
			if (unsorted_string_list_has_string(&fails, item->string)) {
				fix_hints(q, &one);
			} else {
				add_hint(q, xstrfmt("check it where its environment is available (see '%s') and rerun with -r %s",
						    req, quoted(res)));
				add_hint(q, "if it is trivial, add --force (recorded as forced)");
			}
			next_hints(q, &one, 1);
			release_hint(q, item->util);
			condition_hint(q, item->util);
		}
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
	scope_list(q, r, &vals, list, q->v, 0, 0);
	r->f[PROVIDER] = vals;
}

static void select_promise(struct request *q, struct record *r)
{
	if (!in_state(r, "future"))
		reject(1, !is_current(r) ? closed_hint(q, r) : head_first(q, r) ?:
		       xstrfmt("'%s' is already current; to check it again, run 'ait amend %s -m <why>'", r->id, r->id),
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
		readable(&q, r);
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
		block(&q, (struct verdict){ 1, "mixed", xstrfmt("unstage the spec files (%s), then rerun this command; when they "
								  "are ready, run %s", quoted_paths(&q.mixed), through_ait(&q.mixed)),
					    xstrfmt("contract changes and code changes are mixed: %s", quoted_paths(&q.mixed)) });
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

/* PATH into OUT, with WHO among what declares it (5.8 DECL) */
static void declared_by(struct string_list *out, const char *path, const char *who)
{
	struct string_list_item *item = string_list_insert(out, path);

	if (!item->util)
		item->util = xcalloc(1, sizeof(struct string_list));
	if (who)
		string_list_insert(item->util, who);
}

/*
 * spec 7.5.2: into OUT, the related paths of the contracts PATHS touch: each
 * record, and what the work tree's record (HEAD's when it has none) declares:
 * its definitions and what those declare as the work tree has them (6.2). A
 * contract that has ended declares nothing (2.4.1), and a record gone from
 * the work tree brings none: whether it may go is the gate's (2.3.1).
 */
static void related_paths(struct state *hs, struct state *ws, struct tree *w, struct string_list *paths,
			  struct string_list *out)
{
	struct string_list_item *p, *f;
	size_t i, j;

	for_each_string_list_item(p, paths)
		for (j = 0; j < 2; j++)
			for (i = 0; i < (j ? hs : ws)->nr; i++) {
				struct record *r = (j ? hs : ws)->r[i];
				struct string_list defs = STRING_LIST_INIT_DUP, declared = STRING_LIST_INIT_DUP;

				if ((j && state_get(ws, r->id)) || !is_live(r) ||
				    (strcmp(p->string, xstrfmt(".ait/%s", r->id)) && !declares(r, p->string)))
					continue;
				declared_by(out, xstrfmt(".ait/%s", r->id), NULL);
				for_each_string_list_item(f, &r->f[FILES])
					if (is_def(f->string))
						string_list_insert(&defs, scope_path(f->string));
				def_inputs(w, &defs, &declared);
				for_each_string_list_item(f, &defs)
					declared_by(out, f->string, xstrfmt(".ait/%s", r->id));
				for_each_string_list_item(f, &declared)
					declared_by(out, f->string, f->util);
			}
}

/* 7.5.2: into OUT, the PATHS the work tree has otherwise than the index, an untracked file too */
static void unstaged_of(struct string_list *paths, struct string_list *out)
{
	struct strvec diff = STRVEC_INIT, others = STRVEC_INIT;
	struct string_list_item *item;

	strvec_pushl(&diff, "diff", "--name-only", "-z", "--", NULL);
	strvec_pushl(&others, "ls-files", "--others", "--exclude-standard", "-z", "--", NULL);
	for_each_string_list_item(item, paths) {
		strvec_push(&diff, item->string);
		strvec_push(&others, item->string);
	}
	z_paths(git_args(&diff, NULL, NULL, NULL), out);
	z_paths(git_args(&others, NULL, NULL, NULL), out);
}

/*
 * spec 7.5.2 and 7.6.1: the related paths of what PATHS touch are in the work
 * tree, and those PATHS leave out are as the index has them; else the
 * "missing" and "unstaged" lines and E_INCOMPLETE. UNTRACKED are "unstaged"
 * too. Staging the unstaged ones makes what they touch related in turn, so
 * the hint stages the paths that close it.
 */
static void bundle_complete(struct request *q, struct string_list *paths, struct string_list *untracked)
{
	struct string_list related = STRING_LIST_INIT_DUP, differ = STRING_LIST_INIT_DUP, missing = STRING_LIST_INIT_DUP;
	struct string_list unstaged = STRING_LIST_INIT_DUP, all = STRING_LIST_INIT_DUP, wrong = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct state hs = { 0 }, ws = { 0 };
	struct tree *w = worktree_state(q, &ws);
	size_t nr;

	state_load(&hs, q->ht);
	related_paths(&hs, &ws, w, paths, &related);
	unstaged_of(&related, &differ);
	for_each_string_list_item(item, &related)
		if (!in_work_tree(item->string))
			string_list_insert(&missing, item->string)->util = item->util;
		else if (string_list_has_string(&differ, item->string) && !unsorted_string_list_has_string(paths, item->string))
			string_list_insert(&unstaged, item->string);
	for_each_string_list_item(item, untracked)
		string_list_insert(&unstaged, item->string);
	if (!missing.nr && !unstaged.nr)
		return;
	for_each_string_list_item(item, &missing) {
		printf("missing %s\n", item->string);
		string_list_insert(&wrong, item->string);
		add_hint(q, xstrfmt("%s, or stop declaring it in %s", blob_at(q->ht, item->string) ?
				    xstrfmt("restore '%s' from HEAD", item->string) : xstrfmt("create '%s'", item->string),
				    quoted_paths(item->util)));
	}
	for_each_string_list_item(item, &unstaged) {
		printf("unstaged %s\n", item->string);
		string_list_insert(&wrong, item->string);
	}
	/* 5.8: the paths staged together, grown until 7.5.2 holds for them */
	for_each_string_list_item(item, paths)
		string_list_insert(&all, item->string);
	for_each_string_list_item(item, &unstaged)
		string_list_insert(&all, item->string);
	do {
		nr = all.nr;
		string_list_clear(&related, 0);
		string_list_clear(&differ, 0);
		related_paths(&hs, &ws, w, &all, &related);
		unstaged_of(&related, &differ);
		for_each_string_list_item(item, &related)
			if (string_list_has_string(&differ, item->string))
				string_list_insert(&all, item->string);
	} while (all.nr != nr);
	if (unstaged.nr)
		add_hint(q, xstrfmt("stage them together: 'ait contract add %s'", shell_words(&all)));
	reject(1, hints(q), "spec files do not match the contract: %s", quoted_paths(&wrong));
}

/*
 * spec 7.5: stage PATHS, contract paths of HEAD and of the work tree, when
 * every related path of what they touch is in the work tree and staged with
 * them (7.5.2); a record gone from the work tree stages its removal
 */
static int contract_add(struct request *q)
{
	struct string_list paths = STRING_LIST_INIT_DUP, none = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct state hs = { 0 }, ws = { 0 };
	struct strvec add = STRVEC_INIT;
	size_t i;

	for (i = 2; i < q->args.nr; i++)
		string_list_append(&paths, q->args.v[i]);
	if (!paths.nr)
		reject(129, NULL, "contract add takes PATH...");
	/* 5.1: no session is needed, but one that is set must be usable (5.13-2) */
	session(0);
	precondition(0, 1);
	q->ht = head_tree();
	state_load(&hs, q->ht);
	worktree_state(q, &ws);
	for_each_string_list_item(item, &paths)
		if (!contract_path(&hs, item->string) && !contract_path(&ws, item->string))
			reject(1, NULL, "contract changes and code changes are mixed: '%s' is no contract path", item->string);
	bundle_complete(q, &paths, &none);
	strvec_pushl(&add, "add", "-A", "--", NULL);
	for_each_string_list_item(item, &paths)
		strvec_push(&add, item->string);
	git_args(&add, NULL, NULL, NULL);
	for_each_string_list_item(item, &paths)
		printf("staged %s\n", item->string);
	return 0;
}

/* a scope as a helper argument: its value without the BLOB or TREE pinned with it (2.2.2) */
static char *scope_arg(const char *v)
{
	return xstrndup(v, strcspn(v, " "));
}

/* the pinned value of OLD's scope of key K written as ARG: a kept scope is not pinned again (6.2); NULL for none */
static const char *kept_scope(struct record *old, enum key k, const char *arg)
{
	struct string_list_item *item;

	if (!old)
		return NULL;
	for_each_string_list_item(item, &old->f[k])
		if (!strcmp(scope_arg(item->string), arg))
			return item->string;
	return NULL;
}

/*
 * spec 6.2 for record R of contract commit, written as W, against its base:
 * role, state and target as written; a scope kept as the base has it, the
 * others pinned (3.3); the definitions are expanded later; the creator is
 * the session's for a new contract and the base's for one it has (2.2.3);
 * the check line is the base's while it stays current (6.5 writes another)
 */
static void normalize(struct request *q, struct record *r, struct record *w)
{
	static const enum key ks[] = { CONSUMER, PROVIDER };
	int other_creator = r->old && w->f[CREATOR].nr && strcmp(val(w, CREATOR), val(r->old, CREATOR));
	struct string_list_item *item;
	size_t k;

	r->f[ROLE] = w->f[ROLE];
	r->f[STATE] = w->f[STATE];
	r->f[TARGET] = w->f[TARGET];
	r->f[FILES] = w->f[FILES];
	for (k = 0; k < ARRAY_SIZE(ks); k++) {
		struct string_list vals = STRING_LIST_INIT_DUP;

		for_each_string_list_item(item, &w->f[ks[k]])
			if (kept_scope(r->old, ks[k], item->string))
				string_list_append(&vals, kept_scope(r->old, ks[k], item->string));
			else
				scope_list(q, r, &vals, item->string, q->ht, in_state(r, "future") || ks[k] == CONSUMER,
					   in_state(r, "future") && ks[k] == PROVIDER);
		r->f[ks[k]] = vals;
	}
	string_list_clear(&r->f[CHECK], 0);
	if (r->old && is_current(r) && is_current(r->old))
		r->f[CHECK] = r->old->f[CHECK];
	/* judged_tree() writes the base's blob */
	if (r->old)
		r->f[PREV] = r->old->f[PREV];
	else
		put(r, PREV, "-");
	/* 5.9a transition hint 2: the creator stays; the rest of the change is not lost when there is any */
	if (other_creator) {
		if (is_live(r))
			expand_files(r, q->v);
		reject(1, as_in_base(r) ? restore_it(r) :
		       xstrfmt("remove the 'creator' line from '.ait/%s' (HEAD's value is kept), then 'ait contract add .ait/%s' "
			       "and 'ait contract commit -m <why>'", r->id, r->id),
		       "not allowed: the creator of '%s' cannot change", r->id);
	}
}

/*
 * spec 7.6: the contract files the index holds go through the gate (6) as
 * they are: the candidate U is the index tree, each changed record read in
 * the authoring form and normalized against HEAD's (6.2)
 */
static int contract_commit(struct request *q)
{
	struct string_list changed = STRING_LIST_INIT_DUP, staged = STRING_LIST_INIT_DUP, untracked = STRING_LIST_INIT_DUP;
	struct string_list code = STRING_LIST_INIT_DUP, written = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct state us = { 0 };
	const char *id;
	size_t i;

	if (q->args.nr > 2)
		reject(129, NULL, "contract commit takes no arguments but its options");
	message(q, "[CONTRACT] ");
	session(1);
	precondition(0, 1);
	q->direct = 1;
	q->ht = head_tree();
	q->noted = q->v = tree_load(line1(git("write-tree")));
	q->index = index_load(q->noted);
	state_load(&q->s, q->ht);
	tree_diff(q->ht, q->v, &changed);
	for_each_string_list_item(item, &changed)
		if (skip_prefix(item->string, ".ait/", &id))
			string_list_insert(&written, id);
	base_versions(&q->s, &written);
	/* 7.6.1: what the index has of the contracts it changes is all there, and nothing waits under .ait/ */
	z_paths(git("ls-files --others --exclude-standard -z -- .ait"), &untracked);
	for_each_string_list_item(item, &changed)
		if (file_entry(q->v, item->string))
			string_list_append(&staged, item->string);
	bundle_complete(q, &staged, &untracked);
	/* the changed records as written (2.2.3) over HEAD's: U's, each declaring what its definitions declare in U */
	state_load(&us, q->ht);
	for_each_string_list_item(item, &changed) {
		struct entry *e = file_entry(q->v, item->string);

		if (!skip_prefix(item->string, ".ait/", &id))
			continue;
		state_drop(&us, id);
		if (!e)
			continue;
		item->util = record_from_entry(e, id, 1);
		*state_add(&us, id) = *record_copy(item->util);
	}
	for (i = 0; i < us.nr; i++)
		if (!us.r[i]->bad && is_live(us.r[i]))
			expand_files(us.r[i], q->v);
	/* 6.1 before 6.2 (user decision 48): only contract paths, as HEAD has them and as U declares them */
	for_each_string_list_item(item, &changed)
		if (!contract_path(&q->s, item->string) && !contract_path(&us, item->string))
			string_list_append(&code, item->string);
	if (code.nr) {
		for_each_string_list_item(item, &code)
			printf("impl %s\n", item->string);
		reject(1, xstrfmt("unstage the code (%s), then rerun this command; when the code is ready, stage it and run "
				  "'ait commit -m <why>'", quoted_paths(&code)),
		       "contract changes and code changes are mixed: %s", quoted_paths(&code));
	}
	/* 6.2: each changed record as written, against its base version (5.10) */
	for_each_string_list_item(item, &changed) {
		struct record *r, *w = item->util;

		if (!skip_prefix(item->string, ".ait/", &id))
			continue;
		r = state_get(&q->s, id);
		/* 2.1.5: a record stays; what is no record, the init file, or a record GOOD lacks may go (5.10) */
		if (!w) {
			if (!r->mend && strcmp(id, "init") && r->old)
				reject(1, restore_it(r), "not allowed: records are never removed: '%s'", item->string);
			state_drop(&q->s, id);
			continue;
		}
		if (!r)
			r = state_add(&q->s, id);
		/* what does not read is refused by validate; the init file is no contract to normalize */
		r->bad = w->bad;
		r->mend = w->mend;
		if (!w->bad && strcmp(id, "init"))
			normalize(q, r, w);
	}
	expand_request(q);
	return finish(q);
}

/* spec 7.4.1a: the frames of a spec definition, whose comments name the keys it may have (rule 7) */
static const char *const spec_frames[][2] = {
	{ "exec", "# ait spec definition (exec): the checker runs 'command'; it passes if the exit code is in 'success'.\n"
		  "# Keys: body, engine, command, success, environment, inputs. Any other key is a format error.\n"
		  "body: |\n  <the condition the code must keep>\nengine: exec\n"
		  "command: [<program>, <arg>]   # argv, run without a shell\n"
		  "success: [0]                  # exit codes that count as pass\n"
		  "inputs: []                    # files pinned with the contract: scripts in 'command', test data. Not the code under check\n"
		  "# environment: <name>         # optional: specs/environments/<name>.yaml (cwd, timeout_ms, env, prepare, cleanup)\n" },
	{ "prompt", "# ait spec definition (prompt): the checker reads the code and judges whether 'body' holds.\n"
		    "# Keys: body, inputs. Any other key is a format error.\n"
		    "body: |\n  <the condition the code must keep>\n"
		    "inputs: []                    # files pinned with the contract, such as test data. Not the code under check\n" },
};

/* spec 7.4.1: the authoring form's frame of a new contract of each role (2.2.3), and its ID's letter (1.10) */
static const char *const contract_frames[][3] = {
	{ "demand", "D", "role demand\nstate future\nconsumer PATH\nprovider PATH\nfile PATH\n" },
	{ "offer", "O", "role offer\nstate future\nprovider PATH\nfile PATH\n" },
};

/*
 * spec 7.4: write a frame to the work tree for the person to fill, staging
 * nothing: a new contract under a new ID, or a spec definition at PATH
 */
static int contract_new(struct request *q)
{
	const char *what = q->args.nr > 2 ? q->args.v[2] : "", *frame = NULL;
	char id[16], *path;
	size_t i;

	if (!strcmp(what, "spec") && q->args.nr == 5) {
		path = xstrdup(q->args.v[3]);
		for (i = 0; i < ARRAY_SIZE(spec_frames); i++)
			if (!strcmp(spec_frames[i][0], q->args.v[4]))
				frame = spec_frames[i][1];
		if (!frame)
			reject(129, NULL, "contract new spec takes PATH exec|prompt");
		if (!path_ok(path))
			reject(128, NULL, "bad scope: '%s'", path);
		session(0);
		if (!lstat(xstrfmt("%s/%s", top, path), &(struct stat){ 0 }))
			reject(1, xstrfmt("edit '%s' or choose another path", path), "not allowed: '%s' already exists", path);
		/* the directories above it, as git checkout makes them */
		for (i = 0; path[i]; i++)
			if (path[i] == '/')
				mkdir(xstrfmt("%s/%.*s", top, (int)i, path), 0777);
		write_state(xstrfmt("%s/%s", top, path), frame);
		printf("path %s\n", path);
		return 0;
	}
	/* a drop could not be fulfilled here, as future drop is not supported */
	if (!strcmp(what, "drop"))
		unsupported("contract new drop");
	for (i = 0; i < ARRAY_SIZE(contract_frames); i++)
		if (!strcmp(contract_frames[i][0], what) && q->args.nr == 3)
			break;
	if (i == ARRAY_SIZE(contract_frames))
		reject(129, NULL, "contract new takes demand, offer or spec PATH exec|prompt");
	/* 5.1: as in contract add, after the shapes (5.13) */
	session(0);
	do
		newid(id, *contract_frames[i][1]);
	while (!access(xstrfmt("%s/.ait/%s", top, id), F_OK) || blob_at(head_tree(), xstrfmt(".ait/%s", id)));
	mkdir(xstrfmt("%s/.ait", top), 0777);
	write_state(xstrfmt("%s/.ait/%s", top, id), contract_frames[i][2]);
	printf("path .ait/%s\n", id);
	return 0;
}

/* spec 7.3: clauses apply in order to the records loaded from HEAD */
int cmd_request(int argc, const char **argv)
{
	static const char *const direct[] = { "new", "add", "commit", "push", "pull", NULL };
	struct request q = REQUEST_INIT;
	size_t k;

	if (argc > 1 && contract_word(argv[0]) && !strcmp(argv[1], "pull"))
		return cmd_pull(argc - 2, argv + 2);
	q.gate = 1;
	parse_options(&q, argc, argv);
	if (q.args.nr > 1 && contract_word(q.args.v[0]) && !strcmp(q.args.v[1], "new"))
		return contract_new(&q);
	if (q.args.nr > 1 && contract_word(q.args.v[0]) && !strcmp(q.args.v[1], "add"))
		return contract_add(&q);
	if (q.args.nr > 1 && contract_word(q.args.v[0]) && !strcmp(q.args.v[1], "commit"))
		return contract_commit(&q);
	if (q.args.nr > 1 && contract_word(q.args.v[0]) && one_of(q.args.v[1], direct))
		unsupported(xstrfmt("contract %s", q.args.v[1]));
	parse_clauses(&q);
	message(&q, "[CONTRACT] ");
	session(1);
	load(&q);
	for (k = 0; k < q.ncl; k++)
		apply(&q, &q.cl[k]);
	q.v = candidate(&q);
	expand_request(&q);
	for (k = 0; k < q.s.nr; k++)
		if (is_live(q.s.r[k]) && !unchanged(q.s.r[k]))
			unpinned(&q, q.s.r[k]);
	return finish(&q);
}
