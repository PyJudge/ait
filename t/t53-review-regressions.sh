#!/bin/sh
# Review findings: a folded trailer keeps listen running (9.4); environments keep their schema's types (7.2).
. "$(dirname "$0")/lib.sh"
setup
BOB=$(ait session open --agent bob)
run ait future offer src/stock.c cond.yaml -m seed
O=$(newest O)
ait listen >"$T/events" 2>"$T/listen.err" &
L=$!
sleep 1.5
tree=$(git rev-parse 'ait/master^{tree}')
folded=$(printf 'folded\n\nAit-Session: %s\n continued\n' "$ALICE" | git commit-tree "$tree" -p ait/master)
git update-ref refs/heads/ait/master "$folded"
sleep 1.5
run env AIT_SESSION="$BOB" ait future amend "$O" src/order.c - -m "bob after the folded trailer"
check_rc 'bob changes the offer after the folded trailer' 0
for i in $(seq 1 50); do grep -q ait-event-end "$T/events" 2>/dev/null && break; sleep 0.1; done
check 'listen outlives a folded trailer' "kill -0 $L"
check "and delivers the next change" "grep -q 'ait-event-end' '$T/events'"
kill $L 2>/dev/null
wait $L 2>/dev/null

mkdir -p specs/environments
printf 'cwd: src\ntimeout_ms: "321"\nenv:\n  set:\n    COUNT: 123\n  inherit: [PATH]\n' >specs/environments/typed.yaml
git add specs && git commit -qm typed
printf 'body: condition\nengine: exec\ncommand: [true]\nenvironment: typed\n' >env.yaml
run ait offer src/stock.c env.yaml -m typed
req=$(sed -n 's/^request //p' "$T/out")
check 'timeout_ms is written as an integer' "grep -qx '    timeout_ms: 321' '$req'"
check 'an environment value is written as a string' "grep -qx '        COUNT: \"123\"' '$req'"
check 'cwd is written as a string' "grep -qx '    cwd: \"src\"' '$req'"
done_testing
