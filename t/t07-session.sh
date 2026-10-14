#!/bin/sh
# v14 7.2 as v1.1 keeps it (4.6): open, list, close, and the session a request needs
. "$(dirname "$0")/lib.sh"
setup

run ait session list
check_rc "list" 0
check "alice is the default" "has out \"session $ALICE\" && has out 'default yes'"
run ait session open --agent "bad name"
check_rc "a bad agent name" 129
run env AIT_SESSION=- ait future demand src/order.c src/stock.c cond.yaml -m x
check_rc "a request needs a session (4.6)" 128
run env AIT_SESSION=Sffffffffffff ait status
check_rc "an unknown session is refused even where none is needed" 128
run ait session close
check_rc "close the default session" 0
run ait status
check_rc "status runs without a session" 0

ait session open --agent dflt >/dev/null 2>&1
B2=$(AIT_SESSION=- ait session open --agent bob2 2>/dev/null)
run env AIT_SESSION=$B2 ait future demand src/order.c src/stock.c cond.yaml -m d
run env AIT_SESSION=$B2 ait status
check "a hint that is a whole command names the session too (v14 5.9a)" "has err \"hint: AIT_SESSION=$B2 ait fulfill \""

done_testing
