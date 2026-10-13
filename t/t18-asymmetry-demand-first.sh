#!/bin/sh
# T18 비대칭 ①: demand는 먼저 이행될 수 있다 (1.6, 7.7)
. "$(dirname "$0")/lib.sh"

ait_a() { AIT_SESSION=$A ait "$@"; }
ait_b() { AIT_SESSION=$B ait "$@"; }
tasks() { grep '^task ' "$T/out"; }

setup
A=$(ait session open --agent agent-a)
B=$(ait session open --agent agent-b)
exec_def lowest; exec_def highest
tick; run ait_a future demand src/order.c src/stock.c specs/lowest.yaml as d -m "최저가를 내놓아라"
D=$(new_id)
tick; run ait_b future offer src/stock.c specs/highest.yaml as o undertake "$D" o -m "최고가와 최저가를 내놓겠다"
O=$(new_id)
check "prepare: D and O exist" test -n "$D" -a -n "$O"

# 1
sed -i.bak '1s/.*/s1 lowest/' src/stock.c && rm -f src/stock.c.bak; git add src/stock.c
expect_err "1: A fulfill D asks for results" E_NEEDS_RESULTS ait_a fulfill "$D" -m m
check "1: the only task is D:specs/lowest.yaml" test "$(tasks)" = "task $D:specs/lowest.yaml exec"
tick; with_r pass ait_a fulfill "$D" -m m
check "1: fulfill D -r pass ok" test "$rc" = 0
R1=$(out_value request)
check "1: fulfilled D without via" out_has "fulfilled $D"
check "1: rec(O) still future" sh -c "git show HEAD:.ait/$O | grep -qxF 'state future'"

# 2
expect_err "2: B fulfill O asks for results" E_NEEDS_RESULTS ait_b fulfill "$O" -m m
check "2: tasks are D:lowest and O:highest" test "$(tasks)" = "$(printf 'task %s:specs/lowest.yaml exec\ntask %s:specs/highest.yaml exec' "$D" "$O" | sort)"
check "2: no reopen hint" sh -c "! grep -qF 'ait future amend' '$T/err'"
W=$(out_value check-result); Q=$(out_value check-request)
fill "$W" pass "$O:specs/highest.yaml=fail"
expect_err "2: O's highest fails" E_CHECK_FAILED ait_b fulfill "$O" -m m -r "$W"

# 4 (in the state of 2)
run ait_b fulfill "$O" -m m
W=$(out_value check-result)
fill "$W" pass "$D:specs/lowest.yaml=fail"
expect_err "4: broken D blocks O" E_CHECK_FAILED ait_b fulfill "$O" -m m -r "$W"
# T18-4 (5.8 E_CHECK_FAILED, 5.9a): B made O, so release D from O; D is current, so change its condition.
# The default session of this worktree is A, so B's commands carry AIT_SESSION
check "4: hint releases D from O" err_has "to fulfill '$O' without '$D', release it first: 'AIT_SESSION=$B ait release $D $O -m <why>', then rerun this command"
check "4: hint changes D's condition" err_has "to change its condition, reopen it with the new one: 'AIT_SESSION=$B ait future amend $D - - <new defs> -m <why>', then rerun this command"
check "4: no plain reopen hint for D" sh -c "! grep -qF 'ait future amend $D -m <why>' '$T/err'"

# 3
sed -i.bak '2s/.*/s2 highest/' src/stock.c && rm -f src/stock.c.bak; git add src/stock.c
tick; with_r pass ait_b fulfill "$O" -m m
check "3: fulfill O -r pass ok" test "$rc" = 0
R3=$(out_value request)
check "3: fulfilled O" out_has "fulfilled $O"
check "3: check D reported" out_has "check $D reported"
check "3: check O reported" out_has "check $O reported"
check "3: no fulfilled D line" sh -c "! grep -q '^fulfilled $D' '$T/out'"
check "3: D re-pinned" sh -c "git show HEAD:.ait/$D | grep -qxF 'provider src/stock.c $(blob HEAD:src/stock.c)'"

# 5: the events, read from each commit's trailers and records (peek is not implemented)
c1=$(git log --format=%H --grep="^Ait-Request: $R1\$")
c3=$(git log --format=%H --grep="^Ait-Request: $R3\$")
check "5: R1 is a FULFILL by agent-a with D reported" sh -c "git log -1 --format=%B $c1 | grep -qxF 'Ait-Agent: agent-a' && [ \"\$(git show $c1:.ait/$D | sed -n 's/^check //p')\" = 'reported $(pins "$c1" "$D")' ] && git log -1 --format=%s $c1 | grep -q '^\[FULFILL\]'"
check "5: R1 commit makes D current itself (no via)" sh -c "git show $c1:.ait/$D | grep -qxF 'state current' && git show $c1:.ait/$O | grep -qxF 'state future'"
check "5: R3 is by agent-b with D reported (verified)" sh -c "git log -1 --format=%B $c3 | grep -qxF 'Ait-Agent: agent-b' && [ \"\$(git show $c3:.ait/$D | sed -n 's/^check //p')\" = 'reported $(pins "$c3" "$D")' ]"

done_testing
