#include <stdio.h>
#include <string.h>
#include "yaml.h"

int main(int argc, const char **argv)
{
	yaml_parser_t parser;
	yaml_event_t event;
	FILE *input;
	int pending = 0, found = 0, end = 0;

	if (argc != 4 || !(input = fopen(argv[1], "r")))
		return 2;
	yaml_parser_initialize(&parser);
	yaml_parser_set_input_file(&parser, input);
	while (!end) {
		if (!yaml_parser_parse(&parser, &event)) {
			fprintf(stderr, "cannot parse check request: %s\n", parser.problem);
			return 1;
		}
		if (event.type == YAML_SCALAR_EVENT) {
			const char *text = (char *)event.data.scalar.value;

			if (pending) {
				if (strcmp(text, argv[pending + 1]))
					return 1;
				found |= pending;
				pending = 0;
			} else if (!strcmp(text, "consumer")) {
				pending = 1;
			} else if (!strcmp(text, "provider")) {
				pending = 2;
			}
		}
		end = event.type == YAML_STREAM_END_EVENT;
		yaml_event_delete(&event);
	}
	yaml_parser_delete(&parser);
	fclose(input);
	return found != 3;
}
