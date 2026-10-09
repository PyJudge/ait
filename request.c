/*
 * Requests (spec 1.7): the helpers (7.3), fulfill (7.7) and commit (7.7a).
 * Each one edits the loaded records in place and names the tree the request
 * is judged in; finish() then runs one pipeline: transitions (2.3), the check
 * set (1.5b), validate (5.9), the code to check, results (8) and the commit (5.5).
 */
#include "ait.h"
#include "strvec.h"
#include "quote.h"
#include <sys/stat.h>

struct request {
	struct state s;		/* every record keeps its HEAD version in ->old */
	struct tree *ht, *v;	/* HEAD's tree and the tree judged */
	int gate, fulfill, force, seq;
	const char *result;
	struct strbuf msg, cmdline;	/* the message; the helper request without options, for hints */
	struct string_list files;	/* spec files given to the helpers */
	struct string_list scoped;	/* IDs given with =SCOPES */
	struct string_list selected;	/* the promises fulfill was asked to keep */
	struct string_list conf;	/* the check set, ID -> record */
	struct string_list missing;	/* the check set without code to check */
	struct string_list tasks;	/* "CONTRACT:DEF" -> record */
	struct string_list hints;
	struct string_list carried;	/* rule 5: D -> " via O..." of the target offers that undertake it */
	struct string_list mixed;	/* the 7.7.6 "mixed PATH" lines */
	struct verdict invalid;		/* the 7.7.6 "invalid" line: the state after the request */
	struct verdict first;		/* the first condition that blocks fulfill or commit (5.13) */
	struct strvec args;		/* the arguments that are not options */
	char *after_option;		/* args.v[i] came right after an option */
};

#define REQUEST_INIT { .msg = STRBUF_INIT, .cmdline = STRBUF_INIT, .files = STRING_LIST_INIT_DUP, \
	.scoped = STRING_LIST_INIT_DUP, .selected = STRING_LIST_INIT_DUP, .conf = STRING_LIST_INIT_DUP, \
	.missing = STRING_LIST_INIT_DUP, .tasks = STRING_LIST_INIT_DUP, .hints = STRING_LIST_INIT_DUP, \
	.carried = STRING_LIST_INIT_DUP, .mixed = STRING_LIST_INIT_DUP, .args = STRVEC_INIT }

/* spec 2.3: the allowed transitions of a demand or offer ('x') and of a link ('u'), and who makes them */
static const struct move {
	char kind;
	const char *from, *to;
	unsigned gate : 1, fulfill : 1;
} moves[] = {
	{ 'x', "-", "future", 1, 0 }, { 'x', "-", "current", 1, 0 }, { 'x', "future", "future", 1, 0 },
	{ 'x', "future", "cancelled", 1, 0 }, { 'x', "future", "current", 0, 1 }, { 'x', "current", "current", 1, 1 },
	{ 'x', "current", "future", 1, 0 }, { 'u', "-", "undertaken", 1, 0 }, { 'u', "released", "undertaken", 1, 0 },
	{ 'u', "undertaken", "released", 1, 0 },
};

static int allowed(struct request *q, struct record *r, const char *from, const char *to)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(moves); i++)
		if (moves[i].kind == (*r->id == 'u' ? 'u' : 'x') && !strcmp(moves[i].from, from) &&
		    !strcmp(moves[i].to, to))
			return q->gate ? moves[i].gate : moves[i].fulfill;
	return 0;
}

/* spec 5.2-2: a merge, cherry-pick, revert, rebase or am is in progress (git v2.53.0 wt-status.c:1791) */
int git_op_in_progress(void)
{
	static const char *const files[] = { "MERGE_HEAD", "rebase-merge", "rebase-apply", "sequencer/todo", NULL };
	static const char *const refs[] = { "CHERRY_PICK_HEAD", "REVERT_HEAD", NULL };
	int i, rc;

	for (i = 0; files[i]; i++)
		if (!access(xstrfmt("%s/%s", gitdir, files[i]), F_OK))
			return 1;
	for (i = 0; refs[i]; i++) {
		git_run(NULL, NULL, &rc, "rev-parse -q --verify %s", refs[i]);
		if (!rc)
			return 1;
	}
	return 0;
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

/*
 * spec 5.5-4: after the commit, the index takes C's version of each changed path
 * where it still held HEAD's, and .ait/ in the work tree follows; anything else
 * staged meanwhile is left alone and reported.
 */
static void follow(struct tree *h, const char *c)
{
	struct tree *ct = tree_load(c), *it = tree_load(line1(git("write-tree")));
	struct string_list paths = STRING_LIST_INIT_DUP, ait = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct strbuf info = STRBUF_INIT;

	tree_diff(h, ct, &paths);
	for_each_string_list_item(item, &paths) {
		struct entry *x = tree_get(it, item->string), *was = tree_get(h, item->string);
		struct entry *now = tree_get(ct, item->string);

		if (x && now && !strcmp(x->oid, now->oid) && !strcmp(x->mode, now->mode))
			continue;
		if (x ? !was || strcmp(x->oid, was->oid) || strcmp(x->mode, was->mode) : was != NULL) {
			warning("committed '%s' to '%s', but '%s' was staged meanwhile; it is left as it is",
				c, headref, item->string);
			continue;
		}
		strbuf_addf(&info, "%s %s\t%s\n", now->mode, now->oid, item->string);
		if (starts_with(item->string, ".ait/"))
			string_list_append(&ait, item->string);
	}
	git_run(info.buf, NULL, NULL, "update-index --index-info");
	for_each_string_list_item(item, &ait)
		git("checkout-index -f -q -- %s", item->string);
}

/*
 * spec 5.5 through git commit: TREE, the tree ait judged, is committed from a
 * throwaway index, so a refused commit leaves the user's index and work tree
 * as they were. ait's own hooks are skipped (11.2) except reference-transaction,
 * which aborts the commit if the branch is not where ait judged it (AIT_EXPECT).
 * Returns 1 when HEAD moved; other failures stop with git's reason.
 */
int publish(const char *tree, const char *msg)
{
	struct tree *h = tree_load(head ? head : EMPTY_TREE);
	char *dir = xstrfmt("%s/ait-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp"), *index;
	struct strvec commit = STRVEC_INIT;
	char *moved, *why;
	int rc, refused, gone;

	if (!mkdtemp(dir))
		die_errno("could not create '%s'", dir);
	index = xstrfmt("%s/index", dir);
	moved = xstrfmt("%s/moved", dir);
	/*
	 * whether TREE is worth a commit was judged against HEAD as ait read it
	 * (noop, or 6.5's check with no changed file), so git does not judge it again
	 */
	strvec_pushl(&commit, "commit", "--no-verify", "--allow-empty", "--cleanup=verbatim", "-q", "-F", "-", NULL);
	git_run(NULL, index, &rc, "read-tree %s", tree);
	/* the hook leaves MOVED when it refuses the transaction: the CAS itself says HEAD moved */
	setenv("AIT_EXPECT", xstrfmt("%s %s %s %s", head ? head : "0000000000000000000000000000000000000000", tree,
				     headref, moved), 1);
	if (!rc)
		git_args(&commit, msg, index, &rc);
	unsetenv("AIT_EXPECT");
	why = xstrdup(git_error);
	/*
	 * two locks guard the branch: ait's hook, which leaves MOVED, and git's own
	 * ref lock, which refuses before any hook runs (git v2.53.0 refs.c:2564-2566)
	 * and so only when the branch already left the commit ait judged
	 */
	refused = !access(moved, F_OK) ||
		  (rc && (strcmp(head ? head : "", line1(git_run(NULL, NULL, &gone, "rev-parse -q --verify HEAD"))) ||
			  strcmp(headref, line1(git_run(NULL, NULL, &gone, "symbolic-ref -q HEAD")))));
	unlink(moved);
	unlink(index);
	rmdir(dir);
	if (refused)
		return 1;
	if (rc)
		reject(128, NULL, "git failed: %s", why);
	follow(h, line1(git("rev-parse HEAD")));
	return 0;
}

/* a file given to ait is read as text (E_READ, or E_FORMAT when it holds a NUL) */
static void text_or_die(struct strbuf *sb, const char *path)
{
	int rc = read_text(sb, path);

	if (rc < 0)
		reject(128, NULL, "could not read '%s'", path);
	if (rc > 0)
		reject(128, NULL, "bad format: '%s': not a text file", path);
}

static void load(struct request *q)
{
	precondition(0);
	q->ht = tree_load(head);
	state_load(&q->s, q->ht);
	reject_malformed(&q->s);
}

/*
 * spec 5.3 and 7.3.1: -m, -F, -r and --force may stand anywhere; the other
 * arguments go to q->args in order. Usage comes first (5.13-1).
 */
static void parse_options(struct request *q, int argc, const char **argv)
{
	int i, from_m = 0, from_file = 0, option = 0;

	q->after_option = xcalloc(argc + 1, 1);
	for (i = 0; i < argc; i++) {
		const char *a = argv[i], *v;

		if (!strcmp(a, "--force")) {
			q->force = option = 1;
			continue;
		}
		if ((strcmp(a, "-m") && strcmp(a, "-F") && strcmp(a, "-r") && strcmp(a, "--result")) || i + 1 >= argc) {
			q->after_option[q->args.nr] = option;
			strvec_push(&q->args, a);
			option = 0;
			continue;
		}
		option = 1;
		v = argv[++i];
		if (!strcmp(a, "-m") && !from_file) {
			strbuf_addf(&q->msg, "%s%s", from_m++ ? "\n\n" : "", v);
		} else if (!strcmp(a, "-F") && !from_file && !from_m) {
			from_file = 1;
			text_or_die(&q->msg, v);
		} else if ((!strcmp(a, "-r") || !strcmp(a, "--result")) && !q->result) {
			q->result = v;
		} else {
			reject(129, NULL, "option '%s' conflicts with another option", a);
		}
	}
	if (q->force && q->result)
		reject(129, NULL, "options '-r' and '--force' cannot be used together");
	if (q->result && access(q->result, R_OK))
		reject(128, "rerun without -r to get the check-result path", "could not read '%s'", q->result);
}

/* spec 5.3 */
static void message(struct request *q, const char *prefix)
{
	struct strbuf sb = STRBUF_INIT;

	strbuf_stripspace(&q->msg, NULL);
	if (!q->msg.len)
		reject(129, NULL, "a message is required: give -m <msg> or -F <file>");
	if (strstr(q->msg.buf, "# ------------------------ >8 ------------------------"))
		reject(129, "remove that line from the message", "the message hides the trailers: "
		       "'# ------------------------ >8 ------------------------'");
	if (prefix && !starts_with(q->msg.buf, prefix))
		strbuf_addstr(&sb, prefix);
	strbuf_addstr(&sb, q->msg.buf);
	q->msg = sb;
}

/* the next argument of a clause; options stand only between clauses (7.3.1) */
static const char *arg(struct request *q, int *i)
{
	const char *a = (size_t)*i < q->args.nr ? q->args.v[*i] : NULL;

	if (!a || (a[0] == '-' && a[1]) || q->after_option[*i])
		reject(129, NULL, "missing argument after '%s'", q->args.v[*i - 1]);
	return q->args.v[(*i)++];
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
	return state_get(&q->s, k);
}

/* spec 3.2a: a scope list "A,B", each pinned at T; the same scope twice is a bad scope */
static void scope_list(struct string_list *out, const char *list, struct tree *t, int allow_new, int allow_abs)
{
	struct string_list parts = STRING_LIST_INIT_DUP;
	struct string_list_item *item;

	string_list_split(&parts, list, ",", -1);
	for_each_string_list_item(item, &parts) {
		char *v = pin(item->string, t, allow_new, allow_abs);

		if (unsorted_string_list_has_string(out, v))
			reject(128, NULL, "bad scope: '%s' is given twice", item->string);
		string_list_append(out, v);
	}
}

/* a helper's scope argument, pinned at HEAD (spec 3.3); "-" keeps the old one */
static void scopes(struct request *q, struct record *r, enum key k, const char *list)
{
	struct string_list vals = STRING_LIST_INIT_DUP;
	int future = in_state(r, "future");

	if (strcmp(list, "-")) {
		scope_list(&vals, list, q->ht, future || k == CONSUMER, future && k == PROVIDER);
		r->f[k] = vals;
	}
}

/* the definitions given to a helper; expand_files() pins them and what they declare */
static void defs(struct request *q, struct record *r, const char *list)
{
	struct string_list parts = STRING_LIST_INIT_DUP;
	struct string_list_item *item;

	if (!strcmp(list, "-"))
		return;
	string_list_split(&parts, list, ",", -1);
	string_list_clear(&r->f[FILES], 0);
	for_each_string_list_item(item, &parts) {
		if (!path_ok(item->string))
			reject(128, NULL, "bad scope: '%s'", item->string);
		string_list_append(&q->files, item->string)->util = (void *)"def";
		string_list_append(&r->f[FILES], xstrfmt("%s - - def", item->string));
	}
}

static void new_contract(struct request *q, const char *role, int future, int *i)
{
	char id[16];
	struct record *r;
	const char *alias;
	size_t k;

	do
		newid(id, toupper(*role));
	while (state_get(&q->s, id));
	r = state_add(&q->s, id);
	r->seq = ++q->seq;
	put(r, ROLE, role);
	put(r, STATE, future ? "future" : "current");
	if (*role == 'd')
		scopes(q, r, CONSUMER, arg(q, i));
	scopes(q, r, PROVIDER, arg(q, i));
	defs(q, r, arg(q, i));
	put(r, CREATOR, xstrfmt("%s %s", agent, sid));
	put(r, PREV, "-");
	if ((size_t)*i >= q->args.nr || strcmp(q->args.v[*i], "as"))
		return;
	++*i;
	alias = arg(q, i);
	if (!islower(*alias) || strlen(alias) > 32 || alias[strspn(alias, "abcdefghijklmnopqrstuvwxyz0123456789_")] ||
	    is_word(alias))
		reject(129, NULL, "bad alias '%s'", alias);
	for (k = 0; k < q->s.nr; k++)
		if (q->s.r[k]->alias && !strcmp(q->s.r[k]->alias, alias))
			reject(129, NULL, "alias '%s' is used twice in one request", alias);
	r->alias = alias;
}

/* spec 8.4: in the task values of a result file, the new contract +SEQ becomes ID */
static void rename_tasks(struct strbuf *sb, int seq, const char *id)
{
	struct string_list kv = STRING_LIST_INIT_DUP;
	char *from = xstrfmt("+%d:", seq), *err;
	const char *t;
	int n;

	yaml_parse(sb->buf, &kv, "results", &err);
	for (n = kv.nr; n >= 0; n--)
		if ((t = yget(&kv, xstrfmt("results.%d.task", n))) && starts_with(t, from))
			strbuf_splice(sb, yat(&kv, xstrfmt("results.%d.task", n)), strlen(from) - 1, id, strlen(id));
}

static int unchanged(struct record *r)
{
	return r->old ? !strcmp(record_text(r, 0), record_text(r->old, 0)) :
	       *r->id == 'u' && in_state(r, "released");
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

		if (q->gate && !r->old)
			add_hint(q, xstrfmt("or make it a promise: 'ait future %s %s%s%s %s -m <why>'", val(r, ROLE),
					    scope_args(r, CONSUMER), *r->id == 'D' ? " " : "", scope_args(r, PROVIDER),
					    def_paths(r)));
		else if (was_current(r) && !carried(q, r))
			add_hint(q, xstrfmt("to change '%s' without a check, reopen it: 'ait future amend %s -m <why>'",
					    r->id, r->id));
	}
}

/* 5.9a, the fix for a failed check: the code, or the spec file that is the provider */
static void fix_hints(struct request *q, struct string_list *ids)
{
	struct string_list_item *item, *sc;
	size_t k;

	for_each_string_list_item(item, ids) {
		for_each_string_list_item(sc, &((struct record *)item->util)->f[PROVIDER]) {
			char *path = scope_path(sc->string);
			struct record *owner = NULL;

			for (k = 0; k < q->s.nr && !owner; k++)
				if (q->s.r[k]->old && declares(q->s.r[k]->old, path))
					owner = q->s.r[k];
			if (!owner)
				add_hint(q, q->gate ? "commit code that meets it first: stage <your code> and run 'ait commit -m <why>', "
					 "then rerun this command" : "make the code meet the failed task(s) and stage it, then rerun "
					 "without -r to get the new tasks");
			else if (q->gate && unsorted_string_list_has_string(&q->files, path))
				add_hint(q, xstrfmt("fix '%s', then rerun this command", path));
			else
				add_hint(q, xstrfmt(q->gate ? "fix '%s' and carry it in this request: '%s amend %s %s%s -m <why>'" :
						    "fix '%s' and commit it first: '%sait amend %s %s%s -m <why>', then rerun this command",
						    path, q->gate ? q->cmdline.buf : "", owner->id, *owner->id == 'D' ? "- - " : "- ",
						    def_paths(owner->old)));
		}
	}
}

/* the first blocking condition wins (5.13); fulfill and commit keep computing their report */
static void block(struct request *q, int code, const char *word, char *hint, char *msg)
{
	if (q->gate)
		reject(code, hint, "%s", msg);
	if (!q->first.code) {
		q->first.code = code;
		q->first.word = word;
		q->first.hint = hint;
		q->first.msg = msg;
	}
}

/* the double-quoted YAML scalar of spec 8.2 */
static char *dq(const char *s)
{
	struct strbuf sb = STRBUF_INIT;

	strbuf_addch(&sb, '"');
	for (; *s; s++) {
		if (*s == '"' || *s == '\\')
			strbuf_addch(&sb, '\\');
		strbuf_addch(&sb, *s);
	}
	strbuf_addch(&sb, '"');
	return strbuf_detach(&sb, NULL);
}

static char *task_def(struct string_list_item *task)
{
	return strchr(task->string, ':') + 1;
}

/* spec 8.2 and 4.7: the check request and the result template for the tasks */
static void write_check_files(struct request *q, const char *in, const char *req, const char *res)
{
	struct strbuf sb = STRBUF_INIT, old = STRBUF_INIT;
	struct string_list kv = STRING_LIST_INIT_DUP;
	struct string_list_item *task;
	char *err;

	strbuf_addf(&sb, "input: %s\ntree: %s\ntasks:\n", dq(in), dq(q->v->oid));
	for_each_string_list_item(task, &q->tasks) {
		struct record *r = task->util;
		struct string_list *kv = def_get(q->v, task_def(task)), body = STRING_LIST_INIT_DUP;
		struct string_list_item *item;
		const char *engine = engine_of(q->v, task_def(task)), *a;
		int k;

		strbuf_addf(&sb, "  - task: %s\n    contract: %s\n    role: %s\n", dq(task->string),
			    dq(xstrndup(task->string, strchr(task->string, ':') - task->string)), dq(val(r, ROLE)));
		if (*r->id == 'D')
			strbuf_addf(&sb, "    consumer: %s\n", dq(shows(r, CONSUMER, q->v, 0)));
		strbuf_addf(&sb, "    provider: %s\n    engine: %s\n    body: |\n",
			    dq(shows(r, PROVIDER, q->v, was_current(r))), dq(engine));
		string_list_split(&body, yget(kv, "body"), "\n", -1);
		for_each_string_list_item(item, &body)
			if (*item->string || item != body.items + body.nr - 1)
				strbuf_addf(&sb, "      %s\n", item->string);
		if (strcmp(engine, "exec"))
			continue;
		for (k = 0; (a = yget(kv, xstrfmt("command.%d", k))); k++)
			strbuf_addf(&sb, "%s%s", k ? ", " : "    command: [", dq(a));
		for (k = 0; (a = yget(kv, xstrfmt("success.%d", k))) || !k; k++)
			strbuf_addf(&sb, "%s%s", k ? ", " : "]\n    success: [", a ? a : "0");
		strbuf_addstr(&sb, "]\n");
	}
	mkdir(xstrfmt("%s/ait", gitdir), 0777);
	write_file_buf(req, sb.buf, sb.len);
	if (!read_text(&old, res) && !yaml_parse(old.buf, &kv, res, &err) && yget(&kv, "input") &&
	    !strcmp(yget(&kv, "input"), in))
		return;
	strbuf_reset(&sb);
	strbuf_addf(&sb, "input: %s\n# for each task: outcome is pass, fail or unknown; a pass needs a basis\n"
		    "# (what you ran or read, and what it showed); reason says why, when fail or unknown\nresults:\n", in);
	for_each_string_list_item(task, &q->tasks)
		strbuf_addf(&sb, "  - task: %s\n    outcome:\n    basis:\n    reason:\n", task->string);
	write_file_buf(res, sb.buf, sb.len);
}

/*
 * spec 8.4: the results answer this state (0 when they do not), every task once,
 * each pass with a basis; FAILED and FAILING collect the tasks that did not pass.
 * TEXT gets the file, for the commit message.
 */
static int read_results(struct request *q, const char *input, struct strbuf *text, struct strbuf *failed,
			struct string_list *failing, int *unknown)
{
	static const char *const outcomes[] = { "pass", "fail", "unknown", NULL };
	static const char *const schema[] = { "input s", "results l", "results.N m", "results.N.task s",
					      "results.N.outcome s", "results.N.basis s", "results.N.reason s", NULL };
	struct string_list kv = STRING_LIST_INIT_DUP, seen = STRING_LIST_INIT_DUP;
	char *hint = xstrfmt("fix '%s': every task needs an outcome (pass, fail or unknown), and a pass needs a basis", q->result);
	const char *t;
	int n;

	text_or_die(text, q->result);
	yaml(text->buf, &kv, q->result);
	if (!yget(&kv, "input"))
		reject(128, hint, "bad format: '%s': no 'input'", q->result);
	if (strcmp(yget(&kv, "input"), input))
		return 0;
	yaml_check(&kv, schema, q->result);
	for (n = 0; (t = yget(&kv, xstrfmt("results.%d.task", n))); n++) {
		const char *outcome = yget(&kv, xstrfmt("results.%d.outcome", n));
		const char *basis = yget(&kv, xstrfmt("results.%d.basis", n));

		if (!string_list_has_string(&q->tasks, t) || unsorted_string_list_has_string(&seen, t) || !outcome ||
		    !one_of(outcome, outcomes) ||
		    (!strcmp(outcome, "pass") && (!basis || !basis[strspn(basis, " \n")])))
			reject(128, hint, "bad format: '%s': task '%s'", q->result, t);
		string_list_append(&seen, t);
		if (!strcmp(outcome, "pass"))
			continue;
		*unknown += !strcmp(outcome, "unknown");
		strbuf_addf(failed, "%s'%s'", failed->len ? ", " : "", t);
		string_list_append(failing, t)->util = string_list_lookup(&q->tasks, t)->util;
	}
	if (seen.nr != q->tasks.nr)
		reject(128, hint, "bad format: '%s': %d of %d tasks", q->result, (int)seen.nr, (int)q->tasks.nr);
	return 1;
}

/* H4 (6.7a): my new link is not the only future offer on its demand */
static void also_undertaken(struct request *q, struct record *l)
{
	struct record *d, *o;
	size_t i;

	if (*l->id != 'u' || !in_state(l, "undertaken") || (l->old && in_state(l->old, "undertaken")))
		return;
	d = link_end(&q->s, l, 0);
	o = link_end(&q->s, l, 1);

	for (i = 0; i < q->s.nr; i++) {
		struct record *m = q->s.r[i], *o2;

		if (*m->id != 'u' || m == l || !in_state(m, "undertaken") || link_end(&q->s, m, 0) != d)
			continue;
		o2 = link_end(&q->s, m, 1);
		if (!in_state(o2, "future") || !strcmp(val(o2, CREATOR), val(o, CREATOR)))
			continue;
		warning("'%s' is also undertaken by '%s'", d->id, o2->id);
		advise("if your work can go on without it, run 'ait future cancel %s -m <why>' (or 'ait release %s %s -m <why>' "
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
				advise("if it is test material, add it to 'inputs' in '%s'", def);
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
				reject(1, j ? xstrfmt("undertake it with a future offer: 'ait future offer %s %s as o undertake %s o -m <why>'",
						      scope_args(d, PROVIDER), def_paths(d), d->id) : NULL,
				       "not allowed: '%s' is %s", end->id, val(end, STATE));
		}
		if (!allowed(q, r, from, to))
			reject(1, was_current(r) && *r->id != 'u' ?
			       xstrfmt("to change '%s' without a check, reopen it: 'ait future amend %s -m <why>'", r->id, r->id) : NULL,
			       "not allowed: '%s' cannot go from %s to %s", r->id, from, to);
	}
}

/* spec 1.5b: the contracts this request checks, decided before anything is pinned again */
static void check_set(struct request *q)
{
	struct string_list_item *item;
	size_t i;

	for (i = 0; i < q->s.nr; i++) {
		struct record *r = q->s.r[i];

		if (is_current(r) && !was_current(r)) {
			repin(r, CONSUMER, q->v, 1);
			repin(r, PROVIDER, q->v, 1);
		}
		if (is_current(r) && (!was_current(r) || r->recheck || touches(r->old, q->ht, q->v) ||
				      carried(q, r) ||
				      (!unsorted_string_list_has_string(&q->scoped, r->id) && strcmp(pin_lines(r), pin_lines(r->old)))))
			string_list_append(&q->conf, r->id)->util = r;
	}
	string_list_sort(&q->conf);
	for_each_string_list_item(item, &q->scoped)
		if (!unsorted_string_list_has_string(&q->conf, item->string))
			reject(128, NULL, "bad scope: '%s' is not checked by this command", item->string);
}

/* spec 7.7.3-4: the code to check is there; abstract contracts need a scope first */
static void code_present(struct request *q)
{
	struct string_list_item *item, *sc;

	for_each_string_list_item(item, &q->conf) {
		for_each_string_list_item(sc, &((struct record *)item->util)->f[PROVIDER]) {
			char *file = scope_path(sc->string);
			int written = !access(xstrfmt("%s/%s", top, file), F_OK), new = starts_with(sc->string, "new:");

			if (ends_with(file, "/"))
				reject(128, xstrfmt("give it the scope you implemented: 'ait fulfill %s=<scope> -m <why>'", item->string),
				       "bad scope: '%s' becomes current without a scope", item->string);
			if (blob_at(q->v, file))
				continue;
			if (!written && new)
				add_hint(q, xstrfmt("write '%s'", file));
			if (written || new)
				add_hint(q, xstrfmt("stage '%s', then rerun this command", file));
			else
				add_hint(q, xstrfmt("if it was moved, rerun with '%s=<new scope>'", item->string));
			if (!unsorted_string_list_has_string(&q->missing, item->string))
				string_list_append(&q->missing, item->string)->util = item->util;
		}
	}
	if (!q->missing.nr)
		return;
	next_hints(q, &q->missing);
	block(q, 1, "code-required", hints(q), xstrfmt("no code under the contract's scope: '%s'", q->missing.items[0].string));
}

/* spec 8.1 and 8.3: a task per definition of the check set, and the input that names this state */
static char *check_input(struct request *q)
{
	struct string_list paths = STRING_LIST_INIT_DUP;
	struct string_list_item *item, *f;
	struct strbuf in = STRBUF_INIT;

	for_each_string_list_item(item, &q->conf) {
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
		for_each_string_list_item(item, &q->conf)
			for_each_string_list_item(sc, &((struct record *)item->util)->f[PROVIDER])
				printf("provider %s %s\n", item->string, show(sc->string, q->v, was_current(item->util)));
		for_each_string_list_item(sc, &q->mixed)
			printf("mixed %s\n", sc->string);
		if (q->invalid.code)
			printf("invalid %s %s\n", q->invalid.word, q->invalid.msg);
		for_each_string_list_item(item, &q->missing)
			printf("code-required %s\n", item->string);
		for_each_string_list_item(item, &q->conf)
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

/* spec 7.7.4 and 6.5: pin the check set at the judged tree, add what changed to it and commit */
static void commit_request(struct request *q, struct strbuf *results)
{
	struct string_list changed = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct strbuf info = STRBUF_INIT;
	char rid[16], *tree;
	size_t i;

	newid(rid, 'R');
	for_each_string_list_item(item, &q->conf)
		repin(item->util, PROVIDER, q->v, 0);
	for (i = 0; i < q->s.nr; i++) {
		struct record *r = q->s.r[i];

		if (unchanged(r))
			continue;
		put(r, PREV, r->old ? blob_at(q->ht, xstrfmt(".ait/%s", r->id)) : "-");
		strbuf_addf(&info, "100644 %s\t.ait/%s\n", line1(git_run(record_text(r, 1), NULL, NULL, "hash-object -w --stdin")),
			    r->id);
		string_list_append(&changed, r->id)->util = r;
		if (!r->old && r->seq)
			rename_tasks(results, r->seq, r->id);
	}
	tree = tree_with(q->v->oid, info.buf);
	if (!strcmp(tree, q->ht->oid) && !q->conf.nr) {
		puts("noop");
		return;
	}
	if (results->len)
		strbuf_addf(&q->msg, "\n%s", results->buf);
	strbuf_addf(&q->msg, "\nAit-Request: %s\nAit-Agent: %s\nAit-Session: %s\n", rid, agent, sid);
	for_each_string_list_item(item, &q->conf)
		strbuf_addf(&q->msg, "Ait-Check: %s %s %s\n", item->string, q->force ? "forced" : "reported", pins(item->util));
	if (publish(tree, q->msg.buf))
		reject(1, "rerun this command", "HEAD moved while running; nothing changed");
	printf("request %s\n", rid);
	for_each_string_list_item(item, &changed) {
		struct record *r = item->util;

		if (!r->old && *r->id != 'u')
			printf("new %s%s%s\n", r->id, r->alias ? " " : "", r->alias ? r->alias : "");
		else if (*r->id == 'u')
			printf("%s %.13s %s\n", val(r, STATE), r->id + 10, r->id + 24);
		else if (q->gate)
			printf("changed %s\n", r->id);
		else if (is_current(r) && in_state(r->old, "future"))
			printf("fulfilled %s%s\n", r->id, via(q, r));
	}
	for_each_string_list_item(item, &q->conf)
		printf("check %s %s\n", item->string, q->force ? "forced" : "reported");
	for_each_string_list_item(item, &changed)
		also_undertaken(q, item->util);
}

static int finish(struct request *q)
{
	char *req = xstrfmt("%s/ait/check-request.yaml", gitdir), *res = xstrfmt("%s/ait/check-result.yaml", gitdir);
	struct string_list failing = STRING_LIST_INIT_DUP;
	struct strbuf failed = STRBUF_INIT, results = STRBUF_INIT;
	char *input = NULL;
	int unknown = 0;

	transitions(q);
	check_set(q);
	if (!q->gate && !q->fulfill && !q->conf.nr && !q->first.code) {
		/* 7.7a.1: nothing to check is a plain commit, without validate, session or trailers */
		if (!strcmp(q->v->oid, q->ht->oid))
			puts("noop");
		else if (publish(q->v->oid, q->msg.buf))
			reject(1, "rerun this command", "HEAD moved while running; nothing changed");
		return 0;
	}
	if (!q->gate && !q->fulfill)
		session(1);
	if (q->gate)
		validate(&q->s, q->v);
	code_present(q);
	if (!q->gate && !q->missing.nr && check_state(&q->s, q->v, &q->invalid))
		block(q, q->invalid.code, q->invalid.word, q->invalid.hint, q->invalid.msg);
	if (!q->missing.nr && !q->force)
		input = check_input(q);
	if (input && q->tasks.nr && q->result && !q->first.code &&
	    !read_results(q, input, &results, &failed, &failing, &unknown))
		strbuf_reset(&results);
	if (q->first.code || (q->tasks.nr && !q->force && !results.len)) {
		if (input && q->tasks.nr)
			write_check_files(q, input, req, res);
		report(q, req, res, input && q->tasks.nr);
		if (q->first.code)
			reject(q->first.code, q->first.hint, "%s", q->first.msg);
		add_hint(q, xstrfmt("check each task, fill '%s', and rerun with -r '%s'", res, res));
		add_hint(q, "if it is trivial, add --force instead (recorded as forced)");
		next_hints(q, &q->conf);
		reject(1, hints(q), "check results required: %d task(s)", (int)q->tasks.nr);
	}
	if (failed.len) {
		if (unknown) {
			add_hint(q, xstrfmt("check it where its environment is available (see '%s') and rerun with -r '%s'", req, res));
			add_hint(q, "if it is trivial, add --force (recorded as forced)");
		}
		if (unknown < (int)failing.nr)
			fix_hints(q, &failing);
		next_hints(q, &failing);
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
		int abstract = 0, inside = 0;

		for_each_string_list_item(old, &r->f[PROVIDER]) {
			char *path = scope_path(old->string);

			abstract |= ends_with(path, "/");
			inside |= !strcmp(path, "/") || (ends_with(path, "/") && starts_with(item->string, path));
		}
		if (abstract && !inside)
			reject(128, NULL, "bad scope: '%s' is outside the scope of '%s'", item->string, r->id);
	}
	scope_list(&vals, list, q->v, 0, 0);
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
		struct record *l = q->s.r[i], *o;
		struct string_list_item *d;

		if (*l->id != 'u' || !in_state(l, "undertaken"))
			continue;
		o = link_end(&q->s, l, 1);
		if (!o || !unsorted_string_list_has_string(ids, o->id) || !in_state(o, "future"))
			continue;
		d = string_list_insert(&q->carried, link_end(&q->s, l, 0)->id);
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
	message(&q, q.fulfill ? "[FULFILL] " : NULL);
	/* spec 7.7.1: IDs once each; aliases live only inside one helper request */
	for (k = 0; k < q.args.nr; k++) {
		const char *a = q.args.v[k], *eq = strchr(a, '=');
		char *id = eq ? xstrndup(a, eq - a) : xstrdup(a);

		if (*a == '-' || unsorted_string_list_has_string(&ids, id) || (!q.fulfill && !eq) || islower(*id))
			reject(129, islower(*id) ? "aliases are local to one command; use the ID printed on its 'new' line" : NULL,
			       "bad argument '%s'", a);
		string_list_append(&ids, id)->util = eq ? xstrdup(eq + 1) : NULL;
	}
	if (q.fulfill)
		session(1);
	load(&q);
	q.v = tree_load(line1(git("write-tree")));
	for_each_string_list_item(item, &ids)
		if (!state_get(&q.s, item->string) || *item->string == 'u')
			reject(128, NULL, "no such contract: '%s'", item->string);
	if (q.fulfill)
		carry(&q, &ids);
	/* the selected targets: plain IDs, and ID=SCOPES of a future contract that no target offer carries */
	for_each_string_list_item(item, &ids) {
		struct record *r = state_get(&q.s, item->string);

		if (q.fulfill && (!item->util || (in_state(r, "future") && !carried(&q, r))))
			select_promise(&q, r);
	}
	for_each_string_list_item(item, &q.carried)
		if (in_state(state_get(&q.s, item->string), "future"))
			put(state_get(&q.s, item->string), STATE, "current");
	for_each_string_list_item(item, &ids)
		if (item->util) {
			string_list_append(&q.scoped, item->string);
			given_scopes(&q, state_get(&q.s, item->string), item->util);
		}
	if (q.fulfill && !q.selected.nr)
		reject(129, NULL, "nothing to fulfill: give the IDs of future contracts");
	tree_diff(q.ht, q.v, &paths);
	for_each_string_list_item(item, &paths)
		if (contract_path(&q.s, item->string)) {
			string_list_append(&q.mixed, item->string);
			block(&q, 1, "mixed", xstrfmt("unstage the spec files ('%s'), then rerun this command", item->string),
			      xstrfmt("contract changes and code changes are mixed: '%s'", item->string));
		}
	return finish(&q);
}

/* spec 7.3.3: the candidate is HEAD plus the given definitions and what they declare, from the work tree */
static struct tree *candidate(struct request *q)
{
	struct strbuf info = STRBUF_INIT;
	size_t k;

	for (k = 0; k < q->files.nr; k++) {
		const char *p = q->files.items[k].string;
		struct strbuf text = STRBUF_INIT;
		struct string_list kv = STRING_LIST_INIT_DUP;
		struct string_list_item *item;
		const char *staged;
		struct stat st;
		char *oid;

		if (lstat(xstrfmt("%s/%s", top, p), &st) || !S_ISREG(st.st_mode))
			reject(1, xstrfmt("create '%s' or stop declaring it", p), "spec files do not match the contract: missing '%s'", p);
		oid = line1(git("hash-object -w -- %s", p));
		staged = git("ls-files -s -- %s", p);
		if (*staged && !strstr(staged, oid) && (!blob_at(q->ht, p) || !strstr(staged, blob_at(q->ht, p))))
			reject(1, xstrfmt("stage all of '%s' or none of it", p), "spec files do not match the contract: '%s' is partly staged", p);
		strbuf_addf(&info, "%s %s\t%s\n", st.st_mode & S_IXUSR ? "100755" : "100644", oid, p);
		if (!q->files.items[k].util)
			continue;
		text_or_die(&text, xstrfmt("%s/%s", top, p));
		yaml(text.buf, &kv, p);
		for_each_string_list_item(item, &kv)
			if (starts_with(item->string, "inputs.") && !unsorted_string_list_has_string(&q->files, yget(&kv, item->string)))
				string_list_append(&q->files, yget(&kv, item->string));
	}
	return tree_load(tree_with(q->ht->oid, info.buf));
}

/* spec 7.3: clauses apply in order to the records loaded from HEAD */
int cmd_request(int argc, const char **argv)
{
	static const char *const direct[] = { "new", "add", "commit", "push", "pull", NULL };
	struct request q = REQUEST_INIT;
	const char **v;
	int n, i;
	size_t k;

	q.gate = 1;
	parse_options(&q, argc, argv);
	v = q.args.v;
	n = q.args.nr;
	i = !strcmp(v[0], "contract") || !strcmp(v[0], "ct");
	if (i && n > 1 && one_of(v[1], direct))
		unsupported(xstrfmt("contract %s", v[1]));
	message(&q, "[CONTRACT] ");
	session(1);
	load(&q);
	strbuf_addstr(&q.cmdline, "ait");
	for (k = 0; k < q.args.nr; k++) {
		strbuf_addch(&q.cmdline, ' ');
		sq_quote_buf_pretty(&q.cmdline, v[k]);
	}
	while (i < n) {
		int future = !strcmp(v[i], "future");
		const char *w;
		struct record *r, *o;

		if (future && i + 1 == n)
			unsupported("future");
		w = v[i + future];
		i += future + 1;
		if (future && !strcmp(w, "drop"))
			unsupported("future drop");
		if (!strcmp(w, "demand") || !strcmp(w, "offer")) {
			new_contract(&q, w, future, &i);
		} else if (!strcmp(w, "amend")) {
			r = resolve(&q, arg(&q, &i));
			if (*r->id == 'u' || !is_live(r))
				reject(1, NULL, "not allowed: '%s' is not a future or current contract", r->id);
			if (future)
				put(r, STATE, "future");
			r->recheck = is_current(r);
			if (i < n && !is_word(v[i])) {
				const char *a = *r->id == 'D' ? arg(&q, &i) : "-", *b = arg(&q, &i);
				const char *f = arg(&q, &i);

				r->recheck &= !strcmp(a, "-") && !strcmp(b, "-") && !strcmp(f, "-");
				scopes(&q, r, CONSUMER, a);
				scopes(&q, r, PROVIDER, b);
				defs(&q, r, f);
			}
		} else if (future && !strcmp(w, "cancel")) {
			put(resolve(&q, arg(&q, &i)), STATE, "cancelled");
		} else if (!future && (!strcmp(w, "undertake") || !strcmp(w, "release"))) {
			char *id;

			r = resolve(&q, arg(&q, &i));
			o = resolve(&q, arg(&q, &i));
			if (*r->id != 'D' || *o->id != 'O')
				reject(128, NULL, "no such contract: '%s' must be a demand and '%s' an offer", r->id, o->id);
			id = xstrfmt("undertake/%s-%s", r->id, o->id);
			r = state_get(&q.s, id);
			if (!r && *w == 'u') {
				r = state_add(&q.s, id);
				put(r, PREV, "-");
			}
			if (r)
				put(r, STATE, *w == 'u' ? "undertaken" : "released");
		} else {
			reject(129, NULL, "unknown clause '%s'", w);
		}
	}
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
