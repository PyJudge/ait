#!/bin/sh
# Regressions for the review of the version 2 conversion; each case was reproduced first
. "$(dirname "$0")/lib.sh"
# line N of FILE becomes TEXT
setl() { f=$1 l=$2; shift 2; awk -v n="$l" -v t="$*" 'NR==n{print t; next}1' "$f" >"$f.tmp" && cat "$f.tmp" >"$f" && rm "$f.tmp"; }
not() { ! "$@"; }
# TEXT A comes before TEXT B in the last run's stderr
err_before() {
	a=$(grep -bo -F -- "$1" "$T/err" | head -1 | cut -d: -f1)
	b=$(grep -bo -F -- "$2" "$T/err" | head -1 | cut -d: -f1)
	[ -n "$a" ] && [ -n "$b" ] && [ "$a" -lt "$b" ]
}
# the last run exited CODE and its stderr has TEXT
fails() { [ "$rc" = "$1" ] && grep -qF -- "$2" "$T/err"; }
prep() {
	setup
	A=$(ait session open --agent a)
	prompt_defs
	git add -A && tick && git commit -q --no-verify -m defs
}

# 7.1.3-7: a side is tried as its hint restores it, the spec files its version declares included
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>"$T/err"; K=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
H=$(git rev-parse HEAD)
git branch side; git switch -q side; setl src/order.c 1 o1x; tick; git commit -qam side; git switch -q master
tick; git merge -q --no-commit --no-ff side
git rm -q specs/p.yaml
run ait hook pre-commit
check "a merge that lost a spec file: E_INCOMPLETE" fails 1 "error: spec files do not match the contract:"
check "the hint restores the record and its spec file from HEAD" err_has "restore '.ait/$K' 'specs/p.yaml' as in '$H' and stage the result, then run 'git commit --allow-empty --no-edit --cleanup=strip'"
check "no 'no side passes'" not err_has "no side passes"
git checkout -q HEAD -- specs/p.yaml
tick; expect_ok "as the hint says, the merge commits" git commit --allow-empty --no-edit --cleanup=strip

# 5.8 E_GUARD: git commit -a commits a temporary index, so unstaging alone does not help
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >/dev/null 2>&1
echo 'body: updated' > specs/p.yaml; git add specs/p.yaml; setl src/order.c 1 o1x
tick; run git commit -qam combined
check "git commit -a: E_GUARD" fails 1 "error: contract files cannot be changed by a plain commit: 'specs/p.yaml'"
check "the hint is the -a form, after unstaging what this index staged" err_has "hint: unstage 'specs/p.yaml' first; commit without -a or paths: stage the code ('src/order.c') and commit it, then commit them through ait: 'ait contract add specs/p.yaml' and 'ait contract commit -m <why>'"
check "no 'and commit the rest'" not err_has "and commit the rest"
git add src/order.c
tick; run git commit -qam all-staged
check "git commit -a with everything staged is still -a" err_has "hint: unstage 'specs/p.yaml' first; commit without -a or paths:"
tick; run git commit -qm plain
check "a plain commit runs on this index: unstage and commit the rest" err_has "hint: unstage 'specs/p.yaml' and commit the rest;"

# 7.1.3-5: versions that differ only in their pins (1.5e) are no conflict; the kept body keeps its prev
prep
tick; ait offer src/stock.c specs/p.yaml --force -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
git branch side
git switch -q side; setl src/stock.c 9 s9x; tick; git commit -qam side; tick; ait amend "$O" --force -m side >/dev/null 2>&1
git switch -q master; setl src/stock.c 1 s1x; tick; git commit -qam ours; tick; ait amend "$O" --force -m ours >/dev/null 2>&1
PREV=$(git show HEAD:.ait/$O | sed -n 's/^prev //p')
tick; git merge -q --no-commit --no-ff side 2>/dev/null
git checkout -q HEAD -- .ait/$O; git add .ait/$O
tick; run git commit --no-edit
check "the merge commits" test $rc = 0
check "O's prev is ours' prev, one line" test "$(git show HEAD:.ait/$O | sed -n 's/^prev //p')" = "$PREV"

# 5.8 E_INCOMPLETE: a broken bundle at HEAD is mended by the gate alone, which pins it again (6.2)
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
echo 'body: changed' > specs/p.yaml
tick; git commit -qam raw --no-verify; run ait hook post-commit
check "post-commit warns of the broken bundle" err_has "warning: spec files do not match the contract: '$O'"
check "its hint is the contract commit alone" err_has "hint: run 'ait contract commit -m <why>'"
run ait contract pull "$O"
check "pull: E_INCOMPLETE at HEAD" fails 1 "error: spec files do not match the contract: '$O'"
check "the same hint" err_has "hint: run 'ait contract commit -m <why>'"
echo x >> src/stock.c; git add src/stock.c
run ait fulfill "$O" -m f
check "fulfill sets the staged code aside around it" err_has "hint: unstage 'src/stock.c' first, then 'ait contract commit -m <why>', stage 'src/stock.c' again, then rerun this command"
git reset -q src/stock.c
tick; expect_ok "as the hint says, the gate pins it again" ait contract commit -m pin
check "the bundle is whole" test "$(git show HEAD:.ait/$O | grep -c "$(git rev-parse HEAD:specs/p.yaml)")" = 1

# 5.9a: pre-push makes no commit, so a merge in progress goes on after it and comes first in its hint
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >/dev/null 2>&1
echo 'body: changed' > specs/p.yaml
tick; git commit -qam raw --no-verify
git branch side HEAD~2; git switch -q side; echo y > src/order.c; tick; git commit -qam side; git switch -q master
echo z > src/order.c; tick; git commit -q --no-verify -am ours
tick; git merge -q side >/dev/null 2>&1
run git push -q origin master
check "pre-push during a merge: E_INCOMPLETE" err_has "error: spec files do not match the contract:"
check "the merge is finished first" err_has "hint: first finish or abort the git merge in progress ('git status')"

# 7.1.8: only a current version is reopened into a new one (7.3.2), so only it gets the hint
prep
tick; ait future demand src/order.c src/stock.c specs/w.yaml -m d >"$T/out" 2>&1; D=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
tick; ait future offer src/stock.c specs/p.yaml as o undertake "$D" o -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \(O[^ ]*\).*/\1/p' "$T/out")
tick; ait release "$D" "$O" -m r >/dev/null 2>&1; tick; ait undertake "$D" "$O" -m u >/dev/null 2>&1
tick; run git revert --no-edit HEAD
check "a link put back: the warning" err_has "warning: this commit put 'undertake/$D-$O' back to an older version; only this branch has it"
check "no amend hint for a link" not err_has "'ait future amend undertake/"
tick; ait future amend "$O" - specs/low.yaml -m a >/dev/null 2>&1
tick; run git revert --no-edit HEAD
check "a future version put back: the warning" err_has "warning: this commit put '$O' back to an older version"
check "no amend hint, which would be noop" not err_has "'ait future amend $O -m <why>'"
tick; ait fulfill "$O" --force -m f >/dev/null 2>&1
tick; run git revert --no-edit HEAD
check "a current version put back: the hint" err_has "hint: to take it back for others too, reopen it with ait once this commit is reverted: 'ait future amend $O -m <why>'"

# 5.10: every request judges the records it writes from their base version, GOOD's when HEAD's is an accident
prep
tick; ait offer src/stock.c specs/p.yaml --force -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out"); GB=$(git rev-parse HEAD:.ait/$O)
grep -v '^check ' .ait/$O > "$T/rec" && cat "$T/rec" > .ait/$O
tick; git commit -qam hand --no-verify
tick; run ait future amend "$O" -m reopen
check "a helper reopens an accident from GOOD's version" test $rc = 0
check "its prev is GOOD's blob" test "$(git show HEAD:.ait/$O | sed -n 's/^prev //p')" = "$GB"
run ait todo
check "the accident is gone" not err_has "changed outside ait's rules"

prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
tick; ait future cancel "$O" -m c >/dev/null 2>&1; G=$(git rev-parse HEAD); CB=$(git rev-parse HEAD:.ait/$O)
sed -e 's/^state cancelled/state future/' -e "s/^prev .*/prev $CB/" .ait/$O > "$T/rec" && cat "$T/rec" > .ait/$O
tick; git commit -qam revive --no-verify
echo x >> src/stock.c; git add src/stock.c
tick; run ait fulfill "$O" --force -m f
check "fulfill of a contract revived by hand: E_STATE from GOOD's cancelled version" fails 1 "error: not allowed: '$O' cannot go from cancelled to current"
git reset -q src/stock.c; git checkout -q src/stock.c
git checkout "$G" -- .ait/$O
tick; run ait contract commit -m fix
check "restoring GOOD's ended version commits (no transition)" test $rc = 0
check "the record is GOOD's again, prev GOOD's blob" test "$(git show HEAD:.ait/$O | sed -n 's/^state //p;s/^prev //p' | tr '\n' ' ')" = "cancelled $CB "

# 7.7.6: a HEAD accident that refuses commit is the report's 'invalid' line, and the commit is not ready
prep
tick; ait offer src/stock.c specs/p.yaml --force -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
tick; ait offer src/order.c specs/w.yaml --force -m p >/dev/null 2>&1
grep -v '^check ' .ait/$O > "$T/rec" && cat "$T/rec" > .ait/$O
tick; git commit -qam hand --no-verify
echo x >> src/order.c; git add src/order.c
run ait commit -m code
check "E_ACCIDENT" fails 1 "error: '$O' at HEAD was changed outside ait's rules"
check "the invalid line" grep -qx "invalid accident '$O' at HEAD was changed outside ait's rules" "$T/out"
check "ready no" grep -qx "ready no" "$T/out"

# 8.1: contract commit's new records have their IDs already, so each gets its own tasks
prep
O1=$(ait contract new offer | sed -n 's#^path .ait/##p')
printf 'role offer\nstate current\nprovider src/stock.c\nfile specs/p.yaml\n' > .ait/$O1
O2=$(ait contract new offer | sed -n 's#^path .ait/##p')
printf 'role offer\nstate current\nprovider src/stock.c\nfile specs/p.yaml\n' > .ait/$O2
ait contract add .ait/$O1 .ait/$O2 >/dev/null
run ait contract commit -m c
check "a task for each new record, by its ID" test "$(grep '^task ' "$T/out" | sort | tr '\n' ' ')" = "$(printf 'task %s:specs/p.yaml prompt\n' $O1 $O2 | sort | tr '\n' ' ')"
tick; with_r pass ait contract commit -m c
check "both are checked" sh -c "[ $rc = 0 ] && git log -1 --format=%B | grep -q 'task: \"\\?$O1:specs/p.yaml' && git log -1 --format=%B | grep -q '$O2:specs/p.yaml'"

# 5.12: contract commit reads its message and result files as every writing command does
prep
O=$(ait contract new offer | sed -n 's#^path .ait/##p')
printf 'role offer\nstate future\nprovider src/stock.c\nfile specs/p.yaml\n' > .ait/$O
ait contract add .ait/$O >/dev/null
run ait contract commit -r "$T/none" -m x
check "a missing -r file: E_READ" fails 128 "fatal: could not read '$T/none'"
echo 'from a file' > "$T/msg"
tick; run ait contract commit -F "$T/msg"
check "-F gives the message" sh -c "[ $rc = 0 ] && [ \"\$(git log -1 --format=%s)\" = '[CONTRACT] from a file' ]"

# 7.5.2: only the contracts PATH touches are expanded; a frame left unfilled is no one's concern but its own
prep
O=$(ait contract new offer | sed -n 's#^path .ait/##p')
printf 'role offer\nstate future\nprovider src/stock.c\nfile specs/p.yaml\n' > .ait/$O
D=$(ait contract new demand | sed -n 's#^path .ait/##p')
run ait contract add .ait/$O
check "contract add of a filled record beside an unfilled frame" sh -c "[ $rc = 0 ] && grep -qx 'staged .ait/$O' '$T/out'"
run ait contract add .ait/$D
check "the frame itself: its missing definition is a missing line" grep -qx "missing PATH" "$T/out"
check "with 5.8's hint" err_has "hint: create 'PATH', or stop declaring it in '.ait/$D'"

# H9: a pull that picks nothing is no choice, so the hint includes the changes
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
git branch b
tick; ait amend "$O" src/stock.c:1-5 - -m a >/dev/null 2>&1
git switch -q b
tick; run ait amend "$O" src/stock.c:6-9 - -m b
check "H9 warns" err_has "warning: '$O' has changes not in HEAD:"
check "H9 hint: to include them" err_has "hint: to include them, run 'ait contract pull $O'"
check "no pick one" not err_has "pick one"

# H4: another ref's link to the same demand came first, so mine is told (5.11)
prep
tick; ait future demand src/order.c src/stock.c specs/w.yaml -m d >"$T/out" 2>&1; D=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
git worktree add -q "$T/wb" -b b
cd "$T/wb"; ait session open --agent b >/dev/null 2>&1
tick; ait future offer src/stock.c specs/p.yaml as o undertake "$D" o -m b >"$T/b.out" 2>&1
cd - >/dev/null
OB=$(sed -n 's/^new \([^ ]*\) o$/\1/p' "$T/b.out")
tick; run ait future offer src/stock.c specs/low.yaml as o undertake "$D" o -m a
check "H4 names b's offer on its branch" err_has "warning: '$D' is also undertaken by '$OB'"

# 7.7.5: the check line says how the record was checked; reported PINS stay reported (2.2.2)
prep
tick; with_r pass ait demand src/order.c src/stock.c specs/w.yaml as d -m d; D=$(sed -n 's/^new \([^ ]*\) d$/\1/p' "$T/out")
tick; ait future offer src/stock.c specs/p.yaml as o undertake "$D" o -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\) o$/\1/p' "$T/out")
echo x >> src/order.c; git add src/order.c
tick; run ait fulfill "$O" --force -m f
check "D is reported in its record" sh -c "git show HEAD:.ait/$D | grep -q '^check reported '"
check "and in the output" grep -qx "check $D reported" "$T/out"

# 3.9 rule 4: todo's range arguments select by intersecting line ranges
prep
tick; ait future offer src/stock.c:1-10 specs/p.yaml -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
run ait todo src/stock.c:40-50
check "a range apart from O's does not select it" not grep -q "^id $O" "$T/out"
run ait todo src/stock.c:5-12
check "an intersecting range does" grep -qx "id $O" "$T/out"

# 5.8 E_SCOPE: a path HEAD lacks gets the command that names it new:, or the code first
prep
run ait future offer src/stock.c,src/nope.c specs/p.yaml -m o
check "a future provider: the same command with new:" err_has "hint: if it does not exist yet, run 'ait future offer src/stock.c,new:src/nope.c specs/p.yaml -m <why>'"
tick; expect_ok "as the hint says" ait future offer src/stock.c,new:src/nope.c specs/p.yaml -m o
run ait offer src/gone.c specs/p.yaml --force -m o
check "a current one, absent: make it future with new:" err_has "hint: if it does not exist yet, make it future: 'ait future offer new:src/gone.c specs/p.yaml -m <why>'"
echo x > src/gone.c
run ait offer src/gone.c specs/p.yaml --force -m o
check "a current one in the work tree: the code first" err_has "hint: commit code that meets it first: stage 'src/gone.c' and run 'ait commit -m <why>', then rerun this command"

# H8: what comes in unverified names where it became so in the judged tree's history, the incoming side's
prep
tick; ait demand src/order.c src/stock.c specs/w.yaml --force -m d >"$T/out" 2>&1; D=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
git branch b; git switch -q b; git rm -q src/stock.c; tick; git commit -qm gone --no-verify; C=$(git rev-parse HEAD)
git switch -q master; echo z >> src/order.c; tick; git commit -qam other --no-verify
tick; run git merge --no-edit b
check "it comes in unverified" err_has "warning: '$D' comes in unverified"
check "H8 names the incoming commit" err_has "hint: '$D' has no code under its scope (unverified since '$C');"

# 5.8 E_NEEDS_RESULTS: check, --force, then the reopen hint; making it a promise is E_CODE_REQUIRED's and E_CHECK_FAILED's
prep
tick; run ait offer src/stock.c specs/p.yaml -m o
check "E_NEEDS_RESULTS" fails 1 "error: check results required: 1 task(s)"
check "no promise hint" not err_has "make it future"

# 5.8 E_CODE_REQUIRED: each contract's hints in turn, not each kind across contracts
prep
tick; ait future offer new:src/a.c specs/p.yaml as a future offer new:src/b.c specs/p.yaml as b -m o >"$T/out" 2>&1
OA=$(sed -n 's/^new \([^ ]*\) a$/\1/p' "$T/out"); OB=$(sed -n 's/^new \([^ ]*\) b$/\1/p' "$T/out")
run ait fulfill "$OA" "$OB" -m f
check "E_CODE_REQUIRED" fails 1 "error: no code under the contract's scope:"
if [ "$OA" \< "$OB" ]; then K1=$OA P2=src/b.c; else K1=$OB P2=src/a.c; fi
check "the first contract's cancel comes before the second one's write (ID order)" err_before "ait future cancel $K1 -m <why>" "write '$P2'"

# 2.2.5, 5.10: a helper writing over a hand-changed creator keeps GOOD's, so it writes no accident (2.3.3)
prep
tick; ait offer src/stock.c specs/p.yaml --force -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out"); B0=$(git rev-parse HEAD:.ait/$O)
sed -e 's/^creator a /creator b /' -e "s/^prev .*/prev $B0/" .ait/$O > "$T/rec" && cat "$T/rec" > .ait/$O
tick; git commit -qam hand --no-verify
tick; run ait future amend "$O" -m r
check "the helper writes GOOD's creator" sh -c "[ $rc = 0 ] && git show HEAD:.ait/$O | grep -q '^creator a '"
run ait todo
check "and no accident" not err_has "changed outside ait's rules"
done_testing
