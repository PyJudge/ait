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
check "the count of tasks" "has err 'check results required: 2 task(s)'"
check "nothing was committed" "test \"\$(git rev-list --count HEAD)\" = 1"

echo guard >> src/stock.c
git add src/stock.c
res=$(pass_all)
cp "$res" "$T/extra.yaml"; sed -i.bak "s/^condition-ids:$/condition-ids:\n  Dabcdefabcdef: \"R000000000000\"/" "$T/extra.yaml"
run ait fulfill "$O" -r "$T/extra.yaml" -m "impl"
check_rc "a condition-id more than the covered set asks for results again (7.3)" 1
check "with a new template" "has out 'result '"
check "a filled template with the same condition-ids and tasks is kept (7.4)" "grep -q 'outcome: pass' $res"
run ait fulfill "$O" -r "$res" -m "impl"
check_rc "fulfill with results" 0
check "the code commit is git's (6.7.5-1)" "test \"\$(git log -1 --format=%s)\" = impl"
check "git commit's summary line comes first (6.7.8)" "head -1 $T/out | grep -q '^\[master [0-9a-f]*\] impl'"
check "the checkin" "has out '\[FULFILL\] impl'"
check "both are fulfilled" "has out \"fulfilled $O\" && has out \"fulfilled $D\""
check "the fulfilment records the commit (2.5)" "git show ait/master:fulfill/\$(git ls-tree --name-only ait/master fulfill/ | sed -n \"s|fulfill/\\($D-.*\\)|\\1|p\") | grep -q \"commit: \$(git rev-parse HEAD)\""
run ait status
check "the demand is current (5.4)" "grep -A3 \"^id $D\" $T/out | grep -qx 'state current'"

sed -i.bak 's/^s7$/S7/' src/stock.c; git add src/stock.c
run ait status src/stock.c
check "a change in the range: current ?unverified (5.3)" "grep -A3 \"^id $D\" $T/out | grep -qx 'state current ?unverified'"
check "the changed range is shown ?changed (3.4)" "grep -A6 \"^id $D\" $T/out | grep -q '^provider src/stock.c:5-9 ?changed'"
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
run ait status
check "a demand no offer holds hints an offer that undertakes it (6.3)" "has err \"hint: ait future offer 'src/stock.c' <files> as o undertake $D3 o -m <why>\""
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

printf 'body: |\n  x\nengine: exec\ncommand: [sh, -c, "exit 1"]\nsuccess: [0, 1]\n' > cond3.yaml
run ait future demand src/order.c src/stock.c cond3.yaml -m d4
run ait fulfill "$(newest D)" -m x
check "success codes are written inside the list (7.2)" "grep -qx '  success: \[0, 1\]' .git/ait/check-request.yaml"

printf 'body: |\n  y\nengine: exec\ncommand: [make]\nenvironment: node\n' > cond4.yaml
run ait future demand src/order.c src/stock.c cond4.yaml -m d5; D5=$(newest D)
run ait fulfill "$D5" -m x
check_rc "an environment missing at HEAD is refused (7.2)" 128
check "the message" "has err \"could not read 'specs/environments/node.yaml' at HEAD\""
mkdir -p specs/environments; printf 'cwd: src\nenv:\n  A: "1"\n' > specs/environments/node.yaml
git add specs; git commit -qm env
run ait fulfill "$D5" -m x
check "the environment's lines are under environment: (7.2)" "grep -A3 '^  environment:\$' .git/ait/check-request.yaml | grep -qx '      A: \"1\"'"

git checkout -q -b side; sed -i.bak 's/^o3$/side/' src/order.c; git commit -qam side
git checkout -q master; sed -i.bak 's/^o3$/main/' src/order.c; git commit -qam main
git merge -q side >/dev/null 2>&1
run ait status src/order.c
check_rc "status reads an unmerged index (5.1)" 0
check "as HEAD has the path" "has err \"warning: 'src/order.c' is unmerged; it is read as HEAD has it\""
check "once per path" "test \"\$(grep -c 'is unmerged' $T/err)\" = 1"
run ait fulfill "$D5" --force -m x
check_rc "fulfill refuses an unmerged index (4.3-5)" 1
check "with git's words" "has err 'fulfilling is not possible because you have unmerged files'"
git merge --abort

sub=$(git rev-parse HEAD)
git update-index --add --cacheinfo 160000,$sub,lib; git commit -qm gitlink
run ait future demand src/order.c lib cond.yaml -m g; DG=$(newest D)
check_rc "a gitlink is a file scope (2.5)" 0
run ait fulfill "$DG" --force -m g
check_rc "and is fulfilled" 0
check "its code entry is '160000 commit OID'" "git show ait/master:fulfill/\$(git ls-tree --name-only ait/master fulfill/ | sed -n \"s|fulfill/\\($DG-.*\\)|\\1|p\") | grep -q '\"lib\": \"160000 commit $sub\"'"

done_testing
