/* The output oracle is libyaml directly, independent of ait's subset reader. */
#include "ait.h"
#include "libyaml/yaml.h"

static int check_body(const char *text, const char *body, size_t length)
{
	yaml_parser_t parser;
	yaml_event_t event;
	int next = 0, matched = 0, done = 0;

	yaml_parser_initialize(&parser);
	yaml_parser_set_input_string(&parser, (const unsigned char *)text, strlen(text));
	while (!done && yaml_parser_parse(&parser, &event)) {
		if (event.type == YAML_SCALAR_EVENT) {
			if (next) {
				matched = event.data.scalar.length == length && !memcmp(event.data.scalar.value, body, length);
				done = 1;
			} else {
				next = !strcmp((char *)event.data.scalar.value, "body");
			}
		}
		done |= event.type == YAML_STREAM_END_EVENT;
		yaml_event_delete(&event);
	}
	yaml_parser_delete(&parser);
	return matched;
}

int main(int argc, const char **argv)
{
	struct strbuf input = STRBUF_INIT;
	int rc = 0;

	if (argc != 3 || strbuf_read_file(&input, argv[2], 0) < 0)
		return 2;
	if (!strcmp(argv[1], "write")) {
		struct contract *c = contract_new("offer", "", "O111111111111");
		char *text;

		c->specs = xcalloc(1, sizeof(*c->specs));
		c->nspecs = 1;
		c->specs[0].body = xstrdup(input.buf);
		text = contract_text(c);
		if (!check_body(text, input.buf, input.len))
			rc = 1;
		fputs(text, stdout);
		free(text);
		contract_free(c);
	} else {
		struct string_list kv = STRING_LIST_INIT_DUP;
		char *err;

		rc = yaml_parse(input.buf, input.len, &kv, &err) != 0;
		if (err)
			puts(err);
		free(err);
		yaml_clear(&kv);
	}
	strbuf_release(&input);
	return rc;
}
