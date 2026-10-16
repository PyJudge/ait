/*
 * ait status (spec 6.3) and subscriptions and notification (6.8, 9): what is
 * shown and sent is computed from the branch each time (rule 1).
 */
#include "ait.h"
#include <signal.h>
#include <time.h>

/* spec 6.3: a scope argument as a git pathspec; git knows no line range and reads "/" outside the repository */
static char *pathspec_of(const struct scope *arg)
{
	if (arg->kind == SCOPE_ROOT)
		return xstrdup(":/");
	return xstrfmt(":(top,literal)%s", arg->path);
}

/* spec 6.3-5: C's block, judged in X's judgment J; G is where its scopes are in X */
static void block(struct branch *b, struct judgment *j, struct contract *c, const struct contract_geometry *g,
		  int touch, const char *me)
{
	const struct judgment_result *result = judgment_get(j, c);
	enum verdict v = result->verdict;
	struct string_list_item *item;
	size_t i;
	int demand = !strcmp(c->kind, "demand"), held = 0;

	printf("id %s\nkind %s\ngroup %s\nstate %s\ncreator %s\n", c->id, c->kind, *c->group ? c->group : "-",
	       verdict_word(v), c->creator);
	if (demand) {
		char *display = geometry_text(g, 0, NULL);

		printf("consumer %s\n", display);
		free(display);
	}
	if (!strcmp(c->kind, "drop")) {
		printf("target %s\n", c->target);
	} else {
		char *display = geometry_text(g, 1, result->matches_any);

		printf("provider %s\n", display);
		free(display);
	}
	for (i = 0; i < c->nspecs; i++)
		printf("spec %s %.*s\n", c->specs[i].engine ? "exec" : "prompt", (int)strcspn(c->specs[i].body, "\n"),
		       c->specs[i].body);
	for (i = 0; i < 2; i++) {
		struct string_list related = STRING_LIST_INIT_DUP;

		undertakings(&j->view, c, i ? 'p' : 'w', &related);
		held |= related.nr > 0;
		for_each_string_list_item(item, &related) {
			struct contract *other = item->util;

			printf("%s %s %s %s %s\n", demand ? "undertaken-by" : "undertakes", other->id,
			       verdict_word(judgment_get(j, other)->verdict), other->creator, i ? "partial" : "whole");
		}
		string_list_clear(&related, 0);
	}
	for_each_string_list_item(item, &c->fulfilled) {
		struct string_list_item *f = string_list_lookup(&b->fulfils, item->string);
		struct fulfilment *ful = f ? f->util : NULL;
		const char *rid = strrchr(item->string, '-') + 1;

		if (ful)
			printf("fulfilled %.13s %s %s %s %s\n", rid, ful->by, ful->outcome,
			       strcmp(ful->condition_id, c->condition_id) ? "old" : "valid", ful->commit);
	}
	if (touch)
		puts("touch");
	putchar('\n');
	if (me && !strcmp(c->creator, me) && promise(c, v) && strcmp(c->kind, "drop"))
		suggest("%s", hint_command("ait fulfill %s -m <why>", c->id));
	if (demand && live(c) && !held)
		suggest("%s", hint_command("ait future offer %s <files> as o undertake %s o -m <why>",
					  quoted(scopes_text(&c->provider, 0)), c->id));
}

/* spec 6.3-4: a violation (4.10) names ID */
static int violates(struct string_list *bad, const char *id)
{
	struct string_list_item *item;

	for_each_string_list_item(item, bad) {
		struct violation *v = item->util;

		if (!strcmp(v->a, id) || (v->kind == PAIR && !strcmp(v->b, id)))
			return 1;
	}
	return 0;
}

/* spec 6.3 */
int cmd_status(int argc, const char **argv)
{
	struct strvec args = STRVEC_INIT;
	struct string_list pins = STRING_LIST_INIT_DUP, bad = STRING_LIST_INIT_DUP;
	struct scope_list scopes = { 0 };
	struct scope_result *positions;
	struct judgment current, original;
	struct contract_view view;
	struct string_list_item *item;
	struct branch *b;
	struct tree *x, *h;
	const char *name, *me;
	int i, rc;

	for (i = 1; i < argc; i++) {
		struct scope q;

		if (scope_parse(&q, argv[i], 1, 1, SCOPE_ARGUMENT))
			reject(129, NULL, "status takes scopes only: '%s'", argv[i]);
		scope_append(&scopes, &q);
		scope_clear(&q);
	}
	b = start(START_WARNS);
	name = b->name;
	printf("On contract branch %s%s\n", name, branch_only() ? " (the only one)" : "");
	x = index_tree();
	h = head ? tree_load(head) : NULL;
	view = (struct contract_view){ b, NULL, NULL };
	judgment_init(&current, &view, x);
	judgment_init(&original, &view, h);
	positions = xcalloc(scopes.nr, sizeof(*positions));
	strvec_pushl(&args, "--no-literal-pathspecs", "status", NULL);
	if (argc > 1)
		strvec_push(&args, "--");
	for (i = 1; i < argc; i++) {
		struct scope *q = &scopes.items[i - 1];
		char *err, *pathspec = pathspec_of(q);

		if (q->kind == SCOPE_RANGE && scope_pin(q, x, &err))
			reject(128, NULL, "bad scope: %s", err);
		scope_result_at(q, x, &positions[i - 1]);
		if (!unsorted_string_list_has_string(&pins, pathspec))
			string_list_append(&pins, pathspec);
		free(pathspec);
	}
	for_each_string_list_item(item, &pins)
		strvec_push(&args, item->string);
	if ((rc = git_pass(&args)))
		goto out;
	putchar('\n');
	me = session(0) ? agent : NULL;
	invariants(&view, b, NULL, &bad);
	for_each_string_list_item(item, &bad)
		warning("%s", violation_words(item->util));
	/* the contracts are in ID order, the order of the blocks */
	for_each_string_list_item(item, &b->contracts) {
		struct contract *c = item->util;
		struct contract_geometry g;
		int pick = 0, touch = 0;

		if (scopes.nr) {
			size_t n;

			for (n = 0; n < scopes.nr; n++)
				pick |= contract_overlaps(c, x, &scopes.items[n], &positions[n], 0);
			touch = pick && h && judgment_touched(&original, c, x);
		} else {
			enum verdict v = judgment_get(&current, c)->verdict;
			int mine = me && !strcmp(c->creator, me);

			touch = h && judgment_touched(&original, c, x);
			pick = (mine && promise(c, v)) || (mine && !strcmp(c->kind, "demand") && v == V_CURRENT) ||
			       v == V_UNVERIFIED || touch || violates(&bad, c->id);
		}
		if (!pick)
			continue;
		geometry_init(&g, c, x);
		block(b, &current, c, &g, touch, me);
		geometry_clear(&g);
	}
out:
	judgment_clear(&current);
	judgment_clear(&original);
	free(positions);
	scopes_clear(&scopes);
	string_list_clear(&pins, 0);
	violations_clear(&bad);
	strvec_clear(&args);
	tree_free(h);
	tree_free(x);
	branch_free(b);
	return rc;
}

/* v14 4.3: keys go to the end of .seen, which is only added to */
static void append_seen(const char *s, const char *keys)
{
	int fd;
	char *path;

	if (!keys || !*keys)
		return;
	path = session_path(s, ".seen");
	if ((fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0666)) < 0 ||
	    write_in_full(fd, keys, strlen(keys)) < 0 || close(fd))
		listen_write_error(path);
	free(path);
}

/* spec 9.1: the tip lines, a snapshot of the ait refs, local and remote-tracking (2.1.2) */
static void tips_now(struct string_list *out)
{
	struct string_list_item *t;
	struct names n;

	names_load(&n);
	for_each_string_list_item(t, &n.tips)
		string_list_append(out, t->string);
	names_clear(&n);
}

/*
 * spec 9.3: the receiving state V: the subscriptions, HEAD and the checkout,
 * read with the session's LINES (under its lock)
 */
struct receiving_state {
	struct string_list lines;
	char head_oid[48];
	char *checkout;
	char *version;
};

static void receiving(const char *s, struct receiving_state *v)
{
	struct strbuf key = STRBUF_INIT;
	struct string_list_item *item;

	memset(v, 0, sizeof(*v));
	string_list_init_dup(&v->lines);
	session_lines(s, &v->lines);
	for_each_string_list_item(item, &v->lines)
		if (starts_with(item->string, "sub "))
			strbuf_addf(&key, "%s\n", item->string);
	object_oid("HEAD^{commit}", v->head_oid);
	v->checkout = checkout_read();
	/* Absence and an empty file have different branch-selection meanings. */
	strbuf_addf(&key, "head %s\ncheckout %d %s", v->head_oid, !!v->checkout, v->checkout ? v->checkout : "");
	v->version = strbuf_detach(&key, NULL);
}

static void receiving_clear(struct receiving_state *v)
{
	string_list_clear(&v->lines, 0);
	free(v->checkout);
	free(v->version);
}

/*
 * spec 9.1: the session's tip lines become TIPS (a snapshot now when NULL),
 * and the keys SKIPPED go to .seen, unless V is no longer AS_OF (v14 9.5-4)
 */
void session_tips(const char *s, const char *as_of, struct string_list *tips, const char *skipped)
{
	struct receiving_state v;
	struct string_list keep = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	int fd = lock_session(s);

	receiving(s, &v);
	if (!as_of || !strcmp(v.version, as_of)) {
		for_each_string_list_item(item, &v.lines)
			if (!starts_with(item->string, "tip "))
				string_list_append(&keep, item->string);
		if (tips)
			for_each_string_list_item(item, tips)
				string_list_append(&keep, item->string);
		else
			tips_now(&keep);
		append_seen(s, skipped);
		session_write(s, &keep);
	}
	close(fd);
	receiving_clear(&v);
	string_list_clear(&keep, 0);
}

struct subscription {
	char *id;
	struct scope scope;
};
struct subscription_list {
	struct subscription *items;
	size_t nr;
	size_t alloc;
};

static void subscription_clear(struct subscription *sub)
{
	free(sub->id);
	scope_clear(&sub->scope);
}

/* Session lines cross the same stored-scope boundary once per operation/round. */
static int subscription_read(struct subscription *sub, const char *line)
{
	const char *value;
	char *err;

	memset(sub, 0, sizeof(*sub));
	if (skip_prefix(line, "sub id ", &value)) {
		sub->id = xstrdup(value);
		return 1;
	}
	if (!skip_prefix(line, "sub scope ", &value))
		return 0;
	err = scope_parse(&sub->scope, value, 1, 1, SCOPE_STORED);
	if (!err)
		return 1;
	free(err);
	return 0;
}

static void subscriptions_read(struct subscription_list *out, const struct string_list *lines)
{
	const struct string_list_item *item;

	for_each_string_list_item(item, lines) {
		struct subscription sub;

		if (!subscription_read(&sub, item->string))
			continue;
		ALLOC_GROW(out->items, out->nr + 1, out->alloc);
		out->items[out->nr++] = sub;
	}
}

static void subscriptions_clear(struct subscription_list *subs)
{
	size_t i;

	for (i = 0; i < subs->nr; i++)
		subscription_clear(&subs->items[i]);
	free(subs->items);
}

static char *sub_line(struct branch *b, const char *arg, struct subscription *sub, struct tree **index)
{
	char *err, *text, *line;

	memset(sub, 0, sizeof(*sub));
	if (id_ok(arg, "DOX")) {
		if (!contract_get(b, arg))
			no_contract(arg);
		sub->id = xstrdup(arg);
		return xstrfmt("sub id %s", arg);
	}
	if ((err = scope_parse(&sub->scope, arg, 1, 1, SCOPE_ARGUMENT)))
		reject(128, NULL, "bad scope: %s", err);
	if (sub->scope.kind == SCOPE_RANGE) {
		if (!*index)
			*index = index_tree();
		if (scope_pin(&sub->scope, *index, &err))
			reject(128, NULL, "bad scope: %s", err);
	}
	text = scope_text(&sub->scope, 1);
	line = xstrfmt("sub scope %s", text);
	free(text);
	return line;
}

static char *sub_display(const struct subscription *sub, struct tree *x)
{
	char *text, *line;

	if (sub->id)
		return xstrfmt("sub id %s", sub->id);
	if (!x && (sub->scope.kind == SCOPE_RANGE || sub->scope.kind == SCOPE_DIR || sub->scope.kind == SCOPE_ROOT))
		text = scope_text(&sub->scope, 0);
	else if (!x) {
		struct tree *h = tree_load(head ? head : EMPTY_TREE);

		text = scope_display(&sub->scope, h);
		tree_free(h);
	} else {
		text = scope_display(&sub->scope, x);
	}
	line = xstrfmt("sub scope %s", text);
	free(text);
	return line;
}

static void save_subscriptions(struct string_list *lines, struct strbuf *changes, FILE *out, int fd)
{
	session_write(sid, lines);
	close(fd);
	fputs(changes->buf, out);
	strbuf_release(changes);
}

static void subscribe(struct branch *b, int argc, const char **argv, FILE *out)
{
	struct string_list lines = STRING_LIST_INIT_DUP;
	struct strbuf added = STRBUF_INIT;
	struct tree *index = NULL;
	int i, fd;

	fd = lock_session(sid);
	session_lines(sid, &lines);
	for (i = 1; i < argc; i++) {
		struct subscription sub;
		char *line = sub_line(b, argv[i], &sub, &index);

		if (!unsorted_string_list_has_string(&lines, line)) {
			char *display = sub_display(&sub, NULL);

			string_list_append(&lines, line);
			strbuf_addf(&added, "%s\n", display);
			free(display);
		}
		free(line);
		subscription_clear(&sub);
	}
	save_subscriptions(&lines, &added, out, fd);
	string_list_clear(&lines, 0);
	tree_free(index);
}

int cmd_sub(int argc, const char **argv)
{
	struct branch *b;

	if (argc < 2)
		reject(129, NULL, "sub names what to subscribe to");
	b = start(START_SESSION);
	subscribe(b, argc, argv, stdout);
	branch_free(b);
	return 0;
}

int cmd_unsub(int argc, const char **argv)
{
	struct string_list lines = STRING_LIST_INIT_DUP, keep = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct tree *h = tree_load(head ? head : EMPTY_TREE);
	struct strbuf removed = STRBUF_INIT;
	int fd, i;

	session(1);
	fd = lock_session(sid);
	session_lines(sid, &lines);
	for_each_string_list_item(item, &lines) {
		struct subscription sub;
		int parsed = subscription_read(&sub, item->string);
		char *display = parsed ? sub_display(&sub, h) : xstrdup(item->string);
		int drop = 0;

		for (i = 1; i < argc; i++)
			drop |= (parsed && sub.id && !strcmp(sub.id, argv[i])) ||
				(starts_with(display, "sub scope ") && !strcmp(display + 10, argv[i]));
		if (starts_with(item->string, "sub ") && (argc == 1 || drop))
			strbuf_addf(&removed, "un%s\n", display);
		else
			string_list_append(&keep, item->string);
		free(display);
		if (parsed)
			subscription_clear(&sub);
	}
	save_subscriptions(&keep, &removed, stdout, fd);
	string_list_clear(&lines, 0);
	string_list_clear(&keep, 0);
	tree_free(h);
	return 0;
}

static volatile sig_atomic_t stopping;

static void stop(int sig)
{
	stopping = sig;
}

/* spec 9.7: category order, then ID or DISPLAY order within each category. */
enum reason_kind {
	REASON_FUTURE_DEMAND,
	REASON_FUTURE_OFFER,
	REASON_FUTURE_DROP,
	REASON_DEMAND,
	REASON_SUB_ID,
	REASON_SUB_SCOPE,
	REASON_NR
};

struct event_reasons {
	struct string_list values[REASON_NR];
};

static const char *const reason_words[REASON_NR] = {
	[REASON_FUTURE_DEMAND] = "future demand",
	[REASON_FUTURE_OFFER] = "future offer",
	[REASON_FUTURE_DROP] = "future drop",
	[REASON_DEMAND] = "demand",
	[REASON_SUB_ID] = "sub id",
	[REASON_SUB_SCOPE] = "sub scope"
};

static void add_reason(struct event_reasons *reasons, enum reason_kind kind, const char *value)
{
	string_list_insert(&reasons->values[kind], value);
}

static enum reason_kind future_reason(const struct contract *c)
{
	if (!strcmp(c->kind, "demand"))
		return REASON_FUTURE_DEMAND;
	if (!strcmp(c->kind, "offer"))
		return REASON_FUTURE_OFFER;
	return REASON_FUTURE_DROP;
}

static void reason_lines(struct event_reasons *reasons, struct strbuf *out)
{
	enum reason_kind kind;
	struct string_list_item *value;

	for (kind = 0; kind < REASON_NR; kind++) {
		for_each_string_list_item(value, &reasons->values[kind])
			strbuf_addf(out, "reason %s %s\n", reason_words[kind], value->string);
		string_list_clear(&reasons->values[kind], 0);
	}
}

/* spec 9.5: why the session's agent receives C (OLD at the first parent): relations at MINE and HEAD H */
static void reasons(struct judgment *judgments, struct contract *c, struct contract *old,
		    struct subscription_list *subs, const struct scope_result *positions, struct event_reasons *out)
{
	struct branch *mine = judgments->view.at;
	struct string_list related = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	size_t i;
	struct contract *k = contract_get(mine, c->id);
	enum verdict v = k && !strcmp(k->creator, agent) ? judgment_get(judgments, k)->verdict : V_FUTURE;

	if (k && !strcmp(k->creator, agent) && promise(k, v))
		add_reason(out, future_reason(k), k->id);
	if (k) {
		undertakings(&judgments->view, k, 'w', &related);
		undertakings(&judgments->view, k, 'p', &related);
	}
	for_each_string_list_item(item, &mine->contracts) {
		struct contract *m = item->util;
		int counterpart;

		if (!k || strcmp(m->creator, agent))
			continue;
		counterpart = string_list_has_string(&related, m->id) ||
			      (!strcmp(m->kind, "drop") && !strcmp(m->target, k->id));
		if (counterpart && promise(m, judgment_get(judgments, m)->verdict))
			add_reason(out, future_reason(m), m->id);
	}
	string_list_clear(&related, 0);
	if (k && !strcmp(k->kind, "demand") && !strcmp(k->creator, agent) && v == V_CURRENT)
		add_reason(out, REASON_DEMAND, k->id);
	for (i = 0; i < subs->nr; i++) {
		const struct subscription *sub = &subs->items[i];

		if (sub->id) {
			if (!strcmp(sub->id, c->id))
				add_reason(out, REASON_SUB_ID, c->id);
			continue;
		}
		/* E and its parent may have the same ID as MINE, but are different values. */
		if (contract_overlaps(c, judgments->tree, &sub->scope, &positions[i], 1) ||
		    (old && contract_overlaps(old, judgments->tree, &sub->scope, &positions[i], 1))) {
			char *display = scope_result_text(&sub->scope, &positions[i]);

			add_reason(out, REASON_SUB_SCOPE, display);
			free(display);
		}
	}
}

/* spec 9.4: the ACTION lines of C changed from OLD */
static void actions(struct contract *c, struct contract *old, struct strbuf *out)
{
	if (!old) {
		strbuf_addf(out, "contract %s create\n", c->id);
	} else {
		if (strcmp(c->condition_id, old->condition_id))
			strbuf_addf(out, "contract %s amend\n", c->id);
		else if (strcmp(c->change_id, old->change_id))
			strbuf_addf(out, "contract %s edit\n", c->id);
		if (strcmp(c->state, old->state))
			strbuf_addf(out, "contract %s %s\n", c->id, c->state);
	}
	if (c->fulfilled.nr > (old ? old->fulfilled.nr : 0))
		strbuf_addf(out, "contract %s fulfil\n", c->id);
}

/*
 * spec 9.4-9.7 for commit E on the branch NAME: its keys (each "KEY PATH
 * BLOB" line), and the event the session receives, if any
 */
static void event(struct judgment *judgments, const char *e, struct string_list *watched, struct subscription_list *subs,
		  const struct scope_result *positions, struct string_list *seen, struct strbuf *keys, struct strbuf *payload)
{
	struct branch *mine = judgments->view.at;
	const char *name = mine->name;
	struct branch *at = branch_load(name, e, e), *before = NULL;
	struct string_list files = STRING_LIST_INIT_DUP;
	struct string_list_item *f;
	struct event_reasons why;
	struct strbuf reason_text = STRBUF_INIT, acts = STRBUF_INIT, refs = STRBUF_INIT;
	enum reason_kind kind;
	char *parent;
	int rc, in_head = 1;

	for (kind = 0; kind < REASON_NR; kind++)
		string_list_init_dup(&why.values[kind]);
	{
		char *first_parent = xstrfmt("%s^1", e);

		parent = line1(git_run(NULL, NULL, &rc, "rev-parse", "-q", "--verify", first_parent));
		free(first_parent);
	}
	if (!rc)
		before = branch_load(name, parent, parent);
	git_records(&files, NULL, NULL, "diff-tree", "-r", "-z", "--root", "--name-only", "--no-commit-id",
		"--diff-merges=first-parent", e);
	for_each_string_list_item(f, &files) {
		const char *blob = blob_at(at->tree, f->string);
		char *id = contract_path_id(f->string);
		struct string_list_item *ful = string_list_lookup(&at->fulfils, f->string);
		struct contract *c = id ? contract_get(at, id) : NULL, *old;
		char *key;

		free(id);

		if (!blob || (!ful && (!c || strcmp(c->path, f->string))))
			continue;
		key = ful ? xstrfmt("fulfil %s %s", ((struct fulfilment *)ful->util)->contract, blob) :
			    xstrfmt("version %s %s", c->id, blob);
		if (string_list_has_string(seen, key)) {
			free(key);
			continue;
		}
		strbuf_addf(keys, "%s\n", key);
		free(key);
		/* in-head: every key is in the local branch's tip */
		in_head &= blob_at(mine->tree, f->string) && !strcmp(blob_at(mine->tree, f->string), blob);
		if (ful)
			continue;
		old = before ? contract_get(before, c->id) : NULL;
		reasons(judgments, c, old, subs, positions, &why);
		actions(c, old, &acts);
		if (!strcmp(c->kind, "drop") && contract_get(at, c->target))
			reasons(judgments, contract_get(at, c->target), before ? contract_get(before, c->target) : NULL,
				subs, positions, &why);
	}
	reason_lines(&why, &reason_text);
	if (!reason_text.len)
		goto out;
	for_each_string_list_item(f, watched) {
		if (is_ancestor(e, f->util))
			strbuf_addf(&refs, "ref %s\n", f->string);
	}
	strbuf_addf(payload, "ait-event 1\ncommit %s\nrecipient %s\n%s%sin-head %s\n%s\n", e, sid, refs.buf,
		    reason_text.buf, in_head ? "yes" : "no", acts.buf);
	{
		char *patch = git("show", "--no-color", "--no-ext-diff", "--no-textconv", "--no-show-signature",
		"--diff-merges=first-parent", "--format=fuller", "--stat", "--patch", e);

		strbuf_addstr(payload, patch);
		free(patch);
	}
	strbuf_addstr(payload, "ait-event-end\n");
out:
	free(parent);
	string_list_clear(&files, 0);
	strbuf_release(&reason_text);
	strbuf_release(&acts);
	strbuf_release(&refs);
	branch_free(before);
	branch_free(at);
}

/* Watch only the ref name/OID pair present in the captured N. */
static const char *watch_ref(struct names *n, struct string_list *watched, const char *ref)
{
	struct string_list_item *item;

	for_each_string_list_item(item, &n->tips)
		if (!strcmp(item->string + 45, ref)) {
			struct string_list_item *w = string_list_insert(watched, ref);

			w->util = xstrndup(item->string + 4, 40);
			return w->util;
		}
	return NULL;
}

/*
 * one round of spec 9.6 (v14 9.5) for the session: the receiving state V and
 * the snapshot N first, then the candidates; a key goes out once; the tips
 * and the keys of the events skipped are written at the end, if V held
 */
static void deliver(int *warned)
{
	struct receiving_state v;
	struct names n;
	struct branch *mine = NULL;
	struct tree *h = NULL;
	struct judgment judgments = { 0 };
	struct scope_result *positions = NULL;
	size_t i;
	struct subscription_list subs = { 0 };
	struct string_list seen = STRING_LIST_INIT_DUP, watched = STRING_LIST_INIT_DUP,
			   candidates = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct strbuf in = STRBUF_INIT, seenbuf = STRBUF_INIT, skipped = STRBUF_INIT, hint = STRBUF_INIT,
		      msg = STRBUF_INIT;
	const char *name, *up, *tip;
	int rc, fd;

	/* Git's collect→consume pattern: V, refs and upstream are inputs to this round. */
	fd = lock_session(sid);
	receiving(sid, &v);
	names_load(&n);
	name = branch_find(&n, v.checkout, &msg, &hint, NULL);
	close(fd);
	if (!name && !*warned)
		warning("%s", msg.buf);
	*warned = !name;
	if (!name)
		goto out;
	{
		char *ref = xstrfmt("refs/heads/%s", name);

		tip = watch_ref(&n, &watched, ref);
		free(ref);
	}
	if (!tip)
		BUG("captured local branch has no tip");
	if ((up = names_upstream(&n, name)))
		watch_ref(&n, &watched, up);
	mine = branch_load(name, tip, tip);
	h = tree_load(*v.head_oid ? v.head_oid : EMPTY_TREE);
	subscriptions_read(&subs, &v.lines);
	judgment_init(&judgments, &(struct contract_view){ mine, NULL, NULL }, h);
	positions = xcalloc(subs.nr, sizeof(*positions));
	for (i = 0; i < subs.nr; i++)
		if (!subs.items[i].id)
			scope_result_at(&subs.items[i].scope, h, &positions[i]);
	for_each_string_list_item(item, &v.lines) {
		if (starts_with(item->string, "tip "))
			strbuf_addf(&in, "^%.40s\n", item->string + 4);
	}
	{
		char *path = session_path(sid, ".seen");

		strbuf_read_file(&seenbuf, path, 0);
		free(path);
	}
	string_list_split(&seen, seenbuf.buf, "\n", -1);
	string_list_sort(&seen);
	for_each_string_list_item(item, &watched)
		strbuf_addf(&in, "%s\n", (char *)item->util);
	string_list_split(&candidates, git_run(in.buf, NULL, NULL, "rev-list", "--reverse", "--topo-order",
		"--ignore-missing", "--stdin"), "\n", -1);
	for_each_string_list_item(item, &candidates) {
		const char *revision = item->string;
		struct strbuf keys = STRBUF_INIT, payload = STRBUF_INIT;
		struct string_list new_keys = STRING_LIST_INIT_DUP;
		struct string_list_item *key;
		struct receiving_state now;
		int stale = 0;
		char *sender;

		if (!*revision)
			continue;
		event(&judgments, revision, &watched, &subs, positions, &seen, &keys, &payload);
		/* a key goes out once, in this round too */
		string_list_split(&new_keys, keys.buf, "\n", -1);
		for_each_string_list_item(key, &new_keys)
			if (*key->string)
				string_list_insert(&seen, key->string);
		/* 9.4: the session's own request is skipped, its keys kept like any skipped event's (v14 9.5-3) */
		sender = payload.len ? git("log", "-1", "--format=%(trailers:key=Ait-Session,valueonly)", revision) : NULL;
		rc = sender && !strcmp(line1(sender), sid);
		free(sender);
		if (!payload.len || rc) {
			strbuf_add(&skipped, keys.buf, keys.len);
			goto event_done;
		}
		/* v14 9.5-3: V as it was, or the round stops and is judged again */
		fd = lock_session(sid);
		receiving(sid, &now);
		rc = strcmp(now.version, v.version);
		receiving_clear(&now);
		close(fd);
		if (rc) {
			stale = 1;
			goto event_done;
		}
		/* 5.7: a reader gone ends listen as git ends on EPIPE; any other failure is a write error */
		if (fputs(payload.buf, stdout) == EOF || fflush(stdout)) {
			if (errno == EPIPE) {
				signal(SIGPIPE, SIG_DFL);
				raise(SIGPIPE);
			}
			stdout_write_error();
		}
		append_seen(sid, keys.buf);
	event_done:
		string_list_clear(&new_keys, 0);
		strbuf_release(&keys);
		strbuf_release(&payload);
		if (stale)
			goto out;
	}
	session_tips(sid, v.version, &n.tips, skipped.buf);
out:
	judgment_clear(&judgments);
	/* a round's blob pairs are not kept for the next (9.6) */
	blob_pairs_clear();
	free(positions);
	string_list_clear(&candidates, 0);
	branch_free(mine);
	tree_free(h);
	receiving_clear(&v);
	names_clear(&n);
	subscriptions_clear(&subs);
	string_list_clear(&seen, 0);
	string_list_clear(&watched, 1);
	strbuf_release(&in);
	strbuf_release(&seenbuf);
	strbuf_release(&skipped);
	strbuf_release(&hint);
	strbuf_release(&msg);
}

/* the listen loop (U51): a round when the hook adds a line to the wake file, and every EVERY seconds */
static void listen_loop(int fetch)
{
	char *wake = state_path("wake");
	struct stat st;
	time_t polled = 0, fetched = 0;
	off_t seen_wake = -1;
	/* 9.8: polling fills in what no hook of ait's told, every second when there is none */
	int every = hook_installed() ? 30 : 1, warned = 0;

	while (!stopping) {
		off_t size = stat(wake, &st) ? -1 : st.st_size;

		if (fetch && time(NULL) - fetched >= fetch) {
			struct string_list remotes = STRING_LIST_INIT_DUP;
			struct string_list_item *r;
			char *names = git("remote");
			int rc;

			/* spec 6.8: each remote on its own; one that fails does not stop the others */
			string_list_split(&remotes, names, "\n", -1);
			free(names);
			for_each_string_list_item(r, &remotes) {
				if (!*r->string)
					continue;
				free(git_run(NULL, NULL, &rc, "fetch", "-q", "--", r->string));
				if (rc) {
					warning("fetch failed: %s", git_error);
					suggest("check the remotes");
				}
			}
			string_list_clear(&remotes, 0);
			fetched = time(NULL);
		}
		if (size != seen_wake || time(NULL) - polled >= every) {
			seen_wake = size;
			polled = time(NULL);
			deliver(&warned);
		}
		usleep(100000);
	}
}

/* v14 7.2.5 */
int cmd_listen(int argc, const char **argv)
{
	struct branch *b;
	struct strvec args = STRVEC_INIT;
	int i, fetch = 0, sfd, fd;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--fetch")) {
			char *end;
			long seconds;

			if (fetch)
				reject(129, NULL, "--fetch is given twice");
			if (++i == argc)
				reject(129, NULL, "option '--fetch' requires a value");
			errno = 0;
			seconds = strtol(argv[i], &end, 10);
			if (errno == ERANGE || *end || seconds <= 0 || seconds > INT_MAX)
				reject(129, NULL, "--fetch takes seconds: '%s'", argv[i]);
			fetch = seconds;
		} else if (*argv[i] == '-') {
			reject(129, NULL, "unknown option '%s'", argv[i]);
		} else {
			strvec_push(&args, argv[i]);
		}
	}
	b = start(START_SESSION);
	if (args.nr) {
		struct strvec sub = STRVEC_INIT;

		strvec_push(&sub, "sub");
		strvec_pushv(&sub, args.v);
		subscribe(b, sub.nr, sub.v, stderr);
	}
	branch_free(b);
	strvec_clear(&args);
	/* v14 4.5: the listener's lock, taken and stamped with the pid under the session's lock */
	sfd = lock_session(sid);
	if ((fd = lock_path(session_path(sid, ".listen"), 1)) < 0)
		reject(128, NULL, "cannot run now: a listener is already running for session '%s'", sid);
	if (ftruncate(fd, 0) == 0)
		dprintf(fd, "%d\n", (int)getpid());
	close(sfd);
	signal(SIGINT, stop);
	signal(SIGTERM, stop);
	signal(SIGPIPE, SIG_IGN);
	listen_loop(fetch);
	return 0;
}
