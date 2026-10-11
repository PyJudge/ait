#!/bin/sh
# T42 미확인: current 계약의 코드를 바꾸고, 옮기고, 지우기 (1.5a, 3.8, 7.3.2 amend, 9.1)
#
# not yet: no notifications (sub, listeners, events), no peek, no contract push, no future drop.
# - Steps 1-14 happen in the main worktree (wb and the listeners only receive events);
#   steps 15 and 16 put A in wa and B in wb as T30 does, since B pulls there.
# - `ait peek now <K>` is replaced by `ait todo <K>`.
. "$(dirname "$0")/lib.sh"

id_of() { sed -n "s/^new \([^ ]*\) $1\$/\1/p" "$T/out"; }
setl() { f=$1 l=$2; shift 2; awk -v n="$l" -v t="$*" 'NR==n{print t; next}1' "$f" >"$f.tmp" && cat "$f.tmp" >"$f" && rm "$f.tmp"; }
not() { ! "$@"; }
todo_k() { run ait todo $K; }
# block_has ID LINE: the todo block of ID in the last output has LINE
block_has() { awk -v id="id $1" '$0==id{b=1} b&&$0==""{b=0} b' "$T/out" | grep -qxF -- "$2"; }

prep() {
	setup
	A=$(ait session open --agent agent-a)
	exec_def nobroken; exec_def pass; prompt_defs
	git add -A; tick; git commit -qm defs
	BEFORE_R1=$(git rev-parse HEAD)
	tick; with_r pass ait offer src/stock.c:1-10 specs/nobroken.yaml as k -m k
	K=$(id_of k)
	[ -n "$K" ] && say_ok "prep: K (R1)" || say_fail "prep: K (R1)"
	# not yet: `ait sub <K>` and `settle`
}
# step 1's commit C (T42-1): lines 1-10 of stock.c go to the new file src/stock2.c
move() {
	sed -n 1,10p src/stock.c >src/stock2.c
	sed 1,10d src/stock.c >"$T/s" && cat "$T/s" >src/stock.c && rm "$T/s"
	tick; git add -A; run git commit -qm refactor; C=$(git rev-parse HEAD)
}

prep

# 1. moving is not blocked but shows
move
check "1: the commit succeeds" test $rc = 0
check "1: the warning holds K" err_has "    $K offer src/stock.c:1-10 (no code under its scope) (agent-a)"
todo_k
check "1: K is ?deleted" out_has "provider src/stock.c:1-10 ?deleted"
# not yet: events of B and A (H8)

# 2. writing is not blocked (T42-2: the demand's provider is src/stock.c)
tick; expect_ok "2: a future demand" ait future demand src/order.c src/stock.c specs/p.yaml -m m
# not yet: `ait contract push origin` (E_STATE, --force E_CODE_REQUIRED) is not supported yet

# 2a. with no code to check, the gate does not look at results
GD=$(git rev-parse --git-dir)
QB=$(cat "$GD/ait/check-request.yaml" 2>/dev/null); WB=$(cat "$GD/ait/check-result.yaml" 2>/dev/null)
before=$(snapshot)
expect_err "2a: empty amend" E_CODE_REQUIRED ait amend $K -m x
check "2a: unchanged" test "$(snapshot)" = "$before"
check "2a: no task lines" not out_matches '^(task|check-request|check-result) '
check "2a: Q and W are as before" test "$(cat "$GD/ait/check-request.yaml" 2>/dev/null)/$(cat "$GD/ait/check-result.yaml" 2>/dev/null)" = "$QB/$WB"
check "2a: no code-first hint (no code in the work tree)" not err_has "stage 'src/stock.c'"
# T42-2a: the gate's hint fixes the same command: move the scope (no KEPT is left) or reopen
check "2a: hint: move the scope" err_has "ait amend $K <new scope> - -m <why>"
check "2a: hint: reopen it" err_has "ait future amend $K -m <why>"
check "2a: no withdraw hint" not err_has "future drop"
tick; expect_err "2a: --force is the same" E_CODE_REQUIRED ait amend $K -m x --force
check "2a: --force unchanged" test "$(snapshot)" = "$before"
check "2a: --force: hint: move the scope" err_has "ait amend $K <new scope> - -m <why>"
check "2a: --force: hint: reopen it" err_has "ait future amend $K -m <why>"

# 3. todo by scope (T42-3: `ait todo src/stock.c`)
run ait todo src/stock.c
check "3: K's block: state current" block_has $K "state current"
check "3: K's block: provider src/stock.c:1-10 ?deleted" block_has $K "provider src/stock.c:1-10 ?deleted"

# 4. not yet: notifications

# 5. move the contract
expect_err "5: amend K to stock2.c:1-10" E_NEEDS_RESULTS ait amend $K src/stock2.c:1-10 - -m "stock2로 옮김"
check "5: the task is K's" test "$(grep '^task ' "$T/out")" = "task $K:specs/nobroken.yaml exec"
tick; with_r pass ait amend $K src/stock2.c:1-10 - -m "stock2로 옮김"
check "5: changed K" out_has "changed $K"
check "5: check K reported" out_has "check $K reported"
check "5: K's check line is reported <pins>" [ "$(check_line HEAD $K)" = "reported $(pins HEAD $K)" ]
todo_k
check "5: provider src/stock2.c:1-10, no ?" out_has "provider src/stock2.c:1-10"
check "5: not unverified" not grep -q unverified "$T/out"
# not yet: peek's event and B's R5 event (H3) are not checked

# 6. change in place and check again
setl src/stock2.c 3 s3x; git add src/stock2.c; tick; git commit -qm c6 2>/dev/null
todo_k
check "6: K is ?changed" out_has "provider src/stock2.c:1-10 ?changed"
setl src/stock2.c 3 s3; git add src/stock2.c; tick; run git commit -qm back
todo_k
check "6: restored: no ?" out_has "provider src/stock2.c:1-10"
check "6: restored: not unverified" not grep -q unverified "$T/out"
setl src/stock2.c 3 s3y; git add src/stock2.c; tick; git commit -qm c6b 2>/dev/null
expect_err "6: amend K in place" E_NEEDS_RESULTS ait amend $K src/stock2.c:1-10 - -m m
W=$(out_value check-result); fill "$W" pass $K:specs/nobroken.yaml=fail
before=$(snapshot)
expect_err "6: a failed check" E_CHECK_FAILED ait amend $K src/stock2.c:1-10 - -m m -r "$W"
check "6: unchanged" test "$(snapshot)" = "$before"
check "6: hint: commit code that meets it first" err_has "commit code that meets it first: stage <your code> and run 'ait commit -m <why>', then rerun this command"
# T42-6: K is current in HEAD, so the same command with 'amend' made 'future amend'
check "6: hint: reopen it in the same command" err_has "ait future amend $K src/stock2.c:1-10 - -m <why>"
check "6: no <new defs> hint" not err_has "<new defs>"
todo_k
check "6: K stays unverified" out_has "unverified yes"
tick; with_r pass ait amend $K src/stock2.c:1-10 - -m m
check "6: changed K" out_has "changed $K"
check "6: check K reported" out_has "check $K reported"
todo_k
check "6: no ?" out_has "provider src/stock2.c:1-10"
check "6: K is verified again: its check line is reported <pins>" [ "$(check_line HEAD $K)" = "reported $(pins HEAD $K)" ]

# 7. checking in a request commit
tick; run ait future offer src/stock2.c specs/pass.yaml as o7 -m m
O7=$(id_of o7)
setl src/stock2.c 5 s5x; git add src/stock2.c
expect_err "7: fulfill O7" E_NEEDS_RESULTS ait fulfill $O7 -m m
check "7: touch K" out_has "touch $K"
tick; with_r pass ait fulfill $O7 -m m
check "7: check K reported" out_has "check $K reported"
todo_k
check "7: K has no ?" out_has "provider src/stock2.c:1-10"
# not yet: "no change event" is about notifications

# 8. a change coming in by merge
prep
git branch bb $BEFORE_R1
git switch -q bb; setl src/stock.c 3 s3x; tick; git commit -qam bb; git switch -q master
tick; run git merge --no-edit bb
check "8: merge ok" test $rc = 0
todo_k
check "8: K is ?changed" out_has "provider src/stock.c:1-10 ?changed"
check "8: K is unverified" out_has "unverified yes"

# 9-11, 13, 14. not yet: notifications, peek history

# 12. withdraw without moving
prep
move
tick; expect_err "12: future drop" UNSUPPORTED ait future drop $K as x -m m

# 15, 16 (T42-15, T42-16): wa(A) and wb(B) as T30 has them; A makes K (R1) in wa
prep_ab() {
	setup
	git worktree add -q "$T/wa" -b a; git worktree add -q "$T/wb" -b b
	cd "$T/wb"; B=$(ait session open --agent agent-b)
	cd "$T/wa"; A=$(ait session open --agent agent-a)
	exec_def nobroken; prompt_defs
	git add -A; tick; git commit -qm defs
	tick; with_r pass ait offer src/stock.c:1-10 specs/nobroken.yaml as k -m k
	K=$(id_of k) R1C=$(git rev-parse HEAD)
	[ -n "$K" ] && say_ok "prep: K (R1) in wa" || say_fail "prep: K (R1) in wa"
}

# 15. what comes in unverified
prep_ab
cd "$T/wb"; tick; expect_ok "15: B pulls K" ait contract pull $K
check "15: included K from R1's commit" test "$(cat "$T/out")" = "included $K from '$R1C'"
cd "$T/wa"; move
check "15: wa commits C" test $rc = 0
cd "$T/wb"; expect_ok "15: B pulls K again" ait contract pull $K
check "15: already K (C did not change K's version)" test "$(cat "$T/out")" = "already $K"
cd "$T/wa"; tick; run ait future offer src/order.c specs/p.yaml as o15 -m m
O15=$(id_of o15) R15C=$(git rev-parse HEAD)
check "15: A's R15" test $rc = 0 -a -n "$O15"
cd "$T/wb"; tick; expect_ok "15: B pulls O15" ait contract pull $O15
check "15: included O15 from R15's commit" test "$(cat "$T/out")" = "included $O15 from '$R15C'"
check "15: fast-forward: b is R15's commit" test "$(git rev-parse HEAD)" = "$R15C"
check "15: C comes along" git merge-base --is-ancestor "$C" HEAD
# not yet: `warning: '<K>' comes in unverified` and its H8 hint are for B's subscription
# `src/stock.c` (7.12.5, 9.3); there is no `ait sub`
prep_ab
cd "$T/wb"; tick; run ait contract pull $K
check "15: B takes R1" test "$(cat "$T/out")" = "included $K from '$R1C'"
cd "$T/wa"; move
cd "$T/wb"; setl src/order.c 50 o50x; tick; git commit -qam order
tick; run git merge --no-edit a
check "15: wb merges a" test $rc = 0
check "15: it is a merge commit" sh -c 'git rev-parse -q --verify HEAD^2 >/dev/null'
check "15: no 'is unverified after this merge' (K is unverified with the same pins in a)" not err_has "is unverified after this merge"
# not yet: commit-msg's `warning: '<K>' comes in unverified` and H8 are for B's subscription (7.1.5, 9.3)

# 16. a drop's maker receives too
prep_ab
cd "$T/wb"; tick; run ait contract pull $K
check "16: included K from R1's commit" test "$(cat "$T/out")" = "included $K from '$R1C'"
tick; expect_err "16: future drop" UNSUPPORTED ait future drop $K as x -m "철회 예정"
# not yet: the rest of 16 needs the drop X and B's events

done_testing
