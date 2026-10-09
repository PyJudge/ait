#!/bin/sh
# T27 쓰기 전제조건 (5.2)
. "$(dirname "$0")/lib.sh"

setup
S=$(ait session open --agent agent-a)
prompt_defs
tick; ait future offer src/stock.c specs/p.yaml as o -m m >"$T/out"
O=$(new_id)

# 1: .ait dirty
# mini: no contract add/commit, so the keep command ('ait contract add PATH...' and
# 'ait contract commit -m <why>') cannot be given; only the drop form is checked
dirty_hints() { err_has "restore '.ait/' to HEAD's version and remove the new files there"; }
echo x >> ".ait/$O"
desc="1: modified .ait record"
tick; expect_unchanged expect_err "$desc" E_DIRTY ait future cancel "$O" -m m
check "1: first line names .ait/" sh -c "head -1 '$T/err' | grep -qxF 'error: uncommitted changes in .ait/'"
check "1: drop command (modified)" dirty_hints
git checkout -q HEAD -- .ait

echo x > .ait/Onew000000000
desc="1: untracked file in .ait"
tick; expect_unchanged expect_err "$desc" E_DIRTY ait future cancel "$O" -m m
check "1: drop command (untracked)" dirty_hints
rm -f .ait/Onew000000000

echo x >> ".ait/$O"; git add ".ait/$O"
desc="1: staged change in .ait"
tick; expect_unchanged expect_err "$desc" E_DIRTY ait future cancel "$O" -m m
check "1: drop command (staged)" dirty_hints
git reset -q HEAD -- .ait; git checkout -q HEAD -- .ait

# 2: a conflicted merge in progress
git branch side
echo master1 > src/order.c; tick; git commit -qam m1
git switch -q side; echo side1 > src/order.c; tick; git commit -qam s1
git switch -q master
tick; git merge -q side >/dev/null 2>&1
check "2: merge stopped with a conflict" sh -c "git rev-parse -q --verify MERGE_HEAD >/dev/null"
desc="2: future cancel during merge"
tick; expect_unchanged expect_err "$desc" E_BUSY ait future cancel "$O" -m m
# mini: basic hint only ('a git merge is in progress ...' is not checked)
desc="2: fulfill during merge"
tick; expect_unchanged expect_err "$desc" E_BUSY ait fulfill "$O" -m m
# mini: no contract commit / contract pull
tick; expect_err "2: contract commit (mini: unsupported)" MINI ait contract commit -m m
tick; expect_err "2: contract pull (mini: unsupported)" MINI ait contract pull Rdeadbeef0000
git merge --abort

# 3: detached HEAD
git checkout -q --detach
desc="3: write on a detached HEAD"
tick; expect_unchanged expect_err "$desc" E_BUSY ait future cancel "$O" -m m
check "3: hint: run this command with HEAD on a branch" err_has "hint: run this command with HEAD on a branch"
check "3: no git command that switches branches" sh -c "! grep '^hint:' '$T/err' | grep -qE 'git (switch|checkout)'"
git switch -q master

# 3: a repository with no commits, after opening a session
git init -q "$T/empty"; cd "$T/empty"
E=$(ait session open --agent agent-e)
check "3: session opened in an empty repository" test -n "$E"
prompt_defs
desc="3: write in a repository with no commits"
tick; expect_unchanged expect_err "$desc" E_BUSY ait future offer src/stock.c specs/p.yaml -m m
check "3: hint: run 'ait init' first" err_has "hint: run 'ait init' first"

done_testing
