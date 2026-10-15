#!/bin/sh
# spec 2.3.2; v14 8.2: only missing success defaults to [0], never explicit [].
. "$(dirname "$0")/lib.sh"
setup

printf 'body: same condition\nengine: exec\ncommand: [sh, -c, "exit 0"]\n' > omitted.yaml
cp omitted.yaml zero.yaml
printf 'success: [0]\n' >> zero.yaml
cp omitted.yaml empty.yaml
printf 'success: []\n' >> empty.yaml

run ait future offer src/stock.c empty.yaml -m 'empty success set'
EMPTY=$(newest O)
check_rc "an explicit empty success list is valid" 0
check "the stored contract preserves success []" "yaml $EMPTY | grep -Fqx '  success: []'"
run ait fulfill "$EMPTY" -m 'check empty success'
check_rc "the empty-success contract still requires check results" 1
check "its check request preserves success []" "grep -Fqx '  success: []' .git/ait/check-request.yaml"

run ait future offer src/order.c omitted.yaml -m 'default success set'
DEFAULT=$(newest O)
check_rc "omitted success remains valid" 0
check "omitted success is normalized to [0] in the stored model" "yaml $DEFAULT | grep -Fqx '  success: [0]'"
run ait fulfill "$DEFAULT" -m 'check default success'
check "the default success reaches the check request" "grep -Fqx '  success: [0]' .git/ait/check-request.yaml"

cid=$(yaml "$DEFAULT" | sed -n 's/^condition-id: //p')
change=$(yaml "$DEFAULT" | sed -n 's/^change-id: //p')
tip=$(git rev-parse ait/master)
run ait future amend "$DEFAULT" - zero.yaml -m 'same normalized condition'
check_rc "explicit [0] and omitted success are the same condition" 0
check "normalizing the default does not change either ID or create a commit" \
	"test \"\$(git rev-parse ait/master)\" = $tip && test \"\$(yaml $DEFAULT | sed -n 's/^condition-id: //p')\" = $cid && test \"\$(yaml $DEFAULT | sed -n 's/^change-id: //p')\" = $change"
run ait future amend "$DEFAULT" - empty.yaml -m 'empty differs from default'
check_rc "changing default [0] to [] is permitted" 0
check "the empty set changes the condition ID and stays empty" \
	"test \"\$(yaml $DEFAULT | sed -n 's/^condition-id: //p')\" != $cid && yaml $DEFAULT | grep -Fqx '  success: []'"

# The draft and stored-contract parser share the same normalized spec model.
run ait contract new offer
P=$(sed -n 's/^path //p' "$T/out")
DRAFT=$(basename "$P" .yaml)
sed 's#provider: "<scope>"#provider: "src/stock.c"#' "$P" | awk '
	/^undertakes:/ {
		print "  engine: exec"
		print "  command: [sh, -c, \"exit 0\"]"
		print "  success: []"
	}
	{ print }
' > "$T/draft"
cp "$T/draft" "$P"
ait contract add "$P" >/dev/null
run ait contract commit -m 'draft empty success set'
check_rc "a directly written empty-success draft commits" 0
check "draft commit preserves the explicitly empty success list" "yaml $DRAFT | grep -Fqx '  success: []'"
run ait fulfill "$DRAFT" -m 'check draft empty success'
check "a stored draft's check request still contains []" "grep -Fqx '  success: []' .git/ait/check-request.yaml"
yaml "$DRAFT" > "$P"
ait contract add "$P" >/dev/null
tip=$(git rev-parse ait/master)
run ait contract commit -m 'empty list roundtrip'
check_rc "an unchanged empty-success draft roundtrips" 0
check "the empty-list roundtrip keeps the condition and request history" \
	"test \"\$(git rev-parse ait/master)\" = $tip && yaml $DRAFT | grep -Fqx '  success: []'"

# A supplied success value must be a list at every reader of spec_from.
stage=$(git rev-parse --git-dir)/ait/index
for shape in null scalar mapping; do
	case $shape in null) value= ;; scalar) value=0 ;; mapping) value='{}' ;; esac
	cp omitted.yaml bad.yaml
	printf 'success: %s\n' "$value" >> bad.yaml
	run ait future offer src/stock.c bad.yaml -m "bad $shape success"
	check_rc "a condition file rejects success as $shape" 128
	tip=$(git rev-parse ait/master)
	yaml "$DRAFT" | awk -v value="$value" '
		!/^  success:/ { print; if (/^  command:/) print "  success: " value }
	' > "$P"
	run ait contract add "$P"
	check_rc "draft add rejects success as $shape" 128
	obj=$(git hash-object -w "$P")
	GIT_INDEX_FILE="$stage" git update-index --add --cacheinfo "100644,$obj,$DRAFT.yaml"
	cp "$stage" "$T/stage"
	run ait contract commit -m "bad staged $shape success"
	check_rc "staged drafts reject success as $shape" 128
	check "the $shape success refusal preserves the draft, stage, and branch" \
		"cmp -s $stage $T/stage && test \"\$(git rev-parse ait/master)\" = $tip && grep -Fqx '  success: $value' $P"
	rm -f "$stage"
	git update-ref refs/heads/ait/master "$tip"
	GIT_INDEX_FILE="$T/branch-index" git read-tree ait/master
	GIT_INDEX_FILE="$T/branch-index" git update-index --cacheinfo "100644,$obj,$DRAFT.yaml"
	tree=$(GIT_INDEX_FILE="$T/branch-index" git write-tree)
	changed=$(printf 'outside ait\n' | git commit-tree -p ait/master "$tree")
	git update-ref refs/heads/ait/master "$changed"
	run ait status src/
	check "stored contracts reject success as $shape" \
		"has err 'bad format:' && ! has out '^id $DRAFT$'"
	run ait future offer src/order.c zero.yaml -m "malformed $shape branch"
	check_rc "a stored $shape success blocks writers" 128
	git update-ref refs/heads/ait/master "$tip"
done

done_testing
