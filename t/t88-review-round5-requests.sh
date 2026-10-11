#!/bin/sh
# Regressions for the fifth review of requests and hints; each case was reproduced, then traced to its cause
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent a)
prompt_defs; exec_def pass
git add -A && tick && git commit -q --no-verify -m defs
H=$(git rev-parse HEAD)

# 5.9a transition hint 10: the undertake clause becomes a future offer in the same request, under a free alias
tick; ait future offer src/order.c specs/w.yaml -m o >"$T/out"; C=$(new_id)
tick; ait future cancel $C -m c >/dev/null
tick; run ait future demand src/order.c src/stock.c specs/p.yaml as d undertake d $C -m m
check "undertake hint in the same request" err_has "'ait future demand src/order.c src/stock.c specs/p.yaml as d future offer src/stock.c specs/p.yaml as o undertake d o -m <why>'"
tick; expect_ok "and runs" ait future demand src/order.c src/stock.c specs/p.yaml as d future offer src/stock.c specs/p.yaml as o undertake d o -m m
git reset -q --hard "$H"

# 5.9 d in the gate: a new current offer is made a promise with the rest of the request kept
tick; run ait future demand src/order.c src/stock.c specs/p.yaml as d offer src/stock.c specs/pass.yaml as o undertake d o -m m
check "the new offer becomes future" err_has "make it future: 'ait future demand src/order.c src/stock.c specs/p.yaml as d future offer src/stock.c specs/pass.yaml as o undertake d o -m <why>'"
git reset -q --hard "$H"

# 5.9a: a rerun leaves out the ID=SCOPES of every contract the reopen takes out of the check set
tick; ait demand src/order.c src/stock.c specs/p.yaml as d offer src/stock.c specs/pass.yaml as o undertake d o --force -m m >"$T/out"
D=$(sed -n 's/^new \(D[^ ]*\) d$/\1/p' "$T/out"); O=$(sed -n 's/^new \(O[^ ]*\) o$/\1/p' "$T/out")
echo x >> src/stock.c && git add src/stock.c
tick; run ait commit $D=src/stock.c $O=src/stock.c -m m
check "the rerun drops both" err_has "reopen it: 'ait future amend $D future amend $O -m <why>', then rerun 'ait commit -m <why>'"
git reset -q --hard "$H"

# 7.7.1 and 5.13-4: ID=SCOPES on what is not checked is a bad scope, whatever its file did
tick; ait future offer src/order.c specs/p.yaml -m o >"$T/out"; G=$(new_id)
echo x >> src/order.c && git add src/order.c
tick; expect_err "scopes for a future contract" E_SCOPE ait commit $G=src/order.c -m m
git reset -q --hard "$H"

# 5.13: the files given as arguments are read after the grammar and the shapes of paths
tick; expect_err "missing FILES before -r's file" E_USAGE ait offer src/stock.c -r /nonexist -m m
tick; expect_err "no message before -r's file" E_USAGE ait fulfill O000000000000 -r /nonexist
tick; expect_err "a bad path before -r's file" E_SCOPE ait offer bad//path specs/p.yaml -r /nonexist -m m

# 5.9a: a commit names its session in hints given before it checks the session
tick; ait offer src/order.c specs/p.yaml --force -m o >"$T/out"; K=$(new_id)
B=$(ait session open --agent b 2>/dev/null)
git rm -q --cached src/order.c
tick; run env AIT_SESSION=$B ait commit -m m
check "the code-required hint carries AIT_SESSION" err_has "'AIT_SESSION=$B ait future amend $K -m <why>'"
ait session close "$B"
git reset -q --hard "$H"

# 7.3.2: future cancel takes a future contract; its condition is checked at its clause
tick; ait offer src/order.c specs/p.yaml --force -m o >"$T/out"; K=$(new_id)
tick; expect_err "cancel a current contract" E_STATE ait future cancel $K future offer src/order.c specs/missing.yaml -m m
git reset -q --hard "$H"

# 8.2: the gate's check request names the normalized candidate T, the new record in it
W=$(git rev-parse --git-path ait/check-result.yaml); Q=$(git rev-parse --git-path ait/check-request.yaml)
tick; run ait offer src/stock.c specs/pass.yaml -m o
check "the check request's tree holds the new record" sh -c "git ls-tree -r \$(sed -n 's/^tree: \"\\(.*\\)\"/\\1/p' '$Q') | grep -q '\.ait/O'"
fill "$W" pass
tick; ait offer src/stock.c specs/pass.yaml -m o -r "$W" >"$T/out"; K=$(new_id)

# 5.8 E_CHECK_FAILED: fulfill and commit may reopen a current contract with a new condition
echo x >> src/stock.c && git add src/stock.c
tick; run ait commit -m x
fill "$W" fail
tick; run ait commit -m x -r "$W"
check "the condition hint" err_has "to change its condition, reopen it with the new one: 'ait future amend $K - <new defs> -m <why>', then rerun this command"
git reset -q --hard "$H"

# 7.3.1: a lone "future" is a grammar error
tick; expect_err "future without a clause" E_USAGE ait future -m m
done_testing
