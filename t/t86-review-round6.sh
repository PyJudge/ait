#!/bin/sh
# Regressions for the sixth review, grouped by cause
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent a)
prompt_defs; exec_def pass
git add -A && tick && git commit -q --no-verify -m defs
H=$(git rev-parse HEAD)

# as git's main: closed standard descriptors are opened on /dev/null, and a failed write to stdout is E_IO (5.7)
run sh -c 'ait todo <&-'
check "stdin closed" [ "$rc" = 0 ]
run sh -c 'ait session list 1</dev/null'
check "stdout not writable is E_IO" sh -c "[ $rc = 128 ] && grep -q '^fatal: write error: standard output' '$T/err'"

# githooks(5): the pre-push fields are read from the right, so a local name with spaces is validated too
mkdir -p .ait && echo garbage > .ait/Daaaaaaaaaaaa && git add .ait && tick && git commit -q --no-verify -m "bad commit"
run git push origin "HEAD^{/bad commit}:refs/heads/spaced"
check "a local name with spaces does not get past pre-push" sh -c "[ $rc != 0 ] && ! git ls-remote --exit-code origin refs/heads/spaced >/dev/null"
git reset -q --hard "$H"

# 5.4: trailers as the parser of 5.4 reads them; a folded value is kept as written and is no check
tick; ait offer src/stock.c specs/p.yaml --force -m o >"$T/out"; O=$(new_id)
git rm -q src/stock.c && tick && git commit -q -m gone 2>/dev/null
tick; git commit -q --allow-empty -m "$(printf 'note\n\nAit-Check: see\n  the other branch')"
expect_ok "todo reads past a folded trailer" ait todo $O
git reset -q --hard "$H"

# 5.10: the mend fits what is wrong: a path that is no record is removed
echo junk > .ait/foo && git add .ait/foo && tick && git commit -q --no-verify -m junk
tick; run ait future offer src/order.c specs/p.yaml -m m
check "a path that is no record is to be removed" err_has "remove '.ait/foo'"
git reset -q --hard "$H"

# 5.9a transition hint 1: an error HEAD already had is mended as 5.10 says, not with a hint for this request;
# D and O are as in GOOD (the commit before), so only the link GOOD lacks goes
tick; ait future demand src/order.c src/stock.c specs/p.yaml -m d >"$T/out"; D=$(new_id)
tick; ait offer src/stock.c specs/pass.yaml --force -m o >"$T/out"; O=$(new_id)
mkdir -p .ait/undertake && printf 'state undertaken\nprev -\n' > .ait/undertake/$D-$O && git add .ait && tick && git commit -q --no-verify -m bad
tick; run ait future offer src/order.c specs/w.yaml -m m
check "HEAD's error gets the restore" err_has "remove '.ait/undertake/$D-$O', then 'ait contract commit -m <why>'"
git reset -q --hard "$H"

# 5.8 E_MIXED and E_CODE_REQUIRED: one hint names every path
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out"; P=$(new_id)
tick; ait future offer src/order.c specs/w.yaml -m o >"$T/out"
echo 'body: a' > specs/p.yaml && echo 'body: b' > specs/w.yaml && git add specs
tick; run ait commit -m m
check "every mixed path in one hint" err_has "unstage the spec files ('specs/p.yaml' 'specs/w.yaml'), then rerun this command"
git reset -q --hard "$H"
tick; ait future offer new:src/a.c,new:src/b.c specs/p.yaml -m o >"$T/out"; P=$(new_id)
echo a > src/a.c && echo b > src/b.c
tick; run ait fulfill $P -m m
check "every path to stage in one hint" err_has "stage 'src/a.c' 'src/b.c', then rerun this command"
git reset -q --hard "$H"; rm -f src/a.c src/b.c

# 5.9a H7: todo asks to check again only what HEAD has unverified; staged code is commit's
tick; ait offer src/stock.c specs/pass.yaml --force -m o >"$T/out"; O=$(new_id)
echo x >> src/stock.c && git add src/stock.c
run ait todo $O
check "no H7 for staged code" sh -c "! grep -q 'check .$O. again' '$T/err'"
check "the index block names it" out_has "touch $O"
done_testing
