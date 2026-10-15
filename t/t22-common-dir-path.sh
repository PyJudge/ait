#!/bin/sh
# spec 2.6 and 6.1: shared state and Git paths follow the common dir Git selected.
. "$(dirname "$0")/lib.sh"
setup

git clone -q "$T/r" "$T/common worktree 한"
common="$T/common worktree 한/.git"
cp .git/info/exclude "$T/original-exclude"
cp .git/hooks/reference-transaction "$T/original-hook"

run env GIT_COMMON_DIR="$common" ait init
check_rc "init accepts an alternate common directory with spaces and Unicode" 0
check "init updates the exclude file selected by Git" "grep -qx '/.ait/' \"$common/info/exclude\""
check "init installs the hook selected by Git" "test -x \"$common/hooks/reference-transaction\""
check "init creates shared state beside the selected hook and exclude" "test -d \"$common/ait\""
check "init preserves the original common directory files" "cmp -s .git/info/exclude $T/original-exclude && cmp -s .git/hooks/reference-transaction $T/original-hook"

run env GIT_COMMON_DIR='../common worktree 한/.git' ait session open --agent selected
S=$(cat "$T/out")
check_rc "a relative common directory is resolved by Git before child commands" 0
check "session data uses the selected common directory" "test -f \"$common/ait/sessions/$S\" && ! test -f .git/ait/sessions/$S"
run env GIT_COMMON_DIR="$common" AIT_SESSION="$S" ait session list
check_rc "session reads use the same selected common directory" 0
check "the session remains selected" "has out \"^session $S\" && has out '^default yes'"

done_testing
