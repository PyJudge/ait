/*
 * Requests (spec 1.7): the helpers (7.3), fulfill (7.7) and commit (7.7a).
 * Each one edits the loaded records in place and names the tree the request
 * is judged in; finish() is the one pipeline after that: transitions (2.3),
 * validate (5.9), the check set (1.5b), results (8), pins and publishing (5.5).
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
	struct strbuf msg;
	struct string_list files;	/* spec files given to the helpers */
	struct string_list scoped;	/* IDs given with =SCOPES */
	struct string_list selected;	/* the promises fulfill was asked to keep */
	struct strbuf mixed;		/* the 7.7.6 "mixed PATH" lines */
	struct verdict first;		/* the first condition that blocks fulfill or commit (5.13) */
	struct strbuf cmdline;		/* the helper request without its options, for hints */
};


/* spec 2.3: the allowed transitions; 'x' is a demand or an offer, 'u' a link */
static const char *gate_moves = " x-:future x-:current xfuture:future xfuture:cancelled xcurrent:current"
				" xcurrent:future u-:undertaken ureleased:undertaken uundertaken:released ";
static const char *fulfill_moves = " xfuture:current xcurrent:current ";

/* spec 5.2 */
void precondition(int allow_unborn)
{
	static const char *ops[] = { "MERGE_HEAD", "CHERRY_PICK_HEAD", "REVERT_HEAD", NULL };
	static const char *dirs[] = { "rebase-merge", "rebase-apply", "sequencer/todo", NULL };
	int i, rc;

	for (i = 0; ops[i]; i++) {
		git_run(NULL, NULL, &rc, "rev-parse -q --verify %s", ops[i]);
		if (!rc || !access(xstrfmt("%s/%s", gitdir, dirs[i]), F_OK))
			reject(128, "finish or abort it as 'git status' says, then rerun this command",
			       "cannot run now: a git operation is in progress");
	}
	if (!headref)
		reject(128, "run this command with HEAD on a branch", "cannot run now: HEAD is detached");
	if (!head && !allow_unborn)
		reject(128, "run 'ait init' first", "cannot run now: the branch has no commit");
	if (*git("status --porcelain --untracked-files=all -- .ait"))
		reject(1, "contracts change only through ait; restore '.ait/' to HEAD's version and remove the new files there",
		       "uncommitted changes in .ait/");
}

/*
 * spec 5.5 through git commit: PATHS come from the work tree; ONLY commits
 * just them and leaves whatever else is staged, otherwise the whole index.
 * ait's own hooks are skipped (11.2); if git refuses, .ait/ goes back to HEAD.
 */
char *publish(struct string_list *paths, int only, const char *msg)
{
	struct strvec add = STRVEC_INIT, commit = STRVEC_INIT;
	size_t i;
	int rc;

	strvec_pushl(&add, "add", "--", NULL);
	strvec_pushl(&commit, "commit", "--no-verify", "--cleanup=verbatim", "-q", "-F", "-", NULL);
	/* a check with no changed file is still a commit: its record is the Ait-Check trailer (6.5) */
	if (only && !paths->nr)
		strvec_pushl(&commit, "--allow-empty", "--only", "--", ".ait/init", NULL);
	else if (only)
		strvec_pushl(&commit, "--only", "--", NULL);
	for (i = 0; i < paths->nr; i++) {
		strvec_push(&add, paths->items[i].string);
		if (only)
			strvec_push(&commit, paths->items[i].string);
	}
	git_args(&add, NULL, NULL, &rc);
	setenv("AIT_EXPECT", xstrfmt("%s %s", head ? head : "0000000000000000000000000000000000000000", headref), 1);
	if (!rc)
		git_args(&commit, msg, NULL, &rc);
	unsetenv("AIT_EXPECT");
	if (rc) {
		char *why = xstrdup(git_error);
		int moved = strcmp(head ? head : "", line1(git_run(NULL, NULL, &rc, "rev-parse -q --verify HEAD")));

		git(head ? "reset -q -- .ait" : "rm -r -q --cached -- .ait");
		if (head)
			git("checkout -q HEAD -- .ait");
		git("clean -f -q -- .ait");
		if (moved)
			reject(1, "rerun this command", "HEAD moved while running; nothing changed");
		reject(128, NULL, "git failed: %s", why);
	}
	return line1(git("rev-parse HEAD"));
}

static void begin(struct request *q)
{
	precondition(0);
	q->ht = tree_load(head);
	state_load(&q->s, q->ht);
	q->files.strdup_strings = q->scoped.strdup_strings = q->selected.strdup_strings = 1;
}

/* -m, -F, -r and --force anywhere among the arguments (spec 5.3, 7.3.1) */
static int option(struct request *q, int argc, const char **argv, int *i)
{
	static int from_file, from_m;
	const char *a = argv[*i];

	if (!strcmp(a, "--force") && !q->force && !q->result)
		return q->force = 1;
	if (*i + 1 >= argc || (strcmp(a, "-m") && strcmp(a, "-F") && strcmp(a, "-r") && strcmp(a, "--result")))
		return 0;
	if (!strcmp(a, "-m") && !from_file) {
		strbuf_addf(&q->msg, "%s%s", from_m++ ? "\n\n" : "", argv[++*i]);
	} else if (!strcmp(a, "-F") && !from_file++ && !from_m) {
		if (strbuf_read_file(&q->msg, argv[++*i], 0) < 0 && strcmp(argv[*i], "-"))
			reject(128, NULL, "could not read '%s'", argv[*i]);
		if (!strcmp(argv[*i], "-"))
			strbuf_read(&q->msg, 0, 0);
	} else if (a[1] != 'm' && a[1] != 'F' && !q->result && !q->force) {
		q->result = argv[++*i];
		if (access(q->result, R_OK))
			reject(128, "rerun without -r to get the check-result path", "could not read '%s'", q->result);
	} else {
		reject(129, NULL, "option '%s' conflicts with another option", a);
	}
	return 1;
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

static const char *arg(int argc, const char **argv, int *i)
{
	if (*i >= argc || (argv[*i][0] == '-' && argv[*i][1]))
		reject(129, NULL, "missing argument after '%s'", argv[*i - 1]);
	return argv[(*i)++];
}

static int is_word(const char *a)
{
	return strstr(" demand offer future amend undertake release as -m -F -r --result --force ",
		      xstrfmt(" %s ", a)) != NULL;
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

static void set1(struct record *r, const char *key, const char *val)
{
	struct string_list v = STRING_LIST_INIT_DUP;

	string_list_append(&v, val);
	rset(r, key, &v);
}

/* a scope list "A,B" pinned at HEAD (spec 3.2a, 3.3); "-" keeps the old one */
static void scopes(struct request *q, struct record *r, const char *key, const char *list)
{
	struct string_list parts = STRING_LIST_INIT_DUP, vals = STRING_LIST_INIT_DUP;
	int future = !strcmp(rget(r, "state"), "future");
	size_t i;

	if (!strcmp(list, "-"))
		return;
	string_list_split(&parts, list, ",", -1);
	for (i = 0; i < parts.nr; i++) {
		char *v = pin(parts.items[i].string, q->ht, future || *key == 'c', future && *key == 'p');

		if (unsorted_string_list_has_string(&vals, v))
			reject(128, NULL, "bad scope: '%s' is given twice", parts.items[i].string);
		string_list_append(&vals, v);
	}
	rset(r, key, &vals);
}

static void defs(struct request *q, struct record *r, const char *list)
{
	struct string_list parts = STRING_LIST_INIT_DUP, vals = STRING_LIST_INIT_DUP;
	size_t i;

	if (!strcmp(list, "-"))
		return;
	string_list_split(&parts, list, ",", -1);
	for (i = 0; i < parts.nr; i++) {
		if (!path_ok(parts.items[i].string))
			reject(128, NULL, "bad scope: '%s'", parts.items[i].string);
		string_list_append(&q->files, parts.items[i].string);
		string_list_append_nodup(&vals, xstrfmt("%s - - def", parts.items[i].string));
	}
	rset(r, "file", &vals);
}

static void new_contract(struct request *q, const char *role, int future, int argc, const char **argv, int *i)
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
	set1(r, "role", role);
	set1(r, "state", future ? "future" : "current");
	if (*role == 'd')
		scopes(q, r, "consumer", arg(argc, argv, i));
	scopes(q, r, "provider", arg(argc, argv, i));
	defs(q, r, arg(argc, argv, i));
	set1(r, "creator", xstrfmt("%s %s", agent, sid));
	set1(r, "prev", "-");
	if (*i >= argc || strcmp(argv[*i], "as"))
		return;
	++*i;
	alias = arg(argc, argv, i);
	if (!islower(*alias) || strlen(alias) > 32 || alias[strspn(alias, "abcdefghijklmnopqrstuvwxyz0123456789_")] ||
	    is_word(alias) || strstr(" drop cancel contract ct fulfill ", xstrfmt(" %s ", alias)))
		reject(129, NULL, "bad alias '%s'", alias);
	for (k = 0; k < q->s.nr; k++)
		if (q->s.r[k]->alias && !strcmp(q->s.r[k]->alias, alias))
			reject(129, NULL, "alias '%s' is used twice in one request", alias);
	r->alias = alias;
}

static void replace(struct strbuf *sb, const char *from, const char *to)
{
	struct strbuf out = STRBUF_INIT;
	const char *p = sb->buf, *hit;

	while ((hit = strstr(p, from))) {
		strbuf_add(&out, p, hit - p);
		strbuf_addstr(&out, to);
		p = hit + strlen(from);
	}
	strbuf_addstr(&out, p);
	*sb = out;
}

/* spec 7.7.4: a contract that becomes current turns new:PATH into PATH once the file exists */
static void resolve_new(struct record *r, const char *key, struct tree *t)
{
	struct string_list v = STRING_LIST_INIT_DUP;
	size_t i;

	rvals(r, key, &v);
	for (i = 0; i < v.nr; i++)
		if (starts_with(v.items[i].string, "new:") && blob_at(t, v.items[i].string + 4))
			v.items[i].string = xstrfmt("%s %s", v.items[i].string + 4, blob_at(t, v.items[i].string + 4));
	rset(r, key, &v);
}

/* rule 5: D is checked with the offer that undertakes it when that offer becomes current */
static int undertaken_by_new_current(struct request *q, struct record *d)
{
	size_t i;

	for (i = 0; i < q->s.nr; i++) {
		struct record *l = q->s.r[i], *o;

		if (*l->id != 'u' || strcmp(rget(l, "state"), "undertaken") || strncmp(l->id + 10, d->id, 13))
			continue;
		o = state_get(&q->s, l->id + 24);
		if (is_current(o) && o->old && !strcmp(rget(o->old, "state"), "future"))
			return 1;
	}
	return 0;
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

static char *task_contract(struct record *r)
{
	return r->old ? r->id : xstrfmt("+%d", r->seq);
}

/* spec 8.2 and 4.7: the check request and the result template for TASKS */
static void write_check_files(struct request *q, struct string_list *tasks, const char *in,
			      const char *req, const char *res)
{
	struct strbuf sb = STRBUF_INIT, old = STRBUF_INIT;
	size_t i, k;

	strbuf_addf(&sb, "input: %s\ntree: %s\ntasks:\n", dq(in), dq(q->v->oid));
	for (i = 0; i < tasks->nr; i++) {
		struct record *r = tasks->items[i].util;
		struct string_list kv = STRING_LIST_INIT_DUP, body = STRING_LIST_INIT_DUP;
		const char *engine;

		def_load(q->v, strchr(tasks->items[i].string, ':') + 1, &kv);
		engine = yget(&kv, "engine") && *yget(&kv, "engine") ? "exec" : "prompt";
		strbuf_addf(&sb, "  - task: %s\n    contract: %s\n    role: %s\n", dq(tasks->items[i].string),
			    dq(task_contract(r)), dq(rget(r, "role")));
		if (*r->id == 'D')
			strbuf_addf(&sb, "    consumer: %s\n", dq(shows(r, "consumer", q->v, 0)));
		strbuf_addf(&sb, "    provider: %s\n    engine: %s\n    body: |\n", dq(shows(r, "provider", q->v, was_current(r))),
			    dq(engine));
		string_list_split(&body, yget(&kv, "body"), "\n", -1);
		for (k = 0; k < body.nr; k++)
			if (k + 1 < body.nr || *body.items[k].string)
				strbuf_addf(&sb, "      %s\n", body.items[k].string);
		for (k = 0; yget(&kv, xstrfmt("command.%d", (int)k)); k++)
			strbuf_addf(&sb, "%s%s", k ? ", " : "    command: [", dq(yget(&kv, xstrfmt("command.%d", (int)k))));
		for (k = 0; *engine == 'e' && (yget(&kv, xstrfmt("success.%d", (int)k)) || !k); k++)
			strbuf_addf(&sb, "%s%s", k ? ", " : "]\n    success: [",
				    yget(&kv, xstrfmt("success.%d", (int)k)) ? yget(&kv, xstrfmt("success.%d", (int)k)) : "0");
		strbuf_addstr(&sb, *engine == 'e' ? "]\n" : "");
	}
	mkdir(xstrfmt("%s/ait", gitdir), 0777);
	write_file_buf(req, sb.buf, sb.len);
	if (strbuf_read_file(&old, res, 0) >= 0 && starts_with(old.buf, xstrfmt("input: %s\n", in)))
		return;
	strbuf_reset(&sb);
	strbuf_addf(&sb, "input: %s\n# for each task: outcome is pass, fail or unknown; a pass needs a basis\n"
		    "# (what you ran or read, and what it showed); reason says why, when fail or unknown\nresults:\n", in);
	for (i = 0; i < tasks->nr; i++)
		strbuf_addf(&sb, "  - task: %s\n    outcome:\n    basis:\n    reason:\n", tasks->items[i].string);
	write_file_buf(res, sb.buf, sb.len);
}

/*
 * spec 8.4: the results answer this state (0 when they do not), every task once,
 * each pass with a basis; FAILED and FAILING collect the tasks that did not pass
 */
static int read_results(struct request *q, struct string_list *tasks, const char *input, struct strbuf *failed,
			struct string_list *failing, int *unknown)
{
	struct string_list kv = STRING_LIST_INIT_DUP, seen = STRING_LIST_INIT_DUP;
	struct strbuf text = STRBUF_INIT;
	char *hint = xstrfmt("fix '%s': every task needs an outcome (pass, fail or unknown), and a pass needs a basis", q->result);
	const char *t;
	size_t i;
	int n;

	strbuf_read_file(&text, q->result, 0);
	yaml(text.buf, &kv, q->result);
	if (!yget(&kv, "input"))
		reject(128, hint, "bad format: '%s': no 'input'", q->result);
	if (strcmp(yget(&kv, "input"), input))
		return 0;
	for (i = 0; i < kv.nr; i++) {
		const char *k = kv.items[i].string, *f = strchr(k, '.') ? strchr(strchr(k, '.') + 1, '.') : NULL;

		if (strcmp(k, "input") && strcmp(k, "results") && (!starts_with(k, "results.") || !f ||
		    !strstr(" task outcome basis reason ", xstrfmt(" %s ", f + 1))))
			reject(128, hint, "bad format: '%s': unknown key '%s'", q->result, k);
	}
	for (n = 0; (t = yget(&kv, xstrfmt("results.%d.task", n))); n++) {
		const char *outcome = yget(&kv, xstrfmt("results.%d.outcome", n));
		const char *basis = yget(&kv, xstrfmt("results.%d.basis", n));

		if (!string_list_has_string(tasks, t) || unsorted_string_list_has_string(&seen, t) || !outcome ||
		    !strstr(" pass fail unknown ", xstrfmt(" %s ", outcome)) ||
		    (!strcmp(outcome, "pass") && (!basis || !basis[strspn(basis, " \n")])))
			reject(128, hint, "bad format: '%s': task '%s'", q->result, t);
		string_list_append(&seen, t);
		if (!strcmp(outcome, "pass"))
			continue;
		*unknown += !strcmp(outcome, "unknown");
		strbuf_addf(failed, "%s'%s'", failed->len ? ", " : "", t);
		string_list_append(failing, t)->util = string_list_lookup(tasks, t)->util;
	}
	if (seen.nr != tasks->nr)
		reject(128, hint, "bad format: '%s': %d of %d tasks", q->result, (int)seen.nr, (int)tasks->nr);
	return 1;
}

static int unchanged(struct record *r)
{
	return r->old ? !strcmp(record_text(r, 0), record_text(r->old, 0)) :
	       *r->id == 'u' && !strcmp(rget(r, "state"), "released");
}

static struct record *link_end(struct request *q, struct record *l, int offer)
{
	return state_get(&q->s, offer ? l->id + 24 : xstrndup(l->id + 10, 13));
}

/* H4 (6.7a): my new link is not the only future offer on its demand */
static void also_undertaken(struct request *q, struct record *l)
{
	struct record *d, *o;
	size_t i;

	if (*l->id != 'u' || strcmp(rget(l, "state"), "undertaken") || (l->old && !strcmp(rget(l->old, "state"), "undertaken")))
		return;
	d = link_end(q, l, 0);
	o = link_end(q, l, 1);
	for (i = 0; i < q->s.nr; i++) {
		struct record *m = q->s.r[i], *o2;

		if (*m->id != 'u' || m == l || strcmp(rget(m, "state"), "undertaken") || link_end(q, m, 0) != d)
			continue;
		o2 = link_end(q, m, 1);
		if (strcmp(rget(o2, "state"), "future") || !strcmp(rget(o2, "creator"), rget(o, "creator")))
			continue;
		warning("'%s' is also undertaken by '%s'", d->id, o2->id);
		advise("if your work can go on without it, run 'ait future cancel %s -m <why>' (or 'ait release %s %s -m <why>' "
		       "to keep '%s' for its other conditions)", o->id, d->id, o->id, o->id);
	}
}

/* spec 1.2: a file that an exec command names but the contract does not pin */
static void unpinned(struct request *q, struct record *r)
{
	struct string_list f = STRING_LIST_INIT_DUP;
	size_t i, k;

	rvals(r, "file", &f);
	for (i = 0; i < f.nr; i++) {
		char *def = xstrndup(f.items[i].string, strcspn(f.items[i].string, " "));
		struct string_list kv = STRING_LIST_INIT_DUP;
		const char *a;

		if (!ends_with(f.items[i].string, " def"))
			continue;
		def_load(q->v, def, &kv);
		for (k = 0; (a = yget(&kv, xstrfmt("command.%d", (int)k))); k++) {
			while (starts_with(a, "./"))
				a += 2;
			if (path_ok(a) && (blob_at(q->v, a) || !access(xstrfmt("%s/%s", top, a), F_OK)) &&
			    !strstr(record_text(r, 1), xstrfmt("\nfile %s ", a))) {
				warning("'%s' is not pinned", a);
				advise("if it is test material, add it to 'inputs' in '%s'", def);
			}
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

/* D is undertaken by a future offer among the arguments, so D is fulfilled with it (7.7.2) */
static int carried(struct request *q, struct string_list *ids, struct record *d)
{
	size_t i;

	for (i = 0; i < q->s.nr; i++) {
		struct record *l = q->s.r[i];

		if (*l->id == 'u' && !strcmp(rget(l, "state"), "undertaken") && link_end(q, l, 0) == d &&
		    unsorted_string_list_has_string(ids, link_end(q, l, 1)->id) &&
		    !strcmp(rget(link_end(q, l, 1), "state"), "future"))
			return 1;
	}
	return 0;
}

/* " via O..." for a demand fulfilled with the offers that undertake it (7.7.5) */
static char *via(struct request *q, struct record *d)
{
	struct strbuf sb = STRBUF_INIT;
	size_t i;

	for (i = 0; i < q->s.nr; i++) {
		struct record *l = q->s.r[i], *o;

		if (*l->id != 'u' || strcmp(rget(l, "state"), "undertaken") || link_end(q, l, 0) != d)
			continue;
		o = link_end(q, l, 1);
		if (is_current(o) && !strcmp(rget(o->old, "state"), "future"))
			strbuf_addf(&sb, "%s %s", sb.len ? "" : " via", o->id);
	}
	return strbuf_detach(&sb, NULL);
}

static void add_hint(struct string_list *hints, const char *h)
{
	if (!unsorted_string_list_has_string(hints, h))
		string_list_append(hints, h);
}

/*
 * 5.9a, after the fix: a contract the helper registers as current can be made a
 * promise instead, and one current in HEAD can be reopened (except a demand
 * checked with the offer that undertakes it)
 */
static void next_hints(struct request *q, struct string_list *hints, struct string_list *ids)
{
	size_t i;

	for (i = 0; i < ids->nr; i++) {
		struct record *r = ids->items[i].util;

		if (q->gate && !r->old)
			add_hint(hints, xstrfmt("or make it a promise: 'ait future %s %s%s%s %s -m <why>'", rget(r, "role"),
						*r->id == 'D' ? rjoin(r, "consumer", ",") : "", *r->id == 'D' ? " " : "",
						rjoin(r, "provider", ","), def_paths(r)));
		else if (was_current(r) && !undertaken_by_new_current(q, r))
			add_hint(hints, xstrfmt("to change '%s' without a check, reopen it: 'ait future amend %s -m <why>'",
						r->id, r->id));
	}
}

/* 5.9a, the fix for a failed check: the code, or the spec file that is the provider */
static void fix_hints(struct request *q, struct string_list *hints, struct string_list *ids)
{
	size_t i, j, k;

	for (i = 0; i < ids->nr; i++) {
		struct string_list sc = STRING_LIST_INIT_DUP;

		rvals(ids->items[i].util, "provider", &sc);
		for (j = 0; j < sc.nr; j++) {
			char *path = xstrndup(sc.items[j].string, strcspn(sc.items[j].string, " "));
			struct record *owner = NULL;

			for (k = 0; k < q->s.nr && !owner; k++)
				if (q->s.r[k]->old && is_live(q->s.r[k]->old) &&
				    strstr(record_text(q->s.r[k]->old, 1), xstrfmt("\nfile %s ", path)))
					owner = q->s.r[k];
			if (!owner)
				add_hint(hints, q->gate ? "commit code that meets it first: stage <your code> and run 'ait commit -m <why>', "
					 "then rerun this command" : "make the code meet the failed task(s) and stage it, then rerun "
					 "without -r to get the new tasks");
			else if (q->gate && unsorted_string_list_has_string(&q->files, path))
				add_hint(hints, xstrfmt("fix '%s', then rerun this command", path));
			else
				add_hint(hints, xstrfmt(q->gate ? "fix '%s' and carry it in this request: '%s amend %s %s%s -m <why>'" :
							"fix '%s' and commit it first: '%sait amend %s %s%s -m <why>', then rerun this command",
							path, q->gate ? q->cmdline.buf : "", owner->id, *owner->id == 'D' ? "- - " : "- ",
							def_paths(owner->old)));
		}
	}
}

static char *join_hints(struct string_list *hints)
{
	struct strbuf sb = STRBUF_INIT;
	size_t i;

	for (i = 0; i < hints->nr; i++)
		strbuf_addf(&sb, "%s%s", i ? "\n" : "", hints->items[i].string);
	return strbuf_detach(&sb, NULL);
}

static int finish(struct request *q)
{
	struct string_list conf = STRING_LIST_INIT_DUP, tasks = STRING_LIST_INIT_DUP;
	struct string_list paths = STRING_LIST_INIT_DUP, written = STRING_LIST_INIT_DUP;
	struct string_list missing = STRING_LIST_INIT_DUP, failing = STRING_LIST_INIT_DUP;
	struct string_list hints = STRING_LIST_INIT_DUP;
	struct strbuf in = STRBUF_INIT, failed = STRBUF_INIT, results = STRBUF_INIT;
	char *req = xstrfmt("%s/ait/check-request.yaml", gitdir), *res = xstrfmt("%s/ait/check-result.yaml", gitdir);
	char rid[16], *input = NULL;
	struct verdict v;
	int unknown = 0;
	size_t i, j;

	/* spec 2.3: every changed record took an allowed step */
	for (i = 0; i < q->s.nr; i++) {
		struct record *r = q->s.r[i];
		const char *from = r->old ? rget(r->old, "state") : "-", *to = rget(r, "state");

		if (is_current(r) && (!r->old || !is_current(r->old))) {
			resolve_new(r, "consumer", q->v);
			resolve_new(r, "provider", q->v);
		}
		if (unchanged(r))
			continue;
		for (j = 0; *r->id == 'u' && !strcmp(to, "undertaken") && j < 2; j++)
			if (!is_live(link_end(q, r, j)))
				reject(1, j ? xstrfmt("undertake it with a future offer: 'ait future offer %s %s as o undertake %s o -m <why>'",
						      rjoin(link_end(q, r, 0), "provider", ","), def_paths(link_end(q, r, 0)),
						      link_end(q, r, 0)->id) : NULL,
				       "not allowed: '%s' is %s", link_end(q, r, j)->id, rget(link_end(q, r, j), "state"));
		if (!strstr(q->gate ? gate_moves : fulfill_moves, xstrfmt(" %c%s:%s ", *r->id == 'u' ? 'u' : 'x', from, to)))
			reject(1, is_current(r->old) && *r->id != 'u' && r->old ?
			       xstrfmt("to change '%s' without a check, reopen it: 'ait future amend %s -m <why>'", r->id, r->id) : NULL,
			       "not allowed: '%s' cannot go from %s to %s", r->id, from, to);
	}
	/* spec 1.5b: the check set, decided before anything is pinned again */
	for (i = 0; i < q->s.nr; i++) {
		struct record *r = q->s.r[i];

		if (is_current(r) && (!r->old || !is_current(r->old) || r->recheck || touches(r->old, q->ht, q->v) ||
				      undertaken_by_new_current(q, r) ||
				      (!unsorted_string_list_has_string(&q->scoped, r->id) && strcmp(pins(r), pins(r->old)))))
			string_list_append(&conf, r->id)->util = r;
	}
	string_list_sort(&conf);
	for (i = 0; i < q->scoped.nr; i++)
		if (!unsorted_string_list_has_string(&conf, q->scoped.items[i].string))
			reject(128, NULL, "bad scope: '%s' is not checked by this command", q->scoped.items[i].string);
	if (!q->gate && !q->fulfill && !conf.nr && !q->first.code) {
		if (strcmp(q->v->oid, q->ht->oid))
			publish(&written, 0, q->msg.buf);
		else
			puts("noop");
		return 0;
	}
	session(1);
	if (q->gate)
		validate(&q->s, q->v);
	/* spec 7.7.3-4: the code to check is there */
	for (i = 0; i < conf.nr; i++) {
		struct string_list sc = STRING_LIST_INIT_DUP;

		rvals(conf.items[i].util, "provider", &sc);
		for (j = 0; j < sc.nr; j++) {
			char *path = xstrndup(sc.items[j].string, strcspn(sc.items[j].string, " "));
			const char *file = path + 4 * starts_with(path, "new:");

			if (path[strlen(path) - 1] == '/')
				reject(128, xstrfmt("give it the scope you implemented: 'ait fulfill %s=<scope> -m <why>'",
						    conf.items[i].string),
				       "bad scope: '%s' becomes current without a scope", conf.items[i].string);
			if (blob_at(q->v, file))
				continue;
			string_list_append(&missing, conf.items[i].string)->util = conf.items[i].util;
			string_list_clear(&hints, 0);
			if (access(xstrfmt("%s/%s", top, file), F_OK) && starts_with(path, "new:"))
				add_hint(&hints, xstrfmt("write '%s'", file));
			if (!access(xstrfmt("%s/%s", top, file), F_OK) || starts_with(path, "new:"))
				add_hint(&hints, xstrfmt("stage '%s', then rerun this command", file));
			if (access(xstrfmt("%s/%s", top, file), F_OK) && !starts_with(path, "new:"))
				add_hint(&hints, xstrfmt("if it was moved, rerun with '%s=<new scope>'", conf.items[i].string));
			next_hints(q, &hints, &missing);
			block(q, 1, "code-required", join_hints(&hints),
			      xstrfmt("no code under the contract's scope: '%s'", conf.items[i].string));
			break;
		}
	}
	if (!q->gate && !missing.nr && check_state(&q->s, q->v, &v)) {
		strbuf_addf(&q->mixed, "invalid %s %s\n", v.word, v.msg);
		block(q, v.code, v.word, v.hint, v.msg);
	}
	for (i = 0; i < conf.nr && !missing.nr; i++) {
		struct string_list f = STRING_LIST_INIT_DUP;

		rvals(conf.items[i].util, "file", &f);
		for (j = 0; j < f.nr; j++)
			if (ends_with(f.items[j].string, " def"))
				string_list_insert(&tasks, xstrfmt("%s:%.*s", task_contract(conf.items[i].util),
								   (int)strcspn(f.items[j].string, " "), f.items[j].string))->util = conf.items[i].util;
	}
	/* spec 8: the results are for exactly this state */
	if (tasks.nr && !q->force) {
		strbuf_addf(&in, "head %s\n", head);
		tree_diff(q->ht, q->v, &paths);
		for (i = 0; i < paths.nr; i++) {
			struct entry *e = tree_get(q->v, paths.items[i].string);

			if (!starts_with(paths.items[i].string, ".ait/"))
				strbuf_addf(&in, "path %s %s %s\n", paths.items[i].string, e ? e->mode : "-", e ? e->oid : "-");
		}
		for (i = 0; i < tasks.nr; i++)
			strbuf_addf(&in, "task %s %s %s\n", tasks.items[i].string,
				    blob_at(q->v, strchr(tasks.items[i].string, ':') + 1),
				    shows(tasks.items[i].util, "provider", q->v, was_current(tasks.items[i].util)));
		input = line1(git_run(in.buf, NULL, NULL, "hash-object --stdin"));
	}
	if (tasks.nr && !q->force && q->result && !q->first.code) {
		struct strbuf text = STRBUF_INIT;

		strbuf_read_file(&text, q->result, 0);
		if (read_results(q, &tasks, input, &failed, &failing, &unknown))
			strbuf_addstr(&results, text.buf);
		else
			q->result = NULL;
	}
	if (q->first.code || (tasks.nr && !q->force && !q->result)) {
		if (tasks.nr && !q->force && !missing.nr)
			write_check_files(q, &tasks, input, req, res);
		for (i = 0; i < q->selected.nr; i++)
			printf("id %s\n", q->selected.items[i].string);
		for (i = 0; !q->gate && i < conf.nr; i++)
			printf("provider %s %s\n", conf.items[i].string, shows(conf.items[i].util, "provider", q->v, was_current(conf.items[i].util)));
		printf("%s", q->mixed.buf ? q->mixed.buf : "");
		for (i = 0; i < missing.nr; i++)
			printf("code-required %s\n", missing.items[i].string);
		for (i = 0; !q->gate && i < conf.nr; i++)
			if (touches(((struct record *)conf.items[i].util)->old, q->ht, q->v))
				printf("touch %s\n", conf.items[i].string);
		printf("%s", q->gate ? "" : "\n");
		for (i = 0; !missing.nr && i < tasks.nr; i++) {
			struct string_list kv = STRING_LIST_INIT_DUP;

			def_load(q->v, strchr(tasks.items[i].string, ':') + 1, &kv);
			printf("task %s %s\n", tasks.items[i].string,
			       yget(&kv, "engine") && *yget(&kv, "engine") ? "exec" : "prompt");
		}
		if (tasks.nr && !q->force && !missing.nr)
			printf("check-request %s\ncheck-result %s\n", req, res);
		printf("%s", q->gate ? "" : missing.nr || strstr(q->mixed.buf ? q->mixed.buf : "", "\n") ? "ready no\n" : "ready yes\n");
		if (q->first.code)
			reject(q->first.code, q->first.hint, "%s", q->first.msg);
		add_hint(&hints, xstrfmt("check each task, fill '%s', and rerun with -r '%s'", res, res));
		add_hint(&hints, "if it is trivial, add --force instead (recorded as forced)");
		next_hints(q, &hints, &conf);
		reject(1, join_hints(&hints), "check results required: %d task(s)", (int)tasks.nr);
	}
	if (failed.len) {
		if (unknown) {
			add_hint(&hints, xstrfmt("check it where its environment is available (see '%s') and rerun with -r '%s'", req, res));
			add_hint(&hints, "if it is trivial, add --force (recorded as forced)");
		}
		if (unknown < (int)failing.nr)
			fix_hints(q, &hints, &failing);
		next_hints(q, &hints, &failing);
		reject(1, join_hints(&hints), "check failed: %s", failed.buf);
	}
	/* spec 7.7.4 and 6.5: pin the check set at the judged tree, then write what changed */
	newid(rid, 'R');
	for (i = 0; i < conf.nr; i++) {
		struct record *r = conf.items[i].util;
		struct string_list sc = STRING_LIST_INIT_DUP;

		rvals(r, "provider", &sc);
		for (j = 0; j < sc.nr; j++) {
			char *path = xstrndup(sc.items[j].string, strcspn(sc.items[j].string, " "));

			sc.items[j].string = xstrfmt("%s %s", path, blob_at(q->v, path));
		}
		rset(r, "provider", &sc);
	}
	for (i = 0; i < q->s.nr; i++) {
		struct record *r = q->s.r[i];

		if (unchanged(r))
			continue;
		set1(r, "prev", r->old ? blob_at(q->ht, xstrfmt(".ait/%s", r->id)) : "-");
		mkdir(xstrfmt("%s/.ait", top), 0777);
		mkdir(xstrfmt("%s/.ait/undertake", top), 0777);
		write_file_buf(xstrfmt("%s/.ait/%s", top, r->id), record_text(r, 1), strlen(record_text(r, 1)));
		string_list_append(&written, xstrfmt(".ait/%s", r->id))->util = r;
		if (!r->old && r->seq)
			replace(&results, xstrfmt("task: +%d:", r->seq), xstrfmt("task: %s:", r->id));
	}
	if (!written.nr && !conf.nr && !strcmp(q->v->oid, q->ht->oid)) {
		puts("noop");
		return 0;
	}
	for (i = 0; q->gate && i < q->files.nr; i++)
		string_list_append(&written, q->files.items[i].string);
	if (results.len)
		strbuf_addf(&q->msg, "\n%s", results.buf);
	strbuf_addf(&q->msg, "\nAit-Request: %s\nAit-Agent: %s\nAit-Session: %s\n", rid, agent, sid);
	for (i = 0; i < conf.nr; i++)
		strbuf_addf(&q->msg, "Ait-Check: %s %s %s\n", conf.items[i].string, q->force ? "forced" : "reported",
			    pins(conf.items[i].util));
	publish(&written, q->gate, q->msg.buf);
	printf("request %s\n", rid);
	for (i = 0; i < written.nr && written.items[i].util; i++) {
		struct record *r = written.items[i].util;

		if (!r->old && *r->id != 'u')
			printf("new %s%s%s\n", r->id, r->alias ? " " : "", r->alias ? r->alias : "");
		else if (*r->id == 'u')
			printf("%s %.13s %s\n", rget(r, "state"), r->id + 10, r->id + 24);
		else if (q->gate)
			printf("changed %s\n", r->id);
		else if (is_current(r) && !strcmp(rget(r->old, "state"), "future"))
			printf("fulfilled %s%s\n", r->id, via(q, r));
	}
	for (i = 0; i < conf.nr; i++)
		printf("check %s %s\n", conf.items[i].string, q->force ? "forced" : "reported");
	for (i = 0; i < written.nr && written.items[i].util; i++)
		also_undertaken(q, written.items[i].util);
	return 0;
}

/* spec 7.7.1: ID=SCOPES moves the provider to scopes found in the staged tree, inside an abstract one */
static void given_scopes(struct request *q, struct record *r, const char *list)
{
	struct string_list parts = STRING_LIST_INIT_DUP, old = STRING_LIST_INIT_DUP, vals = STRING_LIST_INIT_DUP;
	size_t i, j;

	rvals(r, "provider", &old);
	string_list_split(&parts, list, ",", -1);
	for (i = 0; i < parts.nr; i++) {
		int inside = !strstr(record_text(r, 1), "/ ");

		for (j = 0; j < old.nr; j++)
			inside |= starts_with(old.items[j].string, "/ ") ||
				  (strstr(old.items[j].string, "/ ") &&
				   starts_with(parts.items[i].string, xstrndup(old.items[j].string, strcspn(old.items[j].string, " "))));
		if (!inside)
			reject(128, NULL, "bad scope: '%s' is outside the scope of '%s'", parts.items[i].string, r->id);
		string_list_append(&vals, pin(parts.items[i].string, q->v, 0, 0));
	}
	rset(r, "provider", &vals);
}

static void select_promise(struct request *q, struct record *r)
{
	if (strcmp(rget(r, "state"), "future"))
		reject(1, is_current(r) ? xstrfmt("'%s' is already current; to check it again, run 'ait amend %s -m <why>' "
						  "to get the tasks", r->id, r->id) : NULL,
		       "not allowed: '%s' is %s", r->id, rget(r, "state"));
	set1(r, "state", "current");
	string_list_append(&q->selected, r->id);
}

/* spec 7.7 and 7.7a: fulfill keeps the selected promises; commit only checks what the change touches */
int cmd_fulfill(int argc, const char **argv)
{
	struct request q = { .fulfill = !strcmp(argv[0], "fulfill"), .msg = STRBUF_INIT, .mixed = STRBUF_INIT };
	struct string_list paths = STRING_LIST_INIT_DUP, args = STRING_LIST_INIT_DUP, ids = STRING_LIST_INIT_DUP;
	struct verdict v;
	int i;
	size_t k;

	for (i = 1; i < argc; i++)
		if (!strcmp(argv[i], "--force"))
			for (k = 1; k < (size_t)argc; k++)
				if (!strcmp(argv[k], "-r") || !strcmp(argv[k], "--result"))
					reject(129, NULL, "options '-r' and '--force' cannot be used together");
	begin(&q);
	for (i = 1; i < argc; i++)
		if (!option(&q, argc, argv, &i))
			string_list_append(&args, argv[i]);
	/* spec 7.7.1: IDs once each; aliases live only inside one helper request */
	for (k = 0; k < args.nr; k++) {
		const char *a = args.items[k].string, *eq = strchr(a, '=');
		char *id = eq ? xstrndup(a, eq - a) : xstrdup(a);
		struct record *r = state_get(&q.s, id);

		if (*a == '-' || unsorted_string_list_has_string(&ids, id) || (!q.fulfill && !eq) || islower(*id))
			reject(129, islower(*id) ? "aliases are local to one command; use the ID printed on its 'new' line" : NULL,
			       "bad argument '%s'", a);
		if (!r || *r->id == 'u')
			reject(128, NULL, "no such contract: '%s'", id);
		string_list_append(&ids, id)->util = (void *)(eq ? eq + 1 : NULL);
	}
	if (q.fulfill)
		session(1);
	q.v = tree_load(line1(git("write-tree")));
	if (check_state(&q.s, q.ht, &v)) {
		strbuf_addf(&q.mixed, "invalid %s %s\n", v.word, v.msg);
		block(&q, v.code, v.word, v.hint, v.msg);
	}
	/* the selected targets: plain IDs, and ID=SCOPES of a future contract that no target offer carries */
	for (k = 0; q.fulfill && k < ids.nr; k++) {
		struct record *r = state_get(&q.s, ids.items[k].string);

		if (!ids.items[k].util || (!strcmp(rget(r, "state"), "future") && !carried(&q, &ids, r)))
			select_promise(&q, r);
	}
	/* rule 5: a future offer that becomes current keeps the future demands it undertakes */
	for (k = 0; k < q.s.nr; k++)
		if (*q.s.r[k]->id == 'D' && !strcmp(rget(q.s.r[k], "state"), "future") &&
		    undertaken_by_new_current(&q, q.s.r[k]))
			set1(q.s.r[k], "state", "current");
	for (k = 0; k < ids.nr; k++)
		if (ids.items[k].util) {
			string_list_append(&q.scoped, ids.items[k].string);
			given_scopes(&q, state_get(&q.s, ids.items[k].string), ids.items[k].util);
		}
	if (q.fulfill && !q.selected.nr)
		reject(129, NULL, "nothing to fulfill: give the IDs of future contracts");
	tree_diff(q.ht, q.v, &paths);
	for (k = 0; k < paths.nr; k++)
		if (contract_path(&q.s, paths.items[k].string)) {
			strbuf_addf(&q.mixed, "mixed %s\n", paths.items[k].string);
			block(&q, 1, "mixed", xstrfmt("unstage the spec files ('%s'), then rerun this command", paths.items[k].string),
			      xstrfmt("contract changes and code changes are mixed: '%s'", paths.items[k].string));
		}
	message(&q, q.fulfill ? "[FULFILL] " : NULL);
	return finish(&q);
}

/* spec 7.3: clauses apply in order to the records loaded from HEAD */
int cmd_request(int argc, const char **argv)
{
	struct request q = { .gate = 1, .msg = STRBUF_INIT, .mixed = STRBUF_INIT, .cmdline = STRBUF_INIT };
	struct strbuf info = STRBUF_INIT;
	int i = !strcmp(argv[0], "contract") || !strcmp(argv[0], "ct");
	size_t k;

	if (i && argc > 1 && strstr(" new add commit push pull ", xstrfmt(" %s ", argv[1])))
		unsupported(xstrfmt("contract %s", argv[1]));
	begin(&q);
	session(1);
	strbuf_addstr(&q.cmdline, "ait");
	for (k = 0; k < (size_t)argc; k++) {
		int skip = !strcmp(argv[k], "-m") || !strcmp(argv[k], "-F") || !strcmp(argv[k], "-r") || !strcmp(argv[k], "--result");

		if (skip || !strcmp(argv[k], "--force")) {
			k += skip;
			continue;
		}
		strbuf_addch(&q.cmdline, ' ');
		sq_quote_buf_pretty(&q.cmdline, argv[k]);
	}
	while (i < argc) {
		int future = !strcmp(argv[i], "future");
		const char *w = argv[i + future];
		struct record *r, *o;

		if (option(&q, argc, argv, &i) && ++i)
			continue;
		i += future + 1;
		if (!w || (future && !strcmp(w, "drop")))
			unsupported(w ? "future drop" : "future");
		if (!strcmp(w, "demand") || !strcmp(w, "offer")) {
			new_contract(&q, w, future, argc, argv, &i);
		} else if (!strcmp(w, "amend")) {
			r = resolve(&q, arg(argc, argv, &i));
			if (*r->id == 'u' || !is_live(r))
				reject(1, NULL, "not allowed: '%s' is not a future or current contract", r->id);
			if (future)
				set1(r, "state", "future");
			r->recheck = is_current(r);
			if (i < argc && !is_word(argv[i])) {
				const char *a = *r->id == 'D' ? arg(argc, argv, &i) : "-", *b = arg(argc, argv, &i);
				const char *f = arg(argc, argv, &i);

				r->recheck &= !strcmp(a, "-") && !strcmp(b, "-") && !strcmp(f, "-");
				scopes(&q, r, "consumer", a);
				scopes(&q, r, "provider", b);
				defs(&q, r, f);
			}
		} else if (future && !strcmp(w, "cancel")) {
			r = resolve(&q, arg(argc, argv, &i));
			if (strcmp(rget(r, "state"), "cancelled"))
				set1(r, "state", "cancelled");
		} else if (!future && (!strcmp(w, "undertake") || !strcmp(w, "release"))) {
			r = resolve(&q, arg(argc, argv, &i));
			o = resolve(&q, arg(argc, argv, &i));
			if (*r->id != 'D' || *o->id != 'O')
				reject(128, NULL, "no such contract: '%s' must be a demand and '%s' an offer", r->id, o->id);
			r = state_get(&q.s, xstrfmt("undertake/%s-%s", r->id, o->id)) ?
			    state_get(&q.s, xstrfmt("undertake/%s-%s", r->id, o->id)) :
			    *w == 'r' ? NULL : state_add(&q.s, xstrfmt("undertake/%s-%s", r->id, o->id));
			if (r && !r->lines.nr)
				set1(r, "prev", "-");
			if (r)
				set1(r, "state", *w == 'u' ? "undertaken" : "released");
		} else {
			reject(129, NULL, "unknown clause '%s'", w);
		}
	}
	message(&q, "[CONTRACT] ");
	/* spec 7.3.3: the candidate is HEAD plus the given definitions and what they declare, from the work tree */
	for (k = 0; k < q.files.nr; k++) {
		const char *p = q.files.items[k].string;
		struct strbuf text = STRBUF_INIT, idx = STRBUF_INIT;
		struct string_list kv = STRING_LIST_INIT_DUP;
		struct stat st;
		char *oid;
		size_t j;

		if (lstat(xstrfmt("%s/%s", top, p), &st) || !S_ISREG(st.st_mode))
			reject(1, xstrfmt("create '%s' or stop declaring it", p), "spec files do not match the contract: missing '%s'", p);
		oid = line1(git("hash-object -w -- %s", p));
		strbuf_addstr(&idx, git("ls-files -s -- %s", p));
		if (idx.len && !strstr(idx.buf, oid) && (!blob_at(q.ht, p) || !strstr(idx.buf, blob_at(q.ht, p))))
			reject(1, xstrfmt("stage all of '%s' or none of it", p), "spec files do not match the contract: '%s' is partly staged", p);
		strbuf_addf(&info, "%s %s\t%s\n", st.st_mode & S_IXUSR ? "100755" : "100644", oid, p);
		if (strbuf_read_file(&text, xstrfmt("%s/%s", top, p), 0) < 0 || !strstr(text.buf, "body:"))
			continue;
		yaml(text.buf, &kv, p);
		for (j = 0; j < kv.nr; j++)
			if (starts_with(kv.items[j].string, "inputs.") && !unsorted_string_list_has_string(&q.files, kv.items[j].util))
				string_list_append(&q.files, kv.items[j].util);
	}
	q.v = tree_load(tree_with(q.ht->oid, info.buf));
	for (k = 0; k < q.s.nr; k++)
		if (is_live(q.s.r[k]) && expand_files(q.s.r[k], q.v))
			reject(1, "create the spec files it declares, or stop declaring them",
			       "spec files do not match the contract: '%s'", q.s.r[k]->id);
	for (k = 0; k < q.s.nr; k++)
		if (is_live(q.s.r[k]) && !unchanged(q.s.r[k]))
			unpinned(&q, q.s.r[k]);
	return finish(&q);
}
