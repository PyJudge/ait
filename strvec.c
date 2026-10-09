#include "git-compat-util.h"
#include "strvec.h"
#include "strbuf.h"

/* git v2.53.0 strvec.c:5-5 */
const char *empty_strvec[] = { NULL };

/* git v2.53.0 strvec.c:7 */
void strvec_init(struct strvec *array)
{
	struct strvec blank = STRVEC_INIT;
	memcpy(array, &blank, sizeof(*array));
}

/* git v2.53.0 strvec.c:13 */
void strvec_push_nodup(struct strvec *array, char *value)
{
	if (array->v == empty_strvec)
		array->v = NULL;

	ALLOC_GROW(array->v, array->nr + 2, array->alloc);
	array->v[array->nr++] = value;
	array->v[array->nr] = NULL;
}

/* git v2.53.0 strvec.c:23 */
const char *strvec_push(struct strvec *array, const char *value)
{
	strvec_push_nodup(array, xstrdup(value));
	return array->v[array->nr - 1];
}

/* git v2.53.0 strvec.c:29 */
const char *strvec_pushf(struct strvec *array, const char *fmt, ...)
{
	va_list ap;
	struct strbuf v = STRBUF_INIT;

	va_start(ap, fmt);
	strbuf_vaddf(&v, fmt, ap);
	va_end(ap);

	strvec_push_nodup(array, strbuf_detach(&v, NULL));
	return array->v[array->nr - 1];
}

/* git v2.53.0 strvec.c:42 */
void strvec_pushl(struct strvec *array, ...)
{
	va_list ap;
	const char *arg;

	va_start(ap, array);
	while ((arg = va_arg(ap, const char *)))
		strvec_push(array, arg);
	va_end(ap);
}

/* git v2.53.0 strvec.c:53 */
void strvec_pushv(struct strvec *array, const char **items)
{
	for (; *items; items++)
		strvec_push(array, *items);
}

/* git v2.53.0 strvec.c:130 */
void strvec_clear(struct strvec *array)
{
	if (array->v != empty_strvec) {
		for (size_t i = 0; i < array->nr; i++)
			free((char *)array->v[i]);
		free(array->v);
	}
	strvec_init(array);
}
