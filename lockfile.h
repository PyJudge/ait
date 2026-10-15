#ifndef LOCKFILE_H
#define LOCKFILE_H
#include "strbuf.h"

/* git v2.53.0 lockfile.h:215,224 */
void unable_to_lock_message(const char *path, int err, struct strbuf *buf);
NORETURN void unable_to_lock_die(const char *path, int err);
#endif
