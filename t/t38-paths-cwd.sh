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
expect_err "3: peek now (not yet: unsupported)" UNSUPPORTED ait peek now "$K"
# not yet: `ait todo K` takes the place of `ait peek now K`
expect_ok "3: todo K" ait todo "$K"
check "3: provider src/재고.c ?changed" out_has "provider src/재고.c ?changed"

# 4: literal paths
# the literal-path rule through a helper's FILES (5.12 GIT_LITERAL_PATHSPECS); it also makes
# specs/a*b.yaml a spec file of a future offer, so a contract path for 'contract add' (6.1, 7.5.1)
echo 'body: star' > 'specs/a*b.yaml'; echo 'body: x' > specs/axb.yaml
tick; expect_ok "4: helper with specs/a*b.yaml" ait future offer src/order.c 'specs/a*b.yaml' -m m
check "4: the commit adds only specs/a*b.yaml besides .ait/" test "$(git diff-tree -r --no-commit-id --name-only HEAD | grep -v '^.ait/')" = 'specs/a*b.yaml'
check "4: specs/axb.yaml is untouched (still untracked)" sh -c "git status --porcelain -- specs/axb.yaml | grep -qxF '?? specs/axb.yaml'"
# T38-4 (5.12, 7.5.2): 'contract add' takes the path literally
echo 'body: star2' > 'specs/a*b.yaml'
expect_ok "4: contract add 'specs/a*b.yaml'" ait contract add 'specs/a*b.yaml'
check "4: stdout is staged specs/a*b.yaml" test "$(cat "$T/out")" = 'staged specs/a*b.yaml'
check "4: only specs/a*b.yaml is staged" test "$(git diff --cached --name-only)" = 'specs/a*b.yaml'
check "4: specs/axb.yaml is still untracked" sh -c "git status --porcelain -- specs/axb.yaml | grep -qxF '?? specs/axb.yaml'"
git reset -q HEAD -- 'specs/a*b.yaml'; git checkout -q HEAD -- 'specs/a*b.yaml'

# 5: the user's environment changes neither the scope computation nor the trailer reading
# T38 step 5: T16's preparation and its steps 1-2 in a shell with GIT_DIFF_OPTS and trailer.separators
GIT_DIFF_OPTS=--unified=10; export GIT_DIFF_OPTS
git config trailer.separators %
tick; with_r pass ait offer src/stock.c:20-30 specs/p.yaml as o -m m
check "5: offer src/stock.c:20-30 ok" test "$rc" = 0
O5=$(out_value new | sed -n 's/ o$//p')
check "5: rec(O) provider" test "$(rec $O5 | grep '^provider ')" = "provider src/stock.c:20-30 $(blob HEAD:src/stock.c)"
{ for i in 1 2 3 4 5; do echo "n$i"; done; cat src/stock.c; } >src/stock.c.new && mv src/stock.c.new src/stock.c
git add src/stock.c; tick; git commit -qm 'T16 step 1' 2>/dev/null
# not yet: `ait peek now O` -> `ait todo O`; the `peek now src/stock.c:35-35`, `:36-36` and `:24-24` checks need peek
expect_ok "5: T16 step 1: todo O" ait todo $O5
check "5: T16 step 1: provider src/stock.c:25-35" out_has "provider src/stock.c:25-35"
check "5: T16 step 1: O is not unverified" sh -c "! grep -q unverified '$T/out'"
grep -vxE 's2[234]' src/stock.c >src/stock.c.new && mv src/stock.c.new src/stock.c
git add src/stock.c; tick; git commit -qm 'T16 step 2' 2>/dev/null
expect_ok "5: T16 step 2: todo O" ait todo $O5
check "5: T16 step 2: provider src/stock.c:25-32 ?changed" out_has "provider src/stock.c:25-32 ?changed"
check "5: T16 step 2: O is unverified" out_has "unverified yes"
# not yet: T15 (the history under trailer.separators) needs peek

done_testing
