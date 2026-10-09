#!/bin/sh
# T10 current 등록의 확인 결과 (6.5, 8)
. "$(dirname "$0")/lib.sh"

fresh() { setup; A=$(ait session open --agent agent-a); prompt_defs; exec_def pass; }
id_new() { sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out" | head -1; }
checks_of() { git log -1 --format='%(trailers:key=Ait-Check,valueonly)' "$1"; }
# a YAML file has KEY with VALUE, compared as values (quotes removed, 0.2)
y_has() { tr -d '"' < "$1" | grep -qE "^ *(- )?$2: $3\$"; }
# the block string under 'body: |' in FILE is VALUE (first task with that body key)
y_body() { awk '/^ *body: \|$/ {getline; sub(/^ +/, ""); print; exit}' "$1"; }
# the input value of a check-result file
w_input() { sed -n 's/^input: //p' "$1" | tr -d '"'; }

# 1
fresh
desc="1: offer without results"
tick; expect_unchanged expect_err "$desc" E_NEEDS_RESULTS ait offer src/stock.c specs/pass.yaml -m m
check "1: task +1:specs/pass.yaml exec" out_has "task +1:specs/pass.yaml exec"
Q=$(out_value check-request) W=$(out_value check-result)
check "1: check-request and check-result lines" [ -n "$Q" -a -n "$W" ]
check "1: Q has input" y_has "$Q" input '[0-9a-f]{40}'
check "1: Q has tree" y_has "$Q" tree '[0-9a-f]{40}'
check "1: Q has task +1:specs/pass.yaml" y_has "$Q" task '\+1:specs/pass.yaml'
check "1: Q role offer" y_has "$Q" role offer
check "1: Q provider src/stock.c" y_has "$Q" provider src/stock.c
check "1: Q engine exec" y_has "$Q" engine exec
check "1: Q body pass" [ "$(y_body "$Q")" = pass ]
check "1: Q command [./tests/pass.sh]" y_has "$Q" command '\[\./tests/pass\.sh\]'
check "1: Q success [0]" y_has "$Q" success '\[0\]'
check "1: W has the same input" [ "$(w_input "$W")" = "$(sed -n 's/^input: //p' "$Q" | tr -d '"')" ]
check "1: W has one task" [ "$(grep -c '^  - task: ' "$W")" = 1 ]
check "1: W task +1:specs/pass.yaml" y_has "$W" task '\+1:specs/pass.yaml'
check "1: W outcome is empty" grep -qE '^ +outcome: *$' "$W"

# 3 (with 1's W, before 2): fail
fill "$W" fail
desc="3: -r with a fail result"
tick; expect_unchanged expect_err "$desc" E_CHECK_FAILED ait offer src/stock.c specs/pass.yaml -m m -r "$W"
check "3: stderr names +1:specs/pass.yaml" err_has "+1:specs/pass.yaml"
check "3: fail hint: code first" err_has "stage <your code> and run 'ait commit -m <why>', then rerun this command"
check "3: fail hint: make it a promise" err_has "ait future offer src/stock.c specs/pass.yaml -m <why>"
check "3: fail: no reopen hint" sh -c "! grep -q 'reopen' '$T/err'"
# 3: unknown
fill "$W" unknown
desc="3: -r with an unknown result"
tick; expect_unchanged expect_err "$desc" E_CHECK_FAILED ait offer src/stock.c specs/pass.yaml -m m -r "$W"
check "3: stderr names +1:specs/pass.yaml" err_has "+1:specs/pass.yaml"
check "3: unknown hint: check where the environment is" err_has "and rerun with -r '$W'"
check "3: unknown hint: --force" err_has "add --force"
check "3: unknown hint: make it a promise" err_has "ait future offer src/stock.c specs/pass.yaml -m <why>"
check "3: unknown: no 'ait commit' hint" sh -c "! grep -q 'ait commit' '$T/err'"

# 2
fill "$W" pass
cp "$W" "$T/w.pass"
tick; expect_ok "2: -r with a pass result" ait offer src/stock.c specs/pass.yaml -m m -r "$W"
O=$(id_new)
check "2: check O reported" out_has "check $O reported"
check "2: Ait-Check O reported pins(HEAD,O)" [ "$(checks_of HEAD)" = "$O reported $(pins HEAD $O)" ]
R=$(out_value request)
expected=$(printf '[CONTRACT] m\n\n%s\n\nAit-Request: %s\nAit-Agent: agent-a\nAit-Session: %s\nAit-Check: %s reported %s' \
	"$(sed "s/^  - task: +1:/  - task: $O:/" "$T/w.pass")" "$R" "$A" "$O" "$(pins HEAD $O)")
check "2: the message is subject, results, trailers" [ "$(git log -1 --format=%B)" = "$expected" ]

# 4
fresh
tick; with_r pass ait offer src/stock.c specs/p.yaml -m m
check "4: prompt definition -r pass ok" [ "$rc" = 0 ]
check "4: check O reported" out_has "check $(id_new) reported"

# 5
fresh
desc="5: two definitions"
tick; expect_unchanged expect_err "$desc" E_NEEDS_RESULTS ait offer src/stock.c specs/p.yaml,specs/pass.yaml -m m
check "5: task +1:specs/p.yaml prompt" out_has "task +1:specs/p.yaml prompt"
check "5: task +1:specs/pass.yaml exec" out_has "task +1:specs/pass.yaml exec"
W=$(out_value check-result)
fill "$W" pass
IN=$(w_input "$W")
res() { printf '  - task: %s\n    outcome: %s\n    basis: %s\n    reason: t\n' "$1" "$2" "$3"; }
bad_w() { desc=$1; shift; { echo "input: $IN"; echo results:; "$@"; } > "$T/bad.yaml"
	tick; expect_unchanged expect_err "$desc" E_FORMAT ait offer src/stock.c specs/p.yaml,specs/pass.yaml -m m -r "$T/bad.yaml"; }
one() { res +1:specs/p.yaml pass t; }
added() { res +1:specs/p.yaml pass t; res +1:specs/pass.yaml pass t; res +1:specs/x.yaml pass t; }
twice() { res +1:specs/p.yaml pass t; res +1:specs/pass.yaml pass t; res +1:specs/pass.yaml pass t; }
nobasis() { res +1:specs/p.yaml pass t; printf '  - task: +1:specs/pass.yaml\n    outcome: pass\n    basis:\n    reason: t\n'; }
okout() { res +1:specs/p.yaml pass t; res +1:specs/pass.yaml ok t; }
bad_w "5: a task removed" one
bad_w "5: a task added" added
bad_w "5: a task twice" twice
bad_w "5: a pass with an empty basis" nobasis
bad_w "5: outcome ok" okout

# 6
fresh
tick; run ait offer src/stock.c specs/p.yaml -m m; W=$(out_value check-result); in1=$(w_input "$W")
tick; run ait offer src/stock.c specs/p.yaml -m m; in2=$(w_input "$(out_value check-result)")
check "6: same input twice" [ -n "$in1" -a "$in1" = "$in2" ]
fill "$W" pass
echo 'body: 재고가 0이면 예약 요청을 거절한다. 바뀜' > specs/p.yaml
tick; expect_err "6: input changed" E_NEEDS_RESULTS ait offer src/stock.c specs/p.yaml -m m -r "$W"
W2=$(out_value check-result)
check "6: a new W is written" [ -n "$W2" ] && [ "$(w_input "$W2")" != "$in1" ]

# 7
fresh
printf '#!/bin/sh\ntouch $T/ran\n' > tests/pass.sh
tick; with_r pass ait offer src/stock.c specs/pass.yaml -m m
check "7: ok" [ "$rc" = 0 ]
check "7: ait ran nothing" [ ! -e "$T/ran" ]

# 8
fresh
tick; expect_ok "8: future offer needs no results" ait future offer src/stock.c specs/pass.yaml -m m

# 9
fresh
tick; expect_ok "9: --force" ait offer src/stock.c specs/pass.yaml --force -m m
O=$(id_new) R=$(out_value request)
check "9: check O forced" out_has "check $O forced"
check "9: Ait-Check O forced pins(HEAD,O)" [ "$(checks_of HEAD)" = "$O forced $(pins HEAD $O)" ]
check "9: no results paragraph" [ "$(git log -1 --format=%B)" = "$(printf '[CONTRACT] m\n\nAit-Request: %s\nAit-Agent: agent-a\nAit-Session: %s\nAit-Check: %s forced %s' "$R" "$A" "$O" "$(pins HEAD $O)")" ]
run ait offer src/stock.c specs/pass.yaml -m m; W=$(out_value check-result); fill "$W" pass
expect_err "9: -r W --force" E_USAGE ait offer src/stock.c specs/pass.yaml -m m -r "$W" --force

# 10: mini: 'future drop' is not in ait-mini (O is current here)
desc="10: future drop K offer ..."
expect_unchanged expect_err "$desc" MINI ait future drop "$O" offer src/stock.c specs/p.yaml -m m

done_testing
