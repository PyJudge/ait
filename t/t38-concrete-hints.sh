#!/bin/sh
# spec 3.3 and 4.9: narrow the actual provider word, or add a covered ID to fulfill.
. "$(dirname "$0")/lib.sh"
setup
retry_concrete() {
	hint=$(sed -n 's/^hint: //p' "$T/err" | head -1)
	concrete=$(printf '%s\n' "$hint" | sed 's|<scope>|src/stock.c|g')
	run sh -c "$concrete"
}
run ait offer / cond.yaml --force -m 'abstract input'
check_rc 'a new inline contract needs a concrete provider' 128
check 'the helper hint changes the provider argument' "has err 'hint:.*<scope>'"
retry_concrete
check_rc 'narrowing the inline helper hint fulfils its new contract' 0

run ait future offer / cond.yaml as untouched offer / cond.yaml as current --force -m 'literal /'
check_rc 'an inline contract after another abstract clause still needs narrowing' 128
hint=$(sed -n 's/^hint: //p' "$T/err" | head -1)
check 'only the fulfilling clause is narrowed, retaining the other provider and message' \
	"printf '%s\n' \"\$hint\" | grep -Fq \"future offer / cond.yaml as untouched offer '<scope>' cond.yaml as current --force -m 'literal /'\""
retry_concrete
check_rc 'the hint preserves the multi-clause request' 0
FUTURE=$(sed -n 's/^new \(O[0-9a-f]*\) untouched$/\1/p' "$T/out")
check 'the untouched future contract retains its abstract provider' "yaml $FUTURE | grep -qx 'provider: \"/\"'"

run ait future demand src/order.c / cond.yaml -m dependency
D=$(newest D)
run ait future offer src/stock.c cond.yaml as o undertake "$D" o -m holder
O=$(newest O)
run ait fulfill "$O" --force -m 'implicit dependency'
check_rc 'an implicit covered dependency must be narrowed too' 128
check 'the fulfill hint includes the covered contract' "has err \"$D=<scope>\""
retry_concrete
check_rc 'the covered-contract hint can actually be fulfilled' 0

run ait future demand src/order.c / cond.yaml -m dependency2
D=$(newest D)
run ait future offer src/stock.c cond.yaml as o undertake "$D" o amend o - - --force -m 'helper dependency'
check_rc 'a helper can also cover an existing abstract dependency' 128
check 'the helper hint includes an amendment for its dependency' "has err \"amend $D\" && has err '<scope>'"
retry_concrete
check_rc 'the helper hint narrows its dependency and completes the original request' 0

run ait future offer / cond.yaml -m explicit
O=$(newest O)
BOB=$(AIT_SESSION=- ait session open --agent bob)
run env AIT_SESSION="$BOB" ait fulfill "$O" --force -m 'selected session'
check_rc 'explicit fulfill still refuses an abstract provider' 128
check 'the hint preserves the selected session and the explicit target' "has err \"hint: AIT_SESSION=$BOB ait fulfill '$O=<scope>'\""
retry_concrete
check_rc 'the explicit narrowing hint still executes in its selected session' 0
done_testing
