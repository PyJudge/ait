#!/bin/sh
# T03 session (5.1, 7.2)
. "$(dirname "$0")/lib.sh"

# local helpers
# the session list block of SID, one line per row
block() { awk -v s="session $1" '$0==s{p=1} p&&/^$/{exit} p' "$T/out"; }
lines_are() { b=$1; shift; [ "$b" = "$(printf '%s\n' "$@")" ]; }
first_err_is() { head -1 "$T/err" | grep -qxF -- "$1"; }

setup
prompt_defs
RT=$(pwd -P)

# 1
run ait session open --agent agent-a
check "1: session open ok" test "$rc" = 0
S=$(cat "$T/out")
check "1: one SID line" sh -c "[ \$(wc -l < '$T/out') = 1 ] && grep -qE '^S[0-9a-f]{12}\$' '$T/out'"

# 2
expect_ok "2: session list" ait session list
check "2: block in order" lines_are "$(block "$S")" "session $S" "agent agent-a" "worktree $RT" "default yes" "subs 0" "listening no"
# mini: no sub/unsub
expect_err "2: sub is mini-unsupported" MINI ait sub src/order.c
expect_err "2: unsub is mini-unsupported" MINI ait unsub src/order.c

# 3
expect_err "3: --agent 'a b'" E_USAGE ait session open --agent 'a b'
expect_err "3: --agent ''" E_USAGE ait session open --agent ''
expect_err "3: --hook x" E_USAGE ait session open --hook x
expect_err "3: --agent unknown" E_USAGE ait session open --agent unknown

# 4
desc="4: AIT_SESSION=- offer"
tick; expect_unchanged expect_err "$desc" E_SESSION env AIT_SESSION=- ait future offer src/stock.c specs/p.yaml -m m
tick; expect_ok "4: unset AIT_SESSION offer uses the default session" ait future offer src/stock.c specs/p.yaml -m m
ID=$(new_id)
check "4: creator agent-a <S>" sh -c "git show HEAD:.ait/$ID | grep -qxF 'creator agent-a $S'"

# 5
tick; expect_err "5: unknown SID offer" E_SESSION env AIT_SESSION=S000000000000 ait future offer src/stock.c specs/p.yaml -m m
expect_err "5: unknown SID peek (mini: peek unsupported)" MINI env AIT_SESSION=S000000000000 ait peek
# mini: todo takes the place of peek (5.1 treats them the same)
expect_err "5: unknown SID todo" E_SESSION env AIT_SESSION=S000000000000 ait todo

# 6
git worktree add -q "$T/w2" -b w2
cd "$T/w2"; prompt_defs
tick; expect_err "6: other worktree's SID offer" E_SESSION env AIT_SESSION=$S ait future offer src/stock.c specs/p.yaml -m m
check "6: hint has cd '<r>'" err_has "cd '$RT'"
expect_err "6: peek (mini: unsupported)" MINI env AIT_SESSION=$S ait peek src/stock.c
expect_err "6: other worktree's SID todo" E_SESSION env AIT_SESSION=$S ait todo
check "6: todo hint has cd '<r>'" err_has "cd '$RT'"
cd "$T/r"

# 7
expect_ok "7: session close S" ait session close "$S"
expect_err "7: close S again" E_SESSION ait session close "$S"
tick; expect_err "7: default session is gone" E_SESSION ait future offer src/stock.c specs/p.yaml -m m

# 8
cd "$T/w2"; S2=$(ait session open --agent agent-b); cd "$T/r"
W2=$(cd "$T/w2" && pwd -P)
rm -rf "$T/w2"
expect_err "8: peek with S2 (mini: unsupported)" MINI env AIT_SESSION=$S2 ait peek
expect_err "8: todo with S2 whose worktree is gone" E_SESSION env AIT_SESSION=$S2 ait todo
# mini: basic hint only; the "its worktree is gone; close it: 'ait session close <S2>', then ...
# 'ait session open --agent agent-b'" form is not checked

# 8 (alternative, from the state of 8): open agent-b in r without closing S2
run ait session open --agent agent-b
check "8b: open agent-b in r ok" test "$rc" = 0
SB=$(cat "$T/out")
check "8b: warning names S2 and its worktree" err_has "warning: agent name 'agent-b' is also used by session '$S2' (worktree '$W2')"
# mini: basic hint only; the "its worktree is gone; close it: ..." hint of 7.2.1 is not checked
ait session close "$SB" >/dev/null 2>&1

# 8 (first item): remove the registration, re-add the branch elsewhere, close and reopen
git worktree remove --force "$T/w2"
git worktree add -q "$T/w2b" w2
cd "$T/w2b"
expect_ok "8a: close S2 from w2b" ait session close "$S2"
run ait session open --agent agent-b
check "8a: open agent-b in w2b ok" test "$rc" = 0
S3=$(cat "$T/out")
W2B=$(pwd -P)
ait session list >"$T/out" 2>"$T/err"
check "8a: its worktree is w2b" sh -c "awk -v s='session $S3' '\$0==s{p=1} p&&/^\$/{exit} p' '$T/out' | grep -qxF 'worktree $W2B'"
check "8a: HEAD is w2" test "$(git symbolic-ref HEAD)" = refs/heads/w2
ait session close "$S3" >/dev/null 2>&1
cd "$T/r"

# 9
SD="$(cd "$(git rev-parse --git-common-dir)" && pwd -P)/ait/sessions"
chmod a-w "$SD"
run ait session open --agent agent-c
chmod u+w "$SD"
check "9: exit 128" test "$rc" = 128
check "9: first line fatal: write error:" sh -c "head -1 '$T/err' | grep -q '^fatal: write error:'"
check "9: hint make '<sessions>' writable" err_has "hint: make '$SD' writable (free space, permissions), then rerun this command"

# 10
S4=$(ait session open --agent agent-d)
git switch -q --orphan o
tick; expect_err "10: offer on an unborn branch" E_BUSY ait future offer src/stock.c specs/p.yaml -m m
check "10: hint run 'ait init' first" err_has "hint: run 'ait init' first"
tick; expect_ok "10: init on the unborn branch" ait init
check "10: init created <OID>" out_has "init created $(git rev-parse HEAD)"
check "10: first commit (no parent)" test "$(git rev-list --count HEAD)" = 1
check "10: tree has .ait/init only" test "$(git ls-tree -r --name-only HEAD)" = .ait/init

done_testing
