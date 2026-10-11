#!/bin/sh
# T45 되돌리기: 확정된 코드를 여러 커밋에 걸쳐 바꾸기 (1.5d, 10.4, 9.1)
#
# not yet: no listen/sub (notifications), no peek.
# - Event expectations are left out.
# - `ait peek` is replaced by rec(ID), the commit's trailers and `ait todo ID`.
. "$(dirname "$0")/lib.sh"

id_of() { sed -n "s/^new \([^ ]*\) $1\$/\1/p" "$T/out"; }
setl() { f=$1 l=$2; shift 2; awk -v n="$l" -v t="$*" 'NR==n{print t; next}1' "$f" >"$f.tmp" && cat "$f.tmp" >"$f" && rm "$f.tmp"; }
not() { ! "$@"; }
block_ids() { sed -n 's/^id //p' "$T/out" | tr '\n' ' '; }
st() { rec "$1" | sed -n 's/^state //p'; }
merging() { git rev-parse -q --verify MERGE_HEAD >/dev/null; }
# pulled C ID...: stdout is `included ID from 'C'` for each ID, in ID order (7.12)
pulled() { c=$1; shift; test "$(cat "$T/out")" = "$(for i in "$@"; do echo "included $i from '$c'"; done | LC_ALL=C sort)"; }

# prep (T45): wa(A) and wb(B); A: future demand D; B pulls it, offers O undertaking D and
# fulfills it; A pulls that
prep() {
	setup
	git worktree add -q "$T/wa" -b a; git worktree add -q "$T/wb" -b b
	cd "$T/wa"; A=$(ait session open --agent agent-a)
	cd "$T/wb"; B=$(ait session open --agent agent-b)
	cd "$T/wa"; prompt_defs
	tick; run ait future demand src/order.c src/stock.c specs/p.yaml as d -m d
	D=$(id_of d)
	cd "$T/wb"; tick; run ait contract pull $D; pulled_d=$rc
	exec_def pass
	tick; run ait future offer src/stock.c specs/pass.yaml as o undertake $D o -m o
	O=$(id_of o)
	setl src/stock.c 1 s1x; git add src/stock.c
	tick; with_r pass ait fulfill $O -m f
	cd "$T/wa"; tick; run ait contract pull $D $O; pulled_do=$rc
	[ -n "$D" ] && [ -n "$O" ] && [ "$pulled_d/$pulled_do" = 0/0 ] && [ "$(st $D)" = current ] && [ "$(st $O)" = current ] &&
		[ "$(git rev-parse HEAD)" = "$(git -C "$T/wb" rev-parse HEAD)" ] &&
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
# not yet: `ait peek now src/stock.c` (empty), peek's `reopened` event and A's event (H14) are not checked

# 2. changes while reopened are no accident
move_three
check "2: no warning on the three commits" test -z "$E1$E2$E3"
expect_ok "2: B's todo" ait todo
check "2: B's todo has O" sh -c "grep -qx 'id $O' '$T/out'"
check "2: H10 for O" err_has "ait fulfill $O -m <why>"
# T45-2 (1.3, 1.5d, 7.8.1): D, created by A, is B's promise too since B reopened it; H10, not H5 (5.9a)
check "2: B's todo has D" sh -c "grep -qx 'id $D' '$T/out'"
check "2: H10 for D" err_has "ait fulfill $D -m <why>"
check "2: no H5 for D" not err_has "undertake $D o"

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
RR=$(git rev-parse HEAD)
cd "$T/wa"; tick; run ait contract pull $D $O
check "4: A pulls D O" test $rc = 0
check "4: included D and O from Rr's commit (T45-4)" pulled $RR $D $O
move_three
tick; run ait future cancel $O -m 중단
check "4: ok" test $rc = 0
check "4: stdout is request and changed O" test "$(cat "$T/out")" = "$(printf 'request %s\nchanged %s' "$(git log -1 --format='%(trailers:key=Ait-Request,valueonly)')" $O)"
check "4: O is cancelled" test "$(st $O)" = cancelled
check "4: D stays future" test "$(st $D)" = future
RX=$(git rev-parse HEAD)
# not yet: A's Rx event is not checked
cd "$T/wa"; tick; run ait contract pull $O
check "4: A pulls O" test $rc = 0
check "4: included O from Rx's commit (T45-4)" pulled $RX $O
check "4: the code commits of 2 come along" test -f src/inventory.c
check "4: O is cancelled and D is future after it" test "$(st $O) $(st $D)" = "cancelled future"

# 5. a contract not reopened is still an accident (T45-5: A's K on src/stock.c:40-50, pulled by B)
prep
cd "$T/wa"; tick; with_r pass ait offer src/stock.c:40-50 specs/p.yaml as k -m k
K=$(id_of k)
# not yet: `ait sub <K>`
cd "$T/wb"; tick; run ait contract pull $K
check "5: B pulls K" test $rc = 0 -a -n "$K"
reopen_od
move_three
check "5: the commit changing stock.c warns" sh -c 'printf "%s\n" "$1" | grep -qxF "warning: this commit leaves 1 current contract(s) unverified"' _ "$E3"
check "5: the warning holds K only" test "$(printf '%s\n' "$E1" "$E2" "$E3" | grep -E '^    [DOX][0-9a-f]{12} ' | sed 's/^    \([^ ]*\) .*/\1/' | sort -u)" = "$K"
# not yet: A's change event is not checked

# 6. a current D undertaken by a current O is not reopened alone
prep
cd "$T/wb"
before=$(snapshot)
expect_err "6: future amend D alone" E_STATE ait future amend $D -m 되돌림
check "6: unchanged" test "$(snapshot)" = "$before"
check "6: hint: reopen them in the same request" err_has "'ait future amend $D future amend $O -m <why>'"
tick; run ait future amend $D future amend $O -m 되돌림
check "6: ok" test $rc = 0
RD=$(git log -1 --format='%(trailers:key=Ait-Request,valueonly)') RDC=$(git rev-parse HEAD)
first=$O second=$D; [ "$D" \< "$O" ] && first=$D second=$O
check "6: stdout is request and two changed lines in ID order" test "$(cat "$T/out")" = "$(printf 'request %s\nchanged %s\nchanged %s' $RD $first $second)"
check "6: D and O are future" test "$(st $D) $(st $O)" = "future future"
expect_ok "6: B's todo" ait todo
check "6: B's todo has O" sh -c "grep -qx 'id $O' '$T/out'"
# T45-6 (1.5d, 7.8.1): D, reopened by B, is B's promise
check "6: B's todo has D" sh -c "grep -qx 'id $D' '$T/out'"
cd "$T/wa"; tick; run ait contract pull $D $O
check "6: A pulls D O" test $rc = 0
check "6: included D and O from Rd's commit (T45-6)" pulled $RDC $D $O
tick; expect_ok "6: A changes D's condition" ait future amend $D - - specs/low.yaml -m 조건
# not yet: B's event (reasons, H3) is not checked
# T45-6, direct writing (5.8 E_STATE, 5.9a): fixed in the request; run after the rest of 6, which needs Rd
prep
cd "$T/wb"
sed 's/^state current$/state future/' .ait/$D >"$T/rec" && cat "$T/rec" >.ait/$D
expect_ok "6: direct writing: contract add .ait/D" ait contract add .ait/$D
before=$(snapshot)
tick; expect_err "6: direct writing: contract commit" E_STATE ait contract commit -m 되돌림
check "6: direct writing: unchanged" test "$(snapshot)" = "$before"
check "6: direct writing: hint sets state future in .ait/O too, then contract add and commit" sh -c "
	grep -F \"set 'state future' in \" '$T/err' | grep -F \"'.ait/$O'\" | grep -F 'ait contract add' | grep -qF 'ait contract commit -m <why>'"
check "6: direct writing: no separate future amend" not err_has "ait future amend"
sed 's/^state current$/state future/' .ait/$O >"$T/rec" && cat "$T/rec" >.ait/$O
expect_ok "6: direct writing: contract add .ait/O" ait contract add .ait/$O
tick; expect_ok "6: direct writing: contract commit as the hint says" ait contract commit -m 되돌림
check "6: direct writing: changed D and changed O" test "$(grep '^changed ' "$T/out" | sort)" = "$(printf 'changed %s\nchanged %s\n' $D $O | sort)"
check "6: direct writing: D and O are future" test "$(st $D) $(st $O)" = "future future"

# 7. a reopened demand: creator, reopener, others (T45-7: D8 on src/stock.c:40-50)
prep
cd "$T/wa"
tick; with_r pass ait demand src/order.c src/stock.c:40-50 specs/p.yaml as d8 -m d8
D8=$(id_of d8)
check "7: D8 current" test "$(st $D8)" = current
cd "$T/wb"; tick; run ait contract pull $D8
check "7: B pulls D8" test $rc = 0
tick; run ait future amend $D8 -m 되돌림
check "7: B reopens D8" test $rc = 0 -a "$(st $D8)" = future
R8RC=$(git rev-parse HEAD)
# not yet: A's event (H14) and B's `ait peek src/stock.c` (H11) are not checked
cd "$T/wa"; tick; run ait contract pull $D8
check "7: A pulls D8" test $rc = 0
check "7: included D8 from R8r's commit (T45-7)" pulled $R8RC $D8
expect_ok "7: A's todo" ait todo
check "7: A's todo has D8" sh -c "grep -qx 'id $D8' '$T/out'"
check "7: H14 for D8" err_has "'$D8' was reopened by 'agent-b'; develop against it, or, if it is no longer needed, run 'ait future cancel $D8 -m <why>'"
check "7: no H5 for D8" not err_has "undertake $D8 o"
tick; expect_ok "7: A changes D8's condition" ait future amend $D8 - - specs/low.yaml -m 조건
# not yet: events, `ait peek src/stock.c` (H11) are not checked

# 8. reopening someone else's offer
prep
cd "$T/wa"
tick; run ait future amend $D future amend $O -m 되돌림
check "8: A reopens D and O" test $rc = 0
RAC=$(git rev-parse HEAD)
expect_ok "8: A's todo" ait todo
check "8: A's todo has D" sh -c "grep -qx 'id $D' '$T/out'"
check "8: H10 for D" err_has "ait fulfill $D -m <why>"
# T45-8 (1.3, 1.5d, H10): O, created by B, is A's promise since A reopened it
check "8: A's todo has O" sh -c "grep -qx 'id $O' '$T/out'"
check "8: H10 for O" err_has "ait fulfill $O -m <why>"
cd "$T/wb"; tick; run ait contract pull $D $O
check "8: B pulls D O" test $rc = 0
check "8: included D and O from Ra's commit (T45-8)" pulled $RAC $D $O
expect_ok "8: B's todo" ait todo
check "8: B's todo has O" sh -c "grep -qx 'id $O' '$T/out'"
# T45-8 (5.9a H14): B made O and A reopened it, so H14 and no H10 (fulfilling it is the reopener's promise)
check "8: H14 for O" err_has "'$O' was reopened by 'agent-a'; develop against it, or, if it is no longer needed, run 'ait future cancel $O -m <why>'"
check "8: no H10 for O" not err_has "ait fulfill $O"
setl src/stock.c 2 s2x; git add src/stock.c
tick; with_r pass ait fulfill $D -m f
check "8: B fulfils D alone" out_has "fulfilled $D"
check "8: O stays future" test "$(st $O)" = future

# 9. several reopeners (T45-9): the merge of two reopenings of the same versions is no reopening commit
prep
cd "$T/wa"; tick; run ait future amend $D future amend $O -m 되돌림
check "9: A reopens D and O (Ra)" test $rc = 0
cd "$T/wb"; tick; run ait future amend $D future amend $O -m 되돌림
check "9: B reopens D and O (Rb)" test $rc = 0
check "9: Ra and Rb make the same record versions" test "$(git rev-parse a:.ait/$D a:.ait/$O)" = "$(git rev-parse b:.ait/$D b:.ait/$O)"
tick; run git merge --no-edit a
check "9: wb merges a without conflict" test $rc = 0
check "9: it is a merge commit" sh -c 'git rev-parse -q --verify HEAD^2 >/dev/null'
check "9: the merge commit does not reopen D or O (7.1.5)" not err_has "is reopened"
cd "$T/wa"; run git merge -q --ff-only b
check "9: wa fast-forwards to b" test $rc = 0
# T45-9 (1.5d, 7.8.1): agent-a and agent-b both reopened D and O, so each has the other's contract too
expect_ok "9: A's todo" ait todo
check "9: A's todo has O" sh -c "grep -qx 'id $O' '$T/out'"
cd "$T/wb"; expect_ok "9: B's todo" ait todo
check "9: B's todo has D" sh -c "grep -qx 'id $D' '$T/out'"

# 10. a `-s ours` integration dropping the fulfilment reopens (T45-10, 7.1.3-5, 7.1.5)
prep
cd "$T/wa"; tick; run ait future amend $O future amend $D -m 되돌림
check "10: A reopens O and D (Ra)" test $rc = 0
RA=$(git rev-parse HEAD) RAR=$(git log -1 --format='%(trailers:key=Ait-Request,valueonly)')
cd "$T/wb"; tick; run ait contract pull $D $O
check "10: B pulls D O" test $rc = 0
setl src/stock.c 2 s2x; git add src/stock.c
tick; with_r pass ait fulfill $O -m f
check "10: fulfilled O" out_has "fulfilled $O"
check "10: fulfilled D via O" out_has "fulfilled $D via $O"
RF=$(git rev-parse HEAD) RFR=$(git log -1 --format='%(trailers:key=Ait-Request,valueonly)')
cd "$T/wa"; tick; run git merge -s ours --no-ff --no-edit b
check "10: the merge stops (exit != 0)" test $rc != 0
check "10: HEAD unchanged" test "$(git rev-parse HEAD)" = "$RA"
check "10: the merge stays in progress" merging
check "10: first line: this merge needs its records pinned and validated again" sh -c "head -1 '$T/err' | grep -qxF 'error: not allowed: this merge needs its records pinned and validated again'"
check "10: hint: finish it with the finishing commit" err_has "finish it with 'git commit --allow-empty --no-edit --cleanup=strip' (its pre-commit hook must run 'ait hook pre-commit')"
tick; run git commit --allow-empty --no-edit --cleanup=strip
check "10: Rm commits" test $rc = 0
check "10: Rm merges b" test "$(git rev-parse HEAD^2)" = "$RF"
check "10: O is Ra's version but its prev" test "$(rec $O | grep -v '^prev ')" = "$(git show $RA:.ait/$O | grep -v '^prev ')"
check "10: D is Ra's version but its prev" test "$(rec $D | grep -v '^prev ')" = "$(git show $RA:.ait/$D | grep -v '^prev ')"
check "10: O's prev is Rf's blob, one line" test "$(rec $O | sed -n 's/^prev //p')" = "$(blob $RF:.ait/$O)"
check "10: D's prev is Rf's blob, one line" test "$(rec $D | sed -n 's/^prev //p')" = "$(blob $RF:.ait/$D)"
RM=$(git log -1 --format='%(trailers:key=Ait-Request,valueonly)')
check "10: Rm has a new Ait-Request" sh -c "[ -n '$RM' ] && [ '$RM' != '$RAR' ] && [ '$RM' != '$RFR' ]"
check "10: Rm's Ait-Agent is agent-a" test "$(git log -1 --format='%(trailers:key=Ait-Agent,valueonly)')" = agent-a
check "10: D is reopened in this merge, keeping the future version" err_has "warning: '$D' is reopened in this merge: it keeps the future version over the current one"
check "10: O is reopened in this merge, keeping the future version" err_has "warning: '$O' is reopened in this merge: it keeps the future version over the current one"
check "10: H10 for D" err_has "ait fulfill $D -m <why>"
check "10: H10 for O" err_has "ait fulfill $O -m <why>"
# T45-10 (1.5d, 7.1.5): Rm's agent-a is the reopener of D and O, so both are A's promises
expect_ok "10: A's todo" ait todo
check "10: A's todo has D" sh -c "grep -qx 'id $D' '$T/out'"
check "10: A's todo has O" sh -c "grep -qx 'id $O' '$T/out'"
check "10: A's todo: H10 for D" err_has "ait fulfill $D -m <why>"
check "10: A's todo: H10 for O" err_has "ait fulfill $O -m <why>"
cd "$T/wb"; tick; with_r pass ait amend $O src/stock.c:1-3 - -m 좁힘
check "10: B narrows O (R10; O is current on b)" test $rc = 0 -a "$(st $O)" = current
# not yet: A's R10 event is not checked

# 11. the reopener of D gets the promise to fulfil D even if someone else's O undertakes it
setup
git worktree add -q "$T/wa" -b a; git worktree add -q "$T/wb" -b b
cd "$T/wa"; A=$(ait session open --agent agent-a)
cd "$T/wb"; B=$(ait session open --agent agent-b)
cd "$T/wa"; prompt_defs
tick; with_r pass ait demand src/order.c src/stock.c:40-50 specs/p.yaml as d -m d
D=$(id_of d)
cd "$T/wb"; tick; run ait contract pull $D
check "11: B pulls D" test $rc = 0
exec_def pass
tick; run ait future offer src/stock.c specs/pass.yaml as o undertake $D o -m 착수
O=$(id_of o)
cd "$T/wa"; tick; run ait contract pull $D $O
check "11: A pulls D O" test $rc = 0
tick; run ait future amend $D -m 되돌림
check "11: A reopens D alone (O is future)" test $rc = 0 -a "$(st $D)" = future
expect_ok "11: A's todo" ait todo
check "11: hint H10 for D" err_has "ait fulfill $D -m <why>"
check "11: no 'undertakes it; develop against it'" not err_has "undertakes it; develop against it"

done_testing
