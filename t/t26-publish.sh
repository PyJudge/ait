#!/bin/sh
# T26 공개 경합·동기화 실패·부분 stage 보존 (5.5)
#
# ait opens the result file at the check-result step (8.4); a named pipe lets a background
# job act in between. The job waits at `exec 3>` until ait opens the pipe (after reading H).
. "$(dirname "$0")/lib.sh"

id_of() { sed -n "s/^new \([^ ]*\) $1\$/\1/p" "$T/out"; }
setl() { f=$1 l=$2; shift 2; awk -v n="$l" -v t="$*" 'NR==n{print t; next}1' "$f" >"$f.tmp" && cat "$f.tmp" >"$f" && rm "$f.tmp"; }
has_line() { grep -qxF -- "$1" "$2"; }

# step 1's preparation: future O on stock.c, a staged stock.c change, W filled with pass, the pipe
prep() {
	setup
	A=$(ait session open --agent agent-a)
	prompt_defs
	tick; run ait future offer src/stock.c specs/p.yaml as o -m m
	O=$(id_of o)
	setl src/stock.c 3 s3x; git add src/stock.c
	run ait fulfill $O -m m
	W=$(out_value check-result)
	[ -n "$O" ] && [ -n "$W" ] && fill "$W" pass && mkfifo "$T/res" && say_ok "prep: O, W and the pipe" || say_fail "prep: O, W and the pipe"
}
# after the run: release a job ait never met (a failed run), then wait for it
reap() { exec 4<>"$T/res"; wait $bg; exec 4<&-; }
# run the restore the hints give (5.5-5): each 'bring' as the state it names
restore() {
	sed -n "s/^hint: bring '\(.*\)' to '\([0-9a-f]*\)' in the index and work tree\$/\2 \1/p" "$1" |
	while read -r c p; do git checkout -q "$c" -- "$p"; done
	sed -n "s/^hint: bring '\(.*\)' to '\([0-9a-f]*\)' in the index\$/\2 \1/p" "$1" |
	while read -r c p; do git reset -q "$c" -- "$p"; done
}

# 1. T26 step 1: HEAD moves between reading H and the CAS (5.5-3): E_HEAD_MOVED, the stage stays
prep
staged=$(git ls-files -s)
(exec 3>$T/res; B=$(git commit-tree -p HEAD -m bump HEAD^{tree}); git update-ref refs/heads/master $B; cat "$W" >&3) & bg=$!
tick; expect_err "1: fulfill O -r <pipe>" E_HEAD_MOVED ait fulfill $O -m m -r "$T/res"
reap
check "1: HEAD's subject is bump" test "$(git log -1 --format=%s)" = "bump"
check "1: the stage stays" test "$(git ls-files -s)" = "$staged"

# 2. T26 step 2: the index is locked while ait syncs it (5.5-5): ok, a warning and the restore
prep
H=$(git rev-parse HEAD)
(exec 3>$T/res; touch .git/index.lock; cat "$W" >&3) & bg=$!
tick; run ait fulfill $O -m m -r "$T/res"
reap
C=$(git rev-parse HEAD)
check "2: ok" test $rc = 0
check "2: fulfilled O on master" test "$(git rev-parse HEAD^)" = "$H" -a "$(git symbolic-ref HEAD)" = refs/heads/master
check "2: warning" has_line "warning: committed '$C' to 'refs/heads/master', but the index and work tree were not updated" "$T/err"
check "2: restore hint for .ait/O" err_has "hint: bring '.ait/$O' to '$C' in the index and work tree"
cp "$T/err" "$T/err.2"
rm .git/index.lock
expect_err "2: a write after removing the lock" E_DIRTY ait future offer src/order.c specs/p.yaml -m m
restore "$T/err.2"
tick; expect_ok "2: after the restore, the write is ok" ait future offer src/order.c specs/p.yaml -m m

# 3. T26 step 3: separately staged and unstaged changes are preserved (src/stock.c:1-10)
setup
A=$(ait session open --agent agent-a)
exec_def pass
tick; run ait future offer src/stock.c:1-10 specs/pass.yaml as o3 -m m
check "3: future offer ok" test $rc = 0
O3=$(id_of o3)
setl src/stock.c 1 "s1 A"; git add src/stock.c
setl src/stock.c 1 "s1 B"
tick; with_r pass ait fulfill $O3 -m m
check "3: fulfill ok" test $rc = 0
check "3: HEAD's stock.c line 1 is 's1 A'" test "$(git show HEAD:src/stock.c | sed -n 1p)" = "s1 A"
check "3: the index has 's1 A'" test "$(git show :src/stock.c | sed -n 1p)" = "s1 A"
check "3: the work tree keeps 's1 B'" test "$(sed -n 1p src/stock.c)" = "s1 B"
check "3: stock.c is unstaged-modified only" test "$(git status --porcelain -- src/stock.c)" = " M src/stock.c"

# the same with a helper (a gate request): staged and unstaged code are preserved
setl src/order.c 1 "o1 A"; git add src/order.c
setl src/order.c 1 "o1 B"
echo untracked >src/new.c
tick; run ait future offer src/order.c specs/pass.yaml as o4 -m m
check "helper: ok" test $rc = 0
check "helper: the commit holds only .ait" test "$(git diff --name-only HEAD^ HEAD | grep -v '^\.ait/')" = ""
check "helper: order.c stays staged as 'o1 A'" test "$(git show :src/order.c | sed -n 1p)" = "o1 A"
check "helper: work tree keeps 'o1 B'" test "$(sed -n 1p src/order.c)" = "o1 B"
check "helper: order.c is staged and unstaged" test "$(git status --porcelain -- src/order.c)" = "MM src/order.c"
check "helper: untracked file kept" test "$(git status --porcelain -- src/new.c)" = "?? src/new.c"

# 4. T26 step 4: the branch is switched while running (5.5-4): no sync
prep
H=$(git rev-parse HEAD)
(exec 3>$T/res; git switch -q -c other; cat "$W" >&3) & bg=$!
before=$(snapshot)
tick; run ait fulfill $O -m m -r "$T/res"
reap
check "4: ok" test $rc = 0
check "4: HEAD is other" test "$(git symbolic-ref HEAD)" = refs/heads/other
check "4: master has the fulfill commit" test "$(git rev-parse master^)" = "$H" -a "$(git log -1 --format=%s master)" = "[FULFILL] m"
check "4: the warning ends with 'but the index and work tree were not updated'" sh -c "grep '^warning: ' '$T/err' | grep -q 'but the index and work tree were not updated\$'"
check "4: other's index and work tree are as they were" test "$(snapshot)" = "$before"

done_testing
