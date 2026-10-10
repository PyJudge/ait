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

/* git v2.53.0 quote.c:9-9 */
int quote_path_fully = 1;

/* git v2.53.0 quote.c:215-233 */
/* 1 means: quote as octal
 * 0 means: quote as octal if (quote_path_fully)
 * -1 means: never quote
 * c: quote as "\\c"
 */
#define X8(x)   x, x, x, x, x, x, x, x
#define X16(x)  X8(x), X8(x)
static signed char const cq_lookup[256] = {
	/*           0    1    2    3    4    5    6    7 */
	/* 0x00 */   1,   1,   1,   1,   1,   1,   1, 'a',
	/* 0x08 */ 'b', 't', 'n', 'v', 'f', 'r',   1,   1,
	/* 0x10 */ X16(1),
	/* 0x20 */  -1,  -1, '"',  -1,  -1,  -1,  -1,  -1,
	/* 0x28 */ X16(-1), X16(-1), X16(-1),
	/* 0x58 */  -1,  -1,  -1,  -1,'\\',  -1,  -1,  -1,
	/* 0x60 */ X16(-1), X8(-1),
	/* 0x78 */  -1,  -1,  -1,  -1,  -1,  -1,  -1,   1,
	/* 0x80 */ /* set to 0 */
};

/* git v2.53.0 quote.c:235 */
static inline int cq_must_quote(char c)
{
	return cq_lookup[(unsigned char)c] + quote_path_fully > 0;
}

/* git v2.53.0 quote.c:243 */
static size_t next_quote_pos(const char *s, ssize_t maxlen)
{
	size_t len;
	if (maxlen < 0) {
		for (len = 0; !cq_must_quote(s[len]); len++);
	} else {
		for (len = 0; len < maxlen && !cq_must_quote(s[len]); len++);
	}
	return len;
}

/* git v2.53.0 quote.c:267 */
static size_t quote_c_style_counted(const char *name, ssize_t maxlen,
				    struct strbuf *sb, FILE *fp, unsigned flags)
{
#undef EMIT
#define EMIT(c)                                 \
	do {                                        \
		if (sb) strbuf_addch(sb, (c));          \
		if (fp) fputc((c), fp);                 \
		count++;                                \
	} while (0)
#define EMITBUF(s, l)                           \
	do {                                        \
		if (sb) strbuf_add(sb, (s), (l));       \
		if (fp) fwrite((s), (l), 1, fp);        \
		count += (l);                           \
	} while (0)

	int no_dq = !!(flags & CQUOTE_NODQ);
	size_t len, count = 0;
	const char *p = name;

	for (;;) {
		int ch;

		len = next_quote_pos(p, maxlen);
		if (len == maxlen || (maxlen < 0 && !p[len]))
			break;

		if (!no_dq && p == name)
			EMIT('"');

		EMITBUF(p, len);
		EMIT('\\');
		p += len;
		ch = (unsigned char)*p++;
		if (maxlen >= 0)
			maxlen -= len + 1;
		if (cq_lookup[ch] >= ' ') {
			EMIT(cq_lookup[ch]);
		} else {
			EMIT(((ch >> 6) & 03) + '0');
			EMIT(((ch >> 3) & 07) + '0');
			EMIT(((ch >> 0) & 07) + '0');
		}
	}

	EMITBUF(p, len);
	if (p == name)   /* no ending quote needed */
		return 0;

	if (!no_dq)
		EMIT('"');
	return count;
}

/* git v2.53.0 quote.c:322 */
size_t quote_c_style(const char *name, struct strbuf *sb, FILE *fp, unsigned flags)
{
	return quote_c_style_counted(name, -1, sb, fp, flags);
}
