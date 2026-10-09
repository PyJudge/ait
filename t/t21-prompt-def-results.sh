#!/bin/sh
# T21 prompt 정의와 확인 결과 (1.2, 7.7, 8)
. "$(dirname "$0")/lib.sh"

block() { awk -v id="id $1" '$0==id{p=1} p{print} p&&$0==""{exit}' "$T/out"; }
yval() { sed -n "s/^ *$2: \"\{0,1\}\([^\"]*\)\"\{0,1\}\$/\1/p" "$1" | head -1; }

# the preparation, then step 2's first run (sets D, Q, W)
prep() {
	setup
	A=$(ait session open --agent agent-a)
	prompt_defs
	tick; run ait future demand src/order.c src/stock.c specs/low.yaml as d -m m
	D=$(new_id)
	sed -i.bak '7s/.*/s7 lowest/' src/stock.c && rm -f src/stock.c.bak; git add src/stock.c
	run ait fulfill "$D" -m m
	Q=$(out_value check-request); W=$(out_value check-result)
}

prep
# 1
expect_ok "1: todo" ait todo
check "1: D block has file specs/low.yaml prompt" sh -c "awk -v id='id $D' '\$0==id{p=1} p{print} p&&\$0==\"\"{exit}' '$T/out' | grep -qxF 'file specs/low.yaml prompt'"
check "1: D block has the body" sh -c "awk -v id='id $D' '\$0==id{p=1} p{print} p&&\$0==\"\"{exit}' '$T/out' | grep -A1 -xF 'file specs/low.yaml prompt' | grep -qxF '    최저가가 보장될 것.'"

# 2
expect_err "2: fulfill D asks for results" E_NEEDS_RESULTS ait fulfill "$D" -m m
check "2: task line" out_has "task $D:specs/low.yaml prompt"
check "2: check-request line" test -n "$(out_value check-request)"
check "2: check-result line" test -n "$(out_value check-result)"
check "2: ready yes" out_has "ready yes"
Q=$(out_value check-request); W=$(out_value check-result)
in2=$(yval "$W" input)
check "2: Q has input" test -n "$(yval "$Q" input)"
check "2: Q has tree" test "$(yval "$Q" tree)" = "$(git write-tree)"
check "2: Q has task D:specs/low.yaml" test "$(sed -n "s/^  - task: \"\(.*\)\"$/\1/p" "$Q")" = "$D:specs/low.yaml"
check "2: Q has engine prompt" test "$(yval "$Q" engine)" = prompt
check "2: Q has the body" sh -c "grep -qxF '    body: \"최저가가 보장될 것.\"' '$Q' || grep -A1 -xF '    body: |' '$Q' | grep -qxF '      최저가가 보장될 것.'"
check "2: Q has no command" sh -c "! grep -q 'command:' '$Q'"

# 3
expect_err "3: again" E_NEEDS_RESULTS ait fulfill "$D" -m m
check "3: same Q path" test "$(out_value check-request)" = "$Q"
check "3: same W path" test "$(out_value check-result)" = "$W"
check "3: same input" test "$(yval "$W" input)" = "$in2"

# 4
fill "$W" pass
tick; expect_ok "4: fulfill D -r W" ait fulfill "$D" -m m -r "$W"
check "4: check D reported" out_has "check $D reported"
check "4: message has the task" sh -c "git log -1 --format=%B | grep -qF -- '- task: $D:specs/low.yaml'"
check "4: message has outcome" sh -c "git log -1 --format=%B | grep -qE '^ +outcome: pass'"
check "4: message has basis" sh -c "git log -1 --format=%B | grep -qE '^ +basis: t'"

# 5
prep; fill "$W" fail
desc="5: fail is E_CHECK_FAILED"
expect_unchanged expect_err "$desc" E_CHECK_FAILED ait fulfill "$D" -m m -r "$W"

prep; fill "$W" pass; sed -i.bak 's/^    basis: t$/    basis:/' "$W" && rm -f "$W.bak"
desc="5: pass with an empty basis is E_FORMAT"
expect_unchanged expect_err "$desc" E_FORMAT ait fulfill "$D" -m m -r "$W"

prep; fill "$W" pass; cp "$W" "$T/w.yaml"; in2=$(yval "$W" input)
sed -i.bak '8s/.*/s8 more/' src/stock.c && rm -f src/stock.c.bak; git add src/stock.c
desc="5: changed input is E_NEEDS_RESULTS"
expect_unchanged expect_err "$desc" E_NEEDS_RESULTS ait fulfill "$D" -m m -r "$T/w.yaml"
check "5: a new W is written (new input)" sh -c "[ -n \"$(yval "$W" input)\" ] && [ \"$(yval "$W" input)\" != '$in2' ]"
check "5: a new Q is written (new input)" test "$(yval "$Q" input)" = "$(yval "$W" input)"

prep
run ait fulfill "$D" -m m -r "$T/none.yaml"
check "5: missing -r file is 128 fatal: could not read" sh -c "[ $rc = 128 ] && head -1 '$T/err' | grep -q '^fatal: could not read'"

prep; fill "$W" fail; cp "$W" "$T/w.yaml"
expect_err "5: fail is rejected" E_CHECK_FAILED ait fulfill "$D" -m m -r "$W"
check "5: the rejection leaves W as filled" cmp -s "$W" "$T/w.yaml"

prep; fill "$W" pass; cp "$W" "$T/w.yaml"
expect_err "5: rerun without -r" E_NEEDS_RESULTS ait fulfill "$D" -m m
check "5: W is kept (same input)" cmp -s "$W" "$T/w.yaml"

# 6
prep
tick; run git commit -qm plain
check "6: a plain commit succeeds" test "$rc" = 0
check "6: no warning" test ! -s "$T/err"

done_testing
