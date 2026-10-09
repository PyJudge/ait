#!/bin/sh
# T26 공개 경합·동기화 실패·부분 stage 보존 (5.5)
#
# mini: ait-mini commits with `git commit`, not commit-tree + update-ref CAS, so steps 1, 2
# and 4 (HEAD moved while running, index.lock, branch switch during the run) do not apply.
# Only step 3 (separately staged and unstaged changes are preserved) is checked here.
. "$(dirname "$0")/lib.sh"

id_of() { sed -n "s/^new \([^ ]*\) $1\$/\1/p" "$T/out"; }
setl() { f=$1 l=$2; shift 2; awk -v n="$l" -v t="$*" 'NR==n{print t; next}1' "$f" >"$f.tmp" && cat "$f.tmp" >"$f" && rm "$f.tmp"; }

setup
A=$(ait session open --agent agent-a)
exec_def pass

# 3. mini: the scope `src/stock.c:1-10` is a line range; the whole file keeps the point
tick; run ait future offer src/stock.c specs/pass.yaml as o3 -m m
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

done_testing
