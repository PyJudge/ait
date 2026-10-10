#ifndef PRECOMPOSE_H
#define PRECOMPOSE_H

/* ait reads the setting with git config */
int ait_config_bool(const char *key);
#ifdef PRECOMPOSE_UNICODE
const char *precompose_string_if_needed(const char *in);
const char *precompose_argv_prefix(int argc, const char **argv, const char *prefix);
#else
/* git v2.53.0 git-compat-util.h:174-183 */
static inline const char *precompose_argv_prefix(int argc MAYBE_UNUSED,
						 const char **argv MAYBE_UNUSED,
						 const char *prefix)
{
	return prefix;
}
static inline const char *precompose_string_if_needed(const char *in)
{
	return in;
}

#endif
#endif