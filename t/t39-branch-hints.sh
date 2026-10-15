#!/bin/sh
# spec 4.3-3, 4.9: the format-repair hint preserves a Git-valid branch as one shell word.
. "$(dirname "$0")/lib.sh"
setup
bad=$(printf 'not a contract\n' | git hash-object -w --stdin)
index="$T/broken-index"
GIT_INDEX_FILE="$index" git read-tree ait/master
GIT_INDEX_FILE="$index" git update-index --add --cacheinfo "100644,$bad,D111111111111.yaml"
tree=$(GIT_INDEX_FILE="$index" git write-tree)
commit=$(printf 'malformed tip\n' | git commit-tree "$tree")
git switch -q --detach HEAD
run ait checkout ait/master
check_rc 'contract checkout can keep a clean detached HEAD' 0
check 'clean detached HEAD has the specified wording' "has err 'the work tree stays on a detached HEAD;'"
printf 'dirty\n' >> src/stock.c
run ait checkout ait/master
check_rc 'contract checkout can keep a dirty detached HEAD' 0
check 'dirty detached HEAD has the same specified wording' "has err '^hint: the work tree stays on a detached HEAD$'"
git restore src/stock.c
git switch -q master
for REF in 'ait/dollar$branch' "ait/quote'branch"; do
	git update-ref "refs/heads/$REF" "$commit"
	ait checkout "$REF" >/dev/null 2>&1
	run ait future offer src/stock.c cond.yaml -m rejected
	check_rc 'the selected malformed branch is refused' 128
	sed -n "s/^hint:.*(enter it with '\(.*\)')$/\1/p" "$T/err" > "$T/switch-hint.sh"
	run sh "$T/switch-hint.sh"
	check_rc 'the format-repair hint executes with the literal branch name' 0
	check 'the repair hint enters exactly that branch' "test \"\$(git symbolic-ref --short HEAD)\" = \"\$REF\""
	git switch -q master
done
# Display abbreviation is not a branch identity when a tag has the same name.
ait checkout ait/master >/dev/null 2>&1
git tag master
run ait checkout ait/master
check 'a same-name tag does not alter the actual HEAD branch label' "grep -Fq \"the work tree stays on 'master';\" $T/err"
run ait future offer src/stock.c cond.yaml -m target
O=$(newest O)
git branch ait/tagged ait/master
git tag ait/tagged
git switch -q ait/tagged 2>/dev/null
before=$(git rev-parse HEAD)
tip=$(git rev-parse ait/master)
run ait fulfill "$O" --force -m blocked
check_rc 'a tag cannot hide that HEAD is on another contract branch' 128
check 'the shared entry check reports the exact contract branch' "grep -Fq \"fatal: HEAD is on contract branch 'ait/tagged'\" $T/err"
check 'the HEAD-branch refusal changes neither branch' "test \"\$(git rev-parse HEAD)\" = $before && test \"\$(git rev-parse ait/master)\" = $tip"
git switch -q master 2>/dev/null
done_testing
