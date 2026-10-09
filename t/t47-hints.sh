#!/bin/sh
# T47 출력이 다음 할 일을 알린다 (규칙 7, 5.8, 5.9a, 6.7a, 7.2, 9.7)
#
# mini: hints only in their basic form; no listen/sub (events and their hints), no peek,
# no contract pull (replaced by git merge), no line ranges (whole-file scopes), no future
# drop. Steps 2, 4, 6, 7 (events), 9, 10 and 11 are about events or `contract pull`
# hints and are left out. Hints are checked by their command and IDs (0.2).
. "$(dirname "$0")/lib.sh"

id_of() { sed -n "s/^new \([^ ]*\) $1\$/\1/p" "$T/out"; }
setl() { f=$1 l=$2; shift 2; awk -v n="$l" -v t="$*" 'NR==n{print t; next}1' "$f" >"$f.tmp" && cat "$f.tmp" >"$f" && rm "$f.tmp"; }
not() { ! "$@"; }
st() { rec "$1" | sed -n 's/^state //p'; }

# T46's setup (C in wa, E in wb as second sessions)
setup
git worktree add -q "$T/wa" -b a; git worktree add -q "$T/wb" -b b
cd "$T/wa"; A=$(ait session open --agent agent-a)
run ait session open --agent agent-c; C=$(cat "$T/out")
check "1: opening C in wa: rc 0" test $rc = 0
check "1: opening C: warning: this worktree is used by session A" err_has "warning: this worktree is used by session '$A' (agent 'agent-a')"
check "1: opening C: hint close this one" err_has "ait session close $C"
check "1: opening C: hint open a session in a worktree of your own" err_has "ait session open --agent agent-c"
cd "$T/wb"; B=$(ait session open --agent agent-b); E=$(ait session open --agent agent-e 2>/dev/null)
# mini: `ait sub src/stock.c` and the listeners
cd "$T/wa"; prompt_defs; exec_def nobroken
tick; run ait future demand src/order.c src/stock.c specs/p.yaml as d -m 요구
D=$(id_of d)
check "prep: R1" test $rc = 0 -a -n "$D"

# 1. session
cd "$T/wa"
run ait session open --agent agent-a
check "1: a second agent-a session opens" test $rc = 0
A2=$(cat "$T/out")
check "1: warning: agent name is also used" err_has "warning: agent name 'agent-a' is also used by session '$A'"
check "1: hint has 'ait session close'" err_has "ait session close"
check "1: hint has 'AIT_SESSION='" err_has "AIT_SESSION="
expect_ok "1: close it" ait session close $A2
# mini: the listener part (`E_BUSY`, `kill $PA`)

# 2. mini: `has changes not in HEAD` (H9) needs other refs and `ait contract pull`
cd "$T/wb"; git merge -q --ff-only a
tick; run ait future offer src/stock.c specs/p.yaml as o undertake $D o -m 착수
O=$(id_of o)
check "prep: B's O undertakes D" test $rc = 0 -a -n "$O"

# 3. undertaking together
# mini: the `ait peek src/stock.c` hint (H11)
tick; run env AIT_SESSION=$E ait future offer src/stock.c specs/p.yaml as o2 undertake $D o2 -m 착수
O2=$(id_of o2)
check "3: E's offer ok" test $rc = 0 -a -n "$O2"
check "3: warning: D is also undertaken by O" err_has "warning: '$D' is also undertaken by '$O'"
check "3: hint ait future cancel O2" err_has "ait future cancel $O2"

# 4. mini: events

# 5. someone else fulfils (mini: the event hints are not checked)
setl src/stock.c 1 s1x; git add src/stock.c
tick; with_r pass ait fulfill $O -m f
check "5: fulfilled D via O" out_has "fulfilled $D via $O"
check "5: the D-O2 link stays undertaken" test "$(git show HEAD:.ait/undertake/$D-$O2 | sed -n 's/^state //p')" = undertaken
# mini: the plain-commit part needs O's line range (`src/stock.c:20-30`) not to be touched
before=$(snapshot)
expect_err "5: E reopens D alone" E_STATE env AIT_SESSION=$E ait future amend $D -m "바꿀 예정"
check "5: unchanged" test "$(snapshot)" = "$before"
check "5: hint reopen them together, with E's prefix" err_has "AIT_SESSION=$E ait future amend $D future amend $O -m <why>"
tick; run env AIT_SESSION=$E ait future amend $D future amend $O -m "바꿀 예정"
check "5: changed D" out_has "changed $D"
check "5: changed O" out_has "changed $O"
check "5: D and O are future" test "$(st $D) $(st $O)" = "future future"
setl src/stock.c 1 s1y; git add src/stock.c
tick; run git commit -qm after
check "5: a later stock.c commit has no warning" test ! -s "$T/err"

# 6, 7. mini: events (H5, H7/H8 in events)
cd "$T/wa"
tick; with_r pass ait offer src/order.c specs/nobroken.yaml as k -m k
K=$(id_of k)
check "prep: K" test $rc = 0 -a -n "$K"

# 8. error hints
cd "$T/wb"
expect_err "8: fulfill an alias" E_USAGE ait fulfill o -m x
check "8: hint: aliases are local to one command" err_has "aliases are local to one command"
expect_err "8: cancel an alias" E_ID ait future cancel o -m x
check "8: hint: aliases are local to one command (cancel)" err_has "aliases are local to one command"
expect_err "8: -r a missing file" E_READ env AIT_SESSION=$E ait fulfill $O2 -m x -r "$T/none.yaml"
check "8: hint: rerun without -r" err_has "rerun without -r"
expect_err "8: fulfill O2" E_NEEDS_RESULTS env AIT_SESSION=$E ait fulfill $O2 -m x
W=$(out_value check-result)
check "8: tasks are O2's and D's" test "$(grep '^task ' "$T/out" | sed 's/^task \([^:]*\):.*/\1/' | sort | tr '\n' ' ')" = "$(printf '%s\n%s\n' $D $O2 | sort | tr '\n' ' ')"
fill "$W" pass
awk 'BEGIN{d=0} /^    basis: / && !d {d=1; next} 1' "$W" >"$W.tmp" && cat "$W.tmp" >"$W" && rm "$W.tmp"
expect_err "8: a pass without basis" E_FORMAT env AIT_SESSION=$E ait fulfill $O2 -m x -r "$W"
check "8: hint: a pass needs a basis" err_has "a pass needs a basis"
fill "$W" fail
expect_err "8: fail" E_CHECK_FAILED env AIT_SESSION=$E ait fulfill $O2 -m x -r "$W"
check "8: hint: make the code meet the failed task(s)" err_has "make the code meet the failed task(s) and stage it, then rerun without -r to get the new tasks"
# mini: the release hint (`AIT_SESSION=<E> ait release <D> <O2> -m <why>`) is not checked (basic hints)

# 8. no one is told to withdraw someone else's contract
cd "$T/wa"
expect_err "8: C cancels current K" E_STATE env AIT_SESSION=$C ait future cancel $K -m x
check "8: C: hint ait future amend K" err_has "ait future amend $K -m <why>"
check "8: C: no future drop hint" not err_has "ait future drop"
expect_err "8: A cancels current K" E_STATE ait future cancel $K -m x
check "8: A: hint ait future amend K" err_has "ait future amend $K -m <why>"
# mini: A's withdraw hint (`ait future drop <K> as x -m <why>`, `with nothing staged`) needs future drop

done_testing
