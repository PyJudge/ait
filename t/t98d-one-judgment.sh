#!/bin/sh
# Regressions for judgments made in several places and merged into one; each failed before
. "$(dirname "$0")/lib.sh"
setl() { f=$1 l=$2; shift 2; awk -v n="$l" -v t="$*" 'NR==n{print t; next}1' "$f" >"$f.tmp" && cat "$f.tmp" >"$f" && rm "$f.tmp"; }
not() { ! "$@"; }
fails() { [ "$rc" = "$1" ] && grep -qF -- "$2" "$T/err"; }
prep() {
	setup
	A=$(ait session open --agent a)
	prompt_defs
	git add -A && tick && git commit -q --no-verify -m defs
}

# 7.1.3-7: a side passes when the error is gone and the integration makes none of its own; HEAD's kept errors do not count
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>"$T/err"; K=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
tick; ait future offer src/order.c specs/w.yaml -m o2 >"$T/out" 2>"$T/err"; K2=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
git rm -q specs/w.yaml; tick; git commit -q --no-verify -m "lose w" 2>/dev/null
H=$(git rev-parse HEAD)
git branch side; git switch -q side; setl src/order.c 1 o1x; tick; git commit -q --no-verify -am side; git switch -q master
tick; git merge -q --no-commit --no-ff side >/dev/null
git rm -q specs/p.yaml
run ait hook pre-commit
check "the merge's own broken bundle: E_INCOMPLETE" fails 1 "error: spec files do not match the contract: '$K'"
check "HEAD's broken bundle does not stop HEAD's side from passing" err_has "hint: restore '.ait/$K' 'specs/p.yaml' as in '$H' and stage the result"
git checkout -q HEAD -- specs/p.yaml
tick; run git commit --no-edit
check "as the hint says, the merge commits" test "$rc" = 0
check "keeping HEAD's error" err_has "warning: '.ait/$K2' keeps an error from HEAD"

# 7.1.3: an incoming malformed record is no starting version, so its removal is no removal of a record (1)
prep
git branch side; git switch -q side
printf 'state nonsense\n' > .ait/O0000000000aa; git add .ait/O0000000000aa; tick; git commit -q --no-verify -m bad 2>/dev/null
B=$(git rev-parse HEAD)
git switch -q master; setl src/order.c 1 o1x; tick; git commit -q --no-verify -am ours
H=$(git rev-parse HEAD)
tick; git merge -q --no-commit --no-ff side >/dev/null
run git commit --no-edit
check "skipped as a starting version" err_has "warning: skipping malformed '.ait/O0000000000aa' at '$B'"
check "the merge's malformed record: E_FORMAT" err_has "fatal: bad format: '.ait/O0000000000aa'"
check "HEAD's side, which lacks it, passes" err_has "hint: restore '.ait/O0000000000aa' as in '$H' and stage the result"
check "not 'no side passes'" not err_has "no side passes"
git rm -q --cached .ait/O0000000000aa; rm -f .ait/O0000000000aa
tick; run git commit --no-edit
check "as the hint says, the merge commits" test "$rc" = 0
check "and does not ask for it back" not err_has "records are never removed"

# 2.1.4 (choice 257): a spec file beyond a symbolic link is missing, as one spec-file judgment says for every entry
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>"$T/err"; K=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
mv specs real; ln -s real specs
echo 'body: changed' > real/p.yaml
run ait contract add ".ait/$K"
check "contract add: the spec file is missing, not read through the link" sh -c "[ $rc = 1 ] && grep -qx 'missing specs/p.yaml' '$T/out'"
check "with the restore hint" err_has "hint: restore 'specs/p.yaml' from HEAD, or stop declaring it in '.ait/$K'"
run ait contract commit -m w
check "contract commit does not crash either" test "$rc" -lt 128

# 7.6.1: contract commit's noop judges the work tree as 7.5.2 does, so its hint is one contract add takes
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>"$T/err"; K=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
rm specs/p.yaml
run ait contract commit -m w
check "noop" out_has "noop"
check "a deleted spec file is missing, not unstaged" sh -c "grep -qx 'missing specs/p.yaml' '$T/out' && ! grep -q '^unstaged' '$T/out'"
check "the hint contract add gives for it, not 'stage it'" sh -c "grep -qF \"hint: restore 'specs/p.yaml' from HEAD, or stop declaring it in '.ait/$K'\" '$T/err' && ! grep -q 'stage it' '$T/err'"
echo 'body: new' > specs/p.yaml
run ait contract commit -m w
check "a changed one is still unstaged, with the stage hint" sh -c "grep -qx 'unstaged specs/p.yaml' '$T/out' && grep -qF \"stage it: 'ait contract add specs/p.yaml'\" '$T/err'"

# 7.12 (as 7.1.5, 9.3): what a pull brings in unverified is told only to whose demand it is, with H7 or H8
setup
SA=$(ait session open --agent a)
prompt_defs
git add -A && tick && git commit -q --no-verify -m defs
tick; ait demand src/order.c src/stock.c specs/p.yaml --force -m d >"$T/out" 2>"$T/err"; D=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
git branch side; git switch -q side
setl src/stock.c 1 s1x; tick; git commit -qam code 2>/dev/null
tick; ait amend "$D" src/stock.c - - -m consumer >/dev/null 2>&1
git switch -q master
SC=$(ait session open --agent c 2>/dev/null)
H=$(git rev-parse HEAD)
tick; AIT_SESSION=$SC run ait contract pull "$D"
check "taken" test "$rc" = 0
check "not told to another agent" not err_has "comes in unverified"
git reset -q --hard "$H"
tick; AIT_SESSION=$SA run ait contract pull "$D"
check "told to whose demand it is" err_has "warning: '$D' comes in unverified"
check "with its hint" err_has "hint: check '$D' again"

# 5.10 (user decision 55): HEAD's error stops a helper first, as git's commands stop on unmerged entries; its repair, or none, is the hint
prep
tick; ait demand src/order.c src/stock.c specs/p.yaml --force -m d >"$T/out" 2>"$T/err"; D=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
tick; ait offer src/order.c specs/w.yaml --force -m o >"$T/out" 2>"$T/err"; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
printf 'body: b\ninputs: [tests/none.sh]\n' > specs/p.yaml; git add specs/p.yaml; tick; git commit -q --no-verify -m nv 2>/dev/null
tick; run ait future cancel "$D" -m c
check "HEAD broke D's bundle beyond repair: the cancel stops on it before its transition" sh -c "grep -q \"^error: spec files do not match the contract: '$D'\" '$T/err' && ! grep -q 'cannot go from' '$T/err'"
check "with no hint, as no command mends it" not err_has "hint:"
sed -i.bak 's|^provider src/order.c |provider src/stock.c |' ".ait/$O" && rm -f ".ait/$O.bak"; git add ".ait/$O"; tick; git commit -q --no-verify -m nv 2>/dev/null
run ait todo
check "todo shows each of HEAD's errors, as git status shows unmerged paths" sh -c "grep -q \"warning: '$O' at HEAD was changed outside ait's rules\" '$T/err' && grep -q \"warning: spec files do not match the contract: '$D'\" '$T/err'"
check "and no empty hint where no command mends one" not err_has "(null)"
echo 'body: 재고가 0이면 예약 요청을 거절한다.' > specs/p.yaml; git add specs/p.yaml; tick; git commit -q --no-verify -m nv 2>/dev/null
git checkout -q HEAD~3 -- ".ait/$O"; git add ".ait/$O"; tick; git commit -q --no-verify -m nv 2>/dev/null
echo 'body: changed' > specs/p.yaml; git add specs/p.yaml; tick; git commit -q --no-verify -m nv 2>/dev/null
tick; run ait future cancel "$D" -m c
check "a bundle the gate pins again: its repair, then the cancel again" err_has "hint: run 'ait contract commit -m <why>', then rerun this command"

done_testing
