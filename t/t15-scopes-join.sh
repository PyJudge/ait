#!/bin/sh
# spec 2.3.2, v14 3.2a: canonical output must not reorder the stored contract's scopes
. "$(dirname "$0")/lib.sh"
T=$(mktemp -d)
trap 'rm -rf "$T"' 0

set -- -std=gnu99 -O2 -Wall -Werror -Wdeclaration-after-statement -I"$AIT_DIR" -I"$AIT_DIR/libyaml"
case $(uname -s) in
Linux) set -- "$@" -DHAVE_GETRANDOM ;;
Darwin) set -- "$@" -DHAVE_ARC4RANDOM -DPRECOMPOSE_UNICODE ;;
esac
compiler=${CC:-cc}
"$compiler" "$@" -Dmain=ait_command_main -c "$AIT_DIR/ait.c" -o "$T/ait.o" || exit 1
for source in "$AIT_DIR"/*.c; do
	case $source in */ait.c) continue ;; esac
	set -- "$@" "$source"
done
set -- "$@" "$AIT_DIR/t/helper-scopes-join.c" "$T/ait.o" "$AIT_DIR"/libyaml/*.o
case $(uname -s) in Darwin) set -- "$@" -liconv ;; esac
"$compiler" "$@" -o "$T/scopes-join" || exit 1

run "$T/scopes-join" borrowed
check_rc "scope serialization preserves borrowed items, order and ownership" 0
run "$T/scopes-join" owned
check_rc "scope serialization preserves owned items, order and ownership" 0
run "$T/scopes-join" empty
check_rc "empty scopes remain empty" 0

done_testing
