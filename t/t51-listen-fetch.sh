#!/bin/sh
# spec 6.8, v14 7.2.5: listen --fetch fetches each remote; a failure is a warning with its reason.
. "$(dirname "$0")/lib.sh"
setup
git remote add broken "$T/nowhere.git"
ait listen --fetch 1 >"$T/events" 2>"$T/listen.err" &
pid=$!
for i in $(seq 1 50); do grep -q 'fetch failed' "$T/listen.err" 2>/dev/null && break; sleep 0.1; done
sleep 0.3
kill "$pid" 2>/dev/null
wait "$pid" 2>/dev/null
check 'a remote that cannot be fetched is warned of with its reason' \
	"grep -q \"^warning: fetch failed: .*nowhere.git\" '$T/listen.err'"
check 'the warning has its hint' "grep -q '^hint: check the remotes$' '$T/listen.err'"
check 'the reachable remote is still fetched on its own' "! grep -q \"fetch failed: .*origin\" '$T/listen.err'"
done_testing
