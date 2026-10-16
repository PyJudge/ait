#ifndef WT_STATUS_H
#define WT_STATUS_H
#include "git-compat-util.h"

/* git v2.53.0 environment.h:210 and wt-status.h:150 */
extern const char *comment_line_str;
size_t wt_status_locate_end(const char *s, size_t len);

#endif
