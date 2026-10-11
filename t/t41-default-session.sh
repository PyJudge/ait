#!/bin/sh
# T41 기본 session (5.1)
. "$(dirname "$0")/lib.sh"

# local helpers
# value of FIELD in the session list block of SID (last `run` must be `ait session list`)
field() { awk -v s="session $1" -v f="$2" '$0==s{p=1} p&&/^$/{exit} p&&index($0,f" ")==1{print substr($0,length(f)+2)}' "$T/out"; }
creator_of_new() { git show "HEAD:.ait/$(new_id)" | sed -n 's/^creator //p'; }
# wa and wb: two worktrees of the fixture, no sessions
worktrees() {
	git worktree add -q "$T/wa" -b wa; git worktree add -q "$T/wb" -b wb
	(cd "$T/wa" && prompt_defs); (cd "$T/wb" && prompt_defs)
	WA=$(cd "$T/wa" && pwd -P); WB=$(cd "$T/wb" && pwd -P)
}

setup
worktrees

# 1
cd "$T/wa"
A=$(ait session open --agent agent-a)
tick; expect_ok "1: offer in a new shell (no AIT_SESSION)" env -u AIT_SESSION ait future offer src/stock.c specs/p.yaml -m m
check "1: creator agent-a <A>" test "$(creator_of_new)" = "agent-a $A"

# 2
C=$(ait session open --agent agent-c 2>/dev/null)
tick; expect_ok "2: offer without AIT_SESSION after opening C" ait future offer src/stock.c specs/p.yaml -m m
check "2: default stays A" test "$(creator_of_new)" = "agent-a $A"
tick; expect_ok "2: offer with AIT_SESSION=C" env AIT_SESSION=$C ait future offer src/stock.c specs/p.yaml -m m
check "2: creator agent-c <C>" test "$(creator_of_new)" = "agent-c $C"
run ait session list
check "2: A is default yes" test "$(field "$A" default)" = yes
check "2: C is default no" test "$(field "$C" default)" = no

# 3
expect_ok "3: close C" ait session close "$C"
tick; expect_ok "3: offer after closing C" ait future offer src/stock.c specs/p.yaml -m m
check "3: default still A" test "$(creator_of_new)" = "agent-a $A"
expect_ok "3: close A" ait session close "$A"
tick; expect_err "3: write without a default session" E_SESSION ait future offer src/stock.c specs/p.yaml -m m

# 4
cd "$T/wb"
B=$(ait session open --agent agent-b)
tick; expect_ok "4: wb offer uses wb's default" ait future offer src/stock.c specs/p.yaml -m m
check "4: creator agent-b <B>" test "$(creator_of_new)" = "agent-b $B"
cd "$T/wa"
tick; expect_err "4: wa still has no default" E_SESSION ait future offer src/stock.c specs/p.yaml -m m
A2=$(ait session open --agent agent-a)
run ait session list
check "4: A2 is wa's default" test "$(field "$A2" default)" = yes
check "4: B is still wb's default" test "$(field "$B" default)" = yes

# 5: restart
cd /; [ "$failed" = 0 ] && rm -rf "$T"
setup
worktrees
cd "$T/wa"; A=$(ait session open --agent agent-a)
cd "$T/wb"; B=$(ait session open --agent agent-b)
cd "$T/wa"
expect_ok "5: session list in wa" ait session list
check "5: B block: agent agent-b" test "$(field "$B" agent)" = agent-b
check "5: B block: worktree wb" test "$(field "$B" worktree)" = "$WB"
check "5: B block: default yes" test "$(field "$B" default)" = yes
expect_err "5: peek with B in wa (not yet: unsupported)" UNSUPPORTED env AIT_SESSION=$B ait peek
# not yet: todo takes the place of peek (5.1 treats them the same)
expect_err "5: todo with B in wa" E_SESSION env AIT_SESSION=$B ait todo
check "5: hint has cd '<wb>'" err_has "cd '$WB'"
cd "$T/wb"
expect_err "5: peek in wb (not yet: unsupported)" UNSUPPORTED ait peek
# not yet: the peek header (`session <B>` on its second line) has no todo counterpart; check todo runs with wb's default
expect_ok "5: todo in wb" ait todo

done_testing
