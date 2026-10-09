/*
 * ait-mini entry point: command table, git calls, messages, and the commands
 * that do not write requests (session, init, hook, todo).
 */
#include "ait.h"
#include "run-command.h"
#include "quote.h"
#include <dirent.h>

const char *top, *gindex, *headref, *head, *gitdir, *commondir;
char *git_error;
char agent[72], sid[16];

/* spec 5.9a: a session that is not this worktree's default prefixes the ait commands it is given */
static char *cmd_prefix;

static char *prefixed(const char *hint)
{
	struct strbuf sb = STRBUF_INIT;
	const char *p, *hit;

	for (p = hint; cmd_prefix && (hit = strstr(p, "'ait ")); p = hit + 1) {
		strbuf_add(&sb, p, hit + 1 - p);
		strbuf_addstr(&sb, cmd_prefix);
	}
	strbuf_addstr(&sb, p);
	return strbuf_detach(&sb, NULL);
}

/* git's error()/die() routines and advise(), with the exit code of spec 5.7 */
void reject(int code, const char *hint, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(code == 128 ? get_die_message_routine() : get_error_routine())(fmt, ap);
	va_end(ap);
	if (code == 129)
		fputs("usage: ait <command> [<args>]  (see spec/ait-mini.md)\n", stderr);
	if (hint)
		advise("%s", prefixed(hint));
	exit(code);
}

void unsupported(const char *what)
{
	reject(128, "see spec/ait-mini.md for what to do instead", "not supported in ait-mini: '%s'", what);
}

/*
 * Run git with ARGS (spec 5.12). IN feeds stdin and INDEX sets GIT_INDEX_FILE.
 * Without RC a failure is E_GIT with git's first stderr line.
 */
char *git_args(struct strvec *args, const char *in, const char *index, int *rc)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	struct strbuf out = STRBUF_INIT, err = STRBUF_INIT;
	int code;

	strvec_pushl(&cmd.args, "git", "-c", "core.quotePath=false", "-c", "trailer.separators=:",
		     "-c", "core.commentChar=#", "-C", top ? top : ".", NULL);
	strvec_pushv(&cmd.args, args->v);
	if (index)
		strvec_pushf(&cmd.env, "GIT_INDEX_FILE=%s", index);
	code = pipe_command(&cmd, in, in ? strlen(in) : 0, &out, 0, &err, 0);
	free(git_error);
	git_error = xstrndup(err.buf, strcspn(err.buf, "\n"));
	if (rc)
		*rc = code;
	else if (code)
		reject(128, top ? NULL : "run this command inside a git repository", "git failed: %s", git_error);
	strbuf_release(&err);
	return strbuf_detach(&out, NULL);
}

/* git_args() with the words of FMT, each "%s" word taking one argument verbatim */
char *git_run(const char *in, const char *index, int *rc, const char *fmt, ...)
{
	struct strvec args = STRVEC_INIT;
	struct string_list words = STRING_LIST_INIT_DUP;
	va_list ap;
	size_t i;

	string_list_split(&words, fmt, " ", -1);
	va_start(ap, fmt);
	for (i = 0; i < words.nr; i++)
		strvec_push(&args, strcmp(words.items[i].string, "%s") ?
			    words.items[i].string : va_arg(ap, const char *));
	va_end(ap);
	string_list_clear(&words, 0);
	return git_args(&args, in, index, rc);
}

char *line1(char *s)
{
	if (s)
		s[strcspn(s, "\n")] = 0;
	return s;
}

/* spec 1.10: six bytes of /dev/urandom */
void newid(char *out, int prefix)
{
	unsigned char b[6];
	int fd = xopen("/dev/urandom", O_RDONLY), i;

	if (read_in_full(fd, b, sizeof(b)) != sizeof(b))
		die_errno("could not read '/dev/urandom'");
	close(fd);
	out[0] = prefix;
	for (i = 0; i < 6; i++)
		sprintf(out + 1 + 2 * i, "%02x", b[i]);
}

/* spec 5.12: fix the toplevel and the hook's index, then clear what redirects git */
static void setup(void)
{
	static const char *clear[] = { "GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_PREFIX",
				       "GIT_COMMON_DIR", "GIT_DIFF_OPTS", NULL };
	int i, major = 0, minor = 0;

	sscanf(git("version"), "git version %d.%d", &major, &minor);
	if (major < 2 || (major == 2 && minor < 48))
		reject(128, "install git 2.48 or later", "git %d.%d is too old for ait", major, minor);
	top = line1(git("rev-parse --show-toplevel"));
	if (getenv("GIT_INDEX_FILE"))
		gindex = realpath(getenv("GIT_INDEX_FILE"), NULL);
	gitdir = line1(git("rev-parse --absolute-git-dir"));
	commondir = line1(git("rev-parse --path-format=absolute --git-common-dir"));
	for (i = 0; clear[i]; i++)
		unsetenv(clear[i]);
	setenv("GIT_LITERAL_PATHSPECS", "1", 1);
	headref = line1(git_run(NULL, NULL, &i, "symbolic-ref -q HEAD"));
	headref = i ? NULL : headref;
	head = line1(git_run(NULL, NULL, &i, "rev-parse -q --verify HEAD^{commit}"));
	head = i ? NULL : head;
}

static char *state_path(const char *name)
{
	return xstrfmt("%s/ait/%s", commondir, name);
}

/* spec 4.6: the default-session pointer of the worktree WT */
static char *pointer_path(const char *wt)
{
	return state_path(xstrfmt("worktrees/%s", line1(git_run(wt, NULL, NULL, "hash-object --stdin"))));
}

static char *default_of(const char *wt)
{
	struct strbuf sb = STRBUF_INIT;

	return strbuf_read_file(&sb, pointer_path(wt), 0) < 0 ? NULL : line1(strbuf_detach(&sb, NULL));
}

/* local state is written whole; a failure is E_IO (spec 5.7) */
static void write_state(const char *path, const char *text)
{
	char *tmp = xstrfmt("%s.tmp", path);
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666);

	if (fd < 0 || write_in_full(fd, text, strlen(text)) < 0 || close(fd) || rename(tmp, path))
		reject(128, xstrfmt("make '%s' writable (free space, permissions), then rerun this command",
				    fd < 0 ? state_path("sessions") : path), "write error: '%s': %s", path, strerror(errno));
}

static char *quoted(const char *s)
{
	struct strbuf sb = STRBUF_INIT;

	sq_quote_buf(&sb, s);
	return strbuf_detach(&sb, NULL);
}

/* "agent NAME\nworktree PATH\n" (spec 4.2); returns the worktree or NULL */
static char *session_read(const char *s, char *name)
{
	struct strbuf sb = STRBUF_INIT;
	char *wt;

	if (!s || strlen(s) != 13 || strbuf_read_file(&sb, state_path(xstrfmt("sessions/%s", s)), 0) < 0)
		return NULL;
	wt = strstr(sb.buf, "\nworktree ");
	if (!starts_with(sb.buf, "agent ") || !wt)
		return NULL;
	if (name)
		snprintf(name, 72, "%.*s", (int)strcspn(sb.buf + 6, "\n"), sb.buf + 6);
	return line1(wt + 10);
}

/* spec 5.1: AIT_SESSION, else this worktree's default session */
const char *session(int need)
{
	const char *env = getenv("AIT_SESSION"), *ptr = default_of(top);
	char *wt;

	if (env && !strcmp(env, "-"))
		env = NULL;
	else if (!env)
		env = ptr;
	wt = session_read(env, agent);
	if (env && !wt && env == ptr)
		unlink(pointer_path(top));
	else if (env && !wt)
		reject(128, "ait session list", "no usable ait session: unknown session '%s'", env);
	else if (wt && strcmp(wt, top))
		reject(128, xstrfmt("cd %s", quoted(wt)), "no usable ait session: '%s' belongs to worktree '%s'", env, wt);
	if (!wt && need)
		reject(128, "find your session with 'ait session list', or open one: 'ait session open --agent <name>'",
		       "no usable ait session: none for this worktree");
	if (!wt)
		return NULL;
	snprintf(sid, sizeof(sid), "%s", env);
	if (!ptr || strcmp(env, ptr))
		cmd_prefix = xstrfmt("AIT_SESSION=%s ", sid);
	return sid;
}

static int cmd_session(int argc, const char **argv)
{
	struct string_list ids = STRING_LIST_INIT_DUP;
	char *mine = default_of(top), other[72];
	struct dirent *de;
	DIR *dir;
	size_t i;

	mkdir(state_path(""), 0777);
	mkdir(state_path("sessions"), 0777);
	mkdir(state_path("worktrees"), 0777);
	dir = opendir(state_path("sessions"));
	while (dir && (de = readdir(dir)))
		if (*de->d_name == 'S' && session_read(de->d_name, NULL))
			string_list_insert(&ids, de->d_name);
	if (dir)
		closedir(dir);
	if (argc == 4 && !strcmp(argv[1], "open") && !strcmp(argv[2], "--agent")) {
		const char *name = argv[3];

		if (!*name || strlen(name) > 64 || !strcmp(name, "unknown") ||
		    name[strspn(name, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-")])
			reject(129, NULL, "bad agent name '%s'", name);
		newid(sid, 'S');
		write_state(state_path(xstrfmt("sessions/%s", sid)), xstrfmt("agent %s\nworktree %s\n", name, top));
		for (i = 0; i < ids.nr; i++) {
			const char *wt = session_read(ids.items[i].string, other);

			if (strcmp(other, name))
				continue;
			warning("agent name '%s' is also used by session '%s' (worktree '%s')", name, ids.items[i].string, wt);
			advise("if '%s' is yours, close this one: 'ait session close %s', and use '%s' in '%s' with "
			       "AIT_SESSION=%s; otherwise close this one and open again with another name",
			       ids.items[i].string, sid, ids.items[i].string, wt, ids.items[i].string);
		}
		if (session_read(mine, other) && strcmp(other, name)) {
			warning("this worktree is used by session '%s' (agent '%s')", mine, other);
			advise("close this one: 'ait session close %s', then open a session in a worktree of your own: "
			       "'ait session open --agent %s' there", sid, name);
		} else if (!session_read(mine, NULL)) {
			write_state(pointer_path(top), xstrfmt("%s\n", sid));
		}
		printf("%s\n", sid);
	} else if (argc == 2 && !strcmp(argv[1], "list")) {
		for (i = 0; i < ids.nr; i++) {
			const char *wt = session_read(ids.items[i].string, other), *d = default_of(wt);

			printf("session %s\nagent %s\nworktree %s\ndefault %s\nsubs 0\nlistening no\n\n", ids.items[i].string,
			       other, wt, d && !strcmp(d, ids.items[i].string) ? "yes" : "no");
		}
	} else if ((argc == 2 || argc == 3) && !strcmp(argv[1], "close")) {
		const char *s = argc == 3 ? argv[2] : session(1);

		if (!string_list_has_string(&ids, s))
			reject(128, "ait session list", "no usable ait session: unknown session '%s'", s);
		unlink(state_path(xstrfmt("sessions/%s", s)));
		dir = opendir(state_path("worktrees"));
		while (dir && (de = readdir(dir))) {
			struct strbuf sb = STRBUF_INIT;
			char *p = state_path(xstrfmt("worktrees/%s", de->d_name));

			if (strbuf_read_file(&sb, p, 0) >= 0 && !strcmp(line1(sb.buf), s))
				unlink(p);
		}
		if (dir)
			closedir(dir);
	} else {
		reject(129, NULL, "unknown session command");
	}
	return 0;
}

/* spec 7.1.1: an executable hook with the words "ait hook NAME" on a line that is not a comment */
static int has_ait_hook(const char *path, const char *name)
{
	struct string_list lines = STRING_LIST_INIT_DUP;
	struct strbuf sb = STRBUF_INIT;
	size_t i;

	if (access(path, X_OK) || strbuf_read_file(&sb, path, 0) < 0)
		return 0;
	string_list_split(&lines, sb.buf, "\n", -1);
	for (i = 0; i < lines.nr; i++) {
		struct string_list w = STRING_LIST_INIT_DUP;
		char *l = lines.items[i].string + strspn(lines.items[i].string, " \t");
		size_t j, k = 0;

		if (*l == '#' || starts_with(l, "//"))
			continue;
		for (j = 0; l[j]; j++)
			if (strchr(" \t'\",()[]", l[j]))
				l[j] = ' ';
		string_list_split(&w, l, " ", -1);
		for (j = 0; j < w.nr; j++)
			if (*w.items[j].string)
				w.items[k++] = w.items[j];
		for (j = 0; j + 2 < k; j++)
			if (!strcmp(w.items[j].string, "ait") && !strcmp(w.items[j + 1].string, "hook") &&
			    !strcmp(w.items[j + 2].string, name))
				return 1;
	}
	return 0;
}

/*
 * spec 7.1.1 and, for the compare-and-swap of 5.5, reference-transaction: the
 * hook rejects a commit whose branch moved since ait judged it (AIT_EXPECT)
 */
static const char *hooks[] = { "pre-commit", "pre-merge-commit", "commit-msg", "pre-push", "reference-transaction", NULL };

static char *hooks_dir(void)
{
	char *dir = line1(git("rev-parse --git-path hooks"));

	return *dir == '/' ? dir : xstrfmt("%s/%s", top, dir);
}

static int cmd_init(int argc, const char **argv)
{
	char *dir = hooks_dir();
	int in_repo = starts_with(dir, xstrfmt("%s/", top)) && !starts_with(dir, commondir);
	struct string_list paths = STRING_LIST_INIT_DUP;
	struct tree *t;
	int i, bad = 0;

	if (argc > 1)
		reject(129, NULL, "init takes no arguments");
	mkdir(dir, 0777);
	for (i = 0; hooks[i]; i++) {
		char *path = xstrfmt("%s/%s", dir, hooks[i]);
		int rc = 0;

		if (access(path, F_OK) && !in_repo) {
			write_file(path, "#!/bin/sh\n%scommand -v ait >/dev/null 2>&1 || exit 0\nexec ait hook %s \"$@\"\n",
				   i == 4 ? "[ -n \"$AIT_EXPECT\" ] || exit 0\n" : "", hooks[i]);
			chmod(path, 0755);
			continue;
		}
		if (has_ait_hook(path, hooks[i]))
			continue;
		error("not allowed: hook '%s' is not set up for ait", path);
		advise("make '%s' run 'ait hook %s' with its arguments and its stdin, and stop when that fails; git runs a hook "
		       "only if it is executable. In a shell hook, these lines after the first line do it ('#!/bin/sh' first "
		       "if the file is new):\n%s\nthen rerun this command", path, hooks[i], strcmp(hooks[i], "pre-push") ?
		       xstrfmt("if command -v ait >/dev/null 2>&1; then ait hook %s \"$@\" || exit $?; fi", hooks[i]) :
		       "t=$(mktemp) && cat > \"$t\" || exit 1\nif command -v ait >/dev/null 2>&1; then ait hook pre-push \"$@\" "
		       "< \"$t\" || { s=$?; rm -f \"$t\"; exit $s; }; fi\nexec < \"$t\"; rm -f \"$t\"");
		if (starts_with(path, xstrfmt("%s/", top)))
			git_run(NULL, NULL, &rc, "--no-literal-pathspecs check-ignore -q -- %s", path + strlen(top) + 1);
		if (starts_with(path, xstrfmt("%s/", top)) && rc)
			advise("then commit it: stage '%s' and run 'ait commit -m <why>'", path + strlen(top) + 1);
		bad = 1;
	}
	mkdir(state_path(""), 0777);
	if (bad)
		return 1;
	t = tree_load(head ? head : EMPTY_TREE);
	if (tree_get(t, ".ait/init") && !strcmp(tree_get(t, ".ait/init")->mode, "100644") &&
	    !strcmp(git("cat-file blob %s", tree_get(t, ".ait/init")->oid), "version 1\n")) {
		puts("init exists");
		return 0;
	}
	precondition(1);
	mkdir(xstrfmt("%s/.ait", top), 0777);
	write_file(xstrfmt("%s/.ait/init", top), "version 1");
	string_list_append(&paths, ".ait/init");
	printf("init created %s\n", publish(&paths, 1, "[CONTRACT] ait init\n"));
	return 0;
}

/* spec 5.2a: a clone with records but without ait's hooks lets plain commits go unnoticed */
static void check_hooks(void)
{
	struct tree *t = tree_load(head ? head : EMPTY_TREE);
	size_t i;
	int k;

	for (i = 0; i < t->nr; i++)
		if (starts_with(t->e[i].path, ".ait/") && strcmp(t->e[i].path, ".ait/init"))
			break;
	for (k = 0; i < t->nr && hooks[k]; k++)
		if (!has_ait_hook(xstrfmt("%s/%s", hooks_dir(), hooks[k]), hooks[k])) {
			advise("this clone is not set up for ait; run 'ait init'");
			return;
		}
}

/* spec 7.1.6: every pushed commit holds a valid state */
static int pre_push(void)
{
	struct strbuf in = STRBUF_INIT;
	struct string_list lines = STRING_LIST_INIT_DUP;
	size_t i;

	strbuf_read(&in, 0, 0);
	string_list_split(&lines, in.buf, "\n", -1);
	for (i = 0; i < lines.nr; i++) {
		char ref[1024], oid[48];
		struct state s = { 0 };
		struct tree *t;

		if (sscanf(lines.items[i].string, "%1023s %47s", ref, oid) != 2 || strspn(oid, "0") == strlen(oid))
			continue;
		t = tree_load(oid);
		state_load(&s, t);
		validate(&s, t);
	}
	return 0;
}

/* spec 5.5: the branch is still where ait judged it, under git's ref lock */
static int ref_transaction(const char *phase)
{
	const char *expect = getenv("AIT_EXPECT");
	struct strbuf in = STRBUF_INIT;
	struct string_list lines = STRING_LIST_INIT_DUP;
	size_t i;

	if (!expect || strcmp(phase, "prepared"))
		return 0;
	strbuf_read(&in, 0, 0);
	string_list_split(&lines, in.buf, "\n", -1);
	for (i = 0; i < lines.nr; i++) {
		const char *l = lines.items[i].string;

		if (strlen(l) > 82 && !strcmp(l + 82, strchr(expect, ' ') + 1) && strncmp(l, expect, 40))
			return 1;
	}
	return 0;
}

/* spec 7.1.2-7.1.3: guard plain commits, validate merges */
static int cmd_hook(int argc, const char **argv)
{
	struct state hs = { 0 }, ts = { 0 };
	struct string_list paths = STRING_LIST_INIT_DUP, checks = STRING_LIST_INIT_DUP, gone = STRING_LIST_INIT_DUP;
	struct tree *ht, *t;
	int no_merge;
	size_t i;

	if (argc > 1 && !strcmp(argv[1], "pre-push"))
		return pre_push();
	if (argc < 2 || (strcmp(argv[1], "pre-commit") && strcmp(argv[1], "pre-merge-commit")))
		return 0;
	t = tree_load(line1(git_run(NULL, gindex, NULL, "write-tree")));
	git_run(NULL, NULL, &no_merge, "rev-parse -q --verify MERGE_HEAD");
	if (!strcmp(argv[1], "pre-merge-commit") || !no_merge) {
		state_load(&ts, t);
		validate(&ts, t);
		return 0;
	}
	ht = tree_load(head ? head : EMPTY_TREE);
	state_load(&hs, ht);
	tree_diff(ht, t, &paths);
	for (i = 0; i < paths.nr; i++)
		if (contract_path(&hs, paths.items[i].string))
			reject(1, xstrfmt("unstage '%s'; contracts change only through ait (see spec/ait-mini.md)",
					  paths.items[i].string),
			       "contract files cannot be changed by a plain commit: '%s'", paths.items[i].string);
	checks_load(&checks);
	for (i = 0; i < hs.nr; i++)
		if (is_current(hs.r[i]) && !unverified(hs.r[i], ht, &checks) && unverified(hs.r[i], t, &checks))
			string_list_append(&gone, hs.r[i]->id)->util = hs.r[i];
	if (!gone.nr)
		return 0;
	/* the block of 7.1.2: its warning and contract lines are normative */
	warning("this commit leaves %d current contract(s) unverified", (int)gone.nr);
	for (i = 0; i < gone.nr; i++) {
		struct record *r = gone.items[i].util;
		struct string_list defs = STRING_LIST_INIT_DUP;
		size_t j;

		fprintf(stderr, "    %s %s %s%s (%.*s)\n", r->id, rget(r, "role"), shows(r, "provider", ht, 1),
			code_found(r, t) ? "" : " (no code under its scope)",
			(int)strcspn(rget(r, "creator"), " "), rget(r, "creator"));
		rvals(r, "file", &defs);
		for (j = 0; j < defs.nr; j++)
			if (ends_with(defs.items[j].string, " def"))
				fputs(def_text(ht, xstrndup(defs.items[j].string, strcspn(defs.items[j].string, " ")),
					       "        "), stderr);
	}
	warning("their receivers are notified after this commit");
	advise("next time, to check and commit in one step, use 'ait commit' instead of 'git commit'");
	for (i = 0; i < gone.nr; i++) {
		struct record *r = gone.items[i].util;

		advise("to change '%s' without a check, reopen it: 'ait future amend %s -m <why>'", r->id, r->id);
		if (code_found(r, t))
			advise("check '%s' again: run 'ait amend %s -m <why>' to get the tasks", r->id, r->id);
		else
			advise("'%s' has no code under its scope; if it was moved, run 'ait amend %s %s<new scope> -m <why>' "
			       "to get the tasks", r->id, r->id, *r->id == 'D' ? "- " : "");
	}
	return 0;
}

/* spec 3.9: two scopes overlap; "DIR/" and "/" cover what is under them */
static int overlap(const char *a, const char *b)
{
	skip_prefix(a, "new:", &a);
	skip_prefix(b, "new:", &b);
	a = xstrndup(a, strcspn(a, " "));
	b = xstrndup(b, strcspn(b, " "));
	return !strcmp(a, "/") || !strcmp(b, "/") || !strcmp(a, b) ||
	       (ends_with(a, "/") && starts_with(b, a)) || (ends_with(b, "/") && starts_with(a, b));
}

/* the todo argument ARG names R: its ID, or a scope over its scopes or spec files (7.8.1) */
static int named(struct record *r, const char *arg)
{
	static const char *keys[] = { "consumer", "provider", "file", NULL };
	size_t k, i;

	if (!strcmp(arg, r->id))
		return 1;
	for (k = 0; keys[k]; k++) {
		struct string_list v = STRING_LIST_INIT_DUP;

		rvals(r, keys[k], &v);
		for (i = 0; i < v.nr; i++)
			if (overlap(arg, v.items[i].string))
				return 1;
	}
	return 0;
}

static void link_lines(struct state *s, struct record *r, struct tree *t)
{
	size_t j;

	for (j = 0; j < s->nr; j++) {
		struct record *l = s->r[j], *other;
		struct string_list f = STRING_LIST_INIT_DUP;
		size_t k;

		if (*l->id != 'u' || strcmp(rget(l, "state"), "undertaken") || !strstr(l->id, r->id))
			continue;
		other = state_get(s, *r->id == 'O' ? xstrndup(l->id + 10, 13) : l->id + 24);
		printf("%s %s %s %.*s\n", *r->id == 'O' ? "undertakes" : "undertaken-by", other->id, rget(other, "state"),
		       (int)strcspn(rget(other, "creator"), " "), rget(other, "creator"));
		if (*r->id != 'O' || strcmp(rget(other, "state"), "future"))
			continue;
		printf("undertakes-provider %s %s\n", other->id, shows(other, "provider", t, 0));
		rvals(other, "file", &f);
		for (k = 0; k < f.nr; k++) {
			char *path = xstrndup(f.items[k].string, strcspn(f.items[k].string, " "));
			struct string_list kv = STRING_LIST_INIT_DUP;

			if (!ends_with(f.items[k].string, " def")) {
				printf("undertakes-file %s %s input\n", other->id, path);
				continue;
			}
			def_load(t, path, &kv);
			printf("undertakes-file %s %s %s\n%s", other->id, path,
			       yget(&kv, "engine") && *yget(&kv, "engine") ? "exec" : "prompt", def_text(t, path, "    "));
		}
	}
}

/* spec 7.8: my promises and my unverified demands, with what to do next */
static int cmd_todo(int argc, const char **argv)
{
	struct state s = { 0 };
	struct string_list checks = STRING_LIST_INIT_DUP, index = STRING_LIST_INIT_DUP;
	struct tree *t = tree_load(line1(git("write-tree"))), *ht;
	const char *me = session(0) ? agent : NULL;
	size_t i, j;
	int k;

	ht = tree_load(head ? head : EMPTY_TREE);
	state_load(&s, ht);
	checks_load(&checks);
	for (k = 1; k < argc; k++) {
		const char *a = argv[k];

		if (state_get(&s, a))
			continue;
		if (strlen(a) == 13 && strchr("DOX", *a))
			reject(128, NULL, "no such contract: '%s'", a);
		if (strchr(a, ':'))
			unsupported("line ranges");
		if (strcmp(a, "/") && !path_ok(ends_with(a, "/") ? xstrndup(a, strlen(a) - 1) : a))
			reject(128, NULL, "bad scope: '%s'", a);
	}
	for (i = 0; i < s.nr; i++) {
		struct record *r = s.r[i];
		struct string_list v = STRING_LIST_INIT_DUP;
		int mine, future, unver, pick = 0;

		if (r->bad) {
			warning("skipping malformed '.ait/%s' at 'HEAD'", r->id);
			continue;
		}
		if (*r->id == 'u' || !strcmp(r->id, "init"))
			continue;
		mine = me && starts_with(rget(r, "creator"), xstrfmt("%s ", me));
		future = !strcmp(rget(r, "state"), "future");
		unver = is_current(r) && unverified(r, t, &checks);
		if (touches(r, ht, t))
			string_list_append(&index, r->id)->util = r;
		for (k = 1; k < argc; k++)
			pick |= state_get(&s, argv[k]) ? !strcmp(argv[k], r->id) : (future || unver) && named(r, argv[k]);
		if (argc == 1)
			pick = me ? mine && (future || (unver && *r->id == 'D')) : future || unver;
		if (!pick)
			continue;
		printf("id %s\nrole %s\nstate %s\ncreator %.*s\n", r->id, rget(r, "role"), rget(r, "state"),
		       (int)strcspn(rget(r, "creator"), " "), rget(r, "creator"));
		for (j = 0; j < 2; j++) {
			struct string_list sc = STRING_LIST_INIT_DUP;
			size_t n;

			rvals(r, j ? "provider" : "consumer", &sc);
			for (n = 0; n < sc.nr; n++)
				printf("%s %s\n", j ? "provider" : "consumer", show(sc.items[n].string, t, j && is_current(r)));
		}
		printf("%s", unver ? "unverified yes\n" : "");
		rvals(r, "file", &v);
		for (j = 0; j < v.nr; j++) {
			char *path = xstrndup(v.items[j].string, strcspn(v.items[j].string, " "));
			struct string_list kv = STRING_LIST_INIT_DUP;

			if (!ends_with(v.items[j].string, " def")) {
				printf("file %s input\n", path);
				continue;
			}
			def_load(ht, path, &kv);
			printf("file %s %s\n%s", path, yget(&kv, "engine") && *yget(&kv, "engine") ? "exec" : "prompt",
			       def_text(ht, path, "    "));
		}
		link_lines(&s, r, t);
		putchar('\n');
		if (unver && code_found(r, t))
			advise("check '%s' again: run 'ait amend %s -m <why>' to get the tasks", r->id, r->id);
		else if (unver)
			advise("'%s' has no code under its scope; if it was moved, run 'ait amend %s %s<new scope> -m <why>' "
			       "to get the tasks", r->id, r->id, *r->id == 'D' ? "- " : "");
		else if (future && mine)
			advise("when ready, stage the code and run 'ait fulfill %s%s -m <why>' to get the tasks", r->id,
			       strstr(record_text(r, 1), "/ ") ? "=<scope>" : "");
	}
	if (index.nr)
		puts("index");
	for (i = 0; i < index.nr; i++)
		if (!code_found(index.items[i].util, t))
			printf("code-required %s\n", index.items[i].string);
	for (i = 0; i < index.nr; i++)
		printf("touch %s\n", index.items[i].string);
	if (index.nr)
		putchar('\n');
	return 0;
}

static struct cmd {
	const char *name;
	int (*fn)(int, const char **);
} commands[] = {
	{ "init", cmd_init }, { "hook", cmd_hook }, { "session", cmd_session }, { "todo", cmd_todo },
	{ "fulfill", cmd_fulfill }, { "commit", cmd_fulfill }, { "contract", cmd_request },
	{ "ct", cmd_request }, { "demand", cmd_request }, { "offer", cmd_request }, { "future", cmd_request },
	{ "amend", cmd_request }, { "undertake", cmd_request }, { "release", cmd_request },
	{ "sub", NULL }, { "unsub", NULL }, { "listen", NULL }, { "peek", NULL }, { "merge-driver", NULL },
};

/* after git v2.53.0 git.c: run the command from the table, hand the rest to git (spec 7.14) */
int main(int argc, const char **argv)
{
	size_t i;

	if (argc < 2) {
		puts("usage: ait <command> [<args>]\n\nsee spec/ait-mini.md for the commands of ait-mini");
		return 1;
	}
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[1], commands[i].name))
			continue;
		if (!commands[i].fn)
			unsupported(argv[1]);
		if (argc > 3 && !strcmp(argv[1], "hook") && !strcmp(argv[2], "reference-transaction"))
			return ref_transaction(argv[3]);
		setup();
		if (strcmp(argv[1], "init") && strcmp(argv[1], "hook"))
			check_hooks();
		return commands[i].fn(argc - 1, argv + 1);
	}
	execvp("git", (char *const *)argv);
	die_errno("cannot run git");
}
