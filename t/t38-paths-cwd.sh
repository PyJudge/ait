#!/bin/sh
# T38 경로와 cwd (5.12)
. "$(dirname "$0")/lib.sh"

setup
S=$(ait session open --agent agent-a)
prompt_defs

# 1: from src/ the same as from the root
cd src
tick; expect_ok "1: offer from src/" ait future offer src/stock.c specs/p.yaml -m m
ID1=$(new_id)
cd ..
tick; expect_ok "1: offer from the root" ait future offer src/stock.c specs/p.yaml -m m
ID2=$(new_id)
check "1: the same record" test "$(git show "HEAD:.ait/$ID1")" = "$(git show "HEAD:.ait/$ID2")"

# 2: .ait dirty from a subdirectory
echo x >> ".ait/$ID1"
cd src
desc="2: dirty .ait from src/"
tick; expect_unchanged expect_err "$desc" E_DIRTY ait future cancel "$ID1" -m m
cd ..
git checkout -q HEAD -- .ait

# 3: a non-ASCII path
for i in $(seq 1 10); do echo "j$i"; done > src/재고.c
git add src/재고.c; tick; git commit -qm jaego
tick; with_r pass ait offer src/재고.c specs/p.yaml -m m
check "3: current offer K on src/재고.c" test "$rc" = 0
K=$(new_id)
check "3: K's provider is the literal path" sh -c "git show HEAD:.ait/$K | grep -q '^provider src/재고.c [0-9a-f]\{40\}\$'"
tick; expect_ok "3: future offer O on src/재고.c" ait future offer src/재고.c specs/p.yaml -m m
O=$(new_id)
echo j11 >> src/재고.c; git add src/재고.c
expect_err "3: fulfill O asks for results" E_NEEDS_RESULTS ait fulfill "$O" -m m
check "3: report has touch <K>" out_has "touch $K"
tick; git commit -qm change 2>/dev/null
expect_err "3: peek now (mini: unsupported)" MINI ait peek now "$K"
# mini: `ait todo K` takes the place of `ait peek now K`
expect_ok "3: todo K" ait todo "$K"
check "3: provider src/재고.c ?changed" out_has "provider src/재고.c ?changed"

# 4: literal paths
# mini: no contract add; the scenario's `ait contract add 'specs/a*b.yaml'` is unsupported
expect_err "4: contract add (mini: unsupported)" MINI ait contract add 'specs/a*b.yaml'
# the same literal-path rule through a helper's FILES (5.12 GIT_LITERAL_PATHSPECS)
echo 'body: star' > 'specs/a*b.yaml'; echo 'body: x' > specs/axb.yaml
tick; expect_ok "4: helper with specs/a*b.yaml" ait future offer src/order.c 'specs/a*b.yaml' -m m
check "4: the commit adds only specs/a*b.yaml besides .ait/" test "$(git diff-tree -r --no-commit-id --name-only HEAD | grep -v '^.ait/')" = 'specs/a*b.yaml'
check "4: specs/axb.yaml is untouched (still untracked)" sh -c "git status --porcelain -- specs/axb.yaml | grep -qxF '?? specs/axb.yaml'"

# 5: the user's environment
# mini: T16 steps 1-2 need line ranges (src/stock.c:20-30) and peek; T15 needs peek. Only the
# unsupported entry point is checked.
tick; expect_err "5: line-range offer (mini: unsupported)" MINI env GIT_DIFF_OPTS=--unified=10 ait offer src/stock.c:20-30 specs/p.yaml -m m

done_testing
