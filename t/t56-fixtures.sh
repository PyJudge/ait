#!/bin/sh
# spec 2.3.2, 2.5, 5.3-5.5, 6.3-5, 6.7.4, 7.2, 9.5 (U60, U66): a spec's fixtures are witnessed with the provider
. "$(dirname "$0")/lib.sh"
setup

state_of() { grep -A4 "^id $1$" "$T/out" | sed -n 's/^state //p'; }
fulfil_file() { git show "ait/master:$(git ls-tree -r --name-only ait/master | grep "fulfill/$1-" | tail -1)"; }

mkdir -p tests specs/environments
printf 'node --test\n' > tests/stock.test.js
printf 'item,count\n' > tests/stock.csv
printf 'cwd: .\n' > specs/environments/node.yaml
git add tests specs; git commit -qm fixtures
printf 'body: |\n  stock never goes below zero\nengine: exec\ncommand: [node, --test, tests/stock.test.js]\nenvironment: node\nfixtures: [tests/stock.test.js, tests/stock.csv]\n' > fix.yaml
printf 'body: |\n  the report names each item once\nfixtures: [tests/stock.csv]\n' > fix-prompt.yaml

# 2.3.2: fixtures are a spec's key, for prompt specs too, written in the order of a scope list (v14 3.2a)
run ait future offer src/stock.c fix.yaml,fix-prompt.yaml -m 'offer with fixtures'
check_rc "a spec with fixtures is taken" 0
O=$(newest O)
check "the contract keeps each spec's fixtures" \
	"yaml $O | grep -qxF '  fixtures: [\"tests/stock.csv\", \"tests/stock.test.js\"]' && yaml $O | grep -cxF '  fixtures: [\"tests/stock.csv\"]' | grep -qx 1"

# 7.2: the check request names each task's fixtures
run ait fulfill "$O" -m impl
check "the check request names the fixtures of each task" \
	"grep -A12 'task: \"$O:1\"' .git/ait/check-request.yaml | grep -qF 'fixtures: [\"tests/stock.csv\", \"tests/stock.test.js\"]'"

# 2.5: the fulfilment records the provider, the fixtures and the environment file, each once
res=$(pass_all)
run ait fulfill "$O" -r "$res" -m impl
check_rc "fulfill with fixtures" 0
check "the fulfilment records every fixture and the environment file" \
	"fulfil_file $O | grep -q '^  \"src/stock.c\": ' && fulfil_file $O | grep -q '^  \"tests/stock.test.js\": ' && fulfil_file $O | grep -q '^  \"tests/stock.csv\": ' && fulfil_file $O | grep -q '^  \"specs/environments/node.yaml\": '"
check "a fixture two specs share is recorded once" "test \"\$(fulfil_file $O | grep -c '^  \"tests/stock.csv\": ')\" = 1"
run ait status /
check "fulfilled, the offer is current" "test \"\$(state_of $O)\" = current"

# 5.4, 3.4: a fixture or the environment changed after the fulfilment leaves the check unverified
for f in tests/stock.test.js specs/environments/node.yaml; do
	echo changed >> "$f"; git add "$f"
	run ait status /
	check "'$f' changed: current ?unverified" "test \"\$(state_of $O)\" = 'current ?unverified'"
	check "'$f' changed: its fixtures line says ?changed" "grep -A8 '^id $O$' '$T/out' | grep -q '^fixtures .*$f ?changed'"
	check "'$f' changed: touch (5.5)" "grep -A12 '^id $O$' '$T/out' | grep -qx touch"
	git reset -q; git checkout -q -- "$f"
done
run ait status /
check "restored, current again" "test \"\$(state_of $O)\" = current"

# U26: the fulfilment's commit rewritten away, the recorded blobs still judge
git commit -q --amend -m 'impl, reworded'
run ait status /
check "a reworded fulfilment commit keeps the offer current" "test \"\$(state_of $O)\" = current"

# 2.3.3: the fixtures are part of the condition
cid=$(yaml "$O" | sed -n 's/^condition-id: //p')
printf 'body: |\n  stock never goes below zero\nengine: exec\ncommand: [node, --test, tests/stock.test.js]\nenvironment: node\nfixtures: [tests/stock.csv, tests/stock.test.js]\n' > fix.yaml
run ait future amend "$O" - fix.yaml,fix-prompt.yaml -m 'the same fixtures, reordered'
check "the same fixtures in another order are the same condition" "test \"\$(yaml $O | sed -n 's/^condition-id: //p')\" = $cid"
printf 'body: |\n  stock never goes below zero\nengine: exec\ncommand: [node, --test, tests/stock.test.js]\nfixtures: [tests/stock.test.js]\n' > fix.yaml
run ait future amend "$O" - fix.yaml -m 'fewer fixtures'
check_rc "amend the fixtures" 0
check "a new fixtures list is a new condition-id" "test \"\$(yaml $O | sed -n 's/^condition-id: //p')\" != $cid"

# 3.2: a fixture is pinned as a provider scope is: there when the contract is made
printf 'body: |\n  stock never goes below zero\nfixtures: [tests/absent.csv]\n' > fix-absent.yaml
run ait future offer src/stock.c fix-absent.yaml -m 'absent fixture'
check_rc "a fixture not in the code is refused" 128
check "as a scope that is not a file" "has err \"^fatal: bad scope: 'tests/absent.csv' is not a file$\""

# 6.7.4: a fixture gone from the code by the fulfilment is refused as a provider scope is
printf 'body: |\n  stock never goes below zero\nfixtures: [tests/stock.csv]\n' > fix-gone.yaml
run ait future offer src/stock.c fix-gone.yaml -m 'fixture to remove'
G=$(newest O)
git rm -q --cached tests/stock.csv
run ait fulfill "$G" --force -m 'trivial'
check_rc "a fixture gone from the code is refused" 128
check "as a scope not in the code" "has err \"^fatal: bad scope: 'tests/stock.csv' of '$G' is not in the code$\""
git add tests/stock.csv

# 3.2, 5.3: a line range of a fixture takes its base blob, and only those lines witness
printf 'a\nb\nc\n' > tests/cases.txt; git add tests/cases.txt; git commit -qm cases
printf 'body: |\n  case b holds\nfixtures: [tests/cases.txt:2-2]\n' > fix-range.yaml
run ait future offer src/stock.c fix-range.yaml -m 'range fixture'
check_rc "a line-range fixture is taken" 0
R=$(newest O)
check "it is written with its base blob" "yaml $R | grep -qE '^  fixtures: \\[\"tests/cases.txt:2-2 [0-9a-f]{40}\"\\]$'"
run ait fulfill "$R" --force -m 'checked'
check_rc "fulfil the range fixture" 0
printf 'a\nb\nc\nd\n' > tests/cases.txt; git add tests/cases.txt
run ait status /
check "a change outside the range keeps it current" "test \"\$(state_of $R)\" = current"
printf 'a\nB\nc\n' > tests/cases.txt; git add tests/cases.txt
run ait status /
check "a change inside the range: current ?unverified" "test \"\$(state_of $R)\" = 'current ?unverified'"
check "the fixtures line shows the range ?changed" "grep -A8 '^id $R$' '$T/out' | grep -q '^fixtures tests/cases.txt:2-2 ?changed$'"
git reset -q; git checkout -q -- tests/cases.txt

# 2.3.2: a fixture is a file or a line range; no directory, root or new: path
for bad in tests/ / new:tests/x.csv; do
	printf 'body: |\n  x\nfixtures: ["%s"]\n' "$bad" > fix-bad.yaml
	run ait future offer src/stock.c fix-bad.yaml -m 'bad fixture'
	check_rc "fixture '$bad' is refused" 128
	check "fixture '$bad' is a format error of its file" "has err \"^fatal: bad format: 'fix-bad.yaml': \""
done

# plan 6, 2.3.2: a file fixture is judged by OIDs alone, in a clone without the recorded blob
printf 'body: |\n  the csv lists items\nfixtures: [tests/stock.csv]\n' > fix-clone.yaml
run ait future offer src/stock.c fix-clone.yaml -m 'clone fixture'
P=$(newest O)
ait fulfill "$P" --force -m 'checked' >/dev/null 2>&1
old=$(git rev-parse HEAD:tests/stock.csv)
printf 'item,count\nnew,1\n' > tests/stock.csv; git add tests/stock.csv; git commit -qm 'csv grows'
git push -q origin master ait/master
git -C "$T/origin.git" config uploadpack.allowFilter true
git clone -q --filter=blob:none "file://$T/origin.git" "$T/lean" 2>/dev/null
cd "$T/lean"
# the contract branch is fetched whole, as ait reads it; only the code's old blob stays behind
git archive origin/ait/master >/dev/null
git config remote.origin.url "$T/gone.git"
git branch -q ait/master origin/ait/master
check "the clone lacks the recorded blob and cannot fetch it" "! git cat-file -e $old 2>/dev/null"
run ait status /
check "status judges the file fixture there" "test \"\$(state_of $P)\" = 'current ?unverified' && grep -A8 '^id $P$' '$T/out' | grep -q '^fixtures tests/stock.csv ?changed'"
cd "$T/r"

# 6.3, 9.5: a path selects the contracts whose consumer or checked scopes (2.3.2) meet it
mkdir -p data; printf 'x\n' > data/only.csv; git add data; git commit -qm 'a fixture alone'
printf 'body: |\n  only the csv\nengine: exec\ncommand: [true]\nenvironment: node\nfixtures: [data/only.csv]\n' > fix-only.yaml
run ait future offer src/order.c fix-only.yaml -m 'fixture selects'
F=$(newest O)
run ait status data/only.csv
check "status PATH picks a contract by its fixture" "grep -qx 'id $F' '$T/out'"
run ait status specs/environments/node.yaml
check "status PATH picks a contract by its environment file" "grep -qx 'id $F' '$T/out'"
BOB=$(AIT_SESSION=- ait session open --agent bob 2>/dev/null)
AIT_SESSION=$BOB ait sub data/only.csv >/dev/null
AIT_SESSION=$BOB ait listen > "$T/events" 2>/dev/null &
L=$!
sleep 1.5
ait future amend "$F" -m 'touch the fixture contract' >/dev/null
for i in $(seq 1 50); do grep -q ait-event-end "$T/events" && break; sleep 0.1; done
kill $L; wait $L 2>/dev/null
check "sub scope on a fixture is told of its contract" "grep -qx 'reason sub scope data/only.csv' '$T/events'"

rm -f fix.yaml fix-prompt.yaml fix-only.yaml fix-absent.yaml fix-gone.yaml fix-range.yaml fix-bad.yaml fix-clone.yaml
done_testing
