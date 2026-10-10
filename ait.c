/*
 * ait-mini entry point: command table, git calls, messages, and the commands
 * that do not write requests (session, init, hook, todo).
 */
#include "ait.h"
#include "run-command.h"
#include "quote.h"
#include "setup.h"
#include <dirent.h>
#include <sys/file.h>
#include <regex.h>

const char *top, *gindex, *headref, *head, *gitdir, *commondir;
char *git_error;
char *git_cmd;
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

/* every hint goes through here: git's advise(), with the session prefix of 5.9a */
void suggest(const char *fmt, ...)
{
	struct strbuf sb = STRBUF_INIT;
	va_list ap;

	va_start(ap, fmt);
	strbuf_vaddf(&sb, fmt, ap);
	va_end(ap);
	advise("%s", prefixed(sb.buf));
}

/* git's error()/die() routines and a hint, with the exit code of spec 5.7 */
void reject(int code, const char *hint, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(code == 128 ? get_die_message_routine() : get_error_routine())(fmt, ap);
	va_end(ap);
	if (code == 129)
		fputs("usage: ait <command> [<args>]  (see spec/ait-mini.md)\n", stderr);
	if (hint)
		suggest("%s", hint);
	exit(code);
}

void unsupported(const char *what)
{
	reject(128, "see spec/ait-mini.md for what to do instead", "not supported in ait-mini: '%s'", what);
}

/*
 * spec 5.12: git runs on the repository and worktree ait settled at start,
 * named by their absolute paths, as git hands GIT_DIR and GIT_WORK_TREE to
 * what it runs; a bare repository, where only hooks run, has no worktree.
 * Before setup, git finds them from here.
 */
static void at_repository(struct strvec *args)
{
	if (gitdir)
		strvec_pushf(args, "--git-dir=%s", gitdir);
	if (top)
		strvec_pushl(args, xstrfmt("--work-tree=%s", top), "-C", top, NULL);
}

/*
 * Run git with ARGS (spec 5.12). IN feeds stdin and INDEX sets GIT_INDEX_FILE.
 * Without RC a failure is E_GIT with git's first stderr line.
 */
char *git_args(struct strvec *args, const char *in, const char *index, int *rc)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	struct strbuf out = STRBUF_INIT, err = STRBUF_INIT;
	const char *reason;
	size_t i;
	int code;

	strvec_pushl(&cmd.args, "git", "-c", "core.quotePath=false", "-c", "trailer.separators=:",
		     "-c", "core.commentChar=#", NULL);
	at_repository(&cmd.args);
	strvec_pushv(&cmd.args, args->v);
	if (index)
		strvec_pushf(&cmd.env, "GIT_INDEX_FILE=%s", index);
	code = pipe_command(&cmd, in, in ? strlen(in) : 0, &out, 0, &err, 0);
	/* git's first stderr line, its "fatal: " or "error: " left to the message ait writes around it */
	free(git_error);
	git_error = xstrndup(err.buf, strcspn(err.buf, "\n"));
	if (skip_prefix(git_error, "fatal: ", &reason) || skip_prefix(git_error, "error: ", &reason))
		memmove(git_error, reason, strlen(reason) + 1);
	for (i = 0; args->v[i] && *args->v[i] == '-'; i++)
		;
	free(git_cmd);
	git_cmd = xstrdup(args->v[i] ? args->v[i] : "");
	if (rc)
		*rc = code;
	else if (code)
		reject(128, top ? NULL : "run this command inside a git repository", "%s", git_failed());
	strbuf_release(&err);
	return strbuf_detach(&out, &git_out_len);
}

/* E_GIT (5.8): the git command that failed last, and its stderr's first line */
char *git_failed(void)
{
	return xstrfmt("git failed: 'git %s': %s", git_cmd, git_error);
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
		strvec_push(&cat.args, "git");
		at_repository(&cat.args);
		strvec_pushl(&cat.args, "cat-file", "--batch-command", NULL);
		cat.in = cat.out = -1;
		if (start_command(&cat))
			reject(128, NULL, "git failed: 'git cat-file': cannot run it");
		/* the pipes are ait's alone, not for the git commands and hooks it runs later */
		fcntl(cat.in, F_SETFD, FD_CLOEXEC);
		fcntl(cat.out, F_SETFD, FD_CLOEXEC);
		out = xfdopen(cat.out, "r");
	}
	if (write_in_full(cat.in, cmd, strlen(cmd)) < 0 || !fgets(line, sizeof(line), out))
		reject(128, NULL, "git failed: 'git cat-file': it stopped");
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
		reject(128, NULL, "git failed: 'git cat-file': it stopped");
	return buf;
}

char *line1(char *s)
{
	if (s)
		s[strcspn(s, "\n")] = 0;
	return s;
}

/* spec 1.10: six random bytes, from the system's CSPRNG as git takes them */
void newid(char *out, int prefix)
{
	unsigned char b[6];
	int i;

	if (csprng_bytes(b, sizeof(b), 0) < 0)
		die_errno("unable to get random bytes");
	out[0] = prefix;
	for (i = 0; i < 6; i++)
		sprintf(out + 1 + 2 * i, "%02x", b[i]);
}

/* spec 5.12: fix the toplevel and the hook's index, then clear what redirects git */
static void setup(int bare_ok)
{
	static const char *clear[] = { "GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_PREFIX",
				       "GIT_COMMON_DIR", "GIT_DIFF_OPTS", NULL };
	struct string_list lines = STRING_LIST_INIT_DUP;
	int i, major = 0, minor = 0, rc;
	char oid[48];

	/* ait runs on git: without it nothing can be read */
	if (sscanf(git_run(NULL, NULL, &rc, "version"), "git version %d.%d", &major, &minor) != 2)
		reject(128, "install git 2.36 or later", "cannot run git");
	/* "git cat-file --batch-command" came in 2.36 (git v2.53.0 Documentation/RelNotes/2.36.0.adoc:75) */
	if (major < 2 || (major == 2 && minor < 36))
		reject(128, "install git 2.36 or later", "git %d.%d is too old for ait", major, minor);
	/*
	 * the work tree last: a bare repository has none, and only a hook may run
	 * there (pre-push, 7.1.6); rev-parse prints what it answered before it stops
	 */
	string_list_split(&lines, git_run(NULL, NULL, &rc, "rev-parse --absolute-git-dir --path-format=absolute "
					  "--git-common-dir --show-object-format --show-toplevel"), "\n", -1);
	if (lines.nr < 4 || (rc && !bare_ok))
		reject(128, lines.nr < 4 ? "run this command inside a git repository" : NULL, "%s", git_failed());
	/* one value a line, and the state files of spec 4 hold a path a line */
	if (lines.nr > (rc ? 4u : 5u))
		unsupported("a repository path with a line feed");
	gitdir = lines.items[0].string;
	commondir = lines.items[1].string;
	/* object IDs are SHA-1 throughout ait-mini */
	if (strcmp(lines.items[2].string, "sha1"))
		unsupported(xstrfmt("object format %s", lines.items[2].string));
	top = rc ? NULL : lines.items[3].string;
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

/*
 * spec 4.6: the shared state of the worktree WT changes only under
 * worktrees/<KEY>.lock; the directories are made when missing (4.1). -1 when it cannot be held.
 */
int lock_worktree(const char *wt)
{
	int fd;

	mkdir(state_path(""), 0777);
	mkdir(state_path("worktrees"), 0777);
	fd = open(xstrfmt("%s.lock", pointer_path(wt)), O_RDWR | O_CREAT, 0666);
	if (fd >= 0 && flock(fd, LOCK_EX)) {
		close(fd);
		fd = -1;
	}
	return fd;
}

static int held(const char *wt)
{
	int fd = lock_worktree(wt);

	if (fd < 0)
		write_error(xstrfmt("%s.lock", pointer_path(wt)));
	return fd;
}

/* a file ait writes is written whole; a failure is E_IO (spec 5.7, 5.8) */
void write_state(const char *path, const char *text)
{
	char *tmp = xstrfmt("%s.%d.tmp", path, (int)getpid());
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666);

	if (fd < 0)
		write_error(xstrndup(path, strrchr(path, '/') - path));
	if (write_in_full(fd, text, strlen(text)) < 0 || close(fd) || rename(tmp, path)) {
		int saved = errno;

		unlink(tmp);
		errno = saved;
		write_error(path);
	}
}

/* the worktrees of this repository, as git lists them */
static struct string_list *worktrees(void)
{
	static struct string_list list = STRING_LIST_INIT_DUP;
	const char *p, *end, *path;

	if (list.nr)
		return &list;
	/* -z ends each line with NUL: a path may hold a line feed */
	for (p = git("worktree list --porcelain -z"), end = p + git_out_len; p < end; p += strlen(p) + 1)
		if (skip_prefix(p, "worktree ", &path))
			string_list_insert(&list, path);
	return &list;
}

/* spec 5.8 E_SESSION: the worktree WT is no longer there, or git no longer lists it */
static int gone(const char *wt)
{
	return access(wt, F_OK) || !string_list_has_string(worktrees(), wt);
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
		int fd = held(top);
		char *now = default_of(top);

		if (now && !strcmp(now, ptr))
			unlink(pointer_path(top));
		close(fd);
	} else if (env && !wt)
		reject(128, "ait session list", "no usable ait session: unknown session '%s'", env);
	else if (wt && strcmp(wt, top))
		reject(128, gone(wt) ? xstrfmt("its worktree is gone; close it: 'ait session close %s', then open a session with "
					       "the same name in the worktree of your branch: 'ait session open --agent %s'", env, agent) :
		       xstrfmt("cd %s", quoted(wt)), "no usable ait session: '%s' belongs to worktree '%s'", env, wt);
	if (!wt && need)
		reject(128, "find your session with 'ait session list', or open one: 'ait session open --agent <name>'",
		       "no usable ait session: none for this worktree");
	if (!wt)
		return NULL;
	snprintf(sid, sizeof(sid), "%s", env);
	return sid;
}

/*
 * 5.9a: hints name the session when it is not this worktree's default; that
 * is known before any step checks the session (5.13)
 */
static void session_prefix(void)
{
	const char *env = getenv("AIT_SESSION"), *ptr = top ? default_of(top) : NULL;
	char *wt = env ? session_read(env, NULL) : NULL;

	if (top && wt && !strcmp(wt, top) && (!ptr || strcmp(env, ptr)))
		cmd_prefix = xstrfmt("AIT_SESSION=%s ", env);
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

		if (!agent_ok(name))
			reject(129, NULL, "bad agent name '%s'", name);
		newid(sid, 'S');
		write_state(state_path(xstrfmt("sessions/%s", sid)), xstrfmt("agent %s\nworktree %s\n", name, top));
		for (i = 0; i < ids.nr; i++) {
			const char *who = ids.items[i].util, *wt = strchr(who, '\n') + 1;

			if (strncmp(who, name, wt - 1 - who) || strlen(name) != (size_t)(wt - 1 - who))
				continue;
			warning("agent name '%s' is also used by session '%s' (worktree '%s')", name, ids.items[i].string, wt);
			if (gone(wt))
				suggest("its worktree is gone; close it: 'ait session close %s', and keep this one if this worktree "
					"is on your branch, otherwise close this one too ('ait session close %s') and open it again "
					"in the worktree of your branch", ids.items[i].string, sid);
			else
				suggest("if '%s' is yours, close this one: 'ait session close %s', and use '%s' in '%s' with "
					"AIT_SESSION=%s; otherwise close this one and open again with another name",
					ids.items[i].string, sid, ids.items[i].string, wt, ids.items[i].string);
		}
		fd = held(top);
		mine = default_of(top);
		if (session_read(mine, other) && strcmp(other, name)) {
			warning("this worktree is used by session '%s' (agent '%s')", mine, other);
			suggest("close this one: 'ait session close %s', then open a session in a worktree of your own: "
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
		fd = held(wt);
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

/* spec 7.1.1 */
static const char *hooks[] = { "pre-commit", "pre-merge-commit", "commit-msg", "pre-push", NULL };

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
	struct string_list_item *wt;
	struct tree *t;
	int i, bad = 0, in_repo = 0;

	/* 7.1.1: inside any worktree of this repository and outside the git common directory */
	for_each_string_list_item(wt, worktrees())
		in_repo |= inside(dir, wt->string) && !inside(dir, commondir);

	if (argc > 1)
		reject(129, NULL, "init takes no arguments");
	mkdir(dir, 0777);
	for (i = 0; hooks[i]; i++) {
		char *path = xstrfmt("%s/%s", dir, hooks[i]);
		int rc = 0;

		if (access(path, F_OK) && !in_repo) {
			write_state(path, xstrfmt("#!/bin/sh\ncommand -v ait >/dev/null 2>&1 || exit 0\nexec ait hook %s \"$@\"\n",
						  hooks[i]));
			chmod(path, 0755);
			continue;
		}
		if (has_ait_hook(path, hooks[i]))
			continue;
		error("not allowed: hook '%s' is not set up for ait", path);
		suggest("make '%s' run 'ait hook %s' with its arguments and its stdin, and stop when that fails; git runs a hook "
		       "only if it is executable. In a shell hook, these lines after the first line do it ('#!/bin/sh' first "
		       "if the file is new):\n%s\nthen rerun this command", path, hooks[i], strcmp(hooks[i], "pre-push") ?
		       xstrfmt("if command -v ait >/dev/null 2>&1; then ait hook %s \"$@\" || exit $?; fi", hooks[i]) :
		       "t=$(mktemp) && cat > \"$t\" || exit 1\nif command -v ait >/dev/null 2>&1; then ait hook pre-push \"$@\" "
		       "< \"$t\" || { s=$?; rm -f \"$t\"; exit $s; }; fi\nexec < \"$t\"; rm -f \"$t\"");
		if (starts_with(path, xstrfmt("%s/", top)))
			git_run(NULL, NULL, &rc, "--no-literal-pathspecs check-ignore -q -- %s", path + strlen(top) + 1);
		if (starts_with(path, xstrfmt("%s/", top)) && rc)
			suggest("then commit it: stage '%s' and run 'ait commit -m <why>'", path + strlen(top) + 1);
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
		    "hash-object -w --stdin"))), NULL), "[CONTRACT] ait init\n", index_load())) {
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
			suggest("this clone is not set up for ait; run 'ait init'");
			return;
		}
}

/*
 * a pre-push record ends at E (its LF) when what is before holds
 * " LOCAL-OID SP REMOTE-REF SP REMOTE-OID": git writes "%s %s %s %s\n" (git
 * v2.53.0 transport.c:1355), LOCAL being what was given, spaces and line
 * feeds included, and REMOTE-REF a ref name, which holds neither; the local
 * object ID of that record, or NULL
 */
static char *push_record(const char *p, const char *e)
{
	const char *ref = e - 41;

	if (e - p < 85 || !oid_ok(xstrndup(e - 40, 40)) || *ref != ' ')
		return NULL;
	while (ref > p && ref[-1] != ' ' && ref[-1] != '\n')
		ref--;
	if (ref - p < 43 || ref[-1] != ' ' || ref[-42] != ' ')
		return NULL;
	return oid_ok(xstrndup(ref - 41, 40)) ? xstrndup(ref - 41, 40) : NULL;
}

/* spec 7.1.6: every pushed commit holds a valid state */
static int pre_push(void)
{
	struct strbuf in = STRBUF_INIT;
	const char *p, *e;
	char *oid;

	strbuf_read(&in, 0, 0);
	for (p = e = in.buf; (e = strchr(e, '\n')); e++) {
		if (!(oid = push_record(p, e)))
			continue;
		p = e + 1;
		/* a deletion pushes nothing; what does not peel to a commit has no contracts */
		if (strspn(oid, "0") != 40 && object(xstrfmt("%s^{commit}", oid), 0, NULL, NULL))
			validate_at(tree_load(oid));
	}
	if (*p)
		reject(128, NULL, "bad pre-push input '%s'", p);
	return 0;
}

static void recheck_hint(struct state *s, struct record *r, struct tree *h, int guard);

/*
 * spec 7.1.3: git stopped while bringing another commit into HEAD and has
 * not committed the result: a merge, cherry-pick, revert, or a rebase that
 * stopped at a commit. Any other operation in progress is no integration.
 */
int integrating(void)
{
	struct strbuf safe = STRBUF_INIT;
	char oid[48];

	if (!access(xstrfmt("%s/MERGE_HEAD", gitdir), F_OK) || object("CHERRY_PICK_HEAD", 0, NULL, NULL) ||
	    object("REVERT_HEAD", 0, NULL, NULL))
		return 1;
	if (!object("REBASE_HEAD", 0, NULL, NULL))
		return 0;
	if (!access(xstrfmt("%s/rebase-merge", gitdir), F_OK))
		return !access(xstrfmt("%s/MERGE_MSG", gitdir), F_OK);
	return strbuf_read_file(&safe, xstrfmt("%s/rebase-apply/abort-safety", gitdir), 0) >= 0 &&
	       object("HEAD", 0, oid, NULL) && !strcmp(line1(safe.buf), oid);
}

/* spec 7.1.2-7.1.3: guard plain commits, validate integrations */
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
	if (!strcmp(argv[1], "pre-merge-commit") || integrating()) {
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
	suggest("next time, to check and commit in one step, use 'ait commit' instead of 'git commit'");
	/* the reopen hint, then H7; H8 brings its own reopen hint after it */
	for_each_string_list_item(item, &gone) {
		if (code_found(item->util, t))
			suggest("to change '%s' without a check, reopen it: 'ait future amend %s -m <why>'", item->string,
				reopen_ids(&hs, item->util));
		recheck_hint(&hs, item->util, t, 1);
	}
	return 0;
}

/*
 * H7 and H8: check an unverified contract again, judged at HEAD's tree H
 * where the amend will run: at its scope, or where its code went, then reopen it.
 * The guard of 7.1.2 makes the commit H8 would name, so it names none.
 */
static void recheck_hint(struct state *s, struct record *r, struct tree *h, int guard)
{
	const char *since;

	if (code_found(r, h)) {
		suggest("check '%s' again: run 'ait amend %s -m <why>' to get the tasks", r->id, r->id);
		return;
	}
	since = guard ? NULL : unverified_since(r);
	suggest("'%s' has no code under its scope%s; if it was moved, run 'ait amend %s %s%s - -m <why>' to get the tasks",
		r->id, since ? xstrfmt(" (unverified since '%s')", since) : "", r->id, *r->id == 'D' ? "- " : "",
		moved_arg(r, h));
	suggest("to change '%s' without a check, reopen it: 'ait future amend %s -m <why>'", r->id, reopen_ids(s, r));
}

/* H10: fulfil my promise P, with a scope for P and each abstract future demand it undertakes, or where its code went */
static void fulfill_hint(struct state *s, struct record *p, struct tree *t)
{
	struct strbuf args = STRBUF_INIT;
	struct string_list_item *item;
	size_t j;

	for_each_string_list_item(item, &p->f[PROVIDER])
		if (!starts_with(item->string, "new:") && !ends_with(scope_path(item->string), "/") &&
		    !blob_at(t, scope_path(item->string))) {
			suggest("'%s' has no code under its scope; if it was moved, run 'ait fulfill %s=%s -m <why>' to get the tasks",
				p->id, p->id, moved_arg(p, t));
			suggest("if it is gone for good, run 'ait future cancel %s -m <why>'", p->id);
			return;
		}
	strbuf_addf(&args, "%s%s", p->id, is_abstract(p) ? "=<scope>" : "");
	for (j = 0; j < s->nr; j++) {
		struct record *d = partner(s, s->r[j], p);

		if (d && in_state(d, "future") && is_abstract(d))
			strbuf_addf(&args, " %s=<scope>", d->id);
	}
	suggest("when ready, stage the code and run 'ait fulfill %s -m <why>' to get the tasks", args.buf);
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
		struct record *other = partner(s, s->r[j], r);

		if (!other)
			continue;
		printf("%s %s %s %s\n", *r->id == 'O' ? "undertakes" : "undertaken-by", other->id, val(other, STATE),
		       scope_path(val(other, CREATOR)));
		if (*r->id != 'O' || !is_live(other))
			continue;
		if (in_state(other, "future"))
			printf("undertakes-provider %s %s\n", other->id, shows(other, PROVIDER, t, 0));
		file_lines(xstrfmt("undertakes-file %s", other->id), other, t);
	}
}

/* spec 7.8: my promises and my unverified demands, with what to do next */
static int cmd_todo(int argc, const char **argv)
{
	struct state s = { 0 }, hs = { 0 };
	struct string_list index = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct tree *t = tree_load(line1(git("write-tree"))), *ht = head_tree();
	const char *me = session(0) ? agent : NULL;
	size_t i;
	int k;

	/* spec 7.8.1: the contracts as the index T0 has them; the index block compares HEAD's with T0 */
	state_load(&s, t);
	state_load(&hs, ht);
	for (k = 1; k < argc; k++) {
		const char *a = argv[k];

		if (state_get(&s, a))
			continue;
		if (strlen(a) == 13 && strchr("DOX", *a))
			reject(128, NULL, "no such contract: '%s'", a);
		scope_shape(a);
	}
	for (i = 0; i < s.nr; i++) {
		struct record *r = s.r[i];
		int mine, future, unver, pick = 0;

		if (r->bad) {
			warning("skipping malformed '.ait/%s' at 'index'", r->id);
			continue;
		}
		if (unusable(r, t)) {
			warning("skipping '.ait/%s' at 'index': %s", r->id, unusable(r, t));
			continue;
		}
		if (*r->id == 'u' || !strcmp(r->id, "init"))
			continue;
		mine = me && !strcmp(scope_path(val(r, CREATOR)), me);
		future = in_state(r, "future");
		unver = is_current(r) && unverified(r, t);
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
		file_lines("file", r, t);
		link_lines(&s, r, t);
		putchar('\n');
		/* H7 and H8 are about K unverified in HEAD, where the amend runs; staged changes are commit's (7.7a) */
		if (unver && state_get(&hs, r->id) && is_current(state_get(&hs, r->id)) && unverified(state_get(&hs, r->id), ht))
			recheck_hint(&hs, state_get(&hs, r->id), ht, 0);
		/* H10: my future offer, or my demand that was reopened (mini counts what I made, 1.3) */
		else if (future && mine && (*r->id == 'O' || reopened(r)))
			fulfill_hint(&s, r, t);
	}
	/* 7.7.6: the current contracts of HEAD whose provider the staged change touches */
	for (i = 0; i < hs.nr; i++)
		if (!hs.r[i]->bad && touches(hs.r[i], ht, t))
			string_list_append(&index, hs.r[i]->id)->util = hs.r[i];
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
/* git v2.53.0 git.c:512-527 (run_builtin), with the message of 5.8 E_IO: a command's output reached stdout */
static int check_stdout(void)
{
	struct stat st;

	/* Somebody closed stdout? */
	if (fstat(fileno(stdout), &st))
		return 0;
	/* Ignore write errors for pipes and sockets.. */
	if (S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode))
		return 0;

	/* Check for ENOSPC and EIO errors.. */
	if (fflush(stdout))
		die_errno("write error: standard output");
	if (ferror(stdout))
		die("write error: standard output");
	if (fclose(stdout))
		die_errno("write error: standard output");
	return 0;
}

int main(int argc, const char **argv)
{
	size_t i;

	/* the standard descriptors are open before anything, as in git's common main (common-init.c:45) */
	sanitize_stdfds();

	if (argc < 2) {
		puts("usage: ait <command> [<args>]\n\nsee spec/ait-mini.md for the commands of ait-mini");
		return 1;
	}
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[1], commands[i].name))
			continue;
		if (!commands[i].fn)
			unsupported(argv[1]);
		setup(!strcmp(argv[1], "hook"));
		session_prefix();
		if (strcmp(argv[1], "init") && strcmp(argv[1], "hook"))
			check_hooks();
		return commands[i].fn(argc - 1, argv + 1) || check_stdout();
	}
	execvp("git", (char *const *)argv);
	die_errno("cannot run git");
}
