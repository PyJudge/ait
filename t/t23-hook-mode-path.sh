#!/bin/sh
# spec 9.8: Git ignores a nonexecutable hook, including a symlink to one.
. "$(dirname "$0")/lib.sh"
setup

BOB=$(AIT_SESSION=- ait session open --agent bob 2>/dev/null)
AIT_SESSION=$BOB ait sub src/stock.c >/dev/null
hook=$(git rev-parse --path-format=absolute --git-path hooks/reference-transaction)
mv "$hook" "$T/nonexecutable-hook"
chmod 0644 "$T/nonexecutable-hook"
ln -s "$T/nonexecutable-hook" "$hook"
run ait init
check_rc "init accepts a nonexecutable existing hook alias" 0
check "init reports that the existing hook leaves listen to polling" "has err 'wakes by polling only'"
check "init preserves the existing hook alias and permissions" "test -L \"$hook\" && ! test -x \"$hook\""

S=$(git rev-parse --git-common-dir)/ait/sessions/$BOB
before=$(ls -i "$S" | awk '{print $1}')
AIT_SESSION=$BOB ait listen >"$T/events" 2>"$T/listen.err" &
L=$!
# Completing a native round atomically replaces the session file.
for i in $(seq 1 50)
do
	test "$(ls -i "$S" | awk '{print $1}')" != "$before" && break
	sleep 0.1
done
after=$(ls -i "$S" | awk '{print $1}')
check "listen completed its initial round before the new event" "test '$after' != '$before'"
ait future demand src/order.c src/stock.c:1-5 cond.yaml -m polling >/dev/null
for i in $(seq 1 30)
do
	grep -q ait-event-end "$T/events" && break
	sleep 0.1
done
check "a nonexecutable ait hook uses one-second polling" "grep -q ait-event-end $T/events"
kill "$L"; wait "$L" 2>/dev/null

done_testing
