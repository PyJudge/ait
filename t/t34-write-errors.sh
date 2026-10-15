#!/bin/sh
# v14 5.8 E_IO: one file diagnostic and a replayable listener recovery hint.
. "$(dirname "$0")/lib.sh"
setup
BOB=$(AIT_SESSION=- ait session open --agent bob)
AIT_SESSION="$BOB" ait sub src/order.c >/dev/null
seen=$(git rev-parse --path-format=absolute --git-common-dir)/ait/sessions/$BOB.seen
mkdir "$seen"
ait future offer src/order.c cond.yaml -m event >/dev/null
AIT_SESSION="$BOB" ait listen >"$T/out" 2>"$T/err" &
listener=$!
trap 'kill "$listener" 2>/dev/null' 0
for i in $(seq 1 100); do
	kill -0 "$listener" 2>/dev/null || break
	sleep 0.05
done
kill "$listener" 2>/dev/null
wait "$listener"
rc=$?
check_rc 'a failed delivery-state write ends the listener' 128
check 'the write failure identifies the affected file' "has err \"write error: '$seen'\""
check 'the shared file error includes listener recovery in the selected session' \
	"grep -Fq \"make '$seen' writable (free space, permissions), then start 'AIT_SESSION=$BOB ait listen' again\" $T/err"
trap - 0
done_testing
