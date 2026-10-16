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

# YAML's null is no string, and a list is a list (empty values: spec 2.3.2's fulfilled only)
printf 'body: c\nengine: exec\ncommand:\n- sh\n-\n' >nullarg.yaml
run ait future offer src/stock.c nullarg.yaml -m 'null argv item'
check 'a null item in command is a format refusal' "test $rc = 128 && has err \"bad format: 'nullarg.yaml'.* in line 5\$\""
printf 'body: c\nengine: exec\ncommand:\n' >nullcmd.yaml
run ait future offer src/stock.c nullcmd.yaml -m 'null command'
check 'an empty command value is no list' "test $rc = 128 && has err \"bad format: 'nullcmd.yaml'\""
printf 'body: c\nengine: exec\ncommand: [true]\nenvironment: nulls\n' >envnull.yaml
printf 'env:\n  set:\n    A:\n' >specs/environments/nulls.yaml
printf 'env:\n  inherit:\n  - PATH\n  -\n' >specs/environments/nullitem.yaml
git add specs && git commit -qm nulls
run ait offer src/stock.c envnull.yaml -m 'null set value'
check 'a null env.set value is a format refusal' "test $rc = 128 && has err \"bad format: 'specs/environments/nulls.yaml'\""
sed -i.bak 's/nulls/nullitem/' envnull.yaml
run ait offer src/stock.c envnull.yaml -m 'null inherit item'
check 'a null inherit item is a format refusal' "test $rc = 128 && has err \"bad format: 'specs/environments/nullitem.yaml'\""

# a hint names the word of a provider given in an early clause, after later clauses were read (3.3)
run ait offer / cond.yaml future offer src/stock.c cond.yaml future offer src/order.c cond.yaml future offer src/stock.c:1-2 cond.yaml future offer src/order.c:1-2 cond.yaml -m many
check 'the abstract provider of the first clause is refused' "test $rc = 128 && has err 'needs concrete scopes'"
check 'its hint replaces that word' "has err \"^hint: ait offer '<scope>' cond.yaml future offer src/stock.c\""
done_testing
