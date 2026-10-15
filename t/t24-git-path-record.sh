#!/bin/sh
# spec 6.1: a scalar path from Git retains embedded newlines.
. "$(dirname "$0")/lib.sh"
setup

hooks=$(printf '%s/hook prefix\nsecond line' "$T")
git config core.hooksPath "$hooks"
run ait init
check_rc "init accepts a hook directory containing a newline" 0
check "init writes the hook at Git's complete scalar path" "test -x \"$hooks/reference-transaction\""
check "init does not create a file at a truncated path" "! test -e '$T/hook prefix'"
wake=$(git rev-parse --git-common-dir)/ait/wake
before=$(wc -c <"$wake")
git update-ref refs/heads/ait/native ait/master
after=$(wc -c <"$wake")
check "Git executes the hook at the complete newline path" "test $after -gt $before"
git update-ref -d refs/heads/ait/native

# The stage path also comes from Git after a symlink target has been resolved.
staged_dir=$(printf '%s/stage prefix\nsecond line' "$T")
mkdir -p "$staged_dir"
backing="$staged_dir/index"
stage=$(git rev-parse --absolute-git-dir)/ait/index
mkdir -p "$(dirname "$stage")"
ln -s "$backing" "$stage"
run ait contract new demand
P=$(sed -n 's/^path //p' "$T/out")
sed -i.bak 's/<scope>/src\/stock.c/g; s/<condition>/newline path/' "$P"
run ait contract add "$P"
check_rc "contract add accepts a stage symlink target containing a newline" 0
check "the contract stage is written at the complete symlink target" "test -f \"$backing\" && GIT_INDEX_FILE=\"$backing\" git ls-files --error-unmatch '${P#.ait/}' >/dev/null"
check "staging does not create a file at the truncated target" "! test -e '$T/stage prefix'"
run ait contract commit -m newline
check_rc "contract commit reads and consumes the complete stage target" 0
check "commit preserves the stage symlink while consuming its target" "test -L \"$stage\" && ! test -e \"$backing\""

# Native Git permits a newline in an alternate code-index path too.
selected=$(printf '%s/code index\nsecond line' "$T")
GIT_INDEX_FILE="$selected" git read-tree HEAD
printf 'alternate newline stock\n' >> src/stock.c
GIT_INDEX_FILE="$selected" git add src/stock.c
run env GIT_INDEX_FILE="$selected" ait status src/stock.c
check_rc "status accepts a selected code-index path containing a newline" 0
check "status uses the complete newline code-index path" "has out '^Changes to be committed:'"

done_testing
