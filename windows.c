/*
 * ait's Windows build: the git functions git's compat/mingw.c and its
 * neighbours call that ait does not otherwise carry
 */
#include "git-compat-util.h"
#include "strbuf.h"
#include "run-command.h"

/* git v2.53.0 environment.c: core.symlinks, core.ignorecase, core.protectNTFS as git starts */
int has_symlinks = 1;
int ignore_case;
int protect_ntfs = 1;

/* git v2.53.0 common-exit.c:12, without trace2 */
int common_exit(const char *file UNUSED, int line UNUSED, int code)
{
	return code & 0xff;
}

/* ait has no trace2 (git-compat-util.h) */
void trace2_initialize_clock(void)
{
}

int trace2_exec_fl(const char *file UNUSED, int line UNUSED, const char *exe UNUSED, const char **argv UNUSED)
{
	return 0;
}

void trace2_exec_result_fl(const char *file UNUSED, int line UNUSED, int exec_id UNUSED, int code UNUSED)
{
}


/* ait reads no core.* for Windows: git's mingw_core_config is never called */
int git_config_bool(const char *name UNUSED, const char *value UNUSED)
{
	BUG("git_config_bool is not used by ait");
}

int config_error_nonbool(const char *var UNUSED)
{
	BUG("config_error_nonbool is not used by ait");
}

/* git v2.53.0 abspath.c:10 */
int is_directory(const char *path)
{
	struct stat st;
	return (!stat(path, &st) && S_ISDIR(st.st_mode));
}

/* git v2.53.0 run-command.c:1011 */
int run_command(struct child_process *cmd)
{
	int code;

	if (cmd->out < 0 || cmd->err < 0)
		BUG("run_command with a pipe can cause deadlock");

	code = start_command(cmd);
	if (code)
		return code;
	return finish_command(cmd);
}

/* git v2.53.0 strbuf.c:265 */
void strbuf_insert(struct strbuf *sb, size_t pos, const void *data, size_t len)
{
	strbuf_splice(sb, pos, 0, data, len);
}

/* git v2.53.0 wrapper.c:666 */
int xsnprintf(char *dst, size_t max, const char *fmt, ...)
{
	va_list ap;
	int len;

	va_start(ap, fmt);
	len = vsnprintf(dst, max, fmt, ap);
	va_end(ap);

	if (len < 0)
		die(_("unable to format message: %s"), fmt);
	if (len >= max)
		BUG("attempt to snprintf into too-small buffer");
	return len;
}

/* git v2.53.0 wrapper.c:429-510 */
char *git_mkdtemp(char *pattern);
int git_mkstemps_mode(char *pattern, int suffix_len, int mode);
int git_mkstemp_mode(char *pattern, int mode);

#undef TMP_MAX
#define TMP_MAX 16384

/*
 * Returns -1 on error, 0 if it created a directory, or an open file
 * descriptor to the created regular file.
 */
static int git_mkdstemps_mode(char *pattern, int suffix_len, int mode, bool dir)
{
	static const char letters[] =
		"abcdefghijklmnopqrstuvwxyz"
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"0123456789";
	static const int num_letters = ARRAY_SIZE(letters) - 1;
	static const char x_pattern[] = "XXXXXX";
	static const int num_x = ARRAY_SIZE(x_pattern) - 1;
	char *filename_template;
	size_t len;
	int fd, count;

	len = strlen(pattern);

	if (len < num_x + suffix_len) {
		errno = EINVAL;
		return -1;
	}

	if (strncmp(&pattern[len - num_x - suffix_len], x_pattern, num_x)) {
		errno = EINVAL;
		return -1;
	}

	/*
	 * Replace pattern's XXXXXX characters with randomness.
	 * Try TMP_MAX different filenames.
	 */
	filename_template = &pattern[len - num_x - suffix_len];
	for (count = 0; count < TMP_MAX; ++count) {
		int i;
		uint64_t v;
		if (csprng_bytes(&v, sizeof(v), 0) < 0)
			return error_errno("unable to get random bytes for temporary file");

		/* Fill in the random bits. */
		for (i = 0; i < num_x; i++) {
			filename_template[i] = letters[v % num_letters];
			v /= num_letters;
		}

		if (dir)
			fd = mkdir(pattern, mode);
		else
			fd = open(pattern, O_CREAT | O_EXCL | O_RDWR, mode);
		if (fd >= 0)
			return fd;
		/*
		 * Fatal error (EPERM, ENOSPC etc).
		 * It doesn't make sense to loop.
		 */
		if (errno != EEXIST)
			break;
	}
	/* We return the null string if we can't find a unique file name.  */
	pattern[0] = '\0';
	return -1;
}

char *git_mkdtemp(char *pattern)
{
	return git_mkdstemps_mode(pattern, 0, 0700, true) ? NULL : pattern;
}

int git_mkstemps_mode(char *pattern, int suffix_len, int mode)
{
	return git_mkdstemps_mode(pattern, suffix_len, mode, false);
}

int git_mkstemp_mode(char *pattern, int mode)
{
	/* mkstemp is just mkstemps with no suffix */
	return git_mkstemps_mode(pattern, 0, mode);
}

/* git v2.53.0 symlinks.c: ait keeps no lstat cache (git-compat-util.h) */
#undef invalidate_lstat_cache
void invalidate_lstat_cache(void)
{
}
