#!/bin/sh
# T19 비대칭 ②: offer는 떠맡은 demand 없이 이행될 수 없다 (1.6, 7.7.2)
. "$(dirname "$0")/lib.sh"

ait_a() { AIT_SESSION=$A ait "$@"; }
ait_b() { AIT_SESSION=$B ait "$@"; }
tasks() { grep '^task ' "$T/out"; }
id_of() { sed -n "s/^new \([^ ]*\) $1\$/\1/p" "$T/out"; }

setup
A=$(ait session open --agent agent-a)
B=$(ait session open --agent agent-b)
exec_def guard; exec_def pass
tick; run ait_a future demand src/order.c src/stock.c specs/guard.yaml as d -m m
D=$(new_id)
# T19 prep: O's provider is the line range src/stock.c:1-10
tick; run ait_b future offer src/stock.c:1-10 specs/pass.yaml as o undertake "$D" o -m m
O=$(new_id)
check "prepare: D and O exist" test -n "$D" -a -n "$O"

# 1
sed -i.bak '1s/.*/s1 x/' src/stock.c && rm -f src/stock.c.bak; git add src/stock.c
expect_err "1: B fulfill O asks for results" E_NEEDS_RESULTS ait_b fulfill "$O" -m m
check "1: tasks are D:guard and O:pass" test "$(tasks)" = "$(printf 'task %s:specs/guard.yaml exec\ntask %s:specs/pass.yaml exec' "$D" "$O" | sort)"
check "1: provider D line" out_has "provider $D src/stock.c"
check "1: provider O line" out_has "provider $O src/stock.c:1-10"
W=$(out_value check-result)
fill "$W" pass "$D:specs/guard.yaml=fail"
expect_err "1: D's failure blocks O" E_CHECK_FAILED ait_b fulfill "$O" -m m -r "$W"

# 2
sed -i.bak '1s/.*/s1 guard/' src/stock.c && rm -f src/stock.c.bak; git add src/stock.c
tick; with_r pass ait_b fulfill "$O" -m m
check "2: fulfill O -r pass ok" test "$rc" = 0
check "2: fulfilled O" out_has "fulfilled $O"
check "2: fulfilled D via O" out_has "fulfilled $D via $O"
check "2: check D reported" out_has "check $D reported"
check "2: check O reported" out_has "check $O reported"
check "2: rec(D) current" sh -c "git show HEAD:.ait/$D | grep -qxF 'state current'"

# 3 (new repository)
setup
A=$(ait session open --agent agent-a)
exec_def both; exec_def pass
# T19-3: O1 and O2 take the line ranges src/stock.c:1-10 and src/stock.c:40-50
tick; run ait future demand src/order.c src/stock.c specs/both.yaml as d2 future offer src/stock.c:1-10 specs/pass.yaml as o1 future offer src/stock.c:40-50 specs/pass.yaml as o2 undertake d2 o1 undertake d2 o2 -m m
check "3: prepare ok" test "$rc" = 0
D2=$(id_of d2); O1=$(id_of o1); O2=$(id_of o2)
sed -i.bak -e '1s/.*/s1 lowest/' -e '45s/.*/s45 highest/' src/stock.c && rm -f src/stock.c.bak; git add src/stock.c
expect_err "3: fulfill O1 O2 asks for results" E_NEEDS_RESULTS ait fulfill "$O1" "$O2" -m m
check "3: ready yes" out_has "ready yes"
check "3: tasks D2:both once, O1:pass, O2:pass" test "$(tasks)" = "$(printf 'task %s:specs/both.yaml exec\ntask %s:specs/pass.yaml exec\ntask %s:specs/pass.yaml exec' "$D2" "$O1" "$O2" | sort)"
tick; with_r pass ait fulfill "$O1" "$O2" -m m
check "3: fulfill -r pass ok" test "$rc" = 0
check "3: fulfilled O1" out_has "fulfilled $O1"
check "3: fulfilled O2" out_has "fulfilled $O2"
check "3: fulfilled D2 via O1 O2" out_has "fulfilled $D2 via $(printf '%s\n%s\n' "$O1" "$O2" | sort | tr '\n' ' ' | sed 's/ $//')"

# 4 not yet: no listener (ait listen); the fact that D is current at A's HEAD was checked in 2

done_testing
