/*
 * ait entry point: the command table, git calls, messages, sessions, the
 * contract branch and the checkin (spec 4), and init, checkout, switch (6.1-6.3).
 */
#include "ait.h"
#include "run-command.h"
#include "quote.h"
#include "precompose.h"
#include "setup.h"
#include "sigchain.h"
#include "abspath.h"
#include "wt-status.h"
#include "tempfile.h"
#include <dirent.h>
#include <sys/file.h>

const char *top, *head, *gitdir;
static const char *commondir;
char *git_error;
char *not_recorded;
static char *retry_command;
size_t git_out_len;
char agent[72], sid[16];
static char *git_cmd;
const char *comment_line_str;
static const char *code_index;
static const char *caller_index;
static struct strbuf code_output = STRBUF_INIT;

/* "AIT_SESSION=SID " when the session in use is not this worktree's default (set by session()) */
static char *cmd_prefix;

/* A known command gets its session once; message text is never parsed as commands. */
char *hint_command_for(const char *session_id, const char *fmt, ...)
{
	struct strbuf sb = STRBUF_INIT;
	va_list ap;

	if (session_id)
		strbuf_addf(&sb, "AIT_SESSION=%s ", session_id);
	else if (cmd_prefix)
		strbuf_addstr(&sb, cmd_prefix);
	va_start(ap, fmt);
	strbuf_vaddf(&sb, fmt, ap);
	va_end(ap);
	return strbuf_detach(&sb, NULL);
}

/* Git's advise() preserves the lines of one logical hint, including quoted messages. */
void suggest(const char *fmt, ...)
{
	static struct string_list said = STRING_LIST_INIT_DUP;
	struct strbuf sb = STRBUF_INIT;
	va_list ap;

	va_start(ap, fmt);
	strbuf_vaddf(&sb, fmt, ap);
	va_end(ap);
	if (sb.len && !string_list_has_string(&said, sb.buf)) {
		string_list_insert(&said, sb.buf);
		advise("%s", sb.buf);
	}
	strbuf_release(&sb);
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

/* 6.7.8: delay only Git's stdout; a later refusal still owes it to the user. */
static void show_code_output(void)
{
	if (code_output.len) {
		fwrite(code_output.buf, 1, code_output.len, stdout);
		strbuf_release(&code_output);
		fflush(stdout);
	}
}

/* git's fatal: and error: forms, the usage line of 129, and a hint (spec 4.7) */
void reject(int code, const char *hint, const char *fmt, ...)
{
	va_list ap;

	show_code_output();
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

NORETURN void no_contract(const char *id)
{
	reject(128, NULL, "no such contract: '%s'", id);
}

/* Like unpack-trees' error table, each E_STATE reason has one wording (4.8). */
char *refusal_words(enum refusal why, const char *a, const char *b, const char *detail)
{
	static const char *const words[] = {
		[ENDED] = "not allowed: '%s' has ended",
		[TARGET] = "not allowed: '%s' targets '%s', which is not current",
		[IDENTITY] = "not allowed: '%s' changes its group, kind or creator",
		[STATE] = "not allowed: '%s' goes from '%s' to '%s'",
		[FULFILMENT] = "fulfilments are recorded only by 'ait fulfill'",
		[FORCE_PROVIDER] = "cannot change the provider of '%s' with --force",
		[STALE_DRAFT] = "'%s' changed at '%s' since '%s' was written",
		[EXISTS] = "not allowed: '%s' already exists",
		[UNMERGED] = "fulfilling is not possible because you have unmerged files",
		[MIX_WHOLLY] = "'%s' is already undertaken wholly by '%s'",
		[MIX_PARTIALLY] = "'%s' is already undertaken partially by '%s'",
		[PAIR] = "changing '%s' would leave '%s' fulfilled on top of an unfulfilled '%s'"
	};

	if (why <= CHANGE_OK || (size_t)why >= ARRAY_SIZE(words) || !words[why])
		BUG("no words for refusal %d", why);
	if (why == PAIR)
		return xstrfmt(words[why], b, a, b);
	return xstrfmt(words[why], a, b, detail);
}

/* git runs on the repository and worktree ait settled at start (v14 5.12) */
static void at_repository(struct child_process *cmd, const char *index, int caller_paths)
{
	if (gitdir)
		strvec_pushf(&cmd->args, "--git-dir=%s", gitdir);
	if (top) {
		strvec_pushf(&cmd->args, "--work-tree=%s", top);
		if (!caller_paths)
			strvec_pushl(&cmd->args, "-C", top, NULL);
	}
	if (commondir)
		strvec_pushf(&cmd->env, "GIT_COMMON_DIR=%s", commondir);
	if (!index)
		index = caller_paths ? caller_index : code_index;
	if (index)
		strvec_pushf(&cmd->env, "GIT_INDEX_FILE=%s", index);
	if (!caller_paths)
		strvec_pushl(&cmd->env, "GIT_DIFF_OPTS", "GIT_LITERAL_PATHSPECS=1", "GIT_GLOB_PATHSPECS",
			     "GIT_NOGLOB_PATHSPECS", "GIT_ICASE_PATHSPECS", NULL);
}

static char *git_failed(void)
{
	return xstrfmt("git failed: 'git %s': %s", git_cmd, git_error);
}

/*
 * ait's, in place of git v2.53.0 sigchain.c:45 for tempfile.c and
 * run-command.c: a cleanup chain must terminate, not return to an ignored
 * disposition with its locks removed (t10). Preserve SIG_IGN, and prevent
 * another signal from reentering cleanup while a publishing child is being
 * reaped (t11).
 */
#ifdef GIT_WINDOWS_NATIVE
/* git v2.53.0 sigchain.c:45: Windows' sigaction takes only SIGALRM (compat/mingw.c:2745) */
void sigchain_push_common(sigchain_fun f)
{
	sigchain_push(SIGINT, f);
	sigchain_push(SIGHUP, f);
	sigchain_push(SIGTERM, f);
	sigchain_push(SIGQUIT, f);
	sigchain_push(SIGPIPE, f);
}
#else
void sigchain_push_common(sigchain_fun f)
{
	static const int cleanup_signals[] = { SIGINT, SIGHUP, SIGTERM, SIGQUIT, SIGPIPE };
	size_t i;

	for (i = 0; i < ARRAY_SIZE(cleanup_signals); i++) {
		struct sigaction action;
		int sig = cleanup_signals[i];

		if (sigaction(sig, NULL, &action))
			die_errno("sigaction");
		if (action.sa_handler == SIG_IGN)
			continue;
		if (sigchain_push(sig, f) || sigaction(sig, NULL, &action) ||
		    sigfillset(&action.sa_mask) || sigaction(sig, &action, NULL))
			die_errno("cannot install cleanup signal handler");
	}
}
#endif

static int git_children_guarded;

void guard_git_children(void)
{
	prepare_child_cleanup();
	git_children_guarded = 1;
}

/* Git takes argv; IN feeds stdin, INDEX selects its index; without RC a failure is fatal (4.7). */
static char *git_argv_n(const char *in, size_t in_len, const char *index, int *rc,
			const char **argv, const char **env)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	struct strbuf out = STRBUF_INIT, err = STRBUF_INIT;
	const char *reason;
	size_t i;
	int code;

	/* git's output as ait parses it, whatever the user's config adds to it */
	strvec_pushl(&cmd.args, "git", "-c", "core.quotePath=false", "-c", "log.showSignature=false",
		     "-c", "i18n.logOutputEncoding=UTF-8", NULL);
	at_repository(&cmd, index, 0);
	strvec_pushv(&cmd.args, argv);
	if (env)
		strvec_pushv(&cmd.env, env);
	/* git's messages untranslated: ait's are English (v14 5.6) */
	strvec_push(&cmd.env, "LANGUAGE=C");
	cmd.clean_on_exit = cmd.wait_after_clean = git_children_guarded;
	code = pipe_command(&cmd, in, in_len, &out, 0, &err, 0);
	free(git_error);
	git_error = xstrndup(err.buf, strcspn(err.buf, "\n"));
	if (skip_prefix(git_error, "fatal: ", &reason) || skip_prefix(git_error, "error: ", &reason))
		memmove(git_error, reason, strlen(reason) + 1);
	for (i = 0; argv[i] && *argv[i] == '-'; i++)
		;
	free(git_cmd);
	git_cmd = xstrdup(argv[i] ? argv[i] : "");
	if (rc)
		*rc = code;
	if (code < 0 || (!rc && code))
		reject(128, NULL, "%s", git_failed());
	strbuf_release(&err);
	return strbuf_detach(&out, &git_out_len);
}

char *git_argv(const char *in, const char *index, int *rc, const char **argv)
{
	return git_argv_n(in, in ? strlen(in) : 0, index, rc, argv, NULL);
}

/* Git's -z records preserve path bytes; no caller decodes display quoting. */
void git_records_argv(struct string_list *out, const char *index, int *rc, const char **argv)
{
	char *bytes = git_argv(NULL, index, rc, argv), *p = bytes, *end;

	for (end = p + git_out_len; p < end; p += strlen(p) + 1)
		string_list_append(out, p);
	free(bytes);
}

/* Binary index input retains its byte length, including NUL record separators. */
static char *git_feed_argv(const struct strbuf *in, const char *index, int *rc, const char **argv)
{
	return git_argv_n(in->buf, in->len, index, rc, argv, NULL);
}
#define git_feed(in, index, rc, ...) \
	git_feed_argv(in, index, rc, (const char *[]){ __VA_ARGS__, NULL })

/* Git run-command.h: pipe_command can collect stdout while inheriting stderr. */
static int pass_git(struct strvec *args, int caller_paths, struct strbuf *out)
{
	struct child_process cmd = CHILD_PROCESS_INIT;

	strvec_push(&cmd.args, "git");
	at_repository(&cmd, NULL, caller_paths);
	strvec_pushv(&cmd.args, args->v);
	fflush(stdout);
	return pipe_command(&cmd, NULL, 0, out, 0, NULL, 0);
}

int git_pass(struct strvec *args)
{
	return pass_git(args, 0, NULL);
}

int git_commit(const char *msg)
{
	struct strvec args = STRVEC_INIT;
	int rc;

	strvec_pushl(&args, "commit", "-m", msg, NULL);
	rc = pass_git(&args, 0, &code_output);
	strvec_clear(&args);
	return rc;
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

	return !strcmp(line1(git_run(NULL, NULL, &rc, "config", "--bool", key)), "true");
}

static struct child_process cat_file = CHILD_PROCESS_INIT;
static FILE *cat_output;

/* transport-helper.c:disconnect closes helper IO before finish_command. */
static void close_cat_file(void)
{
	if (!cat_output)
		return;
	close(cat_file.in);
	fclose(cat_output);
	cat_output = NULL;
	finish_command(&cat_file);
}

/* the object NAME resolves to, through one "git cat-file --batch-command" (git-cat-file(1)) */
char *object(const char *name, int contents, char *oid, size_t *size)
{
	char *cmd = xstrfmt("%s %s\n", contents ? "contents" : "info", name);
	char line[256], id[48], type[16], *buf;
	size_t n;

	if (!cat_output) {
		strvec_push(&cat_file.args, "git");
		at_repository(&cat_file, NULL, 0);
		strvec_pushl(&cat_file.args, "cat-file", "--batch-command", NULL);
		cat_file.in = cat_file.out = -1;
		cat_file.no_stderr = 1;
		if (start_command(&cat_file))
			reject(128, NULL, "git failed: 'git cat-file': cannot run it");
		fcntl(cat_file.in, F_SETFD, FD_CLOEXEC);
		fcntl(cat_file.out, F_SETFD, FD_CLOEXEC);
		cat_output = xfdopen(cat_file.out, "r");
		atexit(close_cat_file);
	}
	if (write_in_full(cat_file.in, cmd, strlen(cmd)) < 0 || !fgets(line, sizeof(line), cat_output))
		reject(128, NULL, "git failed: 'git cat-file': it stopped");
	free(cmd);
	if (sscanf(line, "%47s %15s %zu", id, type, &n) != 3)
		return NULL;
	if (oid)
		snprintf(oid, 48, "%s", id);
	if (size)
		*size = n;
	if (!contents)
		return xstrdup(type);
	buf = xmallocz(n);
	if (fread(buf, 1, n, cat_output) != n || fgetc(cat_output) != '\n')
		reject(128, NULL, "git failed: 'git cat-file': it stopped");
	return buf;
}

int object_oid(const char *name, char *oid)
{
	char *type = object(name, 0, oid, NULL);
	int found = type != NULL;

	free(type);
	return found;
}

int branch_tip(const char *name, char *oid)
{
	char *ref = xstrfmt("refs/heads/%s^{commit}", name);
	int found = object_oid(ref, oid);

	free(ref);
	return found;
}

/* S as one shell word for the commands hints give (v14 5.9a), quoted only where needed, as git shows a command */
char *quoted(const char *s)
{
	struct strbuf sb = STRBUF_INIT;

	sq_quote_buf_pretty(&sb, s);
	return strbuf_detach(&sb, NULL);
}

/* NAMES as messages list them: 'A', 'B' */
char *quoted_list(const struct string_list *names)
{
	struct strbuf sb = STRBUF_INIT;
	const struct string_list_item *item;

	for_each_string_list_item(item, names)
		strbuf_addf(&sb, "%s'%s'", sb.len ? ", " : "", item->string);
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
	return s && strlen(s) == 13 && strchr(letters, *s) && !s[1 + strspn(s + 1, "0123456789abcdef")];
}

/* forty lowercase hex digits, a SHA-1 object name */
int oid_ok(const char *s)
{
	return strlen(s) == 40 && !s[strspn(s, "0123456789abcdef")];
}

/* v14 7.2.1: an agent name, [A-Za-z0-9._-]{1,64} and not "unknown" */
int agent_ok(const char *name)
{
	return *name && strlen(name) <= 64 && strcmp(name, "unknown") &&
	       !name[strspn(name, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-")];
}

/* Git prints one scalar path followed by a newline; embedded newlines are path bytes. */
char *git_path(const char *name)
{
	char *path = git("rev-parse", "--path-format=absolute", "--git-path", name);

	if (git_out_len && path[git_out_len - 1] == '\n')
		path[git_out_len - 1] = 0;
	return path;
}

/* the repository, its worktree and HEAD (v14 5.12); a bare repository has no contract commands (2.6.5) */
static void setup(void)
{
	static const char *const clear[] = { "GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_PREFIX",
				       "GIT_COMMON_DIR", NULL };
	struct string_list lines = STRING_LIST_INIT_DUP;
	int i, major = 0, minor = 0, rc;
	char oid[48];
	const char *index_env = getenv("GIT_INDEX_FILE");

	if (sscanf(git_run(NULL, NULL, &rc, "version"), "git version %d.%d", &major, &minor) != 2)
		reject(128, "install git 2.38 or later", "cannot run git");
	/* pull's 'git merge-tree --write-tree' came in Git 2.38 (spec 6.1-1) */
	if (major < 2 || (major == 2 && minor < 38))
		reject(128, "install git 2.38 or later", "git %d.%d is too old for ait", major, minor);
	string_list_split(&lines, git_run(NULL, NULL, &rc, "rev-parse", "--absolute-git-dir",
		"--path-format=absolute", "--git-common-dir", "--show-object-format", "--show-toplevel"), "\n", -1);
	if (lines.nr < 4)
		reject(128, NULL, "not a git repository");
	if (rc)
		reject(128, NULL, "this operation must be run in a work tree");
	if (lines.nr > 5)
		reject(128, NULL, "not supported: a repository path with a line feed");
	if (strcmp(lines.items[2].string, "sha1"))
		reject(128, NULL, "not supported: object format '%s'", lines.items[2].string);
	/*
	 * Git's setup_explicit_git_dir keeps the caller's cwd outside a worktree.
	 * Native commands keep the raw value; an empty value names no path to resolve.
	 */
	caller_index = index_env ? xstrdup(index_env) : NULL;
	code_index = caller_index && !*caller_index ? caller_index : git_path("index");
	gitdir = lines.items[0].string;
	commondir = lines.items[1].string;
	top = lines.items[3].string;
	for (i = 0; clear[i]; i++)
		unsetenv(clear[i]);
	if (object_oid("HEAD^{commit}", oid))
		head = xstrdup(oid);
}

/* the shared state under <git-common-dir>/ait/ (v14 4.1) */
char *state_path(const char *name)
{
	return xstrfmt("%s/ait/%s", commondir, name);
}

static void state_dirs(void)
{
	static const char *const dirs[] = { "", "sessions", "worktrees" };
	size_t i;

	for (i = 0; i < ARRAY_SIZE(dirs); i++) {
		char *path = state_path(dirs[i]);

		mkdir(path, 0777);
		free(path);
	}
}

static NORETURN void write_failure(const char *path, int listening)
{
	int saved = errno;
	const char *retry;

	if (!path)
		reject(128, NULL, "write error: standard output: %s", strerror(saved));
	retry = listening ? xstrfmt("start '%s' again", hint_command("ait listen")) : "rerun this command";
	reject(128, xstrfmt("make '%s' writable (free space, permissions), then %s", path, retry),
	       "write error: '%s': %s", path, strerror(saved));
}

NORETURN void write_error(const char *path)
{
	write_failure(path, 0);
}

NORETURN void listen_write_error(const char *path)
{
	write_failure(path, 1);
}

/* Worktree-local ait files use Git's directory choice at one boundary (2.6). */
char *worktree_state_dir(void)
{
	char *dir = git_path("ait");

	mkdir(dir, 0777);
	return dir;
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
	free(tmp);
}

/* v14 4.6: the default-session pointer of the worktree WT */
static char *pointer_path(const char *wt)
{
	static struct string_list hashed = STRING_LIST_INIT_DUP;
	struct string_list_item *hit = string_list_lookup(&hashed, wt);
	char *name, *path;

	if (!hit) {
		hit = string_list_insert(&hashed, wt);
		hit->util = line1(git_run(wt, NULL, NULL, "hash-object", "--stdin"));
	}
	name = xstrfmt("worktrees/%s", (char *)hit->util);
	path = state_path(name);
	free(name);
	return path;
}

/* the first line of the state file PATH, or NULL when there is none; PATH is freed */
static char *state_line(char *path)
{
	struct strbuf sb = STRBUF_INIT;
	char *value = NULL;

	if (strbuf_read_file(&sb, path, 0) >= 0)
		value = line1(strbuf_detach(&sb, NULL));
	free(path);
	strbuf_release(&sb);
	return value;
}

static char *default_of(const char *wt)
{
	return state_line(pointer_path(wt));
}

/*
 * v14 4.1: a lock file is opened, never removed, and held with flock(2);
 * -1 when NOWAIT finds it held
 */
static int lock_path(const char *path, int nowait)
{
	int fd;

	state_dirs();
	if ((fd = open(path, O_RDWR | O_CREAT, 0666)) < 0)
		write_error(path);
#ifdef GIT_WINDOWS_NATIVE
	{
		/*
		 * Windows' locks are mandatory: lock a byte past any content, so
		 * the pid in '.listen' stays readable (v14 4.5)
		 */
		OVERLAPPED at = { .Offset = 0xffffffff, .OffsetHigh = 0x7fffffff };

		if (LockFileEx((HANDLE)_get_osfhandle(fd),
			       LOCKFILE_EXCLUSIVE_LOCK | (nowait ? LOCKFILE_FAIL_IMMEDIATELY : 0),
			       0, 1, 0, &at))
			return fd;
	}
#else
	if (!flock(fd, LOCK_EX | (nowait ? LOCK_NB : 0)))
		return fd;
#endif
	if (!nowait)
		write_error(path);
	close(fd);
	return -1;
}

/* v14 4.2-4.5: the session S's files: SUFFIX "" its lines, ".lock", ".listen", ".seen" */
char *session_path(const char *s, const char *suffix)
{
	char *name = xstrfmt("sessions/%s%s", s, suffix), *path = state_path(name);

	free(name);
	return path;
}

void session_lines(const char *s, struct string_list *lines)
{
	struct strbuf sb = STRBUF_INIT;
	char *path = session_path(s, "");

	string_list_clear(lines, 0);
	strbuf_read_file(&sb, path, 0);
	free(path);
	string_list_split(lines, sb.buf, "\n", -1);
	strbuf_release(&sb);
}

void session_write(const char *s, struct string_list *lines)
{
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;
	char *path = session_path(s, "");

	for_each_string_list_item(item, lines)
		if (*item->string)
			strbuf_addf(&sb, "%s\n", item->string);
	write_state(path, sb.buf);
	free(path);
	strbuf_release(&sb);
}

/* v14 4.5: the listener's lock of the session S, taken without waiting; -1 while a listener holds it */
int listener_lock(const char *s)
{
	char *path = session_path(s, ".listen");
	int fd = lock_path(path, 1);

	free(path);
	return fd;
}

/* v14 4.6: the worktree WT's lock */
static int hold_lock(const char *wt)
{
	char *pointer = pointer_path(wt), *path = xstrfmt("%s.lock", pointer);
	int fd = lock_path(path, 0);

	free(pointer);
	free(path);
	return fd;
}

static struct string_list *worktrees(void)
{
	static struct string_list list = STRING_LIST_INIT_DUP;
	struct string_list records = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	const char *path;

	if (list.nr)
		return &list;
	git_records(&records, NULL, NULL, "worktree", "list", "--porcelain", "-z");
	for_each_string_list_item(item, &records)
		if (skip_prefix(item->string, "worktree ", &path))
			string_list_insert(&list, path);
	return &list;
}

static int gone(const char *wt)
{
	return access(wt, F_OK) || !string_list_has_string(worktrees(), wt);
}

/* "agent NAME\nworktree PATH\n..." (v14 4.2), NAME whole and valid: the worktree, or NULL */
static char *session_read(const char *s, char *name)
{
	struct string_list lines = STRING_LIST_INIT_DUP;
	const char *who, *wt = NULL;
	char *result = NULL;
	size_t i;

	if (!id_ok(s, "S"))
		return NULL;
	session_lines(s, &lines);
	for (i = 1; !wt && i < lines.nr; i++)
		skip_prefix(lines.items[i].string, "worktree ", &wt);
	if (lines.nr && skip_prefix(lines.items[0].string, "agent ", &who) && agent_ok(who) && wt) {
		if (name)
			snprintf(name, 72, "%s", who);
		result = xstrdup(wt);
	}
	string_list_clear(&lines, 0);
	return result;
}

static NORETURN void unknown_session(const char *s)
{
	reject(128, hint_command_for("-", "ait session list"), "no usable ait session: unknown session '%s'", s);
}

/*
 * v14 4.4: the session S's lock, for reading and writing its file; the
 * session is still there once it is held (a close may have come first)
 */
int lock_session(const char *s)
{
	char *path = session_path(s, ".lock"), *wt;
	int fd = lock_path(path, 0);

	free(path);
	wt = session_read(s, NULL);
	if (!wt)
		unknown_session(s);
	free(wt);
	return fd;
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
		unknown_session(env);
	} else if (wt && strcmp(wt, top)) {
		char *hint = gone(wt) ? xstrfmt("its worktree is gone; close it: '%s', then open a session "
						"with the same name in the worktree of your branch: '%s'",
						hint_command_for("-", "ait session close %s", env),
						hint_command_for("-", "ait session open --agent %s", agent)) : xstrfmt("use it in its worktree '%s'", wt);

		reject(128, hint, "no usable ait session: '%s' belongs to worktree '%s'", env, wt);
	}
	if (!wt && need)
		reject(128, xstrfmt("find your session with '%s', or open one: '%s'",
				    hint_command("ait session list"), hint_command("ait session open --agent <name>")),
		       "no usable ait session: none for this worktree");
	if (!wt) {
		free((char *)dflt);
		return NULL;
	}
	snprintf(sid, sizeof(sid), "%s", env);
	/* v14 5.9a: hints name the session when it is not this worktree's default */
	if (!from_default && (!dflt || strcmp(env, dflt)))
		cmd_prefix = xstrfmt("AIT_SESSION=%s ", env);
	free(wt);
	free((char *)dflt);
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
	char *path;

	state_dirs();
	path = state_path("sessions");
	dir = opendir(path);
	free(path);
	if (!dir)
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

	newid(sid, 'S');
	write_state(session_path(sid, ""), xstrfmt("agent %s\nworktree %s\n", name, top));
	session_tips(sid, NULL, NULL, NULL);
	for_each_string_list_item(item, ids) {
		struct session_file *f = item->util;

		if (strcmp(f->agent, name))
			continue;
		warning("agent name '%s' is also used by session '%s' (worktree '%s')", name, item->string, f->wt);
		if (gone(f->wt))
			suggest("its worktree is gone; close it: '%s', and keep this one if this worktree "
				"is on your branch, otherwise close this one too ('%s') and open it again "
				"in the worktree of your branch", hint_command("ait session close %s", item->string),
				hint_command("ait session close %s", sid));
		else
			suggest("if '%s' is yours, close this one: '%s', and use '%s' in '%s' with "
				"AIT_SESSION=%s; otherwise close this one and open again with another name",
				item->string, hint_command("ait session close %s", sid), item->string, f->wt, item->string);
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
		int fd = listener_lock(item->string), n = 0, listening = fd < 0;

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
		unknown_session(s);
	f = item->util;
	/* v14 4.5: the worktree's lock, the session's, then the listener's tried */
	fd = hold_lock(f->wt);
	lock_session(s);
	if (listener_lock(s) < 0) {
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
	const char *current;
	struct string_list_item *item;
	enum { SESSION_OPEN, SESSION_LIST, SESSION_CLOSE } action;

	if (argc == 4 && !strcmp(argv[1], "open") && !strcmp(argv[2], "--agent")) {
		if (!agent_ok(argv[3]))
			reject(129, NULL, "bad agent name '%s'", argv[3]);
		action = SESSION_OPEN;
	} else if (argc == 2 && !strcmp(argv[1], "list")) {
		action = SESSION_LIST;
	} else if ((argc == 2 || argc == 3) && !strcmp(argv[1], "close")) {
		action = SESSION_CLOSE;
	} else {
		reject(129, NULL, "unknown session command");
	}
	current = session(action == SESSION_CLOSE && argc == 2);
	read_sessions(&ids);
	if (action == SESSION_OPEN)
		session_open(&ids, argv[3]);
	else if (action == SESSION_LIST)
		session_list(&ids);
	else
		session_close(&ids, argc == 3 ? argv[2] : current);
	for_each_string_list_item(item, &ids) {
		struct session_file *file = item->util;

		free(file->wt);
		free(file);
	}
	string_list_clear(&ids, 0);
	return 0;
}

/*
 * spec 2.1.2: the names of the local ait branches and of the remote-tracking
 * ones, matched against the configured remotes, whose names may hold '/'
 */
static const char *remote_ait_name(const struct string_list *remotes, const char *ref)
{
	struct string_list_item *r;
	const char *rest, *best = NULL;

	if (!skip_prefix(ref, "refs/remotes/", &rest))
		return NULL;
	/* the longest configured remote that names the start, then "/ait/" */
	for_each_string_list_item(r, remotes) {
		size_t len = strlen(r->string);

		if (!strncmp(rest, r->string, len) && starts_with(rest + len, "/ait/") &&
		    rest[len + 5] && (!best || len > strlen(best)))
			best = r->string;
	}
	return best ? rest + strlen(best) + 1 : NULL;
}

/* git remote: the configured remotes, one name each */
void remote_names(struct string_list *out)
{
	char *text = git("remote");

	/* each name ends its line */
	if (*text) {
		text[strlen(text) - 1] = 0;
		string_list_split(out, text, "\n", -1);
	}
	free(text);
}

void names_load(struct names *n)
{
	struct string_list refs = STRING_LIST_INIT_DUP;
	struct string_list_item *ref;
	char *text;

	string_list_init_dup(&n->local);
	string_list_init_dup(&n->remote);
	string_list_init_dup(&n->remotes);
	string_list_init_dup(&n->tips);
	remote_names(&n->remotes);
	text = git("for-each-ref", "--format=%(objectname) %(refname) %(upstream)", "refs/heads/ait/", "refs/remotes/");
	string_list_split(&refs, text, "\n", -1);
	free(text);
	for_each_string_list_item(ref, &refs) {
		const char *name = strchr(ref->string, ' ');
		const char *rest;
		const char *remote_name;
		char *up;

		if (!name)
			continue;
		name++;
		up = strchr(name, ' ');
		if (up)
			*up++ = 0;
		if (skip_prefix(name, "refs/heads/ait/", &rest) && *rest) {
			char *local = xstrfmt("ait/%s", rest);

			string_list_insert(&n->local, local)->util = up && *up ? xstrdup(up) : NULL;
			free(local);
			string_list_append_nodup(&n->tips, xstrfmt("tip %s", ref->string));
			continue;
		}
		remote_name = remote_ait_name(&n->remotes, name);
		if (remote_name) {
			struct string_list_item *item = string_list_insert(&n->remote, remote_name);

			if (!item->util) {
				item->util = xcalloc(1, sizeof(struct string_list));
				string_list_init_dup(item->util);
			}
			string_list_append(item->util, name);
			string_list_append_nodup(&n->tips, xstrfmt("tip %s", ref->string));
		}
	}
	n->nr = n->local.nr;
	for_each_string_list_item(ref, &n->remote)
		n->nr += !string_list_has_string(&n->local, ref->string);
	string_list_clear(&refs, 0);
}

/* spec 9.1: the OID of REF in N's snapshot (its first 40 characters), or NULL */
const char *names_tip(struct names *n, const char *ref)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &n->tips)
		if (!strcmp(item->string + 45, ref))
			return item->string + 4;
	return NULL;
}

/* spec 6.5.6-2, 9.2: BRANCH's upstream, when it is a remote ait ref N holds */
const char *names_upstream(struct names *n, const char *branch)
{
	struct string_list_item *item = string_list_lookup(&n->local, branch);

	return item && item->util && remote_ait_name(&n->remotes, item->util) &&
	       names_tip(n, item->util) ? item->util : NULL;
}

void names_clear(struct names *n)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &n->remote) {
		string_list_clear(item->util, 0);
		free(item->util);
	}
	string_list_clear(&n->local, 1);
	string_list_clear(&n->remote, 0);
	string_list_clear(&n->remotes, 0);
	string_list_clear(&n->tips, 0);
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
	dflt = line1(git_run(NULL, NULL, &rc, "config", "--get", "checkout.defaultRemote"));
	for (i = 0; !rc && i < refs->nr; i++)
		if (!strcmp(refs->items[i].string, xstrfmt("refs/remotes/%s/%s", dflt, name)))
			return refs->items[i].string;
	return NULL;
}

/* A is B or an ancestor of it (git merge-base --is-ancestor) */
int is_ancestor(const char *a, const char *b)
{
	int rc;

	free(git_run(NULL, NULL, &rc, "merge-base", "--is-ancestor", a, b));
	return !rc;
}

/* the directories that hold PATH, made as needed (git's safe_create_leading_directories) */
static void leading_dirs(const char *path)
{
	const char *slash;

	for (slash = strchr(path + 1, '/'); slash; slash = strchr(slash + 1, '/'))
		mkdir(xstrndup(path, slash - path), 0777);
}

/*
 * Git's entry.c:create_file reserves an absent path atomically. Return zero
 * if it exists; a nonzero MODE fixes permissions after all bytes are written.
 */
int create_file(const char *path, const char *text, mode_t mode)
{
	int fd, failed, saved;

	leading_dirs(path);
	fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
	if (fd < 0) {
		if (errno == EEXIST)
			return 0;
		write_error(path);
	}
	failed = write_in_full(fd, text, strlen(text)) < 0;
#ifndef GIT_WINDOWS_NATIVE
	/* Windows has no mode bits; git runs a hook by its '#!' line (compat/mingw.c:1602) */
	if (!failed && mode)
		failed = fchmod(fd, mode);
#endif
	saved = errno;
	if (close(fd) && !failed) {
		failed = 1;
		saved = errno;
	}
	if (failed) {
		unlink(path);
		errno = saved;
		write_error(path);
	}
	return 1;
}

/* spec 2.6.1: the worktree's checkout file */
static char *checkout_file(void)
{
	return git_path("ait-branch");
}

char *checkout_read(void)
{
	return state_line(checkout_file());
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
const char *branch_find(struct names *n, const char *chosen, struct strbuf *msg, struct strbuf *hint, int *only)
{
	struct string_list_item *item;

	if (only)
		*only = 0;
	strbuf_reset(msg);
	strbuf_reset(hint);
	if (chosen && string_list_has_string(&n->local, chosen))
		return chosen;
	/* 2.1.2: one name, counting the remote-tracking names too, so a DWIM-made branch is not taken silently */
	if (!chosen && n->nr == 1 && n->local.nr) {
		if (only)
			*only = 1;
		return n->local.items[0].string;
	}
	for_each_string_list_item(item, &n->local)
		strbuf_addf(hint, "%s\n", hint_command("ait checkout %s", quoted(item->string)));
	/* init makes the default branch where there is none, or a local one from a remote-tracking one (6.1) */
	if (!n->nr || init_hint(n))
		strbuf_addf(hint, "%s\n", hint_command("ait init"));
	if (!n->local.nr)
		strbuf_addstr(msg, n->remote.nr ? "no local ait branch" : "no ait branch");
	else if (chosen)
		strbuf_addf(msg, "contract branch '%s' does not exist", chosen);
	else
		strbuf_addstr(msg, "more than one ait branch; none is checked out");
	return NULL;
}

static int only_branch;

static const char *branch_name(void)
{
	static const char *name;
	struct names n;
	struct strbuf hint = STRBUF_INIT, msg = STRBUF_INIT;

	if (!name) {
		names_load(&n);
		if (!(name = branch_find(&n, checkout_read(), &msg, &hint, &only_branch)))
			reject(128, hint.len ? hint.buf : NULL, "%s", msg.buf);
	}
	return name;
}

int branch_only(void)
{
	branch_name();
	return only_branch;
}

/*
 * spec 4.3-4, 4.4-3 (a): whether a worktree uses BRANCH, judged by git as it
 * judges a fetch into a checked-out branch (builtin/fetch.c:1382), with its words
 */
static void refuse_used(const char *branch, const char *commit)
{
	int rc;

	git_run(NULL, NULL, &rc, "fetch", "-q", "--dry-run", "--no-write-fetch-head", "--no-recurse-submodules",
		".", xstrfmt("%s:refs/heads/%s", commit, branch));
	if (rc == 128)
		reject(128, NULL, "%s", git_error);
}

/* spec 4.3-3: a tip with a malformed file is refused (or, for status, warned of), wherever it is met */
static void refuse_malformed(struct branch *b, int warn)
{
	struct string_list_item *e;

	for_each_string_list_item(e, &b->errors) {
		char *line = xstrfmt("bad format: '%s' at '%s': %s", e->string, b->name, (char *)e->util);

		if (warn)
			warning("%s", line);
		else
			reject(128, xstrfmt("fix '%s' in '%s' (enter it with '%s')", e->string,
					    b->name, hint_command("ait switch %s", quoted(b->name))), "%s", line);
	}
}

/* the branch HEAD is on, for the hints of 6.2 */
static char *head_branch(void)
{
	int rc;
	const char *name;
	char *ref = line1(git_run(NULL, NULL, &rc, "symbolic-ref", "-q", "HEAD"));

	/* --short is display disambiguation: a same-name tag can produce heads/NAME. */
	return !rc && skip_prefix(ref, "refs/heads/", &name) ? xstrdup(name) : NULL;
}

/* spec 4.5: assembly, parser probes and diagnostics share these keys. */
static const char *const trailer_keys[] = { "Ait-Request", "Ait-Agent", "Ait-Session" };

static char *trailer_message(const char *msg, const char *rid, const char *who, const char *session_id)
{
	const char *values[] = { rid, who, session_id };
	struct strbuf sb = STRBUF_INIT;
	size_t i;

	strbuf_addstr(&sb, msg);
	strbuf_complete_line(&sb);
	strbuf_addch(&sb, '\n');
	for (i = 0; i < ARRAY_SIZE(trailer_keys); i++)
		strbuf_addf(&sb, "%s: %s\n", trailer_keys[i], values[i]);
	return strbuf_detach(&sb, NULL);
}

/*
 * Like Git's tmp_objdir_destroy(), own the entire private ODB, including
 * interrupted loose writes.
 */
static char *message_directory;

/*
 * PATH and all below it, as git's remove_dir_recurse (dir.c:3393) without
 * its repository-aware dir.c; not nftw, whose Windows walk reads entries
 * through the C library's struct dirent while git's readdir is linked
 */
static int remove_tree(struct strbuf *path)
{
	DIR *dir = opendir(path->buf);
	struct dirent *e;
	size_t len = path->len;
	int ret = 0, saved;

	if (!dir)
		return errno == ENOENT ? 0 : -1;
	while (!ret && (e = readdir(dir))) {
		struct stat st;

		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		strbuf_addf(path, "/%s", e->d_name);
		if (lstat(path->buf, &st))
			ret = errno == ENOENT ? 0 : -1;
		else if (S_ISDIR(st.st_mode))
			ret = remove_tree(path);
		else
			ret = unlink(path->buf) && errno != ENOENT ? -1 : 0;
		strbuf_setlen(path, len);
	}
	saved = errno;
	closedir(dir);
	errno = saved;
	return ret ? ret : rmdir(path->buf);
}

static int remove_message_tree(const char *directory)
{
	struct strbuf path = STRBUF_INIT;
	int ret, saved;

	strbuf_addstr(&path, directory);
	ret = remove_tree(&path);
	saved = errno;
	strbuf_release(&path);
	errno = saved;
	return ret;
}

/* Like builtin/clone.c:396-423, clean on exit and signals below child cleanup. */
static void remove_message_directory(void)
{
	if (message_directory)
		remove_message_tree(message_directory);
}

static void remove_message_on_signal(int signo)
{
	remove_message_directory();
	sigchain_pop(signo);
	raise(signo);
}

/*
 * The same Git writer and history reader handle encoding and trailer config.
 * Return the decoded body followed by the three values, each NUL-terminated.
 */
static char *read_message(const char *message)
{
	static int initialized;
	struct tempfile *probe = mks_tempfile_dt("ait-message-XXXXXX", "owner");
	struct strbuf format = STRBUF_INIT;
	char *dir, *oid = NULL, *out = NULL;
	const char *env[] = { NULL, "GIT_AUTHOR_NAME=ait", "GIT_AUTHOR_EMAIL=ait@invalid",
		"GIT_AUTHOR_DATE=@0 +0000", "GIT_COMMITTER_NAME=ait", "GIT_COMMITTER_EMAIL=ait@invalid",
		"GIT_COMMITTER_DATE=@0 +0000", NULL };
	size_t i;
	int rc, cleanup_rc, cleanup_errno;

	if (!probe)
		write_error("temporary message");
	message_directory = dir = xstrdup(absolute_path(probe->directory));
	if (!initialized) {
		atexit(remove_message_directory);
		sigchain_push_common(remove_message_on_signal);
		initialized = 1;
	}
	/* Transfer directory ownership; tempfile still owns its anchor file. */
	free(probe->directory);
	probe->directory = NULL;
	guard_git_children();
	if (close_tempfile_gently(probe))
		write_error(probe->filename.buf);
	env[0] = xstrfmt("GIT_OBJECT_DIRECTORY=%s", message_directory);
	/* git knows the empty tree without its object (object-file.c) */
	oid = line1(git_argv_n(message, strlen(message), NULL, &rc,
		(const char *[]){ "commit-tree", EMPTY_TREE, NULL }, env));
	if (!rc) {
		strbuf_addstr(&format, "--format=format:%B%x00");
		for (i = 0; i < ARRAY_SIZE(trailer_keys); i++)
			strbuf_addf(&format, "%%(trailers:key=%s,valueonly)%%x00", trailer_keys[i]);
		out = git_argv_n(NULL, 0, NULL, &rc,
			(const char *[]){ "log", "--no-walk", format.buf, oid, "--", NULL }, env);
	}
	cleanup_rc = remove_message_tree(dir);
	cleanup_errno = errno;
	/* Deactivate the file even after a failed directory cleanup. */
	delete_tempfile(&probe);
	free(oid);
	free((char *)env[0]);
	strbuf_release(&format);
	/* Cleanup must not replace the writer's original error with an IO hint. */
	if (rc)
		reject(128, NULL, "%s", git_failed());
	if (cleanup_rc) {
		errno = cleanup_errno;
		write_error(dir);
	}
	message_directory = NULL;
	free(dir);
	return out;
}

/*
 * spec 4.5, v14 5.3: the trailers must read back through the reader history
 * uses, on a commit as git writes it. RECORD is a message with the values
 * RID, WHO and SESSION_ID appended; UNREAD gets the keys not read back as
 * given. The body git decodes is returned.
 */
static char *trailer_probe(const char *record, const char *rid, const char *who, const char *session_id,
			   struct string_list *unread)
{
	const char *values[] = { rid, who, session_id };
	char *view = read_message(record), *value;
	size_t i;

	value = view + strlen(view) + 1;
	for (i = 0; i < ARRAY_SIZE(trailer_keys); i++) {
		if (strncmp(value, values[i], strlen(values[i])) || strcmp(value + strlen(values[i]), "\n"))
			string_list_append(unread, trailer_keys[i]);
		value += strlen(value) + 1;
	}
	return view;
}

/*
 * spec 4.5: a line of the message that cuts the trailers off is E_USAGE,
 * judged before the common checks. A fresh nonce stands for the values, so
 * a block quoted in the message cannot pose as the appended one; whether the
 * config lets the real values read is checked_record()'s.
 */
char *request_message(const char *msg, int fulfilling)
{
	char *text = xstrfmt("%s%s", fulfilling ? "[FULFILL] " : "[CONTRACT] ", msg);
	struct string_list unread = STRING_LIST_INIT_DUP;
	char nonce[16], *probe, *view;
	size_t len, end;

	newid(nonce, 'R');
	probe = trailer_message(text, nonce, nonce, nonce);
	view = trailer_probe(probe, nonce, nonce, nonce, &unread);
	len = strlen(view);
	if (unread.nr) {
		/* git resolves core.commentChar and core.commentString for its cut line */
		char *prefix = git_run("x\n", NULL, NULL, "stripspace", "--comment-lines");

		if (!ends_with(prefix, " x\n"))
			BUG("git stripspace: missing commented line");
		prefix[git_out_len - 3] = 0;
		comment_line_str = prefix;
		end = wt_status_locate_end(view, len);
		if (end < len)
			reject(129, "remove that line from the message", "the message hides the trailers: '%s'",
			       xstrndup(view + end, strcspn(view + end, "\n")));
	}
	string_list_clear(&unread, 0);
	free(probe);
	free(view);
	return text;
}

/*
 * spec 4.5, v14 5.3: E_CONFIG, after the common checks and the session: the
 * message MSG as it is committed, with this request's RID and the session's
 * agent and SID, reads its trailers back (the message itself counts: git
 * decodes it by i18n.commitEncoding), or the settings that change what git's
 * trailer parser reads are named. The committed text is returned.
 */
char *checked_record(const char *msg, const char *rid)
{
	struct string_list unread = STRING_LIST_INIT_DUP, names = STRING_LIST_INIT_DUP;
	char *record = trailer_message(msg, rid, agent, sid), *settings;
	int rc;

	free(trailer_probe(record, rid, agent, sid, &unread));
	if (!unread.nr)
		return record;
	git_records(&names, NULL, &rc, "config", "--null", "--name-only", "--get-regexp",
		    "^(core\\.comment(char|string)|trailer\\.separators|trailer\\..*\\.key)$");
	if (rc > 1)
		reject(128, NULL, "%s", git_failed());
	string_list_sort(&names);
	string_list_remove_duplicates(&names, 0);
	settings = quoted_list(&names);
	reject(128, NULL, "git config hides the trailers: %s%s%s%s", quoted_list(&unread),
	       *settings ? " (" : "", settings, *settings ? ")" : "");
}

/*
 * spec 4.3, then 4.6: what every contract command does first, in one place
 * (git refuses a broken state at each command's start with one function);
 * the contract branch it works with
 */
struct branch *start(int how)
{
	struct branch *b = branch_load(branch_name(), NULL, NULL);
	char *on;

	refuse_malformed(b, how & START_WARNS);
	if ((how & START_WRITES) && *b->tip)
		refuse_used(b->name, b->tip);
	if ((how & START_FULFILS) && (on = head_branch()) && starts_with(on, "ait/"))
		reject(128, NULL, "HEAD is on contract branch '%s'", on);
	if ((how & START_FULFILS) && *git("ls-files", "--unmerged"))
		reject(1, NULL, "%s", refusal_words(UNMERGED, NULL, NULL, NULL));
	session(!!(how & START_SESSION));
	return b;
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
	dir = xstrdup(absolute_path(dir));
	index = xstrfmt("%s/index", dir);
	git_run(NULL, index, NULL, "read-tree", treeish);
	git_feed(info, index, NULL, "update-index", "-z", "--index-info");
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
		strbuf_addf(&info, "100644 %s\t%s%c",
			    line1(git_run(item->util, NULL, NULL, "hash-object", "-w", "--stdin")), item->string, 0);
	return tree_with(*c->tip ? c->tip : EMPTY_TREE, &info);
}

/*
 * spec 4.10: a new tree may not add violations of the structural
 * invariants; those of T stay as they were (a git merge made them)
 */
static void checkin_invariants(struct checkin *c, struct branch *now)
{
	struct branch *after = branch_load(c->branch, NULL, c->tree);
	struct string_list before = STRING_LIST_INIT_DUP, then = STRING_LIST_INIT_DUP, added = STRING_LIST_INIT_DUP;
	struct string_list_item *v;

	if (after->errors.nr && !c->refusal)
		BUG("ait wrote a malformed contract: '%s': %s", after->errors.items[0].string,
		    (char *)after->errors.items[0].util);
	if (after->errors.nr)
		reject(1, c->violation_hint ? c->violation_hint(c, NULL) : NULL, "%s'%s': %s", c->refusal, after->errors.items[0].string,
		       (char *)after->errors.items[0].util);
	/* the published tree is read back once; its invariants are judged on what was read */
	invariants(now, now, NULL, &before);
	invariants(after, now, c->fulfilling, &then);
	for_each_string_list_item(v, &then)
		if (!string_list_has_string(&before, v->string))
			string_list_insert(&added, v->string)->util = v->util;
	if (added.nr)
		reject(1, c->violation_hint ? c->violation_hint(c, &added) : NULL, "%s%s",
		       c->refusal ? c->refusal : "", violation_words(added.items[0].util));
	string_list_clear(&added, 0);
	violations_clear(&before);
	violations_clear(&then);
	branch_free(after);
}

/* spec 4.4-4 (E_MOVED): what the checkin read changed meanwhile */
NORETURN void checkin_moved(struct checkin *c)
{
	reject(1, NULL, "'%s' moved while running; nothing changed", c->branch);
}

/* spec 4.4-1: T, the branch's tip now ("" when it has no commit) */
void checkin_tip(struct checkin *c)
{
	if (!branch_tip(c->branch, c->tip))
		*c->tip = 0;
}

/* what a checkin judged on one T; a new T starts without it */
static void checkin_reset(struct checkin *c)
{
	branch_free(c->at);
	free(c->tree);
	free((char *)c->second_parent);
	free((char *)c->ff);
	string_list_clear(&c->put, 1);
	c->at = NULL;
	c->tree = NULL;
	c->second_parent = c->ff = NULL;
	c->fulfilling = NULL;
}

/*
 * spec 4.4-1, 2: everything judged on the tip T the checkin holds; the new
 * tree, the invariants checked. Step 0 of fulfill asks the same (6.7.5-0).
 */
char *checkin_judge(struct checkin *c, judge_fn decide)
{
	struct branch *now;

	checkin_reset(c);
	c->at = now = branch_load(c->branch, *c->tip ? c->tip : NULL, NULL);
	/* a tip that a git commit or merge broke after start() (4.3-3) */
	refuse_malformed(now, 0);
	if (decide)
		decide(c);
	if (c->ff)
		return NULL;
	if (!c->tree)
		c->tree = checkin_tree(c);
	checkin_invariants(c, now);
	return c->tree;
}

/* The checkin owns T and its assembled output until the caller has reported it. */
void checkin_clear(struct checkin *c)
{
	checkin_reset(c);
	free((char *)c->record);
}

/*
 * spec 4.4: one checkin. JUDGE decides on T what to put; on a lost CAS the
 * whole command is judged again on the new tip, unless a file it puts moved.
 * A judge that names a commit to fast-forward to (6.5.6) moves the branch there.
 */
char *checkin_run(struct checkin *c, judge_fn decide)
{
	/* Register after a caller's stage lock so children are reaped before it is released. */
	guard_git_children();
	checkin_tip(c);
	for (;;) {
		char old[48], *tree = checkin_judge(c, decide);
		const char *commit;
		struct strvec args = STRVEC_INIT;
		struct string_list_item *item;
		int rc;

		/* a request that changes nothing is a noop (6.4.1): no commit */
		if (!c->ff && !c->second_parent && *c->tip && !strcmp(tree, c->at->tree->oid))
			return NULL;
		if (c->ff) {
			commit = c->ff;
		} else {
			strvec_pushl(&args, "commit-tree", tree, NULL);
			if (*c->tip)
				strvec_pushl(&args, "-p", c->tip, NULL);
			if (c->second_parent)
				strvec_pushl(&args, "-p", c->second_parent, NULL);
			commit = line1(git_argv(c->record, NULL, NULL, args.v));
			strvec_clear(&args);
		}
		/* 4.4-3: (a) no worktree uses the branch, judged by git; (b) the CAS */
		refuse_used(c->branch, commit);
		/* pipe_command sends input only after Git has registered the child for cleanup. */
		git_run(xstrfmt("update refs/heads/%s %s %s\n", c->branch, commit, *c->tip ? c->tip : NULL_OID),
			NULL, &rc, "update-ref", "-m", c->msg, "--stdin");
		if (!rc && c->ff)
			return xstrfmt("Fast-forward %.7s..%.7s\n", c->tip, commit);
		if (!rc)
			return c->no_trailers ? NULL : xstrfmt("[%s %s%s] %s\n", c->branch,
				line1(git("rev-parse", "--short", commit)), *c->tip ? "" : " (root-commit)",
				line1(xstrdup(c->msg)));
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
		free(git_argv(NULL, NULL, &rc, args.v));
		if (!rc)
			continue;
		if (c->retry_hint)
			retry_command = c->retry_hint(c, old);
		checkin_moved(c);
	}
}

/* Like Git's builtin/commit.c:1974, report after publication; porcelain comes first. */
void checkin_report(char *summary)
{
	show_code_output();
	if (summary)
		fputs(summary, stdout);
	free(summary);
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
	/* Keep the leaf itself: even a dangling hook symlink is an existing hook. */
	return xstrfmt("%s/reference-transaction", git_path("hooks"));
}

/* the hook is ait's (9.8); another one leaves listen to polling every second */
int hook_installed(void)
{
	struct strbuf now = STRBUF_INIT;
	char *path = hook_file();

	return !access(path, X_OK) && strbuf_read_file(&now, path, 0) >= 0 && !strcmp(now.buf, hook_text);
}

static void install_hook(void)
{
	char *path = hook_file();

	if (!create_file(path, hook_text, 0755)) {
		if (!hook_installed())
			warning("'%s' exists; 'ait listen' wakes by polling only", path);
		return;
	}
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
	session(0);
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
		git("branch", "-q", item->string, from);
		printf("branch %s\n", item->string);
		made++;
	}
	if (!n.local.nr && !made && !n.remote.nr) {
		struct string_list_item *r;
		struct checkin c = { .msg = "[CONTRACT] init", .no_trailers = 1 };

		for_each_string_list_item(r, &n.remotes) {
			int rc;
			char *out = git_run(NULL, NULL, &rc, "ls-remote", "--", r->string, "refs/heads/ait/*");

			if (rc)
				warning("cannot reach '%s'; skipped", r->string);
			else if (*out)
				reject(128, xstrfmt("fetch 'refs/heads/ait/*' from '%s', then '%s'", r->string,
						    hint_command("ait init")),
				       "'%s' has ait branches that are not fetched", r->string);
		}
		c.branch = xstrfmt("ait/%s", line1(git("var", "GIT_DEFAULT_BRANCH")));
		c.record = xstrfmt("%s\n", c.msg);
		string_list_init_dup(&c.put);
		free(checkin_run(&c, NULL));
		printf("branch %s\n", c.branch);
		checkin_clear(&c);
	}
	return 0;
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
	return pass_git(&args, 1, NULL);
}

static void local_or_refuse(struct names *n, const char *name)
{
	if (!string_list_has_string(&n->local, name))
		reject(128, init_source(n, name) ? hint_command("ait init") : NULL, "no local branch '%s'", name);
}

static int clean_work_tree(void)
{
	int rc;

	git_run(NULL, NULL, &rc, "diff", "--quiet", "HEAD");
	return !rc;
}

/* spec 6.2 */
static int cmd_checkout(int argc, const char **argv)
{
	struct names n;
	char *on;

	session(0);
	if (!contract_form(argc, argv, &n))
		return pass_to_git("checkout", argv);
	local_or_refuse(&n, argv[1]);
	write_state(checkout_file(), xstrfmt("%s\n", argv[1]));
	printf("On contract branch %s\n", argv[1]);
	on = head_branch();
	on = on ? xstrfmt("'%s'", on) : xstrdup("a detached HEAD");
	if (clean_work_tree())
		suggest("the work tree stays on %s; to enter the contract branch: '%s'",
			on, hint_command("ait switch %s", quoted(argv[1])));
	else
		suggest("the work tree stays on %s", on);
	return 0;
}

/* spec 6.3 */
static int cmd_switch(int argc, const char **argv)
{
	struct names n;
	struct strvec args = STRVEC_INIT;
	int rc;

	session(0);
	if (argc != 2 || !contract_form(argc, argv, &n))
		return pass_to_git("switch", argv);
	local_or_refuse(&n, argv[1]);
	if (!clean_work_tree())
		reject(128, NULL, "uncommitted changes in the work tree; commit them before entering '%s'", argv[1]);
	strvec_pushl(&args, "switch", argv[1], NULL);
	rc = git_pass(&args);
	if (!rc)
		suggest("contracts change only through ait commands; to go back: '%s'", hint_command("ait switch -"));
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
	/* ait's own output keeps bytes of 0x80 and up as the git it runs does (core.quotePath=false) */
	quote_path_fully = 0;
	if (argc < 2)
		reject(129, NULL, "no command");
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[1], commands[i].name))
			continue;
		setup();
		precompose_argv_prefix(argc, argv, NULL);
		status = commands[i].fn(argc - 1, argv + 1);
		if (fflush(stdout) || ferror(stdout))
			write_error(NULL);
		return status;
	}
	reject(129, NULL, "'%s' is not an ait command", argv[1]);
}
