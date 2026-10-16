/* An oracle independent of ait: libyaml reads the first "body" scalar of a YAML file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libyaml/yaml.h"

static size_t slurp(const char *path, unsigned char *buf, size_t size)
{
	FILE *f = fopen(path, "rb");
	size_t n;

	if (!f)
		exit(2);
	n = fread(buf, 1, size, f);
	fclose(f);
	return n;
}

int main(int argc, char **argv)
{
	static unsigned char text[1 << 16], want[1 << 16];
	size_t len, want_len;
	yaml_parser_t parser;
	yaml_event_t event;
	int next = 0, matched = 0, done = 0;

	if (argc != 3)
		return 2;
	len = slurp(argv[1], text, sizeof(text));
	want_len = slurp(argv[2], want, sizeof(want));
	yaml_parser_initialize(&parser);
	yaml_parser_set_input_string(&parser, text, len);
	while (!done && yaml_parser_parse(&parser, &event)) {
		if (event.type == YAML_SCALAR_EVENT) {
			if (next) {
				matched = event.data.scalar.length == want_len &&
					  !memcmp(event.data.scalar.value, want, want_len);
				done = 1;
			}
			next = !strcmp((char *)event.data.scalar.value, "body");
		}
		done |= event.type == YAML_STREAM_END_EVENT;
		yaml_event_delete(&event);
	}
	yaml_parser_delete(&parser);
	return !matched;
}
