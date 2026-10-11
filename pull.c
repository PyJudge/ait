/* contract pull: the version DAG and Git history, spec 7.12 */
#include "ait.h"
#include "strvec.h"
#include "quote.h"

struct version {
	const char *oid;
	struct record *r;
	struct string_list ancestors;
	char *body;
	int walked;
	int accident;	/* -1 unknown, 0 valid, 1 accident, 2 visiting */
};
struct selection {
	const char *id;
	const char *pick;
	struct version *mine;
	struct version *result;
	struct string_list candidates;
	struct string_list latest;
	struct string_list remaining;
	const char *incoming;
	int diverged;
	int invalid;
};
struct pull {
	struct string_list refs;
	struct string_list commits;
	struct string_list versions;
	struct string_list trees;
	struct string_list selections;
	struct string_list ends;
	struct string_list shallow;
	struct strvec clauses;
	struct strvec named;
	char *after_option;
	struct strbuf msg;
	int force;
	int chosen;
	int quiet;
};

#define PULL_INIT { .refs = STRING_LIST_INIT_DUP, .commits = STRING_LIST_INIT_DUP, \
	.versions = STRING_LIST_INIT_DUP, .trees = STRING_LIST_INIT_DUP, .selections = STRING_LIST_INIT_DUP, \
	.ends = STRING_LIST_INIT_DUP, .shallow = STRING_LIST_INIT_DUP, .clauses = STRVEC_INIT, .named = STRVEC_INIT, .msg = STRBUF_INIT }

static struct version *version_get(struct pull *p, const char *id, const char *oid)
{
	struct string_list_item *item;
	struct version *v;

	if (!oid || !strcmp(oid, "-"))
		return NULL;
	item = string_list_insert(&p->versions, xstrfmt("%s %s", id, oid));
	if (item->util)
		return item->util;
	v = xcalloc(1, sizeof(*v));
	v->oid = xstrdup(oid);
	v->r = record_decode(id, oid);
	v->body = record_text(v->r, MEANING_KEYS);
	v->accident = -1;
	string_list_init_dup(&v->ancestors);
	item->util = v;
	return v;
}

static struct tree *pull_tree(struct pull *p, const char *commit)
{
	struct string_list_item *item = string_list_insert(&p->trees, commit);

	if (!item->util)
		item->util = tree_load(commit);
	return item->util;
}

static struct version *version_at(struct pull *p, const char *id, const char *commit)
{
	struct entry *e = file_entry(pull_tree(p, commit), xstrfmt(".ait/%s", id));
	struct record *r;
	struct string_list_item *item;
	struct version *v;

	if (!e)
		return NULL;
	/* Entry validity has a separate cache identity from blob ancestry. */
	item = string_list_insert(&p->versions, xstrfmt("%s %s %s", id, e->mode, e->oid));
	if (item->util)
		return item->util;
	r = record_from_entry(e, id, 1);
	if (!r->bad) {
		v = version_get(p, id, e->oid);
		string_list_lookup(&p->versions, xstrfmt("%s %s %s", id, e->mode, e->oid))->util = v;
		return v;
	}
	v = xcalloc(1, sizeof(*v));
	v->oid = xstrdup(e->oid);
	v->r = r;
	v->body = record_text(r, MEANING_KEYS);
	v->accident = -1;
	string_list_init_dup(&v->ancestors);
	item->util = v;
	return v;
}

/* 2.2.4: a missing blob ends this branch, but its OID still participates */
static void walk_version(struct pull *p, struct version *v)
{
	struct string_list todo = STRING_LIST_INIT_DUP;
	struct string_list_item *prev;
	size_t i;

	if (v->walked)
		return;
	v->walked = 1;
	string_list_append(&todo, v->oid);
	for (i = 0; i < todo.nr; i++) {
		const char *oid = todo.items[i].string;
		struct version *x;

		if (string_list_has_string(&v->ancestors, oid))
			continue;
		string_list_insert(&v->ancestors, oid);
		x = version_get(p, v->r->id, oid);
		if (!x->r->bad)
			for_each_string_list_item(prev, &x->r->f[PREV])
				if (strcmp(prev->string, "-"))
					string_list_append(&todo, prev->string);
	}
	string_list_clear(&todo, 0);
}

static int contains_version(struct pull *p, struct version *a, struct version *b)
{
	if (!a || !b)
		return 0;
	walk_version(p, a);
	return string_list_has_string(&a->ancestors, b->oid);
}

static struct string_list maximal_versions(struct pull *p, struct string_list *versions)
{
	struct string_list result = STRING_LIST_INIT_DUP;
	struct string_list_item *item, *other;

	for_each_string_list_item(item, versions) {
		if (!item->util)
			continue;
		for_each_string_list_item(other, versions)
			if (other != item && other->util && contains_version(p, other->util, item->util))
				break;
		if (!other || other == versions->items + versions->nr)
			string_list_insert(&result, item->string)->util = item->util;
	}
	return result;
}

static int terminal(struct version *v)
{
	return in_state(v->r, "cancelled") || in_state(v->r, "withdrawn") || in_state(v->r, "fulfilled");
}

static int transition(struct record *a, struct record *b)
{
	if (!a)
		return record_step(b->id, "-", val(b, STATE), -1);
	if (strcmp(val(a, ROLE), val(b, ROLE)) || strcmp(val(a, CREATOR), val(b, CREATOR)))
		return 0;
	return !strcmp(record_text(a, MEANING_KEYS), record_text(b, MEANING_KEYS)) ||
	       record_step(b->id, val(a, STATE), val(b, STATE), -1);
}

/* 2.3.3: data only; unknown ancestry does not turn a version into an accident */
static int accident(struct pull *p, struct version *v)
{
	struct string_list_item *item;
	int good = 0, unknown = 0, ended = 0, same_end = 0;

	if (v->accident != -1)
		return v->accident == 2 ? -1 : v->accident;
	v->accident = 2;
	if (v->r->bad)
		return v->accident = object(v->oid, 0, NULL, NULL) ? 1 : -1;
	if (is_current(v->r) && strcmp(val(v->r, CHECK), xstrfmt("%s %s",
						     starts_with(val(v->r, CHECK), "reported ") ? "reported" : "forced", pins(v->r))))
		return v->accident = 1;
	walk_version(p, v);
	for_each_string_list_item(item, &v->ancestors) {
		struct version *x = version_get(p, v->r->id, item->string);

		if (x != v && !x->r->bad && terminal(x)) {
			ended = 1;
			same_end |= !strcmp(x->body, v->body);
		}
	}
	if (ended && !same_end)
		return v->accident = 1;
	for_each_string_list_item(item, &v->r->f[PREV]) {
		struct version *x;
		int bad;

		if (!strcmp(item->string, "-")) {
			good |= transition(NULL, v->r);
			continue;
		}
		x = version_get(p, v->r->id, item->string);
		bad = accident(p, x);
		unknown |= bad == -1;
		good |= bad == 0 && transition(x->r, v->r);
	}
	return v->accident = good ? 0 : unknown ? -1 : 1;
}

static int forward(struct pull *p, struct version *v, struct version *w)
{
	struct string_list common = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	const char *body = NULL;

	walk_version(p, v);
	walk_version(p, w);
	for_each_string_list_item(item, &v->ancestors)
		if (string_list_has_string(&w->ancestors, item->string))
			string_list_insert(&common, item->string)->util = version_get(p, v->r->id, item->string);
	common = maximal_versions(p, &common);
	for_each_string_list_item(item, &common) {
		struct version *x = item->util;
		if (x->r->bad || (body && strcmp(body, x->body)))
			return 0;
		body = x->body;
	}
	string_list_clear(&common, 0);
	return body && !strcmp(v->body, body) && strcmp(w->body, body);
}

static int ancestor(const char *a, const char *b)
{
	int rc;

	git_run(NULL, NULL, &rc, "merge-base --is-ancestor %s %s", a, b);
	if (rc > 1)
		reject(128, NULL, "%s", git_failed());
	return !rc;
}

/* 7.12: the introducing commit is chosen from the snapshotted ref history */
static const char *introduction(struct pull *p, const char *id, struct version *v)
{
	static struct string_list introduced = STRING_LIST_INIT_DUP, counts = STRING_LIST_INIT_DUP;
	struct string_list_item *item, *cached;
	const char *best = NULL;
	unsigned long best_count = ULONG_MAX;
	char *key = xstrfmt("%s %s %s", head, id, v->oid);

	cached = string_list_lookup(&introduced, key);
	if (cached) {
		free(key);
		return *(char *)cached->util ? cached->util : NULL;
	}
	for_each_string_list_item(item, &p->commits) {
		struct version *at = version_at(p, id, item->string);
		struct string_list *parents = item->util;
		size_t i;
		unsigned long n;

		if (!at || at->r->bad || strcmp(at->oid, v->oid))
			continue;
		for (i = 0; i < parents->nr; i++) {
			struct version *up = version_at(p, id, parents->items[i].string);

			if (up && !up->r->bad && !strcmp(up->oid, v->oid))
				break;
		}
		if (i < parents->nr)
			continue;
		if (string_list_has_string(&p->shallow, item->string))
			reject(128, xstrfmt("'%s' can be taken once this clone has its history down to the merge base with HEAD", item->string), "'%s' is beyond the shallow boundary of this clone", item->string);
		cached = string_list_lookup(&counts, xstrfmt("%s %s", head, item->string));
		if (!cached) {
			cached = string_list_insert(&counts, xstrfmt("%s %s", head, item->string));
			cached->util = xstrdup(git("rev-list --count %s %s", item->string, xstrfmt("^%s", head)));
		}
		n = strtoul(cached->util, NULL, 10);
		if (!best || n < best_count || (n == best_count && strcmp(item->string, best) < 0)) {
			best = item->string;
			best_count = n;
		}
	}
	string_list_insert(&introduced, key)->util = xstrdup(best ?: "");
	free(key);
	return best;
}

static void snapshot_refs(struct pull *p)
{
	struct string_list lines = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct strvec args = STRVEC_INIT;
	struct strbuf shallow = STRBUF_INIT;

	if (!read_text(&shallow, xstrfmt("%s/shallow", commondir))) {
		string_list_split(&p->shallow, shallow.buf, "\n", -1);
		string_list_sort(&p->shallow);
	}
	string_list_split(&lines, git("for-each-ref --format=%(refname)%20%(objectname)%20%(symref) refs/heads refs/remotes"), "\n", -1);
	/* for-each-ref's percent-hex is a literal byte; symbolic refs are excluded (5.11) */
	for_each_string_list_item(item, &lines) {
		char ref[4096], oid[48], sym[4096];

		if (sscanf(item->string, "%4095s %47s %4095s", ref, oid, sym) == 2) {
			const char *name = ref;

			skip_prefix(ref, "refs/heads/", &name) || skip_prefix(ref, "refs/remotes/", &name);
			string_list_insert(&p->refs, name)->util = xstrdup(oid);
		}
	}
	string_list_insert(&p->refs, "HEAD")->util = (void *)head;
	strvec_pushl(&args, "rev-list", "--parents", "--topo-order", NULL);
	for_each_string_list_item(item, &p->refs)
		strvec_push(&args, item->util);
	string_list_clear(&lines, 0);
	string_list_split(&lines, git_args(&args, NULL, NULL, NULL), "\n", -1);
	for_each_string_list_item(item, &lines) {
		struct string_list *ids = xcalloc(1, sizeof(*ids));

		if (!*item->string)
			continue;
		string_list_init_dup(ids);
		string_list_split(ids, item->string, " ", -1);
		string_list_insert(&p->commits, ids->items[0].string)->util = ids;
		free(ids->items[0].string);
		MOVE_ARRAY(ids->items, ids->items + 1, --ids->nr);
	}
}

static void candidate_add(struct pull *p, struct selection *s, const char *commit, const char *ref)
{
	struct version *v = version_at(p, s->id, commit);
	struct string_list_item *known;

	if (!v)
		return;
	known = string_list_lookup(&s->candidates, v->oid);
	if (known && known->util)
		return;
	if (v->r->bad || accident(p, v) == 1) {
		if (known)
			return;
		if (!p->quiet && v->r->bad)
			warning("skipping malformed '.ait/%s' at '%s'", s->id, commit);
		else if (!p->quiet)
			warning("skipping '%s' at '%s' (%s): it was changed outside ait's rules", s->id, ref, commit);
		string_list_insert(&s->candidates, v->oid);
		return;
	}
	string_list_insert(&s->candidates, v->oid)->util = v;
}

static struct selection *selection_add(struct pull *p, const char *arg)
{
	const char *eq = strchr(arg, '=');
	char *id = eq ? xstrndup(arg, eq - arg) : xstrdup(arg);
	struct string_list_item *item;
	struct selection *s;

	if (strlen(id) == 27 && id[13] == '-')
		id = xstrfmt("undertake/%s", id);
	if (!(id_like(id, "DOX") || (starts_with(id, "undertake/") && strlen(id) == 37 &&
		id_like(xstrndup(id + 10, 13), "D") && id[23] == '-' && id_like(id + 24, "O"))))
		reject(129, NULL, "bad contract ID '%s'", id);
	item = string_list_insert(&p->selections, id);
	if (item->util)
		reject(129, NULL, "contract '%s' is named more than once", id);
	s = xcalloc(1, sizeof(*s));
	s->id = item->string;
	s->pick = eq ? eq + 1 : NULL;
	p->chosen |= eq != NULL;
	string_list_init_dup(&s->candidates);
	string_list_init_dup(&s->latest);
	string_list_init_dup(&s->remaining);
	item->util = s;
	return s;
}

static char *pull_command(struct pull *p, const char *choice_id, const char *choice)
{
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;
	size_t i;

	strbuf_addstr(&sb, "ait contract pull");
	for_each_string_list_item(item, &p->selections) {
		struct selection *s = item->util;
		const char *id = starts_with(s->id, "undertake/") ? s->id + 10 : s->id;

		if (*s->id == 'u' && !s->pick)
			continue;
		strbuf_addf(&sb, " %s", id);
		if (choice_id && !strcmp(choice_id, s->id)) {
			if (choice)
				strbuf_addf(&sb, "=%s", choice);
		} else if (s->pick)
			strbuf_addf(&sb, "=%s", s->pick);
	}
	for (i = 0; i < p->clauses.nr; i++) {
		strbuf_addch(&sb, ' ');
		sq_quote_buf_pretty(&sb, p->clauses.v[i]);
	}
	return strbuf_detach(&sb, NULL);
}

static const char *confirmation(struct selection *s, struct record *r)
{
	struct string_list_item *item;
	const char *pinned = pins(r), *kind = "forced";

	for_each_string_list_item(item, &s->latest) {
		const char *check = val(((struct version *)item->util)->r, CHECK);

		if (starts_with(check, "reported ") && ends_with(check, pinned))
			kind = "reported";
	}
	if (s->mine && starts_with(val(s->mine->r, CHECK), "reported ") && ends_with(val(s->mine->r, CHECK), pinned))
		kind = "reported";
	return xstrfmt("%s %s", kind, pinned);
}

/* 7.12.1: latest -> simultaneous three-way reduction -> terminal versions */
static void select_version(struct pull *p, struct selection *s)
{
	struct string_list_item *item;
	struct version *chosen = NULL;
	int diverged = 0;
	const char *body = NULL, *check = NULL;
	int has_terminal = 0;
	size_t i, j, n;
	unsigned char *edges, *reach;

	s->mine = version_at(p, s->id, head);
	candidate_add(p, s, head, "HEAD");
	for_each_string_list_item(item, &p->refs)
		candidate_add(p, s, item->util, item->string);
	for_each_string_list_item(item, &p->ends)
		candidate_add(p, s, item->string, item->string);
	s->latest = maximal_versions(p, &s->candidates);
	n = s->latest.nr;
	if (!n) {
		for_each_string_list_item(item, &p->refs)
			if (version_at(p, s->id, item->util))
				reject(1, xstrfmt("'%s' has to be fixed on branch '%s' first", s->id, item->string), "'%s' at '%s' (%s) was changed outside ait's rules", s->id, item->string, (char *)item->util);
		reject(128, NULL, "unknown contract '%s'", s->id);
	}
	edges = xcalloc(n * n, 1);
	reach = xcalloc(n * n, 1);
	for (i = 0; i < n; i++)
		for (j = 0; j < n; j++)
			reach[i * n + j] = edges[i * n + j] = i != j && forward(p, s->latest.items[i].util, s->latest.items[j].util);
	for (i = 0; i < n; i++)
		for (j = 0; j < n; j++) {
			size_t k;

			for (k = 0; k < n; k++)
				reach[j * n + k] |= reach[j * n + i] && reach[i * n + k];
		}
	for (i = 0; i < n; i++) {
		int moved = 0;
		struct version *v = s->latest.items[i].util;

		for (j = 0; j < n; j++)
			moved |= edges[i * n + j];
		if (!moved || reach[i * n + i]) {
			string_list_insert(&s->remaining, v->oid)->util = v;
			has_terminal |= terminal(v);
		}
	}
	if (has_terminal)
		for (i = s->remaining.nr; i-- > 0; ) {
			struct version *v = s->remaining.items[i].util;

			if (!terminal(v))
				string_list_remove(&s->remaining, v->oid, 0);
		}
	if (s->pick) {
		char oid[48];

		if (!object(xstrfmt("%s^{commit}", s->pick), 0, oid, NULL))
			reject(128, NULL, "cannot resolve commit '%s'", s->pick);
		chosen = version_at(p, s->id, oid);
		if (!chosen || chosen->r->bad || !string_list_has_string(&s->remaining, chosen->oid))
				s->invalid = 1;
		if (s->invalid)
			chosen = NULL;
		body = chosen ? chosen->body : NULL;
	}
	for_each_string_list_item(item, &s->remaining) {
		struct version *v = item->util;
		if (!body)
			body = v->body;
		if (!s->pick && strcmp(body, v->body))
			diverged = 1;
	}
	s->diverged = diverged;
	{
		struct version *source = chosen ? chosen : s->remaining.items[0].util;

		check = is_current(source->r) ? confirmation(s, source->r) : NULL;
	}
	for (i = 0; i <= s->remaining.nr; i++) {
		struct version *v = i == s->remaining.nr ? s->mine : s->remaining.items[i].util;

		if (!v || strcmp(v->body, body) || (check && strcmp(check, val(v->r, CHECK))))
			continue;
		for_each_string_list_item(item, &s->latest)
			if (!contains_version(p, v, item->util))
				break;
		if (!item || item == s->latest.items + s->latest.nr) {
			s->result = v;
			break;
		}
	}
	if (!s->result) {
		struct version *v = chosen ? chosen : s->remaining.items[0].util;
		struct record *r = record_decode(s->id, v->oid);
		const char *oid;

		string_list_clear(&r->f[PREV], 0);
		for_each_string_list_item(item, &s->latest)
			string_list_append(&r->f[PREV], item->string);
		if (check)
			put(r, CHECK, check);
		oid = line1(git_run(record_text(r, ALL_KEYS), NULL, NULL, "hash-object -w --stdin"));
		s->result = version_get(p, s->id, oid);
	}
	if (s->diverged || s->invalid)
		return;
	if (s->mine && !strcmp(s->mine->body, body) && (s->result == s->mine ||
	    !string_list_has_string(&s->remaining, s->result->oid)))
		return;
	if (string_list_has_string(&s->remaining, s->result->oid) && s->result != s->mine) {
		s->incoming = introduction(p, s->id, s->result);
	} else if (chosen) {
		s->incoming = introduction(p, s->id, chosen);
	} else {
		int prefer = -1;
		unsigned long count = ULONG_MAX;

		for_each_string_list_item(item, &s->remaining) {
			struct version *v = item->util;
			const char *intro;
			unsigned long nr;
			int priority;

			if (strcmp(v->body, body))
				continue;
			intro = introduction(p, s->id, v);
			if (!intro)
				continue;
			priority = contains_version(p, v, s->mine);
			nr = strtoul(git("rev-list --count %s %s", intro, xstrfmt("^%s", head)), NULL, 10);
			if (priority > prefer || (priority == prefer && (nr < count ||
			    (nr == count && (!s->incoming || strcmp(intro, s->incoming) < 0))))) {
				s->incoming = intro;
				prefer = priority;
				count = nr;
			}
		}
	}
}

static void collect_ends(struct pull *p)
{
	struct string_list inputs = STRING_LIST_INIT_DUP, independent = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct strvec args = STRVEC_INIT;

	string_list_insert(&inputs, head);
	for_each_string_list_item(item, &p->selections) {
		struct selection *s = item->util;

		if (s->incoming)
			string_list_insert(&inputs, s->incoming);
	}
	strvec_pushl(&args, "merge-base", "--independent", NULL);
	for_each_string_list_item(item, &inputs)
		strvec_push(&args, item->string);
	string_list_split(&independent, git_args(&args, NULL, NULL, NULL), "\n", -1);
	string_list_clear(&p->ends, 0);
	for_each_string_list_item(item, &independent)
		if (*item->string && strcmp(item->string, head))
			string_list_insert(&p->ends, item->string);
	string_list_clear(&inputs, 0);
	string_list_clear(&independent, 0);
	strvec_clear(&args);
}

static void add_links(struct pull *p)
{
	struct string_list ids = STRING_LIST_INIT_DUP, commits = STRING_LIST_INIT_DUP;
	struct string_list_item *ref, *item;

	for_each_string_list_item(ref, &p->refs)
		string_list_insert(&commits, ref->util);
	for_each_string_list_item(ref, &p->ends)
		string_list_insert(&commits, ref->string);
	for_each_string_list_item(ref, &commits) {
		struct tree *t = pull_tree(p, ref->string);
		size_t i;

		for (i = 0; i < t->nr; i++) {
			const char *id;
			struct version *v;

			if (!skip_prefix(t->e[i].path, ".ait/undertake/", &id) || strlen(id) != 27)
				continue;
			v = version_at(p, xstrfmt("undertake/%s", id), ref->string);
			if (!v || v->r->bad || accident(p, v) == 1)
				continue;
			if (string_list_has_string(&p->selections, xstrndup(id, 13)) || string_list_has_string(&p->selections, id + 14))
				string_list_insert(&ids, xstrfmt("undertake/%s", id));
		}
	}
	for_each_string_list_item(item, &ids)
		if (!string_list_has_string(&p->selections, item->string))
			selection_add(p, item->string);
}

static void plan(struct pull *p)
{
	struct string_list_item *item, *end;
	size_t before, after;

	do {
		before = p->selections.nr;
		after = 0;
		for_each_string_list_item(item, &p->selections) {
			struct selection *s = item->util;

			before += s->candidates.nr;
			string_list_clear(&s->latest, 0);
			string_list_clear(&s->remaining, 0);
			s->result = NULL;
			s->incoming = NULL;
			s->diverged = s->invalid = 0;
			select_version(p, s);
		}
		collect_ends(p);
		add_links(p);
		for_each_string_list_item(item, &p->selections) {
			struct selection *s = item->util;

			for_each_string_list_item(end, &p->ends)
				candidate_add(p, s, end->string, end->string);
			after += s->candidates.nr;
		}
		after += p->selections.nr;
	} while (before != after);
}

static void terminal_warnings(struct pull *p)
{
	struct string_list_item *item, *candidate;

	for_each_string_list_item(item, &p->selections) {
		struct selection *s = item->util;
		struct version *winner = s->remaining.items[0].util;
		const char *end;

		if (!terminal(winner))
			continue;
		end = introduction(p, s->id, winner);
		for_each_string_list_item(candidate, &s->latest) {
			struct version *v = candidate->util;

			if (terminal(v) || forward(p, v, winner))
				continue;
			if (v == s->mine)
				warning("your change to '%s' is overridden: '%s' was %s in '%s'", s->id, s->id, val(winner->r, STATE), end);
			else
				warning("the change to '%s' in '%s' is overridden: '%s' was %s in '%s'", s->id, introduction(p, s->id, v), s->id, val(winner->r, STATE), end);
		}
	}
}

static struct pull snapshot_copy(struct pull *p)
{
	struct pull copy = *p;
	struct string_list_item *item;

	copy.quiet = 1;
	copy.chosen = 0;
	copy.selections = (struct string_list)STRING_LIST_INIT_DUP;
	copy.ends = (struct string_list)STRING_LIST_INIT_DUP;
	copy.versions = (struct string_list)STRING_LIST_INIT_DUP;
	copy.trees = (struct string_list)STRING_LIST_INIT_DUP;
	for_each_string_list_item(item, &p->versions)
		string_list_insert(&copy.versions, item->string)->util = item->util;
	for_each_string_list_item(item, &p->trees)
		string_list_insert(&copy.trees, item->string)->util = item->util;
	return copy;
}

static void check_head(struct pull *p)
{
	struct state s = { 0 };
	struct verdict v;
	const char *init = object("HEAD:.ait/init", 1, NULL, NULL);
	size_t i;

	if (!init || strcmp(init, "version 2\n"))
		reject(128, "rewrite '.ait/init' as the single line 'version 2' with mode 100644, then 'ait contract add .ait/init' and 'ait contract commit -m <why>'", "bad format: '.ait/init': must be the single line 'version 2'");
	state_load(&s, head_tree());
	if (check_state(&s, head_tree(), &v))
		reject(v.code, v.hint, "%s", v.msg);
	for (i = 0; i < s.nr; i++) {
		struct record *r = s.r[i];
		struct version *version;

		if (!strcmp(r->id, "init"))
			continue;
		version = version_at(p, r->id, head);
		if (version && accident(p, version) == 1)
			reject(1, "restore the affected records from their last valid state", "'%s' at HEAD was changed outside ait's rules", r->id);
	}
}

static struct string_list incoming_missing(struct pull *p, struct string_list *sources)
{
	struct string_list missing = STRING_LIST_INIT_DUP;
	struct string_list_item *end;

	for_each_string_list_item(end, &p->ends) {
		struct tree *t = pull_tree(p, end->string);
		size_t i;

		for (i = 0; i < t->nr; i++) {
			const char *id;
			struct version *v, *mine;
			size_t before = missing.nr;

			if (!skip_prefix(t->e[i].path, ".ait/", &id) || !strcmp(id, "init") || !strcmp(t->e[i].mode, "040000"))
				continue;
			v = version_at(p, id, end->string);
			mine = version_at(p, id, head);
			if (v->r->bad || accident(p, v) == 1 || contains_version(p, mine, v))
				continue;
			if (*id == 'u' && strlen(id) == 37) {
				if (!string_list_has_string(&p->selections, xstrndup(id + 10, 13)))
					string_list_insert(&missing, xstrndup(id + 10, 13))->util = (void *)(id + 10);
				if (!string_list_has_string(&p->selections, id + 24))
					string_list_insert(&missing, id + 24)->util = (void *)(id + 10);
			} else if (!string_list_has_string(&p->selections, id)) {
				string_list_insert(&missing, id);
			}
			if (missing.nr != before)
				string_list_insert(sources, end->string);
		}
	}
	for_each_string_list_item(end, &missing) {
		struct string_list_item *source;

		for_each_string_list_item(source, &p->ends) {
			struct version *v = version_at(p, end->string, source->string);

			if (v && !v->r->bad && !accident(p, v) && !contains_version(p, version_at(p, end->string, head), v))
				end->util = NULL;
		}
	}
	return missing;
}

static void copy_selection(struct pull *p, struct pull *trial, const char *id)
{
	struct string_list_item *hit = string_list_lookup(&p->selections, id), *item;
	struct selection *s, *original = hit ? hit->util : NULL;

	s = selection_add(trial, original && original->pick ? xstrfmt("%s=%s", id, original->pick) : id);
	if (original)
		for_each_string_list_item(item, &original->candidates)
			string_list_insert(&s->candidates, item->string)->util = item->util;
}

static struct string_list complete_names(struct pull *p, struct pull *trial, struct string_list *sources)
{
	struct string_list needed = STRING_LIST_INIT_DUP, missing;
	struct string_list_item *item;

	plan(trial);
	while ((missing = incoming_missing(trial, sources)).nr) {
		for_each_string_list_item(item, &missing) {
			string_list_insert(&needed, item->string)->util = item->util;
			copy_selection(p, trial, item->string);
		}
		plan(trial);
	}
	return needed;
}

static void check_incoming(struct pull *p)
{
	struct pull trial = snapshot_copy(p);
	struct string_list sources = STRING_LIST_INIT_DUP, needed;
	struct string_list_item *item;
	struct strbuf msg = STRBUF_INIT, commits = STRBUF_INIT;

	for_each_string_list_item(item, &p->selections)
		copy_selection(p, &trial, item->string);
	for_each_string_list_item(item, &p->ends)
		string_list_insert(&trial.ends, item->string);
	needed = complete_names(p, &trial, &sources);
	if (!needed.nr)
		return;
	for_each_string_list_item(item, &needed)
		strbuf_addf(&msg, "%s'%s'%s", msg.len ? ", " : "", item->string, item->util ? xstrfmt(" (undertake '%s')", (char *)item->util) : "");
	for_each_string_list_item(item, &sources)
		strbuf_addf(&commits, "%s'%s'", commits.len ? " and " : "", item->string);
	reject(1, xstrfmt("name them too: '%s'", pull_command(&trial, NULL, NULL)), "taking %s also changes contracts you did not name: %s", commits.buf, msg.buf);
}

static struct state result_state(struct pull *p)
{
	struct state s = { 0 };
	struct string_list_item *item;
	size_t i;

	state_load(&s, head_tree());
	for_each_string_list_item(item, &p->selections) {
		struct selection *pick = item->util;
		struct record *r = state_get(&s, pick->id);

		if (!r)
			r = state_add(&s, pick->id);
		*r = *record_decode(pick->id, pick->result->oid);
	}
	for (i = 0; i < s.nr; i++)
		if (*s.r[i]->id == 'u' && !link_end(&s, s.r[i], 0))
			s.r[i]->bad = xstrdup("its demand does not exist");
	return s;
}

static struct string_list pins_at(struct state *s, const char *path)
{
	struct string_list out = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	size_t i;

	for (i = 0; i < s->nr; i++) {
		struct record *r = s->r[i];

		if (!r->bad && !is_live(r))
			continue;
		for_each_string_list_item(item, &r->f[FILES]) {
			char file[4096], mode[8], oid[48];

			if (sscanf(item->string, "%4095s %7s %47s", file, mode, oid) == 3 && !strcmp(file, path))
				string_list_insert(&out, xstrfmt("%s %s", mode, oid))->util = r;
		}
	}
	return out;
}

static const char *incompatible_pins(struct state *state)
{
	struct string_list_item *item;
	size_t i;

	for (i = 0; i < state->nr; i++)
		if (is_live(state->r[i]))
			for_each_string_list_item(item, &state->r[i]->f[FILES])
				if (pins_at(state, scope_path(item->string)).nr > 1)
					return scope_path(item->string);
	return NULL;
}

/* Closure and choice resolution advance together; a provisional result is no choice. */
static void choice_commands(struct pull *p, struct string_list *commands,
			    struct string_list *resolved, int all)
{
	struct pull trial = snapshot_copy(p);
	struct string_list sources = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct selection *next = NULL;
	size_t i;

	for_each_string_list_item(item, &p->selections)
		copy_selection(p, &trial, item->string);
	complete_names(p, &trial, &sources);
	for_each_string_list_item(item, &trial.selections) {
		struct selection *s = item->util;

		if (s->diverged || s->invalid ||
		    (all && !string_list_has_string(resolved, s->id))) {
			next = s;
			break;
		}
	}
	if (!next) {
		struct state state = result_state(&trial);
		struct verdict v;

		if (!check_records(&state, &v) && (trial.ends.nr || !incompatible_pins(&state)))
			string_list_insert(commands, pull_command(&trial, NULL, NULL));
		return;
	}
	string_list_insert(resolved, next->id);
	for (i = 0; i < next->remaining.nr; i++) {
		struct version *v = next->remaining.items[i].util;
		const char *choice = next->mine == v ? "HEAD" : introduction(&trial, next->id, v);

		if (!choice)
			continue;
		next->pick = choice;
		choice_commands(&trial, commands, resolved, all);
	}
	string_list_remove(resolved, next->id, 0);
}

static int choice_rank(const char *command)
{
	struct string_list tokens = STRING_LIST_INIT_DUP, picks = STRING_LIST_INIT_DUP;
	struct string_list_item *item;

	string_list_split(&tokens, command, " ", -1);
	for_each_string_list_item(item, &tokens) {
		const char *eq = strchr(item->string, '=');

		if (eq)
			string_list_insert(&picks, eq + 1);
	}
	return picks.nr;
}

static char *choices_hint(struct pull *p, int all)
{
	struct string_list commands = STRING_LIST_INIT_DUP, resolved = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct strbuf hint = STRBUF_INIT;
	int pass;

	choice_commands(p, &commands, &resolved, all);
	for (pass = 0; pass < 2; pass++)
		for_each_string_list_item(item, &commands)
			if ((choice_rank(item->string) > 1) == pass)
				strbuf_addf(&hint, "%s'%s -m <why>'", hint.len ? " or " : "pick one: ", item->string);
	return hint.len ? strbuf_detach(&hint, NULL) : NULL;
}

static void check_choices(struct pull *p)
{
	struct string_list_item *item, *first = NULL;
	struct strbuf message = STRBUF_INIT;
	char *hint;

	for_each_string_list_item(item, &p->selections) {
		struct selection *s = item->util;

		if (s->diverged || s->invalid) {
			first = item;
			break;
		}
	}
	if (!first)
		return;
	hint = choices_hint(p, 0);
	{
		struct selection *s = first->util;
		struct version *initial = s->remaining.items[0].util;
		int diverged = 0;

		for_each_string_list_item(item, &s->remaining) {
			struct version *v = item->util;
			const char *intro = s->mine == v ? "HEAD" : introduction(p, s->id, v), *name = NULL;
			struct string_list_item *ref;

			diverged |= strcmp(initial->body, v->body) != 0;
			for_each_string_list_item(ref, &p->refs) {
				struct version *at = version_at(p, s->id, ref->util);

				if (at == v && strcmp(ref->string, "HEAD")) {
					name = ref->string;
					break;
				}
			}
			strbuf_addf(&message, "%s'%s'%s", message.len ? " and " : "", intro,
				strcmp(intro, "HEAD") ? xstrfmt(" (%s)", name ? name : intro) : "");
		}
		if (s->invalid)
			reject(1, !diverged ? xstrfmt("take it as it is: '%s'", pull_command(p, s->id, NULL)) : hint, "'%s' has no version of '%s' to choose: a newer one includes it", s->pick, s->id);
		reject(1, hint, "'%s' has diverged between %s", s->id, message.buf);
	}
}

static const char *entry_value(struct tree *t, const char *path)
{
	struct entry *e = file_entry(t, path);

	return e ? xstrfmt("%s %s", e->mode, e->oid) : "";
}

static void put_entry(struct strbuf *info, const char *path, const char *value)
{
	char mode[8], oid[48];

	if (value && sscanf(value, "%7s %47s", mode, oid) == 2)
		index_line(info, mode, oid, path);
	else
		index_line(info, NULL, NULL, path);
}

/* All merge sides share one serialized result, including unchanged HEAD records. */
static char *contract_overlay(struct state *result)
{
	struct strbuf info = STRBUF_INIT;
	size_t i;

	for (i = 0; i < result->nr; i++) {
		struct record *r = result->r[i];
		const char *oid = !strcmp(r->id, "init") ? blob_at(head_tree(), ".ait/init") :
			line1(git_run(record_text(r, ALL_KEYS), NULL, NULL, "hash-object -w --stdin"));

		index_line(&info, "100644", oid, xstrfmt(".ait/%s", r->id));
	}
	return strbuf_detach(&info, NULL);
}

/* Replace only unwanted side changes; Git chooses every merge base itself */
static const char *filtered_side(struct pull *p, const char *rev, const char *base, struct state *result, const char *overlay)
{
	struct tree *t = pull_tree(p, rev), *b = pull_tree(p, base);
	struct state side = { 0 };
	struct string_list paths = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct strbuf info = STRBUF_INIT;
	const char *tree;
	size_t i;

	state_load(&side, t);
	tree_diff(b, t, &paths);
	for_each_string_list_item(item, &paths) {
		const char *path = item->string, *now = entry_value(t, path), *replacement;
		struct string_list target, own;

		if (starts_with(path, ".ait/"))
			continue;
		target = pins_at(result, path);
		own = pins_at(&side, path);
		replacement = target.nr == 1 ? target.items[0].string : entry_value(b, path);
		if (string_list_has_string(&target, now))
			continue;
		if (string_list_has_string(&own, now)) {
			put_entry(&info, path, replacement);
		} else if (own.nr) {
			if (!p->quiet)
				warning("skipping '%s' at '%s': it was changed outside ait's rules", path, rev);
			for (i = 0; i < own.nr; i++)
				if (string_list_has_string(&target, own.items[i].string))
					break;
			put_entry(&info, path, i < own.nr ? own.items[i].string : replacement);
		} else if (!*now && target.nr) {
			struct record *r = target.items[0].util;

			if (!p->quiet)
				warning("skipping the removal of '%s' in '%s': '%s' pins it", path, rev, r->id);
			put_entry(&info, path, replacement);
		}
	}
	/* Equal contract files cannot interfere with Git's code merge */
	for (i = 0; i < t->nr; i++)
		if (starts_with(t->e[i].path, ".ait/") && strcmp(t->e[i].mode, "040000"))
			index_line(&info, NULL, NULL, t->e[i].path);
	strbuf_addstr(&info, overlay);
	tree = tree_with(t->oid, info.buf, NULL);
	if (!tree)
		reject(128, NULL, "%s", git_failed());
	return !strcmp(tree, t->oid) ? rev : line1(git_run("pull input\n", NULL, NULL, "commit-tree %s -p %s", tree, rev));
}

struct merged {
	const char *commit;
	const char *tree;
	struct string_list conflicts;
	struct string_list stages;
	struct strbuf messages;
};

/* Git's documented NUL-delimited merge-tree output (git-merge-tree.adoc:153) */
static struct merged merge_one(const char *a, const char *b)
{
	struct merged m = { .conflicts = STRING_LIST_INIT_DUP, .stages = STRING_LIST_INIT_DUP, .messages = STRBUF_INIT };
	char *out, *line, *end;
	int rc;

	out = git_run(NULL, NULL, &rc, "merge-tree --write-tree -z %s %s", a, b);
	if (rc > 1 || !oid_ok(out))
		reject(128, NULL, "%s", git_failed());
	end = out + git_out_len;
	m.tree = xstrdup(out);
	for (line = out + strlen(out) + 1; line < end && *line; line += strlen(line) + 1) {
		char mode[8], oid[48];
		int stage;
		const char *tab = strchr(line, '\t');

		if (!tab || sscanf(line, "%7s %47s %d", mode, oid, &stage) != 3)
			reject(128, NULL, "cannot read git merge conflict output");
		{
			struct strbuf entry = STRBUF_INIT;

			strbuf_addf(&entry, "%s %s %d\t", mode, oid, stage);
			quote_c_style(tab + 1, &entry, NULL, 0);
			string_list_insert(&m.conflicts, tab + 1);
			string_list_append(&m.stages, entry.buf)->util = xstrdup(tab + 1);
		}
	}
	for (line += line < end; line < end && *line; ) {
		int n = atoi(line);

		line += strlen(line) + 1;
		while (n-- > 0)
			line += strlen(line) + 1;
		line += strlen(line) + 1;
		strbuf_addf(&m.messages, "%s\n", line);
		line += strlen(line) + 1;
	}
	m.commit = line1(git_run("pull merge\n", NULL, NULL, "commit-tree %s -p %s -p %s", m.tree, a, b));
	return m;
}

/* Continue a prefix only while every conflicted path is changed once. */
static int merge_prefix(struct pull *p, const struct strvec *inputs,
			const struct strvec *starts, struct strvec *order,
			const struct merged *prefix, struct merged *out)
{
	size_t i, j;

	if (order->nr == p->ends.nr) {
		*out = *prefix;
		return 1;
	}
	for (i = 0; i < p->ends.nr; i++) {
		const char *end = p->ends.items[i].string;
		struct merged empty = { .commit = starts->v[i],
			.conflicts = STRING_LIST_INIT_DUP, .stages = STRING_LIST_INIT_DUP,
			.messages = STRBUF_INIT };
		const struct merged *acc = order->nr ? prefix : &empty;
		struct string_list changed = STRING_LIST_INIT_DUP;
		struct string_list_item *item;
		struct merged next, branch;
		const char *base;
		int used = 0, repeated = 0;

		for (j = 0; j < order->nr; j++)
			used |= !strcmp(order->v[j], end);
		if (used)
			continue;
		base = line1(git("merge-base %s %s", acc->commit, end));
		tree_diff(pull_tree(p, base), pull_tree(p, end), &changed);
		for_each_string_list_item(item, &changed)
			if (string_list_has_string(&acc->conflicts, item->string)) {
				string_list_insert(&out->conflicts, item->string);
				repeated = 1;
			}
		string_list_clear(&changed, 0);
		if (repeated)
			continue;
		next = merge_one(acc->commit, inputs->v[i]);
		branch = (struct merged){ .commit = next.commit, .tree = next.tree,
			.conflicts = STRING_LIST_INIT_DUP, .stages = STRING_LIST_INIT_DUP,
			.messages = STRBUF_INIT };
		for_each_string_list_item(item, &acc->conflicts)
			string_list_insert(&branch.conflicts, item->string);
		for_each_string_list_item(item, &next.conflicts)
			string_list_insert(&branch.conflicts, item->string);
		for_each_string_list_item(item, &acc->stages)
			string_list_append(&branch.stages, item->string)->util = item->util;
		for_each_string_list_item(item, &next.stages)
			string_list_append(&branch.stages, item->string)->util = item->util;
		strbuf_add(&branch.messages, acc->messages.buf, acc->messages.len);
		strbuf_add(&branch.messages, next.messages.buf, next.messages.len);
		string_list_clear(&next.conflicts, 0);
		string_list_clear(&next.stages, 0);
		strbuf_release(&next.messages);
		strvec_push(order, end);
		if (merge_prefix(p, inputs, starts, order, &branch, out))
			return 1;
		free((char *)order->v[--order->nr]);
		order->v[order->nr] = NULL;
		string_list_clear(&branch.conflicts, 0);
		string_list_clear(&branch.stages, 0);
		strbuf_release(&branch.messages);
	}
	return 0;
}

/* Side filtering is independent of the permutation; Git still chooses the bases. */
static int merge_order(struct pull *p, struct state *result, struct strvec *order,
		       struct merged *out)
{
	struct strvec inputs = STRVEC_INIT, starts = STRVEC_INIT;
	struct string_list_item *end;
	int found;
	char *overlay = contract_overlay(result);

	for_each_string_list_item(end, &p->ends) {
		const char *base = line1(git("merge-base %s %s", head, end->string));

		strvec_push(&inputs, filtered_side(p, end->string, base, result, overlay));
		strvec_push(&starts, filtered_side(p, head, base, result, overlay));
	}
	found = merge_prefix(p, &inputs, &starts, order, NULL, out);
	strvec_clear(&inputs);
	strvec_clear(&starts);
	free(overlay);
	return found;
}

/* A split hint is simulated against the tree and ancestry after each receive */
static int split_sequence(struct pull *p, struct string_list *left, struct strbuf *hint)
{
	struct string_list_item *end, *item;
	const char *saved_head = head;

	if (!left->nr)
		return 1;
	for_each_string_list_item(end, &p->ends) {
		struct pull trial = snapshot_copy(p);
		struct string_list sources = STRING_LIST_INIT_DUP, rest = STRING_LIST_INIT_DUP;
		struct state state;
		struct merged m = { .conflicts = STRING_LIST_INIT_DUP, .stages = STRING_LIST_INIT_DUP, .messages = STRBUF_INIT };
		struct strvec order = STRVEC_INIT;
		struct strbuf resolved = STRBUF_INIT;
		struct verdict v;
		const char *tree;
		size_t len = hint->len;

		for_each_string_list_item(item, left) {
			struct selection *s = string_list_lookup(&p->selections, item->string)->util;

			if (s->incoming && ancestor(s->incoming, end->string))
				copy_selection(p, &trial, s->id);
		}
		if (!trial.selections.nr)
			continue;
		add_links(&trial);
		plan(&trial);
		complete_names(p, &trial, &sources);
		for_each_string_list_item(item, left)
			if (!string_list_has_string(&trial.selections, item->string))
				string_list_insert(&rest, item->string);
		if ((!rest.nr && !hint->len) || rest.nr == left->nr || !trial.ends.nr)
			continue;
		state = result_state(&trial);
		if (check_records(&state, &v) || !merge_order(&trial, &state, &order, &m))
			continue;
		for_each_string_list_item(item, &m.conflicts)
			put_entry(&resolved, item->string, entry_value(head_tree(), item->string));
		tree = tree_with(m.tree, resolved.buf, NULL);
		strbuf_addf(hint, "%s'%s%s'", hint->len ? ", then " : "take them one at a time: ", pull_command(&trial, NULL, NULL), trial.chosen ? " -m <why>" : "");
		head = line1(git_run("pull split preview\n", NULL, NULL, "commit-tree %s -p %s -p %s", tree, saved_head, m.commit));
		if (split_sequence(p, &rest, hint)) {
			head = saved_head;
			return 1;
		}
		head = saved_head;
		strbuf_setlen(hint, len);
	}
	return 0;
}

static char *joined_names(struct string_list *names)
{
	struct strbuf out = STRBUF_INIT;
	size_t i;

	for (i = 0; i < names->nr; i++)
		strbuf_addf(&out, "%s'%s'", i ? i == names->nr - 1 ? " and " : ", " : "", names->items[i].string);
	return strbuf_detach(&out, NULL);
}

static void split_error(struct pull *p, struct merged *m)
{
	struct string_list ends = STRING_LIST_INIT_DUP, left = STRING_LIST_INIT_DUP;
	struct string_list_item *end, *item;
	struct strbuf hint = STRBUF_INIT;
	int mine = 0, split;

	for_each_string_list_item(item, &p->selections)
		if (*item->string != 'u')
			string_list_insert(&left, item->string);
	for_each_string_list_item(end, &p->ends) {
		const char *base = line1(git("merge-base %s %s", head, end->string));
		struct string_list changes = STRING_LIST_INIT_DUP, own = STRING_LIST_INIT_DUP;

		tree_diff(pull_tree(p, base), pull_tree(p, end->string), &changes);
		tree_diff(pull_tree(p, base), head_tree(), &own);
		for_each_string_list_item(item, &m->conflicts) {
			if (unsorted_string_list_has_string(&changes, item->string))
				string_list_insert(&ends, end->string);
			mine |= unsorted_string_list_has_string(&own, item->string);
		}
	}
	split = !p->clauses.nr && split_sequence(p, &left, &hint);
	reject(1, split ? hint.buf : p->ends.nr == 2 ? "they can be taken once one of them is rebased onto the other, or once your branch merges cleanly with one of them" : "they can be taken once one of them is rebased onto the others", "%s%s%s change %s%s", joined_names(&ends), split ? " " : " can only be taken together, but ", ends.nr == 2 ? "both" : "all", joined_names(&m->conflicts), mine ? ", which you changed too" : "");
}

static const char *restore_pins(struct pull *p, struct state *s)
{
	struct strbuf info = STRBUF_INIT;
	struct string_list seen = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	size_t i;

	for (i = 0; i < s->nr; i++) {
		struct record *r = s->r[i];

		if (!is_live(r))
			continue;
		for_each_string_list_item(item, &r->f[FILES]) {
			const char *path = scope_path(item->string);
			struct string_list target = pins_at(s, path);

			if (string_list_has_string(&seen, path))
				continue;
			string_list_insert(&seen, path);
			if (target.nr != 1)
				reject(1, choices_hint(p, 1), "spec files do not match the contract: '%s' has incompatible pins", path);
			put_entry(&info, path, target.items[0].string);
		}
	}
	strbuf_addstr(&info, contract_overlay(s));
	return tree_with(head_tree()->oid, info.buf, NULL);
}

static void check_local(struct tree *t, struct string_list *conflicts, int merge)
{
	struct state s = { 0 };
	struct string_list paths = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	int rc;

	state_load(&s, head_tree());
	tree_diff(head_tree(), t, &paths);
	for_each_string_list_item(item, conflicts)
		if (!unsorted_string_list_has_string(&paths, item->string))
			string_list_append(&paths, item->string);
	for_each_string_list_item(item, &paths) {
		const char *path = item->string;
		struct strvec args = STRVEC_INIT;
		int rc;

		/* 7.12.0 rejects changes on write paths even when Git could keep them */
		strvec_pushl(&args, "diff", "--cached", "--quiet", "HEAD", "--", path, NULL);
		git_args(&args, NULL, NULL, &rc);
		strvec_clear(&args);
		if (!rc) {
			strvec_pushl(&args, "diff-files", "--quiet", "--", path, NULL);
			git_args(&args, NULL, NULL, &rc);
			strvec_clear(&args);
		}
		if (rc) {
			struct strbuf arg = STRBUF_INIT;

			sq_quote_buf_pretty(&arg, path);
			reject(1, contract_path(&s, path) ? xstrfmt("commit your changes with 'ait contract add %s' and 'ait contract commit -m <why>', or set them aside, before you pull", arg.buf) :
			       xstrfmt("commit or set aside your changes to '%s' before you pull", path), "your local changes to '%s' would be overwritten by this pull", path);
		}
		/* Without excludes Git also lists ignored data, which 7.12.0 protects */
		strvec_pushl(&args, "ls-files", "--others", "--directory", "-z", "--", path, NULL);
		if (*git_args(&args, NULL, NULL, NULL))
			reject(1, xstrfmt("move or remove '%s' before you pull", path), "untracked working tree files would be overwritten by this pull: '%s'", path);
		strvec_clear(&args);
	}
	/* unpack-trees owns untracked, symlink and directory/file protection */
	git_run(NULL, NULL, &rc, "read-tree -n -m -u %s %s", head, t->oid);
	if (rc) {
		const char *why = git_failed();
		const char *formats[][2] = {
			{ "Untracked working tree file '", "' would be overwritten by merge" },
			{ "Untracked working tree file '", "' would be removed by merge" },
			{ "Updating '", "' would lose untracked files in it" }
		};
		size_t i;

		/* unpack-trees.c keeps these plumbing diagnoses stable for scripts */
		for (i = 0; i < ARRAY_SIZE(formats); i++) {
			const char *path = strstr(why, formats[i][0]), *end;
			char *name;

			if (!path)
				continue;
			path += strlen(formats[i][0]);
			end = strstr(path, formats[i][1]);
			if (!end)
				continue;
			name = xstrndup(path, end - path);
			reject(1, xstrfmt("move or remove '%s' before you pull", name), "untracked working tree files would be overwritten by this pull: '%s'", name);
		}
		reject(1, "commit or set aside your changes before you pull", "cannot update the index and working tree safely: %s", why);
	}
	git_run(NULL, NULL, &rc, "diff --cached --quiet HEAD");
	if (merge && rc)
		reject(1, "first commit or set aside your changes, then rerun this command", "uncommitted changes in the index");
}

/* 1.5d: count a new future result only when no parent already carries its reopen */
static int reopened_version(struct pull *p, struct version *v, struct string_list *parents)
{
	struct string_list currents = STRING_LIST_INIT_DUP;
	struct string_list_item *item, *parent, *ancestor_item;

	if (!in_state(v->r, "future"))
		return 0;
	for_each_string_list_item(parent, parents) {
		struct version *up = version_at(p, v->r->id, parent->string);

		if (contains_version(p, up, v))
			return 0;
		if (up && is_current(up->r))
			string_list_insert(&currents, up->oid);
	}
	for_each_string_list_item(item, &v->r->f[PREV]) {
		struct version *prev = version_get(p, v->r->id, item->string);

		if (prev && !prev->r->bad && is_current(prev->r))
			string_list_insert(&currents, prev->oid);
	}
	for_each_string_list_item(item, &currents) {
		struct version *current = version_get(p, v->r->id, item->string);
		int carried = 0;

		for_each_string_list_item(parent, parents) {
			struct version *up = version_at(p, v->r->id, parent->string);

			if (!up || !in_state(up->r, "future"))
				continue;
			walk_version(p, up);
			for_each_string_list_item(ancestor_item, &up->ancestors) {
				struct version *x = version_get(p, v->r->id, ancestor_item->string);

				carried |= x->r->bad || !strcmp(current->body, x->body);
			}
		}
		if (!carried)
			return 1;
	}
	return 0;
}

static void reopen_warnings(struct pull *p, struct state *state, struct tree *t, const char *where)
{
	struct string_list parents = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	size_t i;

	string_list_insert(&parents, head);
	for_each_string_list_item(item, &p->ends)
		string_list_insert(&parents, item->string);
	for (i = 0; i < state->nr; i++) {
		struct record *r = state->r[i];
		struct version *v = version_get(p, r->id, blob_at(t, xstrfmt(".ait/%s", r->id)));
		const char *reason = "";
		int current = 0, matching = 0;

		if (!v || !reopened_version(p, v, &parents))
			continue;
		for_each_string_list_item(item, &parents) {
			struct version *up = version_at(p, r->id, item->string);

			if (up && in_state(up->r, "future") && !strcmp(up->body, v->body))
				reason = ": it keeps the future version over the current one";
			if (up && is_current(up->r)) {
				current = 1;
				matching |= !strcmp(pins(up->r), pins(r));
			}
		}
		if (!*reason && current && !matching)
			reason = ": its pins changed without a check";
		if (!*reason && *r->id == 'O') {
			size_t j;

			for (j = 0; j < state->nr; j++) {
				struct record *d = partner(state, state->r[j], r);
				struct version *dv = d ? version_get(p, d->id, blob_at(t, xstrfmt(".ait/%s", d->id))) : NULL;

				if (dv && reopened_version(p, dv, &parents)) {
					reason = xstrfmt(": it undertakes reopened '%s'", d->id);
					break;
				}
			}
		}
		warning("'%s' is reopened %s%s", r->id, where, reason);
		fulfill_hint(state, r, t);
	}
}

static void incoming_warnings(struct pull *p)
{
	struct string_list_item *end;
	size_t i;

	for_each_string_list_item(end, &p->ends) {
		struct tree *t = pull_tree(p, end->string);
		struct tree *base = pull_tree(p, line1(git("merge-base %s %s", head, end->string)));

		for (i = 0; i < head_tree()->nr; i++) {
			const char *id;

			if (skip_prefix(head_tree()->e[i].path, ".ait/", &id) && strcmp(id, "init") && !file_entry(t, head_tree()->e[i].path) && file_entry(base, head_tree()->e[i].path) && strcmp(head_tree()->e[i].mode, "040000"))
				warning("skipping the removal of '%s' in '%s': records are never removed", id, end->string);
		}
		for (i = 0; i < t->nr; i++) {
			const char *id;
			struct version *v;

			if (!skip_prefix(t->e[i].path, ".ait/", &id) || !strcmp(id, "init") || !strcmp(t->e[i].mode, "040000") || string_list_has_string(&p->selections, id))
				continue;
			if (!id_like(id, "DOX") && !(starts_with(id, "undertake/") && strlen(id) == 37)) {
				warning("skipping '%s' in '%s': not a contract file", t->e[i].path, end->string);
				continue;
			}
			v = version_at(p, id, end->string);
			if (v->r->bad)
				warning("skipping malformed '.ait/%s' at '%s'", id, end->string);
			else if (accident(p, v) == 1)
				warning("skipping '%s' at '%s': it was changed outside ait's rules", id, end->string);
		}
	}
}

static struct string_list integration_sides(struct pull *p)
{
	struct string_list sources = STRING_LIST_INIT_DUP;
	struct string_list_item *source;
	struct strbuf data = STRBUF_INIT;

	string_list_insert(&sources, head);
	if (!read_text(&data, xstrfmt("%s/MERGE_HEAD", gitdir))) {
		string_list_split(&p->ends, data.buf, "\n", -1);
		string_list_sort(&p->ends);
		string_list_remove(&p->ends, "", 0);
		for_each_string_list_item(source, &p->ends)
			if (*source->string)
				string_list_insert(&sources, source->string);
	} else {
		const char *rev = object("CHERRY_PICK_HEAD", 0, NULL, NULL) ? "CHERRY_PICK_HEAD" :
			object("REVERT_HEAD", 0, NULL, NULL) ? "REVERT_HEAD^" : object("REBASE_HEAD", 0, NULL, NULL) ? "REBASE_HEAD" : NULL;

		if (rev)
			string_list_insert(&sources, line1(git("rev-parse %s", rev)));
	}
	string_list_sort(&sources);
	string_list_remove(&sources, "", 0);
	return sources;
}

static void check_integration(struct state *state, struct tree *t, int report, int automatic)
{
	struct state hs = { 0 };
	struct verdict v, hv;

	if (!check_state(state, t, &v))
		return;
	state_load(&hs, head_tree());
	if (check_state(&hs, head_tree(), &hv) && !strcmp(hv.msg, v.msg)) {
		if (report)
			warning("'%s' keeps an error from HEAD", v.k ? xstrfmt(".ait/%s", v.k->id) : ".ait");
	} else {
		reject(v.code, automatic ? "fix it in this merge and run 'git commit --allow-empty --no-edit --cleanup=strip'; the records and spec files are pinned and validated again then" : v.hint, "%s", v.msg);
	}
}

/* 7.1.4: MERGE_HEAD is not yet available in an automatic merge. */
void integration_validate(struct tree *t)
{
	struct pull p = PULL_INIT;
	struct state state = { 0 };
	const char *init = object(xstrfmt("%s:.ait/init", t->oid), 1, NULL, NULL);
	size_t i;

	if (!init || strcmp(init, "version 2\n")) {
		validate_at(t);
		return;
	}
	state_load(&state, t);
	check_integration(&state, t, 1, 1);
	for (i = 0; i < state.nr; i++) {
		struct record *r = state.r[i];
		const char *path = xstrfmt(".ait/%s", r->id);
		struct version *v;

		if (!strcmp(r->id, "init"))
			continue;
		v = version_get(&p, r->id, blob_at(t, path));
		if (v && !strcmp(v->oid, blob_at(head_tree(), path) ?: "") && accident(&p, v) == 1)
			warning("'%s' keeps an error from HEAD", path);
	}
}

/* Integration shares the version and transition judgments used by pull */
static void integrate_records(struct tree *t, int write)
{
	struct pull p = PULL_INIT;
	struct state state = { 0 };
	struct string_list sources, paths = STRING_LIST_INIT_DUP, clean = STRING_LIST_INIT_DUP;
	struct string_list_item *item, *source;
	struct strbuf info = STRBUF_INIT;
	const char *init = object(xstrfmt("%s:.ait/init", t->oid), 1, NULL, NULL);
	size_t i;

	if (!init || strcmp(init, "version 2\n")) {
		validate_at(t);
		return;
	}
	sources = integration_sides(&p);
	state_load(&state, t);
	for_each_string_list_item(source, &sources) {
		struct tree *side = pull_tree(&p, source->string);

		for (i = 0; i < side->nr; i++)
			if (starts_with(side->e[i].path, ".ait/") && strcmp(side->e[i].mode, "040000"))
				string_list_insert(&paths, side->e[i].path);
	}
	for_each_string_list_item(item, &paths)
		if (!file_entry(t, item->string))
			reject(1, xstrfmt("restore '%s' as in 'HEAD' and stage the result", item->string), "not allowed: records are never removed: '%s'", item->string);
	for (i = 0; i < state.nr; i++) {
		struct record *r = state.r[i];
		const char *check = NULL;
		int had_current = 0;

		if (!strcmp(r->id, "init"))
			continue;
		if (is_live(r) && !files_match(r, t))
			expand_files(r, t);
		if (!is_current(r))
			continue;
		for_each_string_list_item(source, &sources) {
			struct version *up = version_at(&p, r->id, source->string);

			if (!up || !is_current(up->r) || (strcmp(source->string, head) && (up->r->bad || accident(&p, up) == 1)))
				continue;
			had_current = 1;
			if (!strcmp(pins(r), pins(up->r)) && (!check || starts_with(val(up->r, CHECK), "reported ")))
				check = val(up->r, CHECK);
		}
		if (check) {
			put(r, CHECK, check);
		} else if (had_current) {
			struct string_list reopen = STRING_LIST_INIT_DUP;

			reopen_set(&state, r, &reopen);
			for_each_string_list_item(item, &reopen) {
				put(item->util, STATE, "future");
				string_list_clear(&((struct record *)item->util)->f[CHECK], 0);
			}
		}
	}
	for (i = 0; i < state.nr; i++) {
		struct record *r = state.r[i];
		const char *path = xstrfmt(".ait/%s", r->id), *old = blob_at(t, path), *allowed_side = NULL;
		struct version *original;
		struct string_list starts = STRING_LIST_INIT_DUP, previous = STRING_LIST_INIT_DUP;
		int allowed = 0, ended = 0, all = 1, rc;

		if (!strcmp(r->id, "init"))
			continue;
		original = version_get(&p, r->id, old);
		for_each_string_list_item(source, &sources) {
			struct version *up = version_at(&p, r->id, source->string);

			if (!up)
				continue;
			if (strcmp(source->string, head) && (up->r->bad || accident(&p, up) == 1)) {
				if (write && up->r->bad)
					warning("skipping malformed '%s' at '%s'", path, source->string);
				else if (write)
					warning("skipping '%s' at '%s': it was changed outside ait's rules", r->id, source->string);
				continue;
			}
			string_list_insert(&starts, up->oid)->util = up;
			all &= contains_version(&p, original, up);
			ended |= terminal(up);
			if (terminal(up))
				allowed_side = source->string;
		}
		if (!strcmp(old, blob_at(head_tree(), path) ?: "")) {
			allowed = 1;
			if (accident(&p, original) == 1) {
				if (write)
					warning("'%s' keeps an error from HEAD", path);
				if (!strcmp(original->body, record_text(r, MEANING_KEYS)) && !strcmp(val(original->r, CHECK), val(r, CHECK)))
					continue;
			}
		}
		for_each_string_list_item(item, &starts) {
			struct version *up = item->util;

			if ((!ended || terminal(up)) && transition(up->r, r))
				allowed = 1;
			string_list_insert(&previous, up->oid);
		}
		if (!starts.nr)
			allowed = transition(NULL, r);
		if (!allowed)
			reject(1, xstrfmt("restore '%s' as in '%s' and stage the result, then run 'git commit --allow-empty --no-edit --cleanup=strip'", path, allowed_side ?: head), "not allowed: '%s' cannot follow the starting versions", r->id);
		if (all && accident(&p, original) != 1) {
			if (!strcmp(record_text(r, MEANING_KEYS), original->body) && !strcmp(val(r, CHECK), val(original->r, CHECK)))
				continue;
			string_list_insert(&r->f[PREV], old);
		} else {
			struct string_list latest = maximal_versions(&p, &starts);
			struct version *same = NULL, *first = latest.nr ? latest.items[0].util : NULL;
			int diverged = 0;

			string_list_clear(&previous, 0);
			for_each_string_list_item(item, &latest) {
				struct version *up = item->util;

				diverged |= strcmp(first->body, up->body) != 0;
				if (!strcmp(up->body, record_text(r, MEANING_KEYS)) && !strcmp(val(up->r, CHECK), val(r, CHECK)))
					same = up;
				string_list_insert(&previous, up->oid);
			}
			r->f[PREV] = !diverged && same ? same->r->f[PREV] : previous;
			if (!previous.nr)
				put(r, PREV, "-");
		}
		if (r->f[PREV].nr > 1)
			string_list_remove(&r->f[PREV], "-", 0);
		index_line(&info, "100644", line1(git_run(record_text(r, ALL_KEYS), NULL, NULL, "hash-object -w --stdin")), path);
		git_run(NULL, hook_index, &rc, "diff --quiet -- %s", path);
		if (!rc)
			string_list_insert(&clean, path);
	}
	check_integration(&state, t, write, 0);
	if (info.len) {
		if (!write)
			reject(1, "finish it with 'git commit --allow-empty --no-edit --cleanup=strip' (its pre-commit hook must run 'ait hook pre-commit')", "not allowed: this merge needs its records pinned and validated again");
		git_run(info.buf, hook_index, NULL, "update-index --index-info");
		for_each_string_list_item(item, &clean)
			git_run(NULL, hook_index, NULL, "checkout-index -f -- %s", item->string);
	}
}

void integrate_at(struct tree *t)
{
	integrate_records(t, 1);
}

static int managed_trailers(const char *text)
{
	struct string_list lines = STRING_LIST_INIT_DUP;
	struct string_list_item *item;

	string_list_split(&lines, text, "\n", -1);
	for_each_string_list_item(item, &lines) {
		char *colon = strchr(item->string, ':');

		if (colon && (!strcasecmp(xstrndup(item->string, colon - item->string), "Ait-Request") ||
		    !strcasecmp(xstrndup(item->string, colon - item->string), "Ait-Agent") ||
		    !strcasecmp(xstrndup(item->string, colon - item->string), "Ait-Session")))
			return 1;
	}
	return 0;
}

/* Parents describe the received state; introducing commits only describe sources. */
static struct string_list pull_parents(struct pull *p)
{
	struct string_list parents = STRING_LIST_INIT_DUP;
	struct string_list_item *end;

	string_list_insert(&parents, head);
	for_each_string_list_item(end, &p->ends)
		string_list_insert(&parents, end->string);
	return parents;
}

/* 7.1.5 and 7.12.6 use the same final-blob rule for request commits. */
static int fresh_records(struct pull *p, struct state *state, struct tree *t,
			 const struct string_list *parents)
{
	struct string_list_item *parent;
	size_t i;

	for (i = 0; i < state->nr; i++) {
		const char *path = xstrfmt(".ait/%s", state->r[i]->id);
		const char *oid = blob_at(t, path);
		int same = 0;

		for_each_string_list_item(parent, parents)
			same |= !strcmp(oid, blob_at(pull_tree(p, parent->string), path) ?: "");
		if (!same)
			return 1;
	}
	return 0;
}

/* 9.1: an existing unverified parent must have this contract's same pins. */
static int parent_unverified(struct pull *p, struct record *r,
			     const struct string_list *parents)
{
	struct string_list_item *parent;

	for_each_string_list_item(parent, parents) {
		struct version *v = version_at(p, r->id, parent->string);

		if (v && !v->r->bad && is_current(v->r) &&
		    !strcmp(pins(v->r), pins(r)) && unverified(v->r, pull_tree(p, parent->string)))
			return 1;
	}
	return 0;
}

/* Commit trailers ignore patch dividers (git v2.53.0 pretty.c:1767). */
void integration_message(const char *path)
{
	struct pull p = PULL_INIT;
	struct tree *t = tree_load(line1(git_run(NULL, hook_index, NULL, "write-tree")));
	struct string_list sides;
	struct state state = { 0 };
	struct strbuf msg = STRBUF_INIT;
	const char *init = object(xstrfmt("%s:.ait/init", t->oid), 1, NULL, NULL);
	char rid[16];

	if (!init || strcmp(init, "version 2\n") || !integrating())
		return;
	integrate_records(t, 0);
	sides = integration_sides(&p);
	state_load(&state, t);
	if (!fresh_records(&p, &state, t, &sides))
		return;
	session(-1);
	if (read_text(&msg, path))
		reject(128, NULL, "could not read '%s'", path);
	while (managed_trailers(git_run(msg.buf, NULL, NULL, "interpret-trailers --parse --no-divider"))) {
		const char *clean = git_run(msg.buf, NULL, NULL, "interpret-trailers --no-divider --if-exists replace --trim-empty --trailer %s --trailer %s --trailer %s", "Ait-Request:", "Ait-Agent:", "Ait-Session:");

		strbuf_reset(&msg);
		strbuf_addstr(&msg, clean);
	}
	newid(rid, 'R');
	write_state(path, git_run(msg.buf, NULL, NULL, "interpret-trailers --no-divider --trailer %s --trailer %s --trailer %s", xstrfmt("Ait-Request: %s", rid), xstrfmt("Ait-Agent: %s", *sid ? agent : "unknown"), xstrfmt("Ait-Session: %s", *sid ? sid : "-")));
	reopen_warnings(&p, &state, t, "in this merge");
}

static void structure_error(struct pull *p, struct verdict *v)
{
	struct string_list_item *ref;

	if (v->k)
		for_each_string_list_item(ref, &p->refs) {
			struct version *bad = version_at(p, v->k->id, ref->util);

			if (strcmp(ref->string, "HEAD") && bad && accident(p, bad) == 1)
				reject(1, xstrfmt("'%s' has to be fixed on branch '%s' first", v->k->id, ref->string), "taking '%s' needs '%s' as '%s' has it, but '%s' at '%s' (%s) was changed outside ait's rules", p->ends.nr ? p->ends.items[0].string : head, v->k->id, ref->string, v->k->id, ref->string, (char *)ref->util);
		}
	if (v->o)
		v->hint = xstrfmt("reopen it in the same request: '%s future amend %s -m <why>'", pull_command(p, NULL, NULL), v->o->id);
	reject(v->code, v->hint, "%s", v->msg);
}

static int receive(struct pull *p)
{
	struct state result = result_state(p);
	struct merged m = { .conflicts = STRING_LIST_INIT_DUP, .stages = STRING_LIST_INIT_DUP, .messages = STRBUF_INIT };
	struct strvec order = STRVEC_INIT;
	struct tree *t;
	struct verdict v;
	struct string_list_item *item;
	struct strbuf info = STRBUF_INIT, stages = STRBUF_INIT, message = STRBUF_INIT, helper_out = STRBUF_INIT;
	struct string_list parents = pull_parents(p);
	int ff, fresh;
	char rid[16];
	size_t i;

	terminal_warnings(p);
	incoming_warnings(p);
	if (p->ends.nr) {
		if (!merge_order(p, &result, &order, &m))
			split_error(p, &m);
	} else {
		m.tree = restore_pins(p, &result);
	}
	t = tree_load(m.tree);
	if (p->force && m.conflicts.nr)
		reject(1, NULL, "--force cannot take a pull whose code conflicts: '%s'", m.conflicts.items[0].string);
	if (p->clauses.nr) {
		session(0);
		t = tree_load(pull_helpers(t->oid, &p->clauses, p->after_option, p->force, &m.conflicts,
			xstrfmt("if it is trivial, run '%s --force -m <why>'", pull_command(p, NULL, NULL)), &helper_out));
		result = (struct state){0};
		state_load(&result, t);
		if (!strcmp(t->oid, head_tree()->oid)) {
			printf("already");
			for_each_string_list_item(item, &p->selections)
				if (*item->string != 'u')
				printf(" %s", item->string);
			putchar('\n');
			return 0;
		}
	}
	for (i = 0; i < result.nr; i++) {
		struct record *r = result.r[i];
		struct string_list_item *selected = string_list_lookup(&p->selections, r->id);
		struct version *initial = selected ? ((struct selection *)selected->util)->result : NULL;
		int changed = 0;
		const char *before_pins = pins(r), *changed_path = NULL;

		if (!is_live(r))
			continue;
		if (initial && !string_list_has_string(&((struct selection *)selected->util)->remaining, initial->oid) && strcmp(blob_at(t, xstrfmt(".ait/%s", r->id)), initial->oid)) {
			for_each_string_list_item(item, &initial->r->f[PREV])
				string_list_insert(&r->f[PREV], item->string);
			changed = 1;
		}
		for_each_string_list_item(item, &r->f[FILES]) {
			const char *path = scope_path(item->string);
			struct entry *e = file_entry(t, path);

			if (string_list_has_string(&m.conflicts, path))
				continue;
			if (e && strcmp(item->string, xstrfmt("%s %s %s %s", path, e->mode, e->oid, is_def(item->string) ? "def" : "input"))) {
				item->string = xstrfmt("%s %s %s %s", path, e->mode, e->oid, is_def(item->string) ? "def" : "input");
				changed = 1;
				if (!changed_path)
					changed_path = path;
			}
		}
		if (is_current(r))
			for_each_string_list_item(item, &m.conflicts) {
				struct string_list_item *scope;
				int checked_path = declares(r, item->string);

				for_each_string_list_item(scope, &r->f[PROVIDER])
					checked_path |= !strcmp(scope_path(scope->string), item->string) && scope_changed(scope->string, t);
				if (checked_path)
					reject(1, xstrfmt("reopen it in the same request: '%s %s -m <why>'", pull_command(p, NULL, NULL), reopen_clauses(&result, r, NULL)), "taking '%s' conflicts in what '%s' checked: '%s'", p->ends.nr ? p->ends.items[0].string : head, r->id, item->string);
			}
		if (is_current(r) && (changed || unverified(r, t))) {
			struct selection *sel = NULL;
			struct string_list_item *hit = string_list_lookup(&p->selections, r->id);
			struct version *mine = version_at(p, r->id, head);
			const char *origin, *path = changed_path ? changed_path : scope_path(val(r, PROVIDER));

			if (hit)
				sel = hit->util;
			if (mine && (!is_current(mine->r) || strcmp(pins(mine->r), before_pins)))
				mine = NULL;
			origin = mine ? head : sel && sel->incoming ? (p->ends.nr == 1 ? p->ends.items[0].string : sel->incoming) : head;
			if (!changed && parent_unverified(p, r, &parents)) {
				if (sel && sel->incoming)
					warning("'%s' comes in unverified", r->id);
				continue;
			}
			if (!p->force) {
				char *command = pull_command(p, NULL, NULL);
				char *hint = xstrfmt("%s: '%s %s -m <why>'", mine ? "reopen it in the same request" : "take it reopened", command, reopen_clauses(&result, r, NULL));

				if (code_found(r, t) && !m.conflicts.nr)
					hint = xstrfmt("%s\nor, if %s trivial for '%s': '%s --force -m <why>'", hint, mine ? "the change is" : "your changes are", r->id, command);
				if (!p->ends.nr && sel && sel->incoming)
					reject(1, hint, "'%s' at '%s' checked code your branch no longer has: '%s'", r->id, origin, path);
				reject(1, hint, mine ? "taking '%s' changes what contract '%s' checked: '%s'" : "taking '%s' merges your changes into what '%s' checked: '%s'", sel && sel->incoming ? sel->incoming : p->ends.nr ? p->ends.items[0].string : head, r->id, path);
			}
			if (!code_found(r, t))
				reject(1, NULL, "cannot check '%s': its provider cannot be found", r->id);
			repin(r, PROVIDER, t, 0);
			put(r, CHECK, confirmation(sel ? sel : &(struct selection){ .mine = mine }, r));
			strbuf_addf(&helper_out, "check %s %s\n", r->id, starts_with(val(r, CHECK), "reported ") ? "reported" : "forced");
			changed = 1;
		}
		if (changed) {
			const char *old = initial ? initial->oid : blob_at(head_tree(), xstrfmt(".ait/%s", r->id));

			if (!initial || string_list_has_string(&((struct selection *)selected->util)->remaining, initial->oid))
				put(r, PREV, old);
			else
				string_list_insert(&r->f[PREV], old);
			string_list_remove(&r->f[PREV], "-", 0);
			index_line(&info, "100644", line1(git_run(record_text(r, ALL_KEYS), NULL, NULL, "hash-object -w --stdin")), xstrfmt(".ait/%s", r->id));
		}
	}
	if (info.len)
		t = tree_load(tree_with(t->oid, info.buf, NULL));
	/* Conflict paths defer file equality only; all other state invariants hold */
	if (check_state_except(&result, t, &v, &m.conflicts))
		structure_error(p, &v);
	ff = p->ends.nr == 1 && ancestor(head, p->ends.items[0].string) && !strcmp(t->oid, pull_tree(p, p->ends.items[0].string)->oid);
	fresh = fresh_records(p, &result, t, &parents);
	session(fresh && !m.conflicts.nr);
	check_local(t, &m.conflicts, !ff);
	if (p->msg.len) {
		strbuf_addf(&message, "%s\n", p->msg.buf);
	} else {
		strbuf_addstr(&message, "[CONTRACT] pull");
		for (i = 0; i < p->named.nr; i++)
			strbuf_addf(&message, " %s", p->named.v[i]);
		strbuf_addch(&message, '\n');
	}
	if (fresh && !m.conflicts.nr)
		newid(rid, 'R');
	if (fresh && !m.conflicts.nr)
		strbuf_addf(&message, "\nAit-Request: %s\nAit-Agent: %s\nAit-Session: %s\n", rid, agent, sid);
	if (m.conflicts.nr) {
		struct string_list ends = STRING_LIST_INIT_DUP;

		for_each_string_list_item(item, &m.conflicts)
			index_line(&stages, NULL, NULL, item->string);
		for_each_string_list_item(item, &m.stages)
			strbuf_addf(&stages, "%s\n", item->string);
		for (i = 0; i < order.nr; i++)
			string_list_append(&ends, order.v[i]);
		pull_conflict(t->oid, &ends, stages.buf, message.buf);
		fputs(m.messages.buf, stdout);
		puts("Automatic merge failed; fix conflicts and then commit the result.");
		suggest("commit the resolution first: 'git commit --allow-empty --no-edit --cleanup=strip', then continue as git says");
	} else if (publish_pull(t->oid, message.buf, index_load(tree_load(line1(git("write-tree")))), &p->ends, ff)) {
		reject(1, "rerun this command", "HEAD moved while running; nothing changed");
	}
	if (fresh && !m.conflicts.nr)
		reopen_warnings(p, &result, t, "by this pull");
	if (fresh && !m.conflicts.nr)
		printf("request %s\n%s", rid, helper_out.buf);
	for_each_string_list_item(item, &p->selections) {
		struct selection *s = item->util;

		if (s->incoming)
			printf("included %s from '%s'\n", s->id, p->ends.nr == 1 ? p->ends.items[0].string : s->incoming);
		else if (!s->mine || strcmp(s->mine->oid, s->result->oid))
			printf("kept %s\n", s->id);
	}
	return m.conflicts.nr ? 1 : 0;
}

int cmd_pull(int argc, const char **argv)
{
	struct pull p = PULL_INIT;
	struct string_list_item *item;
	int i, helper = 0;
	char *after_option;
	struct strvec args = STRVEC_INIT;

	after_option = pull_options(argc, argv, &args, &p.force, &p.msg);
	for (i = 0; i < args.nr; i++) {
		const char *a = args.v[i];

		if (helper || !strcmp(a, "future") || !strcmp(a, "amend") || !strcmp(a, "undertake") || !strcmp(a, "release") || !strcmp(a, "demand") || !strcmp(a, "offer") || !strcmp(a, "drop") || !strcmp(a, "cancel")) {
			if (!helper)
				p.after_option = after_option + i;
			helper = 1;
			strvec_push(&p.clauses, a);
		} else {
			strvec_push(&p.named, selection_add(&p, a)->id);
		}
	}
	if (!p.selections.nr)
		reject(129, NULL, "contract pull needs a contract ID");
	if ((p.chosen || helper || p.force) && !p.msg.len)
		reject(129, NULL, "a message is required: give -m <msg> or -F <file>");
	precondition(0);
	check_head(&p);
	snapshot_refs(&p);
	plan(&p);
	check_incoming(&p);
	check_choices(&p);
	if (!p.ends.nr && !helper && !p.force) {
		for_each_string_list_item(item, &p.selections) {
			struct selection *s = item->util;

			if (!s->mine || strcmp(s->mine->oid, s->result->oid))
				break;
		}
		if (!item || item == p.selections.items + p.selections.nr) {
			session(0);
			printf("already");
			for_each_string_list_item(item, &p.selections)
				if (*item->string != 'u')
				printf(" %s", item->string);
			putchar('\n');
			return 0;
		}
	}
	return receive(&p);
}
