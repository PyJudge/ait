/*
 * Requests (spec 6.4), the contract commands (6.5), fulfill (6.7) and the
 * check results (7): every one is one checkin of the contract branch.
 */
#include "ait.h"
#include "quote.h"
#include "lockfile.h"
#include "tempfile.h"

struct scope_input {
	struct scope_list value;
	char *error;
	const char *raw;
	int keep;
};

/* one clause of a request as given (6.4): its verb and its words */
struct clause {
	const char *verb;		/* demand, offer, drop, amend, cancel, undertake, release */
	int future;
	int partial;			/* undertake -p */
	const char *args[4];		/* its words after the verb: scopes and files, IDs */
	int arg_words[4];		/* their positions in the original command */
	int nargs;
	const char *as;
	struct scope_input scopes[2];
};

/* Inputs and values fixed once, kept across every judgment (4.4, 6.7.5). */
struct request_input {
	struct clause *clauses;		/* the clauses, read once from the words (6.4) */
	size_t nclauses;
	struct tree *x;			/* the index's tree (5.1) */
	struct tree *h;			/* fixed HEAD input */
	struct string_list given;	/* the contracts ait fulfill names */
	struct string_list made;	/* the IDs the new contracts took the first time, kept across judgments */
	struct string_list fixed;	/* step 0's covered set: ID -> its condition-id (6.7.2) */
	struct strvec words;		/* the command as given, for the hints that repeat it */
	int *word_index;		/* positional argument -> original command word */
	int force_word;
	struct string_list scopes;	/* ID -> immutable struct scope_input */
	struct string_list files;	/* path -> immutable struct spec, read once */
	struct strbuf msg;
	const char *result;
	const char *group;
	int force;
	const char *rid;			/* borrowed from the checkin */
	/* fulfilling: the code commit and its tree (6.7.5) */
	char commit[48];
	struct tree *code;
	struct string_list results;	/* task -> outcome, basis and reason, without a task line */
};

/* One judgment on T, retained through its CAS, failure hints and output. */
struct request_result {
	struct branch *at;
	struct contract_view view;	/* T with this calculation's changed contracts and new fulfilments */
	struct string_list records; /* path -> owned typed fulfilment */
	struct string_list changed;	/* ID -> struct contract: new and changed contracts */
	struct string_list order;	/* the new contracts in order: "+1", "+2", ... (7.1) */
	struct string_list alias;	/* alias -> ID */
	struct string_list fulfil;	/* the given contracts and those of the clauses */
	struct string_list reopened;	/* explicit empty amends, retained while stamping against T */
	struct string_list normal_specs;	/* specs changed by an ordinary clause, still different from T */
	struct string_list provider_words; /* ID -> borrowed position of its last explicit provider */
	struct string_list covered;	/* the covered set (6.7.2) */
};

struct request {
	struct request_input input;
	struct request_result result;
};

/* Like merge-ort's merge_start(), start each calculation with fresh work state. */
static void request_result_reset(struct request_result *r, struct branch *at)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &r->changed)
		contract_free(item->util);
	for_each_string_list_item(item, &r->records)
		fulfilment_free(item->util);
	string_list_clear(&r->records, 0);
	string_list_clear(&r->changed, 0);
	string_list_clear(&r->order, 0);
	string_list_clear(&r->alias, 1);
	string_list_clear(&r->fulfil, 0);
	string_list_clear(&r->reopened, 0);
	string_list_clear(&r->normal_specs, 0);
	string_list_clear(&r->provider_words, 0);
	string_list_clear(&r->covered, 0);
	r->at = at;
	r->view = (struct contract_view){ at, &r->changed };
}

/* Fixed input outlives retries; none of its values is borrowed by candidates. */
static void request_input_clear(struct request_input *q)
{
	struct string_list_item *item;
	size_t i, j;

	for (i = 0; i < q->nclauses; i++)
		for (j = 0; j < 2; j++) {
			scopes_clear(&q->clauses[i].scopes[j].value);
			free(q->clauses[i].scopes[j].error);
		}
	for_each_string_list_item(item, &q->scopes) {
		struct scope_input *input = item->util;

		scopes_clear(&input->value);
		free(input->error);
		free(input);
	}
	for_each_string_list_item(item, &q->files) {
		spec_clear(item->util);
		free(item->util);
	}
	string_list_clear(&q->given, 0);
	string_list_clear(&q->made, 0);
	string_list_clear(&q->fixed, 1);
	string_list_clear(&q->scopes, 0);
	string_list_clear(&q->files, 0);
	string_list_clear(&q->results, 1);
	strvec_clear(&q->words);
	strbuf_release(&q->msg);
	free(q->word_index);
	free(q->clauses);
	tree_free(q->x);
	tree_free(q->h);
	tree_free(q->code);
}

static void request_init(struct request *req, const char **argv)
{
	struct request_input *q = &req->input;
	struct request_result *r = &req->result;

	memset(req, 0, sizeof(*req));
	q->force_word = -1;
	strvec_init(&q->words);
	strvec_pushv(&q->words, argv);
	string_list_init_dup(&q->given);
	string_list_init_dup(&q->made);
	string_list_init_dup(&q->fixed);
	string_list_init_dup(&q->scopes);
	string_list_init_dup(&q->files);
	string_list_init_dup(&q->results);
	strbuf_init(&q->msg, 0);
	string_list_init_dup(&r->records);
	string_list_init_dup(&r->changed);
	string_list_init_dup(&r->order);
	string_list_init_dup(&r->alias);
	string_list_init_dup(&r->fulfil);
	string_list_init_dup(&r->reopened);
	string_list_init_dup(&r->normal_specs);
	string_list_init_dup(&r->provider_words);
	string_list_init_dup(&r->covered);
}

/* spec 7.2, 7.3: the condition-id of ID at T, which results are made for ("" for a new contract) */
static const char *condition_at_t(struct branch *at, const char *id)
{
	struct contract *c = contract_get(at, id);

	return c ? c->condition_id : "";
}

/* the contract ID as it stands after this request's changes so far */
static struct contract *contract_after(struct request_result *r, const char *id)
{
	return contract_lookup(&r->view, id);
}

/*
 * the command as it was given, for a hint (4.9): the word FROM becomes TO
 * (or goes, when TO is NULL), EXTRA is added, and a message is asked for
 * when none was given
 */
static char *command_again(struct request_input *q, int replace_word, const char *to, const char *extra)
{
	struct strbuf sb = STRBUF_INIT;
	size_t i;
	int msg = 0;

	strbuf_addstr(&sb, "ait");
	for (i = 0; i < q->words.nr; i++) {
		/* A consumed stdin or message file is represented by the text already read. */
		if (!strcmp(q->words.v[i], "-m") || !strcmp(q->words.v[i], "-F")) {
			if (!msg)
				strbuf_addf(&sb, " -m %s", quoted(q->msg.buf));
			msg = 1;
			i++;
			continue;
		}
		if ((int)i == replace_word && !to)
			continue;
		/* each word quoted only where the shell needs it, as git shows a command (quote.c) */
		strbuf_addch(&sb, ' ');
		sq_quote_buf_pretty(&sb, (int)i == replace_word ? to : q->words.v[i]);
	}
	strbuf_addstr(&sb, extra ? extra : "");
	if (!msg)
		strbuf_addstr(&sb, " -m <why>");
	return hint_command("%s", sb.buf);
}

/* The request supplies context and its retry hint; the reason supplies the words. */
static NORETURN void refuse(struct request_input *q, enum refusal why, struct contract *c, struct contract *old)
{
	const char *b = NULL, *detail = NULL, *hint = NULL;

	if (why == TARGET)
		b = c->target;
	else if (why == STATE) {
		b = old ? old->state : "absent";
		detail = c->state;
	} else if (why == FORCE_PROVIDER) {
		hint = command_again(q, q->force_word, NULL, NULL);
	}
	reject(1, hint, "%s", refusal_words(why, c->id, b, detail));
}

/* spec 1.4, 2.4: cancelled, withdrawn, or a fulfilled drop: it changes no more */
static int ended(struct contract *c)
{
	return !live(c) || (*c->id == 'X' && c->fulfilled.nr);
}

/* K, an ID or an alias of this request, of a kind in KINDS ("DOX") */
static struct contract *contract_arg(struct request_result *r, const char *k, const char *kinds)
{
	struct string_list_item *a = string_list_lookup(&r->alias, k);
	const char *id = a ? a->util : k;
	struct contract *c = contract_after(r, id);

	if (!c || !strchr(kinds, *c->id))
		no_contract(k);
	return c;
}

/* spec 3.1: a condition file's path is a repository path, and may be under .ait/ */
static void require_condition_path(const char *path)
{
	if (!worktree_path_ok(path, 1))
		reject(128, NULL, "bad path '%s'", path);
}

/* Conditions and results cross the same length-aware file/YAML boundary (6.4.2, 7.3). */
static struct ynode *read_yaml_file(const char *path, const char *what, const char *read_hint)
{
	struct strbuf sb = STRBUF_INIT;
	struct ynode *map;
	char *err;

	if (strbuf_read_file(&sb, path, 0) < 0)
		reject(128, read_hint, "could not read '%s'", what);
	if (!(map = yaml_parse(sb.buf, sb.len, &err)))
		reject(128, NULL, "bad format: '%s': %s", what, err);
	strbuf_release(&sb);
	return map;
}

/* 6.4.2, 6.7.5: overlapping FILES lists and later judgments share each path's first read. */
static const struct spec *condition_from_file(struct request_input *q, const char *path)
{
	struct string_list_item *read = string_list_lookup(&q->files, path);
	struct ynode *map;
	struct spec *s;
	char *err;

	if (read)
		return read->util;
	/* The template hint only applies to a path the template can be written at. */
	require_condition_path(path);
	map = read_yaml_file(xstrfmt("%s/%s", top, path), path,
			     hint_command("ait contract new spec %s exec|prompt", quoted(path)));
	if (!(s = spec_from(map, &err)))
		reject(128, NULL, "bad format: '%s': %s", path, err);
	ynode_free(map);
	string_list_insert(&q->files, path)->util = s;
	return s;
}

/* Repeated files count once; each candidate owns a copy of fixed input. */
static void specs_from_files(struct request_input *q, struct contract *c, const char *files)
{
	struct string_list paths = STRING_LIST_INIT_DUP, seen = STRING_LIST_INIT_DUP;
	struct string_list_item *item;

	specs_clear(c);
	string_list_split(&paths, files, ",", -1);
	for_each_string_list_item(item, &paths) {
		if (string_list_has_string(&seen, item->string))
			continue;
		string_list_insert(&seen, item->string);
		REALLOC_ARRAY(c->specs, c->nspecs + 1);
		spec_copy(&c->specs[c->nspecs++], condition_from_file(q, item->string));
	}
	string_list_clear(&paths, 0);
	string_list_clear(&seen, 0);
}

/*
 * spec 3.2: a scope argument, parsed once; "-" keeps what is there. An error
 * waits until the clause is applied, after the common and state checks.
 */
static void prepare_scopes(struct scope_input *input, const char *arg, int allow_new, int allow_abs)
{
	input->raw = arg;
	input->keep = !strcmp(arg, "-");
	if (!input->keep) {
		input->error = scopes_parse(&input->value, arg, allow_new, allow_abs, SCOPE_ARGUMENT);
		if (!input->error && !input->value.nr)
			input->error = xstrfmt("'%s' is not a scope", arg);
	}
}

static void scopes_arg(struct scope_list *into, const struct scope_input *input, struct tree *t)
{
	char *err;

	if (input->keep)
		return;
	if (input->error)
		reject(128, NULL, "bad scope: %s", input->error);
	scopes_copy(into, &input->value);
	if (scopes_pin(into, t, NULL, &err))
		reject(128, NULL, "bad scope: %s", err);
}

static struct contract *make(struct request_input *q, struct request_result *r, const char *kind)
{
	char id[16];
	struct contract *c;

	/* a judgment made again takes the same IDs, so the step-0 results and the record agree (6.7.5) */
	if (r->order.nr < q->made.nr)
		snprintf(id, sizeof(id), "%s", q->made.items[r->order.nr].string);
	else
		do
			newid(id, kind_letter(kind));
		while (contract_after(r, id));
	if (r->order.nr == q->made.nr)
		string_list_append(&q->made, id);
	c = contract_new(kind, q->group ? q->group : "", id);
	replace_text(&c->creator, agent);
	string_list_append(&r->order, id);
	return c;
}

/* spec 2.4: the drop X targets a D or O judged current */
static int target_current(struct request_result *r, struct tree *x, struct contract *drop)
{
	struct contract *t = contract_lookup(&r->view, drop->target);
	struct judgment j;
	int current;

	if (!t)
		return 0;
	/* each clause changes the view, so each judges afresh (6.4.1) */
	judgment_init(&j, &r->view, x);
	current = judgment_get(&j, t)->verdict == V_CURRENT;
	judgment_clear(&j);
	return current;
}

enum change_action {
	EDIT,
	CANCEL,
	DRAFT,
	RESCOPE,
	FULFILL,
	RECORD,
	WITHDRAW
};

static int list_is_prefix(const struct string_list *a, const struct string_list *b)
{
	size_t i;

	if (a->nr > b->nr)
		return 0;
	for (i = 0; i < a->nr; i++)
		if (strcmp(a->items[i].string, b->items[i].string))
			return 0;
	return 1;
}

/* spec 2.4: one action is judged against the contract immediately before it. */
static enum refusal change_refusal(struct request_input *q, struct request_result *r,
				   struct contract *old, struct contract *c,
				   enum change_action action, int trivial)
{
	if (old && ended(old) && !(action == CANCEL && !strcmp(old->state, "cancelled")) &&
	    !(action == DRAFT && contract_same(old, c, CONTRACT_ALL)))
		return ENDED;
	if (old && (strcmp(c->path, old->path) || strcmp(c->kind, old->kind) || strcmp(c->creator, old->creator)))
		return IDENTITY;
	if (old && !list_is_prefix(&old->fulfilled, &c->fulfilled))
		return FULFILMENT;
	if (c->fulfilled.nr > (old ? old->fulfilled.nr : 0) &&
	    !(action == RECORD && q->code && string_list_has_string(&r->covered, c->id)))
		return FULFILMENT;
	if ((!old && strcmp(c->state, "future")) ||
	    (old && strcmp(c->state, old->state) &&
	     !((action == CANCEL || action == DRAFT) && !strcmp(c->state, "cancelled")) &&
	     !(action == WITHDRAW && *c->id != 'X' && !strcmp(c->state, "withdrawn"))))
		return STATE;
	if (*c->id == 'X' && (!old || strcmp(c->target, old->target) || action == FULFILL) &&
	    !target_current(r, q->code ? q->code : q->x, c))
		return TARGET;
	if (old && trivial && !scopes_same(&c->provider, &old->provider))
		return FORCE_PROVIDER;
	return CHANGE_OK;
}

static void validate_change(struct request_input *q, struct request_result *r,
			    struct contract *old, struct contract *c,
			    enum change_action action, int trivial)
{
	enum refusal why = change_refusal(q, r, old, c, action, trivial);

	if (why)
		refuse(q, why, c, old);
}

/*
 * spec 2.3.3, C68: the IDs come from the final difference from T. A new or
 * reopened contract takes the RID for both; otherwise the condition-id when
 * its conditions differ from T and not only by trivial changes, and the
 * change-id when a row differs from T.
 */
static void publish_change(struct request_input *q, struct request_result *r,
			   struct contract *old, struct contract *c,
			   enum change_action action, int trivial)
{
	struct contract *at = contract_get(r->at, c->id);
	struct string_list_item *slot;
	int reopened, trivial_conditions;

	if (action == RESCOPE)
		string_list_insert(&r->reopened, c->id);
	if (at && contract_same(c, at, CONTRACT_SPECS))
		string_list_remove(&r->normal_specs, c->id, 0);
	else if (!trivial && old && !contract_same(c, old, CONTRACT_SPECS))
		string_list_insert(&r->normal_specs, c->id);
	reopened = !at || string_list_has_string(&r->reopened, c->id);
	trivial_conditions = (action == DRAFT && trivial) ||
		(at && !string_list_has_string(&r->normal_specs, c->id) && contract_same(c, at, CONTRACT_REQUIREMENTS));
	replace_text(&c->condition_id, reopened || (!contract_same(c, at, CONTRACT_CONDITIONS) && !trivial_conditions) ?
		     q->rid : at->condition_id);
	replace_text(&c->change_id, reopened || !contract_same(c, at, CONTRACT_ROWS) ? q->rid : at->change_id);
	slot = string_list_insert(&r->changed, c->id);
	contract_free(slot->util);
	slot->util = c;
}

/* Clauses validate immediately; staged drafts first publish the complete batch. */
static void accept_change(struct request_input *q, struct request_result *r,
			  struct contract *old, struct contract *c,
			  enum change_action action, int trivial)
{
	validate_change(q, r, old, c, action, trivial);
	publish_change(q, r, old, c, action, trivial);
}

/* spec 6.4: the clause verbs, the one list the grammar and the reserved words come from */
static const struct verb {
	const char *verb;
	int words;		/* the words it always takes */
	int future;		/* 0 never future, 1 always, 2 either */
	int fulfils;		/* without future, it fulfils (6.4.1) */
	int named;		/* it makes a contract an alias can name */
} verbs[] = {
	{ "demand", 3, 2, 1, 1 },
	{ "offer", 2, 2, 1, 1 },
	{ "drop", 1, 1, 0, 1 },
	{ "amend", 1, 2, 1, 0 },
	{ "cancel", 1, 1, 0, 0 },
	{ "undertake", 2, 0, 0, 0 },
	{ "release", 2, 0, 0, 0 },
};

static const struct verb *verb_of(const char *w)
{
	size_t v;

	for (v = 0; v < ARRAY_SIZE(verbs); v++)
		if (!strcmp(w, verbs[v].verb))
			return &verbs[v];
	return NULL;
}

/* a word that starts a clause (v14 7.3.1) */
static int clause_word(const char *w)
{
	return !strcmp(w, "future") || verb_of(w);
}

/* v14 7.3.1: an alias is a lowercase word that is no clause word, "as" or command */
static void alias_ok(const char *name, struct string_list *seen)
{
	if (clause_word(name) || !strcmp(name, "as") || !strcmp(name, "contract") || !strcmp(name, "fulfill"))
		reject(129, NULL, "'%s' is a reserved word", name);
	if (!*name || !islower(*name) || name[strspn(name, "abcdefghijklmnopqrstuvwxyz0123456789_")] || strlen(name) > 32)
		reject(129, NULL, "bad alias '%s'", name);
	if (string_list_lookup(seen, name))
		reject(129, NULL, "alias '%s' is given twice", name);
	string_list_insert(seen, name);
}

/* An earlier alias or a complete ID determines grammar without looking up a contract. */
static const struct verb *contract_kind(const char *name, struct string_list *aliases)
{
	struct string_list_item *alias = string_list_lookup(aliases, name);
	size_t v;

	if (alias)
		return alias->util;
	if (id_ok(name, "DOX"))
		for (v = 0; v < ARRAY_SIZE(verbs); v++)
			if (verbs[v].named && kind_letter(verbs[v].verb) == *name)
				return &verbs[v];
	return NULL;
}

/* the next word of a clause; only amend's "-" starts with '-' (v14 7.3.1) */
static const char *word(int argc, const char **argv, int *i, int keep)
{
	if (*i >= argc || (*argv[*i] == '-' && (!keep || strcmp(argv[*i], "-"))))
		reject(129, NULL, "a clause is cut short");
	return argv[(*i)++];
}

/*
 * spec 6.4: the clauses of ARGV, read once and without the branch, so
 * whether the request fulfils is known before it is judged (4.3-5);
 * amend uses the kind carried by an ID or an earlier alias
 */
static int read_clauses(struct request_input *q, int argc, const char **argv)
{
	struct string_list aliases = STRING_LIST_INIT_DUP;
	int i = 0, fulfils = 0;

	while (i < argc) {
		struct clause *cl;
		const char *w = argv[i++];
		const struct verb *v;
		int future = !strcmp(w, "future");

		/* options() leaves only -p and --partial here, and they belong right after undertake */
		if (*w == '-')
			reject(129, NULL, "'%s' goes right after 'undertake'", w);
		if (future)
			w = word(argc, argv, &i, 0);
		if (!(v = verb_of(w)) || (v->future != 2 && v->future != future))
			reject(129, NULL, "unknown clause '%s%s'", future ? "future " : "", w);
		REALLOC_ARRAY(q->clauses, q->nclauses + 1);
		cl = &q->clauses[q->nclauses++];
		memset(cl, 0, sizeof(*cl));
		cl->verb = v->verb;
		cl->future = future;
		if (!strcmp(w, "undertake") && i < argc && (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--partial"))) {
			cl->partial = 1;
			i++;
		}
		while (cl->nargs < v->words) {
			int at = i;
			const char *value = word(argc, argv, &i, 0);

			cl->arg_words[cl->nargs] = q->word_index[at];
			cl->args[cl->nargs++] = value;
		}
		/* v14 7.3.1: amend K takes up to three more words; none is the empty amend */
		while (!strcmp(w, "amend") && i < argc && cl->nargs < 4 && !clause_word(argv[i])) {
			int at = i;
			const char *value = word(argc, argv, &i, 1);

			cl->arg_words[cl->nargs] = q->word_index[at];
			cl->args[cl->nargs++] = value;
		}
		if (!strcmp(w, "amend")) {
			const struct verb *kind = contract_kind(cl->args[0], &aliases);

			if (kind && cl->nargs > 1 && cl->nargs - 1 != kind->words)
				reject(129, NULL, "amend '%s' takes %s", cl->args[0],
				       !strcmp(kind->verb, "demand") ? "A B FILES" :
				       !strcmp(kind->verb, "offer") ? "B FILES" : "the target");
		}
		if (v->named && i + 1 < argc && !strcmp(argv[i], "as")) {
			alias_ok(cl->as = argv[i + 1], &aliases);
			string_list_lookup(&aliases, cl->as)->util = (void *)v;
			i += 2;
		}
		if (!strcmp(w, "demand")) {
			prepare_scopes(&cl->scopes[0], cl->args[0], 1, 0);
			prepare_scopes(&cl->scopes[1], cl->args[1], 1, 1);
		} else if (!strcmp(w, "offer")) {
			prepare_scopes(&cl->scopes[1], cl->args[0], 1, 1);
		} else if (!strcmp(w, "amend") && cl->nargs > 2) {
			if (cl->nargs == 4)
				prepare_scopes(&cl->scopes[0], cl->args[1], 1, 0);
			prepare_scopes(&cl->scopes[1], cl->args[cl->nargs - 2], 1, 1);
		}
		fulfils |= v->fulfils && !future;
	}
	return fulfils;
}

/* L without ID, in its order */
static void remove_id(struct string_list *l, const char *id)
{
	size_t i, j = 0;

	for (i = 0; i < l->nr; i++)
		if (strcmp(l->items[i].string, id))
			l->items[j++] = l->items[i];
	l->nr = j;
}

/* spec 6.4.1: the clauses applied in order to the branch at T */
static void apply_clauses(struct request_input *q, struct request_result *r)
{
	struct tree *h = q->h;
	size_t n;

	for (n = 0; n < q->nclauses; n++) {
		struct clause *cl = &q->clauses[n];
		struct tree *pin_at = cl->future ? h : q->x;
		const char **a = cl->args;
		struct contract *c, *old = NULL, *d;
		enum change_action action = EDIT;
		int trivial = 0;

		if (!strcmp(cl->verb, "demand") || !strcmp(cl->verb, "offer")) {
			int demand = !strcmp(cl->verb, "demand");

			c = make(q, r, cl->verb);
			if (demand)
				scopes_arg(&c->consumer, &cl->scopes[0], pin_at);
			scopes_arg(&c->provider, &cl->scopes[1], pin_at);
			string_list_insert(&r->provider_words, c->id)->util = &cl->arg_words[demand];
			specs_from_files(q, c, a[demand + 1]);
		} else if (!strcmp(cl->verb, "drop")) {
			d = contract_arg(r, a[0], "DO");
			c = make(q, r, "drop");
			c->target = xstrdup(d->id);
		} else if (!strcmp(cl->verb, "amend")) {
			int more = cl->nargs - 1;

			old = contract_arg(r, a[0], "DOX");
			c = contract_copy(old);
			if (*c->id == 'X' && more) {
				if (strcmp(a[1], "-"))
					replace_text(&c->target, contract_arg(r, a[1], "DO")->id);
			} else if (more) {
				if (*c->id == 'D')
					scopes_arg(&c->consumer, &cl->scopes[0], pin_at);
				scopes_arg(&c->provider, &cl->scopes[1], pin_at);
				if (strcmp(a[more - 1], "-"))
					string_list_insert(&r->provider_words, c->id)->util = &cl->arg_words[more - 1];
				if (strcmp(a[more], "-"))
					specs_from_files(q, c, a[more]);
			}
			trivial = cl->future && q->force;
			/* an empty amend explicitly reopens, even if later rows return to T */
			if (!more)
				string_list_insert(&r->reopened, c->id);
		} else if (!strcmp(cl->verb, "cancel")) {
			old = contract_arg(r, a[0], "DOX");
			c = contract_copy(old);
			replace_text(&c->state, "cancelled");
			action = CANCEL;
		} else {
			d = contract_arg(r, a[0], "D");
			old = contract_arg(r, a[1], "O");
			if (!strcmp(cl->verb, "undertake"))
				validate_change(q, r, d, d, EDIT, 0);
			c = contract_copy(old);
			remove_id(&c->undertakes, d->id);
			remove_id(&c->partially, d->id);
			if (!strcmp(cl->verb, "undertake"))
				string_list_append(cl->partial ? &c->partially : &c->undertakes, d->id);
		}
		accept_change(q, r, old, c, action, trivial);
		if (cl->as)
			string_list_insert(&r->alias, cl->as)->util = xstrdup(c->id);
		if (!cl->future && (!strcmp(cl->verb, "demand") || !strcmp(cl->verb, "offer") || !strcmp(cl->verb, "amend")))
			string_list_insert(&r->fulfil, c->id);
	}
}

/* An alias in the original command will name its new contract again on replay. */
static const char *request_name(struct request_result *r, const char *id)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &r->alias)
		if (!strcmp(item->util, id))
			return item->string;
	return id;
}

/* spec 4.10: repair the initial violations and any pairs the repair would reopen. */
static char *violation_hint(struct checkin *ck, struct string_list *violations)
{
	struct request *req = ck->data;
	struct request_input *q = &req->input;
	struct request_result *r = &req->result;
	struct string_list fixes = STRING_LIST_INIT_DUP, reopened = STRING_LIST_INIT_DUP;
	struct strvec pending = STRVEC_INIT;
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item, *o;
	char *hint = NULL;
	size_t i;

	for_each_string_list_item(item, violations) {
		struct violation *v = item->util;

		if (v->kind == PAIR) {
			strvec_push(&pending, v->a);
		} else {
			struct string_list opposite = STRING_LIST_INIT_DUP;

			undertakings(&r->view, contract_after(r, v->a), v->kind == MIX_WHOLLY ? 'w' : 'p', &opposite);
			for_each_string_list_item(o, &opposite) {
				struct contract *c = o->util;

				/* Rule 7 restricts advice, not the user's explicit release commands. */
				if (strcmp(c->creator, agent)) {
					string_list_clear(&opposite, 0);
					goto done;
				}
				string_list_insert(&fixes, xstrfmt("release %s %s", request_name(r, v->a),
							 request_name(r, o->string)));
				if (v->kind == MIX_WHOLLY && (has_valid_fulfilment(r->at, c) ||
				    string_list_has_string(&r->covered, c->id)))
					strvec_push(&pending, c->id);
			}
			string_list_clear(&opposite, 0);
		}
	}
	for (i = 0; i < pending.nr; i++) {
		struct contract *c = contract_after(r, pending.v[i]);
		struct string_list dependants = STRING_LIST_INIT_DUP;

		if (string_list_has_string(&reopened, c->id))
			continue;
		/* Reopening cannot undo a scheduled fulfilment; =SCOPES gets a future-only preparation. */
		if (!q->scopes.nr && string_list_has_string(&r->covered, c->id))
			goto done;
		string_list_insert(&reopened, c->id);
		string_list_insert(&fixes, xstrfmt("future amend %s", request_name(r, c->id)));
		undertakings(&r->view, c, !strcmp(c->kind, "demand") ? 'w' : 'p', &dependants);
		for_each_string_list_item(o, &dependants)
			if (has_valid_fulfilment(r->at, o->util) || string_list_has_string(&r->covered, o->string))
				strvec_push(&pending, o->string);
		string_list_clear(&dependants, 0);
	}
	/* fulfill accepts IDs, not repair clauses. */
	if (!q->scopes.nr && !strcmp(q->words.v[0], "fulfill"))
		goto done;
	if (q->scopes.nr) {
		strbuf_addstr(&sb, "ait");
		for_each_string_list_item(item, &q->scopes) {
			struct contract *c = contract_after(r, item->string);

			strbuf_addf(&sb, " future amend %s%s %s -", c->id,
				    !strcmp(c->kind, "demand") ? " -" : "", quoted(((struct scope_input *)item->util)->raw));
		}
	}
	for_each_string_list_item(item, &fixes)
		strbuf_addf(&sb, " %s", item->string);
	if (q->scopes.nr) {
		strbuf_addf(&sb, " -m %s", quoted(q->msg.buf));
		hint = hint_command("%s", sb.buf);
	} else {
		hint = command_again(q, -1, NULL, sb.buf);
	}
done:
	strvec_clear(&pending);
	string_list_clear(&reopened, 0);
	string_list_clear(&fixes, 0);
	strbuf_release(&sb);
	return hint;
}

/* spec 6.7.2: the covered set of K, judged in J */
static void cover(struct request_result *r, struct judgment *j, struct contract *k, struct string_list *out)
{
	struct string_list needs = STRING_LIST_INIT_DUP;
	struct string_list_item *item;

	string_list_insert(out, k->id);
	dependencies(&r->view, k, &needs);
	for_each_string_list_item(item, &needs)
		if (!strcmp(k->kind, "offer") || judgment_get(j, item->util)->verdict != V_CURRENT)
			string_list_insert(out, item->string);
	string_list_clear(&needs, 0);
}

static char *task_of(const char *id, size_t n)
{
	return xstrfmt("%s:%d", id, (int)n + 1);
}

/* the name a contract has in the tasks: "+N" for one this request makes (7.1) */
static const char *task_id(struct request_result *r, const char *id)
{
	size_t i;

	for (i = 0; i < r->order.nr; i++)
		if (!strcmp(r->order.items[i].string, id))
			return xstrfmt("+%d", (int)i + 1);
	return id;
}

/* v14 2.5.3: the schema of an environment definition; NULL, or why not */
static char *environment_refusal(struct ynode *map)
{
	static const char *const keys[] = { "cwd", "timeout_ms", "env", "prepare", "cleanup", NULL };
	static const char *const env_keys[] = { "inherit", "set", "required", NULL };
	static const char *const commands[] = { "prepare", "cleanup" };
	struct ynode *n, *env = yget(map, "env");
	size_t i, j;
	char *why;

	if ((why = keys_refusal(map, keys, 0)))
		return why;
	if ((n = yget(map, "cwd")) && n->kind != 's')
		return yaml_refusal(n, "'cwd' is not a string");
	if ((n = yget(map, "timeout_ms")) && (!ystr(n) || !*n->value || n->value[strspn(n->value, "0123456789")]))
		return yaml_refusal(n, "'timeout_ms' is not a decimal integer");
	if (env && env->kind != 'm')
		return yaml_refusal(env, "'env' is not a map");
	if (env && (why = keys_refusal(env, env_keys, 0)))
		return why;
	if ((n = yget(env, "inherit")) && (why = yaml_list(n, "inherit", NULL, 0)))
		return why;
	if ((n = yget(env, "required")) && (why = yaml_list(n, "required", NULL, 0)))
		return why;
	if ((n = yget(env, "set")) && n->kind != 'm')
		return yaml_refusal(n, "'set' is not a map of strings");
	for (i = 0; n && i < n->nr; i += 2)
		if (!ystr(n->kids[i + 1]))
			return yaml_refusal(n->kids[i + 1], "'%s' is not a string", n->kids[i]->value);
	for (j = 0; j < ARRAY_SIZE(commands); j++) {
		if (!(n = yget(map, commands[j])))
			continue;
		if (n->kind != 'l')
			return yaml_refusal(n, "'%s' is not a list of argv lists", commands[j]);
		for (i = 0; i < n->nr; i++)
			if ((why = yaml_list(n->kids[i], commands[j], NULL, 1)))
				return why;
	}
	return NULL;
}

/* v14 2.5.3 types the values: every one a string, but timeout_ms a number (keys stay as written) */
static void environment_types(struct ynode *n)
{
	size_t i;

	if (n->kind == 's' || n->kind == '?')
		n->style = '"';
	for (i = n->kind == 'm'; i < n->nr; i += n->kind == 'm' ? 2 : 1)
		environment_types(n->kids[i]);
}

/* spec 7.2 and v14 2.5.3: HEAD's environment NAME, checked, written with its schema's types */
static char *environment_out(struct strbuf *sb, const char *name, struct tree *h)
{
	char *path = xstrfmt("specs/environments/%s.yaml", name), *text, *err = NULL;
	const char *oid = head ? blob_at(h, path) : NULL;
	struct ynode *map;
	size_t len;

	if (!oid || !(text = object(oid, 1, NULL, &len)))
		return xstrfmt("could not read '%s' at HEAD", path);
	if ((map = yaml_parse(text, len, &err)) && !(err = environment_refusal(map))) {
		environment_types(map);
		if (yget(map, "timeout_ms"))
			yget(map, "timeout_ms")->style = 'p';
		yaml_out_node(sb, "environment", map);
	}
	ynode_free(map);
	free(text);
	return err ? xstrfmt("bad format: '%s': %s", path, err) : NULL;
}

/* spec 7.3: the existing covered contracts have exactly these condition-ids at T. */
static int conditions_changed(struct request_result *r, struct ynode *ids)
{
	struct string_list_item *item;
	size_t i;

	for (i = 0; i < ids->nr; i += 2) {
		const char *id = ids->kids[i]->value;

		if (!string_list_has_string(&r->covered, id) || strcmp(task_id(r, id), id))
			return 1;
	}
	for_each_string_list_item(item, &r->covered) {
		const char *got = ystr(yget(ids, item->string));

		if (!strcmp(task_id(r, item->string), item->string) &&
		    (!got || strcmp(got, condition_at_t(r->at, item->string))))
			return 1;
	}
	return 0;
}

/* spec 7.4: map order is irrelevant, but task positions remain part of the template identity. */
static int template_same(struct request_result *r, struct ynode *map)
{
	struct ynode *ids = yget(map, "condition-ids"), *results = yget(map, "results");
	struct string_list_item *item;
	size_t n, position = 0;

	if (!ids || ids->kind != 'm' || conditions_changed(r, ids) || !results || results->kind != 'l')
		return 0;
	for_each_string_list_item(item, &r->covered) {
		struct contract *c = contract_after(r, item->string);

		for (n = 0; n < c->nspecs; n++) {
			const char *task = position < results->nr ? ystr(yget(results->kids[position], "task")) : NULL;

			if (!task || strcmp(task, task_of(task_id(r, c->id), n)))
				return 0;
			position++;
		}
	}
	return position == results->nr;
}

/* spec 7.2: the check request and the result template, unless the template is already this one */
static char *write_check_files(struct request_input *q, struct request_result *r, struct ynode *input)
{
	struct strbuf req = STRBUF_INIT, res = STRBUF_INIT, ids = STRBUF_INIT, old = STRBUF_INIT;
	struct strbuf task_lines = STRBUF_INIT;
	struct string_list environments = STRING_LIST_INIT_DUP;
	struct ynode *existing = NULL;
	struct string_list_item *item;
	char *dir = worktree_state_dir(), *rq = xstrfmt("%s/check-request.yaml", dir),
	     *rs = xstrfmt("%s/check-result.yaml", dir), *err;
	struct stat supplied, stored;
	size_t n;
	int tasks = 0;

	for_each_string_list_item(item, &r->covered)
		if (!strcmp(task_id(r, item->string), item->string))
			strbuf_addf(&ids, "  %s: \"%s\"\n", item->string, condition_at_t(r->at, item->string));
	strbuf_addf(&req, "condition-ids:%s\n%stree: \"%s\"\ntasks:\n", ids.len ? "" : " {}", ids.buf, q->x->oid);
	strbuf_addf(&res, "condition-ids:%s\n%sresults:\n", ids.len ? "" : " {}", ids.buf);
	for_each_string_list_item(item, &r->covered) {
		struct contract *c = contract_after(r, item->string);
		struct contract_geometry g;
		char *consumer, *provider;

		geometry_init(&g, c, q->x);
		consumer = geometry_text(&g, 0, NULL);
		provider = geometry_text(&g, 1, NULL);
		geometry_clear(&g);
		for (n = 0; n < c->nspecs; n++) {
			struct spec *s = &c->specs[n];
			char *task = task_of(task_id(r, c->id), n);

			strbuf_addf(&task_lines, "task %s\n", task);
			tasks++;
			strbuf_addf(&req, "- task: \"%s\"\n  contract: \"%s\"\n  kind: \"%s\"\n", task, task_id(r, c->id), c->kind);
			if (!strcmp(c->kind, "demand")) {
				strbuf_addstr(&req, "  consumer: ");
				yaml_out_scalar(&req, consumer);
				strbuf_addch(&req, '\n');
			}
			strbuf_addstr(&req, "  provider: ");
			yaml_out_scalar(&req, provider);
			strbuf_addf(&req, "\n  engine: \"%s\"\n  body: ", s->engine ? "exec" : "prompt");
			yaml_out_body(&req, s->body);
			if (s->engine)
				spec_lists(&req, s);
			if (s->environment) {
				struct string_list_item *env = string_list_lookup(&environments, s->environment);

				if (!env) {
					struct strbuf text = STRBUF_INIT;

					if ((err = environment_out(&text, s->environment, q->h)))
						return err;
					env = string_list_insert(&environments, s->environment);
					env->util = strbuf_detach(&text, NULL);
				}
				strbuf_addstr(&req, env->util);
			}
			strbuf_addf(&res, "- task: \"%s\"\n  outcome:\n  basis: |\n  reason:\n", task);
		}
		free(consumer);
		free(provider);
	}
	if (!tasks)
		return NULL;
	write_state(rq, req.buf);
	/* 7.4: a template with these condition-ids and tasks stays, filled or not */
	if (input && !stat(q->result, &supplied) && !stat(rs, &stored) &&
	    supplied.st_dev == stored.st_dev && supplied.st_ino == stored.st_ino)
		existing = input;
	else if (strbuf_read_file(&old, rs, 0) >= 0)
		existing = yaml_parse(old.buf, old.len, &err);
	if (!existing || !template_same(r, existing))
		write_state(rs, res.buf);
	fputs(task_lines.buf, stdout);
	printf("request %s\nresult %s\n", rq, rs);
	reject(1, xstrfmt("check each task, fill '%s', and rerun with -r %s\n"
			  "if it is trivial, add --force instead (recorded as forced)", rs, quoted(rs)),
	       "check results required: %d task(s)", tasks);
}

static NORETURN void result_bad(struct request_input *q, const char *why)
{
	reject(128, NULL, "bad format: '%s': %s", q->result, why);
}

/* spec 7.3, v14 8.4: read once, then compare input identity before providers or task contents. */
static int read_result_input(struct request_input *q, struct request_result *r, struct ynode **map)
{
	struct ynode *ids;

	*map = read_yaml_file(q->result, q->result, NULL);
	if (!(ids = yget(*map, "condition-ids")) || ids->kind != 'm')
		result_bad(q, yaml_refusal(ids ? ids : *map, "'condition-ids' is missing or not a map"));
	return conditions_changed(r, ids);
}

/* v14 8.4: after matching input and located providers, retain each task's accepted result. */
static void check_results(struct request_input *q, struct request_result *r, struct ynode *map)
{
	static const char *const keys[] = { "condition-ids", "results", NULL },
		*const fields[] = { "task", "outcome", "basis", "reason", NULL };
	struct string_list want = STRING_LIST_INIT_DUP, failed = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct ynode *results = yget(map, "results");
	char *err;
	size_t i, n;

	for_each_string_list_item(item, &r->covered) {
		struct contract *c = contract_after(r, item->string);

		for (n = 0; n < c->nspecs; n++)
			string_list_insert(&want, task_of(task_id(r, c->id), n));
	}
	if ((err = keys_refusal(map, keys, 0)))
		result_bad(q, err);
	if (!results || results->kind != 'l')
		result_bad(q, yaml_refusal(results ? results : map, "'results' is missing or not a list"));
	for (i = 0; i < results->nr; i++) {
		struct ynode *result = results->kids[i];
		const char *task = ystr(yget(result, "task")), *outcome = ystr(yget(result, "outcome")),
			   *basis = ystr(yget(result, "basis")), *reason = ystr(yget(result, "reason"));
		struct strbuf block = STRBUF_INIT;

		if (result->kind != 'm')
			result_bad(q, yaml_refusal(result, "a result is not a map"));
		if ((err = keys_refusal(result, fields, 0)))
			result_bad(q, err);
		for (n = 0; n + 1 < result->nr; n += 2)
			if (!ytext(result->kids[n + 1]))
				result_bad(q, yaml_refusal(result->kids[n + 1], "'%s' is not a scalar", result->kids[n]->value));
		if (!task || !string_list_has_string(&want, task) || string_list_has_string(&q->results, task))
			result_bad(q, yaml_refusal(yget(result, "task") ? yget(result, "task") : result,
						   "task '%s' is unknown or given twice", task ? task : ""));
		if (!outcome || (strcmp(outcome, "pass") && strcmp(outcome, "fail") && strcmp(outcome, "unknown")))
			result_bad(q, yaml_refusal(yget(result, "outcome") ? yget(result, "outcome") : result,
						   "task '%s' has no valid outcome", task));
		if (!strcmp(outcome, "pass") && (!basis || !*basis))
			result_bad(q, yaml_refusal(yget(result, "basis") ? yget(result, "basis") : result,
						   "task '%s' has no basis", task));
		if (strcmp(outcome, "pass"))
			string_list_append(&failed, task);
		strbuf_addf(&block, "  outcome: %s\n  basis: ", outcome);
		yaml_out_body(&block, basis ? basis : "");
		if (reason && *reason) {
			strbuf_addstr(&block, "  reason: ");
			yaml_out_body(&block, reason);
		}
		string_list_insert(&q->results, task)->util = strbuf_detach(&block, NULL);
	}
	for_each_string_list_item(item, &want)
		if (!string_list_has_string(&q->results, item->string))
			result_bad(q, yaml_refusal(results, "task '%s' is missing", item->string));
	if (failed.nr) {
		for_each_string_list_item(item, &failed)
			fprintf(stderr, "%s\n", item->string);
		reject(1, NULL, "check failed: %d task(s)", (int)failed.nr);
	}
}

/* 3.3: repair a parsed provider argument, or name an implicitly covered contract. */
static char *concrete_hint(struct request_input *q, struct request_result *r, const struct contract *c)
{
	struct string_list_item *at;
	char *narrowed = xstrfmt("%s=<scope>", c->id);

	if (!strcmp(q->words.v[0], "fulfill")) {
		at = string_list_lookup(&q->given, c->id);
		return at ? command_again(q, *(int *)at->util, narrowed, NULL) :
			command_again(q, -1, NULL, xstrfmt(" %s", quoted(narrowed)));
	}
	at = string_list_lookup(&r->provider_words, c->id);
	if (at)
		return command_again(q, *(int *)at->util, "<scope>", NULL);
	if (contract_get(r->at, c->id))
		return command_again(q, -1, NULL, xstrfmt(" amend %s %s%s -", c->id,
				     *c->id == 'D' ? "- " : "", quoted("<scope>")));
	return NULL;
}

/* spec 6.7.4: every provider scope of C is found in T; the refusal names what is not */
static char *locate_all(struct request_input *q, struct request_result *r, const struct contract_geometry *g,
		       char **hint)
{
	const struct contract *c = g->contract;
	size_t i;

	*hint = NULL;
	for (i = 0; i < c->provider.nr; i++) {
		const struct scope *item = &c->provider.items[i];

		switch (g->provider[i].status) {
		case FOUND:
			break;
		case ABSTRACT:
			/* 3.3: the same command, the contract narrowed */
			*hint = concrete_hint(q, r, c);
			return xstrfmt("bad scope: '%s' needs concrete scopes to be fulfilled", c->id);
		case NOBASE:
			/* the provider given again at this code takes new base blobs */
			*hint = hint_command("ait future amend %s %s%s - -m <why>", c->id, strcmp(c->kind, "demand") ? "" : "- ",
					     quoted(scopes_text(&c->provider, 0)));
			return xstrfmt("repository lacks the base blob of '%s' ('%s')", c->id, item->path);
		default: {
			char *text = scope_text(item, 1);
			char *error = xstrfmt("bad scope: '%s' of '%s' is not in the code", text, c->id);

			free(text);
			return error;
		}
		}
	}
	return NULL;
}

/* the names of IDs as messages give them: 'A', 'B' */
static char *contract_names(struct string_list *ids)
{
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;

	for_each_string_list_item(item, ids)
		strbuf_addf(&sb, "%s'%s'", sb.len ? ", " : "", item->string);
	return strbuf_detach(&sb, NULL);
}

/* the fulfilments of the covered set (spec 6.7.5-2) into C's files, from the code commit */
static void record(struct request_input *q, struct request_result *r)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &r->covered) {
		struct contract *old = contract_after(r, item->string), *c = contract_copy(old);
		struct fulfilment *f = xcalloc(1, sizeof(*f));
		struct contract_geometry geometry;
		struct strbuf body = STRBUF_INIT;
		char *err, *hint;
		size_t n;

		f->path = fulfil_path(c, q->rid);
		f->contract = xstrdup(c->id);
		f->condition_id = xstrdup(c->condition_id);
		f->by = xstrdup(agent);
		f->session = xstrdup(sid);
		f->commit = xstrdup(q->commit);
		f->outcome = xstrdup(!strcmp(c->kind, "drop") ? "withdrawn" : q->force ? "forced" : "pass");
		geometry_init(&geometry, c, q->code);
		if ((err = locate_all(q, r, &geometry, &hint)))
			reject(128, hint, "%s", err);
		for (n = 0; n < c->provider.nr; n++) {
			struct recorded_code value = code_at(q->code, &geometry.provider[n].at);

			code_add(&f->code, &c->provider.items[n], &value);
		}
		geometry_clear(&geometry);
		for (n = 0; n < c->nspecs && !q->force; n++) {
			struct string_list_item *result = string_list_lookup(&q->results, task_of(task_id(r, c->id), n));

			/* v14 8.4: a new contract's "+n" becomes its ID */
			if (result)
				strbuf_addf(&body, "- task: %s:%d\n%s", c->id, (int)n + 1, (char *)result->util);
		}
		if (q->force || !body.len)
			strbuf_add(&body, q->msg.buf, q->msg.len);
		f->body = strbuf_detach(&body, NULL);
		string_list_append(&c->fulfilled, f->path);
		accept_change(q, r, old, c, RECORD, 0);
		string_list_insert(&r->records, f->path)->util = f;
	}
}

/* The validated candidates go into the checkin as their contract files. */
static void settle(struct request_result *r, struct checkin *ck)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &r->records)
		string_list_insert(&ck->put, item->string)->util = fulfil_text(item->util);
	for_each_string_list_item(item, &r->changed)
		string_list_insert(&ck->put, ((struct contract *)item->util)->path)->util = contract_text(item->util);
}

/* the judge of a request's checkin: everything again on the tip (spec 4.4-1) */
static void request_judge(struct checkin *ck)
{
	struct request *req = ck->data;
	struct request_input *q = &req->input;
	struct request_result *r = &req->result;
	struct string_list targets = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct judgment j;

	request_result_reset(r, ck->at);
	/* 2.1.3, 6.7.2: a retry keeps its new IDs, but cannot take an ID another checkin made. */
	for_each_string_list_item(item, &q->made)
		if (contract_get(r->at, item->string))
			checkin_moved(ck);
	/* the contracts fulfill names exist before anything is done to them */
	for_each_string_list_item(item, &q->given) {
		if (!contract_after(r, item->string))
			no_contract(item->string);
		string_list_insert(&r->fulfil, item->string);
	}
	apply_clauses(q, r);
	for_each_string_list_item(item, &q->scopes) {
		struct contract *old = contract_after(r, item->string), *c = contract_copy(old);

		scopes_arg(&c->provider, item->util, q->x);
		accept_change(q, r, old, c, RESCOPE, 0);
	}
	/* the clauses are applied: one view, one judgment in X for every covered set */
	judgment_init(&j, &r->view, q->x);
	for_each_string_list_item(item, &r->fulfil)
		cover(r, &j, contract_after(r, item->string), &r->covered);
	judgment_clear(&j);
	for_each_string_list_item(item, &r->covered) {
		struct contract *c = contract_after(r, item->string);

		validate_change(q, r, c, c, FULFILL, 0);
	}
	/* 6.7.2: the covered set fixed at step 0, with the condition-ids its results are for, holds to the end */
	for_each_string_list_item(item, &r->covered)
		if (q->fixed.nr && (!string_list_lookup(&q->fixed, item->string) ||
				    strcmp(string_list_lookup(&q->fixed, item->string)->util, condition_at_t(r->at, item->string))))
			checkin_moved(ck);
	if (q->fixed.nr && q->fixed.nr != r->covered.nr)
		checkin_moved(ck);
	if (q->code)
		record(q, r);
	/* A drop's final state effect is present at step zero, before 4.10 too. */
	for_each_string_list_item(item, &r->covered) {
		struct contract *c = contract_after(r, item->string);

		if (*c->id == 'X')
			string_list_insert(&targets, c->target);
	}
	for_each_string_list_item(item, &targets) {
		struct contract *old = contract_after(r, item->string), *c = contract_copy(old);

		replace_text(&c->state, "withdrawn");
		accept_change(q, r, old, c, WITHDRAW, 0);
	}
	string_list_clear(&targets, 0);
	ck->fulfilling = !q->code && !q->scopes.nr ? &r->covered : NULL;
	settle(r, ck);
}

/* spec 6.7.5-3: ait fulfill as given, when the covered contracts are as they were at OLD */
static char *retry(struct checkin *ck, const char *old)
{
	struct request *req = ck->data;
	struct request_input *q = &req->input;
	struct request_result *r = &req->result;
	struct strvec args = STRVEC_INIT;
	struct string_list_item *item;
	int rc;

	/* a request's clauses are not made again by ait fulfill */
	if (strcmp(q->words.v[0], "fulfill"))
		return NULL;
	strvec_pushl(&args, "diff-tree", "--quiet", old, ck->tip, "--", NULL);
	for_each_string_list_item(item, &r->covered)
		strvec_push(&args, contract_get(r->at, item->string)->path);
	git_args(&args, NULL, NULL, &rc);
	return rc ? NULL : command_again(q, -1, NULL, NULL);
}

/* Writers retain the selection's name; their checkin observes its own T. */
static char *start_name(int how)
{
	struct branch *b = start(how);
	char *name = xstrdup(b->name);

	branch_free(b);
	return name;
}

/* spec 6.7.5: judge first, commit the staged code, then check in */
static void run_request(struct request *req, int fulfilling)
{
	struct checkin ck = { .msg = NULL };
	struct request_input *q = &req->input;
	struct request_result *r = &req->result;
	struct ynode *result_input = NULL;
	struct string_list_item *item;
	char *summary, *scope_error = NULL, *scope_hint = NULL, *err;
	int rc, stale = 0;

	ck.msg = request_message(q->msg.buf, fulfilling);
	ck.branch = start_name(START_SESSION | START_WRITES | START_TRAILERS | (fulfilling ? START_FULFILS : 0));
	newid(ck.rid, 'R');
	q->x = index_tree();
	q->h = tree_load(head ? head : EMPTY_TREE);
	string_list_init_dup(&ck.put);
	ck.violation_hint = violation_hint;
	ck.retry_hint = retry;
	ck.data = req;
	q->rid = ck.rid;
	if (fulfilling) {
		/* step 0: everything judged on T as a checkin judges it (4.10 too), nothing changed */
		checkin_tip(&ck);
		checkin_judge(&ck, request_judge);
		if (q->result)
			stale = read_result_input(q, r, &result_input);
		for_each_string_list_item(item, &r->covered) {
			struct contract_geometry g;

			geometry_init(&g, contract_after(r, item->string), q->x);
			scope_error = locate_all(q, r, &g, &scope_hint);
			geometry_clear(&g);
			if (scope_error)
				break;
		}
		if (stale) {
			/* 7.3 wins; v14 8.4 only emits a template when its code and environment are available. */
			if (!scope_error)
				write_check_files(q, r, result_input);
			reject(1, NULL, "check results required: condition-ids differ");
		}
		if (scope_error)
			reject(128, scope_hint, "%s", scope_error);
		if (!q->result && !q->force && (err = write_check_files(q, r, NULL)))
			reject(128, NULL, "%s", err);
		if (q->result)
			check_results(q, r, result_input);
		for_each_string_list_item(item, &r->covered)
			string_list_insert(&q->fixed, item->string)->util = xstrdup(condition_at_t(r->at, item->string));
		/* step 1: the staged code, committed by git */
		git_run(NULL, NULL, &rc, "diff", "--cached", "--quiet");
		if (rc) {
			if (git_commit(q->msg.buf))
				reject(1, NULL, "commit failed; %s not fulfilled", contract_names(&r->covered));
			not_recorded = xstrfmt("committed '%s' but %s not fulfilled",
					       line1(git("rev-parse", "--short", "HEAD")), contract_names(&r->covered));
		} else {
			warning("nothing to commit; recording the code at HEAD ('%s') for %s",
				line1(git("rev-parse", "--short", "HEAD")), contract_names(&r->covered));
			not_recorded = xstrfmt("%s not fulfilled", contract_names(&r->covered));
		}
		snprintf(q->commit, sizeof(q->commit), "%s", line1(git("rev-parse", "HEAD")));
		q->code = tree_load(q->commit);
	}
	summary = checkin_run(&ck, request_judge);
	not_recorded = NULL;
	for_each_string_list_item(item, &r->order) {
		struct string_list_item *a;
		const char *name = "";

		for_each_string_list_item(a, &r->alias)
			if (!strcmp(a->util, item->string))
				name = a->string;
		printf("new %s%s%s\n", item->string, *name ? " " : "", name);
	}
	for_each_string_list_item(item, &r->covered) {
		printf("fulfilled %s\n", item->string);
		if (*item->string == 'X')
			printf("withdrawn %s\n", contract_after(r, item->string)->target);
	}
	checkin_report(summary);
	request_result_reset(r, NULL);
	request_input_clear(q);
	checkin_clear(&ck);
	free((char *)ck.branch);
	ynode_free(result_input);
}

/* Keep source positions while removing options; later hints use the parsed word's identity. */
static void positional_word(struct request_input *q, struct strvec *rest, const char *value, int at)
{
	REALLOC_ARRAY(q->word_index, rest->nr + 1);
	q->word_index[rest->nr] = at;
	strvec_push(rest, value);
}

/* the options of a request, taken out of ARGV (spec 6.4) */
static void options(struct request_input *q, int argc, const char **argv, struct strvec *rest)
{
	int i, m = 0, f = 0;

	for (i = 0; i < argc; i++) {
		static const char *const valued[] = { "-m", "-F", "-r", "--result", "-g", NULL };
		int k;

		for (k = 0; valued[k]; k++)
			if (!strcmp(argv[i], valued[k]) && i + 1 == argc)
				reject(129, NULL, "option '%s' requires a value", argv[i]);
		/* v14 5.3, with git commit's words */
		if (!strcmp(argv[i], "-F") && f)
			reject(129, NULL, "-F is given twice");
		if ((!strcmp(argv[i], "-m") && f) || (!strcmp(argv[i], "-F") && m))
			reject(129, NULL, "options '-m' and '-F' cannot be used together");
		if (!strcmp(argv[i], "-m") && i + 1 < argc) {
			m = 1;
			if (q->msg.len)
				strbuf_addstr(&q->msg, "\n\n");
			strbuf_addstr(&q->msg, argv[++i]);
		} else if (!strcmp(argv[i], "-F") && i + 1 < argc) {
			f = 1;
			if ((strcmp(argv[i + 1], "-") ? strbuf_read_file(&q->msg, argv[i + 1], 0) :
			     strbuf_read(&q->msg, 0, 0)) < 0)
				reject(128, NULL, "could not read '%s'", argv[i + 1]);
			if (memchr(q->msg.buf, 0, q->msg.len))
				reject(1, NULL, "a NUL byte in commit log message is not allowed");
			i++;
		} else if ((!strcmp(argv[i], "-r") || !strcmp(argv[i], "--result")) && i + 1 < argc) {
			if (q->result)
				reject(129, NULL, "-r is given twice");
			q->result = argv[++i];
		} else if (!strcmp(argv[i], "--force")) {
			if (q->force)
				reject(129, NULL, "--force is given twice");
			q->force = 1;
			q->force_word = (int)q->words.nr - argc + i;
		} else if (!strcmp(argv[i], "-g") && i + 1 < argc) {
			if (q->group)
				reject(129, NULL, "-g is given twice");
			q->group = argv[++i];
			if (!group_ok(q->group) || !*q->group)
				reject(129, NULL, "bad group '%s'", q->group);
		} else if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--partial")) {
			positional_word(q, rest, argv[i], (int)q->words.nr - argc + i);
		} else if (*argv[i] == '-' && strcmp(argv[i], "-")) {
			reject(129, NULL, "unknown option '%s'", argv[i]);
		} else {
			positional_word(q, rest, argv[i], (int)q->words.nr - argc + i);
		}
	}
	if (q->result && q->force)
		reject(129, NULL, "-r and --force do not go together");
	strbuf_stripspace(&q->msg, NULL);
	strbuf_rtrim(&q->msg);
	/* 4.5: every command that takes options writes, and says why */
	if (!q->msg.len)
		reject(129, NULL, "a message is required: -m <msg> or -F <file>");
}

int cmd_request(int argc, const char **argv)
{
	struct request q;
	struct strvec rest = STRVEC_INIT;
	int fulfilling;

	request_init(&q, argv);
	options(&q.input, argc, argv, &rest);
	fulfilling = read_clauses(&q.input, rest.nr, rest.v);
	run_request(&q, fulfilling);
	return 0;
}

/* spec 6.7 */
int cmd_fulfill(int argc, const char **argv)
{
	struct request q;
	struct strvec rest = STRVEC_INIT;
	size_t i;

	request_init(&q, argv);
	options(&q.input, argc - 1, argv + 1, &rest);
	if (!rest.nr || q.input.group)
		reject(129, NULL, "fulfill takes K[=SCOPES]..., -r, --force, -m and -F");
	for (i = 0; i < rest.nr; i++) {
		const char *eq = strchr(rest.v[i], '=');
		char *id = eq ? xstrndup(rest.v[i], eq - rest.v[i]) : xstrdup(rest.v[i]);

		if (eq && (*id == 'X' || !strcmp(eq + 1, "-")))
			reject(129, NULL, "=SCOPES takes concrete provider scopes of a demand or offer");
		string_list_insert(&q.input.given, id)->util = &q.input.word_index[i];
		if (eq) {
			struct string_list_item *item = string_list_insert(&q.input.scopes, id);
			struct scope_input *input = xcalloc(1, sizeof(*input));

			if (item->util) {
				struct scope_input *old = item->util;

				scopes_clear(&old->value);
				free(old->error);
				free(old);
			}
			prepare_scopes(input, eq + 1, 0, 0);
			item->util = input;
		}
	}
	run_request(&q, 1);
	return 0;
}

/* the ".ait/<group>/<ID>.yaml" a draft of C lives at (spec 2.2) */
static char *draft_path(struct contract *c)
{
	return xstrfmt(".ait/%s", c->path);
}

/* REL written new in the work tree, its directories made; a file there is not overwritten */
static void write_new_file(const char *rel, const char *text)
{
	char *full = xstrfmt("%s/%s", top, rel);

	if (!create_file(full, text, 0))
		reject(1, NULL, "%s", refusal_words(EXISTS, rel, NULL, NULL));
	printf("path %s\n", rel);
}

/* spec 6.5.1: the condition file template (v14 7.4.1a without inputs) */
static int contract_new_spec(const char *path, const char *engine)
{
	session(0);
	require_condition_path(path);
	write_new_file(path, !strcmp(engine, "exec") ?
		"# ait spec (exec): the checker runs 'command'; it passes if the exit code is in 'success'.\n"
		"# Keys: body, engine, command, success, environment. Any other key is a format error.\n"
		"body: |\n  <the condition the code must keep>\n"
		"engine: exec\n"
		"command: [<program>, <arg>]   # argv, run without a shell\n"
		"success: [0]                  # exit codes that count as pass\n"
		"# environment: <name>         # optional: specs/environments/<name>.yaml in the code\n" :
		"# ait spec (prompt): the checker reads the code and judges 'body'.\n"
		"# Keys: body. Any other key is a format error.\n"
		"body: |\n  <the condition the code must keep>\n");
	return 0;
}

/* spec 6.5.2 */
static int contract_new_draft(const char *kind, const char *group)
{
	struct contract *c;
	char id[16];
	struct names n;
	struct strbuf hint = STRBUF_INIT, msg = STRBUF_INIT;
	struct string_list branches = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	const char *chosen;
	char *path, *text;
	int taken;

	if (!group_ok(group))
		reject(129, NULL, "bad group '%s'", group);
	session(0);
	names_load(&n);
	/* unique on the contract branch, or on every local one when none is chosen (6.5.2) */
	if ((chosen = branch_find(&n, checkout_read(), &msg, &hint, NULL)))
		string_list_append(&branches, chosen)->util = branch_load(chosen, NULL, NULL);
	else
		for_each_string_list_item(item, &n.local)
			string_list_append(&branches, item->string)->util = branch_load(item->string, NULL, NULL);
	do {
		newid(id, kind_letter(kind));
		taken = 0;
		for_each_string_list_item(item, &branches)
			taken |= contract_get(item->util, id) != NULL;
	} while (taken);
	for_each_string_list_item(item, &branches)
		branch_free(item->util);
	string_list_clear(&branches, 0);
	c = contract_new(kind, group, id);
	if (strcmp(kind, "drop")) {
		scope_append(&c->provider, &(struct scope){ .kind = SCOPE_FILE, .path = (char *)"<scope>" });
		if (!strcmp(kind, "demand"))
			scope_append(&c->consumer, &(struct scope){ .kind = SCOPE_FILE, .path = (char *)"<scope>" });
		c->specs = xcalloc(1, sizeof(*c->specs));
		c->specs->body = xstrdup("<condition>\n");
		strvec_init(&c->specs->command);
		string_list_init_dup(&c->specs->success);
		c->nspecs = 1;
	} else {
		c->target = xstrdup("<ID>");
	}
	path = draft_path(c);
	text = contract_text(c);
	write_new_file(path, text);
	free(path);
	free(text);
	contract_free(c);
	names_clear(&n);
	strbuf_release(&hint);
	strbuf_release(&msg);
	return 0;
}

/* spec 6.5.2a */
static int contract_checkout(int argc, const char **argv)
{
	struct branch *b;
	int i;

	b = start(0);
	for (i = 1; i < argc; i++) {
		struct contract *c = contract_get(b, argv[i]);
		char *path, *text;

		if (!c)
			no_contract(argv[i]);
		path = draft_path(c);
		text = object(blob_at(b->tree, c->path), 1, NULL, NULL);
		write_new_file(path, text);
		free(path);
		free(text);
	}
	branch_free(b);
	return 0;
}

static char *stage_index(void)
{
	worktree_state_dir();
	/* Git's writer, our lock, and consumption must name the same symlink target. */
	return git_path("ait/index");
}

/*
 * Git's index lock excludes add until commit has consumed its snapshot
 * (2.6.4). Register only a lock we created; signal cleanup must not see
 * an unregistered lock or remove another process's lock.
 */
static struct tempfile *lock_stage(const char *index)
{
	struct tempfile *lock = NULL;
	char *path = xstrfmt("%s.lock", index);
	sigset_t blocked, previous;
	int fd, saved;

	sigfillset(&blocked);
	sigprocmask(SIG_BLOCK, &blocked, &previous);
	fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
	saved = errno;
	if (fd >= 0) {
		lock = register_tempfile(path);
		lock->fd = fd;
		guard_git_children();
	}
	sigprocmask(SIG_SETMASK, &previous, NULL);
	free(path);
	if (!lock)
		unable_to_lock_die(index, saved);
	return lock;
}

/* A released lock must leave the cleanup list before another owner can lose it. */
static void unlock_stage(struct tempfile **lock, const char *index)
{
	sigset_t blocked, previous;
	int failed, saved;

	sigfillset(&blocked);
	sigprocmask(SIG_BLOCK, &blocked, &previous);
	failed = delete_tempfile(lock);
	saved = errno;
	sigprocmask(SIG_SETMASK, &previous, NULL);
	if (failed) {
		errno = saved;
		write_error(xstrfmt("%s.lock", index));
	}
}

/* spec 6.5.3: drafts into the contract stage, as they are now */
static int contract_add(int argc, const char **argv)
{
	struct strvec args = STRVEC_INIT;
	struct string_list paths = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	const char *index;
	int i;

	if (argc < 2)
		reject(129, NULL, "add names the drafts");
	for (i = 1; i < argc; i++) {
		const char *rel;

		if (!skip_prefix(argv[i], ".ait/", &rel) || !contract_path_id(rel))
			reject(129, NULL, "'%s' is not a draft: .ait/<group>/<ID>.yaml", argv[i]);
		string_list_append(&paths, rel);
	}
	branch_free(start(0));
	index = stage_index();
	strvec_pushl(&args, "update-index", "--add", NULL);
	for_each_string_list_item(item, &paths) {
		struct strbuf sb = STRBUF_INIT;
		char *err, *oid, *operand;
		struct contract *c;

		if (strbuf_read_file(&sb, xstrfmt("%s/.ait/%s", top, item->string), 0) < 0)
			reject(128, NULL, "could not read '.ait/%s'", item->string);
		if (!(c = contract_parse(sb.buf, sb.len, item->string, 1, &err)))
			reject(128, NULL, "bad format: '%s': %s", item->string, err);
		contract_free(c);
		oid = line1(git_run(sb.buf, NULL, NULL, "hash-object", "-w", "--stdin"));
		operand = xstrfmt("100644,%s,%s", oid, item->string);
		strvec_pushl(&args, "--cacheinfo", operand, NULL);
		free(operand);
		free(oid);
		strbuf_release(&sb);
	}
	/* --index-info silently skips invalid paths; --cacheinfo refuses the transaction. */
	git_args(&args, NULL, index, NULL);
	for_each_string_list_item(item, &paths)
		printf("add %s\n", item->string);
	return 0;
}

struct staged {
	struct request q;
	struct string_list drafts;	/* path -> immutable parsed draft */
};

/* v14 3.3: drafts are fixed at HEAD, retaining unchanged tuples from T. */
static void pin_drafted(struct scope_list *scopes, const struct scope_list *was, struct tree *h)
{
	char *err;

	if (scopes_pin(scopes, h, was, &err))
		reject(128, NULL, "bad scope: %s", err);
}

/* spec 6.5.4: the staged drafts judged against the tip, each a change from T */
static void commit_judge(struct checkin *ck)
{
	struct staged *st = ck->data;
	struct request_input *q = &st->q.input;
	struct request_result *r = &st->q.result;
	struct string_list_item *item;

	request_result_reset(r, ck->at);
	for_each_string_list_item(item, &st->drafts) {
		struct contract *d = contract_copy(item->util), *old;
		struct string_list_item *prior;

		old = contract_get(r->at, d->id);
		prior = string_list_lookup(&r->changed, d->id);
		if (prior)
			reject(128, NULL, "bad format: '%s': %s", d->path,
			       yaml_refusal_at(1, "'%s' is also at '%s'", d->id, ((struct contract *)prior->util)->path));
		pin_drafted(&d->consumer, old ? &old->consumer : NULL, q->h);
		pin_drafted(&d->provider, old ? &old->provider : NULL, q->h);
		if (!old) {
			replace_text(&d->creator, agent);
			string_list_append(&r->order, d->id);
		} else {
			if (strcmp(d->change_id, old->change_id))
				reject(1, NULL, "%s",
				       refusal_words(STALE_DRAFT, d->id, ck->branch, xstrfmt(".ait/%s", item->string)));
			/* a draft's fulfilled is what it was checked out with: T's list, or the start of it */
			if (!list_is_prefix(&d->fulfilled, &old->fulfilled))
				refuse(q, FULFILMENT, d, old);
			contract_fulfilled_copy(d, old);
			replace_text(&d->condition_id, old->condition_id);
			replace_text(&d->change_id, old->change_id);
		}
		publish_change(q, r, old, d, DRAFT, q->force);
	}
	/* 6.5.4 is a staged batch: every target is judged in the same final view. */
	for_each_string_list_item(item, &r->changed)
		validate_change(q, r, contract_get(r->at, item->string), item->util, DRAFT, q->force);
	settle(r, ck);
}

/* spec 6.5.4 */
static int contract_commit(int argc, const char **argv)
{
	struct staged st;
	struct strvec rest = STRVEC_INIT;
	struct checkin ck = { 0 };
	struct string_list_item *item;
	struct string_list lines = STRING_LIST_INIT_DUP;
	struct tempfile *lock;
	const char *index;
	char *summary;

	request_init(&st.q, argv - 1);
	string_list_init_dup(&st.drafts);
	options(&st.q.input, argc - 1, argv + 1, &rest);
	if (rest.nr || st.q.input.result || st.q.input.group)
		reject(129, NULL, "contract commit takes -m, -F and --force");
	ck.msg = request_message(st.q.input.msg.buf, 0);
	ck.branch = start_name(START_SESSION | START_WRITES | START_TRAILERS);
	newid(ck.rid, 'R');
	st.q.input.x = index_tree();
	st.q.input.h = tree_load(head ? head : EMPTY_TREE);
	st.q.input.rid = ck.rid;
	index = stage_index();
	lock = lock_stage(index);
	git_records(&lines, index, NULL, "ls-files", "-s", "-z");
	for_each_string_list_item(item, &lines) {
		char mode[8], oid[48], *bytes;
		struct contract *draft;
		char *err;
		size_t len;

		if (!*item->string)
			continue;
		sscanf(item->string, "%7s %47s", mode, oid);
		bytes = object(oid, 1, NULL, &len);
		if (!bytes)
			reject(128, NULL, "could not read '%s' from the contract stage", strchr(item->string, '\t') + 1);
		draft = contract_parse(bytes, len, strchr(item->string, '\t') + 1, 1, &err);
		free(bytes);
		if (!draft)
			reject(128, NULL, "bad format: '%s': %s", strchr(item->string, '\t') + 1, err);
		string_list_insert(&st.drafts, strchr(item->string, '\t') + 1)->util = draft;
	}
	if (!st.drafts.nr)
		reject(1, hint_command("ait contract add <path>..."), "nothing staged");
	string_list_init_dup(&ck.put);
	ck.data = &st;
	summary = checkin_run(&ck, commit_judge);
	if (unlink(index))
		write_error(index);
	unlock_stage(&lock, index);
	for_each_string_list_item(item, &st.drafts) {
		char *id = contract_path_id(item->string);

		printf("%s %s\n", unsorted_string_list_has_string(&st.q.result.order, id) ? "new" : "change", id);
	}
	checkin_report(summary);
	request_result_reset(&st.q.result, NULL);
	request_input_clear(&st.q.input);
	for_each_string_list_item(item, &st.drafts)
		contract_free(item->util);
	string_list_clear(&st.drafts, 0);
	string_list_clear(&lines, 0);
	strvec_clear(&rest);
	checkin_clear(&ck);
	free((char *)ck.branch);
	return 0;
}

static const char *remote_arg(int argc, const char **argv)
{
	if (argc > 2)
		reject(129, NULL, "too many arguments");
	return argc == 2 ? argv[1] : NULL;
}

/* the remote of a push or pull (spec 6.5.5-6): git's own choice */
static char *remote_of(const char *branch, const char *which, const char *given)
{
	char *r;

	if (given)
		return xstrdup(given);
	r = line1(git("for-each-ref", xstrfmt("--format=%%(%s:remotename)", which),
		xstrfmt("refs/heads/%s", branch)));
	return *r ? r : xstrdup("origin");
}

/* spec 6.5.5 */
static int contract_push(int argc, const char **argv)
{
	const char *b, *remote;
	struct strvec args = STRVEC_INIT;
	int rc;

	remote = remote_arg(argc, argv);
	b = start_name(0);
	strvec_pushl(&args, "push", "--", remote_of(b, "push", remote),
		     xstrfmt("refs/heads/%s:refs/heads/%s", b, b), NULL);
	rc = git_pass(&args);
	strvec_clear(&args);
	free((char *)b);
	return rc;
}

struct pull {
	const char *upstream;		/* U, a remote-tracking ref */
	char *name;			/* U as git's merge message names it (fmt-merge-msg.c) */
};

static char *pull_hint(struct checkin *ck, struct string_list *violations)
{
	return xstrfmt("merge '%s' into '%s' and resolve the conflicts there (enter it with '%s')",
		       ((struct pull *)ck->data)->name, ck->branch, hint_command("ait switch %s", quoted(ck->branch)));
}

/*
 * spec 6.5.6-3, 4, judged again on every new T: U resolved once, so the
 * merge and its second parent are the same commit
 */
static void pull_judge(struct checkin *ck)
{
	struct string_list lines = STRING_LIST_INIT_DUP;
	struct strbuf paths = STRBUF_INIT;
	char u[48], *expression = xstrfmt("%s^{commit}", ((struct pull *)ck->data)->upstream);
	size_t i;
	int rc;

	object_oid(expression, u);
	free(expression);
	if (is_ancestor(u, ck->tip)) {
		puts("Already up to date");
		exit(0);
	}
	if (is_ancestor(ck->tip, u)) {
		ck->ff = xstrdup(u);
		return;
	}
	ck->second_parent = xstrdup(u);
	git_records(&lines, NULL, &rc, "merge-tree", "--write-tree", "--name-only", "-z", ck->tip, u);
	if (rc != 0 && rc != 1)
		reject(128, NULL, "git failed: 'git merge-tree': %s", git_error);
	if (!lines.nr)
		reject(128, NULL, "git failed: 'git merge-tree': no tree returned");
	ck->tree = xstrdup(lines.items[0].string);
	if (!rc)
		return;
	for (i = 1; i < lines.nr && *lines.items[i].string; i++)
		strbuf_addf(&paths, "%s%s", paths.len ? "', '" : "", lines.items[i].string);
	reject(1, pull_hint(ck, NULL), "contract branches conflict in '%s'", paths.buf);
}

/* spec 6.5.6 */
static int contract_pull(int argc, const char **argv)
{
	struct strvec args = STRVEC_INIT;
	struct checkin ck = { 0 };
	struct pull p;
	const char *remote;

	remote = remote_arg(argc, argv);
	ck.branch = start_name(START_SESSION | START_WRITES | START_TRAILERS);
	strvec_pushl(&args, "fetch", "-q", "--", remote_of(ck.branch, "upstream", remote), NULL);
	if (git_pass(&args))
		return 1;
	if (!(p.upstream = upstream_of(ck.branch)))
		reject(128, NULL, "'%s' has no upstream", ck.branch);
	p.name = line1(git("rev-parse", "--abbrev-ref", p.upstream));
	string_list_init_dup(&ck.put);
	/* ait's own words hold no cut line; start() read the trailers back alone */
	ck.msg = xstrfmt("[CONTRACT] merge '%s'", p.name);
	ck.refusal = xstrfmt("merging '%s' breaks contract rules: ", p.name);
	ck.violation_hint = pull_hint;
	ck.data = &p;
	checkin_report(checkin_run(&ck, pull_judge));
	checkin_clear(&ck);
	free((char *)ck.branch);
	free((char *)p.upstream);
	free(p.name);
	strvec_clear(&args);
	return 0;
}

int cmd_contract(int argc, const char **argv)
{
	if (argc == 5 && !strcmp(argv[1], "new") && !strcmp(argv[2], "spec") &&
	    (!strcmp(argv[4], "exec") || !strcmp(argv[4], "prompt")))
		return contract_new_spec(argv[3], argv[4]);
	if (argc >= 3 && !strcmp(argv[1], "new") &&
	    (!strcmp(argv[2], "demand") || !strcmp(argv[2], "offer") || !strcmp(argv[2], "drop")) && argc <= 4)
		return contract_new_draft(argv[2], argc == 4 ? argv[3] : "");
	if (argc >= 3 && !strcmp(argv[1], "checkout"))
		return contract_checkout(argc - 1, argv + 1);
	if (argc >= 2 && !strcmp(argv[1], "add"))
		return contract_add(argc - 1, argv + 1);
	if (argc >= 2 && !strcmp(argv[1], "commit"))
		return contract_commit(argc - 1, argv + 1);
	if (argc >= 2 && !strcmp(argv[1], "push"))
		return contract_push(argc - 1, argv + 1);
	if (argc >= 2 && !strcmp(argv[1], "pull"))
		return contract_pull(argc - 1, argv + 1);
	reject(129, NULL, "unknown contract command");
}
