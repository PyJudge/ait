#include "git-compat-util.h"
#include "strbuf.h"

#ifdef HAVE_RTLGENRANDOM
/* git v2.53.0 wrapper.c:15-19: this is required to get access to RtlGenRandom */
#define SystemFunction036 NTAPI SystemFunction036
#include <ntsecapi.h>
#undef SystemFunction036
#endif

/* git v2.53.0 wrapper.c:21 */
static int memory_limit_check(size_t size, int gentle)
{
	static size_t limit = 0;
	if (!limit) {
		limit = git_env_ulong("GIT_ALLOC_LIMIT", 0);
		if (!limit)
			limit = SIZE_MAX;
	}
	if (size > limit) {
		if (gentle) {
			error("attempting to allocate %"PRIuMAX" over limit %"PRIuMAX,
			      (uintmax_t)size, (uintmax_t)limit);
			return -1;
		} else
			die("attempting to allocate %"PRIuMAX" over limit %"PRIuMAX,
			    (uintmax_t)size, (uintmax_t)limit);
	}
	return 0;
}

/* git v2.53.0 wrapper.c:41 */
char *xstrdup(const char *str)
{
	char *ret = strdup(str);
	if (!ret)
		die("Out of memory, strdup failed");
	return ret;
}

/* git v2.53.0 wrapper.c:49 */
static void *do_xmalloc(size_t size, int gentle)
{
	void *ret;

	if (memory_limit_check(size, gentle))
		return NULL;
	ret = malloc(size);
	if (!ret && !size)
		ret = malloc(1);
	if (!ret) {
		if (!gentle)
			die("Out of memory, malloc failed (tried to allocate %lu bytes)",
			    (unsigned long)size);
		else {
			error("Out of memory, malloc failed (tried to allocate %lu bytes)",
			      (unsigned long)size);
			return NULL;
		}
	}
#ifdef XMALLOC_POISON
	memset(ret, 0xA5, size);
#endif
	return ret;
}

/* git v2.53.0 wrapper.c:74 */
void *xmalloc(size_t size)
{
	return do_xmalloc(size, 0);
}

/* git v2.53.0 wrapper.c:79 */
static void *do_xmallocz(size_t size, int gentle)
{
	void *ret;
	if (unsigned_add_overflows(size, 1)) {
		if (gentle) {
			error("Data too large to fit into virtual memory space.");
			return NULL;
		} else
			die("Data too large to fit into virtual memory space.");
	}
	ret = do_xmalloc(size + 1, gentle);
	if (ret)
		((char*)ret)[size] = 0;
	return ret;
}

/* git v2.53.0 wrapper.c:95 */
void *xmallocz(size_t size)
{
	return do_xmallocz(size, 0);
}

/* git v2.53.0 wrapper.c:111 */
void *xmemdupz(const void *data, size_t len)
{
	return memcpy(xmallocz(len), data, len);
}

/* git v2.53.0 wrapper.c:116 */
char *xstrndup(const char *str, size_t len)
{
	char *p = memchr(str, '\0', len);
	return xmemdupz(str, p ? p - str : len);
}

/* git v2.53.0 wrapper.c:130 */
void *xrealloc(void *ptr, size_t size)
{
	void *ret;

	if (!size) {
		free(ptr);
		return xmalloc(0);
	}

	memory_limit_check(size, 0);
	ret = realloc(ptr, size);
	if (!ret)
		die("Out of memory, realloc failed");
	return ret;
}

/* git v2.53.0 wrapper.c:146 */
void *xcalloc(size_t nmemb, size_t size)
{
	void *ret;

	if (unsigned_mult_overflows(nmemb, size))
		die("data too large to fit into virtual memory space");

	memory_limit_check(size * nmemb, 0);
	ret = calloc(nmemb, size);
	if (!ret && (!nmemb || !size))
		ret = calloc(1, 1);
	if (!ret)
		die("Out of memory, calloc failed");
	return ret;
}

/* git v2.53.0 wrapper.c:171 */
int xopen(const char *path, int oflag, ...)
{
	mode_t mode = 0;
	va_list ap;

	/*
	 * va_arg() will have undefined behavior if the specified type is not
	 * compatible with the argument type. Since integers are promoted to
	 * ints, we fetch the next argument as an int, and then cast it to a
	 * mode_t to avoid undefined behavior.
	 */
	va_start(ap, oflag);
	if (oflag & O_CREAT)
		mode = va_arg(ap, int);
	va_end(ap);

	for (;;) {
		int fd = open(path, oflag, mode);
		if (fd >= 0)
			return fd;
		if (errno == EINTR)
			continue;

		if ((oflag & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL))
			die_errno(_("unable to create '%s'"), path);
		else if ((oflag & O_RDWR) == O_RDWR)
			die_errno(_("could not open '%s' for reading and writing"), path);
		else if ((oflag & O_WRONLY) == O_WRONLY)
			die_errno(_("could not open '%s' for writing"), path);
		else
			die_errno(_("could not open '%s' for reading"), path);
	}
}

/* git v2.53.0 wrapper.c:205 */
static int handle_nonblock(int fd, short poll_events, int err)
{
	struct pollfd pfd;

	if (err != EAGAIN && err != EWOULDBLOCK)
		return 0;

	pfd.fd = fd;
	pfd.events = poll_events;

	/*
	 * no need to check for errors, here;
	 * a subsequent read/write will detect unrecoverable errors
	 */
	poll(&pfd, 1, -1);
	return 1;
}

/* git v2.53.0 wrapper.c:228 */
ssize_t xread(int fd, void *buf, size_t len)
{
	ssize_t nr;
	if (len > MAX_IO_SIZE)
		len = MAX_IO_SIZE;
	while (1) {
		nr = read(fd, buf, len);
		if (nr < 0) {
			if (errno == EINTR)
				continue;
			if (handle_nonblock(fd, POLLIN, errno))
				continue;
		}
		return nr;
	}
}

/* git v2.53.0 wrapper.c:250 */
ssize_t xwrite(int fd, const void *buf, size_t len)
{
	ssize_t nr;
	if (len > MAX_IO_SIZE)
		len = MAX_IO_SIZE;
	while (1) {
		nr = write(fd, buf, len);
		if (nr < 0) {
			if (errno == EINTR)
				continue;
			if (handle_nonblock(fd, POLLOUT, errno))
				continue;
		}

		return nr;
	}
}

/* git v2.53.0 wrapper.c:286 */
ssize_t read_in_full(int fd, void *buf, size_t count)
{
	char *p = buf;
	ssize_t total = 0;

	while (count > 0) {
		ssize_t loaded = xread(fd, p, count);
		if (loaded < 0)
			return -1;
		if (loaded == 0)
			return total;
		count -= loaded;
		p += loaded;
		total += loaded;
	}

	return total;
}

/* git v2.53.0 wrapper.c:305 */
ssize_t write_in_full(int fd, const void *buf, size_t count)
{
	const char *p = buf;
	ssize_t total = 0;

	while (count > 0) {
		ssize_t written = xwrite(fd, p, count);
		if (written < 0)
			return -1;
		if (!written) {
			errno = ENOSPC;
			return -1;
		}
		count -= written;
		p += written;
		total += written;
	}

	return total;
}

/* git v2.53.0 wrapper.c:346 */
int xdup(int fd)
{
	int ret = dup(fd);
	if (ret < 0)
		die_errno("dup failed");
	return ret;
}

/* git v2.53.0 wrapper.c:375 */
FILE *xfdopen(int fd, const char *mode)
{
	FILE *stream = fdopen(fd, mode);
	if (!stream)
		die_errno("Out of memory? fdopen failed");
	return stream;
}

/* git v2.53.0 wrapper.c:682 */
void write_file_buf(const char *path, const char *buf, size_t len)
{
	int fd = xopen(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (write_in_full(fd, buf, len) < 0)
		die_errno(_("could not write to '%s'"), path);
	if (close(fd))
		die_errno(_("could not close '%s'"), path);
}

/* git v2.53.0 wrapper.c:691 */
void write_file(const char *path, const char *fmt, ...)
{
	va_list params;
	struct strbuf sb = STRBUF_INIT;

	va_start(params, fmt);
	strbuf_vaddf(&sb, fmt, params);
	va_end(params);

	strbuf_complete_line(&sb);

	write_file_buf(path, sb.buf, sb.len);
	strbuf_release(&sb);
}

/* git v2.53.0 wrapper.c:785 */
int csprng_bytes(void *buf, size_t len, MAYBE_UNUSED unsigned flags)
{
#if defined(HAVE_ARC4RANDOM) || defined(HAVE_ARC4RANDOM_LIBBSD)
	/* This function never returns an error. */
	arc4random_buf(buf, len);
	return 0;
#elif defined(HAVE_GETRANDOM)
	ssize_t res;
	char *p = buf;
	while (len) {
		res = getrandom(p, len, 0);
		if (res < 0)
			return -1;
		len -= res;
		p += res;
	}
	return 0;
#elif defined(HAVE_GETENTROPY)
	int res;
	char *p = buf;
	while (len) {
		/* getentropy has a maximum size of 256 bytes. */
		size_t chunk = len < 256 ? len : 256;
		res = getentropy(p, chunk);
		if (res < 0)
			return -1;
		len -= chunk;
		p += chunk;
	}
	return 0;
#elif defined(HAVE_RTLGENRANDOM)
	if (!RtlGenRandom(buf, len))
		return -1;
	return 0;
#elif defined(HAVE_OPENSSL_CSPRNG)
	switch (RAND_pseudo_bytes(buf, len)) {
	case 1:
		return 0;
	case 0:
		if (flags & CSPRNG_BYTES_INSECURE)
			return 0;
		errno = EIO;
		return -1;
	default:
		errno = ENOTSUP;
		return -1;
	}
#else
	ssize_t res;
	char *p = buf;
	int fd, err;
	fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0)
		return -1;
	while (len) {
		res = xread(fd, p, len);
		if (res < 0) {
			err = errno;
			close(fd);
			errno = err;
			return -1;
		}
		len -= res;
		p += res;
	}
	close(fd);
	return 0;
#endif
}

/* git v2.53.0 compat/nonblock.c:4-40 */
#ifdef O_NONBLOCK
int enable_pipe_nonblock(int fd)
{
	int flags = fcntl(fd, F_GETFL);
	if (flags < 0)
		return -1;
	flags |= O_NONBLOCK;
	return fcntl(fd, F_SETFL, flags);
}
#else
int enable_pipe_nonblock(int fd)
{
	HANDLE h = (HANDLE)_get_osfhandle(fd);
	DWORD mode;
	DWORD type = GetFileType(h);
	if (type == FILE_TYPE_UNKNOWN && GetLastError() != NO_ERROR) {
		errno = EBADF;
		return -1;
	}
	if (type != FILE_TYPE_PIPE)
		BUG("unsupported file type: %lu", type);
	if (!GetNamedPipeHandleState(h, &mode, NULL, NULL, NULL, NULL, 0)) {
		errno = err_win_to_posix(GetLastError());
		return -1;
	}
	mode |= PIPE_NOWAIT;
	if (!SetNamedPipeHandleState(h, &mode, NULL, NULL)) {
		errno = err_win_to_posix(GetLastError());
		return -1;
	}
	return 0;
}
#endif

/* git v2.53.0 compat/qsort_s.c:9 */
static void msort_with_tmp(void *b, size_t n, size_t s,
			   int (*cmp)(const void *, const void *, void *),
			   char *t, void *ctx)
{
	char *tmp;
	char *b1, *b2;
	size_t n1, n2;

	if (n <= 1)
		return;

	n1 = n / 2;
	n2 = n - n1;
	b1 = b;
	b2 = (char *)b + (n1 * s);

	msort_with_tmp(b1, n1, s, cmp, t, ctx);
	msort_with_tmp(b2, n2, s, cmp, t, ctx);

	tmp = t;

	while (n1 > 0 && n2 > 0) {
		if (cmp(b1, b2, ctx) <= 0) {
			memcpy(tmp, b1, s);
			tmp += s;
			b1 += s;
			--n1;
		} else {
			memcpy(tmp, b2, s);
			tmp += s;
			b2 += s;
			--n2;
		}
	}
	if (n1 > 0)
		memcpy(tmp, b1, n1 * s);
	memcpy(b, t, (n - n2) * s);
}

/* git v2.53.0 compat/qsort_s.c:48 */
int git_qsort_s(void *b, size_t n, size_t s,
		int (*cmp)(const void *, const void *, void *), void *ctx)
{
	const size_t size = st_mult(n, s);
	char *tmp;

	if (!n)
		return 0;
	if (!b || !cmp)
		return -1;

	tmp = xmalloc(size);
	msort_with_tmp(b, n, s, cmp, tmp, ctx);
	free(tmp);
	return 0;
}

/* git v2.53.0 wrapper.c:601 */
static int warn_if_unremovable(const char *op, const char *file, int rc)
{
	int err;
	if (!rc || errno == ENOENT)
		return 0;
	err = errno;
	warning_errno("unable to %s '%s'", op, file);
	errno = err;
	return rc;
}

/* git v2.53.0 wrapper.c:626 */
int unlink_or_warn(const char *file)
{
	return warn_if_unremovable("unlink", file, unlink(file));
}

/* git v2.53.0 wrapper.c:631 */
int rmdir_or_warn(const char *file)
{
	return warn_if_unremovable("rmdir", file, rmdir(file));
}

/* git v2.53.0 wrapper.c:658 */
char *xgetcwd(void)
{
	struct strbuf sb = STRBUF_INIT;
	if (strbuf_getcwd(&sb))
		die_errno(_("unable to get current working directory"));
	return strbuf_detach(&sb, NULL);
}
