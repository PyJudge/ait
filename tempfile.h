#ifndef TEMPFILE_H
#define TEMPFILE_H
#include "list.h"
#include "strbuf.h"

/* git v2.53.0 tempfile.h:78-85 */
struct tempfile {
	volatile struct volatile_list_head list;
	volatile int fd;
	FILE *volatile fp;
	volatile pid_t owner;
	struct strbuf filename;
	char *directory;
};

struct tempfile *register_tempfile(const char *path);
/* git v2.53.0 tempfile.h:210-211 */
struct tempfile *mks_tempfile_dt(const char *directory_template,
				 const char *filename);
int close_tempfile_gently(struct tempfile *tempfile);
int delete_tempfile(struct tempfile **tempfile_p);

/* git v2.53.0 tempfile.h:221 */
static inline int is_tempfile_active(struct tempfile *tempfile)
{
	return !!tempfile;
}

#endif
