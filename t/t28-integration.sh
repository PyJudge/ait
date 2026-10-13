#!/bin/sh
# T28 통합: merge driver·해결·재방송·되돌리기 (7.1.3–7.1.5)
#
# not yet: no merge driver, no peek, no `contract push`, no `future drop`.
# Steps 1, 5 and 8 need the merge driver; 12, 16 and 19 need `future drop`; they are left
# out. 2a leaves out its `contract push`; in 24 and 25 the merge driver's hints are left
# out. Peek's events are not checked; `ait peek now` becomes `ait todo`. Also checked: a
# structurally broken merged record is rejected and the merge stays in progress (3), and a
# clean merge breaking validate is rejected by pre-merge-commit (7.1.4).
. "$(dirname "$0")/lib.sh"

id_of() { sed -n "s/^new \([^ ]*\) $1\$/\1/p" "$T/out"; }
setl() { f=$1 l=$2; shift 2; awk -v n="$l" -v t="$*" 'NR==n{print t; next}1' "$f" >"$f.tmp" && cat "$f.tmp" >"$f" && rm "$f.tmp"; }
not() { ! "$@"; }
merging() { git rev-parse -q --verify MERGE_HEAD >/dev/null; }
rec_has() { rec "$1" | grep -qxF -- "$2"; }
prevs() { rec "$1" | sed -n 's/^prev //p'; }
# oids REV:PATH...: the blobs in OID order, one per line (2.2.2's prev order)
oids() { for r; do git rev-parse "$r"; done | LC_ALL=C sort; }
rid() { git log -1 --format='%(trailers:key=Ait-Request,valueonly)' "${1:-HEAD}"; }
# new_rid REV...: HEAD has an Ait-Request and it is none of REV's
new_rid() { r=$(rid); [ -n "$r" ] || return 1; for c; do [ "$r" != "$(rid "$c")" ] || return 1; done; }
# set_provider PATH LINE: the provider line of record file PATH becomes LINE
# write_long L2 [L11]: tests/long.sh (0.1) with its lines 2 and 11 as given, for a resolution
write_long() { { echo '#!/bin/sh'; echo "$1"; for i in 3 4 5 6 7 8 9 10; do echo "# l$i"; done; echo "${2:-# l11}"; echo 'exit 0'; } >tests/long.sh; }
# unmerged: the paths git left unmerged, one line, sorted
unmerged() { git diff --name-only --diff-filter=U | LC_ALL=C sort | tr '\n' ' '; }
paths() { printf '%s\n' "$@" | LC_ALL=C sort | tr '\n' ' '; }
set_provider() { awk -v t="$2" '/^provider /{print t; next}1' "$1" >"$1.tmp" && cat "$1.tmp" >"$1" && rm "$1.tmp"; }

prep() {
	setup
	A=$(ait session open --agent agent-a)
	exec_def long
	tick; run ait future offer src/stock.c specs/long.yaml as o future demand src/order.c src/stock.c specs/long.yaml as d undertake d o -m base
	O=$(id_of o) D=$(id_of d)
	HBASE=$(git rev-parse HEAD)
	[ $rc = 0 ] && [ -n "$O" ] && [ -n "$D" ] && say_ok "prep: Hbase" || say_fail "prep: Hbase"
}
# a current offer K on src/stock.c:1-10 with p (T28-17, 21, 22, 24)
prep_k() {
	prep; prompt_defs; rm specs/low.yaml specs/w.yaml
	tick; with_r pass ait offer src/stock.c:1-10 specs/p.yaml as k -m k
	K=$(id_of k)
	[ $rc = 0 ] && [ -n "$K" ] && say_ok "prep: K" || say_fail "prep: K"
}

# 4. a merge does not ask for results (T28-4: K on src/stock.c:1-10)
prep
git branch bb
git switch -q bb
setl src/stock.c 3 BROKEN; tick; git commit -qam broken
git switch -q master
exec_def nobroken
tick; with_r pass ait offer src/stock.c:1-10 specs/nobroken.yaml as k -m m
K=$(id_of k)
check "4: K registered" test $rc = 0 -a -n "$K"
setl src/order.c 50 o50x; tick; git commit -qam other
REC_K=$(git rev-parse HEAD:.ait/$K)
tick; run git merge --no-edit bb
check "4: git merge succeeds" test $rc = 0
check "4: it is a merge commit" sh -c 'git rev-parse -q --verify HEAD^2 >/dev/null'
check "4: no merge in progress" not merging
check "4: K's record is unchanged (not reopened)" test "$(git rev-parse HEAD:.ait/$K)" = "$REC_K"
# T28-4: commit-msg warns of the new unverified K and gives H7 (7.1.5)
check "4: warning: K is unverified after this merge" err_has "warning: '$K' is unverified after this merge"
check "4: H7: ait amend K -m <why>" err_has "ait amend $K -m <why>"
# not yet: `ait peek now <K>` -> `ait todo <K>`
expect_ok "4: todo K" ait todo $K
check "4: K is ?changed" out_has "provider src/stock.c:1-10 ?changed"
check "4: K is unverified" out_has "unverified yes"

# 3. one side picked is a resolution too (T28-3: z1 and z2 amend O's provider to different ranges)
prep
git branch z1; git branch z2
git switch -q z2; tick; run ait future amend $O src/stock.c:6-9 - -m z2
check "z2: amend ok" test $rc = 0
git switch -q z1; tick; run ait future amend $O src/stock.c:1-5 - -m z1
check "z1: amend ok" test $rc = 0
Z1=$(git rev-parse HEAD)
tick; run git merge --no-edit z2
check "merge z2: conflicts" test $rc != 0
check "merge z2: merge in progress" merging
# a conflict resolved into a malformed record is rejected by pre-commit
git checkout --ours .ait/$O
sed 's/^state future$/state bogus/' .ait/$O >.ait/$O.tmp && cat .ait/$O.tmp >.ait/$O && rm .ait/$O.tmp
git add -A
# git exits 1 whatever the hook's code; the hook's first line is 5.8's E_FORMAT line
tick; run git commit --no-edit
check "malformed record: git commit is rejected" test $rc != 0
check "malformed record: first line is E_FORMAT's" sh -c "head -1 '$T/err' | grep -q '^fatal: bad format:'"
check "malformed record: HEAD unchanged" test "$(git rev-parse HEAD)" = "$Z1"
check "malformed record: merge still in progress" merging
# the --ours resolution
git checkout HEAD -- .ait/$O; git add -A
tick; expect_ok "3: the --ours resolution commits" git commit --no-edit
check "3: merge commit" test "$(git rev-parse HEAD^2)" = "$(git rev-parse z2)"
check "3: O is z1's version (prev aside)" test "$(rec $O | grep -v '^prev ')" = "$(git show $Z1:.ait/$O | grep -v '^prev ')"
check "3: O's prev is z1's and z2's versions in OID order" test "$(prevs $O)" = "$(oids $Z1:.ait/$O z2:.ait/$O)"
check "3: a new Ait-Request" new_rid $Z1 z2
# not yet: `ait peek <O>`'s last event (`resolved`) is not checked

# y1 and y2 from Hbase each change line 2 of tests/long.sh through `contract add`·`contract
# commit` (7.5, 7.6); y1 merges y2, which conflicts (T28-2, 9)
prep_y() {
	prep
	git branch y1; git branch y2
	git switch -q y2; setl tests/long.sh 2 '# l2 y2'
	run ait contract add tests/long.sh; tick; run ait contract commit -m y2; ry2=$rc
	git switch -q y1; setl tests/long.sh 2 '# l2 y1'
	run ait contract add tests/long.sh; tick; run ait contract commit -m y1; ry1=$rc
	Y1=$(git rev-parse HEAD) Y2=$(git rev-parse y2)
	[ "$ry1/$ry2" = 0/0 ] && out_has "changed $O" && out_has "changed $D" &&
		say_ok "prep: y1 and y2 re-pin O and D" || say_fail "prep: y1 and y2 re-pin O and D"
	tick; run git merge y2
}

# 2. concurrent changes resolved (T28-2, 7.1.3-2·5, 7.1.5)
prep_y
check "2: the merge conflicts in tests/long.sh, .ait/O and .ait/D" test "$rc/$(unmerged)" = "1/$(paths .ait/$O .ait/$D tests/long.sh)"
write_long '# l2 y1 y2'
git checkout --ours .ait/$O .ait/$D; git add -A
tick; expect_ok "2: git commit succeeds" git commit --no-edit
check "2: a merge commit of y2" test "$(git rev-parse HEAD^2)" = "$Y2"
check "2: O is pinned to the merged tests/long.sh" rec_has $O "file tests/long.sh 100755 $(blob HEAD:tests/long.sh) input"
check "2: D is pinned to the merged tests/long.sh" rec_has $D "file tests/long.sh 100755 $(blob HEAD:tests/long.sh) input"
check "2: O's prev is y1's and y2's versions in OID order" test "$(prevs $O)" = "$(oids $Y1:.ait/$O $Y2:.ait/$O)"
check "2: D's prev is y1's and y2's versions in OID order" test "$(prevs $D)" = "$(oids $Y1:.ait/$D $Y2:.ait/$D)"
check "2: a new Ait-Request" new_rid $Y1 $Y2

# 2a. two inherited prev lines are no new resolution (T28-2a, 7.1.5)
Y1M=$(git rev-parse HEAD)
git switch -q -c cc; setl src/order.c 50 o50x; tick; git commit -qam cc
git switch -q y1; tick; run git merge --no-ff --no-edit cc
check "2a: the merge M ends without a conflict" test "$rc/$(git rev-parse HEAD^2)" = "0/$(git rev-parse cc)"
check "2a: M has no Ait-Request" test -z "$(rid)"
check "2a: the records are y1's versions with two prev lines" test "$(git rev-parse :.ait/$O :.ait/$D)" = "$(git rev-parse $Y1M:.ait/$O $Y1M:.ait/$D)"
# not yet: `git push -q origin y1` and `ait contract push origin` (E_EMPTY) need `contract push`

# 9. a record no starting version can reach is rejected in an integration (T28-9, 7.1.3-3·4·7)
prep_y
git checkout --ours tests/long.sh .ait/$O .ait/$D
sed 's/^state future$/state current/' .ait/$O >"$T/rec" && cat "$T/rec" >.ait/$O
git add -A
tick; expect_err "9: git commit is E_STATE (pre-commit)" E_STATE git commit --no-edit
check "9: HEAD unchanged" test "$(git rev-parse HEAD)" = "$Y1"
check "9: hint: restore .ait/O as in y1's HEAD" err_has "restore '.ait/$O' as in '$Y1' and stage the result"
git --literal-pathspecs checkout $Y1 -- .ait/$O
tick; expect_ok "9: restored: git commit succeeds" git commit --no-edit
# T28-9: a contract that was never current is not reopened
prep_y
write_long '# l2 y1 y2'
git checkout --ours .ait/$O .ait/$D
sed 's/^state future$/state current/' .ait/$O >"$T/rec" && cat "$T/rec" >.ait/$O
git add -A
tick; expect_err "9: merged tests/long.sh: git commit is E_STATE" E_STATE git commit --no-edit
check "9: merged tests/long.sh: no 'is reopened' warning" not err_has "is reopened"
git merge --abort

# a clean (pre-merge-commit) merge whose result breaks validate d is rejected (7.1.4)
# u: `undertake D P`; f: P is fulfilled (current); merged: an undertaken link with current P and future D
prep; exec_def pass
tick; run ait future offer src/order.c specs/pass.yaml as p -m p
P=$(id_of p)
check "p: future offer ok" test $rc = 0 -a -n "$P"
git branch u; git branch f
git switch -q f; tick; with_r pass ait fulfill $P -m f
check "f: P fulfilled" out_has "fulfilled $P"
git switch -q u; tick; run ait undertake $D $P -m u
check "u: undertake ok" test $rc = 0
U=$(git rev-parse HEAD)
tick; expect_err "auto-merge breaking validate d is rejected" E_STATE git merge --no-edit f
check "auto-merge: HEAD unchanged" test "$(git rev-parse HEAD)" = "$U"
check "auto-merge: merge left in progress" merging
check "auto-merge: hint: fix it in this merge and run the finishing commit (7.1.4)" err_has "fix it in this merge and run 'git commit --allow-empty --no-edit --cleanup=strip'; the records and spec files are pinned and validated again then"
git --literal-pathspecs checkout HEAD -- .ait/$P; git add -A
tick; expect_ok "restored P: git commit succeeds" git commit --no-edit
check "restored P: merge commit" test "$(git rev-parse HEAD^2)" = "$(git rev-parse f)"
check "restored P: P is future" test "$(rec $P | sed -n 2p)" = "state future"

# 7. a record the cherry-pick did not touch stays as it is (T28-7: E is D; c2's other
# commit and master both amend O; the resolution is committed with the finishing commit)
prep
git branch c2
git switch -q c2
tick; run ait future amend $D - src/stock.c:1-5 - -m c2d
tick; run ait future amend $O src/stock.c:6-9 - -m c2o
C2O=$(git rev-parse HEAD)
git switch -q master
tick; run ait future amend $D - src/stock.c:6-9 - -m md
tick; run ait future amend $O src/stock.c:1-5 - -m mo
MH=$(git rev-parse HEAD) DP=$(prevs $D)
check "7: D's prev on master is one line" test "$(echo "$DP" | wc -l | tr -d ' ')" = 1
tick; run git cherry-pick $C2O
check "7: the pick of c2's other commit conflicts in .ait/O" test $rc != 0
git checkout --ours .ait/$O; git add -A
tick; run git commit --allow-empty --no-edit --cleanup=strip
check "7: the resolution commits" test $rc = 0 -a "$(git rev-parse HEAD^)" = "$MH"
check "7: D is master's version, no resolution version" test "$(blob HEAD:.ait/$D)" = "$(blob $MH:.ait/$D)"
check "7: D's prev is the same one line" test "$(prevs $D)" = "$DP"
check "7: O (both sides changed it) has two prev lines" test "$(prevs $O)" = "$(oids $MH:.ait/$O $C2O:.ait/$O)"

# 6. a cherry-pick conflict (T28-6, 7.1.3, 7.12)
prep
git switch -q -c c1; setl tests/long.sh 2 '# l2 c1'
run ait contract add tests/long.sh; tick; run ait contract commit -m c1
RC1=$(git rev-parse HEAD)
check "6: c1 re-pins O and D (Rc1)" sh -c "[ $rc = 0 ] && grep -qx 'changed $O' '$T/out' && grep -qx 'changed $D' '$T/out'"
git switch -q master; setl tests/long.sh 2 '# l2 m'
run ait contract add tests/long.sh; tick; run ait contract commit -m m
check "6: master re-pins O and D" sh -c "[ $rc = 0 ] && grep -qx 'changed $O' '$T/out' && grep -qx 'changed $D' '$T/out'"
tick; run git cherry-pick $RC1
check "6: the pick conflicts in tests/long.sh, .ait/O and .ait/D" test "$(unmerged)" = "$(paths .ait/$O .ait/$D tests/long.sh)"
write_long '# l2 m c1'
git checkout --ours .ait/$O .ait/$D; git add -A
tick; run git cherry-pick --continue
check "6: git cherry-pick --continue succeeds" test $rc = 0
check "6: a new Ait-Request, not Rc1's" new_rid $RC1
expect_ok "6: contract pull O D" ait contract pull $O $D
check "6: already O D (c1's versions are in HEAD's prev)" test "$(cat "$T/out")" = "already $O $D"

# 10. spec files fixed in an integration are pinned again (T28-10)
prep
git branch nc; git switch -q nc; setl src/order.c 50 o50x; tick; git commit -qam nc; git switch -q master
tick; run git merge --no-commit --no-ff nc
check "10: the merge stops before its commit" merging
setl tests/long.sh 2 '# l2x'; git add tests/long.sh
tick; expect_ok "10: git commit succeeds" git commit --no-edit
check "10: O's file line is pinned to the fixed tests/long.sh" rec_has $O "file tests/long.sh 100755 $(blob HEAD:tests/long.sh) input"
check "10: D's file line is pinned to the fixed tests/long.sh" rec_has $D "file tests/long.sh 100755 $(blob HEAD:tests/long.sh) input"
run ait todo $O $D
check "10: nothing is unverified (O and D are future)" not grep -q unverified "$T/out"

# a current offer K on src/stock.c:1-10 with tests/long.sh (T28-11, 14, 23)
prep_k11() {
	prep
	tick; with_r pass ait offer src/stock.c:1-10 specs/long.yaml as k -m k
	K=$(id_of k) HK=$(git rev-parse HEAD)
	[ $rc = 0 ] && [ -n "$K" ] && say_ok "prep: K on tests/long.sh" || say_fail "prep: K on tests/long.sh"
}
# k1 and k2 from K's commit change lines 2 and 11 of tests/long.sh through `contract add` and
# `contract commit -r pass` (Rk1, Rk2); k1 merges k2 and takes ours for the records (T28-11);
# with an argument, a session B of agent-b is opened too (the default session stays A's)
prep_k12() {
	prep_k11
	[ -n "${1-}" ] && B=$(ait session open --agent agent-b)
	git branch k1; git branch k2
	git switch -q k2; setl tests/long.sh 11 '# l11 k2'; run ait contract add tests/long.sh
	tick; with_r pass ait contract commit -m k2; rk2=$rc; RK2=$(git rev-parse HEAD)
	git switch -q k1; setl tests/long.sh 2 '# l2 k1'; run ait contract add tests/long.sh
	tick; with_r pass ait contract commit -m k1; rk1=$rc; RK1=$(git rev-parse HEAD)
	tick; run git merge k2
	[ "$rk1/$rk2/$rc" = 0/0/1 ] && [ "$(unmerged)" = "$(paths .ait/$K .ait/$O .ait/$D)" ] &&
		say_ok "prep: .ait/K, .ait/O and .ait/D conflict" || say_fail "prep: .ait/K, .ait/O and .ait/D conflict"
	git checkout --ours .ait/$K .ait/$O .ait/$D; git add -A
}

# 11. the condition of a current contract merged without results is reopened (T28-11, 7.1.3-3, 7.1.5)
prep_k12
tick; run git commit --no-edit
check "11: git commit succeeds" test $rc = 0
check "11: warning: K is reopened, its pins changed without a check" err_has "warning: '$K' is reopened in this merge: its pins changed without a check"
check "11: H10: ait fulfill K" err_has "ait fulfill $K -m <why>"
check "11: no 'is unverified after this merge'" not err_has "is unverified after this merge"
check "11: K is future" rec_has $K "state future"
check "11: K's file line is pinned to the merged tests/long.sh" rec_has $K "file tests/long.sh 100755 $(blob HEAD:tests/long.sh) input"
check "11: K's provider is as pinned" test "$(rec $K | grep '^provider ')" = "provider src/stock.c:1-10 $(blob $HK:src/stock.c)"
check "11: K's prev is k1's and k2's versions in OID order" test "$(prevs $K)" = "$(oids $RK1:.ait/$K $RK2:.ait/$K)"
check "11: K has no check line" sh -c "! git show HEAD:.ait/$K | grep -q '^check '"
check "11: a new Ait-Request" new_rid $RK1 $RK2
# not yet: `ait peek now src/stock.c` (no K) and peek's `reopened` event
setl src/stock.c 3 s3x; tick; run git commit -qam s3
check "11: a commit changing stock.c line 3 does not warn of K" not err_has "$K"
tick; with_r pass ait fulfill $K -m f
check "11: fulfill K -r pass" test $rc = 0
check "11: fulfilled K" out_has "fulfilled $K"
check "11: check K reported" out_has "check $K reported"
# not yet: `ait peek now src/stock.c` -> `ait todo <K>`
expect_ok "11: todo K" ait todo $K
check "11: K is not unverified" not grep -q '^unverified' "$T/out"
# T28-11, by judgment
prep_k12
tick; run git commit --no-edit
tick; expect_ok "11: fulfill K --force" ait fulfill $K --force -m 사소함
check "11: --force: fulfilled K" out_has "fulfilled $K"
check "11: --force: check K forced" out_has "check $K forced"
# T28-11: whoever made the integration hook's reopening is the reopener (1.5d)
prep_k12 b
tick; run env AIT_SESSION=$B git commit --no-edit
check "11: B's commit succeeds" test $rc = 0
check "11: Ait-Agent is agent-b" test "$(git log -1 --format='%(trailers:key=Ait-Agent,valueonly)')" = agent-b
run env AIT_SESSION=$B ait todo
check "11: B's todo has K" grep -qx "id $K" "$T/out"
check "11: B's H10 with AIT_SESSION" err_has "AIT_SESSION=$B ait fulfill $K -m <why>"
run ait todo
check "11: A's todo has K" grep -qx "id $K" "$T/out"
check "11: A's H14" err_has "'$K' was reopened by 'agent-b'; develop against it, or, if it is no longer needed, run 'ait future cancel $K -m <why>'"
check "11: no H10 for A" not err_has "ait fulfill $K"

# 23. without the agent in the record lines there is no reopener (T28-23, 1.5d, H10, H14)
prep_k12
tick; run env AIT_SESSION=- git commit --no-edit
check "23: git commit succeeds" test $rc = 0
check "23: Ait-Agent is unknown" test "$(git log -1 --format='%(trailers:key=Ait-Agent,valueonly)')" = unknown
check "23: warning: K is reopened" err_has "warning: '$K' is reopened in this merge"
check "23: no H10 after it" not err_has "ait fulfill $K"
run ait todo
check "23: A's todo has K" grep -qx "id $K" "$T/out"
check "23: H10: ait fulfill K" err_has "ait fulfill $K -m <why>"
check "23: no H14" not err_has "was reopened by"

# 13. a current O undertaking a D reopened in an integration is reopened too (T28-13, 7.1.3-3, 5.9 d)
prep; prompt_defs; rm specs/low.yaml specs/w.yaml
cp tests/long.sh tests/long2.sh
printf 'body: long2\nengine: exec\ncommand: [./tests/long2.sh]\ninputs: [tests/long2.sh]\n' >specs/long2.yaml
tick; with_r pass ait demand src/order.c src/stock.c specs/long2.yaml as d2 offer src/stock.c:1-10 specs/p.yaml as o2 undertake d2 o2 -m d2
D2=$(id_of d2) O2=$(id_of o2)
check "13: D2 and O2" test $rc = 0 -a -n "$D2" -a -n "$O2"
HO2=$(git rev-parse HEAD)
git branch m1; git branch m2
git switch -q m2; setl tests/long2.sh 11 '# l11 m2'
expect_ok "13: m2: contract add tests/long2.sh" ait contract add tests/long2.sh
tick; expect_err "13: m2: contract commit needs results" E_NEEDS_RESULTS ait contract commit -m m2
check "13: m2: the task is D2:specs/long2.yaml" test "$(grep '^task ' "$T/out")" = "task $D2:specs/long2.yaml exec"
tick; with_r pass ait contract commit -m m2
check "13: m2: contract commit -r pass" test $rc = 0
git switch -q m1; setl tests/long2.sh 2 '# l2 m1'
expect_ok "13: m1: contract add tests/long2.sh" ait contract add tests/long2.sh
tick; with_r pass ait contract commit -m m1
check "13: m1: contract commit -r pass" test $rc = 0
tick; run git merge m2
check "13: .ait/D2 conflicts" test "$rc/$(unmerged)" = "1/$(paths .ait/$D2)"
git checkout --ours .ait/$D2; git add -A
tick; run git commit --no-edit
check "13: git commit succeeds" test $rc = 0
check "13: warning: D2 is reopened, its pins changed without a check" err_has "warning: '$D2' is reopened in this merge: its pins changed without a check"
check "13: warning: O2 is reopened, it undertakes reopened D2" err_has "warning: '$O2' is reopened in this merge: it undertakes reopened '$D2'"
check "13: H10: ait fulfill D2" err_has "ait fulfill $D2 -m <why>"
check "13: H10: ait fulfill O2" err_has "ait fulfill $O2 -m <why>"
check "13: D2 and O2 are future" test "$(rec $D2 | sed -n 's/^state //p') $(rec $O2 | sed -n 's/^state //p')" = "future future"
check "13: the link stays undertaken" test "$(git show HEAD:.ait/undertake/$D2-$O2 | sed -n 's/^state //p')" = undertaken
check "13: O2's prev is its version before the merge, one line" test "$(prevs $O2)" = "$(blob $HO2:.ait/$O2)"
tick; expect_err "13: fulfill O2 needs results" E_NEEDS_RESULTS ait fulfill $O2 -m f
check "13: tasks D2:specs/long2.yaml and O2:specs/p.yaml" test "$(grep '^task ' "$T/out" | LC_ALL=C sort)" = "$(printf 'task %s:specs/long2.yaml exec\ntask %s:specs/p.yaml prompt\n' $D2 $O2 | LC_ALL=C sort)"
tick; with_r pass ait fulfill $O2 -m f
check "13: fulfill O2 -r pass" test $rc = 0
check "13: fulfilled D2 via O2" out_has "fulfilled $D2 via $O2"
check "13: fulfilled O2" out_has "fulfilled $O2"

# 14. a cherry-pick's resolution is judged by the check line too (T28-14, 7.1.3-3, 1.5a)
prep_k11
git switch -q -c c3; setl tests/long.sh 2 '# l2 c3'
run ait contract add tests/long.sh; tick; with_r pass ait contract commit -m c3
RK3=$(git rev-parse HEAD)
check "14: c3: contract commit -r pass (Rk3)" test $rc = 0
check "14: rec(K) has check reported pins(Rk3, K)" test "$(check_line $RK3 $K)" = "reported $(pins $RK3 $K)"
git switch -q master; setl tests/long.sh 2 '# l2 m'
run ait contract add tests/long.sh; tick; with_r pass ait contract commit -m m
check "14: master: contract commit -r pass" test $rc = 0
tick; run git cherry-pick $RK3
check "14: tests/long.sh and the records conflict" test "$(unmerged)" = "$(paths .ait/$K .ait/$O .ait/$D tests/long.sh)"
write_long '# l2 m c3'
git checkout --ours .ait/$K .ait/$O .ait/$D; git add -A
tick; run git cherry-pick --continue
check "14: git cherry-pick --continue succeeds" test $rc = 0
check "14: a new Ait-Request, not Rk3's" new_rid $RK3
check "14: warning: K is reopened, its pins changed without a check" err_has "warning: '$K' is reopened in this merge: its pins changed without a check"
check "14: K is future" rec_has $K "state future"

# 15. versions pushed aside are not in prev (T28-15, 7.1.3-5, 7.1.5)
prep
git branch p1; git branch p2
git switch -q p1; tick; run ait future amend $O src/stock.c:1-5 - -m p1
git switch -q p2; tick; run ait future amend $O src/stock.c:6-9 - -m p2
git switch -q master
tick; run git merge -s ours --no-ff --no-commit p1 p2
check "15: the octopus stops before its commit" merging
tick; expect_ok "15: git commit succeeds" git commit --no-edit
check "15: O is Hbase's version (prev aside)" test "$(rec $O | grep -v '^prev ')" = "$(git show $HBASE:.ait/$O | grep -v '^prev ')"
check "15: O's provider is src/stock.c <blob>" rec_has $O "provider src/stock.c $(blob HEAD:src/stock.c)"
check "15: O's prev is p1's and p2's versions in OID order" test "$(prevs $O)" = "$(oids p1:.ait/$O p2:.ait/$O)"
check "15: D's prev is one line as before" test "$(prevs $D)" = "$(git show $HBASE:.ait/$D | sed -n 's/^prev //p')"
check "15: a new Ait-Request" new_rid p1 p2

# 17. only the pins of current starting versions count (T28-17, 7.1.3-3)
prep_k
git branch f; git branch c
git switch -q f; tick; run ait future amend $K src/stock.c:1-5 - -m f
git switch -q c; tick; with_r pass ait amend $K src/stock.c:1-8 - -m c
CC=$(git rev-parse HEAD)
tick; run git merge f
check "17: .ait/K conflicts" sh -c "[ $rc != 0 ] && git rev-parse -q --verify MERGE_HEAD >/dev/null"
git checkout --ours .ait/$K
set_provider .ait/$K "$(git show f:.ait/$K | grep '^provider ')"
git add -A; tick; run git commit --no-edit
check "17: git commit succeeds (not E_STATE)" test $rc = 0
check "17: warning: K is reopened, keeping the future version" err_has "warning: '$K' is reopened in this merge: it keeps the future version over the current one"
check "17: K is future" rec_has $K "state future"
check "17: K's provider is src/stock.c:1-5 <blob>" test "$(rec $K | grep '^provider ')" = "provider src/stock.c:1-5 $(blob HEAD:src/stock.c)"
check "17: K's prev is c's and f's versions in OID order" test "$(prevs $K)" = "$(oids $CC:.ait/$K f:.ait/$K)"
check "17: a new Ait-Request" new_rid $CC f

# 18. a record made during an integration has prev '-' (T28-18, 7.1.3-5, 7.4.1)
prep_nb() {
	prep
	git switch -q -c nb; setl src/order.c 50 o50x; tick; git commit -qam nb; git switch -q master
	tick; run git merge --no-commit --no-ff nb
	run ait contract new offer
	ON=$(sed -n 's|^path \.ait/\(O[0-9a-f]\{12\}\)$|\1|p' "$T/out")
	[ $rc = 0 ] && [ -n "$ON" ] && [ "$(wc -l <"$T/out" | tr -d ' ')" = 1 ] && merging &&
		say_ok "18: contract new offer during the merge: path .ait/<On>" || say_fail "18: contract new offer during the merge: path .ait/<On>"
	prompt_defs; rm specs/low.yaml specs/w.yaml
	printf 'role offer\nstate future\nprovider src/stock.c %s\nfile specs/p.yaml 100644 %s def\ncreator agent-a %s\nprev %s\n' \
		"$(blob HEAD:src/stock.c)" "$(git hash-object specs/p.yaml)" "$A" "$(blob HEAD:.ait/$O)" >.ait/$ON
	git add -A
}
prep_nb
tick; expect_ok "18: git commit succeeds" git commit --no-edit
check "18: On's prev is -" test "$(prevs $ON)" = "-"
check "18: a new Ait-Request" new_rid HEAD^1 nb
# not yet: `ait peek <On>`'s first event (created-future)
prep_nb
tick; run env AIT_SESSION=- git commit --no-edit
check "18: without a session: git commit succeeds" test $rc = 0
check "18: without a session: On's creator is unknown -" rec_has $ON "creator unknown -"
check "18: without a session: Ait-Agent is unknown" test "$(git log -1 --format='%(trailers:key=Ait-Agent,valueonly)')" = unknown

# 20. an automatic merge cannot drop a record either (T28-20, 7.1.5, 7.1.3-1·7)
# sb: Ob (Rb) on Hbase; master: a plain commit; then `git merge -s ours` of sb
prep_sb() {
	prep
	git switch -q -c sb
	[ -n "${1-}" ] && printf 'body: new\n' >specs/new.yaml
	tick; run ait future offer src/order.c "${1:-specs/long.yaml}" as ob -m ob
	OB=$(id_of ob) SB=$(git rev-parse HEAD)
	git switch -q master; setl src/order.c 50 o50x; tick; git commit -qam other
	M0=$(git rev-parse HEAD)
	tick; run git merge -s ours --no-ff --no-edit sb
}
prep_sb
check "20: the merge fails (exit != 0)" test $rc != 0
check "20: HEAD unchanged" test "$(git rev-parse HEAD)" = "$M0"
check "20: a line starting with 'error: not allowed:'" grep -q '^error: not allowed:' "$T/err"
check "20: hint: restore .ait/Ob as in sb" err_has "restore '.ait/$OB' as in '$SB' and stage the result"
check "20: then the finishing commit" err_has "then run 'git commit --allow-empty --no-edit --cleanup=strip'"
check "20: no 'finish it with'" not err_has "finish it with"
check "20: MERGE_HEAD is sb" test "$(git rev-parse -q --verify MERGE_HEAD)" = "$SB"
tick; expect_err "20: git commit is E_STATE (pre-commit)" E_STATE git commit --no-edit
check "20: HEAD still unchanged" test "$(git rev-parse HEAD)" = "$M0"
check "20: pre-commit hint: restore .ait/Ob as in sb" err_has "restore '.ait/$OB' as in '$SB' and stage the result"
git checkout sb -- .ait/$OB
tick; expect_ok "20: restored: git commit succeeds" git commit --no-edit
check "20: Ob is sb's version" test "$(blob HEAD:.ait/$OB)" = "$(blob $SB:.ait/$OB)"
check "20: Ob's prev is -" test "$(prevs $OB)" = "-"
check "20: no record line in the merge commit" test -z "$(rid)"
expect_ok "20: contract pull Ob" ait contract pull $OB
check "20: already Ob" test "$(cat "$T/out")" = "already $OB"
prep_sb
git merge --abort
check "20: abort: HEAD as before the merge" test "$(git rev-parse HEAD)" = "$M0"
tick; expect_ok "20: abort: contract pull Ob" ait contract pull $OB
check "20: abort: included Ob from Rb's commit" test "$(cat "$T/out")" = "included $OB from '$SB'"
prep_sb specs/new.yaml
check "20: new spec file: the merge fails" test $rc != 0
check "20: new spec file: hint restores the record and its new spec file" err_has "restore '.ait/$OB' 'specs/new.yaml' as in '$SB' and stage the result"
git checkout sb -- .ait/$OB specs/new.yaml
tick; expect_ok "20: new spec file: restored: git commit succeeds" git commit --no-edit

# 21. a pick that drops the incoming current version reopens it too (T28-21, 1.5d, 7.1.5, decision 41)
prep_k
git switch -q -c g; tick; with_r pass ait amend $K src/stock.c:1-8 - -m g
RG=$(git rev-parse HEAD)
git switch -q master; tick; run ait future amend $K src/stock.c:1-5 - -m m
tick; run git cherry-pick $RG
check "21: .ait/K conflicts" test $rc != 0
git checkout --ours .ait/$K
set_provider .ait/$K "provider src/stock.c:1-6 $(blob HEAD:src/stock.c)"
git add -A; tick; run git cherry-pick --continue
check "21: git cherry-pick --continue succeeds" test $rc = 0
check "21: warning: K is reopened in this merge, with no reason" grep -qxF "warning: '$K' is reopened in this merge" "$T/err"
check "21: H10: ait fulfill K" err_has "ait fulfill $K -m <why>"
# not yet: `ait peek <K>`'s last event (`reopened`), as peek is not supported

# 22. a resolution changing only the state reopens too (T28-22, 7.1.5)
prep_k
git branch x; git branch y
git switch -q x; tick; with_r pass ait amend $K src/stock.c:1-8 - -m x
git switch -q y; tick; with_r pass ait amend $K src/stock.c:1-9 - -m y
git switch -q x; tick; run git merge y
check "22: .ait/K conflicts" test $rc != 0
git checkout --ours .ait/$K
sed 's/^state current$/state future/' .ait/$K >.ait/$K.tmp && cat .ait/$K.tmp >.ait/$K && rm .ait/$K.tmp
git add -A; tick; run git commit --no-edit
check "22: git commit succeeds" test $rc = 0
check "22: warning: K is reopened in this merge, with no reason" grep -qxF "warning: '$K' is reopened in this merge" "$T/err"
check "22: H10: ait fulfill K" err_has "ait fulfill $K -m <why>"

# 24. a resolution is committed first, then git goes on (T28-24; not yet: the merge driver's hints)
# rb: Rr reopens K to 1-5; master reopens K to 1-6; rb rebases onto master
prep_rb() {
	prep_k
	git switch -q -c rb; tick; run ait future amend $K src/stock.c:1-5 - -m rr
	RR=$(git rev-parse HEAD)
	git switch -q master; tick; run ait future amend $K src/stock.c:1-6 - -m m
	git switch -q rb
}
prep_rb
tick; run git rebase master
check "24: .ait/K conflicts" test $rc != 0
expect_err "24: a helper during the rebase" E_BUSY ait future offer src/order.c specs/p.yaml -m m
check "24: hint: a git rebase is in progress" err_has "a git rebase is in progress; finish or abort it as 'git status' says, then rerun this command"
check "24: hint: commit the resolution first" err_has "commit the resolution first: 'git commit --allow-empty --allow-empty-message -C REBASE_HEAD', then continue as git says"
echo "x" >>.ait/$O
expect_err "24: .ait/O changed in the work tree only" E_BUSY ait future offer src/order.c specs/p.yaml -m m
check "24: hint: keep your contract changes out of it" err_has "then continue as git says; keep your contract changes out of it, since only ait commits contract files"
git --literal-pathspecs checkout -- .ait/$O
git checkout --theirs .ait/$K; git add -A
tick; run git commit --allow-empty --allow-empty-message -C REBASE_HEAD
check "24: the resolution commits" test $rc = 0
check "24: a new Ait-Request" new_rid $RR
check "24: subject and author are Rr's" test "$(git log -1 --format='%s/%an/%ae')" = "$(git log -1 --format='%s/%an/%ae' $RR)"
run git rebase --continue
check "24: git rebase --continue succeeds" test $rc = 0
# apply backend
prep_rb
tick; run git rebase --apply master
check "24 apply: .ait/K conflicts" test $rc != 0
git checkout --theirs .ait/$K; git add -A
tick; run git commit --allow-empty --allow-empty-message -C REBASE_HEAD
check "24 apply: the resolution commits" test $rc = 0
check "24 apply: a new Ait-Request" new_rid $RR
expect_err "24 apply: a helper after that commit" E_BUSY ait future offer src/order.c specs/p.yaml -m m
check "24 apply: hint: a git rebase is in progress" err_has "a git rebase is in progress; finish or abort it as 'git status' says, then rerun this command"
check "24 apply: no finishing hint" not err_has "commit the resolution first"
check "24 apply: no contract-change hint" not err_has "keep your contract changes out of it"
run git rebase --continue
check "24 apply: git rebase --continue stops at the empty patch" test $rc != 0
run git rebase --skip
check "24 apply: git rebase --skip succeeds" test $rc = 0

# 25. a cherry-pick's resolution is committed first too (T28-25; not yet: the merge driver's hints)
prep
git switch -q -c c4; tick; run ait future amend $O src/stock.c:1-5 - -m c4
RC=$(git rev-parse HEAD)
git switch -q master; tick; run ait future amend $O src/stock.c:1-6 - -m m
tick; run git cherry-pick $RC
check "25: .ait/O conflicts" test $rc != 0
git checkout --ours .ait/$O; git add -A
H0=$(git rev-parse HEAD)
# T28-25 (5.8 E_BUSY, 7.1.3): contract add during the integration is git's to finish first
desc="25: contract add .ait/O during the cherry-pick"
expect_unchanged expect_err "$desc" E_BUSY ait contract add .ait/$O
check "25: hint: a git cherry-pick is in progress" err_has "a git cherry-pick is in progress; finish or abort it as 'git status' says, then rerun this command"
check "25: hint: commit the resolution first" err_has "commit the resolution first: 'git commit --allow-empty --no-edit --cleanup=strip', then continue as git says"
run git cherry-pick --continue
check "25: git cherry-pick --continue stops (the resolution was HEAD)" test $rc != 0 -a "$(git rev-parse HEAD)" = "$H0"
tick; run git commit --allow-empty --no-edit --cleanup=strip
check "25: the finishing commit succeeds" test $rc = 0
check "25: a new Ait-Request" new_rid $RC $H0
expect_ok "25: contract pull O" ait contract pull $O
check "25: already O (c4's version is in HEAD's prev)" test "$(cat "$T/out")" = "already $O"

# 26. during a git operation, back to git's own guidance (T28-26, 5.8 E_BUSY, 7.1.3)
prep; prompt_defs; rm specs/low.yaml specs/w.yaml
git switch -q -c pk
tick; run ait future offer src/order.c specs/p.yaml as p1 -m p1; PK1=$(git rev-parse HEAD)
tick; run ait future offer src/order.c specs/p.yaml as p2 -m p2; PK2=$(git rev-parse HEAD)
git switch -q master
setl src/order.c 1 o1x; git add src/order.c
run git cherry-pick $PK1 $PK2
check "26: git refuses and leaves sequencer/todo" test $rc != 0 -a -f "$(git rev-parse --git-path sequencer/todo)"
expect_err "26: a helper" E_BUSY ait future offer src/stock.c specs/p.yaml -m m
check "26: hint: a git cherry-pick is in progress" err_has "a git cherry-pick is in progress; finish or abort it as 'git status' says, then rerun this command"
check "26: one hint only (no finishing hint, no local-change hint)" test "$(grep -c '^hint:' "$T/err")" = 1
git cherry-pick --quit; git reset -q --hard
git switch -q -c amb $HBASE; setl src/order.c 2 o2x; tick; git commit -qam am-src
git format-patch -1 --stdout HEAD >"$T/am.patch"
git switch -q master; setl src/order.c 2 o2y; tick; git commit -qam am-dst
tick; run git am "$T/am.patch"
check "26: git am stops" test $rc != 0 -a -f "$(git rev-parse --git-path rebase-apply/applying)"
expect_err "26: a helper while git am stands" E_BUSY ait future offer src/stock.c specs/p.yaml -m m
check "26: hint: a git am is in progress" err_has "a git am is in progress; finish or abort it as 'git status' says, then rerun this command"
check "26: am: one hint only" test "$(grep -c '^hint:' "$T/err")" = 1
git am --abort

# 27. a rebase stopped to edit is no integration (T28-27, 7.1.3, 5.8 E_BUSY)
prep
tick; run env GIT_SEQUENCE_EDITOR='sed -i.bak 1s/^pick/edit/' git rebase -i HEAD~1
check "27: the rebase stops (REBASE_HEAD, no MERGE_MSG)" sh -c "[ $rc = 0 ] && git rev-parse -q --verify REBASE_HEAD >/dev/null && [ ! -e \"\$(git rev-parse --git-path MERGE_MSG)\" ]"
set_provider .ait/$O "provider src/stock.c:1-5 $(blob HEAD:src/stock.c)"
git add .ait/$O
expect_err "27: a helper" E_BUSY ait future offer src/stock.c specs/p.yaml -m m
check "27: hint: the rebase, and keep the contract changes out" err_has "a git rebase is in progress; finish or abort it as 'git status' says, then rerun this command; keep your contract changes out of it, since only ait commits contract files"
check "27: no finishing hint" not err_has "commit the resolution first"
tick; expect_err "27: git commit is the plain pre-commit's E_GUARD" E_GUARD git commit -qm x
check "27: hint: first finish or abort the rebase" err_has "first finish or abort the git rebase in progress ('git status'); keep your contract changes out of it, since only ait commits contract files"

done_testing
