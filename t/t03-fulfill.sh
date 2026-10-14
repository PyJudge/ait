#!/bin/sh
# spec 5, 6.7, 7: fulfill, results, the code commit, judgments
. "$(dirname "$0")/lib.sh"
setup

run ait future demand src/order.c src/stock.c:5-9 cond.yaml -m "need"
D=$(newest D)
run ait future offer src/stock.c cond.yaml as o undertake "$D" o -m "take"
O=$(newest O)

run ait fulfill "$O" -m "impl"
check_rc "fulfill without results writes the template (6.7.1)" 1
check "it lists both tasks of the covered set (6.7.2)" "has out \"task $O:1\" && has out \"task $D:1\""
check "the message" "has err 'check results required'"
check "nothing was committed" "test \"\$(git rev-list --count HEAD)\" = 1"

echo guard >> src/stock.c
git add src/stock.c
res=$(pass_all)
run ait fulfill "$O" -r "$res" -m "impl"
check_rc "fulfill with results" 0
check "the code commit is git's (6.7.5-1)" "test \"\$(git log -1 --format=%s)\" = impl"
check "the checkin" "has out '\[FULFILL\] impl'"
check "both are fulfilled" "has out \"fulfilled $O\" && has out \"fulfilled $D\""
check "the fulfilment records the commit (2.5)" "git show ait/master:fulfill/\$(git ls-tree --name-only ait/master fulfill/ | sed -n \"s|fulfill/\\($D-.*\\)|\\1|p\") | grep -q \"commit: \$(git rev-parse HEAD)\""
run ait status
check "the demand is current (5.4)" "grep -A3 \"^id $D\" $T/out | grep -qx 'state current'"

sed -i.bak 's/^s7$/S7/' src/stock.c; git add src/stock.c
run ait status src/stock.c
check "a change in the range: current ?unverified (5.3)" "grep -A3 \"^id $D\" $T/out | grep -qx 'state current ?unverified'"
check "touch (5.5)" "grep -A12 \"^id $D\" $T/out | grep -qx touch"
git reset -q; git checkout -q -- src/stock.c

sed -i.bak 's/^s40$/S40/' src/stock.c; git add src/stock.c
run ait status src/stock.c:5-9
check "a change outside the range keeps the demand current" "grep -A3 \"^id $D\" $T/out | grep -qx 'state current'"
check "the file-scoped offer is ?unverified" "grep -A3 \"^id $O\" $T/out | grep -qx 'state current ?unverified'"
git reset -q; git checkout -q -- src/stock.c

run ait future amend "$D" - src/stock.c:5-12 - -m "wider"
check_rc "amending a demand an offer wholly holds is refused (1.8)" 1
check "the message" "has err \"would leave '$O' fulfilled on top of an unfulfilled '$D'\""
check "the hint reopens the offer in the same request" "has err \"future amend $O\""

run ait fulfill "$D" --force -m "trivial"
check_rc "--force fulfils without results; nothing staged (U33)" 0
check "the warning says what is recorded" "has err 'nothing to commit; recording the code at HEAD'"

run ait future demand src/order.c src/stock.c cond.yaml -m d3
D3=$(newest D)
mkdir -p .git/hooks; printf '#!/bin/sh\nexit 1\n' > .git/hooks/pre-commit; chmod +x .git/hooks/pre-commit
echo more >> src/stock.c; git add src/stock.c
run ait fulfill "$D3" --force -m "blocked"
check_rc "a failed commit fulfils nothing (U32)" 1
check "the message" "has err \"commit failed; '$D3' not fulfilled\""
rm .git/hooks/pre-commit
git reset -q --hard

run ait future drop "$O" -m "retire"
X=$(newest X)
run ait fulfill "$X" -m "withdraw"
check_rc "a drop fulfils without results (6.7.2)" 0
check "the target is withdrawn" "yaml $O | grep -qx 'state: withdrawn'"

run ait future demand src/order.c src/ cond.yaml -m abstract
DA=$(newest D)
run ait fulfill "$DA" --force -m x
check_rc "an abstract provider needs =SCOPES (3.3)" 128
run ait fulfill "$DA=src/stock.c" --force -m x
check_rc "fulfill K=SCOPES narrows it" 0
check "the provider is narrowed" "yaml $DA | grep -q 'provider: \"src/stock.c\"'"

done_testing
