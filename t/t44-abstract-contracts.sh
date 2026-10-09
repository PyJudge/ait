#!/bin/sh
# T44 추상 계약과 범위 목록 (1.5c, 3.2a, 3.4, 7.7.1)
#
# mini: no line ranges and no peek. `src/cli.c:1-10` becomes `src/cli.c`; `ait peek` is
# replaced by `ait todo ID` and rec(ID). Steps whose point is a line range are left out.
. "$(dirname "$0")/lib.sh"

id_of() { sed -n "s/^new \([^ ]*\) $1\$/\1/p" "$T/out"; }
setl() { f=$1 l=$2; shift 2; awk -v n="$l" -v t="$*" 'NR==n{print t; next}1' "$f" >"$f.tmp" && cat "$f.tmp" >"$f" && rm "$f.tmp"; }
not() { ! "$@"; }

setup
A=$(ait session open --agent agent-a)
for i in $(seq 1 20); do echo "r$i"; done >src/runner.c
for i in $(seq 1 20); do echo "c$i"; done >src/cli.c
echo readme >README.md
git add -A; tick; git commit -qm files
exec_def pass; prompt_defs
printf 'body: test-a\nengine: exec\ncommand: [./tests/pass.sh]\ninputs: [tests/pass.sh]\n' >specs/test-a.yaml

# 1. an abstract offer
tick; run ait future offer / specs/test-a.yaml as o -m "코드베이스가 테스트 A를 통과"
check "1: ok" test $rc = 0
O=$(id_of o)
check "1: rec(O) provider is / with HEAD^'s root tree" test "$(rec $O | grep '^provider ')" = "provider / $(git rev-parse HEAD^^{tree})"
# mini: `ait peek O` -> `ait todo O`
expect_ok "1: todo O" ait todo $O
check "1: provider /" out_has "provider /"

# 2. abstract only for a future provider
expect_err "2: current offer of /" E_SCOPE ait offer / specs/test-a.yaml -m m
tick; run ait future offer src/ specs/p.yaml as s -m m
check "2: future offer src/ ok" test $rc = 0
S=$(id_of s)
check "2: provider src/ <tree>" test "$(rec $S | grep '^provider ')" = "provider src/ $(git rev-parse HEAD^:src)"
tick; expect_ok "2: future demand with an abstract provider" ait future demand src/order.c / specs/p.yaml -m m
expect_err "2: an abstract consumer" E_SCOPE ait future demand / src/stock.c specs/p.yaml -m m

# 3. an abstract contract must be narrowed to become current
setl src/runner.c 5 r5x; setl src/cli.c 3 c3x; git add -A; tick; git commit -qm impl
before=$(snapshot)
expect_err "3: fulfill O without a scope" E_SCOPE ait fulfill $O -m m
check "3: unchanged" test "$(snapshot)" = "$before"
check "3: hint: ait fulfill <O>=<scope> -m <why>" err_has "ait fulfill $O=<scope> -m <why>"

# 4. narrowed fulfill (mini: src/cli.c:1-10 -> src/cli.c)
tick; with_r pass ait fulfill $O=src/runner.c,src/cli.c -m m
check "4: ok" test $rc = 0
check "4: fulfilled O" out_has "fulfilled $O"
check "4: two provider rows in PATH order" test "$(rec $O | grep '^provider ')" = "$(printf 'provider src/cli.c %s\nprovider src/runner.c %s' "$(blob HEAD:src/cli.c)" "$(blob HEAD:src/runner.c)")"
# mini: the `peek now src/cli.c:5-5` / `src/cli.c:15-15` checks need line ranges and peek

# 6. the decision on a scope list (mini: cli.c line 15 is in the whole-file scope, so that part is left out)
setl src/runner.c 6 r6x; git add src/runner.c
tick; run git commit -qm runner
check "6: a runner.c commit touches O (warning)" err_has "warning: this commit leaves 1 current contract(s) unverified"
expect_ok "6: todo O" ait todo $O
check "6: only the runner.c row is ?changed" test "$(grep '^provider ' "$T/out")" = "$(printf 'provider src/cli.c\nprovider src/runner.c ?changed')"

# 5. narrowing out of the abstract scope is refused
tick; run ait future offer src/ specs/p.yaml as o2 -m m
O2=$(id_of o2)
setl src/runner.c 7 r7x; git add src/runner.c
before=$(snapshot)
expect_err "5: O2=README.md is outside src/" E_SCOPE ait fulfill $O2=README.md -m m
check "5: unchanged" test "$(snapshot)" = "$before"
git reset -q --hard
setl src/stock.c 1 s1x; git add src/stock.c
expect_err "5: = on a contract not in the check set" E_SCOPE ait fulfill $O2=src/stock.c $O=src/cli.c -m m
git reset -q --hard

# 7. scope list syntax
expect_err "7: the same scope twice" E_SCOPE ait future offer src/cli.c,src/cli.c specs/p.yaml -m m
tick; run ait future demand src/order.c,src/cli.c src/stock.c specs/p.yaml as d -m m
check "7: ok" test $rc = 0
D=$(id_of d)
check "7: consumer rows in PATH order" test "$(rec $D | grep '^consumer ')" = "$(printf 'consumer src/cli.c %s\nconsumer src/order.c %s' "$(blob HEAD:src/cli.c)" "$(blob HEAD:src/order.c)")"

# 8. mini: notifications (sub, listen)

done_testing
