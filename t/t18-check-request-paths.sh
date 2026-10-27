#!/bin/sh
# spec 7.2 and v14 8.2: emitted YAML preserves every scope path byte.
. "$(dirname "$0")/lib.sh"
setup
test_have_prereq FUNNYNAMES "emitted YAML preserves the scope paths 'a\"b' and 'a\\b'" || { done_testing; exit $?; }

run "${CC:-cc}" -std=gnu99 -Wall -Werror -I"$AIT_DIR/libyaml" "$AIT_DIR/t/helper-check-request.c" "$AIT_DIR"/libyaml/*.o -o "$T/check-request"
check_rc "the independent check-request YAML reader builds" 0
if test "$rc" != 0
then
	done_testing
	exit $?
fi

for name in 'a"b' 'a\b'
do
	consumer="src/$name-consumer.c"
	provider="src/$name-provider.c"
	printf 'consumer\n' > "$consumer"
	printf 'provider\n' > "$provider"
	git add -- "$consumer" "$provider"
	git commit -qm paths
	run ait future demand "$consumer" "$provider" cond.yaml -m scope
	check_rc "a contract stores the valid scope path '$name'" 0
	D=$(newest D)
	run ait fulfill "$D" -m check
	check_rc "fulfill writes the check request for '$name'" 1
	request=$(sed -n 's/^request //p' "$T/out")
	run "$T/check-request" "$request" "$consumer" "$provider"
	check_rc "the check request decodes both scope paths as '$name'" 0
done

done_testing
