#ifndef ABSPATH_H
#define ABSPATH_H
#include "git-compat-util.h"
#include "strbuf.h"

/* git v2.53.0 abspath.h:28 */
static inline int is_absolute_path(const char *path)
{
	return is_dir_sep(path[0]) || has_dos_drive_prefix(path);
}

void strbuf_add_absolute_path(struct strbuf *sb, const char *path);
const char *absolute_path(const char *path);
#endif
