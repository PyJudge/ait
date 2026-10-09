#ifndef STRVEC_H
#define STRVEC_H

/* git v2.53.0 strvec.h:30-38 */
struct strvec {
	const char **v;
	size_t nr;
	size_t alloc;
};

#define STRVEC_INIT { \
	.v = empty_strvec, \
}

extern const char *empty_strvec[];
void strvec_init(struct strvec *);
const char *strvec_push(struct strvec *, const char *);
__attribute__((format (printf,2,3)))
const char *strvec_pushf(struct strvec *, const char *fmt, ...);
LAST_ARG_MUST_BE_NULL
void strvec_pushl(struct strvec *, ...);
void strvec_push_nodup(struct strvec *, char *);
void strvec_pushv(struct strvec *, const char **);
void strvec_clear(struct strvec *);
#endif