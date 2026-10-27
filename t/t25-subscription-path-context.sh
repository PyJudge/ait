#!/bin/sh
# New subscription display does not read code trees that the subscription does not need.
. "$(dirname "$0")/lib.sh"
setup
run ait future offer src/stock.c cond.yaml -m watched
O=$(newest O)
BOB=$(AIT_SESSION=- ait session open --agent listener-path 2>/dev/null)
index=$(git rev-parse --path-format=absolute --git-path index)
printf 'another Git writer\n' > "$index.lock"
cp "$index.lock" "$T/lock-before"
cp "$index" "$T/index-before"
for item in "scope /" "scope src/" "scope src/stock.c" "id $O"
do
	arg=${item#* }
	run ait sub "$arg"
	check_rc "new subscription '$arg' does not require the code index" 0
	check "new subscription '$arg' is stored" "grep -Fqx 'sub $item' .git/ait/sessions/$ALICE"
done
run ait sub absent-file
check_rc "a missing-file subscription does not acquire the code index lock" 0
check "missing-file display retains its location marker" "grep -qx 'sub scope absent-file ?missing' $T/out"
run ait sub new:src/stock.c
check_rc "a new-file subscription does not acquire the code index lock" 0
check "new-file display retains its existence marker" "grep -qx 'sub scope new:src/stock.c ?exists' $T/out"
AIT_SESSION=$BOB ait listen / > "$T/events" 2> "$T/listen.err" &
listener=$!
for i in $(seq 1 100)
do
	grep -qx 'sub scope /' "$T/listen.err" && break
	kill -0 "$listener" 2>/dev/null || break
	sleep 0.02
done
check "listen can add an abstract subscription while Git owns the index lock" "grep -qx 'sub scope /' $T/listen.err && kill -0 $listener 2>/dev/null"
kill -TERM "$listener" 2>/dev/null
wait "$listener" 2>/dev/null
rc=$?
test_have_prereq SIGNALS "the subscribed listener terminates normally" && check_rc "the subscribed listener terminates normally" 0
check "subscriptions preserve the foreign lock and code index bytes" "cmp -s $T/lock-before $index.lock && cmp -s $T/index-before $index"
rm "$index.lock"

done_testing
