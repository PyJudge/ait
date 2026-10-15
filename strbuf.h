#ifndef STRBUF_H
#define STRBUF_H

/* git v2.53.0 strbuf.h:74-81 */
struct strbuf {
	size_t alloc;
	size_t len;
	char *buf;
};

extern char strbuf_slopbuf[];
#define STRBUF_INIT  { .buf = strbuf_slopbuf }

int strbuf_getcwd(struct strbuf *sb);
void strbuf_init(struct strbuf *sb, size_t alloc);
void strbuf_release(struct strbuf *sb);
char *strbuf_detach(struct strbuf *sb, size_t *sz);
void strbuf_grow(struct strbuf *sb, size_t amount);
void strbuf_rtrim(struct strbuf *sb);
void strbuf_splice(struct strbuf *sb, size_t pos, size_t len, const void *data, size_t data_len);
void strbuf_add(struct strbuf *sb, const void *data, size_t len);
__attribute__((format (printf,2,3)))
void strbuf_addf(struct strbuf *sb, const char *fmt, ...);
__attribute__((format (printf,2,0)))
void strbuf_vaddf(struct strbuf *sb, const char *fmt, va_list ap);
ssize_t strbuf_read(struct strbuf *sb, int fd, size_t hint);
ssize_t strbuf_read_once(struct strbuf *sb, int fd, size_t hint);
ssize_t strbuf_read_file(struct strbuf *sb, const char *path, size_t hint);
void strbuf_stripspace(struct strbuf *sb, const char *comment_prefix);
bool starts_with(const char *str, const char *prefix);
__attribute__((format (printf, 1, 2)))
char *xstrfmt(const char *fmt, ...);
__attribute__((format (printf, 1, 0)))
char *xstrvfmt(const char *fmt, va_list ap);
/* git v2.53.0 strbuf.h:143 */
static inline size_t strbuf_avail(const struct strbuf *sb)
{
	return sb->alloc ? sb->alloc - sb->len - 1 : 0;
}

/* git v2.53.0 strbuf.h:164 */
static inline void strbuf_setlen(struct strbuf *sb, size_t len)
{
	if (len > (sb->alloc ? sb->alloc - 1 : 0))
		BUG("strbuf_setlen() beyond buffer");
	sb->len = len;
	if (sb->buf != strbuf_slopbuf)
		sb->buf[len] = '\0';
	else
		assert(!strbuf_slopbuf[0]);
}

/* git v2.53.0 strbuf.h:178-178 */
#define strbuf_reset(sb)  strbuf_setlen(sb, 0)

/* git v2.53.0 strbuf.h:232 */
static inline void strbuf_addch(struct strbuf *sb, int c)
{
	if (!strbuf_avail(sb))
		strbuf_grow(sb, 1);
	sb->buf[sb->len++] = c;
	sb->buf[sb->len] = '\0';
}

/* git v2.53.0 strbuf.h:308 */
static inline void strbuf_addstr(struct strbuf *sb, const char *s)
{
	strbuf_add(sb, s, strlen(s));
}

/* git v2.53.0 strbuf.h:655 */
static inline void strbuf_complete(struct strbuf *sb, char term)
{
	if (sb->len && sb->buf[sb->len - 1] != term)
		strbuf_addch(sb, term);
}

/* git v2.53.0 strbuf.h:661 */
static inline void strbuf_complete_line(struct strbuf *sb)
{
	strbuf_complete(sb, '\n');
}

/* git v2.53.0 strbuf.h:715 */
static inline bool ends_with(const char *str, const char *suffix)
{
	size_t len;
	return strip_suffix(str, suffix, &len);
}

#endif