/* ait-mini: the bootstrap subset of spec/ait-phase1-spec.md v14 (spec/ait-mini.md) */
#ifndef AIT_H
#define AIT_H
#include "git-compat-util.h"
#include "strbuf.h"
#include "string-list.h"

extern const char *top, *gindex, *headref, *head, *gitdir, *commondir;
extern char *git_error;
extern size_t git_out_len;	/* the length of the last git output */
#define EMPTY_TREE "4b825dc642cb6eb9a060e54bf8d69288fbee4904"	/* the first stderr line of the last git call */

/* ait.c: process, messages and the commands that are not requests */
NORETURN void reject(int code, const char *hint, const char *fmt, ...) __attribute__((format (printf, 3, 4)));
NORETURN void unsupported(const char *what);
struct strvec;
char *git_args(struct strvec *args, const char *in, const char *index, int *rc);
char *git_run(const char *in, const char *index, int *rc, const char *fmt, ...);
#define git(...) git_run(NULL, NULL, NULL, __VA_ARGS__)
char *line1(char *s);
void newid(char *out, int prefix);
const char *session(int need);
extern char agent[72], sid[16];

/* record.c: trees, records and the judgements computed from them */
struct entry { const char *path; char mode[8], oid[48]; };
struct tree { char oid[48]; size_t nr; struct entry *e; };
struct tree *tree_load(const char *treeish);
struct entry *tree_get(struct tree *t, const char *path);
const char *blob_at(struct tree *t, const char *path);
const char *blob_text(const char *oid);
void blobs_read(struct string_list *oids);
char *tree_with(const char *base, const char *index_info);
void tree_diff(struct tree *a, struct tree *b, struct string_list *paths);

/* a record is its values per key (spec 2.2.2); "new:PATH", "PATH OID", "DIR/ OID" for scopes */
enum key { ROLE, STATE, CONSUMER, PROVIDER, TARGET, FILES, CREATOR, PREV, NKEYS };
struct record {
	char id[48];
	struct string_list f[NKEYS];
	struct record *old;		/* the HEAD version */
	const char *alias;
	char *bad;
	int seq, recheck;
};
struct state { size_t nr, alloc; struct record **r; };
void state_load(struct state *s, struct tree *t);
struct record *state_get(struct state *s, const char *id);
struct record *state_add(struct state *s, const char *id);
struct record *record_copy(struct record *r);
struct record *link_end(struct state *s, struct record *l, int offer);
const char *val(struct record *r, enum key k);
void put(struct record *r, enum key k, const char *v);
int in_state(struct record *r, const char *state);
char *scope_path(const char *v);
const char *scope_oid(const char *v);
int is_def(const char *file);
char *record_text(struct record *r, int with_prev);
int is_live(struct record *r);
int is_current(struct record *r);
int was_current(struct record *r);
struct verdict { int code; const char *word; char *hint, *msg; };
int check_state(struct state *s, struct tree *t, struct verdict *v);
void validate(struct state *s, struct tree *t);
int files_match(struct record *r, struct tree *t);
char *scope_args(struct record *r, enum key k);
char *def_paths(struct record *r);
int declares(struct record *r, const char *path);
int contract_path(struct state *s, const char *path);
int path_ok(const char *p);
char *pin(const char *arg, struct tree *t, int allow_new, int allow_abs);
char *show(const char *scope, struct tree *t, int current);
char *shows(struct record *r, enum key k, struct tree *t, int current);
char *pins(struct record *r);
char *pin_lines(struct record *r);
int code_found(struct record *r, struct tree *t);
int touches(struct record *r, struct tree *a, struct tree *b);
void repin(struct record *r, enum key k, struct tree *t, int new_only);
char *def_text(struct tree *t, const char *path, const char *indent);
const char *engine_of(struct tree *t, const char *def);
int expand_files(struct record *r, struct tree *t);
int unverified(struct record *r, struct tree *t, struct string_list *checks);
void checks_load(struct string_list *checks);
void yaml(const char *text, struct string_list *kv, const char *what);
const char *yget(struct string_list *kv, const char *key);
struct string_list *def_get(struct tree *t, const char *path);

/* request.c: the helpers, fulfill and commit share one pipeline */
int cmd_request(int argc, const char **argv);
int cmd_fulfill(int argc, const char **argv);
void precondition(int allow_unborn);
char *publish(struct string_list *paths, int only, const char *msg);
#endif
