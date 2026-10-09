#include "git-compat-util.h"
#include "run-command.h"
#include "strbuf.h"
#include "string-list.h"

/* git v2.53.0 run-command.c:22 */
void child_process_init(struct child_process *child)
{
	struct child_process blank = CHILD_PROCESS_INIT;
	memcpy(child, &blank, sizeof(*child));
}

/* git v2.53.0 run-command.c:28 */
void child_process_clear(struct child_process *child)
{
	strvec_clear(&child->args);
	strvec_clear(&child->env);
}

/* git v2.53.0 run-command.c:126 */
static inline void close_pair(int fd[2])
{
	close(fd[0]);
	close(fd[1]);
}

/* git v2.53.0 run-command.c:132-172 */
int is_executable(const char *name)
{
	struct stat st;

	if (stat(name, &st) || /* stat, not lstat */
	    !S_ISREG(st.st_mode))
		return 0;

#if defined(GIT_WINDOWS_NATIVE)
	/*
	 * On Windows there is no executable bit. The file extension
	 * indicates whether it can be run as an executable, and Git
	 * has special-handling to detect scripts and launch them
	 * through the indicated script interpreter. We test for the
	 * file extension first because virus scanners may make
	 * it quite expensive to open many files.
	 */
	if (ends_with(name, ".exe"))
		return S_IXUSR;

{
	/*
	 * Now that we know it does not have an executable extension,
	 * peek into the file instead.
	 */
	char buf[3] = { 0 };
	int n;
	int fd = open(name, O_RDONLY);
	st.st_mode &= ~S_IXUSR;
	if (fd >= 0) {
		n = read(fd, buf, 2);
		if (n == 2)
			/* look for a she-bang */
			if (!strcmp(buf, "#!"))
				st.st_mode |= S_IXUSR;
		close(fd);
	}
}
#endif
	return st.st_mode & S_IXUSR;
}

/* git v2.53.0 run-command.c:174-224 */
#ifndef locate_in_PATH
/*
 * Search $PATH for a command.  This emulates the path search that
 * execvp would perform, without actually executing the command so it
 * can be used before fork() to prepare to run a command using
 * execve() or after execvp() to diagnose why it failed.
 *
 * The caller should ensure that file contains no directory
 * separators.
 *
 * Returns the path to the command, as found in $PATH or NULL if the
 * command could not be found.  The caller inherits ownership of the memory
 * used to store the resultant path.
 *
 * This should not be used on Windows, where the $PATH search rules
 * are more complicated (e.g., a search for "foo" should find
 * "foo.exe").
 */
static char *locate_in_PATH(const char *file)
{
	const char *p = getenv("PATH");
	struct strbuf buf = STRBUF_INIT;

	if (!p || !*p)
		return NULL;

	while (1) {
		const char *end = strchrnul(p, ':');

		strbuf_reset(&buf);

		/* POSIX specifies an empty entry as the current directory. */
		if (end != p) {
			strbuf_add(&buf, p, end - p);
			strbuf_addch(&buf, '/');
		}
		strbuf_addstr(&buf, file);

		if (is_executable(buf.buf))
			return strbuf_detach(&buf, NULL);

		if (!*end)
			break;
		p = end + 1;
	}

	strbuf_release(&buf);
	return NULL;
}
#endif


/* git v2.53.0 run-command.c:225 */
int exists_in_PATH(const char *command)
{
	char *r = locate_in_PATH(command);
	int found = r != NULL;
	free(r);
	return found;
}

/* git v2.53.0 run-command.c:278 */
char *git_shell_path(void)
{
#ifndef GIT_WINDOWS_NATIVE
	return xstrdup(SHELL_PATH);
#else
	char *p = locate_in_PATH("sh");
	convert_slashes(p);
	return p;
#endif
}

/* git v2.53.0 run-command.c:289 */
static const char **prepare_shell_cmd(struct strvec *out, const char **argv)
{
	if (!argv[0])
		BUG("shell command is empty");

	if (strcspn(argv[0], "|&;<>()$`\\\"' \t\n*?[#~=%") != strlen(argv[0])) {
		strvec_push_nodup(out, git_shell_path());
		strvec_push(out, "-c");

		/*
		 * If we have no extra arguments, we do not even need to
		 * bother with the "$@" magic.
		 */
		if (!argv[1])
			strvec_push(out, argv[0]);
		else
			strvec_pushf(out, "%s \"$@\"", argv[0]);
	}

	strvec_pushv(out, argv);
	return out->v;
}

/* git v2.53.0 run-command.c:312-549 */
#ifndef GIT_WINDOWS_NATIVE
static int child_notifier = -1;

enum child_errcode {
	CHILD_ERR_CHDIR,
	CHILD_ERR_DUP2,
	CHILD_ERR_CLOSE,
	CHILD_ERR_SIGPROCMASK,
	CHILD_ERR_SILENT,
	CHILD_ERR_ERRNO
};

struct child_err {
	enum child_errcode err;
	int syserr; /* errno */
};

static void child_die(enum child_errcode err)
{
	struct child_err buf;

	buf.err = err;
	buf.syserr = errno;

	/* write(2) on buf smaller than PIPE_BUF (min 512) is atomic: */
	xwrite(child_notifier, &buf, sizeof(buf));
	_exit(1);
}

static void child_dup2(int fd, int to)
{
	if (dup2(fd, to) < 0)
		child_die(CHILD_ERR_DUP2);
}

static void child_close(int fd)
{
	if (close(fd))
		child_die(CHILD_ERR_CLOSE);
}

static void child_close_pair(int fd[2])
{
	child_close(fd[0]);
	child_close(fd[1]);
}

static void child_error_fn(const char *err UNUSED, va_list params UNUSED)
{
	const char msg[] = "error() should not be called in child\n";
	xwrite(2, msg, sizeof(msg) - 1);
}

static void child_warn_fn(const char *err UNUSED, va_list params UNUSED)
{
	const char msg[] = "warn() should not be called in child\n";
	xwrite(2, msg, sizeof(msg) - 1);
}

static void NORETURN child_die_fn(const char *err UNUSED, va_list params UNUSED)
{
	const char msg[] = "die() should not be called in child\n";
	xwrite(2, msg, sizeof(msg) - 1);
	_exit(2);
}

/* this runs in the parent process */
static void child_err_spew(struct child_process *cmd, struct child_err *cerr)
{
	static void (*old_errfn)(const char *err, va_list params);
	report_fn die_message_routine = get_die_message_routine();

	old_errfn = get_error_routine();
	set_error_routine(die_message_routine);
	errno = cerr->syserr;

	switch (cerr->err) {
	case CHILD_ERR_CHDIR:
		error_errno("exec '%s': cd to '%s' failed",
			    cmd->args.v[0], cmd->dir);
		break;
	case CHILD_ERR_DUP2:
		error_errno("dup2() in child failed");
		break;
	case CHILD_ERR_CLOSE:
		error_errno("close() in child failed");
		break;
	case CHILD_ERR_SIGPROCMASK:
		error_errno("sigprocmask failed restoring signals");
		break;
	case CHILD_ERR_SILENT:
		break;
	case CHILD_ERR_ERRNO:
		error_errno("cannot exec '%s'", cmd->args.v[0]);
		break;
	}
	set_error_routine(old_errfn);
}

static int prepare_cmd(struct strvec *out, const struct child_process *cmd)
{
	if (!cmd->args.v[0])
		BUG("command is empty");

	/*
	 * Add SHELL_PATH so in the event exec fails with ENOEXEC we can
	 * attempt to interpret the command with 'sh'.
	 */
	strvec_push(out, SHELL_PATH);

	if (cmd->git_cmd) {
		prepare_git_cmd(out, cmd->args.v);
	} else if (cmd->use_shell) {
		prepare_shell_cmd(out, cmd->args.v);
	} else {
		strvec_pushv(out, cmd->args.v);
	}

	/*
	 * If there are no dir separator characters in the command then perform
	 * a path lookup and use the resolved path as the command to exec. If
	 * there are dir separator characters, we have exec attempt to invoke
	 * the command directly.
	 */
	if (!has_dir_sep(out->v[1])) {
		char *program = locate_in_PATH(out->v[1]);
		if (program) {
			free((char *)out->v[1]);
			out->v[1] = program;
		} else {
			strvec_clear(out);
			errno = ENOENT;
			return -1;
		}
	}

	return 0;
}

static char **prep_childenv(const char *const *deltaenv)
{
	extern char **environ;
	char **childenv;
	struct string_list env = STRING_LIST_INIT_DUP;
	struct strbuf key = STRBUF_INIT;
	const char *const *p;
	int i;

	/* Construct a sorted string list consisting of the current environ */
	for (p = (const char *const *) environ; p && *p; p++) {
		const char *equals = strchr(*p, '=');

		if (equals) {
			strbuf_reset(&key);
			strbuf_add(&key, *p, equals - *p);
			string_list_append(&env, key.buf)->util = (void *) *p;
		} else {
			string_list_append(&env, *p)->util = (void *) *p;
		}
	}
	string_list_sort(&env);

	/* Merge in 'deltaenv' with the current environ */
	for (p = deltaenv; p && *p; p++) {
		const char *equals = strchr(*p, '=');

		if (equals) {
			/* ('key=value'), insert or replace entry */
			strbuf_reset(&key);
			strbuf_add(&key, *p, equals - *p);
			string_list_insert(&env, key.buf)->util = (void *) *p;
		} else {
			/* otherwise ('key') remove existing entry */
			string_list_remove(&env, *p, 0);
		}
	}

	/* Create an array of 'char *' to be used as the childenv */
	ALLOC_ARRAY(childenv, env.nr + 1);
	for (i = 0; i < env.nr; i++)
		childenv[i] = env.items[i].util;
	childenv[env.nr] = NULL;

	string_list_clear(&env, 0);
	strbuf_release(&key);
	return childenv;
}

struct atfork_state {
#ifndef NO_PTHREADS
	int cs;
#endif
	sigset_t old;
};

#define CHECK_BUG(err, msg) \
	do { \
		int e = (err); \
		if (e) \
			BUG("%s: %s", msg, strerror(e)); \
	} while(0)

static void atfork_prepare(struct atfork_state *as)
{
	sigset_t all;

	/*
	 * POSIX says sigfillset() can fail, but an overly clever
	 * compiler can see through the header files and decide
	 * it cannot fail on a particular platform it is compiling for,
	 * triggering -Wunreachable-code false positive.
	 */
	if (NOT_CONSTANT(sigfillset(&all)))
		die_errno("sigfillset");
#ifdef NO_PTHREADS
	if (sigprocmask(SIG_SETMASK, &all, &as->old))
		die_errno("sigprocmask");
#else
	CHECK_BUG(pthread_sigmask(SIG_SETMASK, &all, &as->old),
		"blocking all signals");
	CHECK_BUG(pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &as->cs),
		"disabling cancellation");
#endif
}

static void atfork_parent(struct atfork_state *as)
{
#ifdef NO_PTHREADS
	if (sigprocmask(SIG_SETMASK, &as->old, NULL))
		die_errno("sigprocmask");
#else
	CHECK_BUG(pthread_setcancelstate(as->cs, NULL),
		"re-enabling cancellation");
	CHECK_BUG(pthread_sigmask(SIG_SETMASK, &as->old, NULL),
		"restoring signal mask");
#endif
}
#endif /* GIT_WINDOWS_NATIVE */

/* git v2.53.0 run-command.c:551 */
static inline void set_cloexec(int fd)
{
	int flags = fcntl(fd, F_GETFD);
	if (flags >= 0)
		fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

/* git v2.53.0 run-command.c:558 */
static int wait_or_whine(pid_t pid, const char *argv0, int in_signal)
{
	int status, code = -1;
	pid_t waiting;
	int failed_errno = 0;

	while ((waiting = waitpid(pid, &status, 0)) < 0 && errno == EINTR)
		;	/* nothing */

	if (waiting < 0) {
		failed_errno = errno;
		if (!in_signal)
			error_errno("waitpid for %s failed", argv0);
	} else if (waiting != pid) {
		if (!in_signal)
			error("waitpid is confused (%s)", argv0);
	} else if (WIFSIGNALED(status)) {
		code = WTERMSIG(status);
		if (!in_signal && code != SIGINT && code != SIGQUIT && code != SIGPIPE)
			error("%s died of signal %d", argv0, code);
		/*
		 * This return value is chosen so that code & 0xff
		 * mimics the exit code that a POSIX shell would report for
		 * a program that died from this signal.
		 */
		code += 128;
	} else if (WIFEXITED(status)) {
		code = WEXITSTATUS(status);
	} else {
		if (!in_signal)
			error("waitpid is confused (%s)", argv0);
	}

	if (!in_signal)
		clear_child_for_cleanup(pid);

	errno = failed_errno;
	return code;
}

/* git v2.53.0 run-command.c:677-991 */
int start_command(struct child_process *cmd)
{
	int need_in, need_out, need_err;
	int fdin[2], fdout[2], fderr[2];
	int failed_errno;
	const char *str;

	/*
	 * In case of errors we must keep the promise to close FDs
	 * that have been passed in via ->in and ->out.
	 */

	need_in = !cmd->no_stdin && cmd->in < 0;
	if (need_in) {
		if (pipe(fdin) < 0) {
			failed_errno = errno;
			if (cmd->out > 0)
				close(cmd->out);
			str = "standard input";
			goto fail_pipe;
		}
		cmd->in = fdin[1];
	}

	need_out = !cmd->no_stdout
		&& !cmd->stdout_to_stderr
		&& cmd->out < 0;
	if (need_out) {
		if (pipe(fdout) < 0) {
			failed_errno = errno;
			if (need_in)
				close_pair(fdin);
			else if (cmd->in)
				close(cmd->in);
			str = "standard output";
			goto fail_pipe;
		}
		cmd->out = fdout[0];
	}

	need_err = !cmd->no_stderr && cmd->err < 0;
	if (need_err) {
		if (pipe(fderr) < 0) {
			failed_errno = errno;
			if (need_in)
				close_pair(fdin);
			else if (cmd->in)
				close(cmd->in);
			if (need_out)
				close_pair(fdout);
			else if (cmd->out)
				close(cmd->out);
			str = "standard error";
fail_pipe:
			error("cannot create %s pipe for %s: %s",
				str, cmd->args.v[0], strerror(failed_errno));
			child_process_clear(cmd);
			errno = failed_errno;
			return -1;
		}
		cmd->err = fderr[0];
	}

	trace2_child_start(cmd);
	trace_run_command(cmd);

	fflush(NULL);

	if (cmd->close_object_store)
		odb_close(the_repository->objects);

#ifndef GIT_WINDOWS_NATIVE
{
	int notify_pipe[2];
	int null_fd = -1;
	char **childenv;
	struct strvec argv = STRVEC_INIT;
	struct child_err cerr;
	struct atfork_state as;

	if (prepare_cmd(&argv, cmd) < 0) {
		failed_errno = errno;
		cmd->pid = -1;
		if (!cmd->silent_exec_failure)
			error_errno("cannot run %s", cmd->args.v[0]);
		goto end_of_spawn;
	}

	trace_argv_printf(&argv.v[1], "trace: start_command:");

	if (pipe(notify_pipe))
		notify_pipe[0] = notify_pipe[1] = -1;

	if (cmd->no_stdin || cmd->no_stdout || cmd->no_stderr) {
		null_fd = xopen("/dev/null", O_RDWR | O_CLOEXEC);
		set_cloexec(null_fd);
	}

	childenv = prep_childenv(cmd->env.v);
	atfork_prepare(&as);

	/*
	 * NOTE: In order to prevent deadlocking when using threads special
	 * care should be taken with the function calls made in between the
	 * fork() and exec() calls.  No calls should be made to functions which
	 * require acquiring a lock (e.g. malloc) as the lock could have been
	 * held by another thread at the time of forking, causing the lock to
	 * never be released in the child process.  This means only
	 * Async-Signal-Safe functions are permitted in the child.
	 */
	cmd->pid = fork();
	failed_errno = errno;
	if (!cmd->pid) {
		int sig;
		/*
		 * Ensure the default die/error/warn routines do not get
		 * called, they can take stdio locks and malloc.
		 */
		set_die_routine(child_die_fn);
		set_error_routine(child_error_fn);
		set_warn_routine(child_warn_fn);

		close(notify_pipe[0]);
		set_cloexec(notify_pipe[1]);
		child_notifier = notify_pipe[1];

		if (cmd->no_stdin)
			child_dup2(null_fd, 0);
		else if (need_in) {
			child_dup2(fdin[0], 0);
			child_close_pair(fdin);
		} else if (cmd->in) {
			child_dup2(cmd->in, 0);
			child_close(cmd->in);
		}

		if (cmd->no_stderr)
			child_dup2(null_fd, 2);
		else if (need_err) {
			child_dup2(fderr[1], 2);
			child_close_pair(fderr);
		} else if (cmd->err > 1) {
			child_dup2(cmd->err, 2);
			child_close(cmd->err);
		}

		if (cmd->no_stdout)
			child_dup2(null_fd, 1);
		else if (cmd->stdout_to_stderr)
			child_dup2(2, 1);
		else if (need_out) {
			child_dup2(fdout[1], 1);
			child_close_pair(fdout);
		} else if (cmd->out > 1) {
			child_dup2(cmd->out, 1);
			child_close(cmd->out);
		}

		if (cmd->dir && chdir(cmd->dir))
			child_die(CHILD_ERR_CHDIR);

		/*
		 * restore default signal handlers here, in case
		 * we catch a signal right before execve below
		 */
		for (sig = 1; sig < NSIG; sig++) {
			/* ignored signals get reset to SIG_DFL on execve */
			if (signal(sig, SIG_DFL) == SIG_IGN)
				signal(sig, SIG_IGN);
		}

		if (sigprocmask(SIG_SETMASK, &as.old, NULL) != 0)
			child_die(CHILD_ERR_SIGPROCMASK);

		/*
		 * Attempt to exec using the command and arguments starting at
		 * argv.argv[1].  argv.argv[0] contains SHELL_PATH which will
		 * be used in the event exec failed with ENOEXEC at which point
		 * we will try to interpret the command using 'sh'.
		 */
		execve(argv.v[1], (char *const *) argv.v + 1,
		       (char *const *) childenv);
		if (errno == ENOEXEC)
			execve(argv.v[0], (char *const *) argv.v,
			       (char *const *) childenv);

		if (cmd->silent_exec_failure && errno == ENOENT)
			child_die(CHILD_ERR_SILENT);
		child_die(CHILD_ERR_ERRNO);
	}
	atfork_parent(&as);
	if (cmd->pid < 0)
		error_errno("cannot fork() for %s", cmd->args.v[0]);
	else if (cmd->clean_on_exit)
		mark_child_for_cleanup(cmd->pid, cmd);

	/*
	 * Wait for child's exec. If the exec succeeds (or if fork()
	 * failed), EOF is seen immediately by the parent. Otherwise, the
	 * child process sends a child_err struct.
	 * Note that use of this infrastructure is completely advisory,
	 * therefore, we keep error checks minimal.
	 */
	close(notify_pipe[1]);
	if (xread(notify_pipe[0], &cerr, sizeof(cerr)) == sizeof(cerr)) {
		/*
		 * At this point we know that fork() succeeded, but exec()
		 * failed. Errors have been reported to our stderr.
		 */
		wait_or_whine(cmd->pid, cmd->args.v[0], 0);
		child_err_spew(cmd, &cerr);
		failed_errno = errno;
		cmd->pid = -1;
	}
	close(notify_pipe[0]);

	if (null_fd >= 0)
		close(null_fd);
	strvec_clear(&argv);
	free(childenv);
}
end_of_spawn:

#else
{
	int fhin = 0, fhout = 1, fherr = 2;
	const char **sargv = cmd->args.v;
	struct strvec nargv = STRVEC_INIT;

	if (cmd->no_stdin)
		fhin = open("/dev/null", O_RDWR);
	else if (need_in)
		fhin = dup(fdin[0]);
	else if (cmd->in)
		fhin = dup(cmd->in);

	if (cmd->no_stderr)
		fherr = open("/dev/null", O_RDWR);
	else if (need_err)
		fherr = dup(fderr[1]);
	else if (cmd->err > 2)
		fherr = dup(cmd->err);

	if (cmd->no_stdout)
		fhout = open("/dev/null", O_RDWR);
	else if (cmd->stdout_to_stderr)
		fhout = dup(fherr);
	else if (need_out)
		fhout = dup(fdout[1]);
	else if (cmd->out > 1)
		fhout = dup(cmd->out);

	if (cmd->git_cmd)
		cmd->args.v = prepare_git_cmd(&nargv, sargv);
	else if (cmd->use_shell)
		cmd->args.v = prepare_shell_cmd(&nargv, sargv);

	trace_argv_printf(cmd->args.v, "trace: start_command:");
	cmd->pid = mingw_spawnvpe(cmd->args.v[0], cmd->args.v,
				  (char**) cmd->env.v,
				  cmd->dir, fhin, fhout, fherr);
	failed_errno = errno;
	if (cmd->pid < 0 && (!cmd->silent_exec_failure || errno != ENOENT))
		error_errno("cannot spawn %s", cmd->args.v[0]);
	if (cmd->clean_on_exit && cmd->pid >= 0)
		mark_child_for_cleanup(cmd->pid, cmd);

	strvec_clear(&nargv);
	cmd->args.v = sargv;
	if (fhin != 0)
		close(fhin);
	if (fhout != 1)
		close(fhout);
	if (fherr != 2)
		close(fherr);
}
#endif

	if (cmd->pid < 0) {
		trace2_child_exit(cmd, -1);

		if (need_in)
			close_pair(fdin);
		else if (cmd->in)
			close(cmd->in);
		if (need_out)
			close_pair(fdout);
		else if (cmd->out)
			close(cmd->out);
		if (need_err)
			close_pair(fderr);
		else if (cmd->err)
			close(cmd->err);
		child_process_clear(cmd);
		errno = failed_errno;
		return -1;
	}

	if (need_in)
		close(fdin[0]);
	else if (cmd->in)
		close(cmd->in);

	if (need_out)
		close(fdout[1]);
	else if (cmd->out)
		close(cmd->out);

	if (need_err)
		close(fderr[1]);
	else if (cmd->err)
		close(cmd->err);

	return 0;
}

/* git v2.53.0 run-command.c:993 */
int finish_command(struct child_process *cmd)
{
	int ret = wait_or_whine(cmd->pid, cmd->args.v[0], 0);
	trace2_child_exit(cmd, ret);
	child_process_clear(cmd);
	invalidate_lstat_cache();
	return ret;
}

/* git v2.53.0 run-command.c:1295-1316 */
struct io_pump {
	/* initialized by caller */
	int fd;
	int type; /* POLLOUT or POLLIN */
	union {
		struct {
			const char *buf;
			size_t len;
		} out;
		struct {
			struct strbuf *buf;
			size_t hint;
		} in;
	} u;

	/* returned by pump_io */
	int error; /* 0 for success, otherwise errno */

	/* internal use */
	struct pollfd *pfd;
};


/* git v2.53.0 run-command.c:1317 */
static int pump_io_round(struct io_pump *slots, int nr, struct pollfd *pfd)
{
	int pollsize = 0;
	int i;

	for (i = 0; i < nr; i++) {
		struct io_pump *io = &slots[i];
		if (io->fd < 0)
			continue;
		pfd[pollsize].fd = io->fd;
		pfd[pollsize].events = io->type;
		io->pfd = &pfd[pollsize++];
	}

	if (!pollsize)
		return 0;

	if (poll(pfd, pollsize, -1) < 0) {
		if (errno == EINTR)
			return 1;
		die_errno("poll failed");
	}

	for (i = 0; i < nr; i++) {
		struct io_pump *io = &slots[i];

		if (io->fd < 0)
			continue;

		if (!(io->pfd->revents & (POLLOUT|POLLIN|POLLHUP|POLLERR|POLLNVAL)))
			continue;

		if (io->type == POLLOUT) {
			ssize_t len;

			/*
			 * Don't use xwrite() here. It loops forever on EAGAIN,
			 * and we're in our own poll() loop here.
			 *
			 * Note that we lose xwrite()'s handling of MAX_IO_SIZE
			 * and EINTR, so we have to implement those ourselves.
			 */
			len = write(io->fd, io->u.out.buf,
				    io->u.out.len <= MAX_IO_SIZE ?
				    io->u.out.len : MAX_IO_SIZE);
			if (len < 0) {
				if (errno != EINTR && errno != EAGAIN &&
				    errno != ENOSPC) {
					io->error = errno;
					close(io->fd);
					io->fd = -1;
				}
			} else {
				io->u.out.buf += len;
				io->u.out.len -= len;
				if (!io->u.out.len) {
					close(io->fd);
					io->fd = -1;
				}
			}
		}

		if (io->type == POLLIN) {
			ssize_t len = strbuf_read_once(io->u.in.buf,
						       io->fd, io->u.in.hint);
			if (len < 0)
				io->error = errno;
			if (len <= 0) {
				close(io->fd);
				io->fd = -1;
			}
		}
	}

	return 1;
}

/* git v2.53.0 run-command.c:1394 */
static int pump_io(struct io_pump *slots, int nr)
{
	struct pollfd *pfd;
	int i;

	for (i = 0; i < nr; i++)
		slots[i].error = 0;

	ALLOC_ARRAY(pfd, nr);
	while (pump_io_round(slots, nr, pfd))
		; /* nothing */
	free(pfd);

	/* There may be multiple errno values, so just pick the first. */
	for (i = 0; i < nr; i++) {
		if (slots[i].error) {
			errno = slots[i].error;
			return -1;
		}
	}
	return 0;
}

/* git v2.53.0 run-command.c:1418 */
int pipe_command(struct child_process *cmd,
		 const char *in, size_t in_len,
		 struct strbuf *out, size_t out_hint,
		 struct strbuf *err, size_t err_hint)
{
	struct io_pump io[3];
	int nr = 0;

	if (in)
		cmd->in = -1;
	if (out)
		cmd->out = -1;
	if (err)
		cmd->err = -1;

	if (start_command(cmd) < 0)
		return -1;

	if (in) {
		if (enable_pipe_nonblock(cmd->in) < 0) {
			error_errno("unable to make pipe non-blocking");
			close(cmd->in);
			if (out)
				close(cmd->out);
			if (err)
				close(cmd->err);
			return -1;
		}
		io[nr].fd = cmd->in;
		io[nr].type = POLLOUT;
		io[nr].u.out.buf = in;
		io[nr].u.out.len = in_len;
		nr++;
	}
	if (out) {
		io[nr].fd = cmd->out;
		io[nr].type = POLLIN;
		io[nr].u.in.buf = out;
		io[nr].u.in.hint = out_hint;
		nr++;
	}
	if (err) {
		io[nr].fd = cmd->err;
		io[nr].type = POLLIN;
		io[nr].u.in.buf = err;
		io[nr].u.in.hint = err_hint;
		nr++;
	}

	if (pump_io(io, nr) < 0) {
		finish_command(cmd); /* throw away exit code */
		return -1;
	}

	return finish_command(cmd);
}
