#!/bin/sh
# spec 2.3.2, 7.2, v14 2.5.3: environments are checked values, not source lines.
. "$(dirname "$0")/lib.sh"
setup

# Read emitted environment values through libyaml independently of ait's parser.
cat > "$T/env-value.c" <<'C'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yaml.h>
int main(int argc, char **argv)
{
	yaml_parser_t parser;
	yaml_event_t event;
	struct frame { char path[2048]; char *key; int list, n; } stack[32];
	FILE *file;
	char want[2048], path[2048], *found = NULL;
	size_t len = 0;
	int depth = 0, done = 0, rc = 1;
	if (argc < 3 || !(file = fopen(argv[1], "rb")))
		return 2;
	snprintf(want, sizeof(want), "tasks/0/environment%s%s", *argv[2] ? "/" : "", argv[2]);
	yaml_parser_initialize(&parser);
	yaml_parser_set_input_file(&parser, file);
	while (!done) {
		struct frame *parent = depth ? &stack[depth - 1] : NULL;
		int scalar, map, list;
		if (!yaml_parser_parse(&parser, &event)) {
			fprintf(stderr, "cannot parse request: %s\n", parser.problem);
			return 2;
		}
		scalar = event.type == YAML_SCALAR_EVENT;
		map = event.type == YAML_MAPPING_START_EVENT;
		list = event.type == YAML_SEQUENCE_START_EVENT;
		if (event.type == YAML_MAPPING_END_EVENT || event.type == YAML_SEQUENCE_END_EVENT)
			depth--;
		if (scalar && parent && !parent->list && !parent->key)
			parent->key = strdup((char *)event.data.scalar.value);
		else if (scalar || map || list) {
			if (!parent)
				*path = 0;
			else if (parent->list)
				snprintf(path, sizeof(path), "%s/%d", parent->path, parent->n++);
			else {
				snprintf(path, sizeof(path), "%s%s%s", parent->path, *parent->path ? "/" : "", parent->key);
				free(parent->key);
				parent->key = NULL;
			}
			if (!strcmp(path, want) && (argc > 3 || scalar)) {
				free(found);
				found = strdup(argc > 3 ? map ? "map" : list ? "list" : "scalar" : (char *)event.data.scalar.value);
				len = argc > 3 ? strlen(found) : event.data.scalar.length;
			}
			if (map || list) {
				if (depth == 32)
					return 2;
				snprintf(stack[depth].path, sizeof(stack[depth].path), "%s", path);
				stack[depth].key = NULL;
				stack[depth].list = list;
				stack[depth++].n = 0;
			}
		}
		done = event.type == YAML_STREAM_END_EVENT;
		yaml_event_delete(&event);
	}
	if (found) {
		fwrite(found, 1, len, stdout);
		rc = 0;
	}
	free(found);
	yaml_parser_delete(&parser);
	fclose(file);
	return rc;
}
C
"${CC:-cc}" -std=gnu99 -Wall -Werror -I"$AIT_DIR/libyaml" "$T/env-value.c" "$AIT_DIR"/libyaml/*.o -o "$T/env-value" || exit 1

mkdir -p specs/environments
printf 'body: condition\nengine: exec\ncommand: [true]\nenvironment: checked\n' > env-cond.yaml
run ait future offer src/stock.c env-cond.yaml -m 'environment task'
O=$(newest O)
request=$(git rev-parse --absolute-git-dir)/ait/check-request.yaml
result=$(git rev-parse --absolute-git-dir)/ait/check-result.yaml
environment=specs/environments/checked.yaml
commit_environment() {
	git add "$environment"
	git commit -qm "$1"
}
environment_value() {
	printf '%b' "$2" > "$T/want-value"
	"$T/env-value" "$request" "$1" > "$T/got-value"
	check "environment $1 keeps its decoded value" "cmp -s '$T/want-value' '$T/got-value'"
}

for kind in unknown cwd-list cwd-map timeout-map timeout-text timeout-empty env-scalar env-list env-unknown inherit-scalar inherit-map inherit-nested required-scalar required-nested set-scalar set-list set-nested prepare-scalar prepare-map prepare-flat prepare-empty-argv prepare-nested cleanup-flat cleanup-empty-argv; do
	case $kind in
	unknown) text='unexpected: value' ;;
	cwd-list) text='cwd: [wrong]' ;;
	cwd-map) text='cwd: {wrong: value}' ;;
	timeout-map) text='timeout_ms: {wrong: value}' ;;
	timeout-text) text='timeout_ms: later' ;;
	timeout-empty) text='timeout_ms:' ;;
	env-scalar) text='env: wrong' ;;
	env-list) text='env: []' ;;
	env-unknown) text='env: {unexpected: value}' ;;
	inherit-scalar) text='env: {inherit: PATH}' ;;
	inherit-map) text='env: {inherit: {PATH: value}}' ;;
	inherit-nested) text='env: {inherit: [[PATH]]}' ;;
	required-scalar) text='env: {required: PATH}' ;;
	required-nested) text='env: {required: [{PATH: value}]}' ;;
	set-scalar) text='env: {set: wrong}' ;;
	set-list) text='env: {set: []}' ;;
	set-nested) text='env: {set: {NAME: {wrong: value}}}' ;;
	prepare-scalar) text='prepare: wrong' ;;
	prepare-map) text='prepare: {wrong: value}' ;;
	prepare-flat) text='prepare: [echo, value]' ;;
	prepare-empty-argv) text='prepare: [[]]' ;;
	prepare-nested) text='prepare: [[echo, {wrong: value}]]' ;;
	cleanup-flat) text='cleanup: [rm, value]' ;;
	cleanup-empty-argv) text='cleanup: [[]]' ;;
	esac
	printf '%s\n' "$text" > "$environment"
	commit_environment "$kind environment"
	printf 'request sentinel\n' > "$request"
	printf 'result sentinel\n' > "$result"
	code=$(git rev-parse HEAD); tip=$(git rev-parse ait/master)
	run ait fulfill "$O" -m "$kind environment"
	check_rc "$kind environment is a format error before results are requested" 128
	check "$kind refusal names the source and preserves code, branch, and check files" \
		"has err \"bad format: '$environment':\" && test \"\$(git rev-parse HEAD)\" = '$code' && test \"\$(git rev-parse ait/master)\" = '$tip' && grep -qx 'request sentinel' '$request' && grep -qx 'result sentinel' '$result'"
done

cat > "$environment" <<'YAML'
---
{cwd: "src # :", timeout_ms: 321,
 env: {inherit: [PATH, HOME], set: {"NAME.DOT": "a # : b", "BACK\\SLASH": "line\n\u0085\x7f", EMPTY: ""}, required: [DATABASE_URL]},
 prepare: [[sh, -c, "echo '# : hi'"], [tool, arg]],
 cleanup: [[rm, -rf, "tmp space"]]}
...
YAML
commit_environment 'valid documented environment'
run ait fulfill "$O" -m 'valid environment'
check_rc "a valid flow environment with document markers requests results" 1
check "the valid environment generates a parseable request" "'$T/env-value' '$request' '' --kind > '$T/kind' && grep -qx map '$T/kind'"
environment_value cwd 'src # :'
environment_value timeout_ms 321
environment_value env/inherit/0 PATH
environment_value env/set/NAME.DOT 'a # : b'
environment_value 'env/set/BACK\SLASH' 'line\n\302\205\177'
environment_value env/set/EMPTY ''
environment_value env/required/0 DATABASE_URL
environment_value prepare/0/2 "echo '# : hi'"
environment_value prepare/1/0 tool
environment_value cleanup/0/2 'tmp space'

printf '{}\n' > "$environment"
commit_environment 'empty environment'
run ait fulfill "$O" -m 'empty environment'
check_rc "an empty environment map requests results" 1
check "an empty environment remains a map" "'$T/env-value' '$request' '' --kind > '$T/kind' && grep -qx map '$T/kind'"
printf 'env: {inherit: [], set: {}, required: []}\nprepare: []\ncleanup: []\n' > "$environment"
commit_environment 'explicit empty environment collections'
run ait fulfill "$O" -m 'empty collections'
check_rc "explicit empty environment collections request results" 1
for field in env/inherit env/required prepare cleanup; do
	check "$field remains an empty sequence" "'$T/env-value' '$request' '$field' --kind > '$T/kind' && grep -qx list '$T/kind'"
done
check "env/set remains a map" "'$T/env-value' '$request' env/set --kind > '$T/kind' && grep -qx map '$T/kind'"

done_testing
