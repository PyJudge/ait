#!/bin/sh
. "$(dirname "$0")/lib.sh"
setup
state_is() { grep -A3 "^id $1$" "$T/out" | grep -qx "state $2"; }
run ait future demand src/order.c src/stock.c:1-2 cond.yaml -m chain-demand; D=$(newest D)
run ait future demand src/order.c src/stock.c:3-4 cond.yaml -m chain-end; E=$(newest D)
run ait future offer src/order.c:1-2 cond.yaml as o undertake -p "$D" o undertake "$E" o -m chain-offer; O=$(newest O)
ait fulfill "$O" --force -m current >/dev/null 2>&1
ait fulfill "$D" --force -m current >/dev/null 2>&1
run ait future amend "$E" -m reopen-chain
check_rc "the end of a fulfilled dependency chain cannot reopen by itself" 1
check "the hint reopens every valid contract in the reverse dependency chain" "has err 'future amend $O' && has err 'future amend $D'"
sed -n 's/^hint: *//p' "$T/err" > "$T/chain-hint.sh"
run sh "$T/chain-hint.sh"
check_rc "the transitive reopening hint can be replayed successfully" 0
run ait status src/
check "the hint reopens the whole fulfilled chain" "state_is $D future && state_is $O future && state_is $E future"

# General request hints must retain a message read from stdin as well.
ait fulfill "$O" --force -m current >/dev/null 2>&1
ait fulfill "$D" --force -m current >/dev/null 2>&1
printf 'request subject\n\nrepeated\nrepeated\n' > "$T/request-message"
run sh -c 'ait future amend "$1" -F - < "$2"' _ "$E" "$T/request-message"
check_rc "a stdin-message request still reports the pair refusal" 1
sed -n 's/^hint: *//p' "$T/err" > "$T/request-hint.sh"
run sh "$T/request-hint.sh"
check_rc "the general request hint retains its consumed stdin message" 0
printf '[CONTRACT] request subject\n\nrepeated\nrepeated\n\n' > "$T/expected-request"
git show -s --format=%B ait/master | sed '/^Ait-Request:/,$d' > "$T/actual-request"
check "general hint replay preserves blank and repeated message lines" "cmp -s $T/expected-request $T/actual-request"

run ait future demand src/order.c src/stock.c:7-8 cond.yaml -m message-demand; MD=$(newest D)
run ait future offer src/order.c:7-8 cond.yaml as o undertake "$MD" o -m message-offer; MO=$(newest O)
ait fulfill "$MO" --force -m current >/dev/null 2>&1
printf 'subject\n\nbody\nbody\n' > "$T/message"
run sh -c 'ait fulfill "$1=src/stock.c:7-9" --force -F - < "$2"' _ "$MD" "$T/message"
check_rc "a scope change is refused before its leaning offer reopens" 1
sed -n 's/^hint: *//p' "$T/err" > "$T/message-hint.sh"
run sh "$T/message-hint.sh"
check_rc "the scope hint made from a stdin message can be replayed" 0
printf '[CONTRACT] subject\n\nbody\nbody\n\n' > "$T/expected-message"
git show -s --format=%B ait/master | sed '/^Ait-Request:/,$d' > "$T/actual-message"
check "scope hint replay preserves the original message paragraph break" "cmp -s $T/expected-message $T/actual-message"

run ait future demand src/order.c src/stock.c:11-12 cond.yaml -m one-step-demand; HD=$(newest D)
run ait future demand src/order.c src/stock.c:13-14 cond.yaml -m one-step-end; HE=$(newest D)
run ait future offer src/order.c:11-12 cond.yaml as o undertake -p "$HD" o undertake "$HE" o -m one-step-offer; HO=$(newest O)
run ait fulfill "$HD" --force -m one-step
check_rc "one-step coverage refuses a missing whole dependency" 1
sed -n 's/^hint: *//p' "$T/err" > "$T/one-step-hint.sh"
# A doubtful hint may be omitted; every hint that is emitted must be executable.
run sh "$T/one-step-hint.sh"
check_rc "a plain fulfill refusal does not give an invalid appended-clause hint" 0

run ait amend "$HD" - - - --force -m one-step-clause
check_rc "a non-future clause also refuses an uncovered dependency" 1
check "a scheduled fulfilment is not repaired by appending an empty amend" "! has err '^hint:'"

run ait future demand src/order.c src/stock.c:17-18 cond.yaml -m owner-demand
OWNER_D=$(newest D)
BOB=$(AIT_SESSION=- ait session open --agent bob 2>/dev/null)
run env AIT_SESSION="$BOB" ait future offer src/order.c:17-18 cond.yaml as o undertake -p "$OWNER_D" o -m bob-partial
BOB_O=$(newest O)
run ait future offer src/order.c:19-20 cond.yaml as o undertake "$OWNER_D" o -m alice-whole
check_rc "mixing an offer with another agent's opposite undertaking is refused" 1
check "a repair hint does not release another agent's undertaking (rule 7)" "! has err '^hint:'"
run ait release "$OWNER_D" "$BOB_O" -m explicit-release
check_rc "the hint restriction does not restrict an explicit release" 0

# The session prefix belongs to the command, never to its quoted message.
run env AIT_SESSION="$BOB" ait future demand src/order.c src/stock.c:23-24 cond.yaml -m bob-demand
BD=$(newest D)
run env AIT_SESSION="$BOB" ait future offer src/order.c:23-24 cond.yaml as o undertake "$BD" o -m bob-offer
BO=$(newest O)
run env AIT_SESSION="$BOB" ait fulfill "$BO" --force -m current
check_rc "the non-default session can fulfil its dependency pair" 0
cat > "$T/bob-message" <<'MESSAGE'
ait preserve this

say 'ait keep this'
ait repeated
ait repeated
MESSAGE
run env AIT_SESSION="$BOB" ait future amend "$BD" -F "$T/bob-message"
check_rc "a non-default session still refuses a lone dependency reopen" 1
sed -n 's/^hint: *//p' "$T/err" > "$T/bob-hint.sh"
run sh "$T/bob-hint.sh"
check_rc "the non-default session hint replays successfully" 0
{ printf '[CONTRACT] '; cat "$T/bob-message"; printf '\n'; } > "$T/expected-bob"
git show -s --format=%B ait/master | sed '/^Ait-Request:/,$d' > "$T/actual-bob"
check "the session prefix does not enter quoted or multiline message text" "cmp -s $T/expected-bob $T/actual-bob"
check "hint replay retains the original non-default agent" "git show -s --format=%B ait/master | grep -qx 'Ait-Agent: bob'"

# Ref names are valid Git inputs, but still need shell quoting in command hints.
for REF in 'ait/cash$AIT_REVIEW_REF_TEST' "ait/quote'name"; do
	git branch "$REF" ait/master
	run ait checkout "$REF"
	check_rc "checkout accepts the literal contract branch $REF" 0
	sed -n "s/^hint:.*to enter the contract branch: '\(.*\)'$/\1/p" "$T/err" > "$T/switch-hint.sh"
	run sh "$T/switch-hint.sh"
	check_rc "the switch hint replays the literal branch $REF" 0
	check "the switch hint selects exactly $REF" '[ "$(git symbolic-ref --short HEAD)" = "$REF" ]'
	git switch -q master
	ait checkout ait/master >/dev/null 2>&1
done

done_testing
