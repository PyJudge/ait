#!/bin/sh
# T42 미확인: current 계약의 코드를 바꾸고, 옮기고, 지우기 (1.5a, 3.8, 7.3.2 amend, 9.1)
#
# mini: file-level scopes only (no line ranges), no notifications (wb, listeners, events),
# no peek, no contract push/pull, no future drop.
# - K's scope `src/stock.c:1-10` is the whole file `src/stock.c`; "move lines 1-10 to
#   src/stock2.c" is "move src/stock.c to src/stock2.c", so K shows `?missing` where the
#   scenario has `?deleted` (3.8: a whole-file scope whose file is gone).
# - The work happens in the main worktree (wb and the listeners only receive events).
# - `ait peek now <K>` / `ait todo src/stock.c` are replaced by `ait todo <K>`.
. "$(dirname "$0")/lib.sh"

id_of() { sed -n "s/^new \([^ ]*\) $1\$/\1/p" "$T/out"; }
setl() { f=$1 l=$2; shift 2; awk -v n="$l" -v t="$*" 'NR==n{print t; next}1' "$f" >"$f.tmp" && cat "$f.tmp" >"$f" && rm "$f.tmp"; }
not() { ! "$@"; }
trailer() { git log -1 --format=%B | grep -qxF -- "$1"; }
todo_k() { run ait todo $K; }

prep() {
	setup
	A=$(ait session open --agent agent-a)
	exec_def nobroken; exec_def pass; prompt_defs
	git add -A; tick; git commit -qm defs
	BEFORE_R1=$(git rev-parse HEAD)
	tick; with_r pass ait offer src/stock.c specs/nobroken.yaml as k -m k
	K=$(id_of k)
	[ -n "$K" ] && say_ok "prep: K (R1)" || say_fail "prep: K (R1)"
	# mini: `ait sub <K>` and `settle`
}
# step 1's commit C
move() { git mv src/stock.c src/stock2.c; tick; run git commit -qm refactor; C=$(git rev-parse HEAD); }

prep

# 1. moving is not blocked but shows
move
check "1: the commit succeeds" test $rc = 0
check "1: the warning holds K" err_has "    $K offer src/stock.c (no code under its scope) (agent-a)"
todo_k
check "1: K is ?missing" out_has "provider src/stock.c ?missing"
# mini: events of B and A (H8)

# 2. writing is not blocked (mini: the demand's provider is stock2.c; stock.c is gone)
tick; expect_ok "2: a future demand" ait future demand src/order.c src/stock2.c specs/p.yaml -m m
# mini: `ait contract push origin` (E_STATE, --force E_CODE_REQUIRED) is not in mini

# 2a. with no code to check, the gate does not look at results
GD=$(git rev-parse --git-dir)
QB=$(cat "$GD/ait/check-request.yaml" 2>/dev/null); WB=$(cat "$GD/ait/check-result.yaml" 2>/dev/null)
before=$(snapshot)
expect_err "2a: empty amend" E_CODE_REQUIRED ait amend $K -m x
check "2a: unchanged" test "$(snapshot)" = "$before"
check "2a: no task lines" not out_matches '^(task|check-request|check-result) '
check "2a: Q and W are as before" test "$(cat "$GD/ait/check-request.yaml" 2>/dev/null)/$(cat "$GD/ait/check-result.yaml" 2>/dev/null)" = "$QB/$WB"
check "2a: no code-first hint (no code in the work tree)" not err_has "stage 'src/stock.c'"
# mini: hint basic form only (`ait amend <K> <new scope> - -m <why>`, `ait future amend <K> -m <why>`)
tick; expect_err "2a: --force is the same" E_CODE_REQUIRED ait amend $K -m x --force
check "2a: --force unchanged" test "$(snapshot)" = "$before"

# 3. todo (mini: `ait todo src/stock.c` -> `ait todo <K>`)
todo_k
check "3: state current" out_has "state current"
check "3: provider src/stock.c ?missing" out_has "provider src/stock.c ?missing"

# 4. mini: notifications

# 5. move the contract
expect_err "5: amend K to stock2.c" E_NEEDS_RESULTS ait amend $K src/stock2.c - -m "stock2로 옮김"
check "5: the task is K's" test "$(grep '^task ' "$T/out")" = "task $K:specs/nobroken.yaml exec"
tick; with_r pass ait amend $K src/stock2.c - -m "stock2로 옮김"
check "5: changed K" out_has "changed $K"
check "5: check K reported" out_has "check $K reported"
check "5: Ait-Check: K reported <pins>" trailer "Ait-Check: $K reported $(pins HEAD $K)"
todo_k
check "5: provider src/stock2.c, no ?" out_has "provider src/stock2.c"
check "5: not unverified" not grep -q unverified "$T/out"

# 6. change in place and check again
setl src/stock2.c 3 s3x; git add src/stock2.c; tick; git commit -qm c6 2>/dev/null
todo_k
check "6: K is ?changed" out_has "provider src/stock2.c ?changed"
setl src/stock2.c 3 s3; git add src/stock2.c; tick; run git commit -qm back
todo_k
check "6: restored: no ?" out_has "provider src/stock2.c"
check "6: restored: not unverified" not grep -q unverified "$T/out"
setl src/stock2.c 3 s3y; git add src/stock2.c; tick; git commit -qm c6b 2>/dev/null
expect_err "6: amend K in place" E_NEEDS_RESULTS ait amend $K src/stock2.c - -m m
W=$(out_value check-result); fill "$W" pass $K:specs/nobroken.yaml=fail
before=$(snapshot)
expect_err "6: a failed check" E_CHECK_FAILED ait amend $K src/stock2.c - -m m -r "$W"
check "6: unchanged" test "$(snapshot)" = "$before"
check "6: hint: commit code that meets it first" err_has "ait commit -m <why>"
# mini: the reopen hint `ait future amend <K> src/stock2.c - -m <why>` (basic hints only)
todo_k
check "6: K stays unverified" out_has "unverified yes"
tick; with_r pass ait amend $K src/stock2.c - -m m
check "6: changed K" out_has "changed $K"
check "6: check K reported" out_has "check $K reported"
todo_k
check "6: no ?" out_has "provider src/stock2.c"
check "6: Ait-Check verified" trailer "Ait-Check: $K reported $(pins HEAD $K)"

# 7. checking in a request commit
tick; run ait future offer src/stock2.c specs/pass.yaml as o7 -m m
O7=$(id_of o7)
setl src/stock2.c 5 s5x; git add src/stock2.c
expect_err "7: fulfill O7" E_NEEDS_RESULTS ait fulfill $O7 -m m
check "7: touch K" out_has "touch $K"
tick; with_r pass ait fulfill $O7 -m m
check "7: check K reported" out_has "check $K reported"
todo_k
check "7: K has no ?" out_has "provider src/stock2.c"
# mini: "no change event" is about notifications

# 8. a change coming in by merge
prep
git branch bb $BEFORE_R1
git switch -q bb; setl src/stock.c 3 s3x; tick; git commit -qam bb; git switch -q master
tick; run git merge --no-edit bb
check "8: merge ok" test $rc = 0
todo_k
check "8: K is ?changed" out_has "provider src/stock.c ?changed"
check "8: K is unverified" out_has "unverified yes"

# 9-11, 13-16. mini: notifications, peek history, contract pull

# 12. withdraw without moving
prep
move
tick; expect_err "12: future drop" MINI ait future drop $K as x -m m

done_testing
