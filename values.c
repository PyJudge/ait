/* Owned domain values follow Git remote.c:copy_ref's copy/free boundary. */
#include "ait.h"

char *xstrdup_null(const char *s)
{
	return s ? xstrdup(s) : NULL;
}

void replace_text(char **field, const char *text)
{
	char *copy = xstrdup_null(text);

	free(*field);
	*field = copy;
}

static void list_copy(struct string_list *out, const struct string_list *from, int lines)
{
	const struct string_list_item *item;

	string_list_clear(out, lines);
	string_list_init_dup(out);
	for_each_string_list_item(item, from) {
		struct string_list_item *copy = string_list_append(out, item->string);

		if (lines && item->util) {
			copy->util = xmalloc(sizeof(int));
			*(int *)copy->util = *(int *)item->util;
		}
	}
}

void spec_clear(struct spec *s)
{
	free(s->body);
	free(s->engine);
	free(s->environment);
	strvec_clear(&s->command);
	string_list_clear(&s->success, 0);
	memset(s, 0, sizeof(*s));
}

void spec_copy(struct spec *out, const struct spec *s)
{
	memset(out, 0, sizeof(*out));
	out->body = xstrdup_null(s->body);
	out->engine = xstrdup_null(s->engine);
	out->environment = xstrdup_null(s->environment);
	strvec_init(&out->command);
	if (s->command.nr)
		strvec_pushv(&out->command, s->command.v);
	list_copy(&out->success, &s->success, 0);
}

void specs_clear(struct contract *c)
{
	size_t i;

	for (i = 0; i < c->nspecs; i++)
		spec_clear(&c->specs[i]);
	free(c->specs);
	c->specs = NULL;
	c->nspecs = 0;
}

void contract_free(struct contract *c)
{
	if (!c)
		return;
	free(c->path);
	free(c->group);
	free(c->condition_id);
	free(c->change_id);
	free(c->kind);
	free(c->creator);
	free(c->state);
	free(c->target);
	scopes_clear(&c->consumer);
	scopes_clear(&c->provider);
	string_list_clear(&c->undertakes, 0);
	string_list_clear(&c->partially, 0);
	string_list_clear(&c->fulfilled, 1);
	specs_clear(c);
	free(c);
}

struct contract *contract_copy(const struct contract *c)
{
	struct contract *copy = xcalloc(1, sizeof(*copy));
	size_t i;

	snprintf(copy->id, sizeof(copy->id), "%s", c->id);
	copy->path = xstrdup(c->path);
	copy->group = xstrdup(c->group);
	copy->condition_id = xstrdup(c->condition_id);
	copy->change_id = xstrdup(c->change_id);
	copy->kind = xstrdup(c->kind);
	copy->creator = xstrdup(c->creator);
	copy->state = xstrdup(c->state);
	copy->target = xstrdup_null(c->target);
	scopes_copy(&copy->consumer, &c->consumer);
	scopes_copy(&copy->provider, &c->provider);
	list_copy(&copy->undertakes, &c->undertakes, 0);
	list_copy(&copy->partially, &c->partially, 0);
	list_copy(&copy->fulfilled, &c->fulfilled, 1);
	copy->specs = xcalloc(c->nspecs, sizeof(*copy->specs));
	copy->nspecs = c->nspecs;
	for (i = 0; i < c->nspecs; i++)
		spec_copy(&copy->specs[i], &c->specs[i]);
	return copy;
}

void contract_fulfilled_copy(struct contract *out, const struct contract *from)
{
	list_copy(&out->fulfilled, &from->fulfilled, 1);
}

static int text_same(const char *a, const char *b)
{
	return a == b || (a && b && !strcmp(a, b));
}

static int list_same(const struct string_list *a, const struct string_list *b, int unordered)
{
	size_t i;

	if (unordered) {
		for (i = 0; i < a->nr; i++)
			if (!unsorted_string_list_has_string((struct string_list *)b, a->items[i].string))
				return 0;
		for (i = 0; i < b->nr; i++)
			if (!unsorted_string_list_has_string((struct string_list *)a, b->items[i].string))
				return 0;
	} else {
		if (a->nr != b->nr)
			return 0;
		for (i = 0; i < a->nr; i++)
			if (strcmp(a->items[i].string, b->items[i].string))
				return 0;
	}
	return 1;
}

int scopes_same(const struct scope_list *a, const struct scope_list *b)
{
	size_t i, j;

	if (a->nr != b->nr)
		return 0;
	for (i = 0; i < a->nr; i++) {
		for (j = 0; j < b->nr; j++)
			if (scope_equal(&a->items[i], &b->items[j], 1))
				break;
		if (j == b->nr)
			return 0;
	}
	return 1;
}

static int specs_same(const struct contract *a, const struct contract *b)
{
	size_t i, j;

	if (a->nspecs != b->nspecs)
		return 0;
	for (i = 0; i < a->nspecs; i++) {
		const struct spec *s = &a->specs[i], *t = &b->specs[i];

		if (!text_same(s->body, t->body) || !text_same(s->engine, t->engine) ||
		    !text_same(s->environment, t->environment) || s->command.nr != t->command.nr ||
		    !list_same(&s->success, &t->success, 0))
			return 0;
		for (j = 0; j < s->command.nr; j++)
			if (strcmp(s->command.v[j], t->command.v[j]))
				return 0;
	}
	return 1;
}

/* Source lines and storage order are not contract semantics. */
int contract_same(const struct contract *a, const struct contract *b, unsigned parts)
{
	if (!text_same(a->kind, b->kind))
		return 0;
	if ((parts & CONTRACT_SPECS) && !specs_same(a, b))
		return 0;
	if ((parts & CONTRACT_REQUIREMENTS) &&
	    (!scopes_same(&a->provider, &b->provider) || !list_same(&a->undertakes, &b->undertakes, 1)))
		return 0;
	if ((parts & CONTRACT_DETAILS) &&
	    (!scopes_same(&a->consumer, &b->consumer) || !text_same(a->creator, b->creator) ||
	     !text_same(a->target, b->target) || !list_same(&a->partially, &b->partially, 1)))
		return 0;
	if ((parts & CONTRACT_IDS) &&
	    (!text_same(a->condition_id, b->condition_id) || !text_same(a->change_id, b->change_id)))
		return 0;
	if ((parts & CONTRACT_STATE) && !text_same(a->state, b->state))
		return 0;
	return !(parts & CONTRACT_FULFILLED) || list_same(&a->fulfilled, &b->fulfilled, 0);
}

void fulfilment_free(struct fulfilment *f)
{
	if (!f)
		return;
	free(f->path);
	free(f->contract);
	free(f->condition_id);
	free(f->by);
	free(f->session);
	free(f->commit);
	free(f->outcome);
	free(f->body);
	code_clear(&f->code);
	free(f);
}
