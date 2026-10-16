#include "git-compat-util.h"
#include "tempfile.h"
#include "abspath.h"
#include "sigchain.h"

/* git v2.53.0 tempfile.c:53 */
static VOLATILE_LIST_HEAD(tempfile_list);

/* git v2.53.0 tempfile.c:55 */
static int remove_template_directory(struct tempfile *tempfile,
				      int in_signal_handler)
{
	if (tempfile->directory) {
		if (in_signal_handler)
			return rmdir(tempfile->directory);
		else
			return rmdir_or_warn(tempfile->directory);
	}

	return 0;
}

/* git v2.53.0 tempfile.c:68 */
static void remove_tempfiles(int in_signal_handler)
{
	pid_t me = getpid();
	volatile struct volatile_list_head *pos;

	list_for_each(pos, &tempfile_list) {
		struct tempfile *p = list_entry(pos, struct tempfile, list);

		if (!is_tempfile_active(p) || p->owner != me)
			continue;

		if (p->fd >= 0)
			close(p->fd);

		if (in_signal_handler)
			unlink(p->filename.buf);
		else
			unlink_or_warn(p->filename.buf);
		remove_template_directory(p, in_signal_handler);
	}
}

/* git v2.53.0 tempfile.c:90 */
static void remove_tempfiles_on_exit(void)
{
	remove_tempfiles(0);
}

/* git v2.53.0 tempfile.c:95 */
static void remove_tempfiles_on_signal(int signo)
{
	remove_tempfiles(1);
	sigchain_pop(signo);
	raise(signo);
}

/* git v2.53.0 tempfile.c:102 */
static struct tempfile *new_tempfile(void)
{
	struct tempfile *tempfile = xmalloc(sizeof(*tempfile));
	tempfile->fd = -1;
	tempfile->fp = NULL;
	tempfile->owner = 0;
	INIT_LIST_HEAD(&tempfile->list);
	strbuf_init(&tempfile->filename, 0);
	tempfile->directory = NULL;
	return tempfile;
}

/* git v2.53.0 tempfile.c:114 */
static void activate_tempfile(struct tempfile *tempfile)
{
	static int initialized;

	if (!initialized) {
		sigchain_push_common(remove_tempfiles_on_signal);
		atexit(remove_tempfiles_on_exit);
		initialized = 1;
	}

	volatile_list_add(&tempfile->list, &tempfile_list);
	tempfile->owner = getpid();
}

/* git v2.53.0 tempfile.c:128 */
static void deactivate_tempfile(struct tempfile *tempfile)
{
	volatile_list_del(&tempfile->list);
	strbuf_release(&tempfile->filename);
	free(tempfile->directory);
	free(tempfile);
}

/* git v2.53.0 tempfile.c:164 */
struct tempfile *register_tempfile(const char *path)
{
	struct tempfile *tempfile = new_tempfile();
	strbuf_add_absolute_path(&tempfile->filename, path);
	activate_tempfile(tempfile);
	return tempfile;
}

/* git v2.53.0 tempfile.c:205-249 */
struct tempfile *mks_tempfile_dt(const char *directory_template,
				 const char *filename)
{
	struct tempfile *tempfile;
	const char *tmpdir;
	struct strbuf sb = STRBUF_INIT;
	int fd;
	size_t directorylen;

	if (!ends_with(directory_template, "XXXXXX")) {
		errno = EINVAL;
		return NULL;
	}

	tmpdir = getenv("TMPDIR");
	if (!tmpdir)
		tmpdir = "/tmp";

	strbuf_addf(&sb, "%s/%s", tmpdir, directory_template);
	directorylen = sb.len;
	if (!mkdtemp(sb.buf)) {
		int orig_errno = errno;
		strbuf_release(&sb);
		errno = orig_errno;
		return NULL;
	}

	strbuf_addf(&sb, "/%s", filename);
	fd = open(sb.buf, O_CREAT | O_EXCL | O_RDWR, 0600);
	if (fd < 0) {
		int orig_errno = errno;
		strbuf_setlen(&sb, directorylen);
		rmdir(sb.buf);
		strbuf_release(&sb);
		errno = orig_errno;
		return NULL;
	}

	tempfile = new_tempfile();
	strbuf_swap(&tempfile->filename, &sb);
	tempfile->directory = xmemdupz(tempfile->filename.buf, directorylen);
	tempfile->fd = fd;
	activate_tempfile(tempfile);
	return tempfile;
}

/* git v2.53.0 tempfile.c:298 */
int close_tempfile_gently(struct tempfile *tempfile)
{
	int fd;
	FILE *fp;
	int err;

	if (!is_tempfile_active(tempfile) || tempfile->fd < 0)
		return 0;

	fd = tempfile->fd;
	fp = tempfile->fp;
	tempfile->fd = -1;
	if (fp) {
		tempfile->fp = NULL;
		if (ferror(fp)) {
			err = -1;
			if (!fclose(fp))
				errno = EIO;
		} else {
			err = fclose(fp);
		}
	} else {
		err = close(fd);
	}

	return err ? -1 : 0;
}

/* git v2.53.0 tempfile.c:360 */
int delete_tempfile(struct tempfile **tempfile_p)
{
	struct tempfile *tempfile = *tempfile_p;
	int err = 0;

	if (!is_tempfile_active(tempfile))
		return 0;

	err |= close_tempfile_gently(tempfile);
	err |= unlink_or_warn(tempfile->filename.buf);
	err |= remove_template_directory(tempfile, 0);
	deactivate_tempfile(tempfile);
	*tempfile_p = NULL;

	return err ? -1 : 0;
}
