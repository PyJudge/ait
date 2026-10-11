#!/bin/sh
# T47 출력이 다음 할 일을 알린다 (규칙 7, 5.8, 5.9a, 6.7a, 7.2, 9.7)
#
# not yet: hints only in their basic form; no listen/sub (events and their hints), no peek,
# no future drop. The events of steps 1-7 and step 10 (it needs `future drop`) are left
# out; the actions of those steps that later steps build on are done. Hints are checked
# by their command and IDs (0.2).
. "$(dirname "$0")/lib.sh"

id_of() { sed -n "s/^new \([^ ]*\) $1\$/\1/p" "$T/out"; }
setl() { f=$1 l=$2; shift 2; awk -v n="$l" -v t="$*" 'NR==n{print t; next}1' "$f" >"$f.tmp" && cat "$f.tmp" >"$f" && rm "$f.tmp"; }
not() { ! "$@"; }
st() { rec "$1" | sed -n 's/^state //p'; }
# fails RC LINE: the last run exited RC and its first stderr line is LINE
fails() { [ "$rc" = "$1" ] && [ "$(head -1 "$T/err")" = "$2" ]; }

# T46's setup (C in wa, E in wb as second sessions)
setup
git worktree add -q "$T/wa" -b a; git worktree add -q "$T/wb" -b b
cd "$T/wa"; A=$(ait session open --agent agent-a)
run ait session open --agent agent-c; C=$(cat "$T/out")
check "1: opening C in wa: rc 0" test $rc = 0
check "1: opening C: warning: this worktree is used by session A" err_has "warning: this worktree is used by session '$A' (agent 'agent-a')"
check "1: opening C: hint close this one" err_has "ait session close $C"
check "1: opening C: hint open a session in a worktree of your own" err_has "ait session open --agent agent-c"
cd "$T/wb"; B=$(ait session open --agent agent-b); E=$(ait session open --agent agent-e 2>/dev/null)
# not yet: `ait sub src/stock.c` and the listeners
cd "$T/wa"; prompt_defs; exec_def nobroken
tick; run ait future demand src/order.c src/stock.c specs/p.yaml as d -m 요구
D=$(id_of d)
check "prep: R1" test $rc = 0 -a -n "$D"

# T47-9, T47-11: "prepare again" is the setup above with R1, without its checks
prep_again() {
	setup
	git worktree add -q "$T/wa" -b a; git worktree add -q "$T/wb" -b b
	cd "$T/wa"; A=$(ait session open --agent agent-a); C=$(ait session open --agent agent-c 2>/dev/null)
	cd "$T/wb"; B=$(ait session open --agent agent-b); E=$(ait session open --agent agent-e 2>/dev/null)
	cd "$T/wa"; prompt_defs; exec_def nobroken
	tick; run ait future demand src/order.c src/stock.c specs/p.yaml as d -m 요구
	D=$(id_of d)
	[ $rc = 0 ] && [ -n "$D" ] && say_ok "prep again: R1" || say_fail "prep again: R1"
}

# 1. session
cd "$T/wa"
run ait session open --agent agent-a
check "1: a second agent-a session opens" test $rc = 0
A2=$(cat "$T/out")
check "1: warning: agent name is also used" err_has "warning: agent name 'agent-a' is also used by session '$A'"
check "1: hint has 'ait session close'" err_has "ait session close"
check "1: hint has 'AIT_SESSION='" err_has "AIT_SESSION="
expect_ok "1: close it" ait session close $A2
# not yet: the listener part (`E_BUSY`, `kill $PA`)

# 2. changes not in HEAD (T47-2, H9)
cd "$T/wb"; tick; run ait contract pull $D
check "2: B pulls D" test $rc = 0
cd "$T/wa"; tick; run ait future amend $D - - specs/low.yaml -m 고침
R2C=$(git rev-parse HEAD)
check "2: A's R2" test $rc = 0
cd "$T/wb"; tick; run ait future offer src/stock.c:20-30 specs/p.yaml as o undertake $D o -m 착수
O=$(id_of o)
check "2: B's O undertakes D" test $rc = 0 -a -n "$O"
check "2: warning: D has changes not in HEAD" err_has "warning: '$D' has changes not in HEAD: '$R2C' on 'refs/heads/a'"
check "2: hint: ait contract pull D" err_has "hint: to include them, run 'ait contract pull $D'"
BH=$(git rev-parse HEAD)
tick; run ait contract pull $D
check "2: included D from R2's commit" test "$(cat "$T/out")" = "included $D from '$R2C'"
check "2: b is ahead, so the pull makes M2 (parents: b's HEAD and R2's commit)" test "$(git rev-parse HEAD^1 HEAD^2 | tr '\n' ' ')" = "$BH $R2C "
# not yet: no inbox has an event of M2

# 3. undertaking together
# not yet: the `ait peek src/stock.c` hint (H11)
tick; run env AIT_SESSION=$E ait future offer src/stock.c specs/p.yaml as o2 undertake $D o2 -m 착수
O2=$(id_of o2)
check "3: E's offer ok" test $rc = 0 -a -n "$O2"
check "3: warning: D is also undertaken by O" err_has "warning: '$D' is also undertaken by '$O'"
check "3: hint ait future cancel O2" err_has "ait future cancel $O2"

# 4. not yet: events (R3's events, H3); A's R3 is done, since step 5 takes it
cd "$T/wa"; tick; run ait future amend $D - - specs/p.yaml -m 되돌림
check "4: A's R3" test $rc = 0

# 5. someone else fulfils (not yet: the event hints are not checked)
cd "$T/wb"; tick; run ait contract pull $D
check "5: B pulls D" test $rc = 0
setl src/stock.c 1 s1x; git add src/stock.c
tick; with_r pass ait fulfill $O -m f
check "5: fulfilled D via O" out_has "fulfilled $D via $O"
check "5: the D-O2 link stays undertaken" test "$(git show HEAD:.ait/undertake/$D-$O2 | sed -n 's/^state //p')" = undertaken
# T47-5: a plain commit by E leaves D unverified; O's scope src/stock.c:20-30 is not touched
setl src/stock.c 1 s1z; git add src/stock.c
tick; run env AIT_SESSION=$E git commit -qm c5
check "5: C5 commits" test $rc = 0
check "5: the warning holds D only" test "$(grep -E '^    [DOX][0-9a-f]{12} ' "$T/err" | sed 's/^    \([^ ]*\) .*/\1/' | sort -u)" = "$D"
check "5: D's reopen hint takes O along, with E's prefix" err_has "AIT_SESSION=$E ait future amend $D future amend $O -m <why>"
# not yet: A's change event of C5
tick; run git revert --no-edit HEAD
check "5: git revert restores it" test $rc = 0
before=$(snapshot)
expect_err "5: E reopens D alone" E_STATE env AIT_SESSION=$E ait future amend $D -m "바꿀 예정"
check "5: unchanged" test "$(snapshot)" = "$before"
check "5: hint reopen them together, with E's prefix" err_has "AIT_SESSION=$E ait future amend $D future amend $O -m <why>"
tick; run env AIT_SESSION=$E ait future amend $D future amend $O -m "바꿀 예정"
check "5: changed D" out_has "changed $D"
check "5: changed O" out_has "changed $O"
check "5: D and O are future" test "$(st $D) $(st $O)" = "future future"
setl src/stock.c 1 s1y; git add src/stock.c
tick; run git commit -qm after
AFTER=$(git rev-parse HEAD)
check "5: a later stock.c commit has no warning" test ! -s "$T/err"

# 6. who takes it when the one undertaking leaves (T47-6; not yet: A's R7 event, H5)
cd "$T/wa"; tick; run ait future demand src/order.c src/stock.c:20-30 specs/p.yaml as d6 -m 요구6
D6=$(id_of d6)
check "6: A's R5" test $rc = 0 -a -n "$D6"
cd "$T/wb"; tick; run ait contract pull $D6
check "6: B pulls D6" test $rc = 0
tick; run ait future offer src/stock.c:20-30 specs/p.yaml as o6 undertake $D6 o6 -m 착수
O6=$(id_of o6)
check "6: B's R6" test $rc = 0 -a -n "$O6"
cd "$T/wa"
NAMES=$(printf '%s\n' $D $D6 $O $O2 $O6 | LC_ALL=C sort | tr '\n' ' ' | sed 's/ $//')
before=$(snapshot)
run ait contract pull $D6 $O6
check "6: pull D6 O6 is E_UNNAMED" sh -c "[ $rc = 1 ] && head -1 '$T/err' | grep -q \"^error: taking '.*' also changes contracts you did not name: \""
check "6: unchanged" test "$(snapshot)" = "$before"
check "6: hint: name D, O and O2 too, in ID order" err_has "name them too: 'ait contract pull $NAMES'"
tick; run ait contract pull $NAMES
check "6: the pull the hint gives succeeds" test $rc = 0
check "6: wb's plain commit comes along" git merge-base --is-ancestor "$AFTER" HEAD
cd "$T/wb"; tick; run ait future cancel $O6 -m 중단
check "6: B cancels O6 (R7)" test $rc = 0

# 7. (T47-7: K on src/order.c:40-45; not yet: `ait sub <K>`, the events and the listener part)
cd "$T/wa"
tick; with_r pass ait offer src/order.c:40-45 specs/nobroken.yaml as k -m k
K=$(id_of k)
check "prep: K" test $rc = 0 -a -n "$K"
setl src/order.c 41 o41x; git add src/order.c; tick; run git commit -qm c8
check "7: C8 commits" test $rc = 0
tick; with_r pass ait amend $K -m m
check "7: A checks K again with the empty amend" test $rc = 0
check "7: check K reported" out_has "check $K reported"

# 8. error hints
cd "$T/wb"
expect_err "8: fulfill an alias" E_USAGE ait fulfill o -m x
check "8: hint: aliases are local to one command" err_has "aliases are local to one command"
expect_err "8: cancel an alias" E_ID ait future cancel o -m x
check "8: hint: aliases are local to one command (cancel)" err_has "aliases are local to one command"
expect_err "8: -r a missing file" E_READ env AIT_SESSION=$E ait fulfill $O2 -m x -r "$T/none.yaml"
check "8: hint: rerun without -r" err_has "rerun without -r"
expect_err "8: fulfill O2" E_NEEDS_RESULTS env AIT_SESSION=$E ait fulfill $O2 -m x
W=$(out_value check-result)
check "8: tasks are O2's and D's" test "$(grep '^task ' "$T/out" | sed 's/^task \([^:]*\):.*/\1/' | sort | tr '\n' ' ')" = "$(printf '%s\n%s\n' $D $O2 | sort | tr '\n' ' ')"
fill "$W" pass
awk 'BEGIN{d=0} /^    basis: / && !d {d=1; next} 1' "$W" >"$W.tmp" && cat "$W.tmp" >"$W" && rm "$W.tmp"
expect_err "8: a pass without basis" E_FORMAT env AIT_SESSION=$E ait fulfill $O2 -m x -r "$W"
check "8: hint: a pass needs a basis" err_has "a pass needs a basis"
fill "$W" fail
expect_err "8: fail" E_CHECK_FAILED env AIT_SESSION=$E ait fulfill $O2 -m x -r "$W"
check "8: hint: make the code meet the failed task(s)" err_has "make the code meet the failed task(s) and stage it, then rerun without -r to get the new tasks"
# T47-8 (5.8 E_CHECK_FAILED, 5.9a): D is undertaken by O2, which E made, so release it; E is not wb's default session
check "8: hint: E releases D from O2" err_has "AIT_SESSION=$E ait release $D $O2 -m <why>"

# 8. no one is told to withdraw someone else's contract
cd "$T/wa"
expect_err "8: C cancels current K" E_STATE env AIT_SESSION=$C ait future cancel $K -m x
check "8: C: hint ait future amend K" err_has "ait future amend $K -m <why>"
check "8: C: no future drop hint" not err_has "ait future drop"
expect_err "8: A cancels current K" E_STATE ait future cancel $K -m x
check "8: A: hint ait future amend K" err_has "ait future amend $K -m <why>"
# not yet: A's withdraw hint (`ait future drop <K> as x -m <why>`, `with nothing staged`) needs future drop

# 9. staged changes are set aside before taking (T47-9, 7.12.0, 6.7a)
prep_again
cd "$T/wa"; tick; with_r pass ait offer src/stock.c:1-10 specs/nobroken.yaml as k -m k
K=$(id_of k) RKC=$(git rev-parse HEAD)
# Rk carries R1, so D comes along and is named too (7.12.3, decision 43)
cd "$T/wb"; tick; run ait contract pull $D $K
check "9: B pulls D and K: both included from Rk's commit, the one taken (SP7)" test "$(sort "$T/out")" = "$(printf "included %s from '%s'\nincluded %s from '%s'\n" $D $RKC $K $RKC | sort)"
check "9: fast-forward" test "$(git rev-parse HEAD)" = "$RKC"
tick; run ait future offer src/order.c specs/p.yaml as o9 -m o9
O9=$(id_of o9)
check "9: B's O9 (b is ahead)" test $rc = 0 -a -n "$O9"
cd "$T/wa"; tick; with_r pass ait amend $K src/stock.c:1-8 - -m 좁힘
RAC=$(git rev-parse HEAD)
check "9: A narrows K (Ra)" test $rc = 0
cd "$T/wb"; setl src/stock.c 5 s5x; git add src/stock.c
before=$(snapshot)
expect_err "9: fulfill O9" E_NEEDS_RESULTS ait fulfill $O9 -m m
check "9: unchanged" test "$(snapshot)" = "$before"
check "9: touch K" out_has "touch $K"
check "9: tasks K:nobroken and O9:p" test "$(grep '^task ' "$T/out" | sort)" = "$(printf 'task %s:specs/nobroken.yaml exec\ntask %s:specs/p.yaml prompt\n' $K $O9 | sort)"
check "9: warning: K has changes not in HEAD" err_has "warning: '$K' has changes not in HEAD: '$RAC' on 'refs/heads/a'"
check "9: hint: first commit or set aside your changes" err_has "first commit or set aside your changes"
check "9: hint: ait contract pull K" err_has "ait contract pull $K"
before=$(snapshot)
tick; run ait contract pull $K
check "9: pull with a staged change: E_DIRTY, uncommitted changes in the index" fails 1 "error: uncommitted changes in the index"
check "9: unchanged" test "$(snapshot)" = "$before"
check "9: hint: first commit or set aside your changes, then rerun this command" err_has "first commit or set aside your changes, then rerun this command"
BH=$(git rev-parse HEAD)
s=$(git stash create); git reset -q --hard; tick; run ait contract pull $K; cp "$T/out" "$T/pull.out"
git stash apply -q --index "$s" >/dev/null 2>&1
check "9: set aside, pulled, put back: included K from Ra's commit" test "$(cat "$T/pull.out")" = "included $K from '$RAC'"
check "9: b has the pull's commit" test "$(git rev-parse HEAD^1 HEAD^2 | tr '\n' ' ')" = "$BH $RAC "
check "9: stock.c line 5 is staged again" test "$(git diff --cached --name-only)/$(git show :src/stock.c | sed -n 5p)" = "src/stock.c/s5x"
run ait fulfill $O9 -m m
check "9: fulfill O9 again: no 'has changes not in HEAD'" not err_has "has changes not in HEAD"
check "9: touch K is still there" out_has "touch $K"
check "9: K's provider is src/stock.c:1-8" test "$(rec $K | sed -n 's/^provider \([^ ]*\) .*/\1/p')" = "src/stock.c:1-8"

# 10. not yet: `future drop` (H6 for the demand's maker)

# 11. local changes on a path the pull writes, even in a fast-forward (T47-11, 7.12.0)
prep_again
cd "$T/wb"; tick; run ait contract pull $D
check "11: B pulls D (fast-forward)" test $rc = 0 -a "$(git rev-parse HEAD)" = "$(git rev-parse a)"
cd "$T/wa"; tick; run ait future amend $D - - specs/low.yaml -m 고침
check "11: A's R2" test $rc = 0
cd "$T/wb"; echo 'body: other' >specs/low.yaml; git add specs/low.yaml
setl src/stock.c 1 s1x; git add src/stock.c
expect_err "11: B fulfills D" E_NEEDS_RESULTS ait fulfill $D -m m
check "11: H9 hint: commit or set aside your changes to specs/low.yaml" err_has "commit or set aside your changes to 'specs/low.yaml' before you pull"
check "11: H9 hint: ait contract pull D" err_has "hint: to include them, run 'ait contract pull $D'"
before=$(snapshot)
tick; run ait contract pull $D
check "11: pull: E_DIRTY, your local changes would be overwritten" fails 1 "error: your local changes to 'specs/low.yaml' would be overwritten by this pull"
check "11: unchanged" test "$(snapshot)" = "$before"
git rm -q --cached specs/low.yaml
expect_err "11: B fulfills D (untracked specs/low.yaml)" E_NEEDS_RESULTS ait fulfill $D -m m
check "11: H9 hint: move or remove specs/low.yaml" err_has "move or remove 'specs/low.yaml' before you pull"
check "11: H9 hint: ait contract pull D (untracked)" err_has "ait contract pull $D"
before=$(snapshot)
tick; run ait contract pull $D
check "11: pull: E_DIRTY, untracked files would be overwritten" fails 1 "error: untracked working tree files would be overwritten by this pull: 'specs/low.yaml'"
check "11: unchanged (untracked)" test "$(snapshot)" = "$before"

done_testing
