/*
 * Copyright (c) 2005, Junio C Hamano
 * Lock error reporting from Git; lock ownership is managed by tempfile.c.
 */
#include "git-compat-util.h"
#include "abspath.h"
#include "lockfile.h"

/* git v2.53.0 lockfile.c:151 */
void unable_to_lock_message(const char *path, int err, struct strbuf *buf)
{
	if (err == EEXIST) {
		strbuf_addf(buf, _("Unable to create '%s.lock': %s.\n\n"
		    "Another git process seems to be running in this repository, e.g.\n"
		    "an editor opened by 'git commit'. Please make sure all processes\n"
		    "are terminated then try again. If it still fails, a git process\n"
		    "may have crashed in this repository earlier:\n"
		    "remove the file manually to continue."),
			    absolute_path(path), strerror(err));
	} else
		strbuf_addf(buf, _("Unable to create '%s.lock': %s"),
			    absolute_path(path), strerror(err));
}

/* git v2.53.0 lockfile.c:166 */
NORETURN void unable_to_lock_die(const char *path, int err)
{
	struct strbuf buf = STRBUF_INIT;

	unable_to_lock_message(path, err, &buf);
	die("%s", buf.buf);
}
