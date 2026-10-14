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
ait future demand src/order.c src/stock.c:1-5 cond.yaml -m "alice asks" >/dev/null
for i in $(seq 1 50); do grep -q ait-event-end "$T/events" && break; sleep 0.1; done
check "the hook wakes listen well before its 30s polling (U51)" "grep -q ait-event-end $T/events"
check "the event names the commit, the recipient and the reason (9.7)" "grep -q '^recipient $BOB' $T/events && grep -q '^reason sub scope src/stock.c' $T/events"
check "the action" "grep -q '^contract D[0-9a-f]* create' $T/events"
ait future offer src/order.c cond.yaml -m "unrelated" >/dev/null
sleep 1.5
check "a change bob is not at risk of is not sent (rule 6)" "test \"\$(grep -c ait-event-end $T/events)\" = 1"
AIT_SESSION=$BOB ait future demand src/order.c src/stock.c:7-9 cond.yaml -m "bob's own" >/dev/null
sleep 1.5
check "bob's own request is not sent to bob (9.4)" "test \"\$(grep -c ait-event-end $T/events)\" = 1"
run env AIT_SESSION=$BOB ait listen
check_rc "a second listener for the session is refused (v14 7.2.5)" 128
kill $L; wait $L 2>/dev/null

run env AIT_SESSION=$BOB ait unsub
check_rc "unsub drops every subscription" 0
check "and prints them" "has out 'unsub scope src/stock.c'"

done_testing
