#!/bin/sh
# T24 `new:` 범위의 이행 (3.11, 7.7.4)
. "$(dirname "$0")/lib.sh"

setup
A=$(ait session open --agent agent-a)
exec_def pass

# 1
tick; expect_ok "1: future offer new:src/guard.c" ait future offer new:src/guard.c specs/pass.yaml as o -m m
O=$(new_id)
check "1: rec(O) provider new:src/guard.c" sh -c "git show HEAD:.ait/$O | grep -qxF 'provider new:src/guard.c'"
desc="1: no file is E_CODE_REQUIRED"
expect_unchanged expect_err "$desc" E_CODE_REQUIRED ait fulfill "$O" -m m

# 2
echo guard > src/guard.c; git add src/guard.c
tick; with_r pass ait fulfill "$O" -m m
check "2: fulfill O -r pass ok" test "$rc" = 0
check "2: provider becomes src/guard.c <blob>" sh -c "git show HEAD:.ait/$O | grep -qxF 'provider src/guard.c $(blob HEAD:src/guard.c)'"

# 3
tick; expect_ok "3: future demand new:src/c.c" ait future demand new:src/c.c src/stock.c specs/pass.yaml as d -m m
D=$(new_id)
sed -i.bak '1s/.*/s1 c/' src/stock.c && rm -f src/stock.c.bak; git add src/stock.c
tick; with_r pass ait fulfill "$D" -m m
check "3: fulfill D -r pass ok" test "$rc" = 0
check "3: consumer stays new:src/c.c" sh -c "git show HEAD:.ait/$D | grep -qxF 'consumer new:src/c.c'"
check "3: D current" sh -c "git show HEAD:.ait/$D | grep -qxF 'state current'"

done_testing
