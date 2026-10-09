#!/bin/sh
# T22 걸린 계약: 내 커밋이 바꾸는 current 계약을 함께 확인한다 (1.5b, 7.1.2, 7.7.3-4·5, 7.7.4, 7.7.6)
#
# mini: no line ranges. The scenario's K is `src/stock.c:1-10` and O is `src/stock.c`,
# and "not touching K" is a change to stock.c line 40. Here K keeps a whole-file scope
# `src/stock.c` and O moves to `src/order.c`, so "a change K's scope does not cover" is a
# change to order.c line 40. Steps whose point is the line range itself are left out.
. "$(dirname "$0")/lib.sh"

id_of() { sed -n "s/^new \([^ ]*\) $1\$/\1/p" "$T/out"; }
# setl FILE N TEXT: replace line N of FILE
setl() { f=$1 l=$2; shift 2; awk -v n="$l" -v t="$*" 'NR==n{print t; next}1' "$f" >"$f.tmp" && cat "$f.tmp" >"$f" && rm "$f.tmp"; }
# delete lines S..E of FILE
dell() { awk -v s="$2" -v e="$3" 'NR<s||NR>e' "$1" >"$1.tmp" && cat "$1.tmp" >"$1" && rm "$1.tmp"; }
not() { ! "$@"; }
has_line() { grep -qxF -- "$1" "$2"; }
no_line() { ! grep -qF -- "$1" "$2"; }
tasks() { grep '^task ' "$T/out"; }

prep() {
	setup
	A=$(ait session open --agent agent-a)
	exec_def nobroken; exec_def pass
	tick; with_r pass ait offer src/stock.c specs/nobroken.yaml as k future offer src/order.c specs/pass.yaml as o -m m
	K=$(id_of k) O=$(id_of o)
	[ -n "$K" ] && [ -n "$O" ] && say_ok "prep: K and O registered" || say_fail "prep: K and O registered"
}

prep

# 1. a change outside K's scope: only O's task, no touch
setl src/order.c 40 o40x; git add src/order.c
expect_err "1: fulfill O needs results" E_NEEDS_RESULTS ait fulfill $O -m m
check "1: the only task is O's" test "$(tasks)" = "task $O:specs/pass.yaml exec"
check "1: no touch line" no_line "touch " "$T/out"
git reset -q --hard

# 2. a change in K's scope: touch K, K's task is added
setl src/stock.c 5 BROKEN; git add src/stock.c
expect_err "2: fulfill O needs results" E_NEEDS_RESULTS ait fulfill $O -m m
check "2: report has touch K" out_has "touch $K"
check "2: tasks are K's and O's" test "$(tasks | sort)" = "$(printf 'task %s:specs/nobroken.yaml exec\ntask %s:specs/pass.yaml exec\n' $K $O | sort)"
W=$(out_value check-result)
expect_ok "2: todo O" ait todo $O
check "2: todo ends with the index block and touch K" test "$(tail -3 "$T/out")" = "$(printf 'index\ntouch %s\n' $K)"
fill "$W" pass $K:specs/nobroken.yaml=fail
before=$(snapshot)
expect_err "2: K's task fails" E_CHECK_FAILED ait fulfill $O -m m -r "$W"
check "2: unchanged after the failed check" test "$(snapshot)" = "$before"
check "2: hint: make the code meet the failed task(s)" err_has "make the code meet the failed task(s) and stage it, then rerun without -r to get the new tasks"
# mini: hint basic form only; the reopen hints (`ait future amend <K> -m <why>`,
# `ait future amend <K> - <new defs> -m <why>`) are not checked
fill "$W" pass $K:specs/nobroken.yaml=unknown
expect_err "2: unknown fails too" E_CHECK_FAILED ait fulfill $O -m m -r "$W"
check "2: unknown hint: check it where its environment is available" err_has "check it where its environment is available (see '$(dirname "$W")/check-request.yaml') and rerun with -r '$W'"
check "2: unknown hint: --force" err_has "if it is trivial, add --force (recorded as forced)"
check "2: unknown: no 'make the code meet'" not err_has "make the code meet"

# 3. mini: the step is about a line-range peek missing a boundary touch (peek and line ranges)

# 4. a plain commit is not blocked; it warns (7.1.2 block; warning and contract lines are normative)
tick; run git commit -qm wip
check "4: git commit succeeds" test $rc = 0
check "4: warning line" has_line "warning: this commit leaves 1 current contract(s) unverified" "$T/err"
check "4: contract line" has_line "    $K offer src/stock.c (agent-a)" "$T/err"
check "4: contract body line" has_line "        nobroken" "$T/err"
check "4: contract command line" has_line "        \$ ./tests/nobroken.sh" "$T/err"
check "4: receivers warning" has_line "warning: their receivers are notified after this commit" "$T/err"
check "4: block order" test "$(grep -v '^hint:' "$T/err")" = "$(printf 'warning: this commit leaves 1 current contract(s) unverified\n    %s offer src/stock.c (agent-a)\n        nobroken\n        $ ./tests/nobroken.sh\nwarning: their receivers are notified after this commit\n' $K)"
# mini: `ait peek now <K>` and `ait todo src/stock.c` -> `ait todo <K>` shows the same facts
expect_ok "4: todo K" ait todo $K
check "4: K is ?changed" out_has "provider src/stock.c ?changed"
check "4: K is unverified" out_has "unverified yes"
expect_err "4: fulfill O with nothing staged" E_NEEDS_RESULTS ait fulfill $O -m m
check "4: no touch line" no_line "touch " "$T/out"
check "4: only O's task" test "$(tasks)" = "task $O:specs/pass.yaml exec"

# 5. mini: removing line 11 is outside K's 1-10 range; with a whole-file K it touches K, so the point is lost

# 6. a check pins K again
prep
setl src/stock.c 5 "s5 y"; git add src/stock.c
tick; with_r pass ait fulfill $O -m m
check "6: fulfill ok" test $rc = 0
check "6: check K reported" out_has "check $K reported"
check "6: check O reported" out_has "check $O reported"
check "6: rec(K) provider is pinned to HEAD" test "$(rec $K | grep '^provider ')" = "provider src/stock.c $(blob HEAD:src/stock.c)"
# mini: peek -> the Ait-Check trailer and todo
check "6: Ait-Check K reported <pins>" test -n "$(git log -1 --format=%B | grep -xF "Ait-Check: $K reported $(pins HEAD $K)")"
expect_ok "6: todo K" ait todo $K
check "6: K has no ?" out_has "provider src/stock.c"
check "6: K is not unverified" no_line "unverified" "$T/out"

# 7. a contract that already lost its scope is not touched
prep
git rm -q src/stock.c
tick; run git commit -qm rm
check "7: warning has (no code under its scope)" has_line "    $K offer src/stock.c (no code under its scope) (agent-a)" "$T/err"
setl src/order.c 50 o50x; git add src/order.c
expect_err "7: fulfill O needs results" E_NEEDS_RESULTS ait fulfill $O -m m
check "7: no touch K" no_line "touch $K" "$T/out"
check "7: no code-required K" no_line "code-required $K" "$T/out"
check "7: no K task" no_line "task $K:" "$T/out"
git reset -q --hard
for i in $(seq 1 5); do echo "n$i"; done >src/stock.c; git add src/stock.c
tick; run git commit -qm back
check "7: a later commit of stock.c does not warn about K" no_line "$K" "$T/err"

# 8. a contract losing its scope in this commit blocks the commit
prep
git rm -q src/stock.c
before=$(snapshot)
expect_err "8: fulfill O" E_CODE_REQUIRED ait fulfill $O -m m
check "8: unchanged" test "$(snapshot)" = "$before"
check "8: report provider K ?missing" out_has "provider $K src/stock.c ?missing"
check "8: report code-required K" out_has "code-required $K"
check "8: report touch K" out_has "touch $K"
check "8: report ready no" out_has "ready no"
check "8: no unverified line" no_line "unverified" "$T/out"
check "8: no task lines" not out_matches '^(task|check-request|check-result) '
# mini: hint basic form only; `rerun with '<K>=<new scope>'` and the reopen hint are not checked
check "8: no stage hint (nothing in the work tree)" not err_has "stage 'src/stock.c'"
# mini: the withdraw hint (`ait future drop <K> as x`) needs future drop
expect_ok "8: todo" ait todo
check "8: todo ends with index, code-required K, touch K" test "$(tail -4 "$T/out")" = "$(printf 'index\ncode-required %s\ntouch %s\n' $K $K)"
before=$(snapshot)
tick; expect_err "8: --force is the same" E_CODE_REQUIRED ait fulfill $O -m m --force
check "8: --force unchanged" test "$(snapshot)" = "$before"

# 8b. a new place: check K there
prep
git show HEAD:src/stock.c >src/stock2.c; git rm -q src/stock.c; git add src/stock2.c
expect_err "8b: fulfill O" E_CODE_REQUIRED ait fulfill $O -m m
tick; with_r pass ait fulfill $O $K=src/stock2.c -m m
check "8b: ok" test $rc = 0
check "8b: fulfilled O" out_has "fulfilled $O"
check "8b: check K reported" out_has "check $K reported"
check "8b: check O reported" out_has "check $O reported"
check "8b: rec(K) provider is stock2.c" test "$(rec $K | grep '^provider ')" = "provider src/stock2.c $(blob HEAD:src/stock2.c)"
expect_ok "8b: todo K" ait todo $K
check "8b: K has no ? and no unverified" sh -c "grep -qx 'provider src/stock2.c' '$T/out' && ! grep -q unverified '$T/out'"

done_testing
