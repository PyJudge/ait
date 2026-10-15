#ifndef RUN_COMMAND_H
#define RUN_COMMAND_H
#include "strvec.h"
struct strbuf;

/* git v2.53.0 run-command.h:44-150 */
struct child_process {

	/**
	 * The .args is a `struct strvec', use that API to manipulate
	 * it, e.g. strvec_pushv() to add an existing "const char **"
	 * vector.
	 *
	 * If the command to run is a git command, set the first
	 * element in the strvec to the command name without the
	 * 'git-' prefix and set .git_cmd = 1.
	 *
	 * The memory in .args will be cleaned up automatically during
	 * `finish_command` (or during `start_command` when it is unsuccessful).
	 */
	struct strvec args;

	/**
	 * Like .args the .env is a `struct strvec'.
	 *
	 * To modify the environment of the sub-process, specify an array of
	 * environment settings. Each string in the array manipulates the
	 * environment.
	 *
	 * - If the string is of the form "VAR=value", i.e. it contains '='
	 *   the variable is added to the child process's environment.
	 *
	 * - If the string does not contain '=', it names an environment
	 *   variable that will be removed from the child process's environment.
	 *
	 * The memory in .env will be cleaned up automatically during
	 * `finish_command` (or during `start_command` when it is unsuccessful).
	 */
	struct strvec env;
	pid_t pid;

	int trace2_child_id;
	uint64_t trace2_child_us_start;
	const char *trace2_child_class;
	const char *trace2_hook_name;

	/*
	 * Using .in, .out, .err:
	 * - Specify 0 for no redirections. No new file descriptor is allocated.
	 * (child inherits stdin, stdout, stderr from parent).
	 * - Specify -1 to have a pipe allocated as follows:
	 *     .in: returns the writable pipe end; parent writes to it,
	 *          the readable pipe end becomes child's stdin
	 *     .out, .err: returns the readable pipe end; parent reads from
	 *          it, the writable pipe end becomes child's stdout/stderr
	 *   The caller of start_command() must close the returned FDs
	 *   after it has completed reading from/writing to it!
	 * - Specify > 0 to set a channel to a particular FD as follows:
	 *     .in: a readable FD, becomes child's stdin
	 *     .out: a writable FD, becomes child's stdout/stderr
	 *     .err: a writable FD, becomes child's stderr
	 *   The specified FD is closed by start_command(), even in case
	 *   of errors!
	 */
	int in;
	int out;
	int err;

	/**
	 * To specify a new initial working directory for the sub-process,
	 * specify it in the .dir member.
	 */
	const char *dir;

	unsigned no_stdin:1;
	unsigned no_stdout:1;
	unsigned no_stderr:1;
	unsigned git_cmd:1; /* if this is to be git sub-command */

	/**
	 * If the program cannot be found, the functions return -1 and set
	 * errno to ENOENT. Normally, an error message is printed, but if
	 * .silent_exec_failure is set to 1, no message is printed for this
	 * special error condition.
	 */
	unsigned silent_exec_failure:1;

	/**
	 * Run the command from argv[0] using a shell (but note that we may
	 * still optimize out the shell call if the command contains no
	 * metacharacters). Note that further arguments to the command in
	 * argv[1], etc, do not need to be shell-quoted.
	 */
	unsigned use_shell:1;

	/**
	 * Release any open file handles to the object store before running
	 * the command; This is necessary e.g. when the spawned process may
	 * want to repack because that would delete `.pack` files (and on
	 * Windows, you cannot delete files that are still in use).
	 */
	unsigned close_object_store:1;

	unsigned stdout_to_stderr:1;
	unsigned clean_on_exit:1;
	unsigned wait_after_clean:1;
	void (*clean_on_exit_handler)(struct child_process *process);
};

#define CHILD_PROCESS_INIT { \
	.args = STRVEC_INIT, \
	.env = STRVEC_INIT, \
}

void child_process_init(struct child_process *);
void child_process_clear(struct child_process *);
int start_command(struct child_process *);
void prepare_child_cleanup(void);
int finish_command(struct child_process *);
int pipe_command(struct child_process *cmd,
		 const char *in, size_t in_len,
		 struct strbuf *out, size_t out_hint,
		 struct strbuf *err, size_t err_hint);
#endif