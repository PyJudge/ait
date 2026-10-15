#!/bin/sh
# spec 3, 6.3, 6.8, 9: paths, scope syntax and subscription display share their boundaries
. "$(dirname "$0")/lib.sh"
setup

run ait contract new spec .ait prompt
check_rc "the .ait directory name itself is not a condition file path" 128
if test -f .ait; then rm .ait; fi

for path in .ait/-spec.yaml .ait/.ait/spec.yaml; do
	run ait contract new spec "$path" prompt
	check_rc "a condition path keeps its original components: $path" 0
	run ait future offer src/stock.c "$path" -m nested-spec
	check_rc "a condition is read with the same original path grammar: $path" 0
done
for path in .ait/../outside.yaml .ait//invalid.yaml .ait/./invalid.yaml; do
	run ait contract new spec "$path" prompt
	check_rc "allowing .ait does not permit invalid components: $path" 128
done

BLOB=$(git rev-parse HEAD:src/stock.c)
for command in sub status; do
	run ait "$command" "src/stock.c:1-1 $BLOB"
	if [ "$command" = status ]; then want=129; else want=128; fi
	check_rc "a $command argument does not contain a stored base blob" "$want"
done
run ait future offer "src/stock.c:1-1 $BLOB" cond.yaml -m stored-argument
check_rc "a request argument does not contain a stored base blob" 128

mkdir -p .ait
printf 'private\n' > .ait/forbidden
git add -f .ait/forbidden && git commit -qm tracked
run ait sub .ait/forbidden:1-1
check_rc "a range does not bypass the subscription PATH grammar" 128
run ait status .ait/forbidden:1-1
check_rc "a range does not bypass the status PATH grammar" 129
run ait contract new offer
DRAFT=$(sed -n 's/^path //p' "$T/out")
sed -i.bak 's|<scope>|.ait/forbidden:1-1|; s|<condition>|condition|' "$DRAFT"
run ait contract add "$DRAFT"
check_rc "a draft range does not bypass the PATH grammar" 128
check "refusing the malformed draft leaves no stage lock" "! test -e .git/ait/index.lock"
rm -f "$DRAFT" "$DRAFT.bak" .git/ait/index

run ait status src/stock.c:99-99
check_rc "status rejects a range beyond the index blob" 128
run ait status absent:1-1
check_rc "status rejects a range without an index blob" 128
printf 'changed\n' >> src/order.c
run ait status /
check_rc "status accepts the repository scope" 0
check "the repository scope reaches git status" "has out 'modified:.*src/order.c'"
printf 'star\n' > 'src/star*'; printf 'other\n' > src/starX
git add -- 'src/star*' src/starX && git commit -qm stars
printf 'changed\n' >> 'src/star*'; printf 'changed\n' >> src/starX
run ait status 'src/star*'
check "status preserves literal filename characters" "has out 'modified:.*src/star\*' && ! has out 'modified:.*src/starX'"
git reset -q --hard

BOB=$(AIT_SESSION=- ait session open --agent bob-quotes 2>/dev/null)
AIT_SESSION=$BOB ait sub src/stock.c >/dev/null
IDS=
for group in plain 한글 'back\slash' 'double"quote'; do
	run ait future offer src/stock.c cond.yaml -g "$group" -m grouped
	check_rc "a valid group is accepted: $group" 0
	IDS="$IDS $(newest O)"
done
AIT_SESSION=$BOB ait listen > "$T/events" 2>"$T/listen.err" &
L=$!
TIP=$(git rev-parse ait/master)
for i in $(seq 1 50); do
	grep -q "^tip $TIP " ".git/ait/sessions/$BOB" && break
	sleep 0.1
done
kill "$L"; wait "$L" 2>/dev/null
for id in $IDS; do
	check "notification resolves the original contract filename: $id" "grep -qx 'contract $id create' '$T/events'"
done

BOB=$(AIT_SESSION=- ait session open --agent bob-abstract 2>/dev/null)
AIT_SESSION=$BOB ait sub src/stock.c >/dev/null
run ait future offer src/ cond.yaml -m directory; DIR=$(newest O)
run ait future offer / cond.yaml -m repository; ALL=$(newest O)
AIT_SESSION=$BOB ait listen > "$T/abstract" 2>"$T/listen.err" &
L=$!
TIP=$(git rev-parse ait/master)
for i in $(seq 1 50); do
	grep -q "^tip $TIP " ".git/ait/sessions/$BOB" && break
	sleep 0.1
done
kill "$L"; wait "$L" 2>/dev/null
check "a concrete subscription does not depend on an abstract provider" "! test -s '$T/abstract'"
check "unrelated abstract events are still consumed" "grep -q '^version $DIR ' '.git/ait/sessions/$BOB.seen' && grep -q '^version $ALL ' '.git/ait/sessions/$BOB.seen'"

BOB=$(AIT_SESSION=- ait session open --agent bob-range 2>/dev/null)
run env AIT_SESSION=$BOB ait sub src/stock.c:1-1
check "sub prints the range display without its stored blob" "grep -qx 'sub scope src/stock.c:1-1' '$T/out'"
{ echo inserted; cat src/stock.c; } > "$T/code"
cp "$T/code" src/stock.c
git add src/stock.c && git commit -qm inserted
run ait future offer src/stock.c:2-2 cond.yaml -m mapped
AIT_SESSION=$BOB ait listen > "$T/range" 2>"$T/listen.err" &
L=$!
TIP=$(git rev-parse ait/master)
for i in $(seq 1 50); do
	grep -q "^tip $TIP " ".git/ait/sessions/$BOB" && break
	sleep 0.1
done
kill "$L"; wait "$L" 2>/dev/null
check "a subscription reason is displayed at the receiving HEAD" "grep -qx 'reason sub scope src/stock.c:2-2' '$T/range'"
run env AIT_SESSION=$BOB ait unsub src/stock.c:2-2
check "unsub matches and prints the current HEAD display" "grep -qx 'unsub scope src/stock.c:2-2' '$T/out' && ! grep -q '^sub scope ' '.git/ait/sessions/$BOB'"

TIP=$(git rev-parse ait/master)
GIT_INDEX_FILE="$T/nonblob-index" git read-tree ait/master
GIT_INDEX_FILE="$T/nonblob-index" git update-index --add --cacheinfo "160000,$(git rev-parse HEAD),README"
TREE=$(GIT_INDEX_FILE="$T/nonblob-index" git write-tree)
COMMIT=$(echo malformed | git commit-tree "$TREE" -p "$TIP")
git update-ref refs/heads/ait/master "$COMMIT" "$TIP"
run ait status src/stock.c
check "status reports nonblob contract-tree entries" "grep -Fq \"warning: bad format: 'README'\" '$T/err'"
run ait future offer src/stock.c cond.yaml -m malformed
check_rc "a gitlink cannot bypass contract-tree path validation" 128
git update-ref refs/heads/ait/master "$TIP"

done_testing
