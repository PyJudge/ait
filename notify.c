/*
 * ait status (spec 6.3) and subscriptions and notification (6.8, 9): what is
 * shown and sent is computed from the branch each time (rule 1).
 */
#include "ait.h"
#include "run-command.h"
#include <sys/file.h>
#include <signal.h>
#include <time.h>

/* spec 6.3: a scope argument as a git pathspec; git knows no line range and reads "/" outside the repository */
static char *pathspec_of(const char *arg)
{
	if (!strcmp(arg, "/"))
		return xstrdup(":/");
	if (ends_with(arg, "/"))
		return xstrdup(arg);
	return scope_path(arg);
}

static void block(struct branch *b, struct contract *c, struct tree *x, struct tree *h, const char *me)
{
	enum verdict v = judge(b, c, x);
	struct string_list_item *item;
	size_t i;
	int held = 0;

	printf("id %s\nkind %s\ngroup %s\nstate %s\ncreator %s\n", c->id, c->kind, *c->group ? c->group : "-",
	       verdict_word(v), c->creator);
	if (!strcmp(c->kind, "demand"))
		printf("consumer %s\n", scopes_display(&c->consumer, x));
	if (!strcmp(c->kind, "drop"))
		printf("target %s\n", c->target);
	else
		printf("provider %s\n", provider_display(b, c, x));
	for (i = 0; i < c->nspecs; i++)
		printf("spec %s %.*s\n", c->specs[i].engine ? "exec" : "prompt", (int)strcspn(c->specs[i].body, "\n"),
		       c->specs[i].body);
	if (!strcmp(c->kind, "offer")) {
		for (i = 0; i < 2; i++)
			for_each_string_list_item(item, i ? &c->partially : &c->undertakes) {
				struct contract *d = contract_get(b, item->string);

				printf("undertakes %s %s %s %s\n", item->string, d ? verdict_word(judge(b, d, x)) : "-",
				       d ? d->creator : "-", i ? "partial" : "whole");
			}
	}
	if (!strcmp(c->kind, "demand"))
		for_each_string_list_item(item, &b->contracts) {
			struct contract *o = item->util;
			int how = holds(o, c->id);

			held |= live(o) && how;
			if (how)
				printf("undertaken-by %s %s %s %s\n", o->id, verdict_word(judge(b, o, x)), o->creator,
				       how == 'p' ? "partial" : "whole");
		}
	for_each_string_list_item(item, &c->fulfilled) {
		struct string_list_item *f = string_list_lookup(&b->fulfils, item->string);
		struct fulfilment *ful = f ? f->util : NULL;
		const char *rid = strrchr(item->string, '-') + 1;

		if (ful)
			printf("fulfilled %.13s %s %s %s %s\n", rid, ful->by, ful->outcome,
			       strcmp(ful->condition_id, c->condition_id) ? "old" : "valid", ful->commit);
	}
	if (h && touched(b, c, h, x))
		puts("touch");
	putchar('\n');
	if (me && !strcmp(c->creator, me) && promise(c, v) && strcmp(c->kind, "drop"))
		suggest("ait fulfill %s -m <why>", c->id);
	if (!strcmp(c->kind, "demand") && live(c) && !held)
		suggest("ait future offer %s <files> as o undertake %s o -m <why>", quoted(scopes_bare(&c->provider)), c->id);
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
	struct string_list wanted = STRING_LIST_INIT_DUP, pins = STRING_LIST_INIT_DUP, bad = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct branch *b;
	struct tree *x, *h;
	const char *name, *me;
	struct names n;
	int i, rc;

	for (i = 1; i < argc; i++)
		if (*argv[i] == '-' || (!scope_ok(argv[i], 1, 1) && range_of(argv[i], &rc, &rc) != 1))
			reject(129, NULL, "status takes scopes only: '%s'", argv[i]);
	name = start(START_WARNS);
	names_load(&n);
	printf("On contract branch %s%s\n", name,
	       n.local.nr == 1 && !access(checkout_file(), F_OK) ? "" : n.local.nr == 1 ? " (the only one)" : "");
	x = index_tree();
	h = head ? tree_load(head) : NULL;
	strvec_push(&args, "status");
	if (argc > 1)
		strvec_push(&args, "--");
	for (i = 1; i < argc; i++)
		if (!unsorted_string_list_has_string(&pins, pathspec_of(argv[i])))
			string_list_append(&pins, pathspec_of(argv[i]));
	for_each_string_list_item(item, &pins)
		strvec_push(&args, item->string);
	if ((rc = git_pass(&args)))
		return rc;
	putchar('\n');
	b = branch_load(name, NULL, NULL);
	me = session(0) ? agent : NULL;
	invariants(b, b, &bad);
	for_each_string_list_item(item, &bad)
		warning("%s", violation_words(item->util));
	for_each_string_list_item(item, &b->contracts) {
		struct contract *c = item->util;
		enum verdict v = judge(b, c, x);
		int pick = 0, j;

		if (argc > 1) {
			for (j = 1; j < argc; j++) {
				char *err, *q = range_of(argv[j], &rc, &rc) == 1 ? scope_pin(argv[j], x, &err) : NULL;

				pick |= contract_overlaps(c, q ? q : argv[j], x);
			}
		} else {
			pick = (me && !strcmp(c->creator, me) && promise(c, v)) ||
			       (me && !strcmp(c->creator, me) && !strcmp(c->kind, "demand") && v == V_CURRENT) ||
			       v == V_UNVERIFIED || (h && touched(b, c, h, x)) || violates(&bad, c->id);
		}
		if (pick)
			string_list_insert(&wanted, c->id)->util = c;
	}
	for_each_string_list_item(item, &wanted)
		block(b, item->util, x, h, me);
	return 0;
}

static int lock_session(const char *s)
{
	return lock_path(session_path(s, ".lock"), 0);
}

/* v14 4.3: keys go to the end of .seen, which is only added to */
static void append_seen(const char *s, const char *keys)
{
	int fd;

	if (!keys || !*keys)
		return;
	if ((fd = open(session_path(s, ".seen"), O_WRONLY | O_APPEND | O_CREAT, 0666)) < 0 ||
	    write_in_full(fd, keys, strlen(keys)) < 0 || close(fd))
		reject(128, NULL, "write error: '%s': %s", session_path(s, ".seen"), strerror(errno));
}

/* spec 9.1: the tip lines, a snapshot of the ait refs, local and remote-tracking (2.1.2) */
static void tips_now(struct string_list *out)
{
	struct string_list refs = STRING_LIST_INIT_DUP, tracking = STRING_LIST_INIT_DUP;
	struct string_list_item *r, *t;
	struct strvec args = STRVEC_INIT;
	struct names n;

	names_load(&n);
	for_each_string_list_item(t, &n.remote)
		for_each_string_list_item(r, (struct string_list *)t->util)
			string_list_insert(&tracking, r->string);
	strvec_pushl(&args, "for-each-ref", "--format=%(objectname) %(refname)", "refs/heads/ait/", "refs/remotes/", NULL);
	string_list_split(&refs, git_args(&args, NULL, NULL, NULL), "\n", -1);
	for_each_string_list_item(r, &refs) {
		const char *name = strchr(r->string, ' ');

		if (name && (starts_with(name + 1, "refs/heads/ait/") || string_list_has_string(&tracking, name + 1)))
			string_list_append(out, xstrfmt("tip %s", r->string));
	}
}

/*
 * spec 9.3: the receiving state V: the subscriptions, HEAD and the checkout,
 * read with the session's LINES (under its lock)
 */
static char *receiving(const char *s, struct string_list *lines)
{
	struct strbuf v = STRBUF_INIT, chosen = STRBUF_INIT;
	struct string_list_item *item;
	char oid[48] = "";

	session_lines(s, lines);
	for_each_string_list_item(item, lines)
		if (starts_with(item->string, "sub "))
			strbuf_addf(&v, "%s\n", item->string);
	object("HEAD^{commit}", 0, oid, NULL);
	strbuf_read_file(&chosen, checkout_file(), 0);
	strbuf_addf(&v, "head %s\ncheckout %s", oid, chosen.buf);
	return strbuf_detach(&v, NULL);
}

/*
 * spec 9.1: the session's tip lines become TIPS (a snapshot now when NULL),
 * and the keys SKIPPED go to .seen, unless V is no longer AS_OF (v14 9.5-4)
 */
void session_tips(const char *s, const char *as_of, struct string_list *tips, const char *skipped)
{
	struct string_list lines = STRING_LIST_INIT_DUP, keep = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	int fd = lock_session(s);
	char *v = receiving(s, &lines);

	if (!as_of || !strcmp(v, as_of)) {
		for_each_string_list_item(item, &lines)
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
}

/* v14 7.2.4: a subscription line of ARG: "sub id ID" or "sub scope SCOPE" (a range pinned in the index) */
static char *sub_line(const char *arg)
{
	char *err, *pinned;

	if (id_ok(arg, "DOX")) {
		if (!contract_get(branch_load(branch_name(), NULL, NULL), arg))
			reject(128, NULL, "no such contract: '%s'", arg);
		return xstrfmt("sub id %s", arg);
	}
	if (range_of(arg, &(int){ 0 }, &(int){ 0 }) == 1) {
		if (!(pinned = scope_pin(arg, index_tree(), &err)))
			reject(128, NULL, "bad scope: %s", err);
		return xstrfmt("sub scope %s", pinned);
	}
	if (!scope_ok(arg, 1, 1))
		reject(128, NULL, "bad scope: '%s' is not a scope", arg);
	return xstrfmt("sub scope %s", arg);
}

static void subscribe(int argc, const char **argv, FILE *out)
{
	struct string_list lines = STRING_LIST_INIT_DUP;
	int i, fd;

	if (argc < 2)
		reject(129, NULL, "sub names what to subscribe to");
	fd = lock_session(sid);
	session_lines(sid, &lines);
	for (i = 1; i < argc; i++) {
		char *line = sub_line(argv[i]);

		if (unsorted_string_list_has_string(&lines, line))
			continue;
		string_list_append(&lines, line);
		fprintf(out, "%s\n", line);
	}
	session_write(sid, &lines);
	close(fd);
}

int cmd_sub(int argc, const char **argv)
{
	start(START_SESSION);
	subscribe(argc, argv, stdout);
	return 0;
}

int cmd_unsub(int argc, const char **argv)
{
	struct string_list lines = STRING_LIST_INIT_DUP, keep = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	int fd, i;

	session(1);
	fd = lock_session(sid);
	session_lines(sid, &lines);
	for_each_string_list_item(item, &lines) {
		int drop = 0;

		for (i = 1; i < argc; i++)
			drop |= !strcmp(item->string, xstrfmt("sub id %s", argv[i])) ||
				(starts_with(item->string, "sub scope ") &&
				 !strcmp(xstrndup(item->string + 10, strcspn(item->string + 10, " ")), argv[i]));
		if (starts_with(item->string, "sub ") && (argc == 1 || drop))
			printf("un%s\n", item->string);
		else
			string_list_append(&keep, item->string);
	}
	session_write(sid, &keep);
	close(fd);
	return 0;
}

static volatile sig_atomic_t stopping;

static void stop(int sig)
{
	stopping = sig;
}

/* spec 9.5: why the session's agent receives C (OLD at the first parent): relations at MINE and HEAD H */
static void reasons(struct branch *mine, struct tree *h, struct contract *c, struct contract *old,
		    struct string_list *subs, struct strbuf *out)
{
	struct string_list_item *item, *s;
	struct contract *k = contract_get(mine, c->id) ? contract_get(mine, c->id) : c;
	enum verdict v = judge(mine, k, h);

	if (!strcmp(k->creator, agent) && promise(k, v))
		strbuf_addf(out, "reason future %s %s\n", k->kind, k->id);
	for_each_string_list_item(item, &mine->contracts) {
		struct contract *m = item->util;

		if (strcmp(m->creator, agent) || !promise(m, judge(mine, m, h)))
			continue;
		if (holds(m, k->id) || holds(k, m->id) ||
		    (!strcmp(m->kind, "drop") && m->target && !strcmp(m->target, k->id)))
			strbuf_addf(out, "reason future %s %s\n", m->kind, m->id);
	}
	if (!strcmp(k->kind, "demand") && !strcmp(k->creator, agent) && v == V_CURRENT)
		strbuf_addf(out, "reason demand %s\n", k->id);
	for_each_string_list_item(s, subs) {
		const char *rest;

		if (skip_prefix(s->string, "sub id ", &rest) && !strcmp(rest, k->id))
			strbuf_addf(out, "reason sub id %s\n", k->id);
		if (skip_prefix(s->string, "sub scope ", &rest) &&
		    (contract_overlaps(c, rest, h) || (old && contract_overlaps(old, rest, h))))
			strbuf_addf(out, "reason sub scope %s\n", xstrndup(rest, strcspn(rest, " ")));
	}
}

/* spec 9.4: the ACTION lines of C changed from OLD */
static void actions(struct contract *c, struct contract *old, struct strbuf *out)
{
	if (!old) {
		strbuf_addf(out, "contract %s create\n", c->id);
		return;
	}
	if (strcmp(c->condition_id, old->condition_id))
		strbuf_addf(out, "contract %s amend\n", c->id);
	else if (strcmp(c->change_id, old->change_id))
		strbuf_addf(out, "contract %s edit\n", c->id);
	if (strcmp(c->state, old->state))
		strbuf_addf(out, "contract %s %s\n", c->id, c->state);
	if (c->fulfilled.nr > old->fulfilled.nr)
		strbuf_addf(out, "contract %s fulfil\n", c->id);
}

/* the reasons, sorted and each once (v14 9.7) */
static char *sorted_lines(const char *text)
{
	struct string_list lines = STRING_LIST_INIT_DUP, sorted = STRING_LIST_INIT_DUP;
	struct string_list_item *r;
	struct strbuf sb = STRBUF_INIT;

	string_list_split(&lines, text, "\n", -1);
	for_each_string_list_item(r, &lines)
		if (*r->string)
			string_list_insert(&sorted, r->string);
	for_each_string_list_item(r, &sorted)
		strbuf_addf(&sb, "%s\n", r->string);
	return strbuf_detach(&sb, NULL);
}

/*
 * spec 9.4-9.7 for commit E on the branch NAME: its keys (each "KEY PATH
 * BLOB" line), and the event the session receives, if any
 */
static void event(const char *name, const char *e, struct string_list *watched, struct string_list *subs,
		  struct string_list *seen, struct strbuf *keys, struct strbuf *payload)
{
	struct branch *mine = branch_load(name, NULL, NULL), *at = branch_load(name, e, e), *before = NULL;
	struct tree *h = tree_load(head ? head : EMPTY_TREE);
	struct string_list files = STRING_LIST_INIT_DUP;
	struct string_list_item *f;
	struct strbuf why = STRBUF_INIT, acts = STRBUF_INIT, refs = STRBUF_INIT;
	char *parent;
	int rc, in_head = 1;

	parent = line1(git_run(NULL, NULL, &rc, "rev-parse -q --verify %s", xstrfmt("%s^1", e)));
	if (!rc)
		before = branch_load(name, parent, parent);
	string_list_split(&files, git("diff-tree -r --root --name-only --no-commit-id --diff-merges=first-parent %s", e),
			  "\n", -1);
	for_each_string_list_item(f, &files) {
		const char *blob = blob_at(at->tree, f->string), *base = strrchr(f->string, '/');
		struct string_list_item *ful = string_list_lookup(&at->fulfils, f->string);
		struct contract *c = contract_get(at, xstrndup(base ? base + 1 : f->string, 13)), *old;
		char *key;

		if (!blob || (!ful && (!c || strcmp(c->path, f->string))))
			continue;
		key = ful ? xstrfmt("fulfil %s %s", ((struct fulfilment *)ful->util)->contract, blob) :
			    xstrfmt("version %s %s", c->id, blob);
		if (string_list_has_string(seen, key))
			continue;
		strbuf_addf(keys, "%s\n", key);
		/* in-head: every key is in the local branch's tip */
		in_head &= blob_at(mine->tree, f->string) && !strcmp(blob_at(mine->tree, f->string), blob);
		if (ful)
			continue;
		old = before ? contract_get(before, c->id) : NULL;
		reasons(mine, h, c, old, subs, &why);
		actions(c, old, &acts);
		if (!strcmp(c->kind, "drop") && contract_get(at, c->target))
			reasons(mine, h, contract_get(at, c->target), before ? contract_get(before, c->target) : NULL,
				subs, &why);
	}
	if (!why.len)
		return;
	for_each_string_list_item(f, watched) {
		if (is_ancestor(e, f->string))
			strbuf_addf(&refs, "ref %s\n", f->string);
	}
	strbuf_addf(payload, "ait-event 1\ncommit %s\nrecipient %s\n%s%sin-head %s\n%s\n", e, sid, refs.buf,
		    sorted_lines(why.buf), in_head ? "yes" : "no", acts.buf);
	strbuf_addstr(payload, git("show --no-color --no-ext-diff --no-textconv --no-show-signature "
				   "--diff-merges=first-parent --format=fuller --stat --patch %s", e));
	strbuf_addstr(payload, "ait-event-end\n");
}

/*
 * one round of spec 9.6 (v14 9.5) for the session: the receiving state V and
 * the snapshot N first, then the candidates; a key goes out once; the tips
 * and the keys of the events skipped are written at the end, if V held
 */
static void deliver(const char *name)
{
	struct string_list lines = STRING_LIST_INIT_DUP, subs = STRING_LIST_INIT_DUP, seen = STRING_LIST_INIT_DUP,
			   cand = STRING_LIST_INIT_DUP, watched = STRING_LIST_INIT_DUP, tips = STRING_LIST_INIT_DUP;
	struct string_list_item *item, *e;
	struct strbuf in = STRBUF_INIT, seenbuf = STRBUF_INIT, skipped = STRBUF_INIT;
	char *v, *up, oid[48];
	int rc, fd;

	/* HEAD moves while listen runs; the relations are judged at the HEAD of now */
	free((char *)head);
	head = object("HEAD^{commit}", 0, oid, NULL) ? xstrdup(oid) : NULL;
	fd = lock_session(sid);
	v = receiving(sid, &lines);
	tips_now(&tips);
	close(fd);
	for_each_string_list_item(item, &lines) {
		if (starts_with(item->string, "sub "))
			string_list_append(&subs, item->string);
		if (starts_with(item->string, "tip "))
			strbuf_addf(&in, "^%.40s\n", item->string + 4);
	}
	strbuf_read_file(&seenbuf, session_path(sid, ".seen"), 0);
	string_list_split(&seen, seenbuf.buf, "\n", -1);
	string_list_sort(&seen);
	/* 9.2: the local branch and its upstream when that is a remote-tracking ait ref; at N */
	string_list_insert(&watched, xstrfmt("refs/heads/%s", name));
	if ((up = upstream_of(name)))
		string_list_insert(&watched, up);
	for_each_string_list_item(item, &tips)
		if (string_list_has_string(&watched, item->string + 45))
			strbuf_addf(&in, "%.40s\n", item->string + 4);
	string_list_split(&cand, git_run(in.buf, NULL, NULL, "rev-list --reverse --topo-order --ignore-missing --stdin"),
			  "\n", -1);
	for_each_string_list_item(e, &cand) {
		struct strbuf keys = STRBUF_INIT, payload = STRBUF_INIT;
		struct string_list now = STRING_LIST_INIT_DUP, new_keys = STRING_LIST_INIT_DUP;

		if (!*e->string)
			continue;
		event(name, e->string, &watched, &subs, &seen, &keys, &payload);
		/* a key goes out once, in this round too */
		string_list_split(&new_keys, keys.buf, "\n", -1);
		for_each_string_list_item(item, &new_keys)
			if (*item->string)
				string_list_insert(&seen, item->string);
		/* 9.4: the session's own request is skipped, its keys kept like any skipped event's (v14 9.5-3) */
		if (!payload.len ||
		    !strcmp(line1(git("log -1 --format=%(trailers:key=Ait-Session,valueonly) %s", e->string)), sid)) {
			strbuf_add(&skipped, keys.buf, keys.len);
			continue;
		}
		/* v14 9.5-3: V as it was, or the round stops and is judged again */
		fd = lock_session(sid);
		rc = strcmp(receiving(sid, &now), v);
		close(fd);
		if (rc)
			return;
		/* 5.7: a reader gone ends listen as git ends on EPIPE; any other failure is a write error */
		if (fputs(payload.buf, stdout) == EOF || fflush(stdout)) {
			if (errno == EPIPE) {
				signal(SIGPIPE, SIG_DFL);
				raise(SIGPIPE);
			}
			reject(128, NULL, "write error: standard output: %s", strerror(errno));
		}
		append_seen(sid, keys.buf);
	}
	session_tips(sid, v, &tips, skipped.buf);
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
			struct strvec f = STRVEC_INIT;

			strvec_pushl(&f, "fetch", "-q", "--all", NULL);
			if (git_pass(&f))
				warning("fetch failed: %s", git_error ? git_error : "");
			fetched = time(NULL);
		}
		if (size != seen_wake || time(NULL) - polled >= every) {
			struct strbuf hint = STRBUF_INIT;
			const char *msg, *name = branch_find(&msg, &hint);

			/* 6.8: a branch that cannot be chosen skips the round, said once */
			if (!name && !warned)
				warning("%s", msg);
			warned = !name;
			seen_wake = size;
			polled = time(NULL);
			if (name)
				deliver(name);
		}
		usleep(100000);
	}
}

/* v14 7.2.5 */
int cmd_listen(int argc, const char **argv)
{
	struct strvec args = STRVEC_INIT;
	int i, fetch = 0, sfd, fd;
	char *end;

	start(START_SESSION);
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--fetch") && i + 1 < argc) {
			fetch = strtol(argv[++i], &end, 10);
			if (*end || fetch <= 0)
				reject(129, NULL, "--fetch takes seconds: '%s'", argv[i]);
		} else {
			strvec_push(&args, argv[i]);
		}
	}
	if (args.nr) {
		struct strvec sub = STRVEC_INIT;

		strvec_push(&sub, "sub");
		strvec_pushv(&sub, args.v);
		subscribe(sub.nr, sub.v, stderr);
	}
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
