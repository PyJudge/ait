#!/bin/sh
# spec 3: scopes become values at input; read consumers never parse them again.
. "$(dirname "$0")/lib.sh"
setup
run ait future demand src/order.c:10-12,src/order.c:2-4 src/stock.c:10-12,src/stock.c:2-4,src/order.c cond.yaml -m values
D=$(newest D)
run ait fulfill "$D" --force -m verified
check_rc 'prepare consumer, provider and recorded-code ranges' 0

set -- -std=gnu99 -O2 -Wall -Werror -Wdeclaration-after-statement -I"$AIT_DIR" -I"$AIT_DIR/libyaml"
case $(uname -s) in
Linux) set -- "$@" -DHAVE_GETRANDOM ;;
Darwin) set -- "$@" -DHAVE_ARC4RANDOM -DPRECOMPOSE_UNICODE ;;
esac
compiler=${CC:-cc}
"$compiler" "$@" -Dmain=ait_command_main -c "$AIT_DIR/ait.c" -o "$T/ait.o" || exit 1
"$compiler" "$@" -finstrument-functions -c "$AIT_DIR/t/helper-scope-values.c" -o "$T/scopes.o" || exit 1
for source in "$AIT_DIR"/*.c; do
	case $source in */ait.c|*/contract.c) continue ;; esac
	set -- "$@" "$source"
done
set -- "$@" "$T/ait.o" "$T/scopes.o" "$AIT_DIR"/libyaml/*.o
case $(uname -s) in Darwin) set -- "$@" -liconv ;; esac
"$compiler" "$@" -o "$T/scope-values" || exit 1
run "$T/scope-values"
check_rc 'sorting, locating, display, overlap and judgment reuse parsed scope values' 0
check 'the read-consumer probe finishes without another coordinate parse' "has out '^no coordinate reparsing after input$'"
run ait status src/stock.c:2-4
check_rc 'a parsed status range filters the verified contract' 0
check 'status retains the parsed filter and canonical numerical range order' \
	"has out '^id $D$' && has out '^state current$' && has out '^consumer src/order.c:2-4,src/order.c:10-12$' && has out '^provider src/order.c,src/stock.c:2-4,src/stock.c:10-12$'"
run ait sub src/stock.c:2-4
check_rc 'a command range subscription uses the shared input grammar' 0
check 'the subscription stores its typed range base and displays its coordinates' \
	"has out '^sub scope src/stock.c:2-4$' && grep -Eq '^sub scope src/stock.c:2-4 [0-9a-f]{40}$' .git/ait/sessions/$ALICE"
run ait unsub src/stock.c:2-4
check_rc 'unsub still compares the HEAD display' 0
check 'unsub removes the parsed stored subscription' "has out '^unsub scope src/stock.c:2-4$' && ! grep -q '^sub scope ' .git/ait/sessions/$ALICE"
done_testing
