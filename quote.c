#include "git-compat-util.h"
#include "strbuf.h"
#include "quote.h"

/* git v2.53.0 quote.c:11 */
static inline int need_bs_quote(char c)
{
	return (c == '\'' || c == '!');
}

/* git v2.53.0 quote.c:28 */
void sq_quote_buf(struct strbuf *dst, const char *src)
{
	char *to_free = NULL;

	if (dst->buf == src)
		to_free = strbuf_detach(dst, NULL);

	strbuf_addch(dst, '\'');
	while (*src) {
		size_t len = strcspn(src, "'!");
		strbuf_add(dst, src, len);
		src += len;
		while (need_bs_quote(*src)) {
			strbuf_addstr(dst, "'\\");
			strbuf_addch(dst, *src++);
			strbuf_addch(dst, '\'');
		}
	}
	strbuf_addch(dst, '\'');
	free(to_free);
}

/* git v2.53.0 quote.c:50 */
void sq_quote_buf_pretty(struct strbuf *dst, const char *src)
{
	static const char ok_punct[] = "+,-./:=@_^";
	const char *p;

	/* Avoid losing a zero-length string by adding '' */
	if (!*src) {
		strbuf_addstr(dst, "''");
		return;
	}

	for (p = src; *p; p++) {
		if (!isalnum(*p) && !strchr(ok_punct, *p)) {
			sq_quote_buf(dst, src);
			return;
		}
	}

	/* if we get here, we did not need quoting */
	strbuf_addstr(dst, src);
}
