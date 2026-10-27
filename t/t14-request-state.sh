#!/bin/sh
# spec 4.4, 6.4.1, 6.5.4, 6.7.2: request inputs survive judgments made again.
. "$(dirname "$0")/lib.sh"
setup
racing_git
printf '%s\n' "$PATH" > "$T/plain-path"

# Move only the parent: the request's files remain eligible for another judgment.
cat > "$T/empty-move" <<'H'
tree=$(git rev-parse 'ait/master^{tree}')
tip=$(printf 'unrelated parent\n' | git commit-tree -p ait/master "$tree")
git update-ref refs/heads/ait/master "$tip"
H
cat > "$T/two-moves" <<'H'
PATH=$(cat "$T/plain-path"); export PATH
round=$(($(cat "$T/round") + 1))
printf '%s\n' "$round" > "$T/round"
if [ "$round" = 2 ] && [ -f "$T/on-second" ]; then
	sh "$T/on-second" || exit 1
else
	sh "$T/empty-move" || exit 1
fi
if [ "$round" = 1 ]; then
	cp "$T/two-moves" "$T/before-update-ref"
fi
H
two_moves() {
	printf '0\n' > "$T/round"
	cp "$T/two-moves" "$T/before-update-ref"
}

# A failed CAS leaves its candidate unreachable, so examine every judgment's output.
candidates() {
	git fsck --no-reflogs --unreachable 2>/dev/null | sed -n 's/^unreachable commit //p' > "$T/commits"
	git rev-parse ait/master >> "$T/commits"
	: > "$T/candidates"
	while read candidate; do
		if [ "$(git show -s --format=%s "$candidate")" = "$1" ]; then
			printf '%s\n' "$candidate" >> "$T/candidates"
		fi
	done < "$T/commits"
	: > "$T/trees"
	: > "$T/rids"
	while read candidate; do
		git rev-parse "$candidate^{tree}" >> "$T/trees"
		git show -s --format=%B "$candidate" | sed -n 's/^Ait-Request: //p' >> "$T/rids"
	done < "$T/candidates"
}

# Two new contracts keep their aliases and +n results; A -> B -> A leaves an old contract unchanged.
cp "$T/exec.yaml" cond-a.yaml
cp "$T/prompt.yaml" cond-b.yaml
run ait future demand src/order.c src/stock.c:1-2 cond-a.yaml -m baseline
BASE=$(newest D)
yaml "$BASE" > "$T/baseline.yaml"
run ait future amend "$BASE" - - cond-b.yaml future amend "$BASE" - - cond-a.yaml \
	future demand src/order.c src/stock.c:3-4 cond-a.yaml as d \
	offer src/stock.c:3-4 cond-a.yaml as o undertake d o -m durable-request
check_rc "the new-contract request asks for results" 1
check "its task names are the alias creation order (7.1)" "has out '^task +1:1$' && has out '^task +2:1$'"
res=$(pass_all)
printf '%s\n' "$res" > "$T/result-path"
cat > "$T/before-commit" <<'H'
printf 'body: |\n  NEVER CHECKED A\n' > cond-a.yaml
printf 'body: |\n  NEVER CHECKED B\n' > cond-b.yaml
printf 'not the results read at step zero\n' > "$(cat "$T/result-path")"
H
echo durable >> src/order.c
git add src/order.c
two_moves
run env PATH="$TP/bin:$PATH" ait future amend "$BASE" - - cond-b.yaml future amend "$BASE" - - cond-a.yaml \
	future demand src/order.c src/stock.c:3-4 cond-a.yaml as d \
	offer src/stock.c:3-4 cond-a.yaml as o undertake d o -r "$res" -m durable-request
check_rc "the request survives two failed CAS operations" 0
D=$(newest D)
O=$(newest O)
candidates '[FULFILL] durable-request'
check "both failed candidates and the final commit have identical files and RID" \
	"test \"\$(cat $T/round)\" = 2 && test \"\$(wc -l < $T/candidates | tr -d ' ')\" = 3 && test \"\$(sort -u $T/trees | wc -l | tr -d ' ')\" = 1 && test \"\$(sort -u $T/rids | wc -l | tr -d ' ')\" = 1"
check "the final aliases and fulfilment output name the same new contracts once" \
	"has out '^new $D d$' && has out '^new $O o$' && test \"\$(grep -c '^new ' $T/out)\" = 2 && has out '^fulfilled $D$' && has out '^fulfilled $O$'"
check "A -> B -> A keeps the old contract and its IDs unchanged" "yaml $BASE > $T/after.yaml && cmp -s $T/baseline.yaml $T/after.yaml"
check "the original condition and +n results are recorded under real IDs" \
	"yaml $D | grep -q 'stock never goes below zero' && yaml $O | grep -q 'stock never goes below zero' && git show ait/master:fulfill/$D-\$(head -1 $T/rids).md | grep -q '^. task: $D:1$' && git show ait/master:fulfill/$O-\$(head -1 $T/rids).md | grep -q '^. task: $O:1$'"
check "the commit hook really changed both external input files" \
	"grep -q 'NEVER CHECKED A' cond-a.yaml && grep -q 'not the results' $res && git show ait/master:fulfill/$O-\$(head -1 $T/rids).md | grep -q 'checked'"

# The first retry is safe; a new partial offer at the second tip changes step zero's covered set.
run ait future demand src/order.c src/stock.c:7-8 cond.yaml -m covered
COVERED=$(newest D)
cat > "$T/on-second" <<H
ait future offer src/stock.c:7-8 cond.yaml as p undertake -p $COVERED p -m new-covered > "\$T/new-covered.out"
H
two_moves
run env PATH="$TP/bin:$PATH" ait fulfill "$COVERED" --force -m fixed-covered
check_rc "a covered set changed after a safe retry is not recorded" 1
check "the fixed-set refusal retains its context and gives no retry hint (C67)" \
	"test \"\$(cat $T/round)\" = 2 && has err \"'$COVERED' not fulfilled: 'ait/master' moved while running\" && ! has err '^hint:' && ! has out '^fulfilled '"
check "the concurrent offer remains, with no fulfilment of the old covered set" \
	"grep -q '^new O' $T/new-covered.out && test -z \"\$(git ls-tree -r --name-only ait/master fulfill/ | grep /$COVERED-)\""

# A later E_MOVED can still name a drop whose own contract was unchanged throughout the retries.
rm -f "$T/on-second"
run ait future demand src/order.c src/stock.c:11-12 cond.yaml -m target
TARGET=$(newest D)
ait fulfill "$TARGET" --force -m current >/dev/null 2>&1
run ait future drop "$TARGET" -m drop
DROP=$(newest X)
cat > "$T/on-second" <<H
ait future amend $TARGET src/order.c:1-2 - - --force -m changed-target >/dev/null
H
two_moves
run env PATH="$TP/bin:$PATH" ait fulfill "$DROP" -m durable-retry
check_rc "a target collision after one safe retry still stops" 1
check "the retry hint retains the original fulfill arguments" \
	"test \"\$(cat $T/round)\" = 2 && has err \"hint: ait fulfill $DROP -m durable-retry\" && ! has out '^withdrawn '"
run ait fulfill "$DROP" -m durable-retry
check_rc "the retained retry command succeeds" 0
check "the successful retry prints the final fulfillment and withdrawal" \
	"has out '^fulfilled $DROP$' && has out '^withdrawn $TARGET$' && has out '\\[FULFILL\\] durable-retry'"

# contract commit reparses the staged bytes, rather than retaining its stamped prior judgment or rereading disk.
rm -f "$T/on-second"
run ait future demand src/order.c src/stock.c:15-16 cond.yaml -m draft
DRAFT=$(newest D)
ait contract checkout "$DRAFT" >/dev/null
sed -i.bak 's#consumer: "src/order.c"#consumer: "src/order.c:5-6"#' ".ait/$DRAFT.yaml"
ait contract add ".ait/$DRAFT.yaml" >/dev/null
printf '%s\n' "$DRAFT" > "$T/draft-id"
cat > "$T/on-second" <<'H'
draft=$(cat "$T/draft-id")
printf 'no longer the staged draft\n' > ".ait/$draft.yaml"
sh "$T/empty-move"
H
two_moves
run env PATH="$TP/bin:$PATH" ait contract commit -m durable-draft
check_rc "the fixed staged draft is reparsed after two failed CAS operations" 0
candidates '[CONTRACT] durable-draft'
check "each draft judgment produces the same contract and request ID" \
	"test \"\$(cat $T/round)\" = 2 && test \"\$(wc -l < $T/candidates | tr -d ' ')\" = 3 && test \"\$(sort -u $T/trees | wc -l | tr -d ' ')\" = 1 && test \"\$(sort -u $T/rids | wc -l | tr -d ' ')\" = 1"
check "the staged range is pinned, the changed disk file is ignored, and output survives" \
	"yaml $DRAFT | grep -q 'consumer: \"src/order.c:5-6 ' && yaml $DRAFT | grep -q 'stock never goes below zero' && grep -q 'no longer the staged draft' .ait/$DRAFT.yaml && has out '^change $DRAFT$' && ! test -e \"\$(git rev-parse --git-dir)/ait/index\""

done_testing
