#!/bin/sh
# spec 2.3.1, v14 2.5.1/2.5.6/8.2: decoded scalar values keep their bytes and types.
. "$(dirname "$0")/lib.sh"
setup
ulimit -c 0

# Decode the first spec body independently through libyaml, without ait's reader or writer.
cat > "$T/body.c" <<'C'
#include <stdio.h>
#include <string.h>
#include <yaml.h>
int main(int argc, char **argv)
{
	yaml_parser_t parser;
	yaml_event_t event;
	FILE *file;
	int body = 0, found = 0, done = 0;
	if (argc != 2 || !(file = fopen(argv[1], "rb")))
		return 2;
	yaml_parser_initialize(&parser);
	yaml_parser_set_input_file(&parser, file);
	while (!done && yaml_parser_parse(&parser, &event)) {
		if (event.type == YAML_SCALAR_EVENT) {
			if (body) {
				fwrite(event.data.scalar.value, 1, event.data.scalar.length, stdout);
				found = done = 1;
			} else
				body = !strcmp((char *)event.data.scalar.value, "body");
		}
		done |= event.type == YAML_STREAM_END_EVENT;
		yaml_event_delete(&event);
	}
	yaml_parser_delete(&parser);
	fclose(file);
	return found ? 0 : 1;
}
C
"${CC:-cc}" -std=gnu99 -Wall -Werror -I"$AIT_DIR/libyaml" "$T/body.c" "$AIT_DIR"/libyaml/*.o -o "$T/body" || exit 1

for value in del c1 nel ls ps nonchar nonchar-last cr bom astral; do
	case $value in
	del) escape='\x7f'; bytes='\177' ;;
	c1) escape='\u0080'; bytes='\302\200' ;;
	nel) escape='\u0085'; bytes='\302\205' ;;
	ls) escape='\u2028'; bytes='\342\200\250' ;;
	ps) escape='\u2029'; bytes='\342\200\251' ;;
	nonchar) escape='\ufffe'; bytes='\357\277\276' ;;
	nonchar-last) escape='\uffff'; bytes='\357\277\277' ;;
	cr) escape='\r'; bytes='\r' ;;
	bom) escape='\ufeff'; bytes='\357\273\277' ;;
	astral) escape='\U0001F600'; bytes='\360\237\230\200' ;;
	esac
	printf 'body: "%s"\n' "$escape" > cond.yaml
	printf '%b' "$bytes" > "$T/want-body"
	run ait future offer src/stock.c cond.yaml -m "$value body"
	check_rc "escaped $value body can be stored as valid YAML" 0
	O=$(newest O)
	if [ -n "$O" ]; then
		yaml "$O" > "$T/contract.yaml"
		"$T/body" "$T/contract.yaml" > "$T/got-body"
		check "escaped $value body keeps its exact decoded bytes" "cmp -s '$T/want-body' '$T/got-body'"
	fi
done

for field in engine environment; do
	for value in '{}' '[]' '{unexpected: value}'; do
		printf 'body: condition\n' > cond.yaml
		if [ "$field" = environment ]; then
			printf 'engine: exec\ncommand: [true]\n' >> cond.yaml
		fi
		printf '%s: %s\n' "$field" "$value" >> cond.yaml
		tip=$(git rev-parse ait/master)
		run ait future offer src/stock.c cond.yaml -m 'invalid scalar field'
		check_rc "$field rejects the collection $value" 128
		check "$field collection refusal preserves the branch" "has err 'bad format:' && test \"\$(git rev-parse ait/master)\" = '$tip'"
	done
done

for field in command success; do
	printf 'body: condition\n%s: []\n' "$field" > cond.yaml
	tip=$(git rev-parse ait/master)
	run ait future offer src/stock.c cond.yaml -m 'prompt has an exec key'
	check_rc "a prompt spec rejects an explicitly empty $field" 128
	check "the prompt $field refusal preserves the branch" "has err 'bad format:' && test \"\$(git rev-parse ait/master)\" = '$tip'"
done
printf 'body: condition\nengine:\n' > cond.yaml
run ait future offer src/stock.c cond.yaml -m 'plain empty engine'
check_rc "a plain empty engine remains a prompt spec" 0

stage=$(git rev-parse --git-dir)/ait/index
for field in condition-id change-id creator; do
	for value in '{}' '[]'; do
		run ait contract new offer
		P=$(sed -n 's/^path //p' "$T/out")
		sed -i.bak "s#provider: \"<scope>\"#provider: \"src/stock.c\"#; s/^$field:.*/$field: $value/" "$P"
		cp "$P" "$T/draft"
		run ait contract add "$P"
		check_rc "a draft rejects $field as the collection $value" 128
		check "the $field collection refusal keeps the draft and stage untouched" "has err 'bad format:' && cmp -s '$P' '$T/draft' && test ! -e '$stage'"
		rm -f "$stage" "$P" "$P.bak"
	done
done

done_testing
