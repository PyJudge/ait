#!/bin/sh
# spec 4.6: an explicitly invalid session is refused even when none is required
. "$(dirname "$0")/lib.sh"
setup

missing=Sffffffffffff
run env AIT_SESSION=$missing ait init extra
check_rc "init syntax precedes optional session validation" 129
run env AIT_SESSION=$missing ait session list extra
check_rc "session syntax precedes optional session validation" 129
run env AIT_SESSION=$missing ait session open --agent "bad name"
check_rc "agent-name syntax precedes optional session validation" 129

git init -q "$T/empty"
cd "$T/empty"
run env AIT_SESSION=$missing ait init
check_rc "init refuses an explicitly invalid session" 128
check "init reports the session refusal" "has err \"unknown session '$missing'\""
check "refused init creates no branch or hook" "! git show-ref --verify --quiet refs/heads/ait/master && ! test -e .git/hooks/reference-transaction"
cd "$T/r"

run env AIT_SESSION=$missing ait session list
check_rc "session list refuses an explicitly invalid session" 128
check "a refused list produces no success output" "! test -s $T/out"
list_hint=$(sed -n 's/^hint: //p' "$T/err")
run env AIT_SESSION=$missing sh -c "$list_hint"
check_rc "the unknown-session hint works with the failing environment" 0
run env AIT_SESSION=$missing ait session open --agent bob
check_rc "session open refuses an explicitly invalid session" 128
check "a refused open creates no session" "test \"\$(ait session list | sed -n 's/^session //p' | wc -l | tr -d ' ')\" = 1"

checkout=$(git rev-parse --git-path ait-branch)
run env AIT_SESSION=$missing ait checkout ait/master
check_rc "contract checkout refuses an explicitly invalid session" 128
check "a refused contract checkout saves no selection" "! test -e $checkout"
rm -f "$checkout"
run env AIT_SESSION=$missing ait switch ait/master
check_rc "contract switch refuses an explicitly invalid session" 128
check "a refused contract switch keeps the code branch" "test \"\$(git symbolic-ref --short HEAD)\" = master"
git switch -q master

git branch other
run env AIT_SESSION=$missing ait checkout other
check_rc "Git checkout is not invoked with an invalid session" 128
check "a refused Git checkout keeps HEAD" "test \"\$(git symbolic-ref --short HEAD)\" = master"
git switch -q master
run env AIT_SESSION=$missing ait switch other
check_rc "Git switch is not invoked with an invalid session" 128
check "a refused Git switch keeps HEAD" "test \"\$(git symbolic-ref --short HEAD)\" = master"
git switch -q master

BOB=$(AIT_SESSION=- ait session open --agent close-target 2>/dev/null)
run env AIT_SESSION=$missing ait session close "$BOB"
check_rc "explicit session close also validates AIT_SESSION" 128
check "a refused close keeps its target session" "test -f .git/ait/sessions/$BOB"

git worktree add -q --detach "$T/gone" HEAD
GONE=$(cd "$T/gone" && AIT_SESSION=- ait session open --agent gone)
git worktree remove --force "$T/gone"
run env AIT_SESSION=$GONE ait init
check_rc "a selected session in a removed worktree is refused" 128
close_hint=$(sed -n "s/.*close it: '\([^']*\)'.*/\1/p" "$T/err")
open_hint=$(sed -n "s/.*with the same name.*: '\([^']*\)'.*/\1/p" "$T/err")
check "the removed-worktree hint supplies both recovery commands" "test -n \"$close_hint\" && test -n \"$open_hint\""
run env AIT_SESSION=$GONE sh -c "$close_hint"
check_rc "the close hint works with the failing environment" 0
check "the close hint removed the abandoned session" "! test -f .git/ait/sessions/$GONE"
run env AIT_SESSION=$GONE sh -c "$open_hint"
check_rc "the open hint works after the selected session was closed" 0

# A session file's agent is read whole and checked, never cut to a buffer (v14 7.2.1).
printf 'body: |\n  x\n' >cond-agent.yaml
BAD=$(ait session open --agent bob)
tip=$(git rev-parse ait/master)
for name in "$(printf 'a%.0s' $(seq 1 100))" "bad name"
do
	sed "1s/.*/agent $name/" .git/ait/sessions/$BAD >"$T/session" && cp "$T/session" .git/ait/sessions/$BAD
	run env AIT_SESSION=$BAD ait future demand src/order.c src/stock.c cond-agent.yaml -m agent
	check_rc "a session whose agent is '$name' is not usable" 128
	check "the unusable agent writes no contract" \
		"has err \"no usable ait session: unknown session '$BAD'\" && test \"\$(git rev-parse ait/master)\" = $tip"
done

run env AIT_SESSION=- ait checkout ait/master
check_rc "contract checkout still allows no session" 0
run env AIT_SESSION=- ait switch --detach HEAD
check_rc "Git switch still allows no session" 0
check "Git still handles its own switch options" "! git symbolic-ref -q HEAD"
git switch -q master
run env AIT_SESSION=- ait session list
check_rc "session list still allows no session" 0
run ait session close
check_rc "implicit close still selects the worktree default" 0
check "implicit close removed that session" "! test -f .git/ait/sessions/$ALICE"
run env AIT_SESSION=- ait session close
check_rc "implicit close still requires a session" 128

done_testing
