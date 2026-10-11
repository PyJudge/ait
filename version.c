/*
 * the versions of a contract file and what is judged from them alone (spec 2.2.4,
 * 2.3.3, 1.5d, 9.1): containment, accidents, reopen commits and new unverified
 */
#include "ait.h"
#include "strvec.h"

/*
 * every version read once: "ID OID" for a blob, "ID MODE OID" for a tree entry,
 * whose validity is its own; an entry is inserted only once its version is
 * made, since making one inserts others and so moves the entries
 */
static struct string_list versions = STRING_LIST_INIT_DUP;

static struct version *version_new(struct record *r, const char *oid)
{
	struct version *v = xcalloc(1, sizeof(*v));

	v->oid = xstrdup(oid);
	v->r = r;
	v->body = record_text(r, MEANING_KEYS);
	v->accident = -1;
	string_list_init_dup(&v->ancestors);
	return v;
}

/* the version OID of contract file ID; "-", the prev of a first version (2.2.4), is none */
struct version *version_of(const char *id, const char *oid)
{
	char *key;
	struct string_list_item *item;
	struct version *v;

	if (!oid || !strcmp(oid, "-"))
		return NULL;
	key = xstrfmt("%s %s", id, oid);
	item = string_list_lookup(&versions, key);
	if (item)
		return item->util;
	v = version_new(record_decode(id, oid), oid);
	string_list_insert(&versions, key)->util = v;
	return v;
}

/* the version of ID that COMMIT holds, judged as its tree entry (mode and path too, 2.1); NULL when it has none */
struct version *version_in(const char *commit, const char *id)
{
	struct entry *e = file_entry(tree_load(commit), xstrfmt(".ait/%s", id));
	char *key;
	struct string_list_item *item;
	struct record *r;
	struct version *v;

	if (!e)
		return NULL;
	key = xstrfmt("%s %s %s", id, e->mode, e->oid);
	item = string_list_lookup(&versions, key);
	if (item)
		return item->util;
	r = record_from_entry(e, id, 0);
	v = r->bad ? version_new(r, e->oid) : version_of(id, e->oid);
	string_list_insert(&versions, key)->util = v;
	return v;
}

/* 2.2.4: the versions V includes; a missing blob ends its branch, but its OID still counts */
void walk_version(struct version *v)
{
	struct string_list todo = STRING_LIST_INIT_DUP;
	struct string_list_item *prev;
	size_t i;

	if (v->walked++)
		return;
	string_list_append(&todo, v->oid);
	for (i = 0; i < todo.nr; i++) {
		const char *oid = todo.items[i].string;
		struct version *x;

		if (string_list_has_string(&v->ancestors, oid))
			continue;
		string_list_insert(&v->ancestors, oid);
		x = version_of(v->r->id, oid);
		if (!x->r->bad)
			for_each_string_list_item(prev, &x->r->f[PREV])
				if (strcmp(prev->string, "-"))
					string_list_append(&todo, prev->string);
	}
	string_list_clear(&todo, 0);
}

/* 2.2.4: A includes B */
int contains(struct version *a, struct version *b)
{
	if (!a || !b)
		return 0;
	walk_version(a);
	return string_list_has_string(&a->ancestors, b->oid);
}

/* 2.2.4: the versions of VERSIONS (each in ->util) that no other of them includes */
struct string_list maximal(struct string_list *list)
{
	struct string_list result = STRING_LIST_INIT_DUP;
	struct string_list_item *item, *other;

	for_each_string_list_item(item, list) {
		if (!item->util)
			continue;
		for_each_string_list_item(other, list)
			if (other != item && other->util && contains(other->util, item->util))
				break;
		if (other == list->items + list->nr)
			string_list_insert(&result, item->string)->util = item->util;
	}
	return result;
}

/* 2.3.2: an ended version, which no transition leaves */
int ended(struct version *v)
{
	return in_state(v->r, "cancelled") || in_state(v->r, "withdrawn") || in_state(v->r, "fulfilled");
}

/* 2.3.2: V is ended, or includes an ended version */
int includes_ended(struct version *v)
{
	struct string_list_item *item;

	walk_version(v);
	for_each_string_list_item(item, &v->ancestors) {
		struct version *x = version_of(v->r->id, item->string);

		if (!x->r->bad && ended(x))
			return 1;
	}
	return 0;
}

/*
 * 2.3.3, 7.1.3-4: B follows A (none when NULL) with the same body or by a
 * transition of 2.3, of the gate's only when GATE (else any); role and
 * creator stay
 */
int follows(struct record *a, struct record *b, int gate)
{
	if (!a)
		return record_step(b->id, "-", val(b, STATE), gate ? 1 : -1);
	if (strcmp(val(a, ROLE), val(b, ROLE)) || strcmp(val(a, CREATOR), val(b, CREATOR)))
		return 0;
	return !strcmp(record_text(a, MEANING_KEYS), record_text(b, MEANING_KEYS)) ||
	       record_step(b->id, val(a, STATE), val(b, STATE), gate ? 1 : -1);
}

/* 1.5a: R's check line confirms the pins R has */
int confirmed(struct record *r)
{
	return r->f[CHECK].nr && !strcmp(strchr(val(r, CHECK), ' ') + 1, pins(r));
}

/*
 * 2.2.2: the check line of current R confirmed now: reported for a result
 * (-r), or when a starting version in FROM (each in ->util) has a reported
 * line with R's pins, as a pass is not undone by a later --force
 */
char *check_line(struct record *r, int result, struct string_list *from)
{
	char *reported = xstrfmt("reported %s", pins(r));
	struct string_list_item *item;

	for_each_string_list_item(item, from)
		result |= !strcmp(val(((struct version *)item->util)->r, CHECK), reported);
	return result ? reported : xstrfmt("forced %s", pins(r));
}

/*
 * 2.3.3: V is an accident (1), is not (0), or cannot be told for a missing
 * blob (-1). Judged from V and its prev alone.
 */
int accident(struct version *v)
{
	struct string_list_item *item;
	int good = 0, unknown = 0, terminal = 0, same_end = 0;

	if (v->accident != -1)
		return v->accident == 2 ? -1 : v->accident;
	v->accident = 2;
	if (v->r->bad)
		return v->accident = object(v->oid, 0, NULL, NULL) ? 1 : -1;
	if (is_current(v->r) && !confirmed(v->r))
		return v->accident = 1;
	/* an ended version it includes, itself aside, leaves no way out but its body (decision 28) */
	walk_version(v);
	for_each_string_list_item(item, &v->ancestors) {
		struct version *x = version_of(v->r->id, item->string);

		if (x != v && !x->r->bad && ended(x)) {
			terminal = 1;
			same_end |= !strcmp(x->body, v->body);
		}
	}
	if (terminal && !same_end)
		return v->accident = 1;
	for_each_string_list_item(item, &v->r->f[PREV]) {
		struct version *x;
		int bad;

		if (!strcmp(item->string, "-")) {
			good |= follows(NULL, v->r, 0);
			continue;
		}
		x = version_of(v->r->id, item->string);
		bad = accident(x);
		unknown |= bad == -1;
		good |= !bad && follows(x->r, v->r, 0);
	}
	return v->accident = good ? 0 : unknown ? -1 : 1;
}

/* 5.10, 7.1.3, 7.12.1: a malformed version or an accident is no starting version where it came from elsewhere */
int wrong_version(struct version *v)
{
	return v->r->bad || accident(v) == 1;
}

/*
 * 1.5d: V, ID's version at a commit with PARENTS, reopens a current version:
 * V is a new future version, a current C is a parent's version or in V's
 * prev, and no parent's future version includes C or one with C's body
 */
int reopens(struct version *v, const struct string_list *parents)
{
	struct string_list currents = STRING_LIST_INIT_DUP;
	struct string_list_item *item, *parent, *x;

	if (!v || v->r->bad || !in_state(v->r, "future"))
		return 0;
	for_each_string_list_item(parent, parents) {
		struct version *up = version_in(parent->string, v->r->id);

		if (contains(up, v))
			return 0;
		if (up && is_current(up->r))
			string_list_insert(&currents, up->oid);
	}
	for_each_string_list_item(item, &v->r->f[PREV]) {
		struct version *prev = version_of(v->r->id, item->string);

		if (prev && !prev->r->bad && is_current(prev->r))
			string_list_insert(&currents, prev->oid);
	}
	for_each_string_list_item(item, &currents) {
		struct version *current = version_of(v->r->id, item->string);
		int carried = 0;

		/* a blob needed for the judgment that is missing counts as carried: it is not counted (1.5d) */
		for_each_string_list_item(parent, parents) {
			struct version *up = version_in(parent->string, v->r->id);

			if (!up || !in_state(up->r, "future"))
				continue;
			walk_version(up);
			for_each_string_list_item(x, &up->ancestors) {
				struct version *before = version_of(v->r->id, x->string);

				carried |= before->r->bad || !strcmp(current->body, before->body);
			}
		}
		if (!carried)
			return 1;
	}
	return 0;
}

/*
 * the commits of HEAD's history that change PATHS against a parent, newest
 * first, each with its parents in ->util: a commit that changes none of them
 * against a parent judges as that parent does (13.4)
 */
static void history(const struct string_list *tips, struct strvec *paths, struct string_list *out)
{
	struct string_list lines = STRING_LIST_INIT_DUP;
	struct strvec args = STRVEC_INIT;
	struct string_list_item *item;

	strvec_pushl(&args, "log", "--full-history", "--topo-order", "--format=%H %P", NULL);
	for_each_string_list_item(item, tips)
		strvec_push(&args, item->string);
	strvec_push(&args, "--");
	strvec_pushv(&args, paths->v);
	string_list_split(&lines, git_args(&args, NULL, NULL, NULL), "\n", -1);
	for_each_string_list_item(item, &lines) {
		struct string_list ids = STRING_LIST_INIT_DUP, *parents = xcalloc(1, sizeof(*parents));
		size_t i;

		if (!*item->string)
			continue;
		string_list_init_dup(parents);
		string_list_split(&ids, item->string, " ", -1);
		for (i = 1; i < ids.nr; i++)
			string_list_append(parents, ids.items[i].string);
		string_list_append(out, ids.items[0].string)->util = parents;
		string_list_clear(&ids, 0);
	}
	string_list_clear(&lines, 0);
}

/*
 * 1.5d: who reopened ID as HEAD's history has it: the agents of the reopen
 * commits after which ID was never current again in that history; a commit
 * without trailers or with agent "unknown" names nobody
 */
void reopeners(const char *id, struct string_list *agents)
{
	struct string_list commits = STRING_LIST_INIT_DUP, current = STRING_LIST_INIT_DUP;
	struct strvec paths = STRVEC_INIT;
	struct string_list_item *item, *later;

	if (!head)
		return;
	strvec_push(&paths, xstrfmt(".ait/%s", id));
	history(single(head), &paths, &commits);
	for_each_string_list_item(item, &commits) {
		struct version *v = version_in(item->string, id);
		int again = 0;
		char *who;

		if (v && !v->r->bad && is_current(v->r)) {
			string_list_append(&current, item->string);
			continue;
		}
		if (!reopens(v, item->util))
			continue;
		/* ID current again in a commit that has this one as an ancestor */
		for_each_string_list_item(later, &current) {
			int rc;

			git_run(NULL, NULL, &rc, "merge-base --is-ancestor %s %s", item->string, later->string);
			again |= !rc;
		}
		who = line1(git("log -1 --format=%(trailers:key=Ait-Agent,valueonly) %s", item->string));
		if (!again && *who && strcmp(who, "unknown"))
			string_list_insert(agents, who);
	}
}

/* 1.3: R is a promise of agent ME: a future contract ME made, or one ME reopened (1.5d), who reopened it into BY */
int my_promise(struct record *r, const char *me, struct string_list *by)
{
	if (!r || !me || r->bad || !in_state(r, "future"))
		return 0;
	reopeners(r->id, by);
	return made_by(r, me) || string_list_has_string(by, me);
}

/* 9.1, 13.4: ID is unverified at COMMIT, worked out once per commit */
static int unverified_in(const char *commit, const char *id)
{
	static struct string_list known = STRING_LIST_INIT_DUP;
	struct string_list_item *item = string_list_insert(&known, xstrfmt("%s %s", commit, id));

	if (!item->util) {
		struct version *v = version_in(commit, id);

		item->util = v && !v->r->bad && unverified(v->r, tree_load(commit)) ? (void *)"yes" : (void *)"no";
	}
	return !strcmp(item->util, "yes");
}

/*
 * 9.1: R, at T, is newly unverified there: unverified at T, and no parent had
 * it unverified with the same pins
 */
int newly_unverified(struct record *r, struct tree *t, const struct string_list *parents)
{
	struct string_list_item *parent;

	if (!unverified(r, t))
		return 0;
	for_each_string_list_item(parent, parents)
		if (unverified_in(parent->string, r->id) &&
		    !strcmp(record_text(version_in(parent->string, r->id)->r, PIN_KEYS), record_text(r, PIN_KEYS)))
			return 0;
	return 1;
}

/* 9.1 for H8: the latest commit of the history of TIPS where R newly became unverified with the pins it has now */
const char *unverified_since(struct record *r, const struct string_list *tips)
{
	struct string_list commits = STRING_LIST_INIT_DUP;
	struct strvec paths = STRVEC_INIT;
	struct string_list_item *item;

	if (!head)
		return NULL;
	strvec_push(&paths, xstrfmt(".ait/%s", r->id));
	for_each_string_list_item(item, &r->f[PROVIDER])
		strvec_push(&paths, scope_path(item->string));
	history(tips, &paths, &commits);
	for_each_string_list_item(item, &commits) {
		struct version *v = version_in(item->string, r->id);

		if (v && !v->r->bad && !strcmp(record_text(v->r, PIN_KEYS), record_text(r, PIN_KEYS)) &&
		    newly_unverified(v->r, tree_load(item->string), item->util))
			return item->string;
	}
	return NULL;
}

/* 5.10: add to SET each record that S ties by a reference to one in it: an X and its target, a link and its D and O */
static void entangle(struct state *s, struct string_list *set)
{
	size_t i, nr;
	int k;

	do {
		nr = set->nr;
		for (i = 0; i < s->nr; i++) {
			struct record *r = s->r[i];
			const char *ends[2] = { NULL, NULL };

			if (*r->id == 'u') {
				ends[0] = link_end_id(r, 0);
				ends[1] = link_end_id(r, 1);
			} else if (*r->id == 'X' && !r->bad) {
				ends[0] = val(r, TARGET);
			}
			for (k = 0; k < 2 && ends[k]; k++)
				if (string_list_has_string(set, r->id) || string_list_has_string(set, ends[k])) {
					string_list_insert(set, r->id);
					string_list_insert(set, ends[k]);
				}
		}
	} while (set->nr != nr);
}

/* 5.10: COMMIT holds the records of SET as versions that are no accidents, pass 5.9 a-d together and keep their bundles */
static int holds(const char *commit, struct string_list *set)
{
	struct state all = { 0 }, part = { 0 };
	struct tree *t = tree_load(commit);
	struct verdict v;
	size_t i;

	state_load(&all, t);
	for (i = 0; i < all.nr; i++) {
		struct record *r = all.r[i];

		if (!string_list_has_string(set, r->id))
			continue;
		if (r->bad || (strcmp(r->id, "init") && accident(version_in(commit, r->id)) == 1))
			return 0;
		*state_add(&part, r->id) = *r;
	}
	return !check_state(&part, t, &v);
}

/* PATH is in the index and the work tree */
static int checked_out(const char *path)
{
	int rc;

	git_run(NULL, NULL, &rc, "ls-files --error-unmatch -- %s", path);
	return !rc && !access(xstrfmt("%s/%s", top, path), F_OK);
}

/*
 * 5.10: GOOD for the records of SET, which grows by the records entangled
 * with them (reached through references at HEAD or at GOOD): the first
 * commit down HEAD's first parents that holds them, its state into GS; NULL
 * when there is none. The two decide each other, so GOOD is sought again
 * until the set grows no more.
 */
static const char *find_good(struct string_list *set, struct state *gs)
{
	struct string_list commits = STRING_LIST_INIT_DUP;
	struct string_list_item *c;
	struct state hs = { 0 };
	const char *good;
	size_t nr;

	state_load(&hs, head_tree());
	string_list_split(&commits, git("rev-list --first-parent %s", head), "\n", -1);
	do {
		nr = set->nr;
		entangle(&hs, set);
		good = NULL;
		for_each_string_list_item(c, &commits)
			if (*c->string && holds(c->string, set)) {
				good = c->string;
				break;
			}
		memset(gs, 0, sizeof(*gs));
		if (good) {
			state_load(gs, tree_load(good));
			entangle(gs, set);
		}
	} while (set->nr != nr);
	return good;
}

/*
 * 5.10: into HINT, the records entangled with V's go back together to GOOD,
 * or go when there is none, and GOOD's spec files they declare come back
 * where the index or work tree lacks them
 */
static void entangled_restore(struct verdict *v, struct strbuf *hint)
{
	struct string_list set = STRING_LIST_INIT_DUP, restore = STRING_LIST_INIT_DUP, remove = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct state gs = { 0 };
	const char *good;

	if (v->k)
		string_list_insert(&set, v->k->id);
	if (v->o)
		string_list_insert(&set, v->o->id);
	good = find_good(&set, &gs);
	for_each_string_list_item(item, &set) {
		char *path = xstrfmt(".ait/%s", item->string);
		struct entry *now = file_entry(head_tree(), path), *then = good ? file_entry(tree_load(good), path) : NULL;
		struct record *r = good ? state_get(&gs, item->string) : NULL;
		struct string_list_item *f;

		if (then && (!now || strcmp(now->oid, then->oid) || strcmp(now->mode, then->mode)))
			string_list_append(&restore, path);
		else if (!then && now)
			string_list_append(&remove, path);
		if (!r || !is_live(r))
			continue;
		for_each_string_list_item(f, &r->f[FILES])
			if (!checked_out(scope_path(f->string)))
				string_list_append(&restore, scope_path(f->string));
	}
	if (restore.nr)
		strbuf_addf(hint, "restore %s as in '%s'", quoted_paths(&restore), good);
	if (remove.nr)
		strbuf_addf(hint, "%sremove %s", restore.nr ? ", " : "", quoted_paths(&remove));
}

/*
 * 5.10: the hint that mends HEAD's error V, then contract commit: what is no
 * record goes, the init file is written again, records go back to GOOD. The
 * staged CODE of a command that checks it is set aside for that commit and
 * staged again (5.9a local changes); RERUN runs the command again.
 */
char *head_repair(struct verdict *v, struct string_list *code, int rerun)
{
	struct strbuf hint = STRBUF_INIT;
	const char *commit = "'ait contract commit -m <why>'";
	struct version *at;

	if (v->k && v->k->mend) {
		/* what is no record goes; it is committed only when HEAD has it */
		strbuf_addstr(&hint, v->k->mend);
		if (!file_entry(head_tree(), xstrfmt(".ait/%s", v->k->id)))
			commit = NULL;
	} else if (v->k && !strcmp(v->k->id, "init")) {
		/* 2.1.2a: the init file has one content */
		strbuf_addstr(&hint, "rewrite '.ait/init' as the single line 'version 2' with mode 100644");
		commit = through_ait(single(".ait/init"));
	} else if (strcmp(v->word, "incomplete")) {
		entangled_restore(v, &hint);
	} else if (v->k && (at = version_in(head, v->k->id)) && !at->r->bad && is_current(at->r)) {
		/* a broken bundle (5.8 E_INCOMPLETE) needs no restore: the gate pins it again (6.2), checking HEAD's current one */
		commit = "'ait contract commit -m <why>' to get the tasks";
	}
	if (commit && code && code->nr)
		strbuf_addf(&hint, "%sunstage %s first", hint.len ? ", " : "", quoted_paths(code));
	if (commit)
		strbuf_addf(&hint, "%s%s", hint.len ? ", then " : "run ", commit);
	if (commit && code && code->nr)
		strbuf_addf(&hint, ", stage %s again", quoted_paths(code));
	if (rerun)
		strbuf_addstr(&hint, ", then rerun this command");
	return strbuf_detach(&hint, NULL);
}

/* 2.3.3: the version of contract ID at COMMIT is an accident */
int accident_at(const char *commit, const char *id)
{
	struct version *v = commit ? version_in(commit, id) : NULL;

	return v && !v->r->bad && strcmp(id, "init") && accident(v) == 1;
}

/* 5.10 E_ACCIDENT: R's version at COMMIT, HEAD as it is judged, is an accident (2.3.3), as a verdict without its hint */
static int accident_verdict(const char *commit, struct record *r, struct verdict *out)
{
	if (!accident_at(commit, r->id))
		return 0;
	*out = (struct verdict){ 1, "accident", NULL, xstrfmt("'%s' at HEAD was changed outside ait's rules", r->id), r, NULL };
	return 1;
}

/* 5.10: R of HEAD is an accident (2.3.3), as a verdict */
int accident_at_head(struct record *r, struct verdict *out)
{
	if (!accident_verdict(head, r, out))
		return 0;
	out->hint = head_repair(out, NULL, 0);
	return 1;
}

/* 5.10: the errors of the state at COMMIT, each a verdict under its first line: structure, format and bundle (5.9), and accidents */
void commit_errors(const char *commit, struct string_list *out)
{
	struct tree *t = tree_load(commit);
	struct state s = { 0 };
	struct verdict v;
	size_t i;

	state_load(&s, t);
	state_errors(&s, t, out);
	for (i = 0; i < s.nr; i++)
		if (accident_verdict(commit, s.r[i], &v))
			string_list_insert(out, v.msg)->util = xmemdupz(&v, sizeof(v));
}

/*
 * 5.10: the base versions a request on HEAD's records S judges the records
 * it writes, WRITTEN, against: for those entangled with HEAD's errors (its
 * structure errors and accidents), GOOD's version, or none, as a version no
 * rule made is no version of the contract; the others keep HEAD's. What the
 * request leaves keeps HEAD's error, which refuses it.
 */
void base_versions(struct state *s, struct string_list *written)
{
	struct string_list set = STRING_LIST_INIT_DUP, errors = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct state gs = { 0 };
	const char *good;

	if (!written->nr)
		return;
	commit_errors(head, &errors);
	for_each_string_list_item(item, &errors) {
		struct verdict *e = item->util;

		/* a broken bundle is pinned again by the gate (6.2), so no version of it is wrong */
		if (!strcmp(e->word, "incomplete"))
			continue;
		string_list_insert(&set, e->k->id);
		if (e->o)
			string_list_insert(&set, e->o->id);
	}
	if (!set.nr)
		return;
	good = find_good(&set, &gs);
	for_each_string_list_item(item, &set) {
		struct record *r = state_get(s, item->string), *then = good ? state_get(&gs, item->string) : NULL;
		const char *blob = good ? blob_at(tree_load(good), xstrfmt(".ait/%s", item->string)) : NULL;

		if (!r || !strcmp(r->id, "init") || !string_list_has_string(written, r->id) || !strcmp(r->base ?: "", blob ?: ""))
			continue;
		r->old = then ? record_copy(then) : NULL;
		r->base = blob;
		r->base_at = good;
		r->rebased = 1;
		/* 2.2.5, 6.2: the creator never changes, so a record keeps its base's; without a base it is new, made by this session */
		if (then)
			r->f[CREATOR] = then->f[CREATOR];
		else
			put(r, CREATOR, xstrfmt("%s %s", agent, sid));
	}
}

/* 5.10: HEAD's first error (structure, format, bundle, then accident), with the hint that mends it; 0 when none */
int head_error(struct verdict *v)
{
	struct state s = { 0 };
	size_t i;

	state_load(&s, head_tree());
	if (check_state(&s, head_tree(), v)) {
		v->hint = head_repair(v, NULL, 0);
		return v->code;
	}
	for (i = 0; i < s.nr; i++)
		if (accident_at_head(s.r[i], v))
			return 1;
	return 0;
}
