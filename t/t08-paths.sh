#!/bin/sh
# spec 2.4, 4.10, 6.4.1, 6.7.2, 6.7.5: every write path judges through one checkin judgment
. "$(dirname "$0")/lib.sh"
setup
racing_git

echo guard >> src/stock.c; git add src/stock.c
run ait offer src/stock.c cond.yaml --force -m "guard stock"
check_rc "a request that makes and fulfils a contract (6.4.1)" 0
O=$(newest O)
check "it is fulfilled" "has out \"fulfilled $O\""
check "the code was committed once" "test \"\$(git rev-list --count HEAD)\" = 2"

run ait future demand src/order.c src/stock.c:1-3 cond.yaml -m d; D=$(newest D)
run ait future offer src/stock.c cond.yaml -g inv as w undertake "$D" w -m whole; W=$(newest O)
echo more >> src/stock.c; git add src/stock.c
run ait offer src/stock.c cond.yaml -g inv as p undertake -p "$D" p --force -m part
check_rc "a mix is refused at step 0 (6.7.5-0, 4.10)" 1
check "before the code commit" "test \"\$(git rev-list --count HEAD)\" = 2"
check "naming what held the demand at T (4.8)" "has err \"'$D' is already undertaken wholly by '$W'\""
check "the hint keeps the request's options (4.10)" "has err \"hint: ait offer src/stock.c cond.yaml -g inv as p undertake -p $D p --force -m part release $D $W\""
git reset -q --hard

run ait future cancel "$W" -m off
run ait release "$D" "$W" -m rel
check_rc "release does not change an ended offer (2.4)" 1
check "the message" "has err \"not allowed: '$W' has ended\""

run ait fulfill "$D" --force -m now
run ait future drop "$D" -m retire; X=$(newest X)
run ait fulfill "$X" -m withdraw
check "a drop's fulfilment says what it withdrew (6.7.8)" "has out \"^withdrawn $D\$\""
ait contract checkout "$X" >/dev/null
sed -i.bak 's/^state: future/state: cancelled/' ".ait/$X.yaml"
ait contract add ".ait/$X.yaml" >/dev/null
run ait contract commit -m "cancel it"
check_rc "contract commit does not change a fulfilled drop (2.4)" 1
check "the message" "has err \"not allowed: '$X' has ended\""
rm -f "$(git rev-parse --git-dir)/ait/index"

run ait future demand src/order.c src/ cond.yaml -m abstract; DA=$(newest D)
run ait fulfill "$O" "$DA" --force -m guard
check "the hint for an abstract provider is the same command, narrowed (3.3)" "has err \"hint: ait fulfill $O '$DA=<scope>' --force -m guard\""

# the covered set grows while the code is committed: step 0's set holds, so nothing is recorded
run ait future demand src/order.c src/stock.c:5-6 cond.yaml -m d2; D2=$(newest D)
echo "cd $T/r && PATH=\"$(echo "$PATH" | sed "s|$T/bin:||")\" ait future offer src/stock.c cond.yaml as o undertake -p $D2 o -m meanwhile >/dev/null" > "$T/before-update-ref"
run env PATH="$T/bin:$PATH" ait fulfill "$D2" --force -m mine
check_rc "a covered set changed after step 0 is not recorded (6.7.2)" 1
check "the message" "has err \"'$D2' not fulfilled: 'ait/master' moved while running\""

run ait future demand src/order.c src/stock.c:20-21 cond.yaml -m d3; D3=$(newest D)
run ait future offer src/stock.c cond.yaml as n undertake -p "$D3" n amend "$D3" - - - --force -m "new partial offer"
check "a partial offer made in the same request is in the demand's covered set (6.7.2)" "has out \"^fulfilled $(newest O)\$\""

run ait fulfill Dabcdefabcdef=src/stock.c --force -m x
check_rc "a contract fulfill names that does not exist, even with =SCOPES (6.7.1)" 128
check "the message" "has err \"no such contract: 'Dabcdefabcdef'\""
run ait future demand src/order.c src/stock.c cond.yaml -m d4; D4=$(newest D)
run ait future offer src/stock.c cond.yaml -m o4; O4=$(newest O)
cid=$(yaml "$O4" | sed -n 's/^condition-id: //p')
cp cond.yaml c2.yaml
run ait future amend "$D4" - - c2.yaml undertake "$D4" "$O4" --force -m t
check "a trivial amend leaves another clause's condition-id change alone (6.4.1, 2.3.3)" "test \"\$(yaml $O4 | sed -n 's/^condition-id: //p')\" != $cid"

# results for a contract this request amends are made for its condition-id at T (7.2, 7.3)
run ait future demand src/order.c src/ cond.yaml -m abstract2; DB=$(newest D)
run ait fulfill "$DB=src/stock.c" -m x
res=$(pass_all)
run ait fulfill "$DB=src/stock.c" -r "$res" -m x
check_rc "fulfill K=SCOPES -r accepts the results it asked for" 0

# the condition files are read once: a hook that rewrites them during the code commit changes nothing recorded
run ait demand src/order.c src/stock.c:30-31 cond.yaml -m need
res=$(pass_all)
echo more >> src/order.c; git add src/order.c
printf 'body: |\n  NEVER CHECKED\n' > "$T/never.yaml"
echo "cp $T/never.yaml cond.yaml" > "$T/before-commit"
run env PATH="$T/bin:$PATH" ait demand src/order.c src/stock.c:30-31 cond.yaml -r "$res" -m need
check_rc "a demand made and fulfilled with results" 0
check "the contract recorded holds the specs step 0 checked (6.7.5)" "yaml \$(newest D) | grep -q 'stock never goes below zero'"
cp "$T/exec.yaml" cond.yaml

done_testing
