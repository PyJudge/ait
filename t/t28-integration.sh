#!/bin/sh
# T28 통합: merge driver·해결·재방송·되돌리기 (7.1.3–7.1.5)
#
# mini: no merge driver, no rebroadcast (new Ait-Request/Ait-Resolves on a resolution), no
# reopening in the integration hook, and the integration hook validates only the structure
# of the records (spec/ait-mini.md). Steps 1-3, 5-8 and 10-27 need the merge driver,
# `contract add`/`contract commit`/`contract pull`/`future drop`, the transition check of
# 7.1.3-4, prev rewriting (7.1.3-5) or the reopen of 7.1.3-3; they are left out. What is
# checked: a merge commit goes through pre-merge-commit / pre-commit, a structurally
# broken merged record is rejected and the merge stays in progress, and a fixed one commits.
. "$(dirname "$0")/lib.sh"

id_of() { sed -n "s/^new \([^ ]*\) $1\$/\1/p" "$T/out"; }
setl() { f=$1 l=$2; shift 2; awk -v n="$l" -v t="$*" 'NR==n{print t; next}1' "$f" >"$f.tmp" && cat "$f.tmp" >"$f" && rm "$f.tmp"; }
not() { ! "$@"; }
merging() { git rev-parse -q --verify MERGE_HEAD >/dev/null; }

prep() {
	setup
	A=$(ait session open --agent agent-a)
	exec_def long; exec_def pass; exec_def nobroken
	tick; run ait future offer src/stock.c specs/long.yaml as o future demand src/order.c src/stock.c specs/long.yaml as d undertake d o -m base
	O=$(id_of o) D=$(id_of d)
	HBASE=$(git rev-parse HEAD)
	[ $rc = 0 ] && [ -n "$O" ] && [ -n "$D" ] && say_ok "prep: Hbase" || say_fail "prep: Hbase"
}

# 4. a merge does not ask for results
# mini: K's scope `src/stock.c:1-10` becomes the whole file `src/stock.c` (line 3 is in both)
prep
git branch bb
git switch -q bb
setl src/stock.c 3 BROKEN; tick; git commit -qam broken
git switch -q master
tick; with_r pass ait offer src/stock.c specs/nobroken.yaml as k -m m
K=$(id_of k)
check "4: K registered" test $rc = 0 -a -n "$K"
setl src/order.c 50 o50x; tick; git commit -qam other
REC_K=$(git rev-parse HEAD:.ait/$K)
tick; run git merge --no-edit bb
check "4: git merge succeeds" test $rc = 0
check "4: it is a merge commit" sh -c 'git rev-parse -q --verify HEAD^2 >/dev/null'
check "4: no merge in progress" not merging
check "4: K's record is unchanged (not reopened)" test "$(git rev-parse HEAD:.ait/$K)" = "$REC_K"
# mini: `ait peek now <K>` -> `ait todo <K>`
expect_ok "4: todo K" ait todo $K
check "4: K is ?changed" out_has "provider src/stock.c ?changed"
check "4: K is unverified" out_has "unverified yes"
# mini: the commit-msg warning `'<K>' is unverified after this merge` and H7 (7.1.5) are not checked

# a conflict resolved into a malformed record is rejected by pre-commit, then fixed
# (step 3's setup: z1 and z2 amend O's provider differently; mini: whole-file scopes)
prep
git branch z1; git branch z2
git switch -q z2; tick; run ait future amend $O new:src/x.c - -m z2
check "z2: amend ok" test $rc = 0
git switch -q z1; tick; run ait future amend $O src/order.c - -m z1
check "z1: amend ok" test $rc = 0
Z1=$(git rev-parse HEAD)
tick; run git merge --no-edit z2
check "merge z2: conflicts" test $rc != 0
check "merge z2: merge in progress" merging
git checkout --ours .ait/$O
sed 's/^state future$/state bogus/' .ait/$O >.ait/$O.tmp && cat .ait/$O.tmp >.ait/$O && rm .ait/$O.tmp
git add -A
# git exits 1 whatever the hook's code; the hook's first line is 5.8's E_FORMAT line
tick; run git commit --no-edit
check "malformed record: git commit is rejected" test $rc != 0
check "malformed record: first line is E_FORMAT's" sh -c "head -1 '$T/err' | grep -q '^fatal: bad format:'"
check "malformed record: HEAD unchanged" test "$(git rev-parse HEAD)" = "$Z1"
check "malformed record: merge still in progress" merging
git checkout HEAD -- .ait/$O; git add -A
tick; expect_ok "fixed record: git commit succeeds" git commit --no-edit
check "fixed record: merge commit" test "$(git rev-parse HEAD^2)" = "$(git rev-parse z2)"
check "fixed record: O is z1's version (prev aside)" test "$(rec $O | grep -v '^prev ')" = "$(git show z1:.ait/$O | grep -v '^prev ')"
# mini: the two-line prev, new Ait-Request and Ait-Resolves of 7.1.3-5 / 7.1.5 are not checked

# a clean (pre-merge-commit) merge whose result breaks validate d is rejected
# u: `undertake D P`; f: P is fulfilled (current); merged: an undertaken link with current P and future D
prep
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
# mini: hint `fix it in this merge and run 'git commit --allow-empty --no-edit --cleanup=strip'; ...` (7.1.4) not checked
git --literal-pathspecs checkout HEAD -- .ait/$P; git add -A
tick; expect_ok "restored P: git commit succeeds" git commit --no-edit
check "restored P: merge commit" test "$(git rev-parse HEAD^2)" = "$(git rev-parse f)"
check "restored P: P is future" test "$(rec $P | sed -n 2p)" = "state future"

done_testing
