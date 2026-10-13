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
check "the hint is the -a form" err_has "hint: commit without -a or paths: stage the code ('src/order.c') and commit it, then commit them through ait: 'ait contract add specs/p.yaml' and 'ait contract commit -m <why>'"
check "no 'and commit the rest'" not err_has "and commit the rest"
git add src/order.c
tick; run git commit -qam all-staged
check "git commit -a with everything staged is still -a" err_has "hint: commit without -a or paths:"
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
check "fulfill: the repair, then the fulfill again" err_has "hint: run 'ait contract commit -m <why>', then rerun this command"
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

# 7.1.8 (user decision 47): once the commit is reverted, the commands that bring each kind back to the older version
prep
tick; ait future demand src/order.c src/stock.c specs/w.yaml -m d >"$T/out" 2>&1; D=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
tick; ait future offer src/stock.c specs/p.yaml as o undertake "$D" o -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \(O[^ ]*\).*/\1/p' "$T/out")
tick; ait release "$D" "$O" -m r >/dev/null 2>&1; tick; ait undertake "$D" "$O" -m u >/dev/null 2>&1
tick; run git revert --no-edit HEAD
check "a link put back: the warning" err_has "warning: this commit put 'undertake/$D-$O' back to an older version; only this branch has it"
check "a link: release it" err_has "hint: to take it back for others too, once this commit is reverted, run 'ait release $D $O -m <why>'"
tick; ait future amend "$O" - specs/low.yaml -m a >/dev/null 2>&1
tick; run git revert --no-edit HEAD
check "a future version: amend what differs, the rest kept" err_has "hint: to take it back for others too, once this commit is reverted, run 'ait future amend $O - specs/p.yaml -m <why>'"
tick; ait offer src/order.c specs/w.yaml --force -m k >"$T/out" 2>&1; K=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
tick; ait future amend "$K" -m r >/dev/null 2>&1
tick; run git revert --no-edit HEAD
check "a current version a reopen left: fulfill it" err_has "hint: to take it back for others too, once this commit is reverted, run 'ait fulfill $K -m <why>'"
tick; ait future cancel "$O" -m c >/dev/null 2>&1
tick; run git revert --no-edit HEAD
check "an ended version stays ended: no command" not err_has "to take it back"

# 5.10 (user decision 55): a helper stops on HEAD's accident first, as git's commands stop on unmerged entries; the repair, then the helper again
prep
tick; ait offer src/stock.c specs/p.yaml --force -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out"); G=$(git rev-parse HEAD)
grep -v '^check ' .ait/$O > "$T/rec" && cat "$T/rec" > .ait/$O
tick; git commit -qam hand --no-verify
tick; run ait future amend "$O" -m reopen
check "a helper stops on HEAD's accident before anything else" sh -c "[ $rc = 1 ] && grep -q \"^error: '$O' at HEAD was changed outside ait's rules\" '$T/err'"
check "with the repair and a rerun" err_has "hint: restore '.ait/$O' as in '$G', then 'ait contract commit -m <why>', then rerun this command"
git checkout -q "$G" -- ".ait/$O"; tick; ait contract commit -m mend >/dev/null 2>&1
tick; run ait future amend "$O" -m reopen
check "then the helper runs" test $rc = 0
run ait todo
check "the accident is gone" not err_has "changed outside ait's rules"

prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
tick; ait future cancel "$O" -m c >/dev/null 2>&1; G=$(git rev-parse HEAD); CB=$(git rev-parse HEAD:.ait/$O)
sed -e 's/^state cancelled/state future/' -e "s/^prev .*/prev $CB/" .ait/$O > "$T/rec" && cat "$T/rec" > .ait/$O
tick; git commit -qam revive --no-verify
echo x >> src/stock.c; git add src/stock.c
tick; run ait fulfill "$O" --force -m f
check "fulfill of a contract revived by hand stops on HEAD's accident first (user decision 55)" fails 1 "error: '$O' at HEAD was changed outside ait's rules"
check "hint 1: HEAD's accident is restored first, then the fulfill is run again" err_has "hint: restore '.ait/$O' as in '$G', then 'ait contract commit -m <why>', then rerun this command"
git reset -q src/stock.c; git checkout -q src/stock.c
git checkout "$G" -- .ait/$O
tick; run ait contract commit -m fix
check "restoring GOOD's ended version commits (no transition)" test $rc = 0
check "the record is GOOD's again, prev GOOD's blob" test "$(git show HEAD:.ait/$O | sed -n 's/^state //p;s/^prev //p' | tr '\n' ' ')" = "cancelled $CB "

# 5.10 (user decision 55): a commit with something to check stops on HEAD's accident before its report
prep
tick; ait offer src/stock.c specs/p.yaml --force -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
tick; ait offer src/order.c specs/w.yaml --force -m p >/dev/null 2>&1
grep -v '^check ' .ait/$O > "$T/rec" && cat "$T/rec" > .ait/$O
tick; git commit -qam hand --no-verify
echo x >> src/order.c; git add src/order.c
run ait commit -m code
check "E_ACCIDENT" fails 1 "error: '$O' at HEAD was changed outside ait's rules"
check "before any report" sh -c "! grep -q '^ready ' '$T/out'"
check "with the repair and a rerun" err_has "then 'ait contract commit -m <why>', then rerun this command"

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
check "with 5.8's hint" err_has "hint: create it or stop declaring it in '.ait/$D'"

# H9 is judged on the commit a command makes, and before a refusal's error on HEAD (5.9a)
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
git branch b
tick; ait amend "$O" src/stock.c:1-5 - -m a >/dev/null 2>&1; M=$(git rev-parse HEAD)
git switch -q b
tick; run ait fulfill "$O" -m f
check "a refusal: H9 on HEAD, which master's version includes" err_has "hint: to include them, run 'ait contract pull $O'"
check "before the error" err_before "has changes not in HEAD" "error:"
tick; run ait amend "$O" src/stock.c:6-9 - -m b
check "a commit: H9 on it, where the versions have diverged" sh -c "grep -q '^hint: pick one: ' '$T/err' && grep -qF \"'ait contract pull $O=$M -m <why>'\" '$T/err' && grep -qF \"'ait contract pull $O=HEAD -m <why>'\" '$T/err'"
tick; expect_ok "the pick the hint gives runs" ait contract pull "$O=$M" -m pick

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

# 2.2.5, 5.10 (user decision 55): a hand-changed creator stops a helper first; GOOD's record comes back, then the helper runs
prep
tick; ait offer src/stock.c specs/p.yaml --force -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out"); B0=$(git rev-parse HEAD:.ait/$O); G=$(git rev-parse HEAD)
sed -e 's/^creator a /creator b /' -e "s/^prev .*/prev $B0/" .ait/$O > "$T/rec" && cat "$T/rec" > .ait/$O
tick; git commit -qam hand --no-verify
tick; run ait future amend "$O" -m r
check "the helper stops on HEAD's accident" sh -c "[ $rc = 1 ] && grep -q \"hint: restore '.ait/$O' as in '$G', then 'ait contract commit -m <why>', then rerun this command\" '$T/err'"
git checkout -q "$G" -- ".ait/$O"; tick; ait contract commit -m mend >/dev/null 2>&1
tick; run ait future amend "$O" -m r
check "then it writes GOOD's creator" sh -c "[ $rc = 0 ] && git show HEAD:.ait/$O | grep -q '^creator a '"
run ait todo
check "and no accident" not err_has "changed outside ait's rules"

# 5.11: refs are named as git shortens them, so a branch named like a remote ref stays a ref of its own
prep
git switch -q -c origin/hidden
tick; ait future offer src/order.c specs/w.yaml -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
git switch -q master; git update-ref refs/remotes/origin/hidden HEAD
tick; run ait contract pull "$O"
check "the branch's contract is found and pulled" sh -c "[ $rc = 0 ] && git cat-file -e HEAD:.ait/$O"

# H4: links one request makes share its commit, so the smaller O came first and is not told
prep
tick; ait future demand src/order.c src/stock.c specs/w.yaml -m d >"$T/out" 2>&1; D=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>&1; O1=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
B=$(ait session open --agent b 2>/dev/null)
tick; AIT_SESSION="$B" ait future offer src/stock.c specs/low.yaml -m o2 >"$T/out" 2>&1; O2=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
tick; run ait undertake "$D" "$O1" undertake "$D" "$O2" -m both
if [ "$O1" \< "$O2" ]; then
	check "my offer, the smaller, is not told" not err_has "is also undertaken"
else
	check "my offer, the larger, is told" err_has "warning: '$D' is also undertaken by '$O2'"
fi

# 5.9 d: every promise or reopen hint takes along the current offers that undertake the demand, and runs as given
hinted() { sed -n "s/^hint: .*$1: '\(ait .*\)'\$/\1/p" "$T/err" | head -1 | sed 's/<why>/x/'; }
prep
tick; ait future demand src/order.c src/stock.c specs/w.yaml -m d >"$T/out" 2>&1; D=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
tick; ait future offer src/stock.c specs/p.yaml as o undertake "$D" o -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \(O[^ ]*\) o$/\1/p' "$T/out")
echo x >> src/stock.c; git add src/stock.c; tick; ait fulfill "$O" --force -m f >/dev/null 2>&1
H=$(git rev-parse HEAD)
tick; run ait amend "$D" - src/nope.c - -m a
CMD=$(hinted "make it future")
check "E_SCOPE's promise reopens O too" sh -c "echo \"$CMD\" | grep -q 'future amend $O'"
tick; expect_ok "and runs" sh -c "$CMD"
git reset -q --hard "$H"
# the fulfill that made D and O current is reverted: D goes back to future with its offer
tick; run git revert --no-edit HEAD
CMD=$(sed -n "s/^hint: to take it back for others too, once this commit is reverted, run '\(ait future amend $D.*\)'\$/\1/p" "$T/err" | sed 's/<why>/x/')
check "7.1.8's reopen takes O along" sh -c "echo \"$CMD\" | grep -q 'future amend $O'"
tick; git revert --no-edit HEAD >/dev/null 2>&1
tick; expect_ok "and runs once the revert is reverted" sh -c "$CMD"
git reset -q --hard "$H"
tick; run ait demand src/order.c src/nope.c specs/w.yaml as d undertake d "$O" --force -m d
CMD=$(hinted "make it future")
check "a new demand's promise takes along the current offer a later clause gives it to" sh -c "echo \"$CMD\" | grep -q 'future amend $O'"
tick; expect_ok "and runs" sh -c "$CMD"

# choice 161: a drop from elsewhere is only cancelled here; making, moving and fulfilling one come with future drop
prep
tick; ait offer src/stock.c specs/p.yaml --force -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
tick; ait offer src/order.c specs/w.yaml --force -m o2 >"$T/out" 2>&1; O2=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
X=Xaaaaaaaaaaaa
printf 'role drop\nstate future\ntarget %s\ncreator a %s\nprev -\n' "$O" "$A" > .ait/$X
git add .ait/$X; tick; git commit -qm drop --no-verify
sed "s/^target .*/target $O2/" .ait/$X > "$T/rec" && cat "$T/rec" > .ait/$X; ait contract add .ait/$X >/dev/null
tick; run ait contract commit -m move
check "contract commit moving a drop: not supported" fails 128 "fatal: not supported: 'drop'"
git checkout -q HEAD -- .ait/$X
sed "s/^state .*/state fulfilled/" .ait/$X > "$T/rec" && cat "$T/rec" > .ait/$X; ait contract add .ait/$X >/dev/null
tick; run ait contract commit -m fulfil
check "contract commit fulfilling a drop: not supported, with no hint to fulfill it" sh -c "[ $rc = 128 ] && ! grep -q 'ait fulfill' '$T/err'"
git checkout -q HEAD -- .ait/$X
tick; run ait fulfill "$X" -m f
check "fulfill of a drop: not supported" fails 128 "fatal: not supported: 'drop'"
tick; expect_ok "future cancel of a drop" ait future cancel "$X" -m c

# 5.9a hint 1: a record made current by hand is HEAD's accident; its repair comes before 'already current'
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out"); G=$(git rev-parse HEAD)
sed 's/^state future/state current/' .ait/$O > "$T/rec" && cat "$T/rec" > .ait/$O
tick; git commit -qam hand --no-verify
echo x >> src/stock.c; git add src/stock.c
tick; run ait fulfill "$O" --force -m f
check "the restore, not the amend that E_ACCIDENT would refuse" sh -c "grep -qF \"hint: restore '.ait/$O' as in '$G'\" '$T/err' && ! grep -q 'ait amend $O' '$T/err'"

# 5.9a: a hint's scopes are input valid where its command runs: new:PATH is a whole file, and old ranges are mapped
prep
run ait future offer src/nope.c:1-5 specs/p.yaml -m o
CMD=$(sed -n "s/^hint: if it does not exist yet, run '\\(.*\\)'\$/\\1/p" "$T/err" | sed 's/<why>/x/')
check "E_SCOPE on a range: new: names the file" test "$CMD" = "ait future offer new:src/nope.c specs/p.yaml -m x"
tick; expect_ok "and runs" sh -c "$CMD"
tick; ait future offer new:src/x.c,src/stock.c:20-30 specs/p.yaml -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
tick; ait future cancel "$O" -m c >/dev/null 2>&1
echo new > src/x.c; awk 'NR>15' src/stock.c > "$T/s" && cat "$T/s" > src/stock.c
git add -A; tick; git commit -qm code
tick; run ait amend "$O" - specs/low.yaml -m a
CMD=$(hinted "to need it again, make a new contract")
check "hint 3 maps the range and names the new file as it is now" test "$CMD" = "ait future offer src/stock.c:5-15,src/x.c specs/p.yaml -m x"
tick; expect_ok "and runs" sh -c "$CMD"

# 5.8 E_GUARD: with only contract paths in the commit, it goes through ait whatever index git gave the hook
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >/dev/null 2>&1
echo 'body: changed' > specs/p.yaml
tick; run git commit -qam a
check "-a with only a contract path: through ait, no empty code list" sh -c "grep -qF \"hint: commit them through ait: 'ait contract add specs/p.yaml' and 'ait contract commit -m <why>'\" '$T/err' && ! grep -q 'stage the code ()' '$T/err'"

# 5.10: a current contract's broken bundle is pinned again by the gate, with nothing to restore
prep
tick; ait offer src/stock.c specs/p.yaml --force -m o >/dev/null 2>&1
echo 'body: changed' > specs/p.yaml
tick; git commit -qam raw --no-verify; run ait hook post-commit
check "a current one's repair: contract commit alone" sh -c "grep -qx \"hint: run 'ait contract commit -m <why>'\" '$T/err'"

# 7.1.3: a revert starts from I's first parent, named as a revision of its own (the words of a git call are whole)
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
git switch -q -c side; tick; ait future amend "$O" src/stock.c:1-5 - -m side >/dev/null 2>&1; IN=$(git rev-parse HEAD)
git switch -q master; tick; ait future amend "$O" src/stock.c:10-15 - -m ours >/dev/null 2>&1
tick; git revert --no-edit "$IN" >/dev/null 2>&1
git checkout -q --ours .ait/$O; git add .ait/$O
tick; expect_ok "the revert's finishing commit" git commit -q --allow-empty --no-edit --cleanup=strip

# 5.10 (user decision 55): a record no rule made, with no GOOD version, stops a helper; it goes, then the helper runs
prep
B=$(ait session open --agent b 2>/dev/null)
printf 'role offer\nstate current\nprovider src/stock.c %s\nfile specs/p.yaml 100644 %s def\ncreator b %s\nprev -\n' "$(git rev-parse HEAD:src/stock.c)" "$(git rev-parse HEAD:specs/p.yaml)" "$B" > .ait/O111111111111
git add .ait/O111111111111; tick; git commit -qm raw --no-verify
tick; run ait future amend O111111111111 -m reopen
check "the helper stops on it, and the hint removes it" sh -c "[ $rc = 1 ] && grep -q \"hint: remove '.ait/O111111111111', then 'ait contract commit -m <why>', then rerun this command\" '$T/err'"

# H9: two latest versions of one contract elsewhere are one contract to pull, and do not refuse the command
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
git branch side1; git branch side2
git switch -q side1; tick; ait future amend "$O" src/stock.c:1-5 - -m s1 >/dev/null 2>&1
git switch -q side2; tick; ait future amend "$O" src/stock.c:10-15 - -m s2 >/dev/null 2>&1
git switch -q master
tick; run ait future amend "$O" src/stock.c:20-25 - -m m
check "the amend runs with H9's warnings" sh -c "[ $rc = 0 ] && [ \$(grep -c 'has changes not in HEAD' '$T/err') = 2 ]"

# 5.10 (user decision 55): an ended contract revived by hand stops a helper; GOOD's version comes back with its file lines
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
tick; ait future cancel "$O" -m c >/dev/null 2>&1; CB=$(git rev-parse HEAD:.ait/$O); G=$(git rev-parse HEAD)
sed -e 's/^state cancelled/state future/' -e "s/^prev .*/prev $CB/" -e "s#^file specs/p.yaml .*#file specs/p.yaml 100644 $(git rev-parse HEAD:specs/w.yaml) def#" .ait/$O > "$T/rec" && cat "$T/rec" > .ait/$O
tick; git commit -qam revive --no-verify
tick; run ait future cancel "$O" -m c
check "the cancel stops on it, and the hint restores GOOD's version" sh -c "[ $rc = 1 ] && grep -q \"hint: restore '.ait/$O' as in '$G', then 'ait contract commit -m <why>', then rerun this command\" '$T/err'"
git checkout -q "$G" -- ".ait/$O"; tick; ait contract commit -m mend >/dev/null 2>&1
check "with GOOD's file line" test "$(git show HEAD:.ait/$O | grep '^file ')" = "$(git show "$CB" | grep '^file ')"

# 7.5.2: a definition gone from the work tree declares nothing there
prep
printf 'body: test\ninputs: [specs/data]\n' > specs/p.yaml; echo input > specs/data; git add -A; tick; git commit -qm data --no-verify
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
rm specs/p.yaml specs/data
run ait contract add .ait/$O
check "only the definition is missing, not its old input" sh -c "grep -qx 'missing specs/p.yaml' '$T/out' && ! grep -q 'specs/data' '$T/out'"

# 5.8 E_SCOPE: the hint is drawn from the parsed clauses, so a consumer file named like a verb stays a consumer
prep
cp src/order.c amend; git add amend; tick; git commit -qm amend --no-verify
run ait demand amend src/nope.c specs/p.yaml --force -m d
CMD=$(hinted "make it future")
check "the verb is where the clause has it" test "$CMD" = "ait future demand amend new:src/nope.c specs/p.yaml -m x"

# 7.1.1-5 (user decision 46): an init file HEAD has that is not right stops ait init with 2.1.2a's E_FORMAT
prep
printf 'version 1\n' > .ait/init; tick; git commit -qam v1 --no-verify
tick; run ait init
check "E_FORMAT, not a rewrite" sh -c "[ $rc = 128 ] && [ \"\$(git show HEAD:.ait/init)\" = 'version 1' ]"
check "5.10's hint" err_has "hint: rewrite '.ait/init' as the single line 'version 2' with mode 100644, then 'ait contract add .ait/init' and 'ait contract commit -m <why>'"

# 6.1 before 6.2 (user decision 48): contract commit says the code is mixed in before it judges the records
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
sed 's/^creator a /creator b /' .ait/$O > "$T/rec" && cat "$T/rec" > .ait/$O; git add .ait/$O
echo x >> src/order.c; git add src/order.c
run ait contract commit -m x
check "E_MIXED first" fails 1 "error: contract changes and code changes are mixed: 'src/order.c'"

# user decision 44: contract commit's noop tells to restore what the index spells otherwise than HEAD
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
sed 's/^prev .*/prev junk/' .ait/$O > "$T/rec" && cat "$T/rec" > .ait/$O; ait contract add .ait/$O >/dev/null
tick; run ait contract commit -m x
check "noop with the restore hint" sh -c "grep -qx noop '$T/out' && grep -qF \"hint: restore '.ait/$O' as in 'HEAD'\" '$T/err'"

# 7.8.1 (user decision 45): todo reads an unmerged path as HEAD has it, and says so
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
git branch side; git switch -q side; echo s > src/order.c; tick; git commit -qam side --no-verify
git switch -q master; echo m > src/order.c; tick; git commit -qam m --no-verify
tick; git merge side >/dev/null 2>&1
run ait todo
check "todo runs in a conflicted merge" sh -c "[ $rc = 0 ] && grep -qx 'id $O' '$T/out'"
check "with the unmerged path named" err_has "warning: 'src/order.c' is unmerged; it is read as HEAD has it"

# 5.5-4, decision 45: todo reads a conflicted index from a copy of it, so a file added with -N stays unstaged
prep
tick; ait future offer new:src/z.c specs/p.yaml -m z >/dev/null 2>&1
echo z > src/z.c; git add -N src/z.c
B=$(git rev-parse HEAD:src/order.c); X=$(echo theirs | git hash-object -w --stdin)
printf '0 0000000000000000000000000000000000000000\tsrc/order.c\n100644 %s 1\tsrc/order.c\n100644 %s 2\tsrc/order.c\n100644 %s 3\tsrc/order.c\n' "$B" "$B" "$X" | git update-index --index-info
run ait todo
check "the -N file is no staged code" sh -c "grep -qx 'provider new:src/z.c' '$T/out'"
# 5.8 E_SCOPE's same command: one new:PATH for every scope of that file, the offers of 5.9 d, and the tasks of a current one
newin() { sed -n "s/^hint: if it does not exist yet, run '\(ait .*\)'\$/\1/p" "$T/err" | head -1 | sed 's/<why>/x/'; }
prep
tick; ait future demand src/order.c src/stock.c specs/w.yaml -m d >"$T/out" 2>&1; D=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
tick; ait future offer src/stock.c specs/p.yaml as o undertake "$D" o -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \(O[^ ]*\) o$/\1/p' "$T/out")
echo x >> src/stock.c; git add src/stock.c; tick; ait fulfill "$O" --force -m f >/dev/null 2>&1
H=$(git rev-parse HEAD)
run ait future amend "$D" - src/nope.c - -m x
CMD=$(newin)
check "reopening a current demand to a new file takes its current offer along" sh -c "echo \"$CMD\" | grep -q 'future amend $O'"
tick; expect_ok "and runs" sh -c "$CMD"
git reset -q --hard "$H"
run ait future offer src/nope.c:1-5,src/nope.c:8-9 specs/p.yaml -m o
check "two ranges of one new file are one new:PATH" test "$(newin)" = 'ait future offer new:src/nope.c specs/p.yaml -m x'
run ait demand src/order.c,src/nope.c src/stock.c specs/w.yaml --force -m d
check "a current contract's same command" err_has "run 'ait demand src/order.c,new:src/nope.c src/stock.c specs/w.yaml -m <why>'"
CMD=$(newin); tick; run sh -c "$CMD"
check "which asks for results itself" fails 1 "error: check results required"

# 7.1.8 (user decision 47): a hint only where an ait command reaches the older version, with the tasks only for a check
backcmd() { sed -n "s/^hint: to take it back for others too, once this commit is reverted, run '\(ait .*\)'\$/\1/p" "$T/err" | sed 's/<why>/x/'; }
prep
tick; ait offer src/stock.c specs/p.yaml --force -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
echo x >> src/stock.c; tick; git commit -qam code >/dev/null 2>&1
tick; ait amend "$O" --force -m again >/dev/null 2>&1
tick; run git revert --no-edit HEAD
check "a re-pin reverted: told, with no command (same scope and spec files)" sh -c "grep -qF \"put '$O' back\" '$T/err' && ! grep -q 'take it back' '$T/err'"
tick; git revert --no-edit HEAD >/dev/null 2>&1
echo 'body: v2' > specs/p.yaml; tick; ait amend "$O" - specs/p.yaml --force -m v2 >/dev/null 2>&1
tick; run git revert --no-edit HEAD
R1=$(git rev-parse HEAD); OLD=$(git rev-parse HEAD:.ait/$O)
check "a definition's content reverted: restore the spec file, then amend" err_has "once this commit is reverted, restore 'specs/p.yaml' as in '$R1', then run 'ait amend $O - specs/p.yaml -m <why>'"
tick; git revert --no-edit HEAD >/dev/null 2>&1; git checkout -q "$R1" -- specs/p.yaml
tick; run ait amend "$O" - specs/p.yaml --force -m back
check "which reaches the older pins" sh -c "[ \"\$(git show HEAD:.ait/$O | grep '^file')\" = \"\$(git show $OLD | grep '^file')\" ]"
prep
tick; ait demand src/order.c src/stock.c specs/w.yaml --force -m d >"$T/out" 2>&1; D=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
tick; ait amend "$D" src/order.c:1-5 - - -m consumer >/dev/null 2>&1
tick; run git revert --no-edit HEAD
check "a consumer reverted: amend of the consumer alone" err_has "run 'ait amend $D src/order.c - - -m <why>'"
CMD=$(backcmd); tick; git revert --no-edit HEAD >/dev/null 2>&1
tick; run sh -c "$CMD"
check "and it commits at once" sh -c "[ $rc = 0 ] && grep -q '^changed $D' '$T/out'"
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
sed "s/^prev .*/prev $(git rev-parse HEAD:.ait/$O)/" .ait/$O > "$T/rec" && cat "$T/rec" > .ait/$O; git add .ait/$O; tick; git commit -qm v1 --no-verify
tick; run git revert --no-edit HEAD
check "two promises that mean the same: no command" sh -c "grep -qF \"put '$O' back\" '$T/err' && ! grep -q 'take it back' '$T/err'"
O2=Oaaaaaaaaaaaa
tick; ait offer src/order.c specs/w.yaml --force -m o2 >"$T/out" 2>&1; O2=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
X=Xaaaaaaaaaaaa
printf 'role drop\nstate future\ntarget %s\ncreator a %s\nprev -\n' "$O" "$A" > .ait/$X; git add .ait/$X; tick; git commit -qm drop --no-verify
sed -e "s/^target .*/target $O2/" -e "s/^prev .*/prev $(git rev-parse HEAD:.ait/$X)/" .ait/$X > "$T/rec" && cat "$T/rec" > .ait/$X
git add .ait/$X; tick; git commit -qm moved --no-verify
tick; run git revert --no-edit HEAD
check "a drop moved back: no command, as only future drop moves one (choice 161)" sh -c "grep -qF \"put '$X' back\" '$T/err' && ! grep -q 'take it back' '$T/err'"

# choice 161: a drop's amend is judged as its step: an ended one is closed, a moved one not supported, none at parse
run ait amend "$X" "$O2" -m move
check "moving a drop: not supported" fails 128 "fatal: not supported: 'drop'"
tick; ait future cancel "$X" -m c >/dev/null 2>&1
run ait amend "$X" "$O2" -m move
check "amending an ended drop: hint 3" sh -c "[ $rc = 1 ] && grep -qx \"hint: '$X' is closed\" '$T/err'"
run ait amend "$O" a b c d -m x
check "amend's arguments: usage" fails 129 "takes A B FILES (a demand), B FILES (an offer) or K (a drop)"

# 5.9a transition hint 1: a cancel refused for HEAD's accident gives its repair first
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
sed 's/^state future/state current/' .ait/$O > "$T/rec" && cat "$T/rec" > .ait/$O; git add .ait/$O; tick; git commit -qm hand --no-verify
run ait future cancel "$O" -m c
check "cancel of HEAD's accident: the repair, not a reopen" sh -c "[ $rc = 1 ] && grep -q 'ait contract commit' '$T/err' && ! grep -q 'reopen it' '$T/err'"

# user decision 44: the restore hint leaves out a record the work tree changed, which the stage hint stages
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
sed 's/^prev .*/prev junk/' .ait/$O > "$T/rec" && cat "$T/rec" > .ait/$O; ait contract add .ait/$O >/dev/null
sed 's#^provider .*#provider src/order.c#' .ait/$O > "$T/rec" && cat "$T/rec" > .ait/$O
tick; run ait contract commit -m x
check "noop: stage the edit, do not restore it away" sh -c "grep -qx noop '$T/out' && grep -qF \"stage it: 'ait contract add .ait/$O'\" '$T/err' && ! grep -qF \"restore '.ait/$O'\" '$T/err'"
# 6.1 (user decision 48): U's records are HEAD's with the written ones, so an input a changed definition declares is no code
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
mkdir -p tests; echo '#!/bin/sh' > tests/new.sh; printf 'body: p\ninputs: [tests/new.sh]\n' > specs/p.yaml
ait contract add specs/p.yaml tests/new.sh >/dev/null
tick; expect_ok "contract commit of a definition declaring a new input" ait contract commit -m declare
check "which it pins" sh -c "git show HEAD:.ait/$O | grep -q '^file tests/new.sh .* input'"

# 5.8 E_GUARD, 5.9a local changes: the hint is E_GUARD's own; code staged here is contract commit's E_MIXED to say
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >/dev/null 2>&1
echo x >> src/order.c; git add src/order.c; echo 'body: changed' > specs/p.yaml
tick; run git commit -qm only specs/p.yaml
check "only contract paths: through ait, nothing predicted" err_has "hint: commit them through ait: 'ait contract add specs/p.yaml' and 'ait contract commit -m <why>'"
ait contract add specs/p.yaml >/dev/null
tick; run ait contract commit -m x
check "and contract commit says the staged code itself" fails 1 "error: contract changes and code changes are mixed: 'src/order.c'"
# 2.1.2a: the init file is no record, so contract commit keeps it as written, and only what is there is removed
prep
rm .ait/init; mkdir .ait/init; echo x > .ait/init/x; git add -A; tick; git -c core.hooksPath=/dev/null commit -qm directory
rm -r .ait/init; printf 'version 2\n' > .ait/init; ait contract add .ait/init >/dev/null
tick; run ait contract commit -m repair
check "the repaired init is committed as written" sh -c "[ $rc = 0 ] && [ \"\$(git show HEAD:.ait/init)\" = 'version 2' ] && ! grep -q 'not updated' '$T/err'"
tick; expect_ok "and init accepts it" ait init

# 5.9a, others' contracts: a link goes back by release only when the offer is mine
prep
tick; ait future demand src/order.c src/stock.c specs/w.yaml -m d >"$T/out" 2>&1; D=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
B=$(ait session open --agent b 2>/dev/null)
tick; AIT_SESSION="$B" ait future offer src/stock.c specs/p.yaml as o undertake "$D" o -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \(O[^ ]*\) o$/\1/p' "$T/out")
tick; ait release "$D" "$O" -m r >/dev/null 2>&1; tick; ait undertake "$D" "$O" -m u >/dev/null 2>&1
tick; run git revert --no-edit HEAD
check "an undertake of another's offer reverted: told, with no release" sh -c "grep -qF 'back to an older version' '$T/err' && ! grep -q 'ait release' '$T/err'"

# 7.8.1 (user decision 45): an unmerged path is read from HEAD, and the other paths keep their bytes
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
mkdir ' .ait'; cp .ait/$O ' .ait/O111111111111'; git add ' .ait'; tick; git commit -qm data --no-verify
F=.ait/$O; B=$(git rev-parse HEAD:$F)
printf '0 0000000000000000000000000000000000000000\t%s\n100644 %s 1\t%s\n100644 %s 3\t%s\n' "$F" "$B" "$F" "$B" "$F" | git update-index --index-info
run ait todo O111111111111
check "a path with a leading space is no contract" fails 128 "fatal: no such contract: 'O111111111111'"
run ait todo "$O"
check "an unmerged record without stage 2 is read as HEAD has it" sh -c "[ $rc = 0 ] && grep -qx 'id $O' '$T/out'"

# 5.9a arguments: a hint's scope is one the tree where it runs takes (3.3), else <scope>
prep
tick; ait future offer src/ specs/p.yaml -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
tick; ait future cancel "$O" -m c >/dev/null 2>&1
git rm -qr src; echo f > src; git add src; tick; git commit -qm file --no-verify
run ait amend "$O" -m again
check "a directory that is now a file: <scope>" err_has "make a new contract: 'ait future offer <scope> specs/p.yaml -m <why>'"
prep
tick; ait future offer new:src/added specs/p.yaml -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
tick; ait future cancel "$O" -m c >/dev/null 2>&1
mkdir src/added; echo x > src/added/code; git add src; tick; git commit -qm dir --no-verify
run ait amend "$O" -m again
check "a new file that is now a directory: <scope>" err_has "make a new contract: 'ait future offer <scope> specs/p.yaml -m <why>'"

# 5.9a: H9 is judged on HEAD as it is when written, after a branch moved under the request
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
git branch side; git switch -q side; tick; ait future amend "$O" src/order.c - -m side >/dev/null 2>&1; git switch -q master
racing_git; printf 'git -C "$T/r" reset -q --hard side\n' > "$T/before-update-ref"
tick; run env PATH="$T/bin:$PATH" ait future amend "$O" src/stock.c:10-20 - -m mine
check "HEAD moved to the other version: no H9 for it" sh -c "[ $rc = 1 ] && grep -q 'HEAD moved while running' '$T/err' && ! grep -q 'has changes not in HEAD' '$T/err'"
# 5.9a (user decision 52): a hint's command, run as given, is taken: it commits, or asks for results itself
hintcmd() { grep -F -- "$1" "$T/err" | head -1 | grep -o "'ait [^']*'" | head -1 | tr -d "'" | sed 's/<why>/x/'; }
follows() {
	cmd=$(hintcmd "$1")
	[ -n "$cmd" ] || return 1
	tick; run sh -c "$cmd"
	[ $rc = 0 ] || fails 1 "error: check results required"
}
prep
run ait future demand src/order.c src/nope.c specs/w.yaml offer src/stock.c specs/p.yaml -m x
check "E_SCOPE of a promise with a current offer after it: taken" follows "if it does not exist yet"
prep
tick; ait demand src/order.c src/stock.c specs/w.yaml --force -m d >"$T/out" 2>&1; D=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
run ait amend "$D" src/nope.c - - -m x
check "E_SCOPE of a current demand's consumer alone: taken" follows "if it does not exist yet"
prep
tick; ait demand src/order.c src/stock.c specs/w.yaml --force -m d >"$T/out" 2>&1; D=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
tick; ait amend "$D" src/order.c:1-5 - - -m consumer >/dev/null 2>&1
setl src/stock.c 3 s3x; tick; git commit -qam code >/dev/null 2>&1
tick; run git revert --no-edit HEAD~1
tick; git revert --no-edit HEAD >/dev/null 2>&1
check "7.1.8 over code changed since: the consumer alone, taken" follows "to take it back for others too"

# 7.1.8: a link goes back by undertake only where undertake runs: live ends, no current offer for a promise (2.3, 5.9 d)
prep
tick; ait future demand src/order.c src/stock.c specs/w.yaml -m d >"$T/out" 2>&1; D=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
tick; ait future offer src/stock.c specs/p.yaml as o undertake "$D" o -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \(O[^ ]*\) o$/\1/p' "$T/out")
tick; ait release "$D" "$O" -m r >/dev/null 2>&1; R=$(git rev-parse HEAD); tick; ait future cancel "$O" -m c >/dev/null 2>&1
tick; run git revert --no-edit "$R"
check "a link whose offer ended since: told, with no undertake" sh -c "grep -qF 'back to an older version' '$T/err' && ! grep -q 'ait undertake' '$T/err'"

# 5.9a 판정 시점: H9 after a commit is judged on that commit, wherever HEAD went after it
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
git branch other; racing_git; printf 'git -C "$T/r" checkout -q other\n' > "$T/before-update-ref"
tick; run env PATH="$T/bin:$PATH" ait future amend "$O" src/order.c - -m mine
check "no H9 for the commit the request made" sh -c "grep -q '^changed $O' '$T/out' && ! grep -q 'has changes not in HEAD' '$T/err'"

# 5.1: a hook only reads the session; a stale default it cannot clean refuses nothing
prep
rm ".git/ait/sessions/$A"; chmod 444 .git/ait/worktrees/*.lock
echo y >> src/order.c; git add src/order.c; tick; run git commit -qm code
chmod 644 .git/ait/worktrees/*.lock
check "a plain commit with a stale session pointer and a locked worktree state" test "$rc" = 0

# 5.1: contract new and add need no session, but one that is set must be usable
prep
AIT_SESSION=S000000000000; export AIT_SESSION
run ait contract add specs/p.yaml
check "contract add with an unknown session: E_SESSION" fails 128 "no usable ait session"
run ait contract new offer
check "contract new with an unknown session: E_SESSION" fails 128 "no usable ait session"
unset AIT_SESSION
# 7.1.1, 5.9a: 'ait hook NAME' in a hint is what a hook runs, so it takes no session prefix and waits for no git operation
prep
B=$(ait session open --agent b 2>/dev/null)
printf '#!/bin/sh\necho mine\n' > "$(git rev-parse --git-path hooks)/pre-commit"
run env AIT_SESSION="$B" ait init
check "the hook line of a non-default session's hint has no prefix" sh -c "grep -qF \"run 'ait hook pre-commit'\" '$T/err' && ! grep -q \"'AIT_SESSION=[^ ']* ait hook\" '$T/err'"
# 5.9 d as the request has it: a link the request releases takes no offer along when the demand is reopened
prep
tick; ait future demand src/order.c src/stock.c specs/w.yaml -m d >"$T/out" 2>&1; D=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
tick; ait future offer src/stock.c specs/p.yaml as o undertake "$D" o -m o >"$T/out" 2>&1; O=$(sed -n 's/^new \(O[^ ]*\) o$/\1/p' "$T/out")
tick; ait fulfill "$O" --force -m both >/dev/null 2>&1
run ait release "$D" "$O" future amend "$D" - src/nope.c - -m change
check "a reopen after a release in the same request: the offer stays" sh -c "grep -q 'if it does not exist yet' '$T/err' && ! grep -q 'future amend $O' '$T/err'"

# 7.1.8: an older version whose scope the tree no longer has is not reached, so no command
prep
printf 'a\n' > a.c; printf 'b\n' > b.c; git add a.c b.c; tick; git commit -qm callers --no-verify
tick; ait demand a.c src/stock.c specs/w.yaml --force -m old >"$T/out" 2>&1; D=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
tick; ait future amend "$D" b.c - - -m reopen >/dev/null 2>&1; REOPEN=$(git rev-parse HEAD)
git rm -q a.c b.c; tick; git commit -qm removed --no-verify
tick; run git revert --no-edit "$REOPEN"
check "the older consumer is gone: told, with no command" sh -c "grep -qF \"put '$D' back\" '$T/err' && ! grep -q 'take it back' '$T/err'"

# 7.6.1: contract commit's noop names every contract path the work tree has otherwise, untracked inputs too
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >/dev/null 2>&1
printf 'body: changed\ninputs: [tests/new.sh]\n' > specs/p.yaml; mkdir -p tests; echo t > tests/new.sh
run ait contract commit -m noop
check "noop names the new input" sh -c "grep -qx noop '$T/out' && grep -qx 'unstaged tests/new.sh' '$T/out'"
check "and its stage hint is taken" follows "stage it"

# 7.3.3: a case twin is a path as git lists it, so a directory scope gets no file-shaped hint
prep
git config core.ignorecase true
run ait future offer SRC/ specs/p.yaml -m d
check "a directory spelled otherwise: refused with no hint" sh -c "[ $rc = 128 ] && ! grep -q 'give it as git has it' '$T/err'"

# 5.6: a message is one line, whatever a value in it holds, a warning too
prep
P=$(printf 'line\nbreak'); B=$(git rev-parse HEAD:src/stock.c)
{ printf '0 0000000000000000000000000000000000000000\t%s\000' "$P"; printf '100644 %s 2\t%s\000' "$B" "$P"; } | git update-index -z --index-info
run ait todo
check "a path with LF is one warning line" sh -c "grep -qx \"warning: 'line?break' is unmerged; it is read as HEAD has it\" '$T/err'"
# 5.10, 5.9a: a broken bundle whose spec file HEAD lost is mended by no command, so it gets no hint
prep
tick; ait offer src/stock.c specs/w.yaml --force -m o >/dev/null 2>&1
git rm -q specs/w.yaml; tick; run git commit -qm gone --no-verify
check "a lost spec file at HEAD: warned, with no hint" sh -c "grep -q 'spec files do not match the contract' '$T/err' && ! grep -q '^hint:' '$T/err'"
# 7.8 (user decision 53): a definition todo cannot read stops it, a partner's too
prep
tick; ait future demand src/order.c src/stock.c specs/w.yaml -m d >"$T/out" 2>&1; D=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
tick; ait future offer src/stock.c specs/p.yaml as o undertake "$D" o -m o >/dev/null 2>&1
git rm -q specs/w.yaml
run ait todo
check "todo with an undertaken demand whose definition is gone" sh -c "[ $rc = 1 ] && grep -q \"missing 'specs/w.yaml'\" '$T/err'"
# 5.8 E_INCOMPLETE: a helper on a contract whose spec file HEAD lost gets no hint either; creating the file did not help it
prep
tick; ait offer src/stock.c specs/w.yaml --force -m o >"$T/out" 2>&1; O=$(new_id)
git rm -q specs/w.yaml; tick; git commit -qm gone --no-verify >/dev/null 2>&1
run ait amend "$O" -m again
check "a helper on it: refused with no hint" sh -c "[ $rc = 1 ] && grep -q 'spec files do not match the contract' '$T/err' && ! grep -q '^hint:' '$T/err'"
# 5.10, 5.8 E_INCOMPLETE: whether the gate can pin HEAD's broken bundle again is the gate's own expansion, inputs a definition newly declares included
prep
tick; ait future offer src/stock.c specs/w.yaml -m o >/dev/null 2>&1
printf 'body: w\ninputs: [tests/none.sh]\n' > specs/w.yaml; git add specs/w.yaml
tick; run git commit -qm raw --no-verify
check "a definition declaring an input HEAD lacks: warned, with no hint" sh -c "grep -q 'spec files do not match the contract' '$T/err' && ! grep -q '^hint:' '$T/err'"
# 5.8 E_READ: a -r file that cannot be read is all the judgment knows (user decision 52), so no hint
prep
setl src/order.c 1 o1x; git add src/order.c
run ait commit -r "$T/none.yaml" -m c
check "an unreadable -r: refused with no hint" sh -c "[ $rc = 128 ] && grep -q 'could not read' '$T/err' && ! grep -q '^hint:' '$T/err'"
# 7.1.3-7, 7.1.4: an error a merge keeps from HEAD is warned once per record; what fixes it is the state after the merge, so no hint
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>&1; O=$(new_id)
git switch -q -c b; setl src/order.c 1 o1x; tick; git commit -qam b; git switch -q master
sed 's/^state future/state bogus/' ".ait/$O" > "$T/rec" && cat "$T/rec" > ".ait/$O"; git add ".ait/$O"; tick; git commit -q --no-verify -m bad >/dev/null 2>&1
tick; run git merge --no-ff --no-edit b
check "a merge keeping HEAD's error: one warning, no hint" sh -c "[ $rc = 0 ] && [ \$(grep -c 'keeps an error from HEAD' '$T/err') = 1 ] && ! grep -q '^hint:' '$T/err'"
# 5.9a: a pull hint gives the pull as it was given, its --force too
prep
git switch -q -c b; setl src/stock.c 1 s1x; tick; git commit -qam code
tick; ait future demand src/order.c src/stock.c specs/w.yaml -m q >/dev/null 2>&1
tick; ait future demand src/order.c src/stock.c specs/p.yaml -m d >"$T/out" 2>&1; D=$(new_id); git switch -q master
tick; ait offer src/stock.c specs/p.yaml --force -m o >/dev/null 2>&1
tick; run ait contract pull "$D" --force -m p
check "the pull's own hint keeps its --force" sh -c "[ $rc = 1 ] && grep -q \"name them too: 'ait contract pull [^']* --force -m <why>'\" '$T/err'"
# 2.1.4, 6.2: a spec file is a regular file wherever it is pinned, so the gate cannot pin a symbolic link HEAD has: no hint
prep
exec_def pass; git add -A; tick; git commit -qm pass --no-verify
tick; ait future offer src/stock.c specs/pass.yaml -m o >/dev/null 2>&1
rm tests/pass.sh; ln -s ../src/stock.c tests/pass.sh; git add tests/pass.sh
tick; run git commit -qm link --no-verify
check "an input HEAD made a symbolic link: warned, with no hint" sh -c "grep -q 'spec files do not match the contract' '$T/err' && ! grep -q '^hint:' '$T/err'"
run ait contract commit -m fix
check "and the gate does not pin it" sh -c "[ $rc = 1 ] && ! grep -q 'fix it in this request' '$T/err'"
# 5.10, 5.9 a: todo skips a record whose form is wrong as validate judges it, a check line on a non-current one too
prep
tick; ait demand src/order.c src/stock.c specs/p.yaml --force -m d >"$T/out" 2>&1; D=$(new_id)
sed 's/^state current/state future/' ".ait/$D" > "$T/rec" && cat "$T/rec" > ".ait/$D"; git add ".ait/$D"; tick; git commit -q --no-verify -m nv >/dev/null 2>&1
run ait todo "$D"
check "a check line on a future demand: skipped as malformed, with no hint to undertake it" sh -c "grep -q \"skipping malformed '.ait/$D'\" '$T/err' && ! grep -q 'undertake it yourself' '$T/err'"
# 2.5 (user decision 53): a definition that does not parse stops what reads it, and fixing it through ait still works
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >/dev/null 2>&1
printf 'body: p\nfoo: 1\n' > specs/p.yaml; git add specs/p.yaml; tick; git commit -q --no-verify -m nv >/dev/null 2>&1
run ait todo
check "todo stops with the definition's error" sh -c "[ $rc = 128 ] && grep -q \"bad format: 'specs/p.yaml'\" '$T/err'"
echo 'body: p' > specs/p.yaml; ait contract add specs/p.yaml >/dev/null 2>&1
tick; expect_ok "fixed and committed through ait" ait contract commit -m fix
# 5.10, 2.1.4: the repair restores the spec files GOOD pins that HEAD does not have as spec files, a symbolic link too
prep
exec_def pass; git add -A; tick; git commit -qm pass --no-verify
tick; ait future offer src/stock.c specs/pass.yaml -m o >"$T/out" 2>&1; O=$(new_id)
sed 's/^state future/state current/' ".ait/$O" > "$T/rec" && cat "$T/rec" > ".ait/$O"; git add ".ait/$O"
rm tests/pass.sh; ln -s ../src/stock.c tests/pass.sh; git add tests/pass.sh
tick; run git commit -qm both --no-verify
check "the repair restores the input with the record" grep -q "^hint: restore '.ait/$O' 'tests/pass.sh' as in " "$T/err"
G=$(sed -n "s/^hint: restore '.ait\/$O' 'tests\/pass.sh' as in '\([0-9a-f]*\)'.*/\1/p" "$T/err")
rm tests/pass.sh; git checkout -q "$G" -- ".ait/$O" tests/pass.sh
tick; expect_ok "and following it mends HEAD" ait contract commit -m fix
# 5.9a (user decision 54): H9's pull names the contracts that changed, not what that pull will add
prep
tick; ait future demand src/order.c src/stock.c specs/p.yaml -m d >"$T/out" 2>&1; D=$(new_id)
git switch -q -c b
tick; ait future demand src/order.c src/stock.c specs/w.yaml -m q >/dev/null 2>&1
tick; ait future amend "$D" src/order.c:1-3 - - -m d2 >/dev/null 2>&1; git switch -q master
tick; run ait future offer src/stock.c specs/w.yaml as o undertake "$D" o -m o
check "H9 names the changed contract alone" grep -qx "hint: to include them, run 'ait contract pull $D'" "$T/err"
run ait contract pull "$D"
check "and that pull names what else it takes" err_has "name them too"
# 7.12.1 (user decision 54): a divergence offers each version untried; the pull taken says what else it needs
prep
tick; ait future demand src/order.c src/stock.c specs/p.yaml -m d >"$T/out" 2>&1; D=$(new_id)
tick; ait future offer src/stock.c specs/w.yaml as o undertake "$D" o -m o >"$T/out" 2>&1; O=$(new_id)
git switch -q -c b; tick; ait future amend "$D" src/order.c:1-3 - - -m d2 >/dev/null 2>&1; B=$(git rev-parse HEAD); git switch -q master
tick; ait fulfill "$O" --force -m f >/dev/null 2>&1
run ait contract pull "$D"
check "a divergence offers both versions" sh -c "grep -q \"pull $D=HEAD -m <why>\" '$T/err' && grep -q \"pull $D=$B -m <why>\" '$T/err'"
# 7.5.2, 2.1.4: a spec file staged through ait is a regular file; a symbolic link is named as not there, with its fix
prep
exec_def pass; git add -A; tick; git commit -qm pass --no-verify
tick; ait future offer src/stock.c specs/pass.yaml -m o >/dev/null 2>&1
rm tests/pass.sh; ln -s ../src/stock.c tests/pass.sh
run ait contract add specs/pass.yaml tests/pass.sh
check "contract add names the link, with its fix" sh -c "[ $rc = 1 ] && grep -qx 'missing tests/pass.sh' '$T/out' && grep -q \"restore 'tests/pass.sh' from HEAD\" '$T/err'"
git add tests/pass.sh
tick; run ait contract commit -m x
check "contract commit too" sh -c "[ $rc = 1 ] && grep -qx 'missing tests/pass.sh' '$T/out'"
# 5.10: todo's skip of a record malformed by 5.9 a gives HEAD's repair, as for one that does not parse
prep
tick; ait demand src/order.c src/stock.c specs/p.yaml --force -m d >"$T/out" 2>&1; D=$(new_id)
sed 's/^state current/state future/' ".ait/$D" > "$T/rec" && cat "$T/rec" > ".ait/$D"; git add ".ait/$D"; tick; git commit -q --no-verify -m nv >/dev/null 2>&1
run ait todo
check "the skip carries the restore" grep -q "^hint: restore '.ait/$D' as in " "$T/err"
# 5.9 c: a link's ends exist, judged for every state a request builds, contract commit's too
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>&1; O=$(new_id)
mkdir -p .ait/undertake; printf 'state undertaken\n' > ".ait/undertake/D000000000000-$O"
ait contract add ".ait/undertake/D000000000000-$O" >/dev/null 2>&1
tick; run ait contract commit -m x
check "a link to no demand: refused for its form, no crash" sh -c "[ $rc = 128 ] && grep -q 'its demand or offer does not exist' '$T/err'"
# 5.9a hint 3: contract commit's refusal of a closed end gives the in-request form
prep
tick; ait future demand src/order.c src/stock.c specs/p.yaml -m d >"$T/out" 2>&1; D=$(new_id)
tick; ait future offer src/stock.c specs/w.yaml -m o >"$T/out" 2>&1; O=$(new_id)
tick; ait future cancel "$D" -m c >/dev/null 2>&1
mkdir -p .ait/undertake; printf 'state undertaken\n' > ".ait/undertake/$D-$O"
ait contract add ".ait/undertake/$D-$O" >/dev/null 2>&1
tick; run ait contract commit -m x
check "the closed demand's hint is in the request, not a helper" sh -c "[ $rc = 1 ] && grep -q 'in this request' '$T/err' && ! grep -q \"'ait future demand\" '$T/err'"
# 7.5.2, 2.1.4: a definition that is a symbolic link is not there as a spec file, for contract add too
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >/dev/null 2>&1
echo 'body: y' > specs/real.yaml; rm specs/p.yaml; ln -s real.yaml specs/p.yaml
run ait contract add specs/p.yaml
check "contract add names the linked definition as missing" sh -c "[ $rc = 1 ] && grep -qx 'missing specs/p.yaml' '$T/out'"
# 5.8 E_INCOMPLETE: one form of the missing hint for helpers and contract add
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >/dev/null 2>&1
printf 'body: x\ninputs: [tests/none.sh]\n' > specs/p.yaml
run ait contract add specs/p.yaml
check "contract add: create it or stop declaring it in DECL" grep -qx "hint: create it or stop declaring it in 'specs/p.yaml'" "$T/err"
# 7.5.2: a record removed from the work tree has no related paths; contract add stages the removal, the gate judges it
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>&1; O=$(new_id)
rm ".ait/$O"
run ait contract add ".ait/$O"
check "contract add stages a removed record" sh -c "[ $rc = 0 ] && grep -qx 'staged .ait/$O' '$T/out'"
tick; run ait contract commit -m x
check "and the gate refuses removing it" err_has "records are never removed"
# 7.12.1: a pull with nothing new to write once 7.12.5 has applied is "already", with --force too
prep
tick; ait future demand src/order.c src/stock.c specs/p.yaml -m d >"$T/out" 2>&1; D=$(new_id)
H=$(git rev-parse HEAD)
tick; run ait contract pull "$D" --force -m why
check "nothing to take with --force: already, no commit" sh -c "[ $rc = 0 ] && grep -q '^already' '$T/out' && [ \$(git rev-parse HEAD) = $H ]"
# 7.1.8: a commit that only changes a record's mode does not put it back to an older version
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>&1; O=$(new_id)
chmod +x ".ait/$O"; git add ".ait/$O"
tick; run git commit -qm mode --no-verify
check "no 'put back' for a mode change" sh -c "! grep -q 'back to an older version' '$T/err'"
check "a record with the wrong mode is restored, not removed" sh -c "grep -q \"^hint: restore '.ait/$O' as in \" '$T/err' && ! grep -q \"remove '.ait/$O'\" '$T/err'"
done_testing
