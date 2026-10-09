#!/bin/sh
# T07 묶음·alias·원자성 (7.3)
. "$(dirname "$0")/lib.sh"
setup

A=$(ait session open --agent a)
prompt_defs
exec_def guard
echo r > "$T/f"

# 1. tests/guard.sh in the FILES position means specs/guard.yaml (0.2)
tick
expect_ok "future demand + future offer + undertake in one request" \
	ait future demand src/order.c src/stock.c specs/p.yaml as d future offer src/stock.c specs/guard.yaml as o undertake d o -m m
D=$(sed -n 's/^new \(D[0-9a-f]*\) d$/\1/p' "$T/out")
O=$(sed -n 's/^new \(O[0-9a-f]*\) o$/\1/p' "$T/out")
check "new D d, new O o, undertaken D O in this order" \
	[ "$(grep -E '^(new|undertaken) ' "$T/out")" = "$(printf 'new %s d\nnew %s o\nundertaken %s %s' "$D" "$O" "$D" "$O")" ]
ns=$(git diff-tree --no-commit-id -r --name-status HEAD)
check "name-status" [ "$ns" = "$(printf 'A\t.ait/%s\nA\t.ait/%s\nA\t.ait/undertake/%s-%s\nA\tspecs/guard.yaml\nA\tspecs/p.yaml\nA\ttests/guard.sh' "$D" "$O" "$D" "$O")" ]
check "undertake file content" [ "$(git show "HEAD:.ait/undertake/$D-$O")" = "$(printf 'state undertaken\nprev -')" ]
check "undertake file ends with LF" [ "$(git cat-file -s "HEAD:.ait/undertake/$D-$O")" = 24 ]

# 2. contract / ct prefix
tick; expect_ok "ait contract future offer" ait contract future offer src/order.c specs/p.yaml -m m
tick; expect_ok "ait ct future offer" ait ct future offer src/order.c specs/p.yaml -m m

# 3. E_ID, unchanged; a failing later clause records nothing
tick
expect_unchanged expect_err "undertake x O" E_ID ait undertake x "$O" -m m
expect_unchanged expect_err "alias used before its clause" E_ID \
	ait undertake d "$O" future demand src/order.c src/stock.c specs/p.yaml as d -m m
expect_unchanged expect_err "last clause fails, first not recorded" E_ID \
	ait future offer src/stock.c specs/p.yaml as o2 undertake "$D" Oaaaaaaaaaaaa -m m

# 4. E_USAGE
FO="future offer src/stock.c specs/p.yaml"
expect_unchanged expect_err "duplicate alias" E_USAGE ait $FO as a future offer src/order.c specs/p.yaml as a -m m
for al in undertake D1 1a; do
	expect_unchanged expect_err "bad alias '$al'" E_USAGE ait $FO as $al -m m
done
expect_unchanged expect_err "fulfill as a clause" E_USAGE ait $FO fulfill "$O" -m m
expect_unchanged expect_err "unknown token" E_USAGE ait $FO bogus -m m
expect_unchanged expect_err "too few arguments" E_USAGE ait future offer src/stock.c -m m
expect_unchanged expect_err "-r twice" E_USAGE ait offer src/stock.c specs/p.yaml -r "$T/f" -r "$T/f" -m m
expect_unchanged expect_err "-r without a file" E_USAGE ait offer src/stock.c specs/p.yaml -m m -r
expect_unchanged expect_err "-r with --force" E_USAGE ait offer src/stock.c specs/p.yaml -r "$T/f" --force -m m
expect_unchanged expect_err "--attest" E_USAGE ait $FO --attest -m m
expect_unchanged expect_err "--check" E_USAGE ait $FO --check -m m

done_testing
