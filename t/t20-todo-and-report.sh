#!/bin/sh
# T20 todo와 이행 준비 보고: 내 할 일과 그 진척 (7.7.6, 7.8)
. "$(dirname "$0")/lib.sh"

ait_a() { AIT_SESSION=$A ait "$@"; }
ait_b() { AIT_SESSION=$B ait "$@"; }
out_is() { printf '%s\n' "$@" | cmp -s - "$T/out"; }
# the todo block of ID in the last output (from 'id ID' to its blank line)
block() { awk -v id="id $1" '$0==id{p=1} p{print} p&&$0==""{exit}' "$T/out"; }
block_has() { block "$1" | grep -qxF -- "$2"; }
lines_of() { grep -E "^($1) " "$T/out"; }
yval() { sed -n "s/^$2: \"\{0,1\}\([^\"]*\)\"\{0,1\}\$/\1/p" "$1" | head -1; }

setup
A=$(ait session open --agent agent-a)
B=$(ait session open --agent agent-b)
prompt_defs; exec_def guard; exec_def pass
tick; run ait_a future demand src/order.c src/stock.c specs/low.yaml,specs/guard.yaml as d -m m
D=$(new_id)
# mini: line ranges are not supported; the scenario's src/stock.c:1-10 becomes src/stock.c
tick; run ait_b future offer src/stock.c specs/pass.yaml as o undertake "$D" o -m m
O=$(new_id)
check "prepare: D and O exist" test -n "$D" -a -n "$O"

# 1
expect_ok "1: B todo" ait_b todo
check "1: stdout is exactly the O block" out_is "id $O" "role offer" "state future" "creator agent-b" "provider src/stock.c" \
	"file specs/pass.yaml exec" "    pass" "    \$ ./tests/pass.sh" "file tests/pass.sh input" \
	"undertakes $D future agent-a" "undertakes-provider $D src/stock.c" \
	"undertakes-file $D specs/guard.yaml exec" "    guard" "    \$ ./tests/guard.sh" \
	"undertakes-file $D specs/low.yaml prompt" "    최저가가 보장될 것." \
	"undertakes-file $D tests/guard.sh input" ""

# 2
expect_ok "2: A todo" ait_a todo
check "2: D block" test -n "$(block "$D")"
check "2: undertaken-by O" block_has "$D" "undertaken-by $O future agent-b"
check "2: file specs/low.yaml prompt with body" sh -c "awk -v id='id $D' '\$0==id{p=1} p{print} p&&\$0==\"\"{exit}' '$T/out' | grep -A1 -xF 'file specs/low.yaml prompt' | grep -qxF '    최저가가 보장될 것.'"
check "2: file specs/guard.yaml exec with body and command" sh -c "awk -v id='id $D' '\$0==id{p=1} p{print} p&&\$0==\"\"{exit}' '$T/out' | grep -A2 -xF 'file specs/guard.yaml exec' | tr '\n' '|' | grep -qxF 'file specs/guard.yaml exec|    guard|    \$ ./tests/guard.sh|'"
check "2: file tests/guard.sh input" block_has "$D" "file tests/guard.sh input"

# 3
expect_err "3: B fulfill O asks for results" E_NEEDS_RESULTS ait_b fulfill "$O" -m m
Q=$(out_value check-request); W=$(out_value check-result)
check "3: report is exactly the 7.7.6 form" out_is "id $O" "provider $D src/stock.c" "provider $O src/stock.c" "" \
	"task $D:specs/guard.yaml exec" "task $D:specs/low.yaml prompt" "task $O:specs/pass.yaml exec" \
	"check-request $Q" "check-result $W" "ready yes"
cp "$W" "$T/w3.yaml"; in3=$(yval "$W" input)
rep3=$(lines_of 'provider|task')

# 4
sed -i.bak '1s/.*/s1 guard/' src/stock.c && rm -f src/stock.c.bak; git add src/stock.c
expect_err "4: B fulfill O asks for results again" E_NEEDS_RESULTS ait_b fulfill "$O" -m m
check "4: provider and task lines as in 3 (no ?changed for future)" test "$(lines_of 'provider|task')" = "$rep3"
check "4: ready yes" out_has "ready yes"
check "4: the input of W differs from 3" test "$(yval "$W" input)" != "$in3"
fill "$T/w3.yaml" pass
expect_err "4: the result of 3 cannot fulfill" E_NEEDS_RESULTS ait_b fulfill "$O" -m m -r "$T/w3.yaml"

# 5
echo '# x' >> tests/pass.sh; git add tests/pass.sh
expect_err "5: staged spec file is E_MIXED" E_MIXED ait_b fulfill "$O" -m m
check "5: report mixed tests/pass.sh" out_has "mixed tests/pass.sh"
check "5: report ready no" out_has "ready no"
git checkout -q HEAD -- tests/pass.sh

# 6
expect_ok "6: todo src/order.c" ait todo src/order.c
check "6: D block" test -n "$(block "$D")"
check "6: no O block" test -z "$(block "$O")"

# 7
expect_ok "7: todo without session" env AIT_SESSION=- ait todo
check "7: D and O blocks" test -n "$(block "$D")" -a -n "$(block "$O")"

# 8
printf '#!/bin/sh\ntouch %s/ran; exit 1\n' "$T" > tests/guard.sh
desc="8: todo commits nothing"
expect_unchanged expect_ok "$desc" ait todo
desc="8: fulfill without -r commits nothing"
expect_unchanged expect_err "$desc" E_NEEDS_RESULTS ait_b fulfill "$O" -m m
check "8: nothing was run" test ! -e "$T/ran"
git checkout -q -- tests/guard.sh

# 9
tick; with_r pass ait_b fulfill "$O" -m m
check "9: fulfill O -r pass ok" test "$rc" = 0
check "9: fulfilled D via O" out_has "fulfilled $D via $O"
check "9: check D reported" out_has "check $D reported"
check "9: check O reported" out_has "check $O reported"
check "9: three tasks in the message" test "$(git log -1 --format=%B | grep -cE '^  - task: ')" = 3

# 10
sed -i.bak '3s/.*/s3 z/' src/stock.c && rm -f src/stock.c.bak
tick; git commit -qam three 2>/dev/null
expect_ok "10: todo src/stock.c" ait todo src/stock.c
check "10: O state current" block_has "$O" "state current"
check "10: O provider ?changed" block_has "$O" "provider src/stock.c ?changed"
check "10: O unverified yes" block_has "$O" "unverified yes"
check "10: D provider ?changed" block_has "$D" "provider src/stock.c ?changed"
check "10: D unverified yes" block_has "$D" "unverified yes"
# the same facts by ID (separates the scope argument from the display)
expect_ok "10: todo O D" ait todo "$O" "$D"
check "10 (by ID): O state current" block_has "$O" "state current"
check "10 (by ID): O provider ?changed" block_has "$O" "provider src/stock.c ?changed"
check "10 (by ID): O unverified yes" block_has "$O" "unverified yes"
check "10 (by ID): D provider ?changed" block_has "$D" "provider src/stock.c ?changed"
check "10 (by ID): D unverified yes" block_has "$D" "unverified yes"
expect_ok "10: A todo" ait_a todo
check "10: A sees D" test -n "$(block "$D")"
expect_ok "10: B todo" ait_b todo
check "10: B does not see O" test -z "$(block "$O")"

done_testing
