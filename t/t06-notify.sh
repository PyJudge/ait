#!/bin/sh
# spec 6.8, 9: subscriptions, listen, the hook that wakes it, events and their keys
. "$(dirname "$0")/lib.sh"
setup

BOB=$(AIT_SESSION=- ait session open --agent bob 2>/dev/null)
run env AIT_SESSION=$BOB ait sub src/stock.c
check_rc "sub adds a scope (6.8)" 0
check "it prints the line" "has out 'sub scope src/stock.c'"
run env AIT_SESSION=$BOB ait sub src/stock.c
check "the same subscription is not added twice" "! test -s $T/out"

AIT_SESSION=$BOB ait listen > "$T/events" 2>"$T/listen.err" &
L=$!
sleep 1.5
D=$(ait future demand src/order.c src/stock.c:1-5 cond.yaml -m "alice asks" | sed -n 's/^new //p')
for i in $(seq 1 50); do grep -q ait-event-end "$T/events" && break; sleep 0.1; done
check "the hook wakes listen well before its 30s polling (U51)" "grep -q ait-event-end $T/events"
check "the event names the commit, the recipient and the reason (9.7)" "grep -q '^recipient $BOB' $T/events && grep -q '^reason sub scope src/stock.c' $T/events"
check "the action" "grep -q '^contract D[0-9a-f]* create' $T/events"
check "the watched ref that holds it (9.7)" "grep -qx 'ref refs/heads/ait/master' $T/events"
check "in-head: its key is in the local branch's tip (9.7)" "grep -qx 'in-head yes' $T/events"
ait future offer src/order.c cond.yaml -m "unrelated" >/dev/null
sleep 1.5
check "a change bob is not at risk of is not sent (rule 6)" "test \"\$(grep -c ait-event-end $T/events)\" = 1"
BD=$(AIT_SESSION=$BOB ait future demand src/order.c src/stock.c:7-9 cond.yaml -m "bob's own" | sed -n 's/^new //p')
sleep 1.5
check "bob's own request is not sent to bob (9.4)" "test \"\$(grep -c ait-event-end $T/events)\" = 1"
check "but its key is kept, so a copy of it is not sent either (v14 9.5-3)" "grep -q \"^version $BD \" \"\$(git rev-parse --git-common-dir)/ait/sessions/$BOB.seen\""
ait future amend "$D" src/order.c:1-2 src/order.c:3-4 - -m "moved away" >/dev/null
for i in $(seq 1 50); do test "$(grep -c ait-event-end "$T/events")" = 2 && break; sleep 0.1; done
check "a contract moved off a subscribed scope is still sent: the first parent's version overlaps (9.5)" "test \"\$(grep -c ait-event-end $T/events)\" = 2"
ait fulfill "$D" --force -m done >/dev/null 2>&1
sleep 1.5
check "a fulfilment file has its own key (9.4)" "grep -q '^fulfil $D ' \"\$(git rev-parse --git-common-dir)/ait/sessions/$BOB.seen\""
git branch ait/other ait/master
sleep 1.5
check "a branch that cannot be chosen skips the round with one warning (6.8)" "test \"\$(grep -c 'more than one ait branch' $T/listen.err)\" = 1"
check "and listen goes on" "kill -0 $L"
git branch -D -q ait/other
run env AIT_SESSION=$BOB ait listen
check_rc "a second listener for the session is refused (v14 7.2.5)" 128
kill $L; wait $L 2>/dev/null

run env AIT_SESSION=$BOB ait unsub
check_rc "unsub drops every subscription" 0
check "and prints them" "has out 'unsub scope src/stock.c'"

done_testing
