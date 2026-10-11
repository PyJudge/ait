#!/bin/sh
# Regressions for the review of ait's work with git; each case was reproduced, then traced to its cause
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent a)
prompt_defs
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out"; O=$(new_id)
H=$(git rev-parse HEAD)

# 7.1.3: a rebase with no stopped commit is no integration; the plain commit guard holds
GIT_SEQUENCE_EDITOR='printf "break\n" >' git rebase -q -i HEAD >/dev/null 2>&1
sed -i.bak 's/^state future$/state current/' .ait/$O && rm .ait/$O.bak && git add .ait
tick; expect_err "a record changed during a rebase stopped at break" E_GUARD git commit -q -m hack
git reset -q --hard "$H"; git rebase --abort 2>/dev/null

# 7.1.6: pre-push runs from a bare repository too (hooks shared through core.hooksPath)
G=$T/shared-hooks
git config core.hooksPath "$G" && ait init >/dev/null 2>&1
git clone -q --mirror "$T/r" "$T/m.git"
git -C "$T/m.git" config core.hooksPath "$G"
git -C "$T/m.git" remote add up "$T/origin.git"
run git -C "$T/m.git" push -q up master
check "a push from a bare repository validates and goes" [ "$rc" = 0 ]
git config --unset core.hooksPath

# 7.1.1: a hook directory inside another worktree of the repository is the repository's
git worktree add -q "$T/wt" -b wt 2>/dev/null
git config core.hooksPath "$T/r/.githooks"
(cd "$T/wt" && run ait init; echo $rc > "$T/rc")
check "init from a linked worktree does not write it" sh -c "[ \$(cat '$T/rc') = 1 ] && [ ! -e '$T/r/.githooks/pre-commit' ]"
git config --unset core.hooksPath

# 5.8 and 7.2.1: a session whose worktree is gone
W=$(cd "$T/wt" && ait session open --agent w)
git worktree remove --force "$T/wt"
run env AIT_SESSION=$W ait todo
check "E_SESSION says the worktree is gone" err_has "its worktree is gone; close it: 'ait session close $W'"
run ait session open --agent w
check "and so does a session open with the same name" err_has "its worktree is gone; close it: 'ait session close $W'"

# object IDs are SHA-1 in ait; another format is refused plainly
S=$(mktemp -d) && git init -q --object-format=sha256 "$S/r"
(cd "$S/r" && ait init >/dev/null 2>"$T/err"; echo $? > "$T/rc")
check "a SHA-256 repository is not supported" sh -c "[ \$(cat '$T/rc') = 128 ] && grep -q 'fatal: not supported:' '$T/err'"
done_testing
