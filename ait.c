/*
 * ait entry point: command table, git calls, messages, and the commands
 * that do not write requests (session, init, hook, todo).
 */
#include "ait.h"
#include "run-command.h"
#include "quote.h"
#include "setup.h"
#include "precompose.h"
#include <dirent.h>
#include <sys/file.h>
#include <regex.h>

const char *top, *hook_index, *headref, *head, *gitdir, *commondir;
char *git_error;
char *git_cmd;
size_t git_out_len;
char agent[72], sid[16];

/* 5.9a and 7.12.6: execution hints retain a nondefault session. */
static char *cmd_prefix;

static char *prefixed(const char *hint)
{
	struct strbuf sb = STRBUF_INIT;
	const char *p, *hit;

	for (p = hint; cmd_prefix; p = hit + 1) {
		const char *ait = strstr(p, "'ait "), *git = strstr(p, "'git commit ");

		hit = ait && git ? (ait < git ? ait : git) : ait ? ait : git;
		if (!hit)
			break;
		strbuf_add(&sb, p, hit + 1 - p);
		strbuf_addstr(&sb, cmd_prefix);
	}
	strbuf_addstr(&sb, p);
	return strbuf_detach(&sb, NULL);
}

/* a hook of a commit is running: what it says is about the state after that commit (5.9a) */
static int commit_hook;

/* HINT runs an ait command that writes, which waits for a git operation in progress (5.2) */
static int ait_writes(const char *hint)
{
	static const char *const readers[] = { "todo", "peek", "session", "listen", "sub", "unsub", NULL };
	const char *p;

	for (p = hint; (p = strstr(p, "'ait ")); p++)
		if (!one_of(xstrndup(p + 5, strcspn(p + 5, " '")), readers))
			return 1;
	return 0;
}

/*
 * 5.9a: the git operation an ait write command would wait for; after a
 * hook's commit a merge or a single pick is over, a rebase or a sequence of
 * picks goes on
 */
static const char *op_ahead(void)
{
	const char *op = git_op();

	if (commit_hook && op && strcmp(op, "rebase") && access(xstrfmt("%s/sequencer/todo", gitdir), F_OK))
		return NULL;
	return op;
}

/*
 * every hint goes through here: git's advise(), each line once (5.9a), with
 * the session prefix of 5.9a, and after what a git operation in progress
 * needs before an ait write command
 */
void suggest(const char *fmt, ...)
{
	static struct string_list said = STRING_LIST_INIT_DUP;
	static int op_said;
	struct string_list lines = STRING_LIST_INIT_DUP;
	struct string_list_item *line;
	struct strbuf sb = STRBUF_INIT;
	const char *op;
	va_list ap;

	va_start(ap, fmt);
	strbuf_vaddf(&sb, fmt, ap);
	va_end(ap);
	string_list_split(&lines, sb.buf, "\n", -1);
	for_each_string_list_item(line, &lines) {
		if (string_list_has_string(&said, line->string))
			continue;
		string_list_insert(&said, line->string);
		if (!op_said && ait_writes(line->string) && (op = op_ahead())) {
			op_said = 1;
			advise("%s", op_guidance(xstrfmt("first finish or abort the git %s in progress ('git status')", op)));
		}
		advise("%s", prefixed(line->string));
	}
}

/* git's error()/die() routines and a hint, with the exit code of spec 5.7 */
void reject(int code, const char *hint, const char *fmt, ...)
{
	struct strbuf msg = STRBUF_INIT;
	va_list ap;
	size_t i;

	va_start(ap, fmt);
	strbuf_vaddf(&msg, fmt, ap);
	va_end(ap);
	/* 5.6: a message is one line, whatever a value in it holds; git's vreportf does so for other controls */
	for (i = 0; i < msg.len; i++)
		if ((unsigned char)msg.buf[i] < 0x20)
			msg.buf[i] = '?';
	if (code == 128)
		die_message("%s", msg.buf);
	else
		error("%s", msg.buf);
	if (code == 129)
		fputs("usage: ait <command> [<args>]\n", stderr);
	if (hint)
		suggest("%s", hint);
	exit(code);
}

void unsupported(const char *what)
{
	reject(128, NULL, "not supported: '%s'", what);
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
	if (top) {
		strvec_pushf(args, "--work-tree=%s", top);
		strvec_pushl(args, "-C", top, NULL);
	}
}

/* E_GIT (5.8): the git command that failed last, and its stderr's first line */
char *git_failed(void)
{
	return xstrfmt("git failed: 'git %s': %s", git_cmd, git_error);
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

	/* git's output as ait parses it, whatever the user's config would add to it or recode it to */
	strvec_pushl(&cmd.args, "git", "-c", "core.quotePath=false", "-c", "trailer.separators=:",
		     "-c", "core.commentChar=#", "-c", "log.showSignature=false", "-c", "i18n.logOutputEncoding=UTF-8",
		     /* ait's trailer keys as they are, whatever alias trailer.<token>.key gives them (user decision) */
		     "-c", "trailer.Ait-Request.key=Ait-Request",
		     "-c", "trailer.Ait-Agent.key=Ait-Agent", "-c", "trailer.Ait-Session.key=Ait-Session", NULL);
	at_repository(&cmd.args);
	strvec_pushv(&cmd.args, args->v);
	if (index)
		strvec_pushf(&cmd.env, "GIT_INDEX_FILE=%s", index);
	/* git's messages untranslated: ait's are English (5.6) */
	strvec_push(&cmd.env, "LANGUAGE=C");
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
		reject(128, NULL, "%s", git_failed());
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

/* git quotes unusual paths unless given -z (core.quotePath), so path listings are read as -z ones */
void z_paths(const char *out, struct string_list *paths)
{
	const char *p, *end = out + git_out_len;

	for (p = out; p < end; p += strlen(p) + 1)
		string_list_insert(paths, p);
}

/* the first line of S, cut in place: a git output of one value */
char *line1(char *s)
{
	if (s)
		s[strcspn(s, "\n")] = 0;
	return s;
}

/* the boolean setting KEY as git config reads it: 1 when true */
int config_true(const char *key)
{
	int rc;

	return !strcmp(line1(git_run(NULL, NULL, &rc, "config --bool %s", key)), "true");
}

/*
 * The object NAME resolves to, through one "git cat-file --batch-command" for
 * the whole command (git-cat-file(1), BATCH OUTPUT): its contents when
 * CONTENTS, else "", with its ID in OID and its size in SIZE; NULL when
 * NAME names no object.
 */
char *object(const char *name, int contents, char *oid, size_t *size)
{
	static struct child_process cat = CHILD_PROCESS_INIT;
	static FILE *out;
	char *cmd = xstrfmt("%s %s\n", contents ? "contents" : "info", name);
	char line[256], id[48], type[16], *buf;
	size_t n;

	if (!out) {
		strvec_push(&cat.args, "git");
		at_repository(&cat.args);
		strvec_pushl(&cat.args, "cat-file", "--batch-command", NULL);
		cat.in = cat.out = -1;
		/* what NAME does not resolve to is the "missing" line; git's note on stderr is not ait's to show */
		cat.no_stderr = 1;
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
		snprintf(oid, sizeof(id), "%s", id);
	if (size)
		*size = n;
	if (!contents)
		return xstrdup("");
	buf = xmallocz(n);
	if (fread(buf, 1, n, out) != n || fgetc(out) != '\n')
		reject(128, NULL, "git failed: 'git cat-file': it stopped");
	return buf;
}

/* S as one shell word, for the commands hints give (5.9a) */
char *quoted(const char *s)
{
	struct strbuf sb = STRBUF_INIT;

	sq_quote_buf(&sb, s);
	return strbuf_detach(&sb, NULL);
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
		snprintf(out + 1 + 2 * i, 3, "%02x", b[i]);
}

/* spec 5.12: check git; settle the repository, the worktree and the hook's index as absolute paths; clear what redirects git; read HEAD */
static void setup(int bare_ok)
{
	static const char *const clear[] = { "GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_PREFIX",
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
	if (lines.nr < 4)
		reject(128, "run this command inside a git repository", "%s", git_failed());
	if (rc && !bare_ok)
		reject(128, NULL, "%s", git_failed());
	/* one value a line, and the state files of spec 4 hold a path a line */
	if (lines.nr > (rc ? 4u : 5u))
		unsupported("a repository path with a line feed");
	gitdir = lines.items[0].string;
	commondir = lines.items[1].string;
	/* object IDs are SHA-1 throughout ait */
	if (strcmp(lines.items[2].string, "sha1"))
		unsupported(xstrfmt("object format %s", lines.items[2].string));
	top = rc ? NULL : lines.items[3].string;
	if (getenv("GIT_INDEX_FILE"))
		hook_index = realpath(getenv("GIT_INDEX_FILE"), NULL);
	for (i = 0; clear[i]; i++)
		unsetenv(clear[i]);
	setenv("GIT_LITERAL_PATHSPECS", "1", 1);
	headref = line1(git_run(NULL, NULL, &rc, "symbolic-ref -q HEAD"));
	if (rc)
		headref = NULL;
	if (object("HEAD^{commit}", 0, oid, NULL))
		head = xstrdup(oid);
}

static char *state_path(const char *name)
{
	return xstrfmt("%s/ait/%s", commondir, name);
}

/* spec 4.1: the directories of the shared state, made by what writes in them */
static void state_dirs(void)
{
	mkdir(state_path(""), 0777);
	mkdir(state_path("sessions"), 0777);
	mkdir(state_path("worktrees"), 0777);
}

/* spec 4.6: the default-session pointer of the worktree WT */
static char *pointer_path(const char *wt)
{
	static struct string_list hashed = STRING_LIST_INIT_DUP;
	struct string_list_item *hit = string_list_lookup(&hashed, wt);

	if (!hit) {
		hit = string_list_insert(&hashed, wt);
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
 * worktrees/<KEY>.lock; -1 when it cannot be held
 */
int lock_worktree(const char *wt)
{
	int fd;

	state_dirs();
	fd = open(xstrfmt("%s.lock", pointer_path(wt)), O_RDWR | O_CREAT, 0666);
	if (fd >= 0 && flock(fd, LOCK_EX)) {
		close(fd);
		fd = -1;
	}
	return fd;
}

static int hold_lock(const char *wt)
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

/*
 * "agent NAME\nworktree PATH\n" (spec 4.2); returns the worktree or NULL.
 * S names a file, so only a session ID (S and 12 hex digits, 4.1) is looked up.
 */
static char *session_read(const char *s, char *name)
{
	struct strbuf sb = STRBUF_INIT;
	char *wt;

	if (!sid_ok(s))
		return NULL;
	if (strbuf_read_file(&sb, state_path(xstrfmt("sessions/%s", s)), 0) < 0)
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
	const char *env = getenv("AIT_SESSION"), *dflt = default_of(top);
	int from_default = !env;
	char *wt;

	if (env && !strcmp(env, "-"))
		env = NULL;
	else if (!env)
		env = dflt;
	wt = session_read(env, agent);
	if (env && !wt && from_default) {
		/* the default pointer names no session: drop it, unless another command changed it meanwhile (5.1) */
		int fd = hold_lock(top);
		char *now = default_of(top);

		if (now && !strcmp(now, dflt))
			unlink(pointer_path(top));
		close(fd);
	} else if (env && !wt) {
		if (need < 0)
			return NULL;
		reject(128, "ait session list", "no usable ait session: unknown session '%s'", env);
	} else if (wt && strcmp(wt, top)) {
		char *hint = gone(wt) ? xstrfmt("its worktree is gone; close it: 'ait session close %s', then open a session "
						"with the same name in the worktree of your branch: 'ait session open --agent %s'",
						env, agent) : xstrfmt("cd %s", quoted(wt));

		if (need < 0)
			return NULL;
		reject(128, hint, "no usable ait session: '%s' belongs to worktree '%s'", env, wt);
	}
	if (!wt && need > 0)
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
	const char *env = getenv("AIT_SESSION"), *dflt = top ? default_of(top) : NULL;
	char *wt = env ? session_read(env, NULL) : NULL;

	if (top && wt && !strcmp(wt, top) && (!dflt || strcmp(env, dflt)))
		cmd_prefix = xstrfmt("AIT_SESSION=%s ", env);
}

/* a session as its file says (4.2) */
struct session_file {
	char agent[72];
	char *wt;
};

/* every session, each file read once: ID -> struct session_file */
static void read_sessions(struct string_list *ids)
{
	struct dirent *de;
	DIR *dir;

	state_dirs();
	if (!(dir = opendir(state_path("sessions"))))
		return;
	while ((de = readdir(dir))) {
		struct session_file f;

		if ((f.wt = session_read(de->d_name, f.agent)))
			string_list_insert(ids, de->d_name)->util = xmemdupz(&f, sizeof(f));
	}
	closedir(dir);
}

/* spec 7.2.1: a new session for the agent NAME in this worktree, its default unless another one is */
static void session_open(struct string_list *ids, const char *name)
{
	struct string_list_item *item;
	char *dflt, other[72];
	int fd;

	if (!agent_ok(name))
		reject(129, NULL, "bad agent name '%s'", name);
	newid(sid, 'S');
	write_state(state_path(xstrfmt("sessions/%s", sid)), xstrfmt("agent %s\nworktree %s\n", name, top));
	for_each_string_list_item(item, ids) {
		struct session_file *f = item->util;

		if (strcmp(f->agent, name))
			continue;
		warning("agent name '%s' is also used by session '%s' (worktree '%s')", name, item->string, f->wt);
		if (gone(f->wt))
			suggest("its worktree is gone; close it: 'ait session close %s', and keep this one if this worktree "
				"is on your branch, otherwise close this one too ('ait session close %s') and open it again "
				"in the worktree of your branch", item->string, sid);
		else
			suggest("if '%s' is yours, close this one: 'ait session close %s', and use '%s' in '%s' with "
				"AIT_SESSION=%s; otherwise close this one and open again with another name",
				item->string, sid, item->string, f->wt, item->string);
	}
	fd = hold_lock(top);
	dflt = default_of(top);
	if (!session_read(dflt, other)) {
		write_state(pointer_path(top), xstrfmt("%s\n", sid));
	} else if (strcmp(other, name)) {
		warning("this worktree is used by session '%s' (agent '%s')", dflt, other);
		suggest("close this one: 'ait session close %s', then open a session in a worktree of your own: "
			"'ait session open --agent %s' there", sid, name);
	}
	close(fd);
	printf("%s\n", sid);
}

/* spec 7.2.2 */
static void session_list(struct string_list *ids)
{
	struct string_list_item *item;

	for_each_string_list_item(item, ids) {
		struct session_file *f = item->util;
		const char *d = default_of(f->wt);

		printf("session %s\nagent %s\nworktree %s\ndefault %s\nsubs 0\nlistening no\n\n", item->string, f->agent,
		       f->wt, d && !strcmp(d, item->string) ? "yes" : "no");
	}
}

/* spec 7.2.3: the session S goes, and its worktree's default pointer with it if it points at S */
static void session_close(struct string_list *ids, const char *s)
{
	struct string_list_item *item = string_list_lookup(ids, s);
	struct session_file *f;
	char *dflt;
	int fd;

	if (!item)
		reject(128, "ait session list", "no usable ait session: unknown session '%s'", s);
	f = item->util;
	fd = hold_lock(f->wt);
	if (unlink(state_path(xstrfmt("sessions/%s", s))))
		write_error(state_path("sessions"));
	dflt = default_of(f->wt);
	if (dflt && !strcmp(dflt, s) && unlink(pointer_path(f->wt)))
		write_error(state_path("worktrees"));
	close(fd);
}

static int cmd_session(int argc, const char **argv)
{
	struct string_list ids = STRING_LIST_INIT_DUP;

	read_sessions(&ids);
	if (argc == 4 && !strcmp(argv[1], "open") && !strcmp(argv[2], "--agent"))
		session_open(&ids, argv[3]);
	else if (argc == 2 && !strcmp(argv[1], "list"))
		session_list(&ids);
	else if ((argc == 2 || argc == 3) && !strcmp(argv[1], "close"))
		session_close(&ids, argc == 3 ? argv[2] : session(1));
	else
		reject(129, NULL, "unknown session command");
	return 0;
}

/*
 * spec 7.1.1: an executable hook where "ait", "hook" and NAME follow each other
 * with only blanks, quotes, commas or brackets between, on a line that is not a comment
 */
static int has_ait_hook(const char *path, const char *name)
{
	const char *between = "[] \t'\",()[]";
	struct string_list lines = STRING_LIST_INIT_DUP;
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;
	regex_t re;
	int found = 0;

	if (access(path, X_OK) || strbuf_read_file(&sb, path, 0) < 0)
		return 0;
	regcomp(&re, xstrfmt("(^|%s)ait%s+hook%s+%s(%s|$)", between, between, between, name, between), REG_EXTENDED | REG_NOSUB);
	string_list_split(&lines, sb.buf, "\n", -1);
	for_each_string_list_item(item, &lines) {
		const char *l = item->string + strspn(item->string, " \t");

		found |= *l != '#' && !starts_with(l, "//") && !regexec(&re, l, 0, NULL, 0);
	}
	regfree(&re);
	return found;
}

/* spec 7.1.1 */
static const char *const hooks[] = { "pre-commit", "pre-merge-commit", "commit-msg", "pre-push", "post-commit", "post-merge", NULL };

/* PATH is DIR or under it */
static int inside(const char *path, const char *dir)
{
	return starts_with(path, dir) && (!path[strlen(dir)] || path[strlen(dir)] == '/');
}

/* the hooks directory, in the form of TOP and COMMONDIR so that inside() compares like with like: absolute, with "..", "." and symlinks resolved by git */
static char *hooks_dir(void)
{
	return line1(git("rev-parse --path-format=absolute --git-path hooks"));
}

/* the shell lines of 7.1.1 that make a hook of a user's run "ait hook NAME", stdin passed on for pre-push */
static char *hook_lines(const char *name)
{
	if (strcmp(name, "pre-push"))
		return xstrfmt("if command -v ait >/dev/null 2>&1; then ait hook %s \"$@\" || exit $?; fi", name);
	return xstrdup("t=$(mktemp) && cat > \"$t\" || exit 1\n"
		       "if command -v ait >/dev/null 2>&1; then ait hook pre-push \"$@\" < \"$t\" "
		       "|| { s=$?; rm -f \"$t\"; exit $s; }; fi\n"
		       "exec < \"$t\"; rm -f \"$t\"");
}

static int cmd_init(int argc, const char **argv)
{
	char *dir = hooks_dir(), *tree;
	struct strbuf info = STRBUF_INIT;
	struct string_list_item *wt;
	struct tree *t;
	int i, bad = 0, in_repo = 0;

	if (argc > 1)
		reject(129, NULL, "init takes no arguments");
	/* 7.1.1: inside any worktree of this repository and outside the git common directory */
	for_each_string_list_item(wt, worktrees())
		in_repo |= inside(dir, wt->string) && !inside(dir, commondir);
	mkdir(dir, 0777);
	for (i = 0; hooks[i]; i++) {
		char *path = xstrfmt("%s/%s", dir, hooks[i]);
		const char *in_tree;
		int not_ignored = 0;

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
			"if the file is new):\n%s\nthen rerun this command", path, hooks[i], hook_lines(hooks[i]));
		/*
		 * a hook in this worktree, outside the git directory, that git does not
		 * ignore is committed with ait; check-ignore fails for such a path
		 */
		if (!inside(path, commondir) && skip_prefix(path, xstrfmt("%s/", top), &in_tree))
			git_run(NULL, NULL, &not_ignored, "--no-literal-pathspecs check-ignore -q -- %s", in_tree);
		if (not_ignored)
			suggest("then commit it: stage '%s' and run 'ait commit -m <why>'", path);
		bad = 1;
	}
	mkdir(state_path(""), 0777);
	if (bad)
		return 1;
	t = head_tree();
	if (blob_at(t, ".ait/init") && !strcmp(tree_get(t, ".ait/init")->mode, "100644") &&
	    !strcmp(object(blob_at(t, ".ait/init"), 1, NULL, NULL), "version 2\n")) {
		puts("init exists");
		return 0;
	}
	precondition(1, 0);
	index_line(&info, "100644", line1(git_run("version 2\n", NULL, NULL, "hash-object -w --stdin")), ".ait/init");
	if (!(tree = tree_with(t->oid, info.buf, NULL)))
		reject(128, NULL, "%s", git_failed());
	/* spec 7.1.1-5: if another init won the race, its init file is ours too */
	if (publish(tree, "[CONTRACT] ait init\n", index_load(tree_load(line1(git("write-tree")))))) {
		const char *now = object("HEAD:.ait/init", 1, NULL, NULL);

		if (!now || strcmp(now, "version 2\n"))
			reject(1, "rerun this command", "HEAD moved while running; nothing changed");
		puts("init exists");
		return 0;
	}
	printf("init created %s\n", published);
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
 * The local object ID of the pre-push record from P that ends at E (its LF),
 * or NULL; its LOCAL into *LOCAL. What is before E holds " LOCAL-OID SP
 * REMOTE-REF SP REMOTE-OID": git writes "%s %s %s %s\n" (git v2.53.0
 * transport.c:1355), LOCAL being what was given, spaces and line feeds
 * included, and REMOTE-REF a ref name, which holds neither.
 */
static char *push_record(const char *p, const char *e, char **local)
{
	const char *ref = e - 41;
	char *oid;

	if (e - p < 85 || !oid_ok(xstrndup(e - 40, 40)) || *ref != ' ')
		return NULL;
	while (ref > p && ref[-1] != ' ' && ref[-1] != '\n')
		ref--;
	if (ref - p < 43 || ref[-1] != ' ' || ref[-42] != ' ')
		return NULL;
	oid = xstrndup(ref - 41, 40);
	*local = xstrndup(p, ref - 42 - p);
	return oid_ok(oid) ? oid : NULL;
}

/*
 * spec 7.1.6: every pushed commit holds a valid state. Only HEAD's branch is
 * mended here, as ait records only on a branch: LOCAL names HEAD or it.
 */
static int pre_push(void)
{
	struct strbuf in = STRBUF_INIT;
	const char *p, *e, *branch;
	char *oid, *local, commit[48];
	int detached;

	branch = line1(git_run(NULL, NULL, &detached, "symbolic-ref -q HEAD"));
	strbuf_read(&in, 0, 0);
	/* LOCAL may hold line feeds: a line that ends no record joins the next */
	for (p = e = in.buf; (e = strchr(e, '\n')); e++) {
		struct state s = { 0 };
		struct verdict v;

		if (!(oid = push_record(p, e, &local)))
			continue;
		p = e + 1;
		/* a deletion pushes nothing; what does not peel to a commit has no contracts */
		if (strspn(oid, "0") == 40 || !object(xstrfmt("%s^{commit}", oid), 0, commit, NULL))
			continue;
		state_load(&s, tree_load(commit));
		if (!check_state(&s, tree_load(commit), &v))
			continue;
		if (!detached && (!strcmp(local, "HEAD") || !strcmp(local, branch)))
			reject(v.code, head_repair(&v, NULL, 0), "%s", v.msg);
		reject(v.code, xstrfmt("'%s' (%s) cannot be fixed here; where that commit is HEAD on a branch, run "
				       "'ait contract commit -m <why>' to get the fix", local, commit), "%s", v.msg);
	}
	/* 5.6: a message is one line */
	if (*p)
		reject(128, NULL, "bad pre-push input '%.*s'", (int)strcspn(p, "\n"), p);
	return 0;
}

/*
 * spec 7.1.3: git stopped while bringing another commit into HEAD and has
 * not committed the result: the file that names what it brings, MERGE_HEAD,
 * CHERRY_PICK_HEAD, REVERT_HEAD, or REBASE_HEAD of a rebase that stopped at
 * a commit. Any other operation in progress is no integration (NULL).
 */
const char *integration_head(void)
{
	struct strbuf safe = STRBUF_INIT;

	/* a merge in progress is the file MERGE_HEAD; git writes it whether or not its contents resolve */
	if (!access(xstrfmt("%s/MERGE_HEAD", gitdir), F_OK))
		return "MERGE_HEAD";
	if (object("CHERRY_PICK_HEAD", 0, NULL, NULL))
		return "CHERRY_PICK_HEAD";
	if (object("REVERT_HEAD", 0, NULL, NULL))
		return "REVERT_HEAD";
	if (!object("REBASE_HEAD", 0, NULL, NULL))
		return NULL;
	/* a merge-backend rebase writes MERGE_MSG when it stops at a commit */
	if (!access(xstrfmt("%s/rebase-merge", gitdir), F_OK))
		return access(xstrfmt("%s/MERGE_MSG", gitdir), F_OK) ? NULL : "REBASE_HEAD";
	/* an apply-backend one stopped at a commit while abort-safety is still HEAD: git am has not committed */
	if (strbuf_read_file(&safe, xstrfmt("%s/rebase-apply/abort-safety", gitdir), 0) >= 0 && head &&
	    !strcmp(line1(safe.buf), head))
		return "REBASE_HEAD";
	return NULL;
}

/*
 * H7 and H8: check an unverified contract again, judged at HEAD's tree H
 * where the amend will run: at its scope, or where its code went, then reopen it.
 * H8 names where it became unverified in the history of TIPS, the judged
 * tree's; none (NULL) when the commit being made does it (7.1.2, 7.1.5).
 */
void recheck_hint(struct state *s, struct record *r, struct tree *h, const struct string_list *tips)
{
	const char *since;

	if (code_found(r, h)) {
		suggest("check '%s' again: run 'ait amend %s -m <why>' to get the tasks", r->id, r->id);
		return;
	}
	since = tips ? unverified_since(r, tips) : NULL;
	suggest("'%s' has no code under its scope%s; if it was moved, run 'ait %s -m <why>' to get the tasks",
		r->id, since ? xstrfmt(" (unverified since '%s')", since) : "", amend_clause(r, moved_arg(r, h), "-"));
	suggest("%s", reopen_hint(s, r));
}

/* H10: fulfil my promise P, with a scope for P and each abstract future demand it undertakes, or where its code went */
void fulfill_hint(struct state *s, struct record *p, struct tree *t)
{
	struct string_list missing = STRING_LIST_INIT_DUP;
	struct strbuf args = STRBUF_INIT;
	struct string_list_item *item;
	size_t j;

	missing_code(p, t, &missing);
	for_each_string_list_item(item, &missing)
		if (!starts_with(item->string, "new:")) {
			suggest("'%s' has no code under its scope; if it was moved, run 'ait fulfill %s=%s -m <why>' to get the tasks",
				p->id, p->id, moved_arg(p, t));
			/* only who made a contract takes it back (1.4) */
			if (made_by(p, *sid ? agent : NULL))
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

/*
 * spec 7.1.8: after a commit or merge from BEFORE (none for a root commit),
 * what no hook of ait judged before it: HEAD's new errors, and records put
 * back to older versions. Git does not look at the exit code; this only tells.
 */
static int post_hook(const char *before)
{
	struct state now = { 0 };
	struct string_list errors = STRING_LIST_INIT_DUP, old = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	size_t i;

	if (!head)
		return 0;
	state_load(&now, head_tree());
	commit_errors(head, &errors);
	if (before)
		commit_errors(before, &old);
	for_each_string_list_item(item, &errors) {
		if (string_list_has_string(&old, item->string))
			continue;
		warning("%s", ((struct verdict *)item->util)->msg);
		suggest("%s", head_repair(item->util, NULL, 0));
	}
	for (i = 0; before && i < now.nr; i++) {
		struct version *then = version_in(before, now.r[i]->id), *v_now = version_in(head, now.r[i]->id);

		if (!then || !v_now || then == v_now || !contains(then, v_now))
			continue;
		warning("this commit put '%s' back to an older version; only this branch has it", now.r[i]->id);
		/* reopening leaves a new version only from a current one (7.3.2); other versions have no such command */
		if (is_current(then->r))
			suggest("to take it back for others too, reopen it with ait once this commit is reverted: "
				"'ait future amend %s -m <why>'", now.r[i]->id);
	}
	return 0;
}

/* PATHS as shell words, each quoted only when the shell would read it (5.9a) */
char *shell_words(struct string_list *paths)
{
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;

	for_each_string_list_item(item, paths) {
		if (sb.len)
			strbuf_addch(&sb, ' ');
		sq_quote_buf_pretty(&sb, item->string);
	}
	return strbuf_detach(&sb, NULL);
}

/* "'A' 'B'": the paths named in a message or a hint's prose, as git names them */
char *quoted_paths(struct string_list *paths)
{
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;

	for_each_string_list_item(item, paths)
		strbuf_addf(&sb, "%s'%s'", sb.len ? " " : "", item->string);
	return strbuf_detach(&sb, NULL);
}

/* hints name their paths as one list (5.9a), even when there is one path */
struct string_list *single(const char *s)
{
	struct string_list *list = xcalloc(1, sizeof(*list));

	string_list_init_dup(list);
	string_list_append(list, s);
	return list;
}

/* 5.9a, the stage rule: contract PATHS are staged and committed through ait */
char *through_ait(struct string_list *paths)
{
	return xstrfmt("'ait contract add %s' and 'ait contract commit -m <why>'", shell_words(paths));
}

/*
 * 7.1.2, 5.8 E_GUARD: a plain commit of PATHS (HEAD to the hook's index)
 * leaves the contract paths of HS alone. A commit with -a or paths commits a
 * temporary index, which shows as blocked paths this worktree's index has not
 * staged; there unstaging does nothing, so the code is committed alone.
 */
static void guard(struct state *hs, struct string_list *paths)
{
	struct string_list blocked = STRING_LIST_INIT_DUP, code = STRING_LIST_INIT_DUP, staged = STRING_LIST_INIT_DUP;
	struct string_list held = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	const char *through;
	char *index;

	for_each_string_list_item(item, paths)
		string_list_append(contract_path(hs, item->string) ? &blocked : &code, item->string);
	if (!blocked.nr)
		return;
	/* the empty tree (git v2.53.0 hash.h) before the first commit */
	z_paths(git("diff --cached --name-only -z %s", head ? head : "4b825dc642cb6eb9a060e54bf8d69288fbee4904"), &staged);
	through = xstrfmt("commit them through ait: %s", through_ait(&blocked));
	/* a plain commit runs its hooks on this worktree's index, -a, -i and paths on another (git v2.53.0 builtin/commit.c:476) */
	index = realpath(line1(git("rev-parse --path-format=absolute --git-path index")), NULL);
	if (!hook_index || (index && !strcmp(hook_index, index)))
		reject(1, code.nr ? xstrfmt("unstage %s and commit the rest; then %s", quoted_paths(&blocked), through) : through,
		       "contract files cannot be changed by a plain commit: %s", quoted_paths(&blocked));
	/* 5.9a, local changes: contract paths this worktree's index has staged go out of the way first */
	for_each_string_list_item(item, &staged)
		if (contract_path(hs, item->string))
			string_list_append(&held, item->string);
	reject(1, xstrfmt("%scommit without -a or paths: stage the code (%s) and commit it, then %s",
			  held.nr ? xstrfmt("unstage %s first; ", quoted_paths(&held)) : "", quoted_paths(&code), through),
	       "contract files cannot be changed by a plain commit: %s", quoted_paths(&blocked));
}

/* spec 7.1.2-7.1.3 and 7.1.8: guard plain commits, validate integrations, tell what got past */
static int cmd_hook(int argc, const char **argv)
{
	struct state hs = { 0 };
	struct string_list paths = STRING_LIST_INIT_DUP, left = STRING_LIST_INIT_DUP, parents = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct tree *ht, *t;
	char before[48];
	size_t i;

	/* pre-push makes no commit, so an operation in progress goes on after it */
	if (argc > 1 && !strcmp(argv[1], "pre-push"))
		return pre_push();
	commit_hook = 1;
	if (argc > 2 && !strcmp(argv[1], "commit-msg")) {
		integration_message(argv[2]);
		return 0;
	}
	/* 7.1.8: the HEAD before is the first parent, or what git merge left in ORIG_HEAD */
	if (argc > 1 && (!strcmp(argv[1], "post-commit") || !strcmp(argv[1], "post-merge")))
		return post_hook(object(strcmp(argv[1], "post-commit") ? "ORIG_HEAD" : "HEAD^", 0, before, NULL) ? before : NULL);
	/* Other hook phases do not write records */
	if (argc < 2 || (strcmp(argv[1], "pre-commit") && strcmp(argv[1], "pre-merge-commit")))
		return 0;
	t = tree_load(line1(git_run(NULL, hook_index, NULL, "write-tree")));
	if (!strcmp(argv[1], "pre-merge-commit")) {
		integration_validate(t);
		return 0;
	}
	if (integration_head()) {
		integrate_at(t);
		return 0;
	}
	ht = head_tree();
	state_load(&hs, ht);
	tree_diff(ht, t, &paths);
	guard(&hs, &paths);
	/* 9.1 with the index as the commit and HEAD as its parent */
	if (head)
		string_list_append(&parents, head);
	for (i = 0; i < hs.nr; i++)
		if (!unusable(hs.r[i], ht) && newly_unverified(hs.r[i], t, &parents))
			string_list_append(&left, hs.r[i]->id)->util = hs.r[i];
	if (!left.nr)
		return 0;
	/* the block of 7.1.2: its warning and contract lines are normative */
	warning("this commit leaves %d current contract(s) unverified", (int)left.nr);
	for_each_string_list_item(item, &left) {
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
	/* the reopen hint, then H7 or H8, whose own reopen hint is said already */
	for_each_string_list_item(item, &left) {
		suggest("%s", reopen_hint(&hs, item->util));
		recheck_hint(&hs, item->util, t, NULL);
	}
	return 0;
}

/* spec 3.9: two scopes overlap; "DIR/" and "/" cover what is under them */
/* the scope ARG of todo overlaps one of R's scopes or spec files at T, a drop's by its target's (7.8.1) */
static int named(struct state *s, struct record *r, const char *arg, struct tree *t)
{
	static const enum key ks[] = { CONSUMER, PROVIDER, FILES };
	struct string_list_item *item;
	size_t k;

	if (*r->id == 'X')
		r = state_get(s, val(r, TARGET));
	for (k = 0; r && k < ARRAY_SIZE(ks); k++)
		for_each_string_list_item(item, &r->f[ks[k]])
			if (overlap(arg, item->string, t))
				return 1;
	return 0;
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

/* the shape of a contract's ID (1.10): D, O or X and 12 hex digits; anything else is taken as a scope */
static int id_shaped(const char *a)
{
	return id_like(a, "DOX");
}

/* a future offer undertakes D in T0's state S, or in HEAD's HS when S has no such offer (H5) */
static int undertaken_future(struct state *s, struct state *hs, struct record *d)
{
	struct record *in_head = state_get(hs, d->id);
	size_t j;

	for (j = 0; j < s->nr; j++) {
		struct record *o = partner(s, s->r[j], d);

		if (o && in_state(o, "future"))
			return 1;
	}
	for (j = 0; in_head && j < hs->nr; j++) {
		struct record *o = partner(hs, hs->r[j], in_head);

		if (o && in_state(o, "future") && !state_get(s, o->id))
			return 1;
	}
	return 0;
}

/*
 * 7.8.2: the hints of 5.9a that the block of R can judge now, R as T0 has it
 * in S and HEAD in HS: H7 and H8 for anyone, then for the agent ME H1, H2,
 * H5, H10 and H14. BY reopened R, as HEAD's history has it (1.5d).
 */
static void todo_hints(struct state *s, struct state *hs, struct record *r, struct tree *t, const char *me,
		       struct string_list *by)
{
	struct strbuf agents = STRBUF_INIT;
	struct record *in_head = state_get(hs, r->id);
	struct string_list_item *item;
	struct tree *ht = head_tree();
	size_t j;
	int made;

	/* H7 and H8 are about K unverified in HEAD, where the amend runs; staged changes are commit's (7.7a) */
	if (in_head && !in_head->bad && unverified(in_head, ht))
		recheck_hint(hs, in_head, ht, single(head));
	/* a drop is not written here (11), so its hints are not given */
	if (!me || !in_state(r, "future") || *r->id == 'X')
		return;
	made = made_by(r, me);
	for_each_string_list_item(item, by)
		strbuf_addf(&agents, "%s'%s'", agents.len ? ", " : "", item->string);
	/* H1 and H2: my offer undertakes a demand that is current or ended */
	for (j = 0; made && j < s->nr; j++) {
		struct record *d = partner(s, s->r[j], r);

		if (d && *r->id == 'O' && !in_state(d, "future"))
			suggest("if '%s' was only for '%s', run 'ait future cancel %s -m <why>'", r->id, d->id, r->id);
	}
	if (made && *r->id == 'D' && !by->nr && !undertaken_future(s, hs, r))
		suggest("if you need it now, undertake it yourself: 'ait future offer %s %s as o undertake %s o -m <why>'",
			scope_args(r, PROVIDER), def_paths(r), r->id);
	if (string_list_has_string(by, me) || (made && !by->nr && *r->id == 'O'))
		fulfill_hint(s, r, t);
	else if (made && by->nr)
		suggest("'%s' was reopened by %s; develop against it, or, if it is no longer needed, run 'ait future cancel %s -m <why>'",
			r->id, agents.buf, r->id);
}

/* spec 7.8: my promises and my unverified demands, with what to do next */
static int cmd_todo(int argc, const char **argv)
{
	struct state s = { 0 }, hs = { 0 };
	struct string_list touched = STRING_LIST_INIT_DUP;
	struct verdict v;
	struct string_list_item *item;
	struct tree *t, *ht = head_tree();
	const char *me;
	size_t i;
	int k;

	/* 5.13-1: the shapes of the arguments come before the session */
	for (k = 1; k < argc; k++)
		if (!id_shaped(argv[k]))
			scope_shape(argv[k]);
	t = tree_load(line1(git("write-tree")));
	me = session(0) ? agent : NULL;
	/* spec 7.8.1: the contracts as the index T0 has them; the index block compares HEAD's with T0 */
	state_load(&s, t);
	state_load(&hs, ht);
	for (k = 1; k < argc; k++)
		if (id_shaped(argv[k]) && !state_get(&s, argv[k]))
			reject(128, NULL, "no such contract: '%s'", argv[k]);
	/* 5.10: HEAD's accidents are shown as they are, their error as a warning */
	for (i = 0; i < hs.nr; i++)
		if (accident_at_head(hs.r[i], &v)) {
			warning("%s", v.msg);
			suggest("%s", v.hint);
		}
	for (i = 0; i < s.nr; i++) {
		struct record *r = s.r[i], *target;
		struct string_list by = STRING_LIST_INIT_DUP;
		int mine, future, unver, pick = 0;

		if (r->bad) {
			struct state one = { 1, 1, &r };
			const char *path = xstrfmt(".ait/%s", r->id);

			warning("skipping malformed '%s' at 'index'", path);
			/* 5.10: one HEAD has too is HEAD's error, mended as such */
			if (!strcmp(blob_at(t, path) ?: "", blob_at(ht, path) ?: "") && malformed(&one, &v))
				suggest("%s", head_repair(&v, NULL, 0));
			continue;
		}
		if (unusable(r, t)) {
			warning("skipping '.ait/%s' at 'index': %s", r->id, unusable(r, t));
			continue;
		}
		/* links and the init file are no contracts */
		if (*r->id == 'u' || !strcmp(r->id, "init"))
			continue;
		future = in_state(r, "future");
		unver = unverified(r, t);
		/* 7.8.1: my promise (1.3), or my demand */
		mine = my_promise(r, me, &by) || (unver && made_by(r, me));
		for (k = 1; k < argc; k++)
			pick |= id_shaped(argv[k]) ? !strcmp(argv[k], r->id) : (future || unver) && named(&s, r, argv[k], t);
		/* without arguments: my promises and my unverified demands; without a session every future and unverified current */
		if (argc == 1 && me)
			pick = mine && (future || (unver && *r->id == 'D'));
		else if (argc == 1)
			pick = future || unver;
		if (!pick)
			continue;
		printf("id %s\nrole %s\nstate %s\ncreator %s\n", r->id, val(r, ROLE), val(r, STATE), scope_path(val(r, CREATOR)));
		for_each_string_list_item(item, &r->f[CONSUMER])
			printf("consumer %s\n", show(item->string, t, 0));
		for_each_string_list_item(item, &r->f[PROVIDER])
			printf("provider %s\n", show(item->string, t, is_current(r)));
		/* 7.8.2: a drop shows its target and the scope it withdraws */
		if (*r->id == 'X')
			printf("target %s\n", val(r, TARGET));
		if (*r->id == 'X' && (target = state_get(&s, val(r, TARGET))))
			for_each_string_list_item(item, &target->f[PROVIDER])
				printf("scope %s\n", show(item->string, t, is_current(target)));
		if (unver)
			puts("unverified yes");
		file_lines("file", r, t);
		link_lines(&s, r, t);
		putchar('\n');
		todo_hints(&s, &hs, r, t, me, &by);
	}
	/* 7.8.2: the index block, the touch set of 7.7.6: the current contracts of HEAD whose provider the staged change touches */
	for (i = 0; i < hs.nr; i++)
		if (!hs.r[i]->bad && touches(hs.r[i], ht, t))
			string_list_append(&touched, hs.r[i]->id)->util = hs.r[i];
	if (touched.nr)
		puts("index");
	for_each_string_list_item(item, &touched)
		if (!code_found(item->util, t))
			printf("code-required %s\n", item->string);
	for_each_string_list_item(item, &touched)
		printf("touch %s\n", item->string);
	if (touched.nr)
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

/* after git v2.53.0 git.c: run the command from the table, hand the rest to git (spec 7.14) */
int main(int argc, const char **argv)
{
	size_t i;
	int status;

	/* the standard descriptors are open before anything, as in git's common main (common-init.c:45) */
	sanitize_stdfds();

	if (argc < 2) {
		puts("usage: ait <command> [<args>]");
		return 1;
	}
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[1], commands[i].name))
			continue;
		if (!commands[i].fn)
			unsupported(argv[1]);
		setup(!strcmp(argv[1], "hook"));
		/* as git's commands take their arguments on macOS: NFD spellings precomposed (core.precomposeUnicode) */
		precompose_argv_prefix(argc, argv, NULL);
		session_prefix();
		if (strcmp(argv[1], "init") && strcmp(argv[1], "hook"))
			check_hooks();
		status = commands[i].fn(argc - 1, argv + 1);
		if (status)
			return status;
		return check_stdout();
	}
	execvp("git", (char *const *)argv);
	die_errno("cannot run git");
}
