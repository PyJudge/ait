/* ait: requests and contract pull, spec/ait-phase1-spec.md v15 */
#ifndef AIT_H
#define AIT_H
#include "git-compat-util.h"
#include "strbuf.h"
#include "string-list.h"
struct strvec;

/* ait.c: the process, git, messages and the commands that are not requests */
/* the worktree, the hook's index, HEAD's ref and commit, the git and common directories (5.12) */
extern const char *top, *hook_index, *headref, *head, *gitdir, *commondir;
/* the empty tree, which every SHA-1 repository has (git v2.53.0 hash.h) */
#define EMPTY_TREE "4b825dc642cb6eb9a060e54bf8d69288fbee4904"
extern char agent[72], sid[16];	/* the session's agent and ID (5.1) */
extern char *git_error;		/* the first stderr line of the last git call, without git's "fatal: " or "error: " */
extern char *git_cmd;		/* the subcommand of the last git call */
extern size_t git_out_len;	/* the length of the last git output */
char *git_args(struct strvec *args, const char *in, const char *index, int *rc);
char *git_run(const char *in, const char *index, int *rc, const char *fmt, ...);
#define git(...) git_run(NULL, NULL, NULL, __VA_ARGS__)
char *git_failed(void);
void z_paths(const char *out, struct string_list *paths);
int config_true(const char *key);
char *object(const char *name, int contents, char *oid, size_t *size);
char *line1(char *s);
char *quoted(const char *s);
char *quoted_paths(struct string_list *paths);
char *shell_words(struct string_list *paths);
struct string_list *single(const char *s);
char *through_ait(struct string_list *paths);
void suggest(const char *fmt, ...) __attribute__((format (printf, 1, 2)));
NORETURN void reject(int code, const char *hint, const char *fmt, ...) __attribute__((format (printf, 3, 4)));
NORETURN void unsupported(const char *what);
void newid(char *out, int prefix);
const char *session(int need);
int lock_worktree(const char *wt);
void write_state(const char *path, const char *text);
const char *integration_head(void);

/* record.c: trees as git lists them; the index as git read-tree compares it */
struct entry {
	const char *path;
	char mode[8];
	char oid[48];
	int ita;		/* an intent-to-add index entry */
};
struct tree {
	char oid[48];
	size_t nr;
	struct entry *e;
};
struct tree *tree_load(const char *treeish);
struct tree *head_tree(void);
struct tree *index_load(struct tree *noted);
struct entry *tree_get(struct tree *t, const char *path);
struct entry *file_entry(struct tree *t, const char *path);
const char *blob_at(struct tree *t, const char *path);
int regular_mode(const char *mode);
const char *spec_file_why(const char *path);
void index_line(struct strbuf *sb, const char *mode, const char *oid, const char *path);
char *tree_with(const char *base, const char *index_info, struct string_list *paths);
void tree_diff(struct tree *a, struct tree *b, struct string_list *paths);

/* record.c: a record is its values per key (spec 2.2.2); "new:PATH", "PATH OID", "DIR/ OID" for scopes */
enum key { ROLE, STATE, CONSUMER, PROVIDER, TARGET, FILES, CHECK, CREATOR, PREV, NKEYS };
#define ALL_KEYS ((1u << NKEYS) - 1)
#define PIN_KEYS ((1u << PROVIDER) | (1u << FILES))	/* the lines PINS hashes (5.4) */
#define MEANING_KEYS (ALL_KEYS & ~((1u << PREV) | (1u << CHECK)))	/* the version body (2.2.4) */
struct record {
	char id[48];
	struct string_list f[NKEYS];
	struct record *old;		/* the base version: HEAD's, or GOOD's for a record entangled with HEAD's errors (5.10) */
	const char *base;		/* its blob, the prev of what a request writes (6.2); NULL for none */
	const char *base_at;		/* GOOD's commit when the base is GOOD's version */
	int rebased;			/* the base is not HEAD's version (5.10) */
	const char *alias;
	char *bad;			/* why it is malformed (5.10), and how to mend it: */
	char *mend;			/* "remove 'PATH'" for what is no record path (5.10) */
	int seq;			/* the order of a new record in the request: N of "+N" */
	int recheck;			/* a current contract this request checks again */
	int clause;			/* 1 + the helper clause that made it */
};
struct state {
	size_t nr;
	size_t alloc;
	struct record **r;
};
void link_ends(struct state *s);
void state_load(struct state *s, struct tree *t);
struct record *record_decode(const char *id, const char *oid);
struct record *record_written(const char *id, const char *text);
struct record *record_from_entry(struct entry *e, const char *id, int written);
int record_step(const char *id, const char *from, const char *to, int gate);
struct record *state_get(struct state *s, const char *id);
struct record *state_add(struct state *s, const char *id);
void state_drop(struct state *s, const char *id);
struct record *record_copy(struct record *r);
char *record_text(struct record *r, unsigned keys_wanted);
const char *val(struct record *r, enum key k);
void put(struct record *r, enum key k, const char *v);
int in_state(struct record *r, const char *state);
int one_of(const char *word, const char *const *set);
int is_abstract(struct record *r);
int is_live(struct record *r);
int is_current(struct record *r);
int made_by(struct record *r, const char *me);
int was_current(struct record *r);
int undertaken(struct record *l);
const char *link_end_id(struct record *l, int offer);
struct record *link_end(struct state *s, struct record *l, int offer);
struct record *partner(struct state *s, struct record *l, struct record *r);

/* record.c: validity (5.9, 5.10); a failed check: exit code, porcelain word, hint, message, and K (and O for 5.9 d) */
struct verdict {
	int code;
	const char *word;
	char *hint;
	char *msg;
	struct record *k;
	struct record *o;
};
int check_state(struct state *s, struct tree *t, struct verdict *v);
int check_records(struct state *s, struct verdict *v);
void state_errors(struct state *s, struct tree *t, struct string_list *conflicts, struct string_list *out);
void integrate_at(struct tree *t);
void integration_validate(struct tree *t);
void integration_message(const char *path);
int as_in_base(struct record *r);
int malformed(struct state *s, struct verdict *v);
const char *format_error(struct record *r);

/* version.c: the versions of a contract file (2.2.4) and what is judged from them alone */
struct version {
	const char *oid;
	struct record *r;
	struct string_list ancestors;	/* the versions it includes, itself too */
	char *body;			/* its text without prev and check (2.2.4) */
	int walked;
	int accident;			/* -1 unknown, 0 none, 1 an accident, 2 being judged (2.3.3) */
};
struct version *version_of(const char *id, const char *oid);
struct version *version_in(const char *commit, const char *id);
void walk_version(struct version *v);
int contains(struct version *a, struct version *b);
struct string_list maximal(struct string_list *list);
int ended(struct version *v);
int includes_ended(struct version *v);
int follows(struct record *a, struct record *b, int gate);
int confirmed(struct record *r);
char *check_line(struct record *r, int result, struct string_list *from);
int accident(struct version *v);
int wrong_version(struct version *v);
int reopens(struct version *v, const struct string_list *parents);
void reopeners(const char *id, struct string_list *agents);
int my_promise(struct record *r, const char *me, struct string_list *by);
int newly_unverified(struct record *r, struct tree *t, const struct string_list *parents);
const char *unverified_since(struct record *r, const struct string_list *tips);
char *head_repair(struct verdict *v, int rerun);
void base_versions(struct state *s, struct string_list *written);
int accident_at(const char *commit, const char *id);
int accident_at_head(struct record *r, struct verdict *out, int rerun);
/* 5.10: a commit's state judged once (judge()): its records and errors; for HEAD, GOOD and what its errors entangle */
struct judgment {
	struct state s;
	struct string_list errors;
	struct string_list entangled;
	struct state good_state;
	const char *good;
	int good_sought;
};
struct judgment *judge(const char *commit);
int at_head(struct verdict *v);
int head_error(struct verdict *v);

/* record.c: names: paths, scopes, object, session and agent IDs (3, 1.10, 7.2.1) */
int path_ok(const char *p);
int oid_ok(const char *s);
int id_like(const char *s, const char *letters);
int sid_ok(const char *s);
int agent_ok(const char *name);
void scope_shape(const char *arg);
char *scope_path(const char *v);
const char *case_twin(struct tree *t, const char *path);
char *pin(const char *arg, struct tree *t, int allow_new, int allow_abs);
void repin(struct record *r, enum key k, struct tree *t, int new_only);
char *show(const char *scope, struct tree *t, int current);
int overlap(const char *arg, const char *r, struct tree *t);
char *shows(struct record *r, enum key k, struct tree *t, int current);
char *input_scope(const char *value, struct tree *t);
char *input_scopes(struct record *r, enum key k, struct tree *t);
int code_changed(struct record *r, struct tree *t);
int touches(struct record *r, struct tree *a, struct tree *b);
int same_meaning(struct record *a, struct record *b);
int code_found(struct record *r, struct tree *t);
void missing_code(struct record *r, struct tree *t, struct string_list *out);

/* record.c: the hints shared by the commands (5.9a) */
char *moved_arg(struct record *r, struct tree *t);
void reopen_set(struct state *s, struct record *k, struct string_list *out);
char *amend_clause(struct record *k, const char *provider, const char *files);
char *reopen_clauses(struct state *s, struct record *k, const char *args);
char *reopen_hint(struct state *s, struct record *k);

/* record.c: spec definitions and the spec files they pin (2.4, 2.5) */
int is_def(const char *file);
struct string_list *def_get(struct tree *t, const char *path);
void def_inputs(struct tree *t, struct string_list *defs, struct string_list *out);
char *def_text(struct tree *t, const char *path, const char *indent);
char *def_paths(struct record *r);
const char *engine_of(struct tree *t, const char *def);
int pinned_as(struct tree *t, const char *line);
int expand_files(struct record *r, struct tree *t);
int files_match(struct record *r, struct tree *t);
int declares(struct record *r, const char *path);
int contract_path(struct state *s, const char *path);

/* record.c: checks in history (1.5a, 5.4, 9.1) */
char *pins(struct record *r);
int unverified(struct record *r, struct tree *t);

/* record.c: YAML (2.5.1, 8) */
int read_text(struct strbuf *sb, const char *path);
int yaml_parse(const char *text, struct string_list *kv, const char *what, char **err);
void yaml_check(struct string_list *kv, const char *const *schema, const char *what, const char *hint, int kinds);
const char *yget(struct string_list *kv, const char *key);
char *yline(struct string_list *kv, const char *key);
char *nul_line(const char *buf);
void yaml_finish(struct strbuf *text, struct string_list *kv, struct string_list *renamed);
int yaml_out(void *sb, unsigned char *buf, size_t n);

/* request.c: the helpers, fulfill and commit share one pipeline */
int cmd_request(int argc, const char **argv);
int cmd_pull(int argc, const char **argv);
void changes_not_in_head(struct string_list *ids);
void ref_list(struct string_list *refs);
int cmd_fulfill(int argc, const char **argv);
const char *git_op(void);
const char *finishing_commit(void);
char *op_guidance(const char *hint);
void precondition(int allow_unborn, int contracts_staged);
extern const char *published;
extern void (*before_error)(void);
int publish(const char *tree, const char *msg, struct tree *index);
int publish_pull(const char *tree, const char *msg, struct tree *index, struct string_list *ends, int ff);
void pull_conflict(const char *tree, struct string_list *ends, const char *stages, const char *msg);
const char *pull_helpers(const char *tree, struct strvec *clauses, char *after_option, int force, struct string_list *conflicts, const char *hint, struct strbuf *out);

int check_state_except(struct state *s, struct tree *t, struct verdict *v, struct string_list *conflicts);
int expand_files_except(struct record *r, struct tree *t, struct string_list *conflicts);
void fulfill_hint(struct state *s, struct record *p, struct tree *t);
void recheck_hint(struct state *s, struct record *r, struct tree *h, const struct string_list *tips);
char *pull_options(int argc, const char **argv, struct strvec *args, int *force, struct strbuf *msg);

int scope_changed(const char *scope, struct tree *t);

#endif
