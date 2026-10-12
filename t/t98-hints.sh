#!/bin/sh
# 5.9a: a hint is a command that runs as given; for a helper it is the same request with one clause changed or added
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent a)
prompt_defs; exec_def pass
printf '#!/bin/sh\nexit 1\n' > tests/fail.sh && chmod 755 tests/fail.sh
printf 'body: fail\nengine: exec\ncommand: [./tests/fail.sh]\ninputs: [tests/fail.sh]\n' > specs/fail.yaml
git add -A && tick && git commit -q --no-verify -m defs
H=$(git rev-parse HEAD)
W=$(git rev-parse --git-path ait/check-result.yaml)

# promise: the clause of the new contract becomes a future one, the other clauses stay
tick; run ait offer src/stock.c specs/pass.yaml as n future offer src/order.c specs/p.yaml -m m
fill "$W" fail
tick; run ait offer src/stock.c specs/pass.yaml as n future offer src/order.c specs/p.yaml -m m -r "$W"
check "the promise keeps the other clause" err_has "make it future: 'ait future offer src/stock.c specs/pass.yaml as n future offer src/order.c specs/p.yaml -m <why>'"
tick; expect_ok "and runs" ait future offer src/stock.c specs/pass.yaml as n future offer src/order.c specs/p.yaml -m m
git reset -q --hard "$H"

# reopen in the gate: the amend clause becomes "future amend" with its scopes kept
tick; ait offer src/stock.c specs/fail.yaml --force -m o >"$T/out"; O=$(new_id)
echo n > src/new.c && git add src/new.c && tick && git commit -q -m new 2>/dev/null
tick; run ait amend $O src/new.c - -m m
fill "$W" fail
tick; run ait amend $O src/new.c - -m m -r "$W"
check "the reopen keeps the scope change" err_has "reopen it: 'ait future amend $O src/new.c - -m <why>'"
tick; expect_ok "and runs" ait future amend $O src/new.c - -m m
git reset -q --hard "$H"

# reopen a current demand with the current offer that undertakes it (5.9 d)
tick; ait demand src/order.c src/stock.c specs/p.yaml as d offer src/stock.c specs/pass.yaml as o undertake d o --force -m m >"$T/out"
D=$(sed -n 's/^new \(D[^ ]*\) d$/\1/p' "$T/out"); O=$(sed -n 's/^new \(O[^ ]*\) o$/\1/p' "$T/out")
echo x >> src/stock.c && git add src/stock.c
tick; run ait commit -m x
check "fulfill and commit reopen the offer with the demand" err_has "reopen it: 'ait future amend $D future amend $O -m <why>', then rerun this command"
git checkout -q src/stock.c && git reset -q
tick; expect_ok "and that reopen runs" ait future amend $D future amend $O -m m
git reset -q --hard "$H"

# the scope move of the gate: an empty amend gets KEPT,<new scope>; fulfill keeps KEPT in K=
tick; ait offer src/stock.c,src/order.c specs/p.yaml --force -m o >"$T/out"; O=$(new_id)
git rm -q src/order.c && tick && git commit -q -m gone 2>/dev/null
tick; run ait amend $O -m m
check "the gate moves the scope in the same command" err_has "if it was moved, run 'ait amend $O src/stock.c,<new scope> - -m <why>' to get the tasks"
git reset -q --hard "$H"
tick; ait future offer src/stock.c,src/order.c specs/p.yaml -m o >"$T/out"; O=$(new_id)
git rm -q src/order.c
tick; run ait fulfill $O -m m
check "fulfill keeps what is left" err_has "if it was moved, rerun with '$O=src/stock.c,<new scope>'"
git reset -q --hard "$H"

# an abstract demand carried by the offer: the same fulfill with D=<scope> added
tick; ait future demand src/order.c src/ specs/p.yaml as d future offer src/stock.c specs/pass.yaml as o undertake d o -m m >"$T/out"
D=$(sed -n 's/^new \(D[^ ]*\) d$/\1/p' "$T/out"); O=$(sed -n 's/^new \(O[^ ]*\) o$/\1/p' "$T/out")
echo x >> src/stock.c && git add src/stock.c
tick; run ait fulfill $O -m m
check "the scope is added to the same fulfill" err_has "give it the scope you implemented: 'ait fulfill $O $D=<scope> -m <why>'"
git reset -q --hard "$H"

# H4 is for my own future offer only (rule 7)
tick; ait future demand src/order.c src/stock.c specs/p.yaml as d future offer src/stock.c specs/pass.yaml as o undertake d o -m m >"$T/out"
D=$(sed -n 's/^new \(D[^ ]*\) d$/\1/p' "$T/out"); O=$(sed -n 's/^new \(O[^ ]*\) o$/\1/p' "$T/out")
B=$(ait session open --agent b 2>/dev/null)
tick; run env AIT_SESSION=$B ait future offer src/stock.c specs/pass.yaml as p undertake $D p -m m
check "b's offer is warned about a's" err_has "is also undertaken by '$O'"
check "but never told to cancel a's" sh -c "! grep -q \"cancel $O\" '$T/err'"
P=$(sed -n 's/^new \(O[^ ]*\) p$/\1/p' "$T/out")
tick; ait undertake $D $O release $D $O -m m >/dev/null 2>&1
tick; run env AIT_SESSION=$B ait undertake $D $O -m m
check "b linking a's offer is no H4 for b" sh -c "! grep -q 'also undertaken' '$T/err'"
done_testing
