#!/bin/sh
# Regressions for the conformance review (each was reproduced before the fix)
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent a)
prompt_defs; exec_def pass
tick; ait future offer src/stock.c specs/pass.yaml as o -m o >"$T/out"; O=$(new_id)
H=$(git rev-parse HEAD)

# 2.2.1: a record is its bytes; a NUL and anything after it is a bad format
printf '\000junk' >> .ait/$O && git add .ait/$O && tick && git commit -q --no-verify -m nul
expect_err "a record with a NUL" E_FORMAT ait future cancel $O -m x
git reset -q --hard "$H"

# 8.4: the template is kept when its input is the same value, however it is written
echo y >> src/stock.c && git add src/stock.c
run ait fulfill $O -m x; W=$(out_value check-result); IN=$(sed -n 's/^input: //p' "$W")
printf '# mine\ninput: "%s"\nresults:\n  - task: %s:specs/pass.yaml\n    outcome: pass\n    basis: filled\n' "$IN" "$O" > "$W"
run ait fulfill $O -m x
check "a filled result with a quoted input is kept" grep -q "basis: filled" "$W"
git reset -q --hard "$H"

# 8.4: only the task values change from +n to the ID
tick; run ait offer src/order.c specs/pass.yaml -m o; W=$(out_value check-result); IN=$(sed -n 's/^input: //p' "$W")
printf 'input: %s\nresults:\n  - task: "+1:specs/pass.yaml"\n    outcome: pass\n    basis: |\n      see task: +1:specs/pass.yaml in the log\n' "$IN" > "$W"
tick; expect_ok "a quoted +n task" ait offer src/order.c specs/pass.yaml -m o -r "$W"
check "the task value takes the ID" sh -c "git log -1 --format=%B | grep -q 'task: \"O[0-9a-f]*:specs/pass.yaml\"'"
check "the basis keeps +1" sh -c "git log -1 --format=%B | grep -q 'see task: +1:specs/pass.yaml'"
git reset -q --hard "$H"

# 2.5.1: flow lists keep quoted commas; flow maps nest in flow lists
printf 'body: x\nengine: exec\ncommand: [sh, -c, "printf a,b"]\n' > specs/c.yaml
tick; expect_ok "a quoted comma in a flow list" ait future offer src/stock.c specs/c.yaml as c -m m
run ait todo "$(sed -n 's/^new \([^ ]*\) c$/\1/p' "$T/out" 2>/dev/null)"
git reset -q --hard "$H"
tick; run ait offer src/order.c specs/pass.yaml -m o; W=$(out_value check-result); IN=$(sed -n 's/^input: //p' "$W")
printf 'input: %s\nresults: [{task: "+1:specs/pass.yaml", outcome: pass, basis: checked}]\n' "$IN" > "$W"
tick; expect_ok "results as a flow list of flow maps" ait offer src/order.c specs/pass.yaml -m o -r "$W"
git reset -q --hard "$H"

# 2.5.1: a key twice is a bad format, also when its value is an empty list
printf 'body: x\ninputs: []\ninputs: []\n' > specs/d.yaml
tick; expect_err "inputs twice" E_FORMAT ait future offer src/stock.c specs/d.yaml -m m

# 2.5.2: success lists exit codes; inputs, command and success are lists
printf 'body: x\nengine: exec\ncommand: [./tests/pass.sh]\nsuccess: [banana]\ninputs: [tests/pass.sh]\n' > specs/e.yaml
tick; expect_err "success: [banana]" E_FORMAT ait future offer src/stock.c specs/e.yaml -m m
printf 'body: x\ninputs: tests/pass.sh\n' > specs/f.yaml
tick; expect_err "inputs as a scalar" E_FORMAT ait future offer src/stock.c specs/f.yaml -m m

# 5.9a: a value the shell reads is quoted in a hint
echo s > 'src/a$b.c' && git add 'src/a$b.c' && tick && git commit -qm dollar
tick; run ait offer 'src/a$b.c' specs/pass.yaml -m o
check "the promise hint quotes the path" err_has "'ait future offer 'src/a\$b.c' specs/pass.yaml -m <why>'" 
# the same causes, elsewhere: every file ait reads as text, task values in any YAML form, links read once
tick; run ait offer src/order.c specs/pass.yaml -m o; W=$(out_value check-result); IN=$(sed -n 's/^input: //p' "$W")
printf 'input: %s\nresults:\n  - task: +1:specs/pass.yaml\n    outcome: pass\n    basis: ok\n\000outcome: fail\n' "$IN" > "$W"
tick; expect_err "a result file with a NUL" E_FORMAT ait offer src/order.c specs/pass.yaml -m o -r "$W"
printf 'msg\000hidden\n' > "$T/m"
tick; expect_err "a message file with a NUL" E_FORMAT ait future offer src/order.c specs/pass.yaml -F "$T/m"
printf 'input: %s\nresults: [{task: "+1:specs/pass.yaml", outcome: pass, basis: checked}]\n' "$IN" > "$W"
tick; expect_ok "a flow-form result" ait offer src/order.c specs/pass.yaml -m o -r "$W"
check "its +1 takes the ID too" sh -c "git log -1 --format=%B | grep -q 'task: \"O[0-9a-f]*:specs/pass.yaml\"'"
git reset -q --hard "$H"
mkdir -p .ait/undertake && printf 'state undertaken\nprev -\n' > .ait/undertake/D000000000000-$O && git add .ait && tick && git commit -q --no-verify -m dangling
echo y >> src/stock.c && git add src/stock.c
expect_err "fulfill with a link whose demand is missing" E_FORMAT ait fulfill $O -m x --force
git reset -q --hard "$H"
done_testing
