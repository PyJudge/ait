#include "git-compat-util.h"
#include "strbuf.h"

/* git v2.53.0 advice.c:96-98 */
static const char turn_off_instructions[] =
N_("\n"
   "Disable this message with \"git config set advice.%s false\"");

/* git v2.53.0 advice.c:100 */
static void vadvise(const char *advice, int display_instructions,
		    const char *key, va_list params)
{
	struct strbuf buf = STRBUF_INIT;
	const char *cp, *np;

	strbuf_vaddf(&buf, advice, params);

	if (display_instructions)
		strbuf_addf(&buf, turn_off_instructions, key);

	for (cp = buf.buf; *cp; cp = np) {
		np = strchrnul(cp, '\n');
		fprintf(stderr,	_("%shint:%s%.*s%s\n"),
			advise_get_color(ADVICE_COLOR_HINT),
			(np == cp) ? "" : " ",
			(int)(np - cp), cp,
			advise_get_color(ADVICE_COLOR_RESET));
		if (*np)
			np++;
	}
	strbuf_release(&buf);
}

/* git v2.53.0 advice.c:124 */
void advise(const char *advice, ...)
{
	va_list params;
	va_start(params, advice);
	vadvise(advice, 0, "", params);
	va_end(params);
}
