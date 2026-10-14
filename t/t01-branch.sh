#!/bin/sh
# spec 4.1-4.3, 6.1-6.3: the command set, init, the contract branch, checkout and switch
. "$(dirname "$0")/lib.sh"
setup

run ait frobnicate
check_rc "an unknown command is a usage error, not passed to git (4.1)" 129
check "its message" "has err \"'frobnicate' is not an ait command\""

check "init made ait/master from git's default branch name (6.1)" "git rev-parse -q --verify ait/master >/dev/null"
check "init excluded .ait/ locally (2.2)" "grep -qx '/.ait/' .git/info/exclude"
check "init installed the notification hook (U51)" "test -x .git/hooks/reference-transaction"
check "the branch shares no history with the code (2.1.1)" "! git merge-base master ait/master >/dev/null"
run ait init
check_rc "init again changes nothing" 0
check "and makes no second branch" "test \"\$(git for-each-ref refs/heads/ait/ | wc -l | tr -d ' ')\" = 1"

run ait status
check_rc "one local branch is the contract branch without a checkout (4.2)" 0
check "status says which one" "has out 'On contract branch ait/master (the only one)'"

git branch ait/try ait/master
run ait status
check_rc "two branches and no checkout cannot be chosen (4.3-2)" 128
check "the message" "has err 'more than one ait branch; none is checked out'"
check "a checkout hint per branch" "has err 'hint: ait checkout ait/try'"

run ait checkout ait/try
check_rc "checkout takes ait/<name> (6.2)" 0
check "it says the contract branch" "has out 'On contract branch ait/try'"
check "the work tree stays on the code branch" "test \"\$(git symbolic-ref --short HEAD)\" = master"
check "the hint to enter it" "has err \"to enter the contract branch: 'ait switch ait/try'\""
check "the checkout is the per-worktree file (2.6.1)" "grep -qx ait/try .git/ait-branch"

run ait checkout -b side
check_rc "any other form is git checkout's" 0
check "git made the branch" "test \"\$(git symbolic-ref --short HEAD)\" = side"
git checkout -q master

run ait switch ait/try
check_rc "switch enters a contract branch (6.3)" 0
check "HEAD is on it" "test \"\$(git symbolic-ref --short HEAD)\" = ait/try"
check "with the way back as a hint" "has err \"to go back: 'ait switch -'\""
run ait switch -
check_rc "switch - is git's" 0

echo x >> src/order.c
run ait switch ait/try
check_rc "a dirty work tree does not enter (6.3)" 128
git checkout -q -- src/order.c

git clone -q "$T/r" "$T/c" 2>/dev/null
cd "$T/c"
run ait status
check_rc "a clone with only remote-tracking ait branches (4.3-1)" 128
check "is told to run ait init" "has err 'hint: ait init'"
run ait checkout ait/master
check_rc "checkout of a remote-only name is not passed to git (6.2)" 128
check "the work tree is untouched" "test -f src/order.c"
run ait init
check_rc "init makes local branches from the remote ones (6.1-3)" 0
check "with their upstream" "test \"\$(git rev-parse --abbrev-ref ait/master@{upstream})\" = origin/ait/master"

done_testing
