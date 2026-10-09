/*
 * ait-mini entry point: command table, git calls, messages, and the commands
 * that do not write requests (session, init, hook, todo).
 */
#include "ait.h"
#include "run-command.h"
#include "quote.h"
#include <dirent.h>
#include <sys/file.h>
#include <regex.h>

const char *top, *gindex, *headref, *head, *gitdir, *commondir;
char *git_error;
size_t git_out_len;
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
	return strbuf_detach(&out, &git_out_len);
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

/*
 * The object NAME resolves to, through one "git cat-file --batch-command" for
 * the whole command (git-cat-file(1), BATCH OUTPUT): its contents when
 * CONTENTS, else "", with its ID in OID; NULL when NAME names no object.
 */
char *object(const char *name, int contents, char *oid, size_t *size)
{
	static struct child_process cat = CHILD_PROCESS_INIT;
	static FILE *out;
	char line[256], id[48], type[16], *cmd = xstrfmt("%s %s\n", contents ? "contents" : "info", name), *buf;
	size_t n;

	if (!out) {
		strvec_pushl(&cat.args, "git", "-C", top ? top : ".", "cat-file", "--batch-command", NULL);
		cat.in = cat.out = -1;
		if (start_command(&cat))
			reject(128, NULL, "git failed: cannot run 'git cat-file'");
		fcntl(cat.in, F_SETFD, FD_CLOEXEC);
		out = xfdopen(cat.out, "r");
	}
	if (write_in_full(cat.in, cmd, strlen(cmd)) < 0 || !fgets(line, sizeof(line), out))
		reject(128, NULL, "git failed: 'git cat-file' stopped");
	if (sscanf(line, "%47s %15s %zu", id, type, &n) != 3)
		return NULL;
	if (oid)
		strcpy(oid, id);
	if (size)
		*size = n;
	if (!contents)
		return xstrdup("");
	buf = xmallocz(n);
	if (fread(buf, 1, n, out) != n || fgetc(out) != '\n')
		reject(128, NULL, "git failed: 'git cat-file' stopped");
	return buf;
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
	char *paths, oid[48];

	sscanf(git("version"), "git version %d.%d", &major, &minor);
	if (major < 2 || (major == 2 && minor < 48))
		reject(128, "install git 2.48 or later", "git %d.%d is too old for ait", major, minor);
	paths = git("rev-parse --show-toplevel --absolute-git-dir --path-format=absolute --git-common-dir");
	top = xstrndup(paths, strcspn(paths, "\n"));
	paths += strlen(top) + 1;
	gitdir = xstrndup(paths, strcspn(paths, "\n"));
	commondir = line1(paths + strlen(gitdir) + 1);
	if (getenv("GIT_INDEX_FILE"))
		gindex = realpath(getenv("GIT_INDEX_FILE"), NULL);
	for (i = 0; clear[i]; i++)
		unsetenv(clear[i]);
	setenv("GIT_LITERAL_PATHSPECS", "1", 1);
	headref = line1(git_run(NULL, NULL, &i, "symbolic-ref -q HEAD"));
	headref = i ? NULL : headref;
	if (object("HEAD^{commit}", 0, oid, NULL))
		head = xstrdup(oid);
}

static char *state_path(const char *name)
{
	return xstrfmt("%s/ait/%s", commondir, name);
}

/* spec 4.6: the default-session pointer of the worktree WT */
static char *pointer_path(const char *wt)
{
	static struct string_list keys = STRING_LIST_INIT_DUP;
	struct string_list_item *hit = string_list_lookup(&keys, wt);

	if (!hit) {
		hit = string_list_insert(&keys, wt);
		hit->util = line1(git_run(wt, NULL, NULL, "hash-object --stdin"));
	}
	return state_path(xstrfmt("worktrees/%s", (char *)hit->util));
}

static char *default_of(const char *wt)
{
	struct strbuf sb = STRBUF_INIT;

	return strbuf_read_file(&sb, pointer_path(wt), 0) < 0 ? NULL : line1(strbuf_detach(&sb, NULL));
}

static NORETURN void write_error(const char *path)
{
	reject(128, xstrfmt("make '%s' writable (free space, permissions), then rerun this command", path),
	       "write error: '%s': %s", path, strerror(errno));
}

/* spec 4.6: the shared state of the worktree WT changes only under worktrees/<KEY>.lock */
static int lock_worktree(const char *wt)
{
	char *path = xstrfmt("%s.lock", pointer_path(wt));
	int fd = open(path, O_RDWR | O_CREAT, 0666);

	if (fd < 0 || flock(fd, LOCK_EX))
		write_error(path);
	return fd;
}

/* local state is written whole; a failure is E_IO (spec 5.7) */
static void write_state(const char *path, const char *text)
{
	char *tmp = xstrfmt("%s.%d.tmp", path, (int)getpid());
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666);

	if (fd < 0 || write_in_full(fd, text, strlen(text)) < 0 || close(fd) || rename(tmp, path))
		write_error(fd < 0 ? xstrndup(path, strrchr(path, '/') - path) : path);
}

static char *quoted(const char *s)
{
	struct strbuf sb = STRBUF_INIT;

	sq_quote_buf(&sb, s);
	return strbuf_detach(&sb, NULL);
}

/*
 * "agent NAME\nworktree PATH\n" (spec 4.2); returns the worktree or NULL.
 * S names a file, so only a session ID (S and 12 hex digits, 4.1) is looked up.
 */
static char *session_read(const char *s, char *name)
{
	struct strbuf sb = STRBUF_INIT;
	char *wt;

	if (!s || *s != 'S' || strlen(s) != 13 || strspn(s + 1, "0123456789abcdef") != 12 || strbuf_read_file(&sb, state_path(xstrfmt("sessions/%s", s)), 0) < 0)
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
	if (env && !wt && env == ptr) {
		int fd = lock_worktree(top);
		char *now = default_of(top);

		if (now && !strcmp(now, ptr))
			unlink(pointer_path(top));
		close(fd);
	} else if (env && !wt)
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
	char *mine, other[72];
	struct dirent *de;
	DIR *dir;
	size_t i;
	int fd;

	mkdir(state_path(""), 0777);
	mkdir(state_path("sessions"), 0777);
	mkdir(state_path("worktrees"), 0777);
	/* each session read once: ID -> "AGENT\nWORKTREE" */
	dir = opendir(state_path("sessions"));
	while (dir && (de = readdir(dir))) {
		char *wt = session_read(de->d_name, other);

		if (wt)
			string_list_insert(&ids, de->d_name)->util = xstrfmt("%s\n%s", other, wt);
	}
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
			const char *who = ids.items[i].util, *wt = strchr(who, '\n') + 1;

			if (strncmp(who, name, wt - 1 - who) || strlen(name) != (size_t)(wt - 1 - who))
				continue;
			warning("agent name '%s' is also used by session '%s' (worktree '%s')", name, ids.items[i].string, wt);
			advise("if '%s' is yours, close this one: 'ait session close %s', and use '%s' in '%s' with "
			       "AIT_SESSION=%s; otherwise close this one and open again with another name",
			       ids.items[i].string, sid, ids.items[i].string, wt, ids.items[i].string);
		}
		fd = lock_worktree(top);
		mine = default_of(top);
		if (session_read(mine, other) && strcmp(other, name)) {
			warning("this worktree is used by session '%s' (agent '%s')", mine, other);
			advise("close this one: 'ait session close %s', then open a session in a worktree of your own: "
			       "'ait session open --agent %s' there", sid, name);
		} else if (!session_read(mine, NULL)) {
			write_state(pointer_path(top), xstrfmt("%s\n", sid));
		}
		close(fd);
		printf("%s\n", sid);
	} else if (argc == 2 && !strcmp(argv[1], "list")) {
		for (i = 0; i < ids.nr; i++) {
			const char *who = ids.items[i].util, *wt = strchr(who, '\n') + 1, *d = default_of(wt);

			printf("session %s\nagent %.*s\nworktree %s\ndefault %s\nsubs 0\nlistening no\n\n", ids.items[i].string,
			       (int)(wt - 1 - who), who, wt, d && !strcmp(d, ids.items[i].string) ? "yes" : "no");
		}
	} else if ((argc == 2 || argc == 3) && !strcmp(argv[1], "close")) {
		const char *s = argc == 3 ? argv[2] : session(1);

		const char *wt = session_read(s, NULL);

		if (!string_list_has_string(&ids, s))
			reject(128, "ait session list", "no usable ait session: unknown session '%s'", s);
		fd = lock_worktree(wt);
		if (unlink(state_path(xstrfmt("sessions/%s", s))))
			write_error(state_path("sessions"));
		if (default_of(wt) && !strcmp(default_of(wt), s) && unlink(pointer_path(wt)))
			write_error(state_path("worktrees"));
		close(fd);
	} else {
		reject(129, NULL, "unknown session command");
	}
	return 0;
}

/*
 * spec 7.1.1: an executable hook where "ait", "hook" and NAME follow each other
 * with only blanks, quotes, commas or brackets between, on a line that is not a comment
 */
static int has_ait_hook(const char *path, const char *name)
{
	const char *sep = "[] \t'\",()[]";
	struct string_list lines = STRING_LIST_INIT_DUP;
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;
	regex_t re;
	int found = 0;

	if (access(path, X_OK) || strbuf_read_file(&sb, path, 0) < 0)
		return 0;
	regcomp(&re, xstrfmt("(^|%s)ait%s+hook%s+%s(%s|$)", sep, sep, sep, name, sep), REG_EXTENDED | REG_NOSUB);
	string_list_split(&lines, sb.buf, "\n", -1);
	for_each_string_list_item(item, &lines) {
		const char *l = item->string + strspn(item->string, " \t");

		found |= *l != '#' && !starts_with(l, "//") && !regexec(&re, l, 0, NULL, 0);
	}
	regfree(&re);
	return found;
}

/*
 * spec 7.1.1 and, for the compare-and-swap of 5.5, reference-transaction: the
 * hook rejects a commit whose branch moved since ait judged it (AIT_EXPECT)
 */
static const char *hooks[] = { "pre-commit", "pre-merge-commit", "commit-msg", "pre-push", "reference-transaction", NULL };

/* PATH is DIR or under it */
static int inside(const char *path, const char *dir)
{
	return starts_with(path, dir) && (!path[strlen(dir)] || path[strlen(dir)] == '/');
}

/* in the form of TOP and COMMONDIR: absolute, with "..", "." and symlinks resolved by git */
static char *hooks_dir(void)
{
	return line1(git("rev-parse --path-format=absolute --git-path hooks"));
}

static int cmd_init(int argc, const char **argv)
{
	char *dir = hooks_dir();
	int in_repo = inside(dir, top) && !inside(dir, commondir);
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
	t = head_tree();
	if (blob_at(t, ".ait/init") && !strcmp(tree_get(t, ".ait/init")->mode, "100644") &&
	    !strcmp(object(blob_at(t, ".ait/init"), 1, NULL, NULL), "version 1\n")) {
		puts("init exists");
		return 0;
	}
	precondition(1);
	/* spec 7.1.1-5: if another init won the race, its init file is ours too */
	if (publish(tree_with(t->oid, xstrfmt("100644 %s\t.ait/init\n", line1(git_run("version 1\n", NULL, NULL,
		    "hash-object -w --stdin")))), "[CONTRACT] ait init\n")) {
		const char *now = object("HEAD:.ait/init", 1, NULL, NULL);

		if (!now || strcmp(now, "version 1\n"))
			reject(1, "rerun this command", "HEAD moved while running; nothing changed");
		puts("init exists");
		return 0;
	}
	printf("init created %s\n", line1(git("rev-parse HEAD")));
	return 0;
}

/* spec 5.2a: a clone with records but without ait's hooks lets plain commits go unnoticed */
static void check_hooks(void)
{
	struct tree *t = head_tree();
	char *dir;
	size_t i;
	int k;

	for (i = 0; i < t->nr; i++)
		if (starts_with(t->e[i].path, ".ait/") && strcmp(t->e[i].path, ".ait/init"))
			break;
	if (i == t->nr)
		return;
	dir = hooks_dir();
	for (k = 0; hooks[k]; k++)
		if (!has_ait_hook(xstrfmt("%s/%s", dir, hooks[k]), hooks[k])) {
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

		if (sscanf(lines.items[i].string, "%1023s %47s", ref, oid) != 2 || strspn(oid, "0") == strlen(oid) ||
		    !object(xstrfmt("%s^{commit}", oid), 0, NULL, NULL))
			continue;
		validate_at(tree_load(oid));
	}
	return 0;
}

/*
 * spec 5.5: under git's ref lock, the commit that lands is the one ait judged:
 * its tree, on the branch ait judged, whose only parent is the commit ait
 * judged. Any other branch or a detached HEAD is refused.
 */
static int ref_transaction(const char *phase)
{
	const char *expect = getenv("AIT_EXPECT");
	char want_old[72], want_tree[72], want_ref[1024], moved[4096];
	struct strbuf in = STRBUF_INIT;
	struct string_list lines = STRING_LIST_INIT_DUP;
	struct string_list_item *item;

	if (!expect || strcmp(phase, "prepared"))
		return 0;
	/* "OLD TREE REF MOVED": what ait judged, and where to say the transaction was refused */
	if (sscanf(expect, "%71s %71s %1023s %4095s", want_old, want_tree, want_ref, moved) != 4) {
		error("bad AIT_EXPECT '%s'", expect);
		return 1;
	}
	strbuf_read(&in, 0, 0);
	string_list_split(&lines, in.buf, "\n", -1);
	for_each_string_list_item(item, &lines) {
		char old[72], new[72], ref[1024];
		int same = 1;

		if (sscanf(item->string, "%71s %71s %1023s", old, new, ref) != 3)
			continue;
		if (!strcmp(ref, want_ref)) {
			/* the commit's header starts "tree T\n", then a "parent P\n" line per parent */
			const char *got = object(new, 1, NULL, NULL);
			/* an unborn branch is all zeros, and its commit has no parent */
			int root = !want_old[strspn(want_old, "0")];
			char *want = xstrfmt("tree %s\n%s%s%s", want_tree, root ? "" : "parent ", root ? "" : want_old, root ? "" : "\n");

			same = got && !strcmp(old, want_old) && starts_with(got, want) && !starts_with(got + strlen(want), "parent ");
		} else if (!strcmp(ref, "HEAD") || starts_with(ref, "refs/heads/"))
			same = 0;
		if (!same) {
			write_file(moved, "%s", ref);
			return 1;
		}
	}
	return 0;
}

static void recheck_hint(struct record *r, struct tree *t);

/* spec 7.1.2-7.1.3: guard plain commits, validate merges */
static int cmd_hook(int argc, const char **argv)
{
	struct state hs = { 0 };
	struct string_list paths = STRING_LIST_INIT_DUP, gone = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct tree *ht, *t;
	size_t i;

	if (argc > 1 && !strcmp(argv[1], "pre-push"))
		return pre_push();
	if (argc < 2 || (strcmp(argv[1], "pre-commit") && strcmp(argv[1], "pre-merge-commit")))
		return 0;
	t = tree_load(line1(git_run(NULL, gindex, NULL, "write-tree")));
	if (!strcmp(argv[1], "pre-merge-commit") || git_op_in_progress()) {
		validate_at(t);
		return 0;
	}
	ht = head_tree();
	state_load(&hs, ht);
	tree_diff(ht, t, &paths);
	for (i = 0; i < paths.nr; i++)
		if (contract_path(&hs, paths.items[i].string))
			reject(1, xstrfmt("unstage '%s'; contracts change only through ait (see spec/ait-mini.md)",
					  paths.items[i].string),
			       "contract files cannot be changed by a plain commit: '%s'", paths.items[i].string);
	for (i = 0; i < hs.nr; i++)
		if (is_current(hs.r[i]) && !unusable(hs.r[i], ht) && !unverified(hs.r[i], ht) && unverified(hs.r[i], t))
			string_list_append(&gone, hs.r[i]->id)->util = hs.r[i];
	if (!gone.nr)
		return 0;
	/* the block of 7.1.2: its warning and contract lines are normative */
	warning("this commit leaves %d current contract(s) unverified", (int)gone.nr);
	for_each_string_list_item(item, &gone) {
		struct record *r = item->util;
		struct string_list_item *f;

		fprintf(stderr, "    %s %s %s%s (%s)\n", r->id, val(r, ROLE), shows(r, PROVIDER, ht, 1),
			code_found(r, t) ? "" : " (no code under its scope)", scope_path(val(r, CREATOR)));
		for_each_string_list_item(f, &r->f[FILES])
			if (is_def(f->string))
				fputs(def_text(ht, scope_path(f->string), "        "), stderr);
	}
	warning("their receivers are notified after this commit");
	advise("next time, to check and commit in one step, use 'ait commit' instead of 'git commit'");
	for_each_string_list_item(item, &gone) {
		advise("to change '%s' without a check, reopen it: 'ait future amend %s -m <why>'", item->string, item->string);
		recheck_hint(item->util, t);
	}
	return 0;
}

/* H7 and H8: check an unverified contract again, at its scope or where its code went */
static void recheck_hint(struct record *r, struct tree *t)
{
	if (code_found(r, t))
		advise("check '%s' again: run 'ait amend %s -m <why>' to get the tasks", r->id, r->id);
	else
		advise("'%s' has no code under its scope; if it was moved, run 'ait amend %s %s<new scope> -m <why>' "
		       "to get the tasks", r->id, r->id, *r->id == 'D' ? "- " : "");
}

/* spec 3.9: two scopes overlap; "DIR/" and "/" cover what is under them */
static int overlap(const char *a, const char *b)
{
	a = scope_path(a);
	b = scope_path(b);
	return !strcmp(a, "/") || !strcmp(b, "/") || !strcmp(a, b) ||
	       (ends_with(a, "/") && starts_with(b, a)) || (ends_with(b, "/") && starts_with(a, b));
}

/* the todo argument ARG names R: its ID, or a scope over its scopes or spec files (7.8.1) */
static int named(struct record *r, const char *arg)
{
	static const enum key ks[] = { CONSUMER, PROVIDER, FILES };
	struct string_list_item *item;
	size_t k;

	for (k = 0; k < ARRAY_SIZE(ks); k++)
		for_each_string_list_item(item, &r->f[ks[k]])
			if (overlap(arg, item->string))
				return 1;
	return !strcmp(arg, r->id);
}

/* "file PATH exec|prompt|input" lines of 7.8.2, each definition with its body under it */
static void file_lines(const char *prefix, struct record *r, struct tree *t)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &r->f[FILES]) {
		char *path = scope_path(item->string);

		if (is_def(item->string))
			printf("%s %s %s\n%s", prefix, path, engine_of(t, path), def_text(t, path, "    "));
		else
			printf("%s %s input\n", prefix, path);
	}
}

static void link_lines(struct state *s, struct record *r, struct tree *t)
{
	size_t j;

	for (j = 0; j < s->nr; j++) {
		struct record *l = s->r[j], *other;

		if (*l->id != 'u' || l->bad || !in_state(l, "undertaken") || !strstr(l->id, r->id))
			continue;
		other = link_end(s, l, *r->id == 'D');
		printf("%s %s %s %s\n", *r->id == 'O' ? "undertakes" : "undertaken-by", other->id, val(other, STATE),
		       scope_path(val(other, CREATOR)));
		if (*r->id != 'O' || !in_state(other, "future"))
			continue;
		printf("undertakes-provider %s %s\n", other->id, shows(other, PROVIDER, t, 0));
		file_lines(xstrfmt("undertakes-file %s", other->id), other, t);
	}
}

/* spec 7.8: my promises and my unverified demands, with what to do next */
static int cmd_todo(int argc, const char **argv)
{
	struct state s = { 0 };
	struct string_list index = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct tree *t = tree_load(line1(git("write-tree"))), *ht = head_tree();
	const char *me = session(0) ? agent : NULL;
	size_t i;
	int k;

	state_load(&s, ht);
	for (k = 1; k < argc; k++) {
		const char *a = argv[k];

		if (state_get(&s, a))
			continue;
		if (strlen(a) == 13 && strchr("DOX", *a))
			reject(128, NULL, "no such contract: '%s'", a);
		no_range(a);
		if (strcmp(a, "/") && !path_ok(ends_with(a, "/") ? xstrndup(a, strlen(a) - 1) : a))
			reject(128, NULL, "bad scope: '%s'", a);
	}
	for (i = 0; i < s.nr; i++) {
		struct record *r = s.r[i];
		int mine, future, unver, pick = 0;

		if (r->bad) {
			warning("skipping malformed '.ait/%s' at 'HEAD'", r->id);
			continue;
		}
		if (unusable(r, ht)) {
			warning("skipping '.ait/%s' at 'HEAD': %s", r->id, unusable(r, ht));
			continue;
		}
		if (*r->id == 'u' || !strcmp(r->id, "init"))
			continue;
		mine = me && !strcmp(scope_path(val(r, CREATOR)), me);
		future = in_state(r, "future");
		unver = is_current(r) && unverified(r, t);
		if (touches(r, ht, t))
			string_list_append(&index, r->id)->util = r;
		for (k = 1; k < argc; k++)
			pick |= state_get(&s, argv[k]) ? !strcmp(argv[k], r->id) : (future || unver) && named(r, argv[k]);
		if (argc == 1)
			pick = me ? mine && (future || (unver && *r->id == 'D')) : future || unver;
		if (!pick)
			continue;
		printf("id %s\nrole %s\nstate %s\ncreator %s\n", r->id, val(r, ROLE), val(r, STATE), scope_path(val(r, CREATOR)));
		for_each_string_list_item(item, &r->f[CONSUMER])
			printf("consumer %s\n", show(item->string, t, 0));
		for_each_string_list_item(item, &r->f[PROVIDER])
			printf("provider %s\n", show(item->string, t, is_current(r)));
		printf("%s", unver ? "unverified yes\n" : "");
		file_lines("file", r, ht);
		link_lines(&s, r, t);
		putchar('\n');
		if (unver)
			recheck_hint(r, t);
		else if (future && mine)
			advise("when ready, stage the code and run 'ait fulfill %s%s -m <why>' to get the tasks", r->id,
			       is_abstract(r) ? "=<scope>" : "");
	}
	if (index.nr)
		puts("index");
	for_each_string_list_item(item, &index)
		if (!code_found(item->util, t))
			printf("code-required %s\n", item->string);
	for_each_string_list_item(item, &index)
		printf("touch %s\n", item->string);
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
