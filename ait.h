/* ait v1.1: contracts on ait/ branches, spec/ait-v1.1-spec.md */
#ifndef AIT_H
#define AIT_H
#include "git-compat-util.h"
#include "strbuf.h"
#include "string-list.h"
#include "strvec.h"

/* ait.c: the process, git, messages and sessions */
extern const char *top, *head, *gitdir, *commondir;
#define EMPTY_TREE "4b825dc642cb6eb9a060e54bf8d69288fbee4904"
#define NULL_OID "0000000000000000000000000000000000000000"
extern char agent[72], sid[16];
extern char *git_error;
/* set after fulfill committed code: every refusal then says what was not recorded (spec 6.7.5-3) */
extern char *not_recorded;
extern size_t git_out_len;
char *git_args(struct strvec *args, const char *in, const char *index, int *rc);
char *git_run(const char *in, const char *index, int *rc, const char *fmt, ...);
char *git_feed(const struct strbuf *in, const char *index, int *rc, const char *fmt, ...);
#define git(...) git_run(NULL, NULL, NULL, __VA_ARGS__)
int git_pass(struct strvec *args);
char *git_failed(void);
char *line1(char *s);
int config_true(const char *key);
char *object(const char *name, int contents, char *oid, size_t *size);
char *quoted(const char *s);
void suggest(const char *fmt, ...) __attribute__((format (printf, 1, 2)));
NORETURN void reject(int code, const char *hint, const char *fmt, ...) __attribute__((format (printf, 3, 4)));
void newid(char *out, int prefix);
const char *session(int need);
void write_state(const char *path, const char *text);
char *state_path(const char *name);
int agent_ok(const char *name);
int id_ok(const char *s, const char *letters);

/* ait.c: the contract branch (spec 2.1, 4.2, 4.3) */
struct names {
	struct string_list local;	/* ait/<name> of refs/heads/ait/<name> */
	struct string_list remote;	/* ait/<name> -> the remote-tracking refs that have it */
};
void names_load(struct names *n);
const char *init_source(struct names *n, const char *name);
const char *branch_name(void);
void refuse_used(const char *branch, const char *commit);
void refuse_broken(int writes, int fulfils);
char *checkout_file(void);

/* ait.c: one checkin (spec 4.4) */
struct checkin {
	const char *branch;		/* ait/<name> */
	char tip[48];			/* T; "" when the branch has no commit */
	struct string_list put;		/* path -> its new text */
	const char *msg;		/* the message; its first line is the summary */
	const char *second_parent;	/* a merge (6.5.6) */
	char *tree;			/* a merge's tree, instead of put (6.5.6) */
	const char *refusal;		/* what a broken rule's message starts with (6.5.6) */
	int no_trailers;		/* ait init's commit */
	char rid[16];			/* this request's ID */
	char made[48];			/* the commit made */
	/* the hint for a structural violation the request would add (4.10), or NULL */
	char *(*violation_hint)(struct checkin *c, const char *key, void *data);
	void *data;
};
typedef void (*judge_fn)(struct checkin *c, void *data);
int checkin_ref(struct checkin *c, const char *commit);
void checkin_run(struct checkin *c, judge_fn judge, void *data);

/* contract.c: trees as git lists them */
struct entry {
	const char *path;
	char mode[8];
	char type[8];
	char oid[48];
};
struct tree {
	char oid[48];
	size_t nr;
	struct entry *e;
};
struct tree *tree_load(const char *treeish);
struct entry *tree_get(struct tree *t, const char *path);
const char *blob_at(struct tree *t, const char *path);
struct tree *index_tree(void);

/* contract.c: contracts and fulfilments (spec 2.3, 2.5) */
struct spec {
	char *body;
	char *engine;			/* "exec", or NULL for prompt */
	struct strvec command;
	struct string_list success;
	char *environment;
};
struct contract {
	char id[16];
	char *path;			/* <group>/<ID>.yaml */
	char *group;			/* "" for the root */
	char *condition_id, *change_id, *kind, *creator, *state, *target;
	struct string_list consumer, provider, undertakes, partially, fulfilled;
	struct spec *specs;
	size_t nspecs;
};
struct fulfilment {
	char *path, *contract, *condition_id, *by, *session, *commit, *outcome, *body;
	struct string_list code;	/* scope -> "mode type oid[ s-e]" */
};
struct branch {
	const char *name;
	char tip[48];
	struct tree *tree;
	struct string_list contracts;	/* ID -> struct contract */
	struct string_list fulfils;	/* path -> struct fulfilment */
	struct string_list errors;	/* "'path': why" */
};
struct branch *branch_load(const char *name, const char *tip, const char *treeish);
struct contract *contract_get(struct branch *b, const char *id);
struct contract *contract_parse(const char *text, const char *path, int draft, char **err);
char *contract_text(struct contract *c);
struct contract *contract_new(const char *kind, const char *group, const char *id);
struct fulfilment *fulfil_parse(const char *text, const char *path, char **err);
char *fulfil_text(struct fulfilment *f);
char *group_of(const char *path);
int group_ok(const char *group);
int path_ok(const char *p);
int scope_ok(const char *v, int allow_new, int allow_abs);
char *scope_path(const char *v);
const char *scope_blob(const char *v);
int range_of(const char *v, int *start, int *end);
void scopes_split(const char *list, struct string_list *out);
char *scopes_join(struct string_list *l);

/* contract.c: judgments (spec 1.4, 1.7, 1.8, 4.10, 5) */
enum verdict { V_FUTURE, V_CURRENT, V_UNVERIFIED, V_FULFILLED, V_CANCELLED, V_WITHDRAWN };
int live(struct contract *c);
int has_valid(struct branch *b, struct contract *c);
enum verdict judge(struct branch *b, struct contract *c, struct tree *x);
const char *verdict_word(enum verdict v);
void invariants(struct branch *b, struct string_list *out);
char *scope_pin(const char *arg, struct tree *t, char **err);
char *scope_locate(const char *scope, struct tree *t, char **err);
char *code_entry(const char *scope, struct tree *t, char **err);
int scope_same(const char *scope, const char *code, struct tree *x);
char *scope_display(const char *scope, struct tree *x);
int scope_overlap(const char *q, const char *r, struct tree *t);
int touched(struct branch *b, struct contract *c, struct tree *h, struct tree *x);

/* contract.c: the YAML subset (v14 2.5.1) and its writing (v14 8.2) */
int yaml_parse(const char *text, struct string_list *kv, const char *what, char **err);
const char *yget(struct string_list *kv, const char *key);
int yline(struct string_list *kv, const char *key);
int yaml_list(struct string_list *kv, const char *key, struct string_list *out);
int read_text(struct strbuf *sb, const char *path);
struct spec *spec_from(struct string_list *kv, const char *prefix, char **err);
void yaml_out_scalar(struct strbuf *sb, const char *s);
void yaml_out_body(struct strbuf *sb, const char *s, const char *indent);
void spec_lists(struct strbuf *sb, struct spec *s, int whole);

/* request.c, notify.c: the commands */
int cmd_request(int argc, const char **argv);
int cmd_contract(int argc, const char **argv);
int cmd_fulfill(int argc, const char **argv);
int cmd_status(int argc, const char **argv);
int cmd_sub(int argc, const char **argv);
int cmd_unsub(int argc, const char **argv);
int cmd_listen(int argc, const char **argv);
int cmd_session(int argc, const char **argv);
void session_tips(const char *s);

#endif
