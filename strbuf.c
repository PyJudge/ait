#include "git-compat-util.h"
#include "strbuf.h"

/* git v2.53.0 strbuf.c:11 */
bool starts_with(const char *str, const char *prefix)
{
	for (; ; str++, prefix++)
		if (!*prefix)
			return true;
		else if (*str != *prefix)
			return false;
}

/* git v2.53.0 strbuf.c:62-67 */
/*
 * Used as the default ->buf value, so that people can always assume
 * buf is non NULL and ->buf is NUL terminated even for a freshly
 * initialized strbuf.
 */
char strbuf_slopbuf[1];

/* git v2.53.0 strbuf.c:69 */
void strbuf_init(struct strbuf *sb, size_t hint)
{
	struct strbuf blank = STRBUF_INIT;
	memcpy(sb, &blank, sizeof(*sb));
	if (hint)
		strbuf_grow(sb, hint);
}

/* git v2.53.0 strbuf.c:77 */
void strbuf_release(struct strbuf *sb)
{
	if (sb->alloc) {
		free(sb->buf);
		strbuf_init(sb, 0);
	}
}

/* git v2.53.0 strbuf.c:85 */
char *strbuf_detach(struct strbuf *sb, size_t *sz)
{
	char *res;
	strbuf_grow(sb, 0);
	res = sb->buf;
	if (sz)
		*sz = sb->len;
	strbuf_init(sb, 0);
	return res;
}

/* git v2.53.0 strbuf.c:106 */
void strbuf_grow(struct strbuf *sb, size_t extra)
{
	int new_buf = !sb->alloc;
	if (unsigned_add_overflows(extra, 1) ||
	    unsigned_add_overflows(sb->len, extra + 1))
		die("you want to use way too much memory");
	if (new_buf)
		sb->buf = NULL;
	ALLOC_GROW(sb->buf, sb->len + extra + 1, sb->alloc);
	if (new_buf)
		sb->buf[0] = '\0';
}

/* git v2.53.0 strbuf.c:125 */
void strbuf_rtrim(struct strbuf *sb)
{
	while (sb->len > 0 && isspace((unsigned char)sb->buf[sb->len - 1]))
		sb->len--;
	sb->buf[sb->len] = '\0';
}

/* git v2.53.0 strbuf.c:246 */
void strbuf_splice(struct strbuf *sb, size_t pos, size_t len,
				   const void *data, size_t dlen)
{
	if (unsigned_add_overflows(pos, len))
		die("you want to use way too much memory");
	if (pos > sb->len)
		die("`pos' is too far after the end of the buffer");
	if (pos + len > sb->len)
		die("`pos + len' is too far after the end of the buffer");

	if (dlen >= len)
		strbuf_grow(sb, dlen - len);
	memmove(sb->buf + pos + dlen,
			sb->buf + pos + len,
			sb->len - pos - len);
	memcpy(sb->buf + pos, data, dlen);
	strbuf_setlen(sb, sb->len + dlen - len);
}

/* git v2.53.0 strbuf.c:311 */
void strbuf_add(struct strbuf *sb, const void *data, size_t len)
{
	strbuf_grow(sb, len);
	memcpy(sb->buf + sb->len, data, len);
	strbuf_setlen(sb, sb->len + len);
}

/* git v2.53.0 strbuf.c:356 */
void strbuf_addf(struct strbuf *sb, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	strbuf_vaddf(sb, fmt, ap);
	va_end(ap);
}

/* git v2.53.0 strbuf.c:407 */
void strbuf_vaddf(struct strbuf *sb, const char *fmt, va_list ap)
{
	int len;
	va_list cp;

	if (!strbuf_avail(sb))
		strbuf_grow(sb, 64);
	va_copy(cp, ap);
	len = vsnprintf(sb->buf + sb->len, sb->alloc - sb->len, fmt, cp);
	va_end(cp);
	if (len < 0)
		die(_("unable to format message: %s"), fmt);
	if (len > strbuf_avail(sb)) {
		strbuf_grow(sb, len);
		len = vsnprintf(sb->buf + sb->len, sb->alloc - sb->len, fmt, ap);
		if (len > strbuf_avail(sb))
			BUG("your vsnprintf is broken (insatiable)");
	}
	strbuf_setlen(sb, sb->len + len);
}

/* git v2.53.0 strbuf.c:523 */
ssize_t strbuf_read(struct strbuf *sb, int fd, size_t hint)
{
	size_t oldlen = sb->len;
	size_t oldalloc = sb->alloc;

	strbuf_grow(sb, hint ? hint : 8192);
	for (;;) {
		ssize_t want = sb->alloc - sb->len - 1;
		ssize_t got = read_in_full(fd, sb->buf + sb->len, want);

		if (got < 0) {
			if (oldalloc == 0)
				strbuf_release(sb);
			else
				strbuf_setlen(sb, oldlen);
			return -1;
		}
		sb->len += got;
		if (got < want)
			break;
		strbuf_grow(sb, 8192);
	}

	sb->buf[sb->len] = '\0';
	return sb->len - oldlen;
}

/* git v2.53.0 strbuf.c:550 */
ssize_t strbuf_read_once(struct strbuf *sb, int fd, size_t hint)
{
	size_t oldalloc = sb->alloc;
	ssize_t cnt;

	strbuf_grow(sb, hint ? hint : 8192);
	cnt = xread(fd, sb->buf + sb->len, sb->alloc - sb->len - 1);
	if (cnt > 0)
		strbuf_setlen(sb, sb->len + cnt);
	else if (oldalloc == 0)
		strbuf_release(sb);
	return cnt;
}

/* git v2.53.0 strbuf.c:768 */
ssize_t strbuf_read_file(struct strbuf *sb, const char *path, size_t hint)
{
	int fd;
	ssize_t len;
	int saved_errno;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	len = strbuf_read(sb, fd, hint);
	saved_errno = errno;
	close(fd);
	if (len < 0) {
		errno = saved_errno;
		return -1;
	}

	return len;
}

/* git v2.53.0 strbuf.c:974 */
char *xstrvfmt(const char *fmt, va_list ap)
{
	struct strbuf buf = STRBUF_INIT;
	strbuf_vaddf(&buf, fmt, ap);
	return strbuf_detach(&buf, NULL);
}

/* git v2.53.0 strbuf.c:981 */
char *xstrfmt(const char *fmt, ...)
{
	va_list ap;
	char *ret;

	va_start(ap, fmt);
	ret = xstrvfmt(fmt, ap);
	va_end(ap);

	return ret;
}

/* git v2.53.0 strbuf.c:1058 */
static size_t cleanup(char *line, size_t len)
{
	while (len) {
		unsigned char c = line[len - 1];
		if (!isspace(c))
			break;
		len--;
	}

	return len;
}

/* git v2.53.0 strbuf.c:1085 */
void strbuf_stripspace(struct strbuf *sb, const char *comment_prefix)
{
	size_t empties = 0;
	size_t i, j, len, newlen;
	char *eol;

	/* We may have to add a newline. */
	strbuf_grow(sb, 1);

	for (i = j = 0; i < sb->len; i += len, j += newlen) {
		eol = memchr(sb->buf + i, '\n', sb->len - i);
		len = eol ? eol - (sb->buf + i) + 1 : sb->len - i;

		if (comment_prefix && len &&
		    starts_with(sb->buf + i, comment_prefix)) {
			newlen = 0;
			continue;
		}
		newlen = cleanup(sb->buf + i, len);

		/* Not just an empty line? */
		if (newlen) {
			if (empties > 0 && j > 0)
				sb->buf[j++] = '\n';
			empties = 0;
			memmove(sb->buf + j, sb->buf + i, newlen);
			sb->buf[newlen + j++] = '\n';
		} else {
			empties++;
		}
	}

	strbuf_setlen(sb, j);
}

/* git v2.53.0 strbuf.c:599 */
int strbuf_getcwd(struct strbuf *sb)
{
	size_t oldalloc = sb->alloc;
	size_t guessed_len = 128;

	for (;; guessed_len *= 2) {
		strbuf_grow(sb, guessed_len);
		if (getcwd(sb->buf, sb->alloc)) {
			strbuf_setlen(sb, strlen(sb->buf));
			return 0;
		}

		/*
		 * If getcwd(3) is implemented as a syscall that falls
		 * back to a regular lookup using readdir(3) etc. then
		 * we may be able to avoid EACCES by providing enough
		 * space to the syscall as it's not necessarily bound
		 * to the same restrictions as the fallback.
		 */
		if (errno == EACCES && guessed_len < PATH_MAX)
			continue;

		if (errno != ERANGE)
			break;
	}
	if (oldalloc == 0)
		strbuf_release(sb);
	else
		strbuf_reset(sb);
	return -1;
}
