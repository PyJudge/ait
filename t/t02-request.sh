#!/bin/sh
# spec 2.3, 6.4: requests, contract YAML, condition-id and change-id, undertakes and their rules
. "$(dirname "$0")/lib.sh"
setup

run ait future demand src/order.c:10-20 src/stock.c:5-9 cond.yaml -m "need stock"
check_rc "future demand checks in (6.4.1)" 0
D=$(newest D)
check "it says new" "has out \"new $D\""
check "and the summary line (4.4-5)" "has out '^\[ait/master [0-9a-f]*\] \[CONTRACT\] need stock'"
check "the line range carries its base blob (2.3.2)" "yaml $D | grep -q \"provider: \\\"src/stock.c:5-9 $(git rev-parse HEAD:src/stock.c)\\\"\""
check "condition-id and change-id are the request" "test \"\$(yaml $D | sed -n 's/^condition-id: //p')\" = \"\$(yaml $D | sed -n 's/^change-id: //p')\""
check "the spec is inside the contract (U45)" "yaml $D | grep -q 'stock never goes below zero'"
check "fulfilled: is always written (2.3.2)" "yaml $D | grep -qx 'fulfilled:'"
check "the trailers name the request (4.5)" "git log -1 --format=%B ait/master | grep -q '^Ait-Request: R'"
check "the code branch has no contract" "! git ls-files | grep -q yaml"

run ait future offer src/stock.c cond.yaml -g inventory as o undertake "$D" o -m "take it"
check_rc "offer, alias and undertake in one request" 0
O=$(newest O)
check "the group folder (2.1.3)" "git ls-tree -r --name-only ait/master | grep -q \"^inventory/$O.yaml\$\""
check "the new line carries the alias (6.4.4)" "has out \"^new $O o\$\""
check "undertakes lists the demand" "yaml $O | grep -q \"undertakes: \\[$D\\]\""

cid=$(yaml "$D" | sed -n 's/^condition-id: //p')
run ait future amend "$D" src/stock.c:1-3 - - -m "edit consumer"
check_rc "an amend of the consumer only" 0
check "keeps condition-id (2.3.3)" "test \"\$(yaml $D | sed -n 's/^condition-id: //p')\" = \"$cid\""
check "moves change-id" "test \"\$(yaml $D | sed -n 's/^change-id: //p')\" != \"$cid\""

run ait future amend "$D" - - "$T/prompt.yaml" -m x
check_rc "a condition file outside the repository is refused" 128

cp "$T/prompt.yaml" cond2.yaml
run ait future amend "$D" - - cond2.yaml --force -m "typo"
check_rc "a trivial amend with --force (6.4.1)" 0
check "keeps condition-id" "test \"\$(yaml $D | sed -n 's/^condition-id: //p')\" = \"$cid\""
run ait future amend "$D" - src/stock.c - --force -m "move"
check_rc "--force cannot change the provider" 1
check "the message" "has err \"cannot change the provider of '$D' with --force\""

run ait future demand src/order.c src/stock.c:30-40 cond.yaml -m "second"
D2=$(newest D)
run ait undertake -p "$D2" "$O" -m "share it"
check_rc "a partial undertake (U40)" 0
check "it is a contract row of the offer" "yaml $O | grep -q \"undertakes-partially: \\[$D2\\]\""
run ait future offer src/order.c cond.yaml as o2 undertake "$D2" o2 -m "whole on a shared one"
check_rc "a whole undertake of a partially undertaken demand is refused (4.10)" 1
check "the message" "has err \"'$D2' is already undertaken partially by '$O'\""
check "the hint releases the partial one in the same request" "has err \"release $D2 $O\""
check "and only that one, not the offer the request makes (4.10)" "test \"\$(grep -o ' release ' $T/err | wc -l | tr -d ' ')\" = 1"
run ait future offer src/order.c cond.yaml as o2 release "$D2" "$O" undertake "$D2" o2 -m "swap"
check_rc "releasing in the same request allows it" 0

run ait future cancel "$D2" -m "not needed"
check_rc "cancel" 0
check "the state" "yaml $D2 | grep -qx 'state: cancelled'"
run ait future amend "$D2" - src/order.c - -m x
check_rc "an ended contract does not change" 1

run ait future drop "$D2" -m x
check_rc "a drop of a contract that is not current is refused (2.4)" 1
check "the message (4.8)" "has err \"not allowed: 'X[0-9a-f]*' targets '$D2', which is not current\""

run ait future demand src/order.c new:src/later.c cond.yaml -m x
check_rc "new:PATH for a provider not written yet" 0
run ait future demand src/order.c src/none.c cond.yaml -m x
check_rc "a missing file is a bad scope" 128

run ait future demand src/order.c src/stock.c cond.yaml
check_rc "a request needs a message (4.5)" 129
echo why > "$T/msg"
run ait future demand src/order.c src/stock.c cond.yaml -m x -F "$T/msg"
check_rc "-m and -F together are a usage error (4.5, v14 5.3)" 129
check "git's words" "has err \"options '-m' and '-F' cannot be used together\""
printf 'body: |\n  x\ninputs: [a]\n' > cond5.yaml
run ait future demand src/order.c src/stock.c cond5.yaml -m x
check_rc "a condition file with an unknown key is refused (2.3.1)" 128
check "naming the key and its line" "has err \"bad format: 'cond5.yaml': unknown key 'inputs' in line 3\""
run ait future demand src/order.c src/stock.c cond.yaml -p future offer src/stock.c cond.yaml -m x
check_rc "-p anywhere but after undertake is a usage error, not a skipped word (6.4)" 129

echo x > amend; git add amend; git commit -qm amend
run ait future demand src/order.c amend cond.yaml -m "a file named amend"
check_rc "a word that is a scope is not read as a clause (6.4)" 0
check "nothing is fulfilled, no code step runs" "! has out '^fulfilled' && ! has err 'nothing to commit'"

done_testing
