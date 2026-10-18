/* ait v1.1: contracts on ait/ branches, spec/ait-v1.1-spec.md */
#ifndef AIT_H
#define AIT_H
#include "git-compat-util.h"
#include "strbuf.h"
#include "string-list.h"
#include "strvec.h"

/* ait.c: the process, git, messages and sessions */
extern const char *top, *head, *gitdir;
#define EMPTY_TREE "4b825dc642cb6eb9a060e54bf8d69288fbee4904"
#define NULL_OID "0000000000000000000000000000000000000000"
extern char agent[72], sid[16];
extern char *git_error;
/* set after fulfill committed code: every refusal then says what was not recorded (spec 6.7.5-3) */
extern char *not_recorded;
extern size_t git_out_len;
void git_records_argv(struct string_list *out, const char *index, int *rc, const char **argv);
#define git_records(out, index, rc, ...) \
	git_records_argv(out, index, rc, (const char *[]){ __VA_ARGS__, NULL })
void guard_git_children(void);
char *git_argv(const char *in, const char *index, int *rc, const char **argv);
#define git_run(in, index, rc, ...) \
	git_argv(in, index, rc, (const char *[]){ __VA_ARGS__, NULL })
#define git(...) git_run(NULL, NULL, NULL, __VA_ARGS__)
int git_pass(struct strvec *args);
int git_commit(const char *msg);
char *line1(char *s);
int config_true(const char *key);
char *object(const char *name, int contents, char *oid, size_t *size);
int object_oid(const char *name, char *oid);
int branch_tip(const char *name, char *oid);
char *quoted(const char *s);
char *quoted_list(const struct string_list *names);
char *hint_command_for(const char *session_id, const char *fmt, ...) __attribute__((format (printf, 2, 3)));
#define hint_command(...) hint_command_for(NULL, __VA_ARGS__)
void suggest(const char *fmt, ...) __attribute__((format (printf, 1, 2)));
NORETURN void reject(int code, const char *hint, const char *fmt, ...) __attribute__((format (printf, 3, 4)));
void newid(char *out, int prefix);
const char *session(int need);
int lock_session(const char *s);
int listener_lock(const char *s);
int hook_installed(void);
char *session_path(const char *s, const char *suffix);
void session_lines(const char *s, struct string_list *lines);
void session_write(const char *s, struct string_list *lines);
void write_state(const char *path, const char *text);
int create_file(const char *path, const char *text, mode_t mode);
NORETURN void write_error(const char *path);
NORETURN void listen_write_error(const char *path);
char *worktree_state_dir(void);
NORETURN void no_contract(const char *id);
char *state_path(const char *name);
int agent_ok(const char *name);
int id_ok(const char *s, const char *letters);
int oid_ok(const char *s);

/* ait.c: the contract branch (spec 2.1, 4.2, 4.3) */
struct names {
	struct string_list local;	/* ait/<name> -> configured upstream ref */
	struct string_list remote;	/* ait/<name> -> the remote-tracking refs that have it */
	struct string_list remotes;	/* the configured remotes (git remote) */
	struct string_list tips;		/* tip OID REF, from the same classified ref snapshot */
	size_t nr;			/* distinct names in local and remote */
};
void remote_names(struct string_list *out);
void names_load(struct names *n);
void names_clear(struct names *n);
const char *names_tip(struct names *n, const char *ref);
const char *names_upstream(struct names *n, const char *branch);
const char *branch_find(struct names *n, const char *chosen, struct strbuf *msg, struct strbuf *hint, int *only);
int branch_only(void);
enum { START_SESSION = 1, START_WRITES = 2, START_FULFILS = 4, START_WARNS = 8 };
struct branch *start(int how);
char *git_path(const char *name);
int is_ancestor(const char *a, const char *b);
char *checkout_read(void);

/* spec 4.8: E_STATE reasons; the words are chosen once in refusal_words(). */
enum refusal {
	CHANGE_OK,
	ENDED,
	TARGET,
	IDENTITY,
	STATE,
	FULFILMENT,
	FORCE_PROVIDER,
	STALE_DRAFT,
	EXISTS,
	UNMERGED,
	MIX_WHOLLY,
	MIX_PARTIALLY,
	PAIR
};
char *refusal_words(enum refusal why, const char *a, const char *b, const char *detail);

/* spec 4.10: a mix on demand A, held by B; or A fulfilled on an unfulfilled B */
struct violation {
	enum refusal kind;
	char *a;
	char *b;
};

/* ait.c: one checkin (spec 4.4) */
struct checkin {
	const char *branch;		/* ait/<name> */
	char tip[48];			/* T; "" when the branch has no commit */
	struct string_list put;		/* path -> its new text */
	const char *msg;		/* the message; its first line is the summary */
	const char *record;		/* the commit message, its trailers validated once (4.5) */
	int no_trailers;		/* ait init's commit: no trailers, no summary line (6.1) */
	char rid[16];			/* this request's ID */
	/* contract pull (6.5.6): a merge's tree and second parent, or the commit to fast-forward to */
	char *tree;
	const char *second_parent;
	const char *ff;
	const char *refusal;		/* what a broken rule's message starts with (6.5.6) */
	/* the hint for a structural violation the request would add (4.10), or NULL */
	char *(*violation_hint)(struct checkin *c, struct string_list *violations);
	/* the command again, when the branch moved from OLD and that is still right (6.7.5-3), or NULL */
	char *(*retry_hint)(struct checkin *c, const char *old);
	/* Step zero's planned fulfilments, before their files exist (6.7.5). */
	const struct string_list *fulfilling;
	struct branch *at;		/* the contracts at T, for the judge */
	void *data;			/* the command's own state, for its judge and hints */
};
typedef void (*judge_fn)(struct checkin *c);
char *request_message(const char *msg, int fulfilling);
char *checked_record(const char *msg, const char *rid);
void checkin_tip(struct checkin *c);
void checkin_clear(struct checkin *c);
char *checkin_judge(struct checkin *c, judge_fn decide);
char *checkin_run(struct checkin *c, judge_fn decide);
void checkin_report(char *summary);
NORETURN void checkin_moved(struct checkin *c);
char *tree_with(const char *treeish, const struct strbuf *info);

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
void tree_free(struct tree *t);
const char *blob_at(struct tree *t, const char *path);
struct tree *index_tree(void);

/* scope.c: like Git pathspec items, parse at input, then pass owned values. */
enum scope_kind { SCOPE_FILE, SCOPE_RANGE, SCOPE_NEW, SCOPE_DIR, SCOPE_ROOT };
enum scope_form { SCOPE_ARGUMENT, SCOPE_DRAFT, SCOPE_STORED };
struct scope {
	enum scope_kind kind;
	char *path;
	int first;
	int last;
	char base[48];
};
struct scope_list {
	struct scope *items;
	size_t nr;
	size_t alloc;
};
#define for_each_scope(s, list) \
	for ((s) = (list)->items; (s) && (s) < (list)->items + (list)->nr; (s)++)
const char *range_numbers(const char *text, int *first, int *last);
char *scope_parse(struct scope *s, const char *text, int allow_new, int allow_abs, enum scope_form form);
char *scopes_parse(struct scope_list *out, const char *text, int allow_new, int allow_abs, enum scope_form form);
void scope_clear(struct scope *s);
void scopes_clear(struct scope_list *list);
void scopes_copy(struct scope_list *out, const struct scope_list *from);
void scope_append(struct scope_list *out, const struct scope *s);
int scope_equal(const struct scope *a, const struct scope *b, int with_base);
int scopes_has(const struct scope_list *list, const struct scope *s, int with_base);
char *scope_text(const struct scope *s, int with_base);
char *scope_field(const char *location);
const struct scope **scope_order(const struct scope_list *list);
char *scopes_text(const struct scope_list *list, int with_base);

/* contract.c: contracts and fulfilments (spec 2.3, 2.5); values.c owns their copies, scope.c their code keys */
struct spec {
	char *body;
	char *engine;			/* "exec", or NULL for prompt */
	struct strvec command;
	struct string_list success;
	char *environment;
	struct scope_list fixtures;	/* files and line ranges of the code the check reads (U60) */
};
/* spec 1.1: the kinds of contract; their words, ID letters and keys are in one table (contract.c) */
enum kind { DEMAND, OFFER, DROP };
const char *kind_word(enum kind kind);
int kind_letter(enum kind kind);
int kind_of(const char *word);
struct contract {
	char id[16];
	char *path;			/* <group>/<ID>.yaml */
	char *group;			/* "" for the root */
	char *condition_id;
	char *change_id;
	enum kind kind;
	char *creator;
	char *state;
	char *target;
	struct scope_list consumer;
	struct scope_list provider;
	struct string_list undertakes;
	struct string_list partially;
	struct string_list fulfilled;	/* parsed paths own int *source lines for the snapshot lifetime */
	struct spec *specs;
	size_t nspecs;
};
struct recorded_code {
	char mode[8];
	char type[8];
	char oid[48];
	int first;
	int last;
};
struct scoped_code {
	struct scope scope;
	struct recorded_code value;
};
struct code_list {
	struct scoped_code *items;
	size_t nr;
	size_t alloc;
};
void code_add(struct code_list *out, const struct scope *s, const struct recorded_code *value);
const struct recorded_code *code_lookup(const struct code_list *code, const struct scope *s);
void code_clear(struct code_list *code);
struct fulfilment {
	char *path;
	char *contract;
	char *condition_id;
	char *by;
	char *session;
	char *commit;
	char *outcome;
	char *body;
	struct code_list code;
};
struct branch {
	const char *name;
	char tip[48];
	struct tree *tree;
	struct string_list contracts;	/* ID -> struct contract */
	struct string_list fulfils;	/* path -> struct fulfilment */
	struct string_list errors;	/* raw path -> reason */
};
struct branch *branch_load(const char *name, const char *tip, const char *treeish);
void branch_free(struct branch *b);
struct contract *contract_get(struct branch *b, const char *id);
char *contract_path_id(const char *path);
struct contract *contract_parse(const char *text, size_t len, const char *path, int draft, char **err);
char *contract_text(struct contract *c);
struct contract *contract_new(enum kind kind, const char *group, const char *id);
enum contract_part {
	CONTRACT_SPECS = 1, CONTRACT_REQUIREMENTS = 2, CONTRACT_DETAILS = 4,
	CONTRACT_IDS = 8, CONTRACT_STATE = 16, CONTRACT_FULFILLED = 32,
	CONTRACT_CONDITIONS = CONTRACT_SPECS | CONTRACT_REQUIREMENTS,
	CONTRACT_ROWS = CONTRACT_CONDITIONS | CONTRACT_DETAILS,
	CONTRACT_ALL = CONTRACT_ROWS | CONTRACT_IDS | CONTRACT_STATE | CONTRACT_FULFILLED
};
void replace_text(char **field, const char *text);
void spec_clear(struct spec *s);
void spec_copy(struct spec *out, const struct spec *s);
void specs_clear(struct contract *c);
struct contract *contract_copy(const struct contract *c);
void contract_free(struct contract *c);
void contract_fulfilled_copy(struct contract *out, const struct contract *from);
int list_is_prefix(const struct string_list *a, const struct string_list *b);
int scopes_same(const struct scope_list *a, const struct scope_list *b);
int contract_same(const struct contract *a, const struct contract *b, unsigned parts);
void fulfilment_free(struct fulfilment *f);
char *fulfil_text(struct fulfilment *f);
char *fulfil_path(struct contract *c, const char *rid);
int group_ok(const char *group);
int worktree_path_ok(const char *p, int allow_ait);
int path_ok(const char *p);
/* contract.c: judgments (spec 1.4, 1.7, 1.8, 4.10, 5) */
enum verdict { V_FUTURE, V_CURRENT, V_UNVERIFIED, V_FULFILLED, V_CANCELLED, V_WITHDRAWN };
/* A branch at T, optionally overlaid with this request's new and changed contracts. */
struct contract_view {
	struct branch *at;
	struct string_list *changed;
};
/* where a scope is (6.7.4): found, or why not */
struct scope_location {
	const char *path;
	int first;
	int last;
};
enum scope_status {
	FOUND,
	NOBASE,
	MISSING,
	DELETED,
	ABSTRACT
};
struct scope_result {
	struct scope_location at;
	enum scope_status status;
	int exists;
};
struct contract_geometry {
	const struct contract *contract;
	struct scope_result *consumer;
	struct scope_list witnessed;	/* the provider, then the fixtures (witnessed_scopes()) */
	struct scope_result *witness;	/* where each witnessed scope is */
};
/*
 * spec 5: the one way to judge. A judgment holds a view and a tree; each
 * contract is judged once in it. MATCHES_ANY is set when C has a valid
 * fulfilment: which witnessed scopes some valid fulfilment recorded as they are.
 */
struct judgment_result {
	enum verdict verdict;
	struct scope_list witnessed;	/* the scopes CODE and MATCHES_ANY follow (witnessed_scopes()) */
	struct recorded_code *code;	/* per witnessed scope, the whole witness's code as it is in the tree */
	unsigned char *matches_any;
};
struct judgment {
	struct contract_view view;
	struct tree *tree;
	struct string_list done;
	struct string_list visiting;
};
void blob_pairs_clear(void);
char *environment_path(const char *name);
void witnessed_scopes(const struct contract *c, struct scope_list *out);
void geometry_init(struct contract_geometry *g, const struct contract *c, struct tree *t);
void geometry_clear(struct contract_geometry *g);
/* the parts of a contract geometry_text() writes (v14 3.8) */
enum geometry_part { GEOMETRY_CONSUMER, GEOMETRY_PROVIDER, GEOMETRY_FIXTURES };
char *geometry_text(const struct contract_geometry *g, enum geometry_part part, const unsigned char *matches,
		    int quote);
char *scope_result_text(const struct scope *scope, const struct scope_result *at, int quote);
void scope_result_at(const struct scope *s, struct tree *t, struct scope_result *out);
int contract_overlaps(const struct contract *c, struct tree *t, const struct scope *q, const struct scope_result *qa,
		      int depending);
void judgment_init(struct judgment *j, const struct contract_view *view, struct tree *t);
const struct judgment_result *judgment_get(struct judgment *j, struct contract *c);
void judgment_clear(struct judgment *j);
int judgment_touched(struct judgment *h, struct contract *c, struct tree *x);

struct contract *contract_lookup(const struct contract_view *v, const char *id);
int live(struct contract *c);
struct fulfilment *valid_fulfilment(struct branch *b, struct contract *c, const char *path);
int has_valid_fulfilment(struct branch *b, struct contract *c);
void undertakings(const struct contract_view *v, struct contract *c, int how, struct string_list *out);
void dependencies(const struct contract_view *v, struct contract *c, struct string_list *out);
const char *verdict_word(enum verdict v);
int is_mine(const struct contract *c);
int promise(struct contract *c, enum verdict v);
char *scope_display(const struct scope *scope, struct tree *x);
char *violation_words(struct violation *v);
void violations_clear(struct string_list *violations);
void invariants(struct branch *b, struct branch *was, const struct string_list *fulfilling, struct string_list *out);
void scope_pin(struct scope *s, struct tree *t);
void scopes_pin(struct scope_list *scopes, struct tree *t, const struct scope_list *was);
struct recorded_code code_at(struct tree *t, const struct scope_location *where);

/* contract.c: the YAML subset (v14 2.5.1) as nodes, and its writing (v14 8.2) */
struct ynode {
	char kind;			/* 'm' a map, 'l' a list, 's' a scalar, '?' an empty plain scalar */
	char style;			/* a scalar's writing: 'p' plain where it can be, '"' quoted, '|' a block */
	char *value;			/* a scalar's text */
	int line;
	size_t nr;
	struct ynode **kids;		/* a list's items; a map's keys and values, alternating (libyaml's pairs) */
};
struct ynode *yaml_parse(const char *text, size_t len, char **err);
void ynode_free(struct ynode *n);
struct ynode *yget(struct ynode *map, const char *key);
const char *ystr(struct ynode *n);
const char *ytext(struct ynode *n);
char *yaml_list(struct ynode *n, const char *name, struct string_list *out, int nonempty);
char *yaml_refusal(struct ynode *n, const char *fmt, ...) __attribute__((format (printf, 2, 3)));
char *yaml_refusal_at(int line, const char *fmt, ...) __attribute__((format (printf, 2, 3)));
char *keys_refusal(struct ynode *map, const char *const *keys, int required);
struct spec *spec_from(struct ynode *map, char **err);
void yaml_out_scalar(struct strbuf *sb, const char *s);
void yaml_out_body(struct strbuf *sb, const char *s);
void yaml_out_node(struct strbuf *sb, const char *key, struct ynode *n);
void spec_lists(struct strbuf *sb, struct spec *s);
void spec_fixtures(struct strbuf *sb, struct spec *s);

/* request.c, notify.c: the commands; notify.c: the session's tip lines */
int cmd_request(int argc, const char **argv);
int cmd_contract(int argc, const char **argv);
int cmd_fulfill(int argc, const char **argv);
int cmd_status(int argc, const char **argv);
int cmd_sub(int argc, const char **argv);
int cmd_unsub(int argc, const char **argv);
int cmd_listen(int argc, const char **argv);
void session_tips(const char *s, const char *as_of, struct string_list *tips, const char *skipped);

#endif
