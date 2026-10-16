#!/bin/sh
# spec 4.5: the trailers read back are the ones committed: the request's own RID, agent and session.
. "$(dirname "$0")/lib.sh"
setup
tip=$(git rev-parse ait/master)
git config core.commentString "Ait-Session: S"
run ait future demand src/order.c src/stock.c:5-9 cond.yaml -m need
check "a comment prefix that hides the real session is refused" \
	"test $rc = 128 && has err \"^fatal: git config hides the trailers: 'Ait-Session' ('core.commentstring')\\$\""
check 'and nothing is checked in' "test \"\$(git rev-parse ait/master)\" = $tip"
git config core.commentString "Ait-Agent: al"
run ait future offer src/stock.c cond.yaml -m agent
check "a comment prefix that hides the real agent is refused" "test $rc = 128 && has err \"'Ait-Agent'\""
git config core.commentString "Ait-Agent: R"
run ait future offer src/stock.c cond.yaml -m readable
check_rc 'a comment prefix that hides no real value is accepted' 0
check 'and the agent reads back' "test \"\$(git log -1 --format='%(trailers:key=Ait-Agent,valueonly)' ait/master)\" = alice"
git config core.commentString "Ait-Session: S"
ait contract push >/dev/null 2>&1
git branch -q --set-upstream-to=origin/ait/master ait/master
run ait contract pull
check "contract pull reads the real values back too" "test $rc = 128 && has err \"'Ait-Session'\""
git config --unset core.commentString
done_testing
