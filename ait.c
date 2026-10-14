/*
 * ait entry point: the command table, git calls, messages, sessions, the
 * contract branch and the checkin (spec 4), and init, checkout, switch (6.1-6.3).
 */
#include "ait.h"
#include "run-command.h"
#include "quote.h"
#include "precompose.h"
#include "setup.h"
#include <dirent.h>
#include <sys/file.h>

const char *top, *head, *gitdir, *commondir;
char *git_error;
char *not_recorded, *retry_command;
size_t git_out_len;
char agent[72], sid[16];
static char *git_cmd;

/* "AIT_SESSION=SID " when the session in use is not this worktree's default (set by session()) */
static char *cmd_prefix;

/* a hint line with the session named in each ait command it gives, whole or quoted */
static char *prefixed(const char *hint)
{
	struct strbuf sb = STRBUF_INIT;
	const char *p = hint, *hit;

	if (cmd_prefix && starts_with(hint, "ait "))
		strbuf_addstr(&sb, cmd_prefix);
	for (; cmd_prefix && (hit = strstr(p, "'ait ")); p = hit + 1) {
		strbuf_add(&sb, p, hit + 1 - p);
		strbuf_addstr(&sb, cmd_prefix);
	}
	strbuf_addstr(&sb, p);
	return strbuf_detach(&sb, NULL);
}

/* every hint goes through here: git's advise(), each line once */
void suggest(const char *fmt, ...)
{
	static struct string_list said = STRING_LIST_INIT_DUP;
	struct string_list lines = STRING_LIST_INIT_DUP;
	struct string_list_item *line;
	struct strbuf sb = STRBUF_INIT;
	va_list ap;

	va_start(ap, fmt);
	strbuf_vaddf(&sb, fmt, ap);
	va_end(ap);
	string_list_split(&lines, sb.buf, "\n", -1);
	for_each_string_list_item(line, &lines) {
		if (!*line->string || string_list_has_string(&said, line->string))
			continue;
		string_list_insert(&said, line->string);
		advise("%s", prefixed(line->string));
	}
}

/* a message is one line, whatever a value in it holds (v14 5.6); git's vreportf keeps LF and TAB */
static void report_line(const char *prefix, const char *fmt, va_list ap)
{
	struct strbuf msg = STRBUF_INIT;
	size_t i;

	strbuf_vaddf(&msg, fmt, ap);
	for (i = 0; i < msg.len; i++)
		if ((unsigned char)msg.buf[i] < 0x20)
			msg.buf[i] = '?';
	fprintf(stderr, "%s%s\n", prefix, msg.buf);
}

static void warn_line(const char *fmt, va_list ap)
{
	report_line("warning: ", fmt, ap);
}

/* git's fatal: and error: forms, the usage line of 129, and a hint (spec 4.7) */
void reject(int code, const char *hint, const char *fmt, ...)
{
	va_list ap;

	/* 6.7.5-3: after the code commit a refusal says what was not recorded, and only a retry is hinted */
	if (not_recorded) {
		code = 1;
		hint = retry_command;
	}
	va_start(ap, fmt);
	report_line(not_recorded ? xstrfmt("error: %s: ", not_recorded) : code == 128 ? "fatal: " : "error: ", fmt, ap);
	va_end(ap);
	if (code == 129)
		fputs("usage: ait <command> [<args>]\n", stderr);
	if (hint)
		suggest("%s", hint);
	exit(code);
}

/* git runs on the repository and worktree ait settled at start (v14 5.12) */
static void at_repository(struct strvec *args)
{
	if (gitdir)
		strvec_pushf(args, "--git-dir=%s", gitdir);
	if (top) {
		strvec_pushf(args, "--work-tree=%s", top);
		strvec_pushl(args, "-C", top, NULL);
	}
}

static char *git_failed(void)
{
	return xstrfmt("git failed: 'git %s': %s", git_cmd, git_error);
}

/* git with ARGS; IN feeds stdin, INDEX sets GIT_INDEX_FILE; without RC a failure is fatal (4.7) */
static char *git_args_n(struct strvec *args, const char *in, size_t in_len, const char *index, int *rc)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	struct strbuf out = STRBUF_INIT, err = STRBUF_INIT;
	const char *reason;
	size_t i;
	int code;

	/* git's output as ait parses it, whatever the user's config adds to it */
	strvec_pushl(&cmd.args, "git", "-c", "core.quotePath=false", "-c", "log.showSignature=false",
		     "-c", "i18n.logOutputEncoding=UTF-8", "-c", "trailer.separators=:", NULL);
	at_repository(&cmd.args);
	strvec_pushv(&cmd.args, args->v);
	if (index)
		strvec_pushf(&cmd.env, "GIT_INDEX_FILE=%s", index);
	/* git's messages untranslated: ait's are English (v14 5.6) */
	strvec_push(&cmd.env, "LANGUAGE=C");
	code = pipe_command(&cmd, in, in_len, &out, 0, &err, 0);
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

char *git_args(struct strvec *args, const char *in, const char *index, int *rc)
{
	return git_args_n(args, in, in ? strlen(in) : 0, index, rc);
}

static void words_of(struct strvec *args, const char *fmt, va_list ap)
{
	struct string_list words = STRING_LIST_INIT_DUP;
	size_t i;

	string_list_split(&words, fmt, " ", -1);
	for (i = 0; i < words.nr; i++)
		strvec_push(args, strcmp(words.items[i].string, "%s") ? words.items[i].string : va_arg(ap, const char *));
	string_list_clear(&words, 0);
}

/* git_run() fed the bytes of IN, NULs too (the -z inputs of update-index) */
static char *git_feed(const struct strbuf *in, const char *index, int *rc, const char *fmt, ...)
{
	struct strvec args = STRVEC_INIT;
	va_list ap;

	va_start(ap, fmt);
	words_of(&args, fmt, ap);
	va_end(ap);
	return git_args_n(&args, in->buf, in->len, index, rc);
}

/* git_args() with the words of FMT, each "%s" word taking one argument verbatim */
char *git_run(const char *in, const char *index, int *rc, const char *fmt, ...)
{
	struct strvec args = STRVEC_INIT;
	va_list ap;

	va_start(ap, fmt);
	words_of(&args, fmt, ap);
	va_end(ap);
	return git_args(&args, in, index, rc);
}

/* git with ARGS on ait's own stdout and stderr: what git says is what the user sees (spec 4.7) */
int git_pass(struct strvec *args)
{
	struct child_process cmd = CHILD_PROCESS_INIT;

	strvec_push(&cmd.args, "git");
	at_repository(&cmd.args);
	strvec_pushv(&cmd.args, args->v);
	fflush(stdout);
	if (start_command(&cmd))
		return -1;
	return finish_command(&cmd);
}

char *line1(char *s)
{
	if (s)
		s[strcspn(s, "\n")] = 0;
	return s;
}

/* the boolean setting KEY as git config reads it (precompose.c asks for core.precomposeUnicode) */
int config_true(const char *key)
{
	int rc;

	return !strcmp(line1(git_run(NULL, NULL, &rc, "config --bool %s", key)), "true");
}

/* the object NAME resolves to, through one "git cat-file --batch-command" (git-cat-file(1)) */
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
		cat.no_stderr = 1;
		if (start_command(&cat))
			reject(128, NULL, "git failed: 'git cat-file': cannot run it");
		fcntl(cat.in, F_SETFD, FD_CLOEXEC);
		fcntl(cat.out, F_SETFD, FD_CLOEXEC);
		out = xfdopen(cat.out, "r");
	}
	if (write_in_full(cat.in, cmd, strlen(cmd)) < 0 || !fgets(line, sizeof(line), out))
		reject(128, NULL, "git failed: 'git cat-file': it stopped");
	if (sscanf(line, "%47s %15s %zu", id, type, &n) != 3)
		return NULL;
	if (oid)
		snprintf(oid, 48, "%s", id);
	if (size)
		*size = n;
	if (!contents)
		return xstrdup(type);
	buf = xmallocz(n);
	if (fread(buf, 1, n, out) != n || fgetc(out) != '\n')
		reject(128, NULL, "git failed: 'git cat-file': it stopped");
	return buf;
}

/* S as one shell word for the commands hints give (v14 5.9a), quoted only where needed, as git shows a command */
char *quoted(const char *s)
{
	struct strbuf sb = STRBUF_INIT;

	sq_quote_buf_pretty(&sb, s);
	return strbuf_detach(&sb, NULL);
}

/* spec 1.11: six random bytes, from the system's CSPRNG as git takes them */
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

/* one of LETTERS and twelve lowercase hex digits (spec 1.11) */
int id_ok(const char *s, const char *letters)
{
	return s && *s && strchr(letters, *s) && strlen(s) == 13 && !s[1 + strspn(s + 1, "0123456789abcdef")];
}

/* v14 7.2.1: an agent name, [A-Za-z0-9._-]{1,64} and not "unknown" */
int agent_ok(const char *name)
{
	return *name && strlen(name) <= 64 && strcmp(name, "unknown") &&
	       !name[strspn(name, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-")];
}

/* the repository, its worktree and HEAD (v14 5.12); a bare repository has no contract commands (2.6.5) */
static void setup(void)
{
	static const char *const clear[] = { "GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_PREFIX",
				       "GIT_COMMON_DIR", NULL };
	struct string_list lines = STRING_LIST_INIT_DUP;
	int i, major = 0, minor = 0, rc;
	char oid[48];

	if (sscanf(git_run(NULL, NULL, &rc, "version"), "git version %d.%d", &major, &minor) != 2)
		reject(128, "install git 2.38 or later", "cannot run git");
	/* git merge-tree --write-tree came in 2.38 (git v2.53.0 Documentation/RelNotes/2.38.0.adoc) */
	if (major < 2 || (major == 2 && minor < 38))
		reject(128, "install git 2.38 or later", "git %d.%d is too old for ait", major, minor);
	string_list_split(&lines, git_run(NULL, NULL, &rc, "rev-parse --absolute-git-dir --path-format=absolute "
					  "--git-common-dir --show-object-format --show-toplevel"), "\n", -1);
	if (lines.nr < 4)
		reject(128, NULL, "not a git repository");
	if (rc)
		reject(128, NULL, "this operation must be run in a work tree");
	if (lines.nr > 5)
		reject(128, NULL, "not supported: a repository path with a line feed");
	gitdir = lines.items[0].string;
	commondir = lines.items[1].string;
	if (strcmp(lines.items[2].string, "sha1"))
		reject(128, NULL, "not supported: object format '%s'", lines.items[2].string);
	top = lines.items[3].string;
	for (i = 0; clear[i]; i++)
		unsetenv(clear[i]);
	setenv("GIT_LITERAL_PATHSPECS", "1", 1);
	if (object("HEAD^{commit}", 0, oid, NULL))
		head = xstrdup(oid);
}

/* the shared state under <git-common-dir>/ait/ (v14 4.1) */
char *state_path(const char *name)
{
	return xstrfmt("%s/ait/%s", commondir, name);
}

static void state_dirs(void)
{
	mkdir(state_path(""), 0777);
	mkdir(state_path("sessions"), 0777);
	mkdir(state_path("worktrees"), 0777);
}

static NORETURN void write_error(const char *path)
{
	reject(128, xstrfmt("make '%s' writable (free space, permissions), then rerun this command", path),
	       "write error: '%s': %s", path, strerror(errno));
}

/* a file ait writes is written whole (v14 5.7) */
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

/* v14 4.6: the default-session pointer of the worktree WT */
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

/*
 * v14 4.1: a lock file is opened, never removed, and held with flock(2);
 * -1 when NOWAIT finds it held
 */
int lock_path(const char *path, int nowait)
{
	int fd;

	state_dirs();
	if ((fd = open(path, O_RDWR | O_CREAT, 0666)) < 0)
		write_error(path);
	if (!flock(fd, LOCK_EX | (nowait ? LOCK_NB : 0)))
		return fd;
	if (!nowait)
		write_error(path);
	close(fd);
	return -1;
}

/* v14 4.2-4.5: the session S's files: SUFFIX "" its lines, ".lock", ".listen", ".seen" */
char *session_path(const char *s, const char *suffix)
{
	return state_path(xstrfmt("sessions/%s%s", s, suffix));
}

void session_lines(const char *s, struct string_list *lines)
{
	struct strbuf sb = STRBUF_INIT;

	string_list_clear(lines, 0);
	strbuf_read_file(&sb, session_path(s, ""), 0);
	string_list_split(lines, sb.buf, "\n", -1);
}

void session_write(const char *s, struct string_list *lines)
{
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;

	for_each_string_list_item(item, lines)
		if (*item->string)
			strbuf_addf(&sb, "%s\n", item->string);
	write_state(session_path(s, ""), sb.buf);
}

/* v14 4.6: the worktree WT's lock */
static int hold_lock(const char *wt)
{
	return lock_path(xstrfmt("%s.lock", pointer_path(wt)), 0);
}

static struct string_list *worktrees(void)
{
	static struct string_list list = STRING_LIST_INIT_DUP;
	const char *p, *end, *path;

	if (list.nr)
		return &list;
	for (p = git("worktree list --porcelain -z"), end = p + git_out_len; p < end; p += strlen(p) + 1)
		if (skip_prefix(p, "worktree ", &path))
			string_list_insert(&list, path);
	return &list;
}

static int gone(const char *wt)
{
	return access(wt, F_OK) || !string_list_has_string(worktrees(), wt);
}

/* "agent NAME\nworktree PATH\n..." (v14 4.2): the worktree, or NULL */
static char *session_read(const char *s, char *name)
{
	struct strbuf sb = STRBUF_INIT;
	char *wt;

	if (!id_ok(s, "S") || strbuf_read_file(&sb, session_path(s, ""), 0) < 0)
		return NULL;
	wt = strstr(sb.buf, "\nworktree ");
	if (!starts_with(sb.buf, "agent ") || !wt)
		return NULL;
	if (name)
		snprintf(name, 72, "%.*s", (int)strcspn(sb.buf + 6, "\n"), sb.buf + 6);
	return line1(wt + 10);
}

/* v14 5.1: AIT_SESSION, else this worktree's default session; NEED 1 requires one */
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
		int fd = hold_lock(top);
		char *now = default_of(top);

		if (now && !strcmp(now, dflt))
			unlink(pointer_path(top));
		close(fd);
	} else if (env && !wt) {
		reject(128, "ait session list", "no usable ait session: unknown session '%s'", env);
	} else if (wt && strcmp(wt, top)) {
		char *hint = gone(wt) ? xstrfmt("its worktree is gone; close it: 'ait session close %s', then open a session "
						"with the same name in the worktree of your branch: 'ait session open --agent %s'",
						env, agent) : xstrfmt("use it in its worktree '%s'", wt);

		reject(128, hint, "no usable ait session: '%s' belongs to worktree '%s'", env, wt);
	}
	if (!wt && need)
		reject(128, "find your session with 'ait session list', or open one: 'ait session open --agent <name>'",
		       "no usable ait session: none for this worktree");
	if (!wt)
		return NULL;
	snprintf(sid, sizeof(sid), "%s", env);
	/* v14 5.9a: hints name the session when it is not this worktree's default */
	if (!from_default && (!dflt || strcmp(env, dflt)))
		cmd_prefix = xstrfmt("AIT_SESSION=%s ", env);
	return sid;
}

struct session_file {
	char agent[72];
	char *wt;
};

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

/* v14 7.2.1 */
static void session_open(struct string_list *ids, const char *name)
{
	struct string_list_item *item;
	char *dflt, other[72];
	int fd;

	if (!agent_ok(name))
		reject(129, NULL, "bad agent name '%s'", name);
	newid(sid, 'S');
	write_state(session_path(sid, ""), xstrfmt("agent %s\nworktree %s\n", name, top));
	session_tips(sid, NULL, NULL, NULL);
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
	if (!session_read(dflt, other))
		write_state(pointer_path(top), xstrfmt("%s\n", sid));
	close(fd);
	printf("%s\n", sid);
}

/* v14 7.2.2: the sub lines and the listener's lock are in the session's files (9.1) */
static void session_list(struct string_list *ids)
{
	struct string_list_item *item;

	for_each_string_list_item(item, ids) {
		struct session_file *f = item->util;
		struct strbuf subs = STRBUF_INIT;
		struct string_list lines = STRING_LIST_INIT_DUP;
		struct string_list_item *line;
		const char *d = default_of(f->wt);
		int fd = lock_path(session_path(item->string, ".listen"), 1), n = 0, listening = fd < 0;

		if (fd >= 0)
			close(fd);
		session_lines(item->string, &lines);
		for_each_string_list_item(line, &lines)
			if (starts_with(line->string, "sub ")) {
				strbuf_addf(&subs, "%s\n", line->string);
				n++;
			}
		printf("session %s\nagent %s\nworktree %s\ndefault %s\nsubs %d\n%slistening %s\n\n", item->string,
		       f->agent, f->wt, d && !strcmp(d, item->string) ? "yes" : "no", n, subs.buf,
		       listening ? "yes" : "no");
	}
}

/* v14 7.2.3 */
static void session_close(struct string_list *ids, const char *s)
{
	struct string_list_item *item = string_list_lookup(ids, s);
	struct session_file *f;
	char *dflt;
	int fd;

	if (!item)
		reject(128, "ait session list", "no usable ait session: unknown session '%s'", s);
	f = item->util;
	/* v14 4.5: the worktree's lock, the session's, then the listener's tried */
	fd = hold_lock(f->wt);
	lock_path(session_path(s, ".lock"), 0);
	if (lock_path(session_path(s, ".listen"), 1) < 0) {
		struct strbuf pid = STRBUF_INIT;

		strbuf_read_file(&pid, session_path(s, ".listen"), 0);
		reject(128, xstrfmt("the listener (pid %s) stopped", line1(pid.buf)),
		       "cannot run now: a listener is running for session '%s'", s);
	}
	if (unlink(session_path(s, "")))
		write_error(state_path("sessions"));
	unlink(session_path(s, ".seen"));
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
 * spec 2.1.2: the names of the local ait branches and of the remote-tracking
 * ones, matched against the configured remotes, whose names may hold '/'
 */
void names_load(struct names *n)
{
	struct string_list refs = STRING_LIST_INIT_DUP;
	struct string_list_item *ref, *r;

	string_list_init_dup(&n->local);
	string_list_init_dup(&n->remote);
	string_list_init_dup(&n->remotes);
	string_list_split(&n->remotes, git("remote"), "\n", -1);
	string_list_split(&refs, git("for-each-ref --format=%(refname) refs/heads/ait/ refs/remotes/"), "\n", -1);
	for_each_string_list_item(ref, &refs) {
		const char *rest;
		const char *best = NULL;

		if (skip_prefix(ref->string, "refs/heads/ait/", &rest) && *rest) {
			string_list_insert(&n->local, xstrfmt("ait/%s", rest));
			continue;
		}
		if (!skip_prefix(ref->string, "refs/remotes/", &rest))
			continue;
		/* the longest configured remote that names the start, then "/ait/" */
		for_each_string_list_item(r, &n->remotes) {
			size_t len = strlen(r->string);

			if (*r->string && !strncmp(rest, r->string, len) && starts_with(rest + len, "/ait/") &&
			    rest[len + 5] && (!best || len > strlen(best)))
				best = r->string;
		}
		if (best) {
			struct string_list_item *item = string_list_insert(&n->remote,
				xstrfmt("ait/%s", rest + strlen(best) + 5));

			if (!item->util)
				item->util = xcalloc(1, sizeof(struct string_list));
			string_list_append(item->util, ref->string);
		}
	}
}

/*
 * spec 6.1-3: the remote-tracking ref ait init makes the local NAME from:
 * the one remote that has it, or the one checkout.defaultRemote names (git's
 * DWIM, builtin/checkout.c); NULL when none or more than one
 */
static const char *init_source(struct names *n, const char *name)
{
	struct string_list_item *item = string_list_lookup(&n->remote, name);
	struct string_list *refs;
	char *dflt;
	size_t i;
	int rc;

	if (!item)
		return NULL;
	refs = item->util;
	if (refs->nr == 1)
		return refs->items[0].string;
	dflt = line1(git_run(NULL, NULL, &rc, "config --get checkout.defaultRemote"));
	for (i = 0; !rc && i < refs->nr; i++)
		if (!strcmp(refs->items[i].string, xstrfmt("refs/remotes/%s/%s", dflt, name)))
			return refs->items[i].string;
	return NULL;
}

/* the absolute path git gives NAME in this worktree's git directory (it honours core.hooksPath and the like) */
char *git_path(const char *name)
{
	return line1(git("rev-parse --path-format=absolute --git-path %s", name));
}

/* A is B or an ancestor of it (git merge-base --is-ancestor) */
int is_ancestor(const char *a, const char *b)
{
	int rc;

	git_run(NULL, NULL, &rc, "merge-base --is-ancestor %s %s", a, b);
	return !rc;
}

/* spec 6.5.6-2, 9.2: the upstream of BRANCH when it is a remote-tracking ref, or NULL */
char *upstream_of(const char *branch)
{
	int rc;
	char *up = line1(git_run(NULL, NULL, &rc, "rev-parse -q --verify --symbolic-full-name %s",
				 xstrfmt("%s@{upstream}", branch)));

	return !rc && starts_with(up, "refs/remotes/") ? up : NULL;
}

/* the directories that hold PATH, made as needed (git's safe_create_leading_directories) */
void leading_dirs(const char *path)
{
	const char *slash;

	for (slash = strchr(path + 1, '/'); slash; slash = strchr(slash + 1, '/'))
		mkdir(xstrndup(path, slash - path), 0777);
}

/* spec 2.6.1: the worktree's checkout file */
char *checkout_file(void)
{
	return git_path("ait-branch");
}

static char *checkout_read(void)
{
	struct strbuf sb = STRBUF_INIT;

	return strbuf_read_file(&sb, checkout_file(), 0) < 0 ? NULL : line1(strbuf_detach(&sb, NULL));
}

/* the "ait init" hint of 4.3: only when init can make a local branch */
static const char *init_hint(struct names *n)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &n->remote)
		if (!string_list_has_string(&n->local, item->string) && init_source(n, item->string))
			return "ait init";
	return NULL;
}

/*
 * spec 4.2, 4.3 0-2: the contract branch this worktree works with; NULL with
 * the refusal in MSG and HINT (listen goes on after one, 6.8)
 */
const char *branch_find(const char **msg, struct strbuf *hint)
{
	struct names n;
	struct string_list_item *item;
	char *chosen;
	int others = 0;

	names_load(&n);
	strbuf_reset(hint);
	if (!n.local.nr) {
		*msg = n.remote.nr ? "no local ait branch" : "no ait branch";
		strbuf_addstr(hint, n.remote.nr && !init_hint(&n) ? "" : "ait init");
		return NULL;
	}
	chosen = checkout_read();
	if (chosen && string_list_has_string(&n.local, chosen))
		return chosen;
	/* 2.1.2: one name, counting the remote-tracking names too, so a DWIM-made branch is not taken silently */
	for_each_string_list_item(item, &n.remote)
		others |= !string_list_has_string(&n.local, item->string);
	if (!chosen && n.local.nr == 1 && !others)
		return n.local.items[0].string;
	for_each_string_list_item(item, &n.local)
		strbuf_addf(hint, "ait checkout %s\n", item->string);
	if (init_hint(&n))
		strbuf_addstr(hint, "ait init\n");
	*msg = chosen ? xstrfmt("contract branch '%s' does not exist", chosen) :
			"more than one ait branch; none is checked out";
	return NULL;
}

const char *branch_name(void)
{
	static const char *name;
	struct strbuf hint = STRBUF_INIT;
	const char *msg;

	if (!name && !(name = branch_find(&msg, &hint)))
		reject(128, hint.len ? hint.buf : NULL, "%s", msg);
	return name;
}

/*
 * spec 4.3-4, 4.4-3 (a): whether a worktree uses BRANCH, judged by git as it
 * judges a fetch into a checked-out branch (builtin/fetch.c:1382), with its words
 */
static void refuse_used(const char *branch, const char *commit)
{
	int rc;

	git_run(NULL, NULL, &rc, "fetch -q --dry-run --no-write-fetch-head --no-recurse-submodules . %s",
		xstrfmt("%s:refs/heads/%s", commit, branch));
	if (rc == 128)
		reject(128, NULL, "%s", git_error);
}

/*
 * spec 4.3, then 4.6: what every contract command does first, in one place
 * (git refuses a broken state at each command's start with one function);
 * the contract branch it works with
 */
/* spec 4.3-3: a tip with a malformed file is refused (or, for status, warned of), wherever it is met */
static void refuse_malformed(struct branch *b, int warn)
{
	struct string_list_item *e;

	for_each_string_list_item(e, &b->errors) {
		/* every error is "'<path>': <why>" */
		int path = strstr(e->string, "': ") - e->string + 1;
		char *line = xstrfmt("bad format: %.*s at '%s': %s", path, e->string, b->name, e->string + path + 2);

		if (warn)
			warning("%s", line);
		else
			reject(128, xstrfmt("fix %.*s in '%s' (enter it with 'ait switch %s')", path, e->string,
					    b->name, b->name), "%s", line);
	}
}

const char *start(int how)
{
	struct branch *b = branch_load(branch_name(), NULL, NULL);

	refuse_malformed(b, how & START_WARNS);
	if ((how & START_WRITES) && *b->tip)
		refuse_used(b->name, b->tip);
	if ((how & START_FULFILS) &&
	    starts_with(line1(git_run(NULL, NULL, NULL, "rev-parse --symbolic-full-name HEAD")), "refs/heads/ait/"))
		reject(128, NULL, "HEAD is on contract branch '%s'", line1(git("rev-parse --abbrev-ref HEAD")));
	if ((how & START_FULFILS) && *git("ls-files --unmerged"))
		reject(1, NULL, "fulfilling is not possible because you have unmerged files");
	session(!!(how & START_SESSION));
	return b->name;
}

/* the trailers of a checkin commit (spec 4.5, v14 5.4) */
static char *checkin_message(struct checkin *c)
{
	struct strbuf sb = STRBUF_INIT;

	strbuf_addstr(&sb, c->msg);
	strbuf_complete_line(&sb);
	if (!c->no_trailers)
		strbuf_addf(&sb, "\nAit-Request: %s\nAit-Agent: %s\nAit-Session: %s\n", c->rid, agent, sid);
	return strbuf_detach(&sb, NULL);
}

/*
 * the tree of TREEISH with the entries INFO ("update-index -z --index-info"
 * lines) put in, through a temporary index: git write-tree writes only an index
 */
char *tree_with(const char *treeish, const struct strbuf *info)
{
	char *dir = xstrfmt("%s/ait-index-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
	char *index, *tree;

	if (!mkdtemp(dir))
		write_error(dir);
	index = xstrfmt("%s/index", dir);
	git_run(NULL, index, NULL, "read-tree %s", treeish);
	git_feed(info, index, NULL, "update-index -z --index-info");
	tree = line1(git_run(NULL, index, NULL, "write-tree"));
	unlink(index);
	rmdir(dir);
	return tree;
}

/* the tree of T with C's files put in (spec 4.4-2) */
static char *checkin_tree(struct checkin *c)
{
	struct strbuf info = STRBUF_INIT;
	struct string_list_item *item;

	for_each_string_list_item(item, &c->put)
		strbuf_addf(&info, "100644 %s\t%s%c", line1(git_run(item->util, NULL, NULL, "hash-object -w --stdin")),
			    item->string, 0);
	return tree_with(*c->tip ? c->tip : EMPTY_TREE, &info);
}

/*
 * spec 4.10: a new tree may not add violations of the structural
 * invariants; those of T stay as they were (a git merge made them)
 */
static void checkin_invariants(struct checkin *c, struct branch *now, const char *tree)
{
	struct branch *after = branch_load(c->branch, NULL, tree);
	struct string_list before = STRING_LIST_INIT_DUP, then = STRING_LIST_INIT_DUP;
	struct string_list_item *v;

	if (after->errors.nr && !c->refusal)
		BUG("ait wrote a malformed contract: %s", after->errors.items[0].string);
	if (after->errors.nr)
		reject(1, c->violation_hint(c, NULL), "%s%s", c->refusal, after->errors.items[0].string);
	invariants(now, now, &before);
	invariants(after, now, &then);
	for_each_string_list_item(v, &then)
		if (!string_list_has_string(&before, v->string))
			reject(1, c->violation_hint ? c->violation_hint(c, v->util) : NULL, "%s%s",
			       c->refusal ? c->refusal : "", violation_words(v->util));
}

/* spec 4.4-4 (E_MOVED): what the checkin read changed meanwhile */
NORETURN void checkin_moved(struct checkin *c)
{
	reject(1, NULL, "'%s' moved while running; nothing changed", c->branch);
}

/* spec 4.4-1: T, the branch's tip now ("" when it has no commit) */
void checkin_tip(struct checkin *c)
{
	if (!object(xstrfmt("refs/heads/%s^{commit}", c->branch), 0, c->tip, NULL))
		*c->tip = 0;
}

/*
 * spec 4.4-1, 2: everything judged on the tip T the checkin holds; the new
 * tree, the invariants checked. Step 0 of fulfill asks the same (6.7.5-0).
 */
char *checkin_judge(struct checkin *c, judge_fn decide)
{
	struct branch *now;

	string_list_clear(&c->put, 0);
	c->tree = NULL;
	c->second_parent = c->ff = NULL;
	c->at = now = branch_load(c->branch, *c->tip ? c->tip : NULL, *c->tip ? c->tip : EMPTY_TREE);
	/* a tip that a git commit or merge broke after start() (4.3-3) */
	refuse_malformed(now, 0);
	if (decide)
		decide(c);
	if (c->ff)
		return NULL;
	if (!c->tree)
		c->tree = checkin_tree(c);
	checkin_invariants(c, now, c->tree);
	return c->tree;
}

/*
 * spec 4.4: one checkin. JUDGE decides on T what to put; on a lost CAS the
 * whole command is judged again on the new tip, unless a file it puts moved.
 * A judge that names a commit to fast-forward to (6.5.6) moves the branch there.
 */
void checkin_run(struct checkin *c, judge_fn decide)
{
	if (!*c->rid)
		newid(c->rid, 'R');
	checkin_tip(c);
	for (;;) {
		char old[48], *tree = checkin_judge(c, decide);
		const char *commit;
		struct strbuf parents = STRBUF_INIT;
		struct strvec args = STRVEC_INIT;
		struct string_list_item *item;
		int rc;

		/* a request that changes nothing is a noop (6.4.1): no commit */
		if (!c->ff && !c->second_parent && *c->tip && !strcmp(tree, c->at->tree->oid))
			return;
		if (*c->tip)
			strbuf_addf(&parents, "-p %s ", c->tip);
		if (c->second_parent)
			strbuf_addf(&parents, "-p %s ", c->second_parent);
		commit = c->ff ? c->ff :
			 line1(git_run(checkin_message(c), NULL, NULL, xstrfmt("commit-tree %s%%s", parents.buf), tree));
		/* 4.4-3: (a) no worktree uses the branch, judged by git; (b) the CAS */
		refuse_used(c->branch, commit);
		git_run(NULL, NULL, &rc, "update-ref -m %s %s %s %s", c->msg, xstrfmt("refs/heads/%s", c->branch), commit,
			*c->tip ? c->tip : NULL_OID);
		if (!rc && c->ff)
			printf("Fast-forward %.7s..%.7s\n", c->tip, commit);
		else if (!rc && !c->no_trailers)
			printf("[%s %s%s] %s\n", c->branch, line1(git("rev-parse --short %s", commit)),
			       *c->tip ? "" : " (root-commit)", line1(xstrdup(c->msg)));
		if (!rc)
			return;
		/* 4.4-4: T' is read once, compared, and judged on: T := T' */
		snprintf(old, sizeof(old), "%s", c->tip);
		checkin_tip(c);
		if (!*c->tip || !strcmp(old, c->tip))
			reject(128, NULL, "%s", git_failed());
		/* a merge reads every file, so any move judges it again (6.5.6) */
		if (c->second_parent || c->ff)
			continue;
		strvec_pushl(&args, "diff-tree", "--quiet", *old ? old : EMPTY_TREE, c->tip, "--", NULL);
		for_each_string_list_item(item, &c->put)
			strvec_push(&args, item->string);
		git_args(&args, NULL, NULL, &rc);
		if (rc && c->retry_hint)
			retry_command = c->retry_hint(c, old);
		if (rc)
			checkin_moved(c);
	}
}

/* the notification hook (spec 9, U51): git's reference-transaction hook (githooks(5)) wakes ait listen */
static const char hook_text[] =
	"#!/bin/sh\n"
	"# ait: wakes 'ait listen' when a contract branch changes (U51)\n"
	"[ \"$1\" = committed ] || exit 0\n"
	"grep -Eq ' refs/(heads|remotes/.+)/ait/' || exit 0\n"
	"# a line more: a size changes where a second-grained mtime may not; kept small\n"
	"w=\"$(git rev-parse --git-common-dir)/ait/wake\"\n"
	"[ \"$(wc -c <\"$w\" 2>/dev/null || echo 0)\" -gt 4096 ] && : >\"$w\"\n"
	"echo >>\"$w\" 2>/dev/null\n"
	"exit 0\n";

static char *hook_file(void)
{
	return git_path("hooks/reference-transaction");
}

/* the hook is ait's (9.8); another one leaves listen to polling every second */
int hook_installed(void)
{
	struct strbuf now = STRBUF_INIT;

	return strbuf_read_file(&now, hook_file(), 0) >= 0 && !strcmp(now.buf, hook_text);
}

static void install_hook(void)
{
	char *path = hook_file();

	if (!access(path, F_OK)) {
		if (!hook_installed())
			warning("'%s' exists; 'ait listen' wakes by polling only", path);
		return;
	}
	leading_dirs(path);
	write_state(path, hook_text);
	if (chmod(path, 0755))
		write_error(path);
	mkdir(state_path(""), 0777);
}

/* spec 6.1 */
static int cmd_init(int argc, const char **argv)
{
	struct names n;
	struct string_list_item *item;
	struct strbuf exclude = STRBUF_INIT;
	char *path = git_path("info/exclude");
	int made = 0;

	if (argc != 1)
		reject(129, NULL, "init takes no arguments");
	strbuf_read_file(&exclude, path, 0);
	if (!strstr(xstrfmt("\n%s", exclude.buf), "\n/.ait/\n")) {
		leading_dirs(path);
		strbuf_complete_line(&exclude);
		strbuf_addstr(&exclude, "/.ait/\n");
		write_state(path, exclude.buf);
	}
	install_hook();
	names_load(&n);
	for_each_string_list_item(item, &n.remote) {
		const char *from;

		if (string_list_has_string(&n.local, item->string))
			continue;
		if (!(from = init_source(&n, item->string))) {
			warning("'%s' matched multiple (%d) remote tracking branches", item->string,
				(int)((struct string_list *)item->util)->nr);
			continue;
		}
		git("branch -q %s %s", item->string, from);
		printf("branch %s\n", item->string);
		made++;
	}
	if (!n.local.nr && !made && !n.remote.nr) {
		struct string_list_item *r;
		struct checkin c = { .msg = "[CONTRACT] init", .no_trailers = 1 };

		for_each_string_list_item(r, &n.remotes) {
			int rc;
			char *out;

			if (!*r->string)
				continue;
			out = git_run(NULL, NULL, &rc, "ls-remote %s refs/heads/ait/*", r->string);
			if (rc)
				warning("cannot reach '%s'; skipped", r->string);
			else if (*out)
				reject(128, xstrfmt("fetch 'refs/heads/ait/*' from '%s', then 'ait init'", r->string),
				       "'%s' has ait branches that are not fetched", r->string);
		}
		c.branch = xstrfmt("ait/%s", line1(git("var GIT_DEFAULT_BRANCH")));
		string_list_init_dup(&c.put);
		checkin_run(&c, NULL);
		printf("branch %s\n", c.branch);
	}
	return 0;
}

/* the branch HEAD is on, for the hints of 6.2 */
static char *head_branch(void)
{
	int rc;
	char *name = line1(git_run(NULL, NULL, &rc, "symbolic-ref -q --short HEAD"));

	return rc ? NULL : name;
}

/* spec 6.2-6.3: the form ait takes is exactly ait/<name> (and for checkout ait/<name> --) of a known name */
static int contract_form(int argc, const char **argv, struct names *n)
{
	if (!(argc == 2 || (argc == 3 && !strcmp(argv[2], "--"))) || !starts_with(argv[1], "ait/"))
		return 0;
	names_load(n);
	return string_list_has_string(&n->local, argv[1]) || string_list_has_string(&n->remote, argv[1]);
}

static int pass_to_git(const char *cmd, const char **argv)
{
	struct strvec args = STRVEC_INIT;

	strvec_push(&args, cmd);
	strvec_pushv(&args, argv + 1);
	return git_pass(&args);
}

static void local_or_refuse(struct names *n, const char *name)
{
	if (!string_list_has_string(&n->local, name))
		reject(128, init_source(n, name) ? "ait init" : NULL, "no local branch '%s'", name);
}

static int clean_work_tree(void)
{
	int rc;

	git_run(NULL, NULL, &rc, "diff --quiet HEAD");
	return !rc;
}

/* spec 6.2 */
static int cmd_checkout(int argc, const char **argv)
{
	struct names n;
	char *on;

	if (!contract_form(argc, argv, &n))
		return pass_to_git("checkout", argv);
	local_or_refuse(&n, argv[1]);
	write_state(checkout_file(), xstrfmt("%s\n", argv[1]));
	printf("On contract branch %s\n", argv[1]);
	on = head_branch();
	if (clean_work_tree())
		suggest("the work tree stays on '%s'; to enter the contract branch: 'ait switch %s'",
			on ? on : "a detached HEAD", argv[1]);
	else
		suggest("the work tree stays on '%s'", on ? on : "a detached HEAD");
	return 0;
}

/* spec 6.3 */
static int cmd_switch(int argc, const char **argv)
{
	struct names n;
	struct strvec args = STRVEC_INIT;
	int rc;

	if (argc != 2 || !contract_form(argc, argv, &n))
		return pass_to_git("switch", argv);
	local_or_refuse(&n, argv[1]);
	if (!clean_work_tree())
		reject(128, NULL, "uncommitted changes in the work tree; commit them before entering '%s'", argv[1]);
	strvec_pushl(&args, "switch", argv[1], NULL);
	rc = git_pass(&args);
	if (!rc)
		suggest("contracts change only through ait commands; to go back: 'ait switch -'");
	return rc;
}

static struct {
	const char *name;
	int (*fn)(int, const char **);
} commands[] = {
	{ "init", cmd_init },
	{ "checkout", cmd_checkout },
	{ "switch", cmd_switch },
	{ "status", cmd_status },
	{ "demand", cmd_request },
	{ "offer", cmd_request },
	{ "future", cmd_request },
	{ "amend", cmd_request },
	{ "undertake", cmd_request },
	{ "release", cmd_request },
	{ "contract", cmd_contract },
	{ "fulfill", cmd_fulfill },
	{ "sub", cmd_sub },
	{ "unsub", cmd_unsub },
	{ "listen", cmd_listen },
	{ "session", cmd_session },
};

int main(int argc, const char **argv)
{
	size_t i;
	int status;

	sanitize_stdfds();
	set_warn_routine(warn_line);
	if (argc < 2)
		reject(129, NULL, "no command");
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[1], commands[i].name))
			continue;
		setup();
		precompose_argv_prefix(argc, argv, NULL);
		status = commands[i].fn(argc - 1, argv + 1);
		if (fflush(stdout) || ferror(stdout))
			reject(128, NULL, "write error: standard output: %s", strerror(errno));
		return status;
	}
	reject(129, NULL, "'%s' is not an ait command", argv[1]);
}
