#!/bin/sh
# Regressions for the fourth review; each case was reproduced, then traced to its cause
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent a)
prompt_defs; exec_def pass
git add -A && tick && git commit -q --no-verify -m defs
H=$(git rev-parse HEAD)

# 2.5.1: a value keeps its kind; a map is not a list, and anchors, aliases and tags are refused wherever they stand
printf 'body: x\nengine: exec\ncommand: {0: ./tests/pass.sh}\n' > specs/m.yaml
tick; expect_err "a map given as a list" E_FORMAT ait future offer src/stock.c specs/m.yaml -m m
printf 'body: x\nengine: exec\ncommand: [*x]\n' > specs/m.yaml
tick; expect_err "an alias in a flow list" E_FORMAT ait future offer src/stock.c specs/m.yaml -m m
printf 'body: x\nengine: exec\ncommand:\n  - !tag sh\n' > specs/m.yaml
tick; expect_err "a tag in a block list" E_FORMAT ait future offer src/stock.c specs/m.yaml -m m
rm specs/m.yaml

# 5.4: only "ID reported|forced PINS" is a check
tick; ait offer src/stock.c specs/p.yaml --force -m o >"$T/out"; O=$(new_id)
tick; git commit -q --amend --no-verify -m "$(git log -1 --format=%B | sed 's/ forced / bogus /')"
run ait todo $O
check "a check with another word does not count" out_has "unverified yes"
git reset -q --hard "$H"

# 7.8.1: todo reads the contracts and their definitions in the index T0
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out"; O=$(new_id)
echo 'body: STAGED' > specs/p.yaml && git add specs/p.yaml
run ait todo $O
check "the staged definition is shown" out_has "    STAGED"
git reset -q --hard "$H"

# 5.9a: in a session that is not the worktree's default, every hint names it
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out"; O=$(new_id)
B=$(ait session open --agent a 2>/dev/null)
run env AIT_SESSION=$B ait todo $O
check "the todo hint carries AIT_SESSION" err_has "'AIT_SESSION=$B ait fulfill $O -m <why>'"
ait session close "$B"
git reset -q --hard "$H"

# 7.8.2: an offer shows the spec files of every demand it carries, and the provider of the future ones
tick; ait demand src/order.c src/stock.c specs/p.yaml --force -m d >"$T/out"; D=$(new_id)
tick; ait future offer src/stock.c specs/pass.yaml as o undertake $D o -m o >"$T/out"; O=$(new_id)
run ait todo $O
check "a current demand's spec file" out_has "undertakes-file $D specs/p.yaml prompt"
check "but not its provider" sh -c "! grep -q '^undertakes-provider $D' '$T/out'"
git reset -q --hard "$H"

# H10: each abstract future demand the promise undertakes gets a scope too
tick; ait future demand src/order.c src/ specs/p.yaml as d -m d >"$T/out"; D=$(new_id)
tick; ait future offer src/stock.c specs/pass.yaml as o undertake $D o -m o >"$T/out"; O=$(new_id)
run ait todo $O
check "D=<scope> for the abstract demand" err_has "'ait fulfill $O $D=<scope> -m <why>'"
git reset -q --hard "$H"

# H8: KEPT,<new scope> and the files '-', and the command runs as given
tick; ait offer src/stock.c,src/order.c specs/p.yaml --force -m o >"$T/out"; O=$(new_id)
git rm -q src/order.c && tick && git commit -q -m gone 2>/dev/null
run ait todo $O
check "the hint keeps what is left" err_has "run 'ait amend $O src/stock.c,<new scope> - -m <why>'"
echo n > src/new.c && git add src/new.c && tick && git commit -q -m new 2>/dev/null
tick; expect_ok "and runs with the new scope" ait amend $O src/stock.c,src/new.c - --force -m moved
done_testing
