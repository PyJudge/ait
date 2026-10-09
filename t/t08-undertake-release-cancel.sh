#!/bin/sh
# T08 undertake·release·cancel·no-op (1.6, 2.3, 7.3.2)
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent agent-a)
prompt_defs

# the ID printed on the 'new ID ALIAS' line of the last run
id_of() { sed -n "s/^new \([^ ]*\) $1\$/\1/p" "$T/out"; }
# the task names of a check-result file, unquoted
tasks_of() { sed -n 's/^  - task: //p' "$1" | tr -d '"'; }
# stdout is exactly the given lines
out_is() { [ "$(cat "$T/out")" = "$1" ]; }

tick; with_r pass ait future demand src/order.c src/stock.c specs/p.yaml as d future offer src/stock.c specs/p.yaml as o future offer src/stock.c specs/p.yaml as o2 offer src/stock.c specs/p.yaml as o3 undertake d o -m m
check "prep: ok" [ "$rc" = 0 ]
D=$(id_of d) O=$(id_of o) O2=$(id_of o2) O3=$(id_of o3)
check "prep: four IDs" [ -n "$D" -a -n "$O" -a -n "$O2" -a -n "$O3" ]

# 1
expect_err "1: undertake O D (roles swapped)" E_ID ait undertake "$O" "$D" -m m
# 2
desc="2: undertake D O again"
expect_unchanged expect_ok "$desc" ait undertake "$D" "$O" -m m
check "2: stdout is noop" out_is noop
# 3
desc="3: release D O2 (not undertaken)"
expect_unchanged expect_ok "$desc" ait release "$D" "$O2" -m m
check "3: stdout is noop" out_is noop
# 4
desc="4: undertake D O3 (current O, future D)"
expect_unchanged expect_err "$desc" E_STATE ait undertake "$D" "$O3" -m m
check "4: hint undertakes with a future offer" err_has "ait future offer src/stock.c specs/p.yaml as o undertake $D o -m <why>"
# 5
prevO=$(blob "HEAD:.ait/$O")
tick; expect_ok "5: future cancel O while undertaken" ait future cancel "$O" -m m
check "5: rec(O) state cancelled" sh -c "git show HEAD:.ait/$O | grep -qx 'state cancelled'"
check "5: rec(O) prev is the previous version" sh -c "git show HEAD:.ait/$O | grep -qx 'prev $prevO'"
expect_ok "5: todo D" ait todo "$D"
check "5: todo D shows undertaken-by O cancelled" out_has "undertaken-by $O cancelled agent-a"
# 6
tick; expect_ok "6: release D O" ait release "$D" "$O" -m m
check "6: stdout has released D O" out_has "released $D $O"
check "6: undertake file is released" sh -c "git show HEAD:.ait/undertake/$D-$O | head -1 | grep -qx 'state released'"
# 7
expect_err "7: undertake D O (O cancelled)" E_STATE ait undertake "$D" "$O" -m m
check "7: hint undertakes with a future offer" err_has "ait future offer src/stock.c specs/p.yaml as o undertake $D o -m <why>"
desc="7: undertake D O2 release D O2"
expect_unchanged expect_ok "$desc" ait undertake "$D" "$O2" release "$D" "$O2" -m m
check "7: stdout is exactly noop" out_is noop
check "7: no undertake file D-O2 in HEAD" sh -c "! git cat-file -e HEAD:.ait/undertake/$D-$O2 2>/dev/null"
check "7: no undertake file D-O2 in the work tree" [ ! -e ".ait/undertake/$D-$O2" ]
# 8
expect_err "8: future cancel O3 (current)" E_STATE ait future cancel "$O3" -m m
expect_ok "8: future cancel O (already cancelled)" ait future cancel "$O" -m m
check "8: stdout is noop" out_is noop
# 9
tick; with_r pass ait demand src/order.c src/stock.c specs/p.yaml as d4 undertake d4 "$O3" -m m
check "9: demand + undertake with current O3 is ok" [ "$rc" = 0 ]
D4=$(id_of d4)
check "9: new D4" [ -n "$D4" ]
check "9: undertaken D4 O3" out_has "undertaken $D4 $O3"
check "9: the only task is +1:specs/p.yaml" [ "$(tasks_of "$W")" = "+1:specs/p.yaml" ]
check "9: check D4 reported" out_has "check $D4 reported"
check "9: O3 is not checked" sh -c "! grep -q '^check $O3' '$T/out'"

done_testing
