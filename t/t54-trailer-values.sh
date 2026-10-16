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

# git decodes a UTF-16LE message only when its bytes pair up: the message itself decides (v14 5.3)
DAVE=$(ait session open --agent dave)
git config i18n.commitEncoding UTF-16LE
git config i18n.logOutputEncoding UTF-8
for m in a ab abc abcd; do
	tip=$(git rev-parse ait/master)
	run env AIT_SESSION="$DAVE" ait future offer src/stock.c cond.yaml -m "$m"
	if test "$(git rev-parse ait/master)" = "$tip"; then
		check "message '$m': unreadable trailers are refused" "test $rc = 128 && has err 'git config hides the trailers'"
	else
		check "message '$m': a commit made reads its request back" \
			"git log -1 --format='%(trailers:key=Ait-Request,valueonly)' ait/master | grep -Eq '^R[0-9a-f]{12}\$'"
	fi
done
git config --unset i18n.commitEncoding
git config --unset i18n.logOutputEncoding

# pull's merge message is its own: its record is read back as committed
ait contract push >/dev/null 2>&1
git clone -q "$T/origin.git" "$T/c"
(cd "$T/c" && ait init >/dev/null 2>&1 && cp "$T/r/cond.yaml" . &&
	AIT_SESSION=$(ait session open --agent bob) ait future offer src/order.c cond.yaml -m remote >/dev/null 2>&1 &&
	AIT_SESSION=$(ait session list | sed -n 's/^session //p' | head -1) ait contract push >/dev/null 2>&1)
run ait future offer src/stock.c:1-2 cond.yaml -m local
check_rc 'a local checkin makes the pull a merge' 0
git config i18n.commitEncoding UTF-16LE
git config i18n.logOutputEncoding UTF-8
for who in dave alice; do
	S=$(ait session open --agent $who)
	tip=$(git rev-parse ait/master)
	run env AIT_SESSION="$S" ait contract pull
	if test "$(git rev-parse ait/master)" = "$tip"; then
		check "pull as $who: unreadable trailers are refused" "test $rc = 128 && has err 'git config hides the trailers'"
	else
		check "pull as $who: the merge reads its request back" \
			"git log -1 --format='%(trailers:key=Ait-Request,valueonly)' ait/master | grep -Eq '^R[0-9a-f]{12}\$'"
		git reset -q --soft "$tip" 2>/dev/null; git update-ref refs/heads/ait/master "$tip"
	fi
	ait session close "$S" >/dev/null 2>&1
done
git config --unset i18n.commitEncoding
git config --unset i18n.logOutputEncoding
run ait contract pull
check_rc 'without the encoding, the same pull merges' 0
check 'and its merge reads its request back' \
	"test \$(git rev-list --parents -1 ait/master | wc -w) = 3 && git log -1 --format='%(trailers:key=Ait-Request,valueonly)' ait/master | grep -Eq '^R[0-9a-f]{12}\$'"
done_testing
