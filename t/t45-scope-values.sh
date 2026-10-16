#!/bin/sh
# spec 3: scopes are read once at input; status, sub and unsub show them in v14 3.2a order.
. "$(dirname "$0")/lib.sh"
setup
run ait future demand src/order.c:10-12,src/order.c:2-4 src/stock.c:10-12,src/stock.c:2-4,src/order.c cond.yaml -m values
D=$(newest D)
run ait fulfill "$D" --force -m verified
check_rc 'prepare consumer, provider and recorded-code ranges' 0

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
