#!/bin/sh
# T45 되돌리기: 확정된 코드를 여러 커밋에 걸쳐 바꾸기 (1.5d, 10.4, 9.1)
#
# mini: no listen/sub (notifications), no peek, no contract pull/add/commit, no line ranges.
# - `ait contract pull R` is replaced by `git merge` of the other worktree's branch (the way
#   spec/ait-mini.md says to receive), and event expectations are left out.
# - `ait peek` is replaced by rec(ID), the commit's trailers and `ait todo ID`.
# - "my promises" in mini count only contracts I created (spec/ait-mini.md, 한계), so
#   expectations that a contract reopened by me but created by someone else shows in my
#   `ait todo` are left out with a comment.
. "$(dirname "$0")/lib.sh"

id_of() { sed -n "s/^new \([^ ]*\) $1\$/\1/p" "$T/out"; }
setl() { f=$1 l=$2; shift 2; awk -v n="$l" -v t="$*" 'NR==n{print t; next}1' "$f" >"$f.tmp" && cat "$f.tmp" >"$f" && rm "$f.tmp"; }
not() { ! "$@"; }
block_ids() { sed -n 's/^id //p' "$T/out" | tr '\n' ' '; }
st() { rec "$1" | sed -n 's/^state //p'; }

# prep: wa(A) and wb(B); A: future demand D; B: takes it, offers O undertaking D and fulfills it; A takes that
prep() {
	setup
	git worktree add -q "$T/wa" -b a; git worktree add -q "$T/wb" -b b
	cd "$T/wa"; A=$(ait session open --agent agent-a)
	cd "$T/wb"; B=$(ait session open --agent agent-b)
	cd "$T/wa"; prompt_defs
	tick; run ait future demand src/order.c src/stock.c specs/p.yaml as d -m d
	D=$(id_of d)
	[ -n "${EXTRA_K-}" ] && { exec_def pass; tick; with_r pass ait offer src/stock.c specs/pass.yaml as k -m k; K=$(id_of k); }
	cd "$T/wb"; git merge -q --ff-only a
	exec_def pass
	tick; run ait future offer src/stock.c specs/pass.yaml as o undertake $D o -m o
	O=$(id_of o)
	setl src/stock.c 1 s1x; git add src/stock.c
	tick; with_r pass ait fulfill $O -m f
	cd "$T/wa"; git merge -q --ff-only b
	[ -n "$D" ] && [ -n "$O" ] && [ "$(st $D)" = current ] && [ "$(st $O)" = current ] &&
		say_ok "prep: D and O current on both sides" || say_fail "prep: D and O current on both sides"
}
# step 1: B reopens O and D
reopen_od() {
	cd "$T/wb"
	tick; run ait future amend $O future amend $D -m "inventory로 옮길 예정"
}
# step 2: B moves stock.c's logic to src/inventory.c in three plain commits
move_three() {
	cd "$T/wb"
	sed -n 1,20p src/stock.c >src/inventory.c; git add src/inventory.c
	tick; run git commit -qm move1; E1=$(cat "$T/err")
	sed -n 21,50p src/stock.c >>src/inventory.c; git add src/inventory.c
	tick; run git commit -qm move2; E2=$(cat "$T/err")
	echo 'call inventory' >src/stock.c; git add src/stock.c
	tick; run git commit -qm move3; E3=$(cat "$T/err")
}

prep

# 1. reopen
reopen_od
check "1: ok" test $rc = 0
check "1: changed D, changed O" test "$(grep '^changed ' "$T/out" | sort)" = "$(printf 'changed %s\nchanged %s\n' $D $O | sort)"
check "1: D is future" test "$(st $D)" = future
check "1: O is future" test "$(st $O)" = future
check "1: the link is still undertaken" test "$(git show HEAD:.ait/undertake/$D-$O | sed -n 's/^state //p')" = undertaken
check "1: the request is B's" test "$(git log -1 --format='%(trailers:key=Ait-Agent,valueonly)')" = "agent-b"
# mini: `ait peek now src/stock.c` (empty), peek's `reopened` event and A's event (H14) are not checked

# 2. changes while reopened are no accident
move_three
check "2: no warning on the three commits" test -z "$E1$E2$E3"
expect_ok "2: B's todo" ait todo
check "2: B's todo has O" sh -c "grep -qx 'id $O' '$T/out'"
check "2: H10 for O" err_has "ait fulfill $O -m <why>"
# mini: D (created by A, reopened by B) in B's todo and its H10 are not checked (my promises = created by me)

# 3. fulfil again
expect_err "3: fulfill O needs results" E_NEEDS_RESULTS ait fulfill $O -m 옮김
check "3: provider D src/stock.c (no ?changed: D is future)" out_has "provider $D src/stock.c"
check "3: provider O src/stock.c (no ?changed: O is future)" out_has "provider $O src/stock.c"
check "3: tasks D:p and O:pass" test "$(grep '^task ' "$T/out" | sort)" = "$(printf 'task %s:specs/p.yaml prompt\ntask %s:specs/pass.yaml exec\n' $D $O | sort)"
tick; with_r pass ait fulfill $O=src/inventory.c $D=src/inventory.c -m 옮김
check "3: ok" test $rc = 0
for l in "fulfilled $O" "fulfilled $D via $O" "check $D reported" "check $O reported"; do
	check "3: $l" out_has "$l"
done
check "3: O's provider is inventory.c" test "$(rec $O | grep '^provider ')" = "provider src/inventory.c $(blob HEAD:src/inventory.c)"
check "3: D's provider is inventory.c" test "$(rec $D | grep '^provider ')" = "provider src/inventory.c $(blob HEAD:src/inventory.c)"
expect_ok "3: todo O" ait todo $O
check "3: O is not unverified" not grep -q unverified "$T/out"

# 4. stopping while reopened leaves a promise
prep
reopen_od
cd "$T/wa"; tick; run git merge -q --ff-only b
check "4: A takes Rr (mini: git merge for contract pull)" test $rc = 0
move_three
tick; run ait future cancel $O -m 중단
check "4: ok" test $rc = 0
check "4: stdout is request and changed O" test "$(cat "$T/out")" = "$(printf 'request %s\nchanged %s' "$(git log -1 --format='%(trailers:key=Ait-Request,valueonly)')" $O)"
check "4: O is cancelled" test "$(st $O)" = cancelled
check "4: D stays future" test "$(st $D)" = future
# mini: A's event and `ait contract pull Rx` (E_MIXED and its hint) are not checked

# 5. a contract not reopened is still an accident
# mini: K's scope `src/stock.c:40-50` is the whole file `src/stock.c`
EXTRA_K=1; prep; EXTRA_K=
reopen_od
move_three
check "5: the commit changing stock.c warns" sh -c 'printf "%s\n" "$1" | grep -qxF "warning: this commit leaves 1 current contract(s) unverified"' _ "$E3"
check "5: the warning holds K only" test "$(printf '%s\n' "$E1" "$E2" "$E3" | grep -E '^    [DOX][0-9a-f]{12} ' | sed 's/^    \([^ ]*\) .*/\1/' | sort -u)" = "$K"
# mini: A's change event is not checked

# 6. a current D undertaken by a current O is not reopened alone
prep
cd "$T/wb"
before=$(snapshot)
expect_err "6: future amend D alone" E_STATE ait future amend $D -m 되돌림
check "6: unchanged" test "$(snapshot)" = "$before"
check "6: hint: reopen them in the same request" err_has "'ait future amend $D future amend $O -m <why>'"
tick; run ait future amend $D future amend $O -m 되돌림
check "6: ok" test $rc = 0
RD=$(git log -1 --format='%(trailers:key=Ait-Request,valueonly)')
first=$O second=$D; [ "$D" \< "$O" ] && first=$D second=$O
check "6: stdout is request and two changed lines in ID order" test "$(cat "$T/out")" = "$(printf 'request %s\nchanged %s\nchanged %s' $RD $first $second)"
check "6: D and O are future" test "$(st $D) $(st $O)" = "future future"
expect_err "6: direct writing" MINI ait contract add .ait/$D
expect_ok "6: B's todo" ait todo
check "6: B's todo has O" sh -c "grep -qx 'id $O' '$T/out'"
# mini: D (reopened by B, created by A) in B's todo is not checked
cd "$T/wa"; tick; run git merge -q --ff-only b
check "6: A takes Rd (mini: git merge)" test $rc = 0
tick; expect_ok "6: A changes D's condition" ait future amend $D - - specs/low.yaml -m 조건
# mini: B's event (reasons, H3) is not checked

# 7. a reopened demand: creator, reopener, others
prep
cd "$T/wa"
tick; with_r pass ait demand src/order.c src/stock.c specs/p.yaml as d8 -m d8
D8=$(id_of d8)
check "7: D8 current" test "$(st $D8)" = current
cd "$T/wb"; git merge -q --no-edit a
tick; run ait future amend $D8 -m 되돌림
check "7: B reopens D8" test $rc = 0 -a "$(st $D8)" = future
cd "$T/wa"; tick; run git merge -q --no-edit b
check "7: A takes R8r (mini: git merge)" test $rc = 0
expect_ok "7: A's todo" ait todo
check "7: A's todo has D8" sh -c "grep -qx 'id $D8' '$T/out'"
# mini: H14 (`'<D8>' was reopened by 'agent-b'; ... 'ait future cancel <D8> -m <why>'`) instead of H10
# needs the reopener (1.5d); mini gives the basic hint only
check "7: no H5 for D8" not err_has "undertake $D8 o"
tick; expect_ok "7: A changes D8's condition" ait future amend $D8 - - specs/low.yaml -m 조건
# mini: events, `ait peek src/stock.c` (H11) are not checked

# 8. reopening someone else's offer
prep
cd "$T/wa"
tick; run ait future amend $D future amend $O -m 되돌림
check "8: A reopens D and O" test $rc = 0
expect_ok "8: A's todo" ait todo
check "8: A's todo has D" sh -c "grep -qx 'id $D' '$T/out'"
check "8: H10 for D" err_has "ait fulfill $D -m <why>"
# mini: O (created by B, reopened by A) in A's todo and its H10 are not checked
cd "$T/wb"; tick; run git merge -q --no-edit a
check "8: B takes Ra (mini: git merge)" test $rc = 0
expect_ok "8: B's todo" ait todo
check "8: B's todo has O" sh -c "grep -qx 'id $O' '$T/out'"
# mini: H14 (`'<O>' was reopened by 'agent-a'; ...`) instead of H10 needs the reopener; basic hint only
setl src/stock.c 2 s2x; git add src/stock.c
tick; with_r pass ait fulfill $D -m f
check "8: B fulfils D alone" out_has "fulfilled $D"
check "8: O stays future" test "$(st $O)" = future

# 9. mini: who reopened (several reopeners) shows only through `ait todo` of contracts not created by me
# 10. mini: the `-s ours` merge needs the integration hook's prev and reopen (7.1.3-3·5, 7.1.5)

# 11. the reopener of D gets the promise to fulfil D even if someone else's O undertakes it
setup
git worktree add -q "$T/wa" -b a; git worktree add -q "$T/wb" -b b
cd "$T/wa"; A=$(ait session open --agent agent-a)
cd "$T/wb"; B=$(ait session open --agent agent-b)
cd "$T/wa"; prompt_defs
tick; with_r pass ait demand src/order.c src/stock.c specs/p.yaml as d -m d
D=$(id_of d)
cd "$T/wb"; git merge -q --ff-only a; exec_def pass
tick; run ait future offer src/stock.c specs/pass.yaml as o undertake $D o -m 착수
O=$(id_of o)
cd "$T/wa"; git merge -q --ff-only b
tick; run ait future amend $D -m 되돌림
check "11: A reopens D alone (O is future)" test $rc = 0 -a "$(st $D)" = future
expect_ok "11: A's todo" ait todo
check "11: hint H10 for D" err_has "ait fulfill $D -m <why>"
check "11: no 'undertakes it; develop against it'" not err_has "undertakes it; develop against it"

done_testing
