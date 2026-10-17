#include "git-compat-util.h"
#include "precompose.h"
#ifdef PRECOMPOSE_UNICODE
#include <iconv.h>
typedef char *iconv_ibp;
static int precomposed_unicode = -1;

/* git v2.53.0 compat/precompose_utf8.c:19-20 */
static const char *repo_encoding = "UTF-8";
static const char *path_encoding = "UTF-8-MAC";

/* git v2.53.0 compat/precompose_utf8.c:22 */
static size_t has_non_ascii(const char *s, size_t maxlen, size_t *strlen_c)
{
	const uint8_t *ptr = (const uint8_t *)s;
	size_t strlen_chars = 0;
	size_t ret = 0;

	if (!ptr || !*ptr)
		return 0;

	while (*ptr && maxlen) {
		if (*ptr & 0x80)
			ret++;
		strlen_chars++;
		ptr++;
		maxlen--;
	}
	if (strlen_c)
		*strlen_c = strlen_chars;

	return ret;
}

/* git v2.53.0 utf8.c:486 */
char *reencode_string_iconv(const char *in, size_t insz, iconv_t conv,
			    size_t bom_len, size_t *outsz_p)
{
	size_t outsz, outalloc;
	char *out, *outpos;
	iconv_ibp cp;

	outsz = insz;
	outalloc = st_add(outsz, 1 + bom_len); /* for terminating NUL */
	out = xmalloc(outalloc);
	outpos = out + bom_len;
	cp = (iconv_ibp)in;

	while (1) {
		size_t cnt = iconv(conv, &cp, &insz, &outpos, &outsz);

		if (cnt == (size_t) -1) {
			size_t sofar;
			if (errno != E2BIG) {
				free(out);
				return NULL;
			}
			/* insz has remaining number of bytes.
			 * since we started outsz the same as insz,
			 * it is likely that insz is not enough for
			 * converting the rest.
			 */
			sofar = outpos - out;
			outalloc = st_add3(sofar, st_mult(insz, 2), 32);
			out = xrealloc(out, outalloc);
			outpos = out + sofar;
			outsz = outalloc - sofar - 1;
#ifdef ICONV_RESTART_RESET
			/*
			 * If iconv(3) messes up piecemeal conversions
			 * then restore the original pointers, sizes,
			 * and converter state, then retry converting
			 * the full string using the reallocated buffer.
			 */
			insz += cp - (iconv_ibp)in; /* Restore insz */
			cp = (iconv_ibp)in;         /* original start value */
			outpos = out + bom_len;     /* original start value */
			outsz = outalloc - bom_len - 1; /* new len */
			iconv(conv, NULL, NULL, NULL, NULL); /* reset iconv machinery */
#endif
		}
		else {
			*outpos = '\0';
			if (outsz_p)
				*outsz_p = outpos - out;
			break;
		}
	}
	return out;
}

/*
 * git v2.53.0 compat/precompose_utf8.c:68, changed by ait: the setting is
 * read by config_true() (ait.c), as ait has no repository config API
 */
const char *precompose_string_if_needed(const char *in)
{
	size_t inlen;
	size_t outlen;
	if (!in)
		return NULL;
	if (has_non_ascii(in, (size_t)-1, &inlen)) {
		iconv_t ic_prec;
		char *out;
		if (precomposed_unicode < 0)
			precomposed_unicode = config_true("core.precomposeunicode");
		if (precomposed_unicode != 1)
			return in;
		ic_prec = iconv_open(repo_encoding, path_encoding);
		if (ic_prec == (iconv_t) -1)
			return in;

		out = reencode_string_iconv(in, inlen, ic_prec, 0, &outlen);
		if (out) {
			if (outlen == inlen && !memcmp(in, out, outlen))
				free(out); /* no need to return indentical */
			else
				in = out;
		}
		iconv_close(ic_prec);

	}
	return in;
}

/* git v2.53.0 compat/precompose_utf8.c:98 */
const char *precompose_argv_prefix(int argc, const char **argv, const char *prefix)
{
	int i = 0;

	while (i < argc) {
		argv[i] = precompose_string_if_needed(argv[i]);
		i++;
	}
	return precompose_string_if_needed(prefix);
}

#endif