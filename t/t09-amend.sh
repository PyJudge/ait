#!/bin/sh
# T09 drop·amend (1.4, 2.3, 7.3.2) -- amend parts; the 'future drop' clause is not implemented,
# so no drop X exists to amend, fulfil or cancel
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent agent-a)
prompt_defs
exec_def pass

id_of() { sed -n "s/^new \([^ ]*\) $1\$/\1/p" "$T/out"; }
tasks_of() { sed -n 's/^  - task: //p' "$1" | tr -d '"'; }
out_is() { [ "$(cat "$T/out")" = "$1" ]; }

tick; with_r pass ait offer src/stock.c:1-10 specs/p.yaml as o1 offer src/order.c specs/p.yaml as o4 future demand src/order.c src/stock.c specs/p.yaml as d1 -m m
check "prep: ok" [ "$rc" = 0 ]
O1=$(id_of o1) O4=$(id_of o4) D1=$(id_of d1)
PREP=$(git rev-parse HEAD)

# 1, 2: not yet: 'future drop' is not supported yet (the clause is not implemented)
desc="1: future drop O1"
expect_unchanged expect_err "$desc" UNSUPPORTED ait future drop "$O1" as x -m m

# 3
tick; expect_ok "3: future amend D1 - src/stock.c:5-9 -" ait future amend "$D1" - src/stock.c:5-9 - -m m
check "3: stdout has changed D1" out_has "changed $D1"
check "3: provider is src/stock.c:5-9 <blob>" sh -c "git show HEAD:.ait/$D1 | grep -qx 'provider src/stock.c:5-9 $(blob HEAD:src/stock.c)'"

# 4
expect_err "4: future amend D1 with two args (demand takes three)" E_USAGE ait future amend "$D1" src/order.c - -m m
desc="4: future amend D1 - - -"
expect_unchanged expect_ok "$desc" ait future amend "$D1" - - - -m m
check "4: stdout is noop" out_is noop

# 5
tick; expect_ok "5: future amend D1 - - tests/pass.sh" ait future amend "$D1" - - specs/pass.yaml -m m
check "5: changed D1" out_has "changed $D1"
check "5: file lines are the definition and its input" [ "$(git show HEAD:.ait/$D1 | grep '^file ')" = "file specs/pass.yaml 100644 $(blob HEAD:specs/pass.yaml) def
file tests/pass.sh 100755 $(blob HEAD:tests/pass.sh) input" ]

# 6
desc="6: amend O1 src/stock.c p without results"
expect_unchanged expect_err "$desc" E_NEEDS_RESULTS ait amend "$O1" src/stock.c specs/p.yaml -m m
check "6: task O1:specs/p.yaml" out_has "task $O1:specs/p.yaml prompt"
tick; with_r pass ait amend "$O1" src/stock.c specs/p.yaml -m m
check "6: -r pass ok" [ "$rc" = 0 ]
check "6: changed O1" out_has "changed $O1"
check "6: check O1 reported" out_has "check $O1 reported"
check "6: O1 provider is src/stock.c" sh -c "git show HEAD:.ait/$O1 | grep -qx 'provider src/stock.c $(blob HEAD:src/stock.c)'"

# 6, empty amend
desc="6: empty amend O4 without results"
expect_unchanged expect_err "$desc" E_NEEDS_RESULTS ait amend "$O4" -m m
check "6: empty amend task O4:specs/p.yaml" out_has "task $O4:specs/p.yaml prompt"
check "6: only one task" [ "$(grep -c '^task ' "$T/out")" = 1 ]
before=$(git rev-parse HEAD)
tick; with_r pass ait amend "$O4" -m m
check "6: empty amend -r pass ok" [ "$rc" = 0 ]
# 6.6: O4 already confirms these pins as reported, so the same check leaves its record as it was
check "6: stdout is noop" out_is noop
check "6: no new commit" [ "$(git rev-parse HEAD)" = "$before" ]
check "6: O4's check line is reported pins(HEAD,O4)" sh -c "git show HEAD:.ait/$O4 | grep -qx 'check reported $(pins HEAD $O4)'"
# 2.2.2: a forced check of the same pins becomes reported when checked with results
tick; ait offer src/order.c:1-5 specs/p.yaml as o5 --force -m m >"$T/out"; O5=$(id_of o5)
check "6: O5 is forced" sh -c "git show HEAD:.ait/$O5 | grep -qx 'check forced $(pins HEAD $O5)'"
before=$(git rev-parse HEAD)
tick; with_r pass ait amend "$O5" -m m
check "6: the forced check is checked again" sh -c "[ $rc = 0 ] && grep -qx 'check $O5 reported' '$T/out' && [ \"\$(git rev-parse HEAD^)\" = $before ]"
check "6: only the check line changed, to reported" sh -c "[ \"\$(git show HEAD:.ait/$O5 | grep -v '^check \|^prev ')\" = \"\$(git show HEAD^:.ait/$O5 | grep -v '^check \|^prev ')\" ] && git show HEAD:.ait/$O5 | grep -qx 'check reported $(pins HEAD $O5)'"

# 7, 8, 9: not yet: they use drops ('future drop', 'future amend X', fulfill X); the
# 'future drop' clause is not implemented, so there is no X to work on

# 10 (T09-10): prepare again and do 1 (O1 is src/stock.c:1-10 again), then reopen O1
setup
A=$(ait session open --agent agent-a)
prompt_defs
exec_def pass
tick; with_r pass ait offer src/stock.c:1-10 specs/p.yaml as o1 offer src/order.c specs/p.yaml as o4 future demand src/order.c src/stock.c specs/p.yaml as d1 -m m
check "10: prep again: ok" [ "$rc" = 0 ]
O1=$(id_of o1)
desc="10: 1 again: future drop O1"
expect_unchanged expect_err "$desc" UNSUPPORTED ait future drop "$O1" as x -m m
tick; expect_ok "10: future amend O1" ait future amend "$O1" -m m
check "10: changed O1" out_has "changed $O1"
check "10: O1 is future" sh -c "git show HEAD:.ait/$O1 | grep -qx 'state future'"
# not yet: the rest of 10 checks drop X's hints and 'fulfill X' (no X: 'future drop' is not implemented)

done_testing
