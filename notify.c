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

static char *shown(struct string_list *l, struct tree *x)
{
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;

	for_each_string_list_item(item, l)
		strbuf_addf(&sb, "%s%s", sb.len ? "," : "", scope_display(item->string, x));
	return strbuf_detach(&sb, NULL);
}

static void block(struct branch *b, struct contract *c, struct tree *x, struct tree *h, const char *me)
{
	enum verdict v = judge(b, c, x);
	struct string_list_item *item;
	size_t i;

	printf("id %s\nkind %s\ngroup %s\nstate %s\ncreator %s\n", c->id, c->kind, *c->group ? c->group : "-",
	       verdict_word(v), c->creator);
	if (!strcmp(c->kind, "demand"))
		printf("consumer %s\n", shown(&c->consumer, x));
	if (!strcmp(c->kind, "drop"))
		printf("target %s\n", c->target);
	else
		printf("provider %s\n", shown(&c->provider, x));
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
			int whole = unsorted_string_list_has_string(&o->undertakes, c->id),
			    part = unsorted_string_list_has_string(&o->partially, c->id);

			if (whole || part)
				printf("undertaken-by %s %s %s %s\n", o->id, verdict_word(judge(b, o, x)), o->creator,
				       part ? "partial" : "whole");
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
	if (me && !strcmp(c->creator, me) && (v == V_FUTURE || v == V_UNVERIFIED) && strcmp(c->kind, "drop"))
		suggest("ait fulfill %s -m <why>", c->id);
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
	name = branch_name();
	names_load(&n);
	printf("On contract branch %s%s\n", name,
	       n.local.nr == 1 && !access(checkout_file(), F_OK) ? "" : n.local.nr == 1 ? " (the only one)" : "");
	refuse_broken(-1, 0);
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
	invariants(b, &bad);
	for_each_string_list_item(item, &bad)
		warning("%s", item->string + strcspn(item->string, " ") + 1);
	for_each_string_list_item(item, &b->contracts) {
		struct contract *c = item->util;
		enum verdict v = judge(b, c, x);
		int pick = 0, j;

		if (argc > 1) {
			struct string_list_item *s;

			for (j = 1; j < argc; j++) {
				char *err, *q = range_of(argv[j], &rc, &rc) == 1 ? scope_pin(argv[j], x, &err) : xstrdup(argv[j]);

				if (!q)
					q = xstrdup(argv[j]);
				for_each_string_list_item(s, &c->consumer)
					pick |= scope_overlap(q, s->string, x);
				for_each_string_list_item(s, &c->provider)
					pick |= scope_overlap(q, s->string, x);
			}
		} else {
			pick = (me && !strcmp(c->creator, me) && (v == V_FUTURE || v == V_UNVERIFIED)) ||
			       (me && !strcmp(c->creator, me) && !strcmp(c->kind, "demand") && v == V_CURRENT) ||
			       v == V_UNVERIFIED || (h && touched(b, c, h, x));
		}
		if (pick)
			string_list_insert(&wanted, c->id)->util = c;
	}
	for_each_string_list_item(item, &wanted)
		block(b, item->util, x, h, me);
	return 0;
}

/* the session file's lines (v14 4.2), read and written whole under its lock */
static char *session_file(const char *s)
{
	return state_path(xstrfmt("sessions/%s", s));
}

static int lock_session(const char *s)
{
	int fd = open(xstrfmt("%s.lock", session_file(s)), O_RDWR | O_CREAT, 0666);

	if (fd < 0 || flock(fd, LOCK_EX))
		reject(128, NULL, "write error: '%s.lock': %s", session_file(s), strerror(errno));
	return fd;
}

static void read_lines(const char *s, struct string_list *lines)
{
	struct strbuf sb = STRBUF_INIT;

	strbuf_read_file(&sb, session_file(s), 0);
	string_list_split(lines, sb.buf, "\n", -1);
}

static void write_lines(const char *s, struct string_list *lines)
{
	struct strbuf sb = STRBUF_INIT;
	struct string_list_item *item;

	for_each_string_list_item(item, lines)
		if (*item->string)
			strbuf_addf(&sb, "%s\n", item->string);
	write_state(session_file(s), sb.buf);
}

/* spec 9.1: the tip lines, a snapshot of every ait ref */
static void tips_now(struct string_list *out)
{
	struct string_list refs = STRING_LIST_INIT_DUP;
	struct string_list_item *r;
	struct names n;

	names_load(&n);
	string_list_split(&refs, git("for-each-ref --format=%(objectname) %(refname) refs/heads/ait/ refs/remotes/"), "\n", -1);
	for_each_string_list_item(r, &refs) {
		const char *name = strchr(r->string, ' ');

		if (name && (starts_with(name + 1, "refs/heads/ait/") || strstr(name + 1, "/ait/")))
			string_list_append(out, xstrfmt("tip %s", r->string));
	}
}

void session_tips(const char *s)
{
	struct string_list lines = STRING_LIST_INIT_DUP, keep = STRING_LIST_INIT_DUP, tips = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	int fd = lock_session(s);

	read_lines(s, &lines);
	for_each_string_list_item(item, &lines)
		if (!starts_with(item->string, "tip "))
			string_list_append(&keep, item->string);
	tips_now(&tips);
	for_each_string_list_item(item, &tips)
		string_list_append(&keep, item->string);
	write_lines(s, &keep);
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
	read_lines(sid, &lines);
	for (i = 1; i < argc; i++) {
		char *line = sub_line(argv[i]);

		if (unsorted_string_list_has_string(&lines, line))
			continue;
		string_list_append(&lines, line);
		fprintf(out, "%s\n", line);
	}
	write_lines(sid, &lines);
	close(fd);
}

int cmd_sub(int argc, const char **argv)
{
	session(1);
	refuse_broken(0, 0);
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
	read_lines(sid, &lines);
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
	write_lines(sid, &keep);
	close(fd);
	return 0;
}

static volatile sig_atomic_t stopping;

static void stop(int sig)
{
	stopping = sig;
}

/* spec 9.5: why the session's agent receives contract C changed in commit E */
static void reasons(struct branch *mine, struct contract *c, struct string_list *subs, struct strbuf *out)
{
	struct string_list_item *item, *s;
	struct contract *k = contract_get(mine, c->id) ? contract_get(mine, c->id) : c;
	struct tree *h = head ? tree_load(head) : tree_load(EMPTY_TREE);
	enum verdict v = judge(mine, k, h);

	if (!strcmp(k->creator, agent) && live(k) && v != V_CURRENT && v != V_FULFILLED)
		strbuf_addf(out, "reason future %s %s\n", k->kind, k->id);
	for_each_string_list_item(item, &mine->contracts) {
		struct contract *m = item->util;
		enum verdict mv;

		if (strcmp(m->creator, agent) || !live(m))
			continue;
		mv = judge(mine, m, h);
		if (mv == V_CURRENT || mv == V_FULFILLED)
			continue;
		if ((!strcmp(m->kind, "offer") && (unsorted_string_list_has_string(&m->undertakes, k->id) ||
						   unsorted_string_list_has_string(&m->partially, k->id))) ||
		    (!strcmp(m->kind, "demand") && !strcmp(k->kind, "offer") &&
		     (unsorted_string_list_has_string(&k->undertakes, m->id) ||
		      unsorted_string_list_has_string(&k->partially, m->id))) ||
		    (!strcmp(m->kind, "drop") && m->target && !strcmp(m->target, k->id)))
			strbuf_addf(out, "reason future %s %s\n", m->kind, m->id);
	}
	if (!strcmp(k->kind, "demand") && !strcmp(k->creator, agent) && v == V_CURRENT)
		strbuf_addf(out, "reason demand %s\n", k->id);
	for_each_string_list_item(s, subs) {
		const char *rest;

		if (skip_prefix(s->string, "sub id ", &rest) && !strcmp(rest, k->id))
			strbuf_addf(out, "reason sub id %s\n", k->id);
		if (skip_prefix(s->string, "sub scope ", &rest)) {
			struct string_list_item *sc;
			int hit = 0;

			for_each_string_list_item(sc, &k->consumer)
				hit |= scope_overlap(rest, sc->string, h);
			for_each_string_list_item(sc, &k->provider)
				hit |= scope_overlap(rest, sc->string, h);
			if (hit)
				strbuf_addf(out, "reason sub scope %s\n", xstrndup(rest, strcspn(rest, " ")));
		}
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

/* one round of spec 9.6 for the session: candidates, events, seen, tips */
static void deliver(const char *name)
{
	struct string_list lines = STRING_LIST_INIT_DUP, subs = STRING_LIST_INIT_DUP, seen = STRING_LIST_INIT_DUP,
			   cand = STRING_LIST_INIT_DUP;
	struct string_list_item *item, *e;
	struct strbuf in = STRBUF_INIT, seenbuf = STRBUF_INIT;
	struct branch *mine = branch_load(name, NULL, NULL);
	char *up;
	int rc, fd;

	fd = lock_session(sid);
	read_lines(sid, &lines);
	close(fd);
	for_each_string_list_item(item, &lines) {
		if (starts_with(item->string, "sub "))
			string_list_append(&subs, item->string);
		if (starts_with(item->string, "tip "))
			strbuf_addf(&in, "^%.40s\n", item->string + 4);
	}
	strbuf_read_file(&seenbuf, xstrfmt("%s.seen", session_file(sid)), 0);
	string_list_split(&seen, seenbuf.buf, "\n", -1);
	string_list_sort(&seen);
	strbuf_addf(&in, "refs/heads/%s\n", name);
	up = line1(git_run(NULL, NULL, &rc, "rev-parse -q --verify --symbolic-full-name %s", xstrfmt("%s@{upstream}", name)));
	if (!rc && starts_with(up, "refs/remotes/"))
		strbuf_addf(&in, "%s\n", up);
	string_list_split(&cand, git_run(in.buf, NULL, NULL, "rev-list --reverse --topo-order --ignore-missing --stdin"),
			  "\n", -1);
	for_each_string_list_item(e, &cand) {
		struct string_list files = STRING_LIST_INIT_DUP;
		struct string_list_item *f;
		struct strbuf payload = STRBUF_INIT, why = STRBUF_INIT, acts = STRBUF_INIT, keys = STRBUF_INIT;
		char *sender, *parent;

		if (!*e->string)
			continue;
		sender = line1(git("log -1 --format=%(trailers:key=Ait-Session,valueonly) %s", e->string));
		if (!strcmp(sender, sid))
			continue;
		parent = git_run(NULL, NULL, &rc, "rev-parse -q --verify %s", xstrfmt("%s^1", e->string));
		string_list_split(&files, git("diff-tree -r --root --name-only --no-commit-id --diff-merges=first-parent %s",
					      e->string), "\n", -1);
		for_each_string_list_item(f, &files) {
			const char *base = strrchr(f->string, '/') ? strrchr(f->string, '/') + 1 : f->string;
			struct branch *at = branch_load(name, e->string, e->string);
			struct branch *before = rc ? NULL : branch_load(name, line1(parent), line1(parent));
			struct contract *c;
			char *key;

			if (!ends_with(base, ".yaml") || !(c = contract_get(at, xstrndup(base, 13))))
				continue;
			key = xstrfmt("version %s %s", c->id, blob_at(at->tree, c->path));
			if (string_list_has_string(&seen, key))
				continue;
			strbuf_addf(&keys, "%s\n", key);
			reasons(mine, c, &subs, &why);
			actions(c, before ? contract_get(before, c->id) : NULL, &acts);
			if (!strcmp(c->kind, "drop") && contract_get(at, c->target))
				reasons(mine, contract_get(at, c->target), &subs, &why);
		}
		if (why.len) {
			struct string_list sorted = STRING_LIST_INIT_DUP;
			struct string_list_item *r;
			struct strbuf rs = STRBUF_INIT;

			{
				struct string_list raw = STRING_LIST_INIT_DUP;

				string_list_split(&raw, why.buf, "\n", -1);
				for_each_string_list_item(r, &raw)
					string_list_insert(&sorted, r->string);
			}
			for_each_string_list_item(r, &sorted)
				if (*r->string)
					strbuf_addf(&rs, "%s\n", r->string);
			git_run(NULL, NULL, &rc, "merge-base --is-ancestor %s %s", e->string, xstrfmt("refs/heads/%s", name));
			strbuf_addf(&payload, "ait-event 1\ncommit %s\nrecipient %s\nref refs/heads/%s\n%sin-head %s\n%s\n",
				    e->string, sid, name, rs.buf, !rc ? "yes" : "no", acts.buf);
			strbuf_addstr(&payload, git("show --no-color --no-ext-diff --no-textconv --no-show-signature "
						    "--diff-merges=first-parent --format=fuller --stat --patch %s", e->string));
			strbuf_addstr(&payload, "ait-event-end\n");
			if (fputs(payload.buf, stdout) == EOF || fflush(stdout))
				return;
		}
		if (keys.len) {
			int sfd = open(xstrfmt("%s.seen", session_file(sid)), O_WRONLY | O_APPEND | O_CREAT, 0666);

			if (sfd >= 0) {
				write_in_full(sfd, keys.buf, keys.len);
				close(sfd);
			}
		}
	}
	session_tips(sid);
}

/* v14 7.2.5 */
int cmd_listen(int argc, const char **argv)
{
	int fd, i, fetch = 0;
	struct strvec args = STRVEC_INIT;
	time_t last = 0;

	session(1);
	for (i = 1; i < argc; i++)
		if (!strcmp(argv[i], "--fetch") && i + 1 < argc)
			fetch = atoi(argv[++i]);
		else
			strvec_push(&args, argv[i]);
	if (args.nr) {
		struct strvec sub = STRVEC_INIT;

		strvec_push(&sub, "sub");
		strvec_pushv(&sub, args.v);
		subscribe(sub.nr, sub.v, stderr);
	}
	fd = open(xstrfmt("%s.listen", session_file(sid)), O_RDWR | O_CREAT, 0666);
	if (fd < 0 || flock(fd, LOCK_EX | LOCK_NB))
		reject(128, NULL, "cannot run now: a listener is already running for session '%s'", sid);
	if (ftruncate(fd, 0) == 0)
		dprintf(fd, "%d\n", (int)getpid());
	signal(SIGINT, stop);
	signal(SIGTERM, stop);
	signal(SIGPIPE, SIG_IGN);
	{
		char *hook = line1(git("rev-parse --path-format=absolute --git-path hooks/reference-transaction"));
		char *wake = state_path("wake");
		struct stat st;
		time_t seen_wake = 0, polled = 0;
		/* the hook wakes it; polling fills in what no hook told (U51) */
		int every = access(hook, X_OK) ? 1 : 30;

		while (!stopping) {
			time_t mt = stat(wake, &st) ? 0 : st.st_mtime;

			if (fetch && time(NULL) - last >= fetch) {
				struct strvec f = STRVEC_INIT;

				strvec_pushl(&f, "fetch", "-q", "--all", NULL);
				if (git_pass(&f))
					warning("fetch failed: %s", git_error ? git_error : "");
				last = time(NULL);
			}
			if (mt != seen_wake || time(NULL) - polled >= every) {
				seen_wake = mt;
				polled = time(NULL);
				deliver(branch_name());
			}
			usleep(100000);
		}
	}
	return 0;
}
