# Regenerates the files ported from git v2.53.0 (strbuf.c, usage.c, run-command.c, ...) from a checkout in ../git-ref.
# Port git v2.53.0 functions verbatim into ait, each with its source location.
import sys, os
G = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'git-ref')
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
src = {}
def lines(p):
    if p not in src: src[p] = open(os.path.join(G, p)).read().split('\n')
    return src[p]
def fn(p, n):
    L = lines(p); i = n - 1; out = []
    while True:
        out.append(L[i])
        if L[i] == '}' or L[i].startswith('};') or (i > n - 1 and L[i] == '' and L[n-1].startswith('#define')): break
        i += 1
    return '/* git v2.53.0 %s:%d */\n%s\n' % (p, n, '\n'.join(out).rstrip())
def rng(p, a, b):
    return '/* git v2.53.0 %s:%d-%d */\n%s\n' % (p, a, b, '\n'.join(lines(p)[a-1:b]))
def write(name, head, parts, subs=()):
    s = head + '\n' + '\n'.join(parts)
    for a, b in subs:
        if a not in s: sys.exit('missing in %s: %r' % (name, a))
        s = s.replace(a, b)
    open(os.path.join(OUT, name), 'w').write(s)

# strbuf
write('strbuf.c', '#include "git-compat-util.h"\n#include "strbuf.h"\n', [
    fn('strbuf.c', 11), rng('strbuf.c', 62, 67), fn('strbuf.c', 69), fn('strbuf.c', 77), fn('strbuf.c', 85),
    fn('strbuf.c', 106), fn('strbuf.c', 125), fn('strbuf.c', 246), fn('strbuf.c', 311), fn('strbuf.c', 356), fn('strbuf.c', 407),
    fn('strbuf.c', 523), fn('strbuf.c', 550), fn('strbuf.c', 768), fn('strbuf.c', 974), fn('strbuf.c', 981),
    fn('strbuf.c', 1058), fn('strbuf.c', 1085)],
    [('die(_("unable to format message: %s"), fmt);', 'die("unable to format message: %s", fmt);')])
write('strbuf.h', '#ifndef STRBUF_H\n#define STRBUF_H\n', [
    rng('strbuf.h', 74, 81), '''void strbuf_init(struct strbuf *sb, size_t alloc);
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
char *xstrvfmt(const char *fmt, va_list ap);''',
    fn('strbuf.h', 143), fn('strbuf.h', 164), rng('strbuf.h', 178, 178), fn('strbuf.h', 232), fn('strbuf.h', 308),
    fn('strbuf.h', 655), fn('strbuf.h', 661), fn('strbuf.h', 715),
    '#endif'])
# strvec
write('strvec.c', '#include "git-compat-util.h"\n#include "strvec.h"\n#include "strbuf.h"\n', [
    rng('strvec.c', 5, 5), fn('strvec.c', 7), fn('strvec.c', 13), fn('strvec.c', 23), fn('strvec.c', 29),
    fn('strvec.c', 42), fn('strvec.c', 53), fn('strvec.c', 130)])
write('strvec.h', '#ifndef STRVEC_H\n#define STRVEC_H\n', [
    rng('strvec.h', 30, 38), '''extern const char *empty_strvec[];
void strvec_init(struct strvec *);
const char *strvec_push(struct strvec *, const char *);
__attribute__((format (printf,2,3)))
const char *strvec_pushf(struct strvec *, const char *fmt, ...);
LAST_ARG_MUST_BE_NULL
void strvec_pushl(struct strvec *, ...);
void strvec_push_nodup(struct strvec *, char *);
void strvec_pushv(struct strvec *, const char **);
void strvec_clear(struct strvec *);
#endif'''])
# string-list
write('string-list.c', '#include "git-compat-util.h"\n#include "string-list.h"\n', [
    fn('string-list.c', 10), fn('string-list.c', 18), fn('string-list.c', 43), fn('string-list.c', 63), fn('string-list.c', 70), fn('string-list.c', 87), fn('string-list.c', 100),
    fn('string-list.c', 164), fn('string-list.c', 207), fn('string-list.c', 218), rng('string-list.c', 226, 241), fn('string-list.c', 243),
    fn('string-list.c', 250),
    fn('string-list.c', 262), fn('string-list.c', 282), fn('string-list.c', 318), fn('string-list.c', 351)])
write('string-list.h', '#ifndef STRING_LIST_H\n#define STRING_LIST_H\n', [
    rng('string-list.h', 68, 96), rng('string-list.h', 160, 163), '''enum {
	STRING_LIST_SPLIT_TRIM = (1 << 0),
	STRING_LIST_SPLIT_NONEMPTY = (1 << 1),
};
void string_list_init_dup(struct string_list *list);
struct string_list_item *string_list_insert(struct string_list *list, const char *string);
bool string_list_has_string(const struct string_list *list, const char *string);
void string_list_remove(struct string_list *list, const char *string, int free_util);
struct string_list_item *string_list_lookup(struct string_list *list, const char *string);
void string_list_sort(struct string_list *list);
void string_list_clear(struct string_list *list, int free_util);
struct string_list_item *string_list_append_nodup(struct string_list *list, char *string);
struct string_list_item *string_list_append(struct string_list *list, const char *string);
struct string_list_item *unsorted_string_list_lookup(struct string_list *list, const char *string);
int unsorted_string_list_has_string(struct string_list *list, const char *string);
int string_list_split(struct string_list *list, const char *string, const char *delim, int maxsplit);
#endif'''])

# usage (die/error/warning), advice (advise)
write('usage.c', '#include "git-compat-util.h"\n#include "strbuf.h"\n\nint BUG_exit_code;\nint bug_called_must_BUG;\n', [
    fn('usage.c', 12), fn('usage.c', 40), fn('usage.c', 45), fn('usage.c', 69), fn('usage.c', 82),
    fn('usage.c', 90), fn('usage.c', 97), fn('usage.c', 104), rng('usage.c', 126, 133),
    fn('usage.c', 135), fn('usage.c', 140), fn('usage.c', 145), fn('usage.c', 150), fn('usage.c', 155), fn('usage.c', 170),
    fn('usage.c', 179), fn('usage.c', 201), fn('usage.c', 215), fn('usage.c', 239), fn('usage.c', 256),
    fn('usage.c', 279), fn('usage.c', 291), fn('usage.c', 311), fn('usage.c', 323), fn('usage.c', 334),
    fn('usage.c', 353)])
write('advice.c', '#include "git-compat-util.h"\n#include "strbuf.h"\n', [
    rng('advice.c', 96, 98), fn('advice.c', 100), fn('advice.c', 124)])
# wrapper and compat
write('wrapper.c', '#include "git-compat-util.h"\n#include "strbuf.h"\n', [
    fn('wrapper.c', 21), fn('wrapper.c', 41), fn('wrapper.c', 49), fn('wrapper.c', 74), fn('wrapper.c', 79),
    fn('wrapper.c', 95), fn('wrapper.c', 111), fn('wrapper.c', 116), fn('wrapper.c', 130), fn('wrapper.c', 146),
    fn('wrapper.c', 171), fn('wrapper.c', 205), fn('wrapper.c', 228), fn('wrapper.c', 250), fn('wrapper.c', 286),
    fn('wrapper.c', 305), fn('wrapper.c', 346), fn('wrapper.c', 375), fn('wrapper.c', 682), fn('wrapper.c', 691), fn('wrapper.c', 785), fn('compat/nonblock.c', 6), fn('compat/qsort_s.c', 9), fn('compat/qsort_s.c', 48)])
# setup: the standard descriptors open, as git's common main makes them before anything (common-init.c:45)
write('setup.c', '#include "git-compat-util.h"\n#include "setup.h"\n', [fn('setup.c', 2074)])
write('setup.h', '#ifndef SETUP_H\n#define SETUP_H\n', ['void sanitize_stdfds(void);\n#endif'])
# precompose: argv precomposed from NFD to NFC as git does on macOS (core.precomposeUnicode); the setting read by ait
write('precompose.c', '#include "git-compat-util.h"\n#include "precompose.h"\n#ifdef PRECOMPOSE_UNICODE\n#include <iconv.h>\n'
      'typedef char *iconv_ibp;\nstatic int precomposed_unicode = -1;\n', [
    rng('compat/precompose_utf8.c', 19, 20), fn('compat/precompose_utf8.c', 22), fn('utf8.c', 486),
    fn('compat/precompose_utf8.c', 68), fn('compat/precompose_utf8.c', 98), '#endif'],
    [('repo_config_get_bool(the_repository, "core.precomposeunicode", &precomposed_unicode);',
      'precomposed_unicode = ait_config_bool("core.precomposeunicode");')])
write('precompose.h', '#ifndef PRECOMPOSE_H\n#define PRECOMPOSE_H\n', [
    '/* ait reads the setting with git config */\nint ait_config_bool(const char *key);\n#ifdef PRECOMPOSE_UNICODE\n'
    'const char *precompose_string_if_needed(const char *in);\n'
    'const char *precompose_argv_prefix(int argc, const char **argv, const char *prefix);\n#else',
    rng('git-compat-util.h', 174, 183), '#endif\n#endif'],
    [('argc UNUSED,', 'argc MAYBE_UNUSED,'), ('argv UNUSED,', 'argv MAYBE_UNUSED,')])
# utf8: validity only; is_utf8 steps with pick_one_utf8_char instead of utf8_width, which also measures the column width
write('utf8.c', '#include "git-compat-util.h"\n#include "utf8.h"\n', [
    fn('utf8.c', 122), fn('utf8.c', 241)],
    [('utf8_width(&text, NULL);', 'pick_one_utf8_char(&text, NULL);')])
write('utf8.h', '#ifndef UTF8_H\n#define UTF8_H\n', [
    rng('utf8.h', 6, 6), 'int is_utf8(const char *text);\n#endif'])
write('quote.c', '#include "git-compat-util.h"\n#include "strbuf.h"\n#include "quote.h"\n', [
    fn('quote.c', 11), fn('quote.c', 28), fn('quote.c', 50), rng('quote.c', 9, 9), rng('quote.c', 215, 233),
    fn('quote.c', 235), fn('quote.c', 243), fn('quote.c', 267), fn('quote.c', 322)])
write('quote.h', '#ifndef QUOTE_H\n#define QUOTE_H\nstruct strbuf;\n', [
    'void sq_quote_buf(struct strbuf *, const char *src);\nvoid sq_quote_buf_pretty(struct strbuf *, const char *src);\n',
    rng('quote.h', 86, 86), 'size_t quote_c_style(const char *name, struct strbuf *, FILE *, unsigned);\n#endif'])
# run-command: start_command(), finish_command(), pipe_command() and what they call
write('run-command.c', '''#include "git-compat-util.h"
#include "run-command.h"
#include "strbuf.h"
#include "string-list.h"
''', [
    fn('run-command.c', 22), fn('run-command.c', 28), fn('run-command.c', 126), rng('run-command.c', 132, 172),
    rng('run-command.c', 174, 224), fn('run-command.c', 225), fn('run-command.c', 278), fn('run-command.c', 289),
    rng('run-command.c', 312, 549),
    fn('run-command.c', 551), fn('run-command.c', 558), rng('run-command.c', 677, 991), fn('run-command.c', 993),
    rng('run-command.c', 1295, 1316), fn('run-command.c', 1317), fn('run-command.c', 1394),
    fn('run-command.c', 1418)])
write('run-command.h', '#ifndef RUN_COMMAND_H\n#define RUN_COMMAND_H\n#include "strvec.h"\nstruct strbuf;\n', [
    rng('run-command.h', 44, 150), '''void child_process_init(struct child_process *);
void child_process_clear(struct child_process *);
int start_command(struct child_process *);
int finish_command(struct child_process *);
int pipe_command(struct child_process *cmd,
		 const char *in, size_t in_len,
		 struct strbuf *out, size_t out_hint,
		 struct strbuf *err, size_t err_hint);
#endif'''])

# git-compat-util.h: the subset of git's portability layer these files use
DECLS = '''
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
int xdup(int fd);
int csprng_bytes(void *buf, size_t len, unsigned flags);
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
'''
write('git-compat-util.h', '''#ifndef GIT_COMPAT_UTIL_H
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
''', [
    rng('git-compat-util.h', 378, 379), rng('git-compat-util.h', 395, 395), rng('git-compat-util.h', 414, 414),
    rng('compat/posix.h', 42, 43), rng('compat/posix.h', 155, 160), rng('wrapper.h', 133, 140), rng('git-compat-util.h', 464, 464), rng('git-compat-util.h', 994, 994),
    rng('git-compat-util.h', 1014, 1014), DECLS,
    rng('git-compat-util.h', 56, 57), rng('git-compat-util.h', 64, 66), rng('git-compat-util.h', 90, 90),
    rng('git-compat-util.h', 95, 96), rng('git-compat-util.h', 107, 108), rng('git-compat-util.h', 115, 116),
    fn('git-compat-util.h', 350), rng('git-compat-util.h', 354, 354), fn('git-compat-util.h', 490), fn('git-compat-util.h', 523), fn('git-compat-util.h', 540),
    fn('git-compat-util.h', 608), rng('git-compat-util.h', 615, 615), fn('git-compat-util.h', 618),
    rng('git-compat-util.h', 697, 704), rng('git-compat-util.h', 721, 723), rng('git-compat-util.h', 734, 740),
    rng('git-compat-util.h', 804, 804), rng('git-compat-util.h', 849, 858), rng('compat/posix.h', 420, 429),
    rng('compat/posix.h', 456, 462), rng('compat/posix.h', 467, 476),
    rng('git-compat-util.h', 61, 63), rng('git-compat-util.h', 88, 88), '#endif'])
