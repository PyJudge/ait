#include "git-compat-util.h"
#include "abspath.h"

/* git v2.53.0 abspath.c:293 */
void strbuf_add_absolute_path(struct strbuf *sb, const char *path)
{
	if (!*path)
		die("The empty string is not a valid path");
	if (!is_absolute_path(path)) {
		struct stat cwd_stat, pwd_stat;
		size_t orig_len = sb->len;
		char *cwd = xgetcwd();
		char *pwd = getenv("PWD");
		if (pwd && strcmp(pwd, cwd) &&
		    !stat(cwd, &cwd_stat) &&
		    (cwd_stat.st_dev || cwd_stat.st_ino) &&
		    !stat(pwd, &pwd_stat) &&
		    pwd_stat.st_dev == cwd_stat.st_dev &&
		    pwd_stat.st_ino == cwd_stat.st_ino)
			strbuf_addstr(sb, pwd);
		else
			strbuf_addstr(sb, cwd);
		if (sb->len > orig_len && !is_dir_sep(sb->buf[sb->len - 1]))
			strbuf_addch(sb, '/');
		free(cwd);
	}
	strbuf_addstr(sb, path);
}

/* git v2.53.0 abspath.c:252 */
const char *absolute_path(const char *path)
{
	static struct strbuf sb = STRBUF_INIT;
	strbuf_reset(&sb);
	strbuf_add_absolute_path(&sb, path);
	return sb.buf;
}
