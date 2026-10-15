#!/bin/sh
# spec 5.1 and 6.7: the index selected by Git's environment remains the code index.
. "$(dirname "$0")/lib.sh"
setup

run ait offer src/stock.c cond.yaml --force -m baseline
O=$(newest O)
mkdir -p "$T/alternate index 한"
selected="$T/alternate index 한/index"
GIT_INDEX_FILE="$selected" git read-tree HEAD
printf 'alternate stock\n' >> src/stock.c
GIT_INDEX_FILE="$selected" git add src/stock.c
selected_tree=$(GIT_INDEX_FILE="$selected" git write-tree)

a=src/order.c
printf 'ordinary order\n' >> "$a"
git add "$a"
ordinary_tree=$(git write-tree)
run env GIT_INDEX_FILE="$selected" ait status src/stock.c
check_rc "status accepts an absolute alternate index with spaces and Unicode" 0
check "status judges the alternate index tree" "grep -A3 \"^id $O\" $T/out | grep -qx 'state current ?unverified'"
check "git status receives the same alternate index" "has out '^Changes to be committed:'"

# Contract staging and temporary checkin indexes override the selected code index.
run env GIT_INDEX_FILE="$selected" ait contract new demand
P=$(sed -n 's/^path //p' "$T/out")
sed -i.bak 's/<scope>/src\/stock.c/g; s/<condition>/alternate index/' "$P"
run env GIT_INDEX_FILE="$selected" ait contract add "$P"
check_rc "contract add still uses its contract stage" 0
check "contract staging preserves the alternate code index" "test \"\$(GIT_INDEX_FILE=\"$selected\" git write-tree)\" = $selected_tree"
run env GIT_INDEX_FILE="$selected" ait future offer src/order.c cond.yaml -m separate
check_rc "checkin still uses its temporary index" 0
check "checkin preserves both code indexes" "test \"\$(GIT_INDEX_FILE=\"$selected\" git write-tree)\" = $selected_tree && test \"\$(git write-tree)\" = $ordinary_tree"

# Git resolves the relative environment path before child Git uses -C.
cd src
run env GIT_INDEX_FILE='../alternate index 한/index' ait fulfill "$O" --force -m alternate
check_rc "fulfill commits from a relative alternate index outside the invocation directory" 0
cd ..
check "fulfill commits only the alternate stock change" "git show HEAD:src/stock.c | grep -qx 'alternate stock' && ! git show HEAD:src/order.c | grep -qx 'ordinary order'"
check "fulfill preserves the ordinary code index" "test \"\$(git write-tree)\" = $ordinary_tree"
check "the alternate code index matches the new HEAD" "GIT_INDEX_FILE=\"$selected\" git diff --cached --quiet HEAD"

# Git also supports a symlink spelling of the selected index.
ln -s "$selected" "$T/index-alias"
printf 'alias stock\n' >> src/stock.c
GIT_INDEX_FILE="$T/index-alias" git add src/stock.c
run env GIT_INDEX_FILE="$T/index-alias" ait fulfill "$O" --force -m alias
check_rc "fulfill accepts an index symlink alias" 0
check "the alias remains and its target supplies the code commit" "test -L $T/index-alias && git show HEAD:src/stock.c | grep -qx 'alias stock'"

done_testing
