#!/bin/sh
# Regressions for the request.c review (each was reproduced before the fix)
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent agent-a)
prompt_defs; exec_def pass
tick; ait future demand src/order.c src/stock.c specs/p.yaml as d -m d >"$T/out"; D=$(new_id)
tick; ait future offer src/stock.c,src/order.c specs/pass.yaml as o undertake $D o -m o >"$T/out"; O=$(sed -n 's/^new \([^ ]*\) o$/\1/p' "$T/out")
echo x >> src/stock.c; git add src/stock.c

# 7.7.1-7.7.2: a demand the target offer undertakes is never a target itself, whatever the order
run ait fulfill $O $D=src/stock.c -m m
check "O D=…: only O is selected" sh -c "! grep -q '^id $D' '$T/out'"
run ait fulfill $D=src/stock.c $O -m m
check "D=… O: only O is selected" sh -c "! grep -q '^id $D' '$T/out'"

# 7.7.6: one provider line per scope
run ait fulfill $O -m m
check "a provider line per scope" sh -c "grep -qx 'provider $O src/order.c' '$T/out' && grep -qx 'provider $O src/stock.c' '$T/out'"

# 3.2a: the same scope twice in ID=SCOPES
tick; ait future offer src/ specs/pass.yaml as a -m a >"$T/out"; AO=$(new_id)
expect_err "ID=SCOPES with a scope twice" E_SCOPE ait fulfill $AO=src/stock.c,src/stock.c --force -m x

# 2.5: an input that mentions 'body:' is not read as a definition
mkdir -p tests
printf '#!/bin/sh\n# checks the body: of the order\nexit 0\n' > tests/x.sh; chmod 755 tests/x.sh
printf 'body: x\nengine: exec\ncommand: [./tests/x.sh]\ninputs: [tests/x.sh]\n' > specs/x.yaml
tick; expect_ok "an input mentioning body:" ait future offer src/stock.c specs/x.yaml -m x

# 5.3 and 5.13: the -r/--force conflict has one message; a message may be '--force'
echo 'input: x' > "$T/res.yaml"
expect_err "-r then --force" E_USAGE ait offer src/stock.c specs/pass.yaml -r "$T/res.yaml" --force -m y
check "-r then --force: one message" err_has "options '-r' and '--force' cannot be used together"
expect_err "--force then -r" E_USAGE ait offer src/stock.c specs/pass.yaml --force -r "$T/res.yaml" -m y
check "--force then -r: one message" err_has "options '-r' and '--force' cannot be used together"
echo z > README && git add README
tick; expect_ok "a message '--force'" ait commit -m --force
git reset -q --soft HEAD~1 && git reset -q

# 5.13: a usage error comes before the state of the repository
git checkout -q --detach
expect_err "no message on a detached HEAD" E_USAGE ait fulfill $O
git checkout -q master

# 7.7a.1: a commit with nothing to check does not validate; 7.7.6: one 'invalid' line, after 'mixed'
echo "body: changed" > specs/p.yaml; git add specs/p.yaml; tick; git commit -q --no-verify -m raw
echo z > README; git add README
tick; expect_ok "a commit with nothing to check" ait commit -m x
echo y >> src/stock.c; echo '# y' >> tests/pass.sh; git add src/stock.c tests/pass.sh
run ait fulfill $O -m x
check "one invalid line" [ "$(grep -c '^invalid ' "$T/out")" = 1 ]
check "mixed before invalid" sh -c "grep -nE '^(mixed|invalid) ' '$T/out' | head -1 | grep -q mixed"
done_testing
