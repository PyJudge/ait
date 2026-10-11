#!/bin/sh
# T17 fulfill 기본 (7.7)
. "$(dirname "$0")/lib.sh"

# local helpers
gitdir_abs() { (cd "$(git rev-parse --git-dir)" && pwd -P); }
# a quoted scalar of the check-request file: qval FILE KEY
qval() { sed -n "s/^ *$2: \"\\(.*\\)\"\$/\\1/p" "$1" | head -1; }
# the porcelain of the last run, verbatim
out_is() { printf '%s\n' "$@" | cmp -s - "$T/out"; }
msg() { git log -1 --format=%B; }

setup
A=$(ait session open --agent agent-a)
prompt_defs; exec_def guard; exec_def pass

# T17 prep: the provider is the line range src/stock.c:1-10
tick; expect_ok "prepare: future offer O" ait future offer src/stock.c:1-10 specs/guard.yaml as o -m m
O=$(new_id)

# 1
desc="1: fulfill without stage asks for results"
expect_unchanged expect_err "$desc" E_NEEDS_RESULTS ait fulfill "$O" -m m
GD=$(gitdir_abs); Q=$GD/ait/check-request.yaml; W=$GD/ait/check-result.yaml
check "1: report is exactly the 7.7.6 form" out_is "id $O" "provider $O src/stock.c:1-10" "" "task $O:specs/guard.yaml exec" "check-request $Q" "check-result $W" "ready yes"
check "1: Q tree is git write-tree" test "$(qval "$Q" tree)" = "$(git write-tree)"
check "1: Q task has command [./tests/guard.sh]" grep -qxF '  command: ["./tests/guard.sh"]' "$Q"
check "1: Q task has body: guard" sh -c "grep -A1 -xF '  body: |-' '$Q' | grep -qxF '    guard'"
with_r fail ait fulfill "$O" -m m
check "1: -r fail is E_CHECK_FAILED" sh -c "[ $rc = 1 ] && head -1 '$T/err' | grep -qF 'error: check failed:'"

# 2
sed -i.bak '5s/.*/s5 guard/' src/stock.c && rm -f src/stock.c.bak
tick; git commit -qam impl
tick; with_r pass ait fulfill "$O" -m m
check "2: fulfill -r pass ok" test "$rc" = 0
R=$(out_value request)
check "2: stdout request R" test -n "$R"
check "2: stdout fulfilled O" out_has "fulfilled $O"
check "2: stdout check O reported" out_has "check $O reported"
check "2: name-status is only M .ait/O" test "$(git show --name-status --format= HEAD)" = "$(printf 'M\t.ait/%s' "$O")"
check "2: subject [FULFILL] m" test "$(git log -1 --format=%s)" = "[FULFILL] m"
check "2: message order: subject, blank, results, blank, trailers" sh -c "git log -1 --format=%B | awk 'NR==2&&\$0!=\"\"{exit 1} NR==3&&\$0!~/^input: /{exit 1}'"
check "2: message has results for the task" sh -c "git log -1 --format=%B | grep -qF -- '- task: $O:specs/guard.yaml'"
check "2: O's check line is reported pins(HEAD,O)" [ "$(check_line HEAD "$O")" = "reported $(pins HEAD "$O")" ]
check "2: results paragraph precedes trailers" sh -c "git log -1 --format=%B | awk '/^results:/{r=NR} /^Ait-Request:/{c=NR} END{exit !(r && c && r<c)}'"
check "2: rec(O) provider re-pinned to HEAD blob" sh -c "git show HEAD:.ait/$O | grep -qxF 'provider src/stock.c:1-10 $(blob HEAD:src/stock.c)'"
check "2: rec(O) state current" sh -c "git show HEAD:.ait/$O | grep -qxF 'state current'"
# not yet: no peek; 'ait todo O' shows the same unverified line
run ait todo "$O"
check "2: todo O has no unverified line" sh -c "! grep -q '^unverified' '$T/out'"

# 3
tick; expect_ok "3: future offer O2" ait future offer src/order.c specs/pass.yaml as o2 -m m
O2=$(new_id)
echo x >> src/order.c; echo '# y' >> tests/pass.sh; git add src/order.c tests/pass.sh
expect_err "3: mixed is E_MIXED" E_MIXED ait fulfill "$O2" -m m
check "3: report mixed tests/pass.sh" out_has "mixed tests/pass.sh"
check "3: report ready no" out_has "ready no"

# 4
git reset -q --hard
expect_err "4: duplicate ID is E_USAGE" E_USAGE ait fulfill "$O2" "$O2" -m m
expect_err "4: alias is E_USAGE" E_USAGE ait fulfill "$O2" as x -m m
expect_err "4: no message is E_USAGE" E_USAGE ait fulfill "$O2"
expect_err "4: unknown ID is E_ID" E_ID ait fulfill O000000000000 -m m
expect_err "4: current O is E_STATE" E_STATE ait fulfill "$O" -m m
check "4: E_STATE hint" err_has "'$O' is already current; to check it again, run 'ait amend $O -m <why>' to get the tasks"
tick; expect_ok "4: fulfill O2 --force" ait fulfill "$O2" --force -m m
check "4: check O2 forced" out_has "check $O2 forced"
check "4: O2's check line is forced pins" [ "$(check_line HEAD "$O2")" = "forced $(pins HEAD "$O2")" ]
check "4: no results paragraph" sh -c "! git log -1 --format=%B | grep -qE '^(input|results):'"

# 5
tick; expect_ok "5: future offer new:src/g.c O3" ait future offer new:src/g.c specs/p.yaml as o3 -m m
O3=$(new_id)
desc="5: no code is E_CODE_REQUIRED"
expect_unchanged expect_err "$desc" E_CODE_REQUIRED ait fulfill "$O3" -m m
check "5: report code-required O3" out_has "code-required $O3"
check "5: report ready no" out_has "ready no"
check "5: no task lines" sh -c "! grep -qE '^(task|check-request|check-result) ' '$T/out'"
check "5: hint write 'src/g.c'" err_has "write 'src/g.c'"
check "5: hint stage 'src/g.c'" err_has "stage 'src/g.c'"
# T17-5 (5.8 E_CODE_REQUIRED, 5.9a): O3 is in HEAD, so the range move; O3 is my selected future, so the cancel
check "5: hint rerun with '<O3>=<new scope>'" err_has "rerun with '$O3=<new scope>'"
check "5: hint cancels my future O3" err_has "ait future cancel $O3 -m <why>"
check "5: no future amend / drop hint" sh -c "! grep -qE 'ait future (amend|drop)' '$T/err'"
desc="5: --force is also E_CODE_REQUIRED"
tick; expect_unchanged expect_err "$desc" E_CODE_REQUIRED ait fulfill "$O3" --force -m m

# 6
tick; expect_ok "6: future offer O4" ait future offer src/stock.c specs/guard.yaml as o4 -m m
O4=$(new_id)
sed -i.bak '5s/.*/s5/' src/stock.c && rm -f src/stock.c.bak; git add src/stock.c
tick; with_r pass ait fulfill "$O4" -m m
check "6: false pass is accepted" test "$rc" = 0
check "6: message has the task" sh -c "git log -1 --format=%B | grep -qF -- '- task: $O4:specs/guard.yaml'"
check "6: message has outcome pass" sh -c "git log -1 --format=%B | grep -qE '^ +outcome: pass'"
check "6: message has basis" sh -c "git log -1 --format=%B | grep -qE '^ +basis: t'"

# 7
setup
A=$(ait session open --agent agent-a)
exec_def guard
tick; ait future offer src/stock.c:1-10 specs/guard.yaml as o -m m >"$T/out" 2>"$T/err"; O=$(new_id)
tick; git rm -q src/stock.c; git commit -qm rm 2>"$T/err"
check "7: no warning for a future contract" test ! -s "$T/err"
expect_ok "7: todo" ait todo
check "7: O provider ?missing" out_has "provider src/stock.c:1-10 ?missing"
check "7: H10 moves O" err_has "'$O' has no code under its scope; if it was moved, run 'ait fulfill $O=<new scope> -m <why>' to get the tasks"
check "7: H10 cancels my O" err_has "if it is gone for good, run 'ait future cancel $O -m <why>'"

# 7 variant: scope list (T17-7: the provider is src/stock.c:1-10,src/order.c:1-5)
setup
A=$(ait session open --agent agent-a)
exec_def guard
tick; ait future offer src/stock.c:1-10,src/order.c:1-5 specs/guard.yaml as o -m m >"$T/out" 2>"$T/err"; O=$(new_id)
check "7b: future offer with a scope list" test -n "$O"
tick; git rm -q src/stock.c; git commit -qm rm
expect_ok "7b: todo" ait todo
check "7b: provider lines" sh -c "grep -qxF 'provider src/order.c:1-5' '$T/out' && grep -qxF 'provider src/stock.c:1-10 ?missing' '$T/out'"
check "7b: H10 keeps what is found" err_has "run 'ait fulfill $O=src/order.c:1-5,<new scope> -m <why>' to get the tasks"

done_testing
