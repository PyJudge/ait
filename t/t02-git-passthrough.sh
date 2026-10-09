#!/bin/sh
# T02 git 통과 (7.14)
. "$(dirname "$0")/lib.sh"

setup
echo x >> src/order.c; echo new > untracked.txt

# 1
run git status --porcelain; g_out=$(cat "$T/out"); g_rc=$rc
run ait status --porcelain
check "1: ait status --porcelain stdout equals git's" test "$(cat "$T/out")" = "$g_out"
check "1: same exit code" test "$rc" = "$g_rc"

# 2
run git rev-parse --verify no-such-ref; g_err=$(cat "$T/err"); g_rc=$rc
run ait rev-parse --verify no-such-ref
check "2: ait rev-parse --verify no-such-ref stderr equals git's" test "$(cat "$T/err")" = "$g_err"
check "2: same exit code ($g_rc)" test "$rc" = "$g_rc"

# 3
run ait
check "3: ait with no arguments exits 1" test "$rc" = 1
check "3: it prints the usage" sh -c "cat '$T/out' '$T/err' | grep -q 'usage: ait'"

done_testing
