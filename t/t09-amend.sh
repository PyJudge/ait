#!/bin/sh
# T09 drop·amend (1.4, 2.3, 7.3.2) -- amend parts; drop is not in ait-mini
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent agent-a)
prompt_defs
exec_def pass

id_of() { sed -n "s/^new \([^ ]*\) $1\$/\1/p" "$T/out"; }
tasks_of() { sed -n 's/^  - task: //p' "$1" | tr -d '"'; }
out_is() { [ "$(cat "$T/out")" = "$1" ]; }
checks_of() { git log -1 --format='%(trailers:key=Ait-Check,valueonly)' "$1"; }

# preparation as written uses a line range, which ait-mini does not have
desc="prep with src/stock.c:1-10"
expect_unchanged expect_err "$desc" MINI ait offer src/stock.c:1-10 specs/p.yaml as o1 offer src/order.c specs/p.yaml as o4 future demand src/order.c src/stock.c specs/p.yaml as d1 -m m
# mini: line ranges are not supported; O1 takes the file scope src/order.c instead
# (not src/stock.c, so that step 6's 'amend O1 src/stock.c p' still changes the provider)
tick; with_r pass ait offer src/order.c specs/p.yaml as o1 offer src/order.c specs/p.yaml as o4 future demand src/order.c src/stock.c specs/p.yaml as d1 -m m
check "prep: ok" [ "$rc" = 0 ]
O1=$(id_of o1) O4=$(id_of o4) D1=$(id_of d1)
PREP=$(git rev-parse HEAD)

# 1, 2: mini: 'future drop' is not in ait-mini
desc="1: future drop O1"
expect_unchanged expect_err "$desc" MINI ait future drop "$O1" as x -m m

# 3: mini: the range src/stock.c:5-9 is not supported
desc="3: future amend D1 with a line range"
expect_unchanged expect_err "$desc" MINI ait future amend "$D1" - src/stock.c:5-9 - -m m
# the same amend with a file scope
tick; expect_ok "3: future amend D1 - src/order.c -" ait future amend "$D1" - src/order.c - -m m
check "3: stdout has changed D1" out_has "changed $D1"
check "3: provider is src/order.c <blob>" sh -c "git show HEAD:.ait/$D1 | grep -qx 'provider src/order.c $(blob HEAD:src/order.c)'"

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
check "6: stdout is request R and check O4 reported" sh -c "[ \"\$(cat '$T/out' | sed 's/^request R[0-9a-f]\{12\}\$/request R/')\" = 'request R
check $O4 reported' ]"
check "6: a new commit" [ "$(git rev-parse HEAD^)" = "$before" ]
check "6: record unchanged" [ "$(blob HEAD:.ait/$O4)" = "$(blob HEAD^:.ait/$O4)" ]
check "6: commit changes no file" [ -z "$(git diff --name-only HEAD^ HEAD)" ]
check "6: Ait-Check O4 reported pins(HEAD,O4)" [ "$(checks_of HEAD)" = "$O4 reported $(pins HEAD $O4)" ]
check "6: same PINS as O4's creating commit" sh -c "git log -1 --format='%(trailers:key=Ait-Check,valueonly)' $PREP | grep -qx '$O4 reported $(pins HEAD $O4)'"

# 7, 8, 9: mini: they use drops ('future drop', 'future amend X', fulfill X)

# 10, the amend part: future amend of a current contract reopens it
tick; expect_ok "10: future amend O1" ait future amend "$O1" -m m
check "10: changed O1" out_has "changed $O1"
check "10: O1 is future" sh -c "git show HEAD:.ait/$O1 | grep -qx 'state future'"
# mini: the rest of 10 checks drop X's hints and 'fulfill X'

done_testing
