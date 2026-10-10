#!/bin/sh
# Regressions for the ninth review, grouped by cause; each case was reproduced first
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent a)
prompt_defs; exec_def pass
git add -A && tick && git commit -q --no-verify -m defs
H=$(git rev-parse HEAD)
bad() { kind=$1 d=$2; shift 2; tick; expect_unchanged expect_err "$d" "$kind" "$@"; }

# 7.3.1 and 5.13-1: "-" keeps a value only in amend; a new contract's clause with it is a grammar error
bad E_USAGE "an offer's provider '-'" env AIT_SESSION=- ait future offer - specs/p.yaml -m m
bad E_USAGE "an offer's files '-'" ait future offer src/stock.c - -m m
bad E_USAGE "a demand's consumer '-'" ait future demand - src/stock.c specs/p.yaml -m m

# 3.2a and 5.13-1: a scope given twice is the argument's shape, before the session
bad E_SCOPE "a scope twice in a helper" env AIT_SESSION=- ait future offer src/stock.c,src/stock.c specs/p.yaml -m m
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out"; O=$(new_id)
bad E_SCOPE "a scope twice in ID=SCOPES" env AIT_SESSION=- ait fulfill "$O=src/stock.c,src/stock.c" -m m
git reset -q --hard "$H"

# 5.8 E_FORMAT names a line: a raw NUL, a missing results
printf 'body: "x"\000\n' > specs/n.yaml
bad E_FORMAT "a definition with a NUL" ait future offer src/stock.c specs/n.yaml -m m
check "names its line" err_has "not a text file in line 1"
rm -f specs/n.yaml
tick; run ait offer src/stock.c specs/pass.yaml -m o
printf 'input: %s\n' "$(sed -n 's/^input: //p' "$(out_value check-result)" | tr -d '"')" > "$T/r.yaml"
bad E_FORMAT "a result file without results" ait offer src/stock.c specs/pass.yaml -m o -r "$T/r.yaml"
check "names the line of its map" err_has "0 of 1 tasks in line 1"
done_testing
