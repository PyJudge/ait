/*
 * Requests (spec 6.4), the contract commands (6.5), fulfill (6.7) and the
 * check results (7): every one is one checkin of the contract branch.
 */
#include "ait.h"
#include "quote.h"

/* one clause of a request as given (6.4): its verb and its words */
struct clause {
	const char *verb;		/* demand, offer, drop, amend, cancel, undertake, release */
	int future;
	int partial;			/* undertake -p */
	const char *args[4];		/* its words after the verb: scopes and files, IDs */
	int nargs;
	const char *as;
};

/* a request: what it changes on the branch, and what it fulfils */
struct request {
	struct clause *clauses;		/* the clauses, read once from the words (6.4) */
	size_t nclauses;
	struct branch *b;		/* the branch at T */
	struct tree *x;			/* the index's tree (5.1) */
	struct string_list changed;	/* ID -> struct contract: new and changed contracts */
	struct string_list order;	/* the new contracts in the order made: "+1", "+2", ... (7.1) */
	struct string_list alias;	/* alias -> ID */
	struct string_list given;	/* the contracts ait fulfill names */
	struct string_list fulfil;	/* IDs to fulfil: the given ones and those of the clauses */
	struct string_list made;	/* the IDs the new contracts took the first time, kept across judgments */
	struct string_list trivial;	/* the contracts changed trivially: future amend --force, contract commit --force */
	struct string_list fixed;	/* step 0's covered set: ID -> its condition-id (6.7.2) */
	struct strvec words;		/* the command as given, for the hints that repeat it */
	struct string_list scopes;	/* ID -> "=SCOPES" of ait fulfill */
	struct strbuf msg;
	const char *result, *group;
	int force, future_force;
	char rid[16];
	/* fulfilling: the code commit and its tree (6.7.5) */
	char commit[48];
	struct tree *code;
	struct string_list results;	/* task -> its result block */
	struct string_list covered;	/* the covered set (6.7.2) */
};

static void request_init(struct request *q, const char **argv)
{
	memset(q, 0, sizeof(*q));
	strvec_init(&q->words);
	strvec_pushv(&q->words, argv);
	string_list_init_dup(&q->given);
	string_list_init_dup(&q->made);
	string_list_init_dup(&q->trivial);
	string_list_init_dup(&q->fixed);
	string_list_init_dup(&q->changed);
	string_list_init_dup(&q->order);
	string_list_init_dup(&q->alias);
	string_list_init_dup(&q->fulfil);
	string_list_init_dup(&q->scopes);
	string_list_init_dup(&q->results);
	string_list_init_dup(&q->covered);
	strbuf_init(&q->msg, 0);
}

/* the contract ID as it stands after this request's changes so far */
static struct contract *now(struct request *q, const char *id)
{
	struct string_list_item *item = string_list_lookup(&q->changed, id);

	return item ? item->util : contract_get(q->b, id);
}

/*
 * the command as it was given, for a hint (4.9): the word FROM becomes TO
 * (or goes, when TO is NULL), EXTRA is added, and a message is asked for
 * when none was given
 */
static char *command_again(struct request *q, const char *from, const char *to, const char *extra)
{
	struct strbuf sb = STRBUF_INIT;
	size_t i;
	int msg = 0;

	strbuf_addstr(&sb, "ait");
	for (i = 0; i < q->words.nr; i++) {
		msg |= !strcmp(q->words.v[i], "-m") || !strcmp(q->words.v[i], "-F");
		if (from && !to && !strcmp(q->words.v[i], from))
			continue;
		/* each word quoted only where the shell needs it, as git shows a command (quote.c) */
		strbuf_addch(&sb, ' ');
		sq_quote_buf_pretty(&sb, from && !strcmp(q->words.v[i], from) ? to : q->words.v[i]);
	}
	strbuf_addstr(&sb, extra ? extra : "");
	if (!msg)
		strbuf_addstr(&sb, " -m <why>");
	return strbuf_detach(&sb, NULL);
}

/*
 * spec 2.4, 4.8 (E_STATE): why a contract may not change. The judgments
 * return the reason and its words are here once, as git's push words its
 * REJECT_* reasons in one table (builtin/push.c).
 */
enum refusal {
	CHANGE_OK,
	ENDED,
	TARGET,
	IDENTITY,
	STATE,
	FULFILMENT,
	FORCE_PROVIDER
};

static NORETURN void refuse(struct request *q, enum refusal why, struct contract *c, struct contract *old)
{
	switch (why) {
	case ENDED:
		reject(1, NULL, "not allowed: '%s' has ended", c->id);
	case TARGET:
		reject(1, NULL, "not allowed: '%s' targets '%s', which is not current", c->id, c->target);
	case IDENTITY:
		reject(1, NULL, "not allowed: '%s' changes its group, kind or creator", c->id);
	case STATE:
		reject(1, NULL, "not allowed: '%s' goes from '%s' to '%s'", c->id, old->state, c->state);
	case FULFILMENT:
		reject(1, NULL, "fulfilments are recorded only by 'ait fulfill'");
	case FORCE_PROVIDER:
		/* the old fulfilments checked the old provider: the change is made without --force */
		reject(1, command_again(q, "--force", NULL, NULL), "cannot change the provider of '%s' with --force",
		       c->id);
	default:
		BUG("no words for refusal %d", why);
	}
}

/* spec 1.4, 2.4: cancelled, withdrawn, or a fulfilled drop: it changes no more */
static int ended(struct contract *c)
{
	return !live(c) || (*c->id == 'X' && c->fulfilled.nr);
}

/* a contract this request changes: its copy, made once; it must not have ended as it stands (2.4) */
static struct contract *changing(struct request *q, const char *id)
{
	struct string_list_item *item = string_list_lookup(&q->changed, id);
	struct contract *c;
	char *err;

	if (item && ended(item->util))
		refuse(q, ENDED, item->util, NULL);
	if (item)
		return item->util;
	c = contract_get(q->b, id);
	if (ended(c))
		refuse(q, ENDED, c, NULL);
	c = contract_parse(contract_text(c), c->path, 0, &err);
	string_list_insert(&q->changed, id)->util = c;
	return c;
}

/* K, an ID or an alias of this request, of a kind in KINDS ("DOX") */
static struct contract *contract_arg(struct request *q, const char *k, const char *kinds)
{
	struct string_list_item *a = string_list_lookup(&q->alias, k);
	const char *id = a ? a->util : k;
	struct contract *c = now(q, id);

	if (!c || !strchr(kinds, *c->id))
		reject(128, NULL, "no such contract: '%s'", k);
	return c;
}

/* spec 3.1: a condition file's path is a repository path, and may be under .ait/ */
static int condition_path_ok(const char *p)
{
	skip_prefix(p, ".ait/", &p);
	return path_ok(p);
}

/* v14 2.5.2 without inputs: the condition files of FILES, read from the work tree (6.4.2) */
static void specs_from_files(struct contract *c, const char *files)
{
	struct string_list paths = STRING_LIST_INIT_DUP;
	struct string_list_item *item;

	string_list_split(&paths, files, ",", -1);
	free(c->specs);
	c->specs = NULL;
	c->nspecs = 0;
	for_each_string_list_item(item, &paths) {
		struct strbuf sb = STRBUF_INIT;
		struct string_list kv = STRING_LIST_INIT_DUP;
		struct spec *s;
		char *err;
		const char *p = item->string;

		if (!condition_path_ok(p) || strbuf_read_file(&sb, xstrfmt("%s/%s", top, p), 0) < 0)
			reject(128, xstrfmt("ait contract new spec %s exec|prompt", quoted(p)), "could not read '%s'", p);
		if (yaml_parse(sb.buf, &kv, p, &err))
			reject(128, NULL, "bad format: %s", err);
		if (!(s = spec_from(&kv, "", &err)))
			reject(128, NULL, "bad format: '%s': %s", p, err);
		REALLOC_ARRAY(c->specs, c->nspecs + 1);
		c->specs[c->nspecs++] = *s;
	}
}

/* scope arguments fixed at T (spec 3.2); "-" keeps what is there */
static void scopes_arg(struct string_list *into, const char *arg, struct tree *t, int allow_new, int allow_abs)
{
	struct string_list args = STRING_LIST_INIT_DUP;
	struct string_list_item *item;

	if (!strcmp(arg, "-"))
		return;
	string_list_clear(into, 0);
	string_list_split(&args, arg, ",", -1);
	for_each_string_list_item(item, &args) {
		char *err, *pinned = scope_pin(item->string, t, &err);

		if (!pinned || !scope_ok(pinned, allow_new, allow_abs))
			reject(128, NULL, "bad scope: %s", pinned ? xstrfmt("'%s' is not allowed here", item->string) : err);
		if (unsorted_string_list_has_string(into, pinned))
			reject(128, NULL, "bad scope: '%s' is given twice", item->string);
		string_list_append(into, pinned);
	}
}

/* the confirmation conditions of 2.3.3: provider, specs, undertakes */
static char *conditions(struct contract *c)
{
	struct contract d = *c;
	struct string_list none = STRING_LIST_INIT_DUP;

	d.consumer = none;
	d.partially = none;
	d.fulfilled = none;
	d.condition_id = d.change_id = d.creator = d.state = (char *)"";
	d.target = (char *)"";
	return contract_text(&d);
}

static char *rows(struct contract *c)
{
	struct contract d = *c;
	struct string_list none = STRING_LIST_INIT_DUP;

	d.fulfilled = none;
	d.condition_id = d.change_id = d.state = (char *)"";
	return contract_text(&d);
}

/*
 * spec 2.3.3: the IDs a change of C from OLD takes: the condition-id when
 * its conditions changed (unless trivial), the change-id when a row did
 */
static void stamp(struct request *q, struct contract *c, struct contract *old, int trivial)
{
	if ((!old || strcmp(conditions(c), conditions(old))) && (!trivial || !old))
		c->condition_id = xstrdup(q->rid);
	if (!old || strcmp(rows(c), rows(old)))
		c->change_id = xstrdup(q->rid);
}

static void new_alias(struct request *q, const char *name, const char *id)
{
	static const char *const reserved[] = { "demand", "offer", "future", "undertake", "release", "as", "drop",
						"amend", "cancel", "contract", "fulfill", NULL };
	int i;

	if (!name)
		return;
	for (i = 0; reserved[i]; i++)
		if (!strcmp(name, reserved[i]))
			reject(129, NULL, "'%s' is a reserved word", name);
	if (!*name || !islower(*name) || name[strspn(name, "abcdefghijklmnopqrstuvwxyz0123456789_")] || strlen(name) > 32)
		reject(129, NULL, "bad alias '%s'", name);
	if (string_list_lookup(&q->alias, name))
		reject(129, NULL, "alias '%s' is given twice", name);
	string_list_insert(&q->alias, name)->util = xstrdup(id);
}

static struct contract *make(struct request *q, const char *kind)
{
	char id[16];
	struct contract *c;

	/* a judgment made again takes the same IDs, so the step-0 results and the record agree (6.7.5) */
	if (q->order.nr < q->made.nr)
		snprintf(id, sizeof(id), "%s", q->made.items[q->order.nr].string);
	else
		do
			newid(id, kind_letter(kind));
		while (now(q, id));
	if (q->order.nr == q->made.nr)
		string_list_append(&q->made, id);
	c = contract_new(kind, q->group ? q->group : "", id);
	c->creator = xstrdup(agent);
	string_list_insert(&q->changed, id)->util = c;
	string_list_append(&q->order, id);
	return c;
}

/* spec 2.4: the drop X targets a D or O judged current */
static int target_current(struct request *q, struct contract *x)
{
	struct contract *t = now(q, x->target);

	return t && judge(q->b, t, q->x) == V_CURRENT;
}

/* a word that starts a clause (v14 7.3.1) */
static int clause_word(const char *w)
{
	static const char *const words[] = { "demand", "offer", "future", "amend", "undertake", "release", NULL };
	int i;

	for (i = 0; words[i]; i++)
		if (!strcmp(w, words[i]))
			return 1;
	return 0;
}

/* the next word of a clause; only amend's "-" starts with '-' (v14 7.3.1) */
static const char *word(int argc, const char **argv, int *i)
{
	if (*i >= argc || (*argv[*i] == '-' && strcmp(argv[*i], "-")))
		reject(129, NULL, "a clause is cut short");
	return argv[(*i)++];
}

/*
 * spec 6.4: the clauses of ARGV, read once and without the branch, so
 * whether the request fulfils is known before it is judged (4.3-5);
 * how many words amend takes is checked against its contract's kind later
 */
static int read_clauses(struct request *q, int argc, const char **argv)
{
	static const struct {
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
	int i = 0, fulfils = 0;

	while (i < argc) {
		struct clause *cl;
		const char *w = argv[i++];
		int future = !strcmp(w, "future");
		size_t v;

		/* options() leaves only -p and --partial here, and they belong right after undertake */
		if (*w == '-')
			reject(129, NULL, "'%s' goes right after 'undertake'", w);
		if (future)
			w = word(argc, argv, &i);
		for (v = 0; v < ARRAY_SIZE(verbs) && strcmp(w, verbs[v].verb); v++)
			;
		if (v == ARRAY_SIZE(verbs) || (verbs[v].future != 2 && verbs[v].future != future))
			reject(129, NULL, "unknown clause '%s%s'", future ? "future " : "", w);
		REALLOC_ARRAY(q->clauses, q->nclauses + 1);
		cl = &q->clauses[q->nclauses++];
		memset(cl, 0, sizeof(*cl));
		cl->verb = verbs[v].verb;
		cl->future = future;
		if (!strcmp(w, "undertake") && i < argc && (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--partial"))) {
			cl->partial = 1;
			i++;
		}
		while (cl->nargs < verbs[v].words)
			cl->args[cl->nargs++] = word(argc, argv, &i);
		/* v14 7.3.1: amend K takes up to three more words; none is the empty amend */
		while (!strcmp(w, "amend") && i < argc && cl->nargs < 4 && !clause_word(argv[i]))
			cl->args[cl->nargs++] = argv[i++];
		if (verbs[v].named && i + 1 < argc && !strcmp(argv[i], "as")) {
			cl->as = argv[i + 1];
			i += 2;
		}
		fulfils |= verbs[v].fulfils && !future;
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
static void apply_clauses(struct request *q)
{
	struct tree *pin_at = head ? tree_load(head) : tree_load(EMPTY_TREE);
	size_t n;

	for (n = 0; n < q->nclauses; n++) {
		struct clause *cl = &q->clauses[n];
		const char **a = cl->args;
		struct contract *c, *d, *o;

		if (!strcmp(cl->verb, "demand") || !strcmp(cl->verb, "offer")) {
			int demand = !strcmp(cl->verb, "demand");

			c = make(q, cl->verb);
			if (demand)
				scopes_arg(&c->consumer, a[0], pin_at, 1, 0);
			scopes_arg(&c->provider, a[demand], pin_at, 1, 1);
			specs_from_files(c, a[demand + 1]);
			new_alias(q, cl->as, c->id);
		} else if (!strcmp(cl->verb, "drop")) {
			struct contract *t = contract_arg(q, a[0], "DO");

			c = make(q, "drop");
			c->target = xstrdup(t->id);
			new_alias(q, cl->as, c->id);
		} else if (!strcmp(cl->verb, "amend")) {
			int more = cl->nargs - 1;

			c = contract_arg(q, a[0], "DOX");
			/* v14 7.3.1: D takes A B FILES, O takes B FILES, X takes K */
			if (more && more != (*c->id == 'D' ? 3 : *c->id == 'O' ? 2 : 1))
				reject(129, NULL, "amend '%s' takes %s", c->id,
				       *c->id == 'D' ? "A B FILES" : *c->id == 'O' ? "B FILES" : "the target");
			c = changing(q, c->id);
			if (*c->id == 'X' && more) {
				c->target = xstrdup(contract_arg(q, a[1], "DO")->id);
			} else if (more) {
				if (*c->id == 'D')
					scopes_arg(&c->consumer, a[1], pin_at, 1, 0);
				scopes_arg(&c->provider, a[more - 1], pin_at, 1, 1);
				if (strcmp(a[more], "-"))
					specs_from_files(c, a[more]);
			}
			if (cl->future && q->future_force)
				string_list_insert(&q->trivial, c->id);
			/* an empty amend reopens: both IDs move (6.4.1) */
			if (!more) {
				c->condition_id = xstrdup(q->rid);
				c->change_id = xstrdup(q->rid);
			}
		} else if (!strcmp(cl->verb, "cancel")) {
			c = contract_arg(q, a[0], "DOX");
			if (strcmp(c->state, "cancelled"))
				changing(q, c->id)->state = xstrdup("cancelled");
			continue;
		} else {
			d = contract_arg(q, a[0], "D");
			o = contract_arg(q, a[1], "O");
			if (!strcmp(cl->verb, "undertake") && ended(d))
				refuse(q, ENDED, d, NULL);
			o = changing(q, o->id);
			remove_id(&o->undertakes, d->id);
			remove_id(&o->partially, d->id);
			if (!strcmp(cl->verb, "undertake"))
				string_list_append(cl->partial ? &o->partially : &o->undertakes, d->id);
			continue;
		}
		if (!cl->future)
			string_list_insert(&q->fulfil, c->id);
	}
}


/* spec 4.10: the hint that extends this request so a violation it would add goes */
static char *violation_hint(struct checkin *ck, const char *key)
{
	struct request *q = ck->data;
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;
	const char *rest;

	if (skip_prefix(key, "pair:", &rest)) {
		strbuf_addf(&sb, " future amend %.*s", (int)strcspn(rest, ":"), rest);
	} else if (skip_prefix(key, "mix:", &rest)) {
		/* the other side: the offers that held D at T, which this request did not touch */
		for_each_string_list_item(item, &q->b->contracts)
			if (live(item->util) && holds(item->util, rest) && !string_list_lookup(&q->changed, item->string))
				strbuf_addf(&sb, " release %s %s", rest, item->string);
	}
	return command_again(q, NULL, NULL, sb.buf);
}

/* spec 6.7.2: the covered set of K, judged in X */
static void cover(struct request *q, struct contract *k, struct string_list *out)
{
	struct string_list_item *item;

	string_list_insert(out, k->id);
	if (!strcmp(k->kind, "offer")) {
		for_each_string_list_item(item, &k->undertakes)
			if (live(now(q, item->string)))
				string_list_insert(out, item->string);
	} else if (!strcmp(k->kind, "demand")) {
		/* the offers at T and those this request makes */
		struct string_list ids = STRING_LIST_INIT_DUP;

		for_each_string_list_item(item, &q->b->contracts)
			string_list_insert(&ids, item->string);
		for_each_string_list_item(item, &q->order)
			string_list_insert(&ids, item->string);
		for_each_string_list_item(item, &ids) {
			struct contract *o = now(q, item->string);

			if (live(o) && holds(o, k->id) == 'p' && judge(q->b, o, q->x) != V_CURRENT)
				string_list_insert(out, o->id);
		}
	}
}

static char *task_of(const char *id, size_t n)
{
	return xstrfmt("%s:%d", id, (int)n + 1);
}

/* the name a contract has in the tasks: "+N" for one this request makes (7.1) */
static const char *task_id(struct request *q, const char *id)
{
	size_t i;

	for (i = 0; i < q->order.nr; i++)
		if (!strcmp(q->order.items[i].string, id))
			return xstrfmt("+%d", (int)i + 1);
	return id;
}

/* spec 7.2 (C39): the environment definition, read from the code at HEAD, its lines under "environment:" */
static void environment_out(struct strbuf *sb, const char *name)
{
	char *path = xstrfmt("specs/environments/%s.yaml", name), *text;
	struct string_list lines = STRING_LIST_INIT_DUP;
	size_t i;

	if (!head || !(text = object(xstrfmt("%s:%s", head, path), 1, NULL, NULL)))
		reject(128, NULL, "could not read '%s' at HEAD", path);
	strbuf_addstr(sb, "  environment:\n");
	string_list_split(&lines, text, "\n", -1);
	for (i = 0; i < lines.nr && (i + 1 < lines.nr || *lines.items[i].string); i++)
		strbuf_addf(sb, "%s%s\n", *lines.items[i].string ? "    " : "", lines.items[i].string);
}

/* what makes a result template the same one (7.4): its condition-ids and its tasks, in order */
static char *template_key(const char *text)
{
	struct string_list kv = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct strbuf sb = STRBUF_INIT;
	char *err;

	if (yaml_parse(text, &kv, "", &err))
		return xstrdup("");
	for_each_string_list_item(item, &kv)
		if (starts_with(item->string, "condition-ids.") ||
		    (starts_with(item->string, "results.") && ends_with(item->string, ".task")))
			strbuf_addf(&sb, "%s=%s\n", item->string, yget(&kv, item->string));
	return strbuf_detach(&sb, NULL);
}

/* spec 7.2: the check request and the result template, unless the template is already this one */
static void write_check_files(struct request *q)
{
	struct strbuf req = STRBUF_INIT, res = STRBUF_INIT, ids = STRBUF_INIT, old = STRBUF_INIT;
	struct string_list_item *item;
	char *dir = xstrfmt("%s/ait", gitdir), *rq = xstrfmt("%s/check-request.yaml", dir),
	     *rs = xstrfmt("%s/check-result.yaml", dir);
	size_t n;
	int tasks = 0;

	mkdir(dir, 0777);
	for_each_string_list_item(item, &q->covered)
		if (!strcmp(task_id(q, item->string), item->string))
			strbuf_addf(&ids, "  %s: \"%s\"\n", item->string, now(q, item->string)->condition_id);
	strbuf_addf(&req, "condition-ids:%s\n%stree: \"%s\"\ntasks:\n", ids.len ? "" : " {}", ids.buf, q->x->oid);
	strbuf_addf(&res, "condition-ids:%s\n%sresults:\n", ids.len ? "" : " {}", ids.buf);
	for_each_string_list_item(item, &q->covered) {
		struct contract *c = now(q, item->string);

		for (n = 0; n < c->nspecs; n++) {
			struct spec *s = &c->specs[n];
			char *task = task_of(task_id(q, c->id), n);

			printf("task %s\n", task);
			tasks++;
			strbuf_addf(&req, "- task: \"%s\"\n  contract: \"%s\"\n  kind: \"%s\"\n", task, task_id(q, c->id), c->kind);
			if (!strcmp(c->kind, "demand"))
				strbuf_addf(&req, "  consumer: \"%s\"\n", scopes_display(&c->consumer, q->x));
			strbuf_addf(&req, "  provider: \"%s\"\n  engine: \"%s\"\n  body: ", scopes_display(&c->provider, q->x),
				    s->engine ? "exec" : "prompt");
			yaml_out_body(&req, s->body, "    ");
			if (s->engine)
				spec_lists(&req, s, 1);
			if (s->environment)
				environment_out(&req, s->environment);
			strbuf_addf(&res, "- task: \"%s\"\n  outcome:\n  basis: |\n  reason:\n", task);
		}
	}
	if (!tasks)
		return;
	write_state(rq, req.buf);
	/* 7.4: a template with these condition-ids and tasks stays, filled or not */
	if (strbuf_read_file(&old, rs, 0) < 0 || strcmp(template_key(old.buf), template_key(res.buf)))
		write_state(rs, res.buf);
	printf("request %s\nresult %s\n", rq, rs);
	reject(1, xstrfmt("check each task, fill '%s', and rerun with -r '%s'\n"
			  "if it is trivial, add --force instead (recorded as forced)", rs, rs),
	       "check results required: %d task(s)", tasks);
}

/* spec 7.3: the result file against the covered set; each task's block kept for its fulfilment */
static void read_results(struct request *q)
{
	struct strbuf sb = STRBUF_INIT;
	struct string_list kv = STRING_LIST_INIT_DUP, want = STRING_LIST_INIT_DUP, failed = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	char *err, *fix = xstrfmt("fix '%s': every task needs an outcome (pass, fail or unknown), and a pass needs a basis",
				  q->result);
	size_t n;
	int i;

	if (strbuf_read_file(&sb, q->result, 0) < 0)
		reject(128, NULL, "could not read '%s'", q->result);
	if (yaml_parse(sb.buf, &kv, q->result, &err))
		reject(128, NULL, "bad format: %s", err);
	/* 7.3: the condition-ids are those of the covered contracts at T, none missing, none more; else a new template */
	for_each_string_list_item(item, &kv) {
		const char *id;

		if (skip_prefix(item->string, "condition-ids.", &id) &&
		    (!string_list_has_string(&q->covered, id) || strcmp(task_id(q, id), id)))
			write_check_files(q);
	}
	for_each_string_list_item(item, &q->covered) {
		struct contract *c = now(q, item->string);
		const char *got = yget(&kv, xstrfmt("condition-ids.%s", item->string));

		if (!strcmp(task_id(q, c->id), c->id) && (!got || strcmp(got, c->condition_id)))
			write_check_files(q);
		for (n = 0; n < c->nspecs; n++)
			string_list_insert(&want, task_of(task_id(q, c->id), n));
	}
	for (i = 0; yget(&kv, xstrfmt("results.%d", i)); i++) {
		const char *task = yget(&kv, xstrfmt("results.%d.task", i)),
			   *outcome = yget(&kv, xstrfmt("results.%d.outcome", i)),
			   *basis = yget(&kv, xstrfmt("results.%d.basis", i)),
			   *reason = yget(&kv, xstrfmt("results.%d.reason", i));
		struct strbuf block = STRBUF_INIT;

		if (!task || !string_list_has_string(&want, task) || string_list_has_string(&q->results, task))
			reject(128, fix, "bad format: '%s': task '%s' is unknown or given twice", q->result, task ? task : "");
		if (!outcome || (strcmp(outcome, "pass") && strcmp(outcome, "fail") && strcmp(outcome, "unknown")) ||
		    (!strcmp(outcome, "pass") && (!basis || !*basis)))
			reject(128, fix, "bad format: '%s': task '%s' has no outcome or basis", q->result, task);
		if (strcmp(outcome, "pass"))
			string_list_append(&failed, task);
		strbuf_addf(&block, "- task: %s\n  outcome: %s\n  basis: ", task, outcome);
		yaml_out_body(&block, basis ? basis : "", "    ");
		if (reason && *reason) {
			strbuf_addstr(&block, "  reason: ");
			yaml_out_body(&block, reason, "    ");
		}
		string_list_insert(&q->results, task)->util = strbuf_detach(&block, NULL);
	}
	for_each_string_list_item(item, &want)
		if (!string_list_has_string(&q->results, item->string))
			reject(128, fix, "bad format: '%s': task '%s' is missing", q->result, item->string);
	if (failed.nr) {
		for_each_string_list_item(item, &failed)
			fprintf(stderr, "%s\n", item->string);
		reject(1, "make the code meet the failed task(s) and stage it, then rerun without -r",
		       "check failed: %d task(s)", (int)failed.nr);
	}
}

/* spec 6.7.4: every provider scope of C is found in T; the refusal names what is not */
static void locate_all(struct request *q, struct contract *c, struct tree *t)
{
	struct string_list_item *item;
	char *where;

	for_each_string_list_item(item, &c->provider)
		switch (scope_locate(item->string, t, &where)) {
		case FOUND:
			break;
		case ABSTRACT:
			/* 3.3: the same command, the contract narrowed */
			reject(128, command_again(q, c->id, xstrfmt("%s=<scope>", c->id), NULL),
			       "bad scope: '%s' needs concrete scopes to be fulfilled", c->id);
		case NOBASE:
			/* the provider given again at this code takes new base blobs */
			reject(128, xstrfmt("ait future amend %s %s%s - -m <why>", c->id, strcmp(c->kind, "demand") ? "" : "- ",
					    quoted(scopes_bare(&c->provider))),
			       "repository lacks the base blob of '%s' ('%s')", c->id, scope_path(item->string));
		default:
			reject(128, NULL, "bad scope: '%s' of '%s' is not in the code", item->string, c->id);
		}
}

/* the names of IDs as messages give them: 'A', 'B' */
static char *names(struct string_list *ids)
{
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;

	for_each_string_list_item(item, ids)
		strbuf_addf(&sb, "%s'%s'", sb.len ? ", " : "", item->string);
	return strbuf_detach(&sb, NULL);
}

/* the fulfilments of the covered set (spec 6.7.5-2) into C's files, from the code commit */
static void record(struct request *q, struct checkin *ck)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &q->covered) {
		struct contract *c = changing(q, item->string);
		struct fulfilment f = { 0 };
		struct string_list_item *s;
		struct strbuf body = STRBUF_INIT;
		size_t n;

		f.path = xstrfmt("%s%sfulfill/%s-%s.md", c->group, *c->group ? "/" : "", c->id, q->rid);
		f.contract = c->id;
		f.condition_id = c->condition_id;
		f.by = xstrdup(agent);
		f.session = xstrdup(sid);
		f.commit = q->commit;
		f.outcome = xstrdup(!strcmp(c->kind, "drop") ? "withdrawn" : q->force ? "forced" : "pass");
		string_list_init_dup(&f.code);
		if (strcmp(c->kind, "drop"))
			locate_all(q, c, q->code);
		for_each_string_list_item(s, &c->provider)
			string_list_insert(&f.code, s->string)->util = code_entry(s->string, q->code);
		for (n = 0; n < c->nspecs && !q->force; n++) {
			struct string_list_item *r = string_list_lookup(&q->results, task_of(task_id(q, c->id), n));

			/* v14 8.4: a new contract's "+n" becomes its ID */
			if (r)
				strbuf_addf(&body, "- task: %s:%d%s", c->id, (int)n + 1,
					    strchr((char *)r->util, '\n'));
		}
		if (q->force || !body.len)
			strbuf_add(&body, q->msg.buf, q->msg.len);
		f.body = strbuf_detach(&body, NULL);
		string_list_append(&c->fulfilled, f.path);
		string_list_insert(&ck->put, f.path)->util = fulfil_text(&f);
		if (!strcmp(c->kind, "drop"))
			changing(q, c->target)->state = xstrdup("withdrawn");
	}
}

/*
 * spec 2.4, 6.4.1: the reason C, changed from OLD at T (NULL: a new
 * contract), may not stand, or CHANGE_OK. The clauses checked what they
 * change; the drafts of contract commit change anything, so every write
 * path asks here.
 */
static enum refusal change_refusal(struct request *q, struct contract *old, struct contract *c)
{
	struct string_list_item *item;
	int withdrawn = 0;
	size_t k;

	if (*c->id == 'X' && (!old || strcmp(c->target, old->target)) && !target_current(q, c))
		return TARGET;
	for (k = 0; old && k < old->fulfilled.nr; k++)
		if (k >= c->fulfilled.nr || strcmp(c->fulfilled.items[k].string, old->fulfilled.items[k].string))
			return FULFILMENT;
	if (c->fulfilled.nr > (old ? old->fulfilled.nr : 0) && !(q->code && string_list_has_string(&q->covered, c->id)))
		return FULFILMENT;
	if (!old)
		return CHANGE_OK;
	if (strcmp(c->path, old->path) || strcmp(c->kind, old->kind) || strcmp(c->creator, old->creator))
		return IDENTITY;
	for_each_string_list_item(item, &q->covered)
		withdrawn |= *item->string == 'X' && q->code && !strcmp(now(q, item->string)->target, c->id);
	if (strcmp(c->state, old->state) && strcmp(c->state, "cancelled") && !(withdrawn && !strcmp(c->state, "withdrawn")))
		return STATE;
	/* a trivial change keeps the fulfilments, which checked the old provider */
	if (string_list_has_string(&q->trivial, c->id) && strcmp(scopes_join(&c->provider), scopes_join(&old->provider)))
		return FORCE_PROVIDER;
	return CHANGE_OK;
}

/* spec 2.3.3: the IDs of every contract the request changed, from T */
static void stamp_all(struct request *q)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &q->changed)
		stamp(q, item->util, contract_get(q->b, item->string), string_list_has_string(&q->trivial, item->string));
}

/* every contract the request changed takes its transition (2.4) and goes in as its file */
static void settle(struct request *q, struct checkin *ck)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &q->changed) {
		struct contract *old = contract_get(q->b, item->string);
		enum refusal why = change_refusal(q, old, item->util);

		if (why)
			refuse(q, why, item->util, old);
		string_list_insert(&ck->put, ((struct contract *)item->util)->path)->util = contract_text(item->util);
	}
}

/* the judge of a request's checkin: everything again on the tip (spec 4.4-1) */
static void request_judge(struct checkin *ck)
{
	struct request *q = ck->data;
	struct string_list_item *item;

	q->b = ck->at;
	string_list_clear(&q->changed, 0);
	string_list_clear(&q->order, 0);
	string_list_clear(&q->alias, 0);
	string_list_clear(&q->covered, 0);
	string_list_clear(&q->fulfil, 0);
	string_list_clear(&q->trivial, 0);
	/* the contracts fulfill names exist before anything is done to them */
	for_each_string_list_item(item, &q->given) {
		if (!now(q, item->string))
			reject(128, NULL, "no such contract: '%s'", item->string);
		string_list_insert(&q->fulfil, item->string);
	}
	snprintf(q->rid, sizeof(q->rid), "%s", ck->rid);
	apply_clauses(q);
	for_each_string_list_item(item, &q->scopes)
		scopes_arg(&changing(q, item->string)->provider, item->util, q->x, 0, 0);
	for_each_string_list_item(item, &q->fulfil) {
		struct contract *c = now(q, item->string);

		if (ended(c))
			refuse(q, ENDED, c, NULL);
		/* 2.4: a drop is fulfilled while its target is current */
		if (*c->id == 'X' && !target_current(q, c))
			refuse(q, TARGET, c, NULL);
		cover(q, c, &q->covered);
	}
	/* the IDs first: a fulfilment records its contract's new condition-id */
	stamp_all(q);
	/* 6.7.2: the covered set fixed at step 0, with the condition-ids its results are for, holds to the end */
	for_each_string_list_item(item, &q->covered)
		if (q->fixed.nr && (!string_list_lookup(&q->fixed, item->string) ||
				    strcmp(string_list_lookup(&q->fixed, item->string)->util, now(q, item->string)->condition_id)))
			checkin_moved(ck);
	if (q->fixed.nr && q->fixed.nr != q->covered.nr)
		checkin_moved(ck);
	if (q->code)
		record(q, ck);
	settle(q, ck);
}

/* spec 6.7.5-3: ait fulfill as given, when the covered contracts are as they were at OLD */
static char *retry(struct checkin *ck, const char *old)
{
	struct request *q = ck->data;
	struct strvec args = STRVEC_INIT;
	struct string_list_item *item;
	int rc;

	/* a request's clauses are not made again by ait fulfill */
	if (strcmp(q->words.v[0], "fulfill"))
		return NULL;
	strvec_pushl(&args, "diff-tree", "--quiet", old, ck->tip, "--", NULL);
	for_each_string_list_item(item, &q->covered)
		strvec_push(&args, contract_get(q->b, item->string)->path);
	git_args(&args, NULL, NULL, &rc);
	return rc ? NULL : command_again(q, NULL, NULL, NULL);
}

/* spec 6.7.5: judge first, commit the staged code, then check in */
static void run_request(struct request *q, int fulfilling)
{
	struct checkin ck = { .msg = NULL };
	struct string_list_item *item;
	int rc;

	ck.branch = start(START_SESSION | START_WRITES | (fulfilling ? START_FULFILS : 0));
	q->x = index_tree();
	string_list_init_dup(&ck.put);
	ck.violation_hint = violation_hint;
	ck.retry_hint = retry;
	ck.data = q;
	newid(ck.rid, 'R');
	if (fulfilling) {
		/* step 0: everything judged on T as a checkin judges it (4.10 too), nothing changed */
		checkin_judge(&ck, request_judge);
		for_each_string_list_item(item, &q->covered)
			if (strcmp(now(q, item->string)->kind, "drop"))
				locate_all(q, now(q, item->string), q->x);
		if (!q->result && !q->force)
			write_check_files(q);
		if (q->result)
			read_results(q);
		for_each_string_list_item(item, &q->covered)
			string_list_insert(&q->fixed, item->string)->util = xstrdup(now(q, item->string)->condition_id);
		/* step 1: the staged code, committed by git */
		git_run(NULL, NULL, &rc, "diff --cached --quiet");
		if (rc) {
			struct strvec args = STRVEC_INIT;

			/* git's summary line, hooks and errors reach the user as they are (6.7.5-1, 6.7.8) */
			strvec_pushl(&args, "commit", "-m", q->msg.buf, NULL);
			if (git_pass(&args))
				reject(1, NULL, "commit failed; %s not fulfilled", names(&q->covered));
			not_recorded = xstrfmt("committed '%s' but %s not fulfilled",
					       line1(git("rev-parse --short HEAD")), names(&q->covered));
		} else {
			warning("nothing to commit; recording the code at HEAD ('%s') for %s",
				line1(git("rev-parse --short HEAD")), names(&q->covered));
			not_recorded = xstrfmt("%s not fulfilled", names(&q->covered));
		}
		snprintf(q->commit, sizeof(q->commit), "%s", line1(git("rev-parse HEAD")));
		q->code = tree_load(q->commit);
	}
	ck.msg = xstrfmt("%s%s", fulfilling ? "[FULFILL] " : "[CONTRACT] ", q->msg.buf);
	checkin_run(&ck, request_judge);
	not_recorded = NULL;
	for_each_string_list_item(item, &q->order) {
		struct string_list_item *a;
		const char *name = "";

		for_each_string_list_item(a, &q->alias)
			if (!strcmp(a->util, item->string))
				name = a->string;
		printf("new %s%s%s\n", item->string, *name ? " " : "", name);
	}
	for_each_string_list_item(item, &q->covered) {
		printf("fulfilled %s\n", item->string);
		if (*item->string == 'X')
			printf("withdrawn %s\n", now(q, item->string)->target);
	}
}

/* the options of a request, taken out of ARGV (spec 6.4) */
static void options(struct request *q, int argc, const char **argv, struct strvec *rest)
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
			i++;
		} else if ((!strcmp(argv[i], "-r") || !strcmp(argv[i], "--result")) && i + 1 < argc) {
			if (q->result)
				reject(129, NULL, "-r is given twice");
			q->result = argv[++i];
		} else if (!strcmp(argv[i], "--force")) {
			q->force = 1;
		} else if (!strcmp(argv[i], "-g") && i + 1 < argc) {
			q->group = argv[++i];
			if (!group_ok(q->group) || !*q->group)
				reject(129, NULL, "bad group '%s'", q->group);
		} else if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--partial")) {
			strvec_push(rest, argv[i]);
		} else if (*argv[i] == '-' && strcmp(argv[i], "-")) {
			reject(129, NULL, "unknown option '%s'", argv[i]);
		} else {
			strvec_push(rest, argv[i]);
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
	options(&q, argc, argv, &rest);
	fulfilling = read_clauses(&q, rest.nr, rest.v);
	/* a --force with no clause that fulfils is the trivial change of 6.4.1 */
	if (q.force && !fulfilling) {
		q.future_force = 1;
		q.force = 0;
	}
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
	options(&q, argc - 1, argv + 1, &rest);
	if (!rest.nr || q.group)
		reject(129, NULL, "fulfill takes K[=SCOPES]..., -r, --force, -m and -F");
	for (i = 0; i < rest.nr; i++) {
		const char *eq = strchr(rest.v[i], '=');
		char *id = eq ? xstrndup(rest.v[i], eq - rest.v[i]) : xstrdup(rest.v[i]);

		if (!id_ok(id, "DOX"))
			reject(128, NULL, "no such contract: '%s'", id);
		string_list_insert(&q.given, id);
		if (eq)
			string_list_insert(&q.scopes, id)->util = xstrdup(eq + 1);
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
	struct stat st;

	if (!lstat(full, &st))
		reject(1, NULL, "not allowed: '%s' already exists", rel);
	leading_dirs(full);
	write_state(full, text);
	printf("path %s\n", rel);
}

/* spec 6.5.1: the condition file template (v14 7.4.1a without inputs) */
static int contract_new_spec(const char *path, const char *engine)
{
	if (!condition_path_ok(path))
		reject(128, NULL, "bad path '%s'", path);
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
	struct spec s = { 0 };
	struct strbuf hint = STRBUF_INIT;
	const char *msg, *chosen = branch_find(&msg, &hint);
	struct string_list_item *item;
	int taken;

	if (!group_ok(group))
		reject(129, NULL, "bad group '%s'", group);
	names_load(&n);
	/* unique on the contract branch, or on every local one when none is chosen (6.5.2) */
	do {
		newid(id, kind_letter(kind));
		taken = chosen && contract_get(branch_load(chosen, NULL, NULL), id);
		for_each_string_list_item(item, &n.local)
			taken |= !chosen && contract_get(branch_load(item->string, NULL, NULL), id);
	} while (taken);
	c = contract_new(kind, group, id);
	if (strcmp(kind, "drop")) {
		string_list_append(&c->provider, "<scope>");
		if (!strcmp(kind, "demand"))
			string_list_append(&c->consumer, "<scope>");
		s.body = (char *)"<condition>\n";
		strvec_init(&s.command);
		string_list_init_dup(&s.success);
		c->specs = xmemdupz(&s, sizeof(s));
		c->nspecs = 1;
	} else {
		c->target = xstrdup("<ID>");
	}
	write_new_file(draft_path(c), contract_text(c));
	return 0;
}

/* spec 6.5.2a */
static int contract_checkout(int argc, const char **argv)
{
	struct branch *b;
	int i;

	b = branch_load(start(0), NULL, NULL);
	for (i = 1; i < argc; i++) {
		struct contract *c = contract_get(b, argv[i]);

		if (!c)
			reject(128, NULL, "no such contract: '%s'", argv[i]);
		write_new_file(draft_path(c), object(blob_at(b->tree, c->path), 1, NULL, NULL));
	}
	return 0;
}

static char *stage_index(void)
{
	mkdir(xstrfmt("%s/ait", gitdir), 0777);
	return xstrfmt("%s/ait/index", gitdir);
}

/* spec 6.5.3: drafts into the contract stage, as they are now */
static int contract_add(int argc, const char **argv)
{
	int i;

	if (argc < 2)
		reject(129, NULL, "add names the drafts");
	start(0);
	for (i = 1; i < argc; i++) {
		const char *rel;
		struct strbuf sb = STRBUF_INIT;
		char *err, *oid;

		if (!skip_prefix(argv[i], ".ait/", &rel) || !contract_path_id(rel))
			reject(129, NULL, "'%s' is not a draft: .ait/<group>/<ID>.yaml", argv[i]);
		if (strbuf_read_file(&sb, xstrfmt("%s/%s", top, argv[i]), 0) < 0)
			reject(128, NULL, "could not read '%s'", argv[i]);
		if (!contract_parse(sb.buf, rel, 1, &err))
			reject(128, NULL, "bad format: %s", err);
		oid = line1(git_run(sb.buf, NULL, NULL, "hash-object -w --stdin"));
		git_run(NULL, stage_index(), NULL, "update-index --add --cacheinfo %s", xstrfmt("100644,%s,%s", oid, rel));
		printf("add %s\n", rel);
	}
	return 0;
}

struct staged {
	struct request q;
	struct string_list drafts;	/* path -> text */
};

/*
 * v14 3.3: a drafted line range without its base blob keeps the tip's base
 * when the tip has the same range, and is fixed at HEAD otherwise
 */
static void pin_drafted(struct string_list *scopes, struct string_list *was)
{
	struct string_list_item *item, *w;
	struct tree *h = tree_load(head ? head : EMPTY_TREE);

	for_each_string_list_item(item, scopes) {
		char *err, *pinned = NULL;
		int a, b;

		if (scope_blob(item->string) || range_of(item->string, &a, &b) != 1)
			continue;
		if (was)
			for_each_string_list_item(w, was)
				if (!strcmp(xstrndup(w->string, strcspn(w->string, " ")), item->string))
					pinned = xstrdup(w->string);
		if (!pinned && !(pinned = scope_pin(item->string, h, &err)))
			reject(128, NULL, "bad scope: %s", err);
		free(item->string);
		item->string = pinned;
	}
}

/* spec 6.5.4: the staged drafts judged against the tip, each a change from T */
static void commit_judge(struct checkin *ck)
{
	struct staged *st = ck->data;
	struct request *q = &st->q;
	struct string_list_item *item;

	q->b = ck->at;
	snprintf(q->rid, sizeof(q->rid), "%s", ck->rid);
	string_list_clear(&q->changed, 0);
	string_list_clear(&q->order, 0);
	for_each_string_list_item(item, &st->drafts) {
		char *err;
		struct contract *d = contract_parse(item->util, item->string, 1, &err), *old = contract_get(q->b, d->id);
		size_t k;

		pin_drafted(&d->consumer, old ? &old->consumer : NULL);
		pin_drafted(&d->provider, old ? &old->provider : NULL);
		if (!old) {
			d->creator = xstrdup(agent);
			d->state = xstrdup("future");
			string_list_append(&q->order, d->id);
		} else {
			if (strcmp(d->change_id, old->change_id))
				reject(1, xstrfmt("ait contract checkout %s", d->id), "'%s' changed at '%s' since '.ait/%s' was written",
				       d->id, ck->branch, item->string);
			/* a draft's fulfilled is what it was checked out with: T's list, or the start of it */
			for (k = 0; k < d->fulfilled.nr; k++)
				if (k >= old->fulfilled.nr || strcmp(d->fulfilled.items[k].string, old->fulfilled.items[k].string))
					refuse(q, FULFILMENT, d, old);
			d->fulfilled = old->fulfilled;
			d->condition_id = old->condition_id;
			d->change_id = old->change_id;
			if (strcmp(contract_text(d), contract_text(old)) && ended(old))
				refuse(q, ENDED, old, NULL);
		}
		string_list_insert(&q->changed, d->id)->util = d;
		if (q->force)
			string_list_insert(&q->trivial, d->id);
	}
	stamp_all(q);
	settle(q, ck);
}

/* spec 6.5.4 */
static int contract_commit(int argc, const char **argv)
{
	struct staged st;
	struct strvec rest = STRVEC_INIT;
	struct checkin ck = { 0 };
	struct string_list_item *item;
	struct string_list lines = STRING_LIST_INIT_DUP;
	char *list;

	request_init(&st.q, argv - 1);
	string_list_init_dup(&st.drafts);
	options(&st.q, argc - 1, argv + 1, &rest);
	if (rest.nr || st.q.result || st.q.group)
		reject(129, NULL, "contract commit takes -m, -F and --force");
	ck.branch = start(START_SESSION | START_WRITES);
	st.q.x = index_tree();
	list = access(stage_index(), F_OK) ? xstrdup("") : git_run(NULL, stage_index(), NULL, "ls-files -s");
	string_list_split(&lines, list, "\n", -1);
	for_each_string_list_item(item, &lines) {
		char mode[8], oid[48];

		if (!*item->string)
			continue;
		sscanf(item->string, "%7s %47s", mode, oid);
		string_list_insert(&st.drafts, strchr(item->string, '\t') + 1)->util = object(oid, 1, NULL, NULL);
	}
	if (!st.drafts.nr)
		reject(1, "ait contract add <path>...", "nothing staged");
	string_list_init_dup(&ck.put);
	ck.msg = xstrfmt("[CONTRACT] %s", st.q.msg.buf);
	ck.data = &st;
	checkin_run(&ck, commit_judge);
	unlink(stage_index());
	for_each_string_list_item(item, &st.drafts) {
		char *id = contract_path_id(item->string);

		printf("%s %s\n", unsorted_string_list_has_string(&st.q.order, id) ? "new" : "change", id);
	}
	return 0;
}

/* the remote of a push or pull (spec 6.5.5-6): git's own choice */
static char *remote_of(const char *branch, const char *which, int argc, const char **argv)
{
	char *r;

	if (argc > 2)
		reject(129, NULL, "too many arguments");
	if (argc == 2)
		return xstrdup(argv[1]);
	r = line1(git("for-each-ref --format=%s %s", xstrfmt("%%(%s:remotename)", which),
		      xstrfmt("refs/heads/%s", branch)));
	return *r ? r : xstrdup("origin");
}

/* spec 6.5.5 */
static int contract_push(int argc, const char **argv)
{
	const char *b;
	struct strvec args = STRVEC_INIT;

	b = start(0);
	strvec_pushl(&args, "push", remote_of(b, "push", argc, argv), xstrfmt("refs/heads/%s:refs/heads/%s", b, b), NULL);
	return git_pass(&args);
}

struct pull {
	const char *upstream;		/* U, a remote-tracking ref */
	char *name;			/* U as git's merge message names it (fmt-merge-msg.c) */
};

static char *pull_hint(struct checkin *ck, const char *key)
{
	return xstrfmt("merge '%s' into '%s' and resolve the conflicts there (enter it with 'ait switch %s')",
		       ((struct pull *)ck->data)->name, ck->branch, ck->branch);
}

/*
 * spec 6.5.6-3, 4, judged again on every new T: U resolved once, so the
 * merge and its second parent are the same commit
 */
static void pull_judge(struct checkin *ck)
{
	struct string_list lines = STRING_LIST_INIT_DUP;
	struct strbuf paths = STRBUF_INIT;
	char u[48], *out;
	size_t i;
	int rc;

	object(xstrfmt("%s^{commit}", ((struct pull *)ck->data)->upstream), 0, u, NULL);
	if (is_ancestor(u, ck->tip)) {
		puts("Already up to date");
		exit(0);
	}
	if (is_ancestor(ck->tip, u)) {
		ck->ff = xstrdup(u);
		return;
	}
	ck->second_parent = xstrdup(u);
	out = git_run(NULL, NULL, &rc, "merge-tree --write-tree --name-only %s %s", ck->tip, u);
	ck->tree = line1(out);
	if (!rc)
		return;
	string_list_split(&lines, out, "\n", -1);
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

	ck.branch = start(START_SESSION | START_WRITES);
	strvec_pushl(&args, "fetch", "-q", remote_of(ck.branch, "upstream", argc, argv), NULL);
	if (git_pass(&args))
		return 1;
	if (!(p.upstream = upstream_of(ck.branch)))
		reject(128, NULL, "'%s' has no upstream", ck.branch);
	p.name = line1(git("rev-parse --abbrev-ref %s", p.upstream));
	string_list_init_dup(&ck.put);
	ck.msg = xstrfmt("[CONTRACT] merge '%s'", p.name);
	ck.refusal = xstrfmt("merging '%s' breaks contract rules: ", p.name);
	ck.violation_hint = pull_hint;
	ck.data = &p;
	checkin_run(&ck, pull_judge);
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
