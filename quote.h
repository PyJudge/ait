#ifndef QUOTE_H
#define QUOTE_H
struct strbuf;

void sq_quote_buf(struct strbuf *, const char *src);
void sq_quote_buf_pretty(struct strbuf *, const char *src);

/* git v2.53.0 quote.h:86-86 */
#define CQUOTE_NODQ 01

size_t quote_c_style(const char *name, struct strbuf *, FILE *, unsigned);
#endif