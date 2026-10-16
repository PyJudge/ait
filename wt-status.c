#include "wt-status.h"
#include "strbuf.h"

/* git v2.53.0 wt-status.c:42-43 */
static const char cut_line[] =
"------------------------ >8 ------------------------\n";

/* git v2.53.0 wt-status.c:1100-1116 */
size_t wt_status_locate_end(const char *s, size_t len)
{
	const char *p;
	struct strbuf pattern = STRBUF_INIT;

	strbuf_addf(&pattern, "\n%s %s", comment_line_str, cut_line);
	if (starts_with(s, pattern.buf + 1))
		len = 0;
	else if ((p = strstr(s, pattern.buf))) {
		size_t newlen = p - s + 1;
		if (newlen < len)
			len = newlen;
	}
	strbuf_release(&pattern);
	return len;
}
