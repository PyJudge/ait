#!/bin/sh
# 3.2: new:PATH names a file that does not exist yet; '/' and 'DIR/' name no file, so new: neither takes nor suggests them
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent a)
prompt_defs
git add -A && tick && git commit -q --no-verify -m defs
not() { ! "$@"; }
bad() { d=$1; shift; tick; expect_unchanged expect_err "$d" E_SCOPE "$@"; }

for s in new:/ new:lib/ new:src/; do
	bad "consumer $s is a bad scope" ait future demand $s src/stock.c specs/p.yaml -m m
	check "no record is written for consumer $s" not err_has "bad format"
	bad "provider $s is a bad scope" ait future offer $s specs/p.yaml -m m
done
for s in / lib/ src/; do
	bad "consumer $s is a bad scope" ait future demand $s src/stock.c specs/p.yaml -m m
	check "consumer $s: no new: hint" not err_has "new:"
done
bad "a missing file as consumer" ait future demand lib/x.c src/stock.c specs/p.yaml -m m
check "a missing file still gets the new: hint" err_has "hint: if it does not exist yet, use 'new:lib/x.c'"
done_testing
