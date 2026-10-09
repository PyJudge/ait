#ifndef GIT_COMPAT_UTIL_H
#define GIT_COMPAT_UTIL_H
#define _GNU_SOURCE
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* git v2.53.0 git-compat-util.h:378-379 */
#define NORETURN __attribute__((__noreturn__))
#define NORETURN_PTR __attribute__((__noreturn__))

/* git v2.53.0 git-compat-util.h:395-395 */
#define LAST_ARG_MUST_BE_NULL __attribute__((sentinel))

/* git v2.53.0 git-compat-util.h:414-414 */
#define MAYBE_UNUSED __attribute__((__unused__))

/* git v2.53.0 compat/posix.h:42-43 */
#define UNUSED __attribute__((unused)) \
	__attribute__((deprecated))

/* git v2.53.0 git-compat-util.h:464-464 */
typedef void (*report_fn)(const char *, va_list params);

/* git v2.53.0 git-compat-util.h:994-994 */
#define BUG(...) BUG_fl(__FILE__, __LINE__, __VA_ARGS__)

/* git v2.53.0 git-compat-util.h:1014-1014 */
# define SHELL_PATH "/bin/sh"


/* declarations of the ported functions (git v2.53.0 git-compat-util.h, wrapper.h, advice.h, compat/nonblock.h) */
NORETURN void BUG_fl(const char *file, int line, const char *fmt, ...);
NORETURN void usage(const char *err);
NORETURN void die(const char *err, ...) __attribute__((format (printf, 1, 2)));
NORETURN void die_errno(const char *err, ...) __attribute__((format (printf, 1, 2)));
int die_message(const char *err, ...) __attribute__((format (printf, 1, 2)));
int error(const char *err, ...) __attribute__((format (printf, 1, 2)));
int error_errno(const char *err, ...) __attribute__((format (printf, 1, 2)));
void warning(const char *err, ...) __attribute__((format (printf, 1, 2)));
void set_die_routine(NORETURN_PTR report_fn routine);
report_fn get_die_message_routine(void);
void set_error_routine(report_fn routine);
report_fn get_error_routine(void);
void set_warn_routine(report_fn routine);
void advise(const char *advice, ...) __attribute__((format (printf, 1, 2)));
void *xmalloc(size_t size);
void *xmallocz(size_t size);
void *xmemdupz(const void *data, size_t len);
char *xstrndup(const char *str, size_t len);
void *xrealloc(void *ptr, size_t size);
void *xcalloc(size_t nmemb, size_t size);
char *xstrdup(const char *str);
int xopen(const char *path, int flags, ...);
ssize_t xread(int fd, void *buf, size_t len);
ssize_t xwrite(int fd, const void *buf, size_t len);
ssize_t read_in_full(int fd, void *buf, size_t count);
ssize_t write_in_full(int fd, const void *buf, size_t count);
FILE *xfdopen(int fd, const char *mode);
int enable_pipe_nonblock(int fd);
void write_file_buf(const char *path, const char *buf, size_t len);
__attribute__((format (printf, 2, 3)))
void write_file(const char *path, const char *fmt, ...);

/*
 * git infrastructure that ait does not carry: tracing, translation, the object
 * store, child cleanup at exit, the git command wrapper and GIT_ALLOC_LIMIT.
 * The ported code calls these; here they do nothing.
 */
#define NO_PTHREADS
#define _(s) (s)
#define N_(s) (s)
#define NOT_CONSTANT(expr) (expr)
#define trace2_cmd_error_va(fmt, ap) do { } while (0)
#define trace2_cmd_name(name) do { } while (0)
#define trace2_child_start(cmd) do { } while (0)
#define trace2_child_exit(cmd, code) do { } while (0)
#define trace2_exec(file, argv) 0
#define trace2_exec_result(id, ec) do { } while (0)
#define trace_run_command(cmd) do { } while (0)
#define trace_argv_printf(argv, fmt) do { } while (0)
#define odb_close(odb) do { } while (0)
#define mark_child_for_cleanup(pid, cmd) do { } while (0)
#define clear_child_for_cleanup(pid) do { } while (0)
#define invalidate_lstat_cache() do { } while (0)
#define prepare_git_cmd(out, argv) BUG("git_cmd is not used by ait")
#define git_env_ulong(key, val) (val)
#define advise_get_color(slot) ""

/* git v2.53.0 git-compat-util.h:56-57 */
#define BUILD_ASSERT_OR_ZERO(cond) \
	(sizeof(char [1 - 2*!(cond)]) - 1)

/* git v2.53.0 git-compat-util.h:64-66 */
# define BARF_UNLESS_COPYABLE(dst, src) \
	BUILD_ASSERT_OR_ZERO(__builtin_types_compatible_p(__typeof__(*(dst)), \
							  __typeof__(*(src))))

/* git v2.53.0 git-compat-util.h:90-90 */
#define bitsizeof(x)  (CHAR_BIT * sizeof(x))

/* git v2.53.0 git-compat-util.h:95-96 */
#define maximum_unsigned_value_of_type(a) \
    (UINTMAX_MAX >> (bitsizeof(uintmax_t) - bitsizeof(a)))

/* git v2.53.0 git-compat-util.h:107-108 */
#define unsigned_add_overflows(a, b) \
    ((b) > maximum_unsigned_value_of_type(a) - (a))

/* git v2.53.0 git-compat-util.h:115-116 */
#define unsigned_mult_overflows(a, b) \
    ((a) && (b) > maximum_unsigned_value_of_type(a) / (a))

/* git v2.53.0 git-compat-util.h:350 */
static inline int git_has_dir_sep(const char *path)
{
	return !!strchr(path, '/');
}

/* git v2.53.0 git-compat-util.h:354-354 */
#define has_dir_sep(path) git_has_dir_sep(path)

/* git v2.53.0 git-compat-util.h:490 */
static inline bool skip_prefix(const char *str, const char *prefix,
			       const char **out)
{
	do {
		if (!*prefix) {
			*out = str;
			return true;
		}
	} while (*str++ == *prefix++);
	return false;
}

/* git v2.53.0 git-compat-util.h:523 */
static inline bool strip_suffix_mem(const char *buf, size_t *len,
				    const char *suffix)
{
	size_t suflen = strlen(suffix);
	if (*len < suflen || memcmp(buf + (*len - suflen), suffix, suflen))
		return false;
	*len -= suflen;
	return true;
}

/* git v2.53.0 git-compat-util.h:540 */
static inline bool strip_suffix(const char *str, const char *suffix,
				size_t *len)
{
	*len = strlen(str);
	return strip_suffix_mem(str, len, suffix);
}

/* git v2.53.0 git-compat-util.h:608 */
static inline size_t st_add(size_t a, size_t b)
{
	if (unsigned_add_overflows(a, b))
		die("size_t overflow: %"PRIuMAX" + %"PRIuMAX,
		    (uintmax_t)a, (uintmax_t)b);
	return a + b;
}

/* git v2.53.0 git-compat-util.h:615-615 */
#define st_add3(a,b,c)   st_add(st_add((a),(b)),(c))

/* git v2.53.0 git-compat-util.h:618 */
static inline size_t st_mult(size_t a, size_t b)
{
	if (unsigned_mult_overflows(a, b))
		die("size_t overflow: %"PRIuMAX" * %"PRIuMAX,
		    (uintmax_t)a, (uintmax_t)b);
	return a * b;
}

/* git v2.53.0 git-compat-util.h:697-704 */
#ifndef MAX_IO_SIZE
# define MAX_IO_SIZE_DEFAULT (8*1024*1024)
# if defined(SSIZE_MAX) && (SSIZE_MAX < MAX_IO_SIZE_DEFAULT)
#  define MAX_IO_SIZE SSIZE_MAX
# else
#  define MAX_IO_SIZE MAX_IO_SIZE_DEFAULT
# endif
#endif

/* git v2.53.0 git-compat-util.h:721-723 */
#define ALLOC_ARRAY(x, alloc) (x) = xmalloc(st_mult(sizeof(*(x)), (alloc)))
#define CALLOC_ARRAY(x, alloc) (x) = xcalloc((alloc), sizeof(*(x)))
#define REALLOC_ARRAY(x, alloc) (x) = xrealloc((x), st_mult(sizeof(*(x)), (alloc)))

/* git v2.53.0 git-compat-util.h:734-740 */
#define MOVE_ARRAY(dst, src, n) move_array((dst), (src), (n), sizeof(*(dst)) + \
	BARF_UNLESS_COPYABLE((dst), (src)))
static inline void move_array(void *dst, const void *src, size_t n, size_t size)
{
	if (n)
		memmove(dst, src, st_mult(size, n));
}

/* git v2.53.0 git-compat-util.h:804-804 */
#define alloc_nr(x) (((x)+16)*3/2)

/* git v2.53.0 git-compat-util.h:849-858 */
#define ALLOC_GROW(x, nr, alloc) \
	do { \
		if ((nr) > alloc) { \
			if (alloc_nr(alloc) < (nr)) \
				alloc = (nr); \
			else \
				alloc = alloc_nr(alloc); \
			REALLOC_ARRAY(x, alloc); \
		} \
	} while (0)

/* git v2.53.0 compat/posix.h:420-429 */

#ifndef HAVE_STRCHRNUL
#define strchrnul gitstrchrnul
static inline char *gitstrchrnul(const char *s, int c)
{
	while (*s && *s != c)
		s++;
	return (char *)s;
}
#endif

/* git v2.53.0 compat/posix.h:456-462 */
#define QSORT(base, n, compar) sane_qsort((base), (n), sizeof(*(base)), compar)
static inline void sane_qsort(void *base, size_t nmemb, size_t size,
			      int(*compar)(const void *, const void *))
{
	if (nmemb > 1)
		qsort(base, nmemb, size, compar);
}

/* git v2.53.0 compat/posix.h:467-476 */
#ifndef HAVE_ISO_QSORT_S
int git_qsort_s(void *base, size_t nmemb, size_t size,
		int (*compar)(const void *, const void *, void *), void *ctx);
#define qsort_s git_qsort_s
#endif

#define QSORT_S(base, n, compar, ctx) do {			\
	if (qsort_s((base), (n), sizeof(*(base)), compar, ctx))	\
		BUG("qsort_s() failed");			\
} while (0)

/* git v2.53.0 git-compat-util.h:61-63 */
# define BARF_UNLESS_AN_ARRAY(arr)						\
	BUILD_ASSERT_OR_ZERO(!__builtin_types_compatible_p(__typeof__(arr), \
							   __typeof__(&(arr)[0])))

/* git v2.53.0 git-compat-util.h:88-88 */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]) + BARF_UNLESS_AN_ARRAY(x))

#endif