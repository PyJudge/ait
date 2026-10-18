/* Owned domain values follow Git remote.c:copy_ref's copy/free boundary. */
#include "ait.h"

static char *xstrdup_null(const char *s)
{
	return s ? xstrdup(s) : NULL;
}

void replace_text(char **field, const char *text)
{
	char *copy = xstrdup_null(text);

	free(*field);
	*field = copy;
}

/* a copy has no source lines: only reading a branch reports them (branch_load) */
static void list_copy(struct string_list *out, const struct string_list *from)
{
	const struct string_list_item *item;

	string_list_clear(out, 1);
	string_list_init_dup(out);
	for_each_string_list_item(item, from)
		string_list_append(out, item->string);
}

void spec_clear(struct spec *s)
{
	free(s->body);
	free(s->engine);
	free(s->environment);
	strvec_clear(&s->command);
	string_list_clear(&s->success, 0);
	scopes_clear(&s->fixtures);
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
	list_copy(&out->success, &s->success);
	scopes_copy(&out->fixtures, &s->fixtures);
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

void contract_fulfilled_copy(struct contract *out, const struct contract *from)
{
	list_copy(&out->fulfilled, &from->fulfilled);
}

/* A's items begin B, in order */
int list_is_prefix(const struct string_list *a, const struct string_list *b)
{
	size_t i;

	if (a->nr > b->nr)
		return 0;
	for (i = 0; i < a->nr; i++)
		if (strcmp(a->items[i].string, b->items[i].string))
			return 0;
	return 1;
}

int scopes_same(const struct scope_list *a, const struct scope_list *b)
{
	size_t i;

	if (a->nr != b->nr)
		return 0;
	for (i = 0; i < a->nr; i++)
		if (!scopes_has(b, &a->items[i], 1))
			return 0;
	return 1;
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
