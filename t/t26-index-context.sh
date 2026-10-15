#!/bin/sh
# v14 5.12, v1.1 5.1 and 6.2: resolve internal indexes before fixing Git's cwd;
# native checkout still receives the original environment in the original cwd.
. "$(dirname "$0")/lib.sh"
setup

run ait offer src/stock.c cond.yaml --force -m baseline
O=$(newest O)
ordinary_tree=$(git write-tree)
selected="$T/selected"
GIT_INDEX_FILE="$selected" git read-tree HEAD
printf 'selected stock\n' >> src/stock.c
GIT_INDEX_FILE="$selected" git add src/stock.c
selected_tree=$(GIT_INDEX_FILE="$selected" git write-tree)
git checkout -- src/stock.c
# Native checkout resolves a relative index after setup_work_tree(), unlike
# rev-parse outside the worktree. Keep both candidates with different content.
GIT_INDEX_FILE="$T/r/selected" git read-tree HEAD

selected_status() {
	label=$1
	shift
	run "$@" ait status src/stock.c
	check_rc "$label: status succeeds" 0
	check "$label: the selected index supplies the contract tree" "grep -A3 \"^id $O\" $T/out | grep -qx 'state current ?unverified'"
}

cd "$T"
selected_status 'outside, relative repository and index' env GIT_DIR=r/.git GIT_WORK_TREE=r GIT_INDEX_FILE=selected
selected_status 'outside, absolute repository and relative index' env GIT_DIR="$T/r/.git" GIT_WORK_TREE="$T/r" GIT_INDEX_FILE=selected
selected_status 'outside, relative repository and absolute index' env GIT_DIR=r/.git GIT_WORK_TREE=r GIT_INDEX_FILE="$selected"
cd "$T/r"
selected_status 'inside root, discovered repository and relative index' env GIT_INDEX_FILE=../selected
cd src
selected_status 'inside subdirectory, discovered repository and relative index' env GIT_INDEX_FILE=../selected
selected_status 'inside subdirectory, relative repository and index' env GIT_DIR=../.git GIT_WORK_TREE=.. GIT_INDEX_FILE=../selected
selected_status 'inside subdirectory, absolute repository and relative index' env GIT_DIR="$T/r/.git" GIT_WORK_TREE="$T/r" GIT_INDEX_FILE=../selected
selected_status 'inside subdirectory, absolute index' env GIT_INDEX_FILE="$selected"

native_checkout() {
	label=$1
	path=$2
	shift 2
	printf 'temporary worktree edit\n' > "$T/r/src/stock.c"
	"$@" git checkout -- "$path" >"$T/native.out" 2>"$T/native.err"
	native_rc=$?
	cp "$T/r/src/stock.c" "$T/native.stock"
	printf 'temporary worktree edit\n' > "$T/r/src/stock.c"
	run "$@" ait checkout -- "$path"
	check_rc "$label: checkout keeps Git's exit code" "$native_rc"
	check "$label: checkout keeps Git's output" "cmp $T/native.out $T/out && cmp $T/native.err $T/err"
	check "$label: checkout restores the same content as Git" "cmp $T/native.stock $T/r/src/stock.c"
}

cd "$T"
native_checkout 'outside, relative repository and index' src/stock.c env GIT_DIR=r/.git GIT_WORK_TREE=r GIT_INDEX_FILE=selected
native_checkout 'outside, absolute repository and relative index' src/stock.c env GIT_DIR="$T/r/.git" GIT_WORK_TREE="$T/r" GIT_INDEX_FILE=selected
native_checkout 'outside, absolute index' src/stock.c env GIT_DIR=r/.git GIT_WORK_TREE=r GIT_INDEX_FILE="$selected"
native_checkout 'outside, unset index' src/stock.c env GIT_DIR=r/.git GIT_WORK_TREE=r
native_checkout 'outside, empty index' src/stock.c env GIT_DIR=r/.git GIT_WORK_TREE=r GIT_INDEX_FILE=
cd "$T/r/src"
native_checkout 'inside subdirectory, discovered repository' stock.c env GIT_INDEX_FILE=../selected
native_checkout 'inside subdirectory, relative repository' stock.c env GIT_DIR=../.git GIT_WORK_TREE=.. GIT_INDEX_FILE=../selected
native_checkout 'inside subdirectory, absolute index' stock.c env GIT_INDEX_FILE="$selected"
cd "$T/r"
git checkout -- src/stock.c

# Contract and temporary indexes take precedence over the caller's code index.
run ait contract new demand
P=$(sed -n 's/^path //p' "$T/out")
sed -i.bak 's/<scope>/src\/stock.c/g; s/<condition>/index context/' "$P"
cd "$T"
run env GIT_DIR=r/.git GIT_WORK_TREE=r GIT_INDEX_FILE=selected ait contract add "$P"
check_rc 'outside contract add selects the contract stage' 0
run env GIT_DIR=r/.git GIT_WORK_TREE=r GIT_INDEX_FILE=selected ait contract commit -m index
check_rc 'outside contract commit selects its temporary index' 0
cd "$T/r"
check 'contract staging and checkin preserve both code indexes' "test \"\$(GIT_INDEX_FILE=\"$selected\" git write-tree)\" = $selected_tree && test \"\$(git write-tree)\" = $ordinary_tree"

# Fulfill uses the selected code index through both its checks and code commit.
cd "$T"
run env GIT_DIR=r/.git GIT_WORK_TREE=r GIT_INDEX_FILE=selected ait fulfill "$O" --force -m selected
check_rc 'outside fulfill commits the selected index' 0
cd "$T/r"
check 'outside fulfill records the selected content' "git show HEAD:src/stock.c | grep -qx 'selected stock'"
check 'outside fulfill leaves the ordinary index alone' "test \"\$(git write-tree)\" = $ordinary_tree"
check 'the selected index agrees with the fulfilled HEAD' "GIT_INDEX_FILE=\"$selected\" git diff --cached --quiet HEAD"

done_testing
