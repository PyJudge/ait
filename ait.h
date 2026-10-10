/* ait-mini: the bootstrap subset of spec/ait-phase1-spec.md v14 (spec/ait-mini.md) */
#ifndef AIT_H
#define AIT_H
#include "git-compat-util.h"
#include "strbuf.h"
#include "string-list.h"

/* ait.c: the process, git, messages and the commands that are not requests */
extern const char *top, *gindex, *headref, *head, *gitdir, *commondir;
extern char agent[72], sid[16];
extern char *git_error;		/* the first stderr line of the last git call, without git's "fatal: " or "error: " */
extern char *git_cmd;		/* the subcommand of the last git call */
extern size_t git_out_len;	/* the length of the last git output */
struct strvec;
char *git_args(struct strvec *args, const char *in, const char *index, int *rc);
char *git_run(const char *in, const char *index, int *rc, const char *fmt, ...);
#define git(...) git_run(NULL, NULL, NULL, __VA_ARGS__)
char *git_failed(void);
int ait_config_bool(const char *key);
char *object(const char *name, int contents, char *oid, size_t *size);
char *line1(char *s);
char *quoted(const char *s);
void suggest(const char *fmt, ...) __attribute__((format (printf, 1, 2)));
NORETURN void reject(int code, const char *hint, const char *fmt, ...) __attribute__((format (printf, 3, 4)));
NORETURN void unsupported(const char *what);
void newid(char *out, int prefix);
const char *session(int need);
int lock_worktree(const char *wt);
void write_state(const char *path, const char *text);
int integrating(void);

/* record.c: trees as git lists them; the index as git read-tree compares it */
struct entry { const char *path; char mode[8], oid[48]; int ita; };	/* ita: an intent-to-add index entry */
struct tree { char oid[48]; size_t nr; struct entry *e; };
struct tree *tree_load(const char *treeish);
struct tree *head_tree(void);
struct tree *index_load(struct tree *noted);
struct entry *tree_get(struct tree *t, const char *path);
struct entry *file_entry(struct tree *t, const char *path);
const char *blob_at(struct tree *t, const char *path);
const char *blob_text(const char *oid);
void index_line(struct strbuf *sb, const char *mode, const char *oid, const char *path);
char *tree_with(const char *base, const char *index_info, struct string_list *paths);
void tree_diff(struct tree *a, struct tree *b, struct string_list *paths);

/* record.c: a record is its values per key (spec 2.2.2); "new:PATH", "PATH OID", "DIR/ OID" for scopes */
enum key { ROLE, STATE, CONSUMER, PROVIDER, TARGET, FILES, CREATOR, PREV, NKEYS };
#define ALL_KEYS ((1u << NKEYS) - 1)
#define PIN_KEYS ((1u << PROVIDER) | (1u << FILES))	/* the lines PINS hashes (5.4) */
#define MEANING_KEYS (ALL_KEYS & ~(1u << PREV))		/* what a change of a record is (6.2) */
struct record {
	char id[48];
	struct string_list f[NKEYS];
	struct record *old;		/* the HEAD version */
	const char *alias;
	char *bad;			/* why it is malformed (5.10), and how to mend it: */
	char *mend;			/* "remove 'PATH'" or "restore 'PATH' ..." */
	int seq, recheck;
	int clause;			/* 1 + the helper clause that made it */
};
struct state { size_t nr, alloc; struct record **r; };
void state_load(struct state *s, struct tree *t);
struct record *state_get(struct state *s, const char *id);
struct record *state_add(struct state *s, const char *id);
struct record *record_copy(struct record *r);
char *record_text(struct record *r, unsigned keys_wanted);
const char *val(struct record *r, enum key k);
void put(struct record *r, enum key k, const char *v);
int in_state(struct record *r, const char *state);
int one_of(const char *word, const char *const *set);
int is_abstract(struct record *r);
int is_live(struct record *r);
int is_current(struct record *r);
int was_current(struct record *r);
int reopened(struct record *r);
int as_in_head(struct record *r);
int undertaken(struct record *l);
struct record *link_end(struct state *s, struct record *l, int offer);
struct record *partner(struct state *s, struct record *l, struct record *r);

/* record.c: validity (5.9, 5.10); a failed check: exit code, porcelain word, hint, message, and K (and O for 5.9 d) */
struct verdict { int code; const char *word; char *hint, *msg; struct record *k, *o; };
int check_state(struct state *s, struct tree *t, struct verdict *v);
void validate(struct state *s, struct tree *t);
void validate_at(struct tree *t);
struct record *readable(struct record *r);
int malformed(struct state *s, struct verdict *v);
const char *unusable(struct record *r, struct tree *t);

/* record.c: paths and scopes (3) */
int path_ok(const char *p);
int oid_ok(const char *s);
int agent_ok(const char *name);
void no_range(const char *arg);
void scope_shape(const char *arg);
char *scope_path(const char *v);
const char *scope_oid(const char *v);
const char *case_twin(struct tree *t, const char *path);
char *pin(const char *arg, struct tree *t, int allow_new, int allow_abs);
void repin(struct record *r, enum key k, struct tree *t, int new_only);
char *show(const char *scope, struct tree *t, int current);
char *shows(struct record *r, enum key k, struct tree *t, int current);
char *scope_args(struct record *r, enum key k);
int touches(struct record *r, struct tree *a, struct tree *b);
int code_found(struct record *r, struct tree *t);
void missing_code(struct record *r, struct tree *t, struct string_list *out);

/* record.c: the hints shared by the commands (5.9a) */
char *moved_arg(struct record *r, struct tree *t);
void reopen_set(struct state *s, struct record *k, struct string_list *out);
char *amend_clause(struct record *k, const char *provider, const char *files);
char *reopen_clauses(struct state *s, struct record *k, const char *defs);
char *reopen_hint(struct state *s, struct record *k);

/* record.c: spec definitions and the spec files they pin (2.4, 2.5) */
int is_def(const char *file);
struct string_list *def_get(struct tree *t, const char *path);
void def_inputs(struct tree *t, struct string_list *defs, struct string_list *out);
char *def_text(struct tree *t, const char *path, const char *indent);
char *def_paths(struct record *r);
const char *engine_of(struct tree *t, const char *def);
int expand_files(struct record *r, struct tree *t);
int files_match(struct record *r, struct tree *t);
int declares(struct record *r, const char *path);
int contract_path(struct state *s, const char *path);

/* record.c: checks in history (1.5a, 5.4, 9.1) */
char *pins(struct record *r);
int unverified(struct record *r, struct tree *t);
const char *unverified_since(struct record *r);

/* record.c: YAML (2.5.1, 8) */
int read_text(struct strbuf *sb, const char *path);
int yaml_parse(const char *text, struct string_list *kv, const char *what, char **err);
void yaml(const char *text, struct string_list *kv, const char *what);
void yaml_check(struct string_list *kv, const char *const *schema, const char *what, const char *hint, int kinds);
const char *yget(struct string_list *kv, const char *key);
char *yline(struct string_list *kv, const char *key);
char *nul_line(const char *buf);
void yaml_finish(struct strbuf *text, struct string_list *kv, struct string_list *renamed);
int yaml_out(void *sb, unsigned char *buf, size_t n);

/* request.c: the helpers, fulfill and commit share one pipeline */
int cmd_request(int argc, const char **argv);
int cmd_fulfill(int argc, const char **argv);
void precondition(int allow_unborn);
extern const char *published;
int publish(const char *tree, const char *msg, struct tree *index);
#endif
