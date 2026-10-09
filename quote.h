#ifndef QUOTE_H
#define QUOTE_H
struct strbuf;

void sq_quote_buf(struct strbuf *, const char *src);
void sq_quote_buf_pretty(struct strbuf *, const char *src);
#endif