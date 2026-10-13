#!/bin/sh
# T43 ait commit과 커밋 안내 (1.5b, 7.1.2, 7.7.3-4, 7.7a)
#
# not yet: `ait peek` is replaced by `ait todo ID` and rec(ID)'s check line.
. "$(dirname "$0")/lib.sh"

id_of() { sed -n "s/^new \([^ ]*\) $1\$/\1/p" "$T/out"; }
setl() { f=$1 l=$2; shift 2; awk -v n="$l" -v t="$*" 'NR==n{print t; next}1' "$f" >"$f.tmp" && cat "$f.tmp" >"$f" && rm "$f.tmp"; }
# delete lines S..E of FILE
dell() { awk -v s="$2" -v e="$3" 'NR<s||NR>e' "$1" >"$1.tmp" && cat "$1.tmp" >"$1" && rm "$1.tmp"; }
# move lines 1-10 of src/stock.c to a new file src/stock2.c, staged
move10() { sed -n 1,10p src/stock.c >src/stock2.c; dell src/stock.c 1 10; git add src/stock.c src/stock2.c; }
not() { ! "$@"; }

prep() {
	setup
	A=$(ait session open --agent agent-a)
	exec_def nobroken; prompt_defs
	tick; with_r pass ait offer src/stock.c:1-10 specs/nobroken.yaml as k -m m
	K=$(id_of k)
	tick; run ait future offer src/order.c specs/p.yaml as o -m m
	O=$(id_of o)
	[ -n "$K" ] && [ -n "$O" ] && say_ok "prep: K and O" || say_fail "prep: K and O"
}

prep

# 1. nothing touched: a plain commit
setl src/order.c 1 o1x; git add src/order.c
H=$(git rev-parse HEAD)
tick; expect_ok "1: ait commit" ait commit -m "주문 정리"
check "1: no output" test ! -s "$T/out"
check "1: a new commit on HEAD" test "$(git rev-parse HEAD^)" = "$H"
check "1: subject" test "$(git log -1 --format=%s)" = "주문 정리"
check "1: no trailers" test -z "$(git log -1 --format='%(trailers)')"
check "1: HEAD tree is the index" test "$(git rev-parse HEAD^{tree})" = "$(git write-tree)"

# 2. nothing changed: noop
before=$(snapshot)
expect_ok "2: ait commit with nothing staged" ait commit -m x
check "2: noop" out_has "noop"
check "2: unchanged" test "$(snapshot)" = "$before"

# 3. a touched contract is checked with it
setl src/stock.c 5 s5x; git add src/stock.c
before=$(snapshot)
expect_err "3: ait commit needs results" E_NEEDS_RESULTS ait commit -m "재고 정리"
check "3: unchanged" test "$(snapshot)" = "$before"
Q=$(out_value check-request) W=$(out_value check-result)
check "3: the report" test "$(cat "$T/out")" = "$(printf 'provider %s src/stock.c:1-10 ?changed\ntouch %s\n\ntask %s:specs/nobroken.yaml exec\ncheck-request %s\ncheck-result %s\nready yes' $K $K $K "$Q" "$W")"
fill "$W" pass
tick; run ait commit -m "재고 정리" -r "$W"
check "3: ok" test $rc = 0
check "3: output" test "$(cat "$T/out")" = "$(printf 'request %s\ncheck %s reported' "$(git log -1 --format='%(trailers:key=Ait-Request,valueonly)')" $K)"
check "3: the commit changes stock.c and .ait/K" test "$(git diff --name-only HEAD^ HEAD)" = "$(printf '.ait/%s\nsrc/stock.c' $K)"
check "3: subject has no prefix" test "$(git log -1 --format=%s)" = "재고 정리"
check "3: the results are in the message" sh -c "git log -1 --format=%B | grep -qF 'outcome: pass'"
check "3: K's check line is reported <pins>" [ "$(check_line HEAD $K)" = "reported $(pins HEAD $K)" ]
check "3: K is pinned again" test "$(rec $K | grep '^provider ')" = "provider src/stock.c:1-10 $(blob HEAD:src/stock.c)"
expect_ok "3: todo K" ait todo $K
check "3: K has no ?" out_has "provider src/stock.c:1-10"
check "3: K is not unverified" not grep -q unverified "$T/out"
# not yet: `ait peek <K>`'s last event `verified check reported` needs peek (the check line above is step 3's own)

# 4. trivial: --force
setl src/stock.c 6 s6x; git add src/stock.c
tick; run ait commit -m 오타 --force
check "4: ok" test $rc = 0
check "4: check K forced" out_has "check $K forced"
check "4: no result paragraph" not sh -c "git log -1 --format=%B | grep -q '^results:'"
# not yet: `ait peek <K>`'s last event `verified check forced` -> K's check line
check "4: K's check line is forced <pins>" [ "$(check_line HEAD $K)" = "forced $(pins HEAD $K)" ]
check "4: K is pinned again" test "$(rec $K | grep '^provider ')" = "provider src/stock.c:1-10 $(blob HEAD:src/stock.c)"

# 5. a staged spec file: E_MIXED
echo '# x' >>tests/nobroken.sh; git add tests/nobroken.sh
setl src/stock.c 9 s9x; git add src/stock.c
expect_err "5: ait commit with a spec file staged" E_MIXED ait commit -m x
check "5: mixed tests/nobroken.sh" out_has "mixed tests/nobroken.sh"
check "5: ready no" out_has "ready no"
git reset -q --hard

# 6. the guidance of git commit (7.1.2; warning and contract lines exact, hints by command and ID)
setl src/stock.c 7 s7x; git add src/stock.c
tick; run git commit -qm raw
check "6: git commit succeeds" test $rc = 0
check "6: stderr is the 7.1.2 block" test "$(cat "$T/err")" = "$(printf '%s\n' \
	"warning: this commit leaves 1 current contract(s) unverified" \
	"    $K offer src/stock.c:1-10 (agent-a)" \
	"        nobroken" \
	"        \$ ./tests/nobroken.sh" \
	"warning: their receivers are notified after this commit" \
	"hint: next time, to check and commit in one step, use 'ait commit' instead of 'git commit'" \
	"hint: to change '$K' without a check, reopen it: 'ait future amend $K -m <why>'" \
	"hint: check '$K' again: run 'ait amend $K -m <why>'")"
setl src/order.c 2 o2x; git add src/order.c
tick; run git commit -qm raw2
check "6: a commit of future O's provider only: no guidance" test ! -s "$T/err"

# 7. a consumer is not watched (T43 step 7: src/order.c:1-5 and src/stock.c:40-50)
tick; with_r pass ait demand src/order.c:1-5 src/stock.c:40-50 specs/p.yaml as d -m m
D=$(id_of d)
check "7: demand registered" test $rc = 0 -a -n "$D"
setl src/order.c 3 o3x; git add src/order.c
tick; run git commit -qm consumer
check "7: no guidance" test ! -s "$T/err"
# not yet: `ait peek now <D>` -> `ait todo <D>`
expect_ok "7: todo D" ait todo $D
check "7: D's consumer has no ?" out_has "consumer src/order.c:1-5"
check "7: D is not unverified" not grep -q unverified "$T/out"

# 8. an ait commit that moves the code must move the contract (T43 step 8: lines 1-10 to src/stock2.c)
prep
move10
before=$(snapshot)
expect_err "8: ait commit" E_CODE_REQUIRED ait commit -m 이동
check "8: unchanged" test "$(snapshot)" = "$before"
check "8: provider K ?deleted" out_has "provider $K src/stock.c:1-10 ?deleted"
check "8: code-required K" out_has "code-required $K"
check "8: touch K" out_has "touch $K"
check "8: ready no" out_has "ready no"
check "8: no id, unverified or task lines" not out_matches '^(id|unverified|task|check-request|check-result) '
# T43 step 8: no piece is left, so no KEPT (5.9a)
check "8: hint: rerun with '<K>=<new scope>'" err_has "rerun with '$K=<new scope>'"
check "8: reopen hint" err_has "'ait future amend $K -m <why>'"
# not yet: the withdraw hint (`ait future drop <K> as x -m <why>`, then 'ait fulfill <X> -m <why>') needs future drop
tick; expect_err "8: --force is the same" E_CODE_REQUIRED ait commit -m 이동 --force
check "8: --force unchanged" test "$(snapshot)" = "$before"
check "8: --force: the same hints" sh -c "grep -qF \"rerun with '$K=<new scope>'\" '$T/err' && grep -qF \"'ait future amend $K -m <why>'\" '$T/err'"
expect_err "8: with K moved, a failed check" E_NEEDS_RESULTS ait commit $K=src/stock2.c:1-10 -m 이동
W=$(out_value check-result); fill "$W" fail
before=$(snapshot)
expect_err "8: fail" E_CHECK_FAILED ait commit $K=src/stock2.c:1-10 -m 이동 -r "$W"
check "8: fail unchanged" test "$(snapshot)" = "$before"
# T43 step 8: the reopened K is not touched, so the rerun drops 'K=' (7.7.1)
check "8: fail: reopen hint, then rerun 'ait commit -m <why>'" err_has "'ait future amend $K -m <why>', then rerun 'ait commit -m <why>'"
tick; with_r pass ait commit $K=src/stock2.c:1-10 -m 이동
check "8: ok" test $rc = 0
check "8: request R" out_matches '^request R[0-9a-f]{12}$'
check "8: check K reported" out_has "check $K reported"
check "8: K's provider is stock2.c:1-10" test "$(rec $K | grep '^provider ')" = "provider src/stock2.c:1-10 $(blob HEAD:src/stock2.c)"
# not yet: `ait peek now <K>` -> `ait todo <K>`
expect_ok "8: todo K" ait todo $K
check "8: K is not unverified" not grep -q unverified "$T/out"

# 9. without a session
prep
setl src/order.c 1 o1x; git add src/order.c
tick; expect_ok "9: nothing to check: no session needed" env AIT_SESSION=- ait commit -m x
setl src/stock.c 2 s2x; git add src/stock.c
before=$(snapshot)
tick; expect_err "9: something to check: E_SESSION" E_SESSION env AIT_SESSION=- ait commit -m y
check "9: unchanged" test "$(snapshot)" = "$before"

# 10. moving with a fulfill (T43 step 10: K=src/stock2.c:1-10)
prep
move10
setl src/order.c 1 o1x; git add src/order.c
tick; with_r pass ait fulfill $O $K=src/stock2.c:1-10 -m m
check "10: ok" test $rc = 0
check "10: check K reported" out_has "check $K reported"
check "10: K's provider is stock2.c:1-10" test "$(rec $K | grep '^provider ')" = "provider src/stock2.c:1-10 $(blob HEAD:src/stock2.c)"
expect_err "10: fulfill with no selected target" E_USAGE ait fulfill $K=src/stock2.c:1-10 -m m

# 11. T43 step 11: two diffs align the line range differently; the pin decides (1.5b, 3.10, 3.11)
prep
printf '%s\n' c a b b c c a b >src/f.c; git add src/f.c; tick; git commit -qm f
tick; with_r pass ait offer src/f.c:4-4 specs/p.yaml as f -m m
F=$(id_of f)
check "11: F registered" test $rc = 0 -a -n "$F"
setl src/f.c 2 b; git add src/f.c
tick; run git commit -qm f2
check "11: line 2 of f.c: no warning" test ! -s "$T/err"
# `a` inserted after line 1
printf '%s\n' c a b b b c c a b >src/f.c; git add src/f.c
before=$(snapshot)
expect_err "11: ait commit needs results" E_NEEDS_RESULTS ait commit -m x
check "11: unchanged" test "$(snapshot)" = "$before"
check "11: touch F" out_has "touch $F"
check "11: task F prompt" out_has "task $F:specs/p.yaml prompt"
tick; with_r pass ait commit -m x
check "11: ok" test $rc = 0
check "11: request R" out_matches '^request R[0-9a-f]{12}$'
check "11: check F reported" out_has "check $F reported"
# not yet: `ait peek now <F>` -> `ait todo <F>`
expect_ok "11: todo F" ait todo $F
check "11: F is not unverified" not grep -q unverified "$T/out"

done_testing
