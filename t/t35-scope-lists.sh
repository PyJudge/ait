#!/bin/sh
# v14 3.2a, 3.3; v1.1 3.2: lists share input identity and draft pinning.
. "$(dirname "$0")/lib.sh"
setup
stage=$(git rev-parse --git-dir)/ait/index
base=$(git rev-parse ait/master)
blob=$(git rev-parse HEAD:src/stock.c)

draft() {
	run ait contract new "$1"
	P=$(sed -n 's/^path //p' "$T/out")
	K=$(basename "$P" .yaml)
	sed -i.bak 's#consumer: "<scope>"#consumer: "src/order.c"#; s#provider: "<scope>"#provider: "src/stock.c"#' "$P"
}
set_scopes() {
	sed "s#^$1: .*#$1: \"$2\"#" "$P" > "$T/edit"
	mv "$T/edit" "$P"
}
stage_raw() {
	obj=$(git hash-object -w "$P")
	GIT_INDEX_FILE="$stage" git update-index --add --cacheinfo "100644,$obj,$K.yaml"
}

# A list is one value, independently of whether a helper or draft supplies it.
run ait future offer src/stock.c,src/stock.c cond.yaml -m duplicate
check_rc "helper duplicate provider is a scope error" 128
check "helper duplicate identifies the scope" "has err \"'src/stock.c' is given twice\""
for kind in offer demand; do
	draft "$kind"
	for field in provider consumer; do
		[ "$kind:$field" = offer:consumer ] && continue
		cp "$P" "$T/valid"
		set_scopes "$field" src/stock.c,src/stock.c
		run ait contract add "$P"
		check_rc "draft add rejects duplicate $kind $field" 128
		stage_raw
		cp "$stage" "$T/stage"
		run ait contract commit -m duplicate
		check_rc "staged duplicate $kind $field is also refused" 128
		check "duplicate $kind $field refusal keeps stage, draft and branch" \
			"cmp -s $stage $T/stage && test \"\$(git rev-parse ait/master)\" = $base && grep -q 'src/stock.c,src/stock.c' $P"
		rm -f "$stage"
		git update-ref refs/heads/ait/master "$base"
		cp "$T/valid" "$P"
	done
done

# A hand-written base cannot exempt a new range from HEAD line-count checks.
draft offer
set_scopes provider "src/stock.c:1-999 $blob"
run ait contract add "$P"
check_rc "a syntactically valid supplied-base range can be staged" 0
cp "$stage" "$T/stage"
run ait contract commit -m 'supplied base beyond EOF'
check_rc "a supplied blob does not bypass range pinning" 128
check "range refusal preserves staged input and branch" \
	"cmp -s $stage $T/stage && test \"\$(git rev-parse ait/master)\" = $base && has err 'is not a range'"
rm -f "$stage"
git update-ref refs/heads/ait/master "$base"

# New file and abstract scopes obey the same tree checks as helper arguments.
for scopes in absent new:src/stock.c absent/; do
	set_scopes provider "$scopes"
	ait contract add "$P" >/dev/null
	run ait contract commit -m 'new draft scope'
	check_rc "new draft scope $scopes is checked against HEAD" 128
	rm -f "$stage"
	git update-ref refs/heads/ait/master "$base"
done

# Supplied bases are facts read from stored contracts, never authority for drafts.
other=$(printf 'unrelated\n' | git hash-object -w --stdin)
set_scopes provider "src/stock.c:1-2 $blob,src/stock.c:1-2 $other"
run ait contract add "$P"
check_rc "different supplied blobs do not distinguish duplicate input coordinates" 128
set_scopes provider "src/stock.c,src/stock.c:1-2 $blob,src/stock.c:2-3 $blob"
ait contract add "$P" >/dev/null
run ait contract commit -m 'overlap is not duplication'
check_rc "whole-file and overlapping distinct ranges remain permitted" 0
git update-ref refs/heads/ait/master "$base"
set_scopes provider "src/stock.c:1-2 $other"
ait contract add "$P" >/dev/null
run ait contract commit -m 'recompute supplied base'
check_rc "a valid new draft range is pinned" 0
check "the new range uses the HEAD blob instead of the supplied blob" \
	"yaml $K | grep -Fqx 'provider: \"src/stock.c:1-2 $blob\"'"
# The existing working draft remains after commit; edit it with its current RID.
yaml "$K" > "$P"
set_scopes provider "src/stock.c:1-2 $other"
ait contract add "$P" >/dev/null
run ait contract commit -m 'ignore changed draft base'
check_rc "an unchanged range keeps its original pin despite a supplied base" 0
check "changing only a draft blob leaves the stored range unchanged" \
	"yaml $K | grep -Fqx 'provider: \"src/stock.c:1-2 $blob\"'"

# The same parser also rejects bad lists imported through raw Git writes.
valid=$(git rev-parse ait/master)
yaml "$K" > "$T/stored"
sed 's#^provider: .*#provider: "src/stock.c,src/stock.c"#' "$T/stored" > "$T/bad-list"
GIT_INDEX_FILE="$T/branch-index" git read-tree ait/master
obj=$(git hash-object -w "$T/bad-list")
GIT_INDEX_FILE="$T/branch-index" git update-index --cacheinfo "100644,$obj,$K.yaml"
tree=$(GIT_INDEX_FILE="$T/branch-index" git write-tree)
changed=$(printf 'outside ait\n' | git commit-tree -p ait/master "$tree")
git update-ref refs/heads/ait/master "$changed"
run ait status src/
check "stored duplicate scopes are reported and skipped by readers" \
	"has err 'is given twice' && ! has out '^id $K$'"
run ait future offer src/order.c cond.yaml -m 'malformed branch'
check_rc "stored duplicate scopes block writers at the malformed-branch boundary" 128
git update-ref refs/heads/ait/master "$valid"

# Unchanged scope identity keeps its old pin even when HEAD no longer contains it.
yaml "$K" > "$P"
printf 'short\n' > src/stock.c
git add src/stock.c && git commit -qm 'shrink code'
ait contract add "$P" >/dev/null
run ait contract commit -m 'keep old range base'
check_rc "unchanged range is not repinned to shortened HEAD" 0
check "unchanged range preserves its older base blob" \
	"yaml $K | grep -Fqx 'provider: \"src/stock.c:1-2 $blob\"'"
set_scopes provider "src/stock.c:1-3 $blob"
ait contract add "$P" >/dev/null
run ait contract commit -m 'changed range checks HEAD'
check_rc "a changed existing range cannot trust its supplied base" 128
rm -f "$stage"

run ait future offer src/order.c cond.yaml -m 'unchanged whole file'
WHOLE=$(newest O)
ait contract checkout "$WHOLE" >/dev/null
WP=.ait/$WHOLE.yaml
git rm -q src/order.c && git commit -qm 'remove old provider'
ait contract add "$WP" >/dev/null
run ait contract commit -m 'keep unchanged missing file scope'
check_rc "an unchanged whole-file scope is not treated as newly supplied" 0

done_testing
