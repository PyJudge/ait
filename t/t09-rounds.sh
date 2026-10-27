#!/bin/sh
# spec 9.1-9.8, v14 4.5, 9.5: a listen round's state, its keys, its locks
. "$(dirname "$0")/lib.sh"
setup
racing_git

ait future demand src/order.c src/stock.c:1-5 cond.yaml -m "before bob" >/dev/null
BOB=$(AIT_SESSION=- ait session open --agent bob 2>/dev/null)
check "session open writes the tip snapshot (9.1)" "grep -q '^tip [0-9a-f]* refs/heads/ait/master\$' \"\$(git rev-parse --git-common-dir)/ait/sessions/$BOB\""
AIT_SESSION=$BOB ait sub src/stock.c >/dev/null
AIT_SESSION=$BOB ait listen > "$T/events" 2>"$T/listen.err" &
L=$!
sleep 1.5
check "what was there before the session is not sent (9.2)" "! grep -q ait-event-end $T/events"

git push -q origin ait/master
git branch -q --set-upstream-to=origin/ait/master ait/master
git clone -q "$T/origin.git" "$T/c"
(cd "$T/c" && ait init >/dev/null && ait session open --agent carol >/dev/null && cp "$T/r/cond.yaml" . &&
 ait future demand src/order.c src/stock.c:7-8 cond.yaml -m carol >/dev/null && ait contract push >/dev/null 2>&1)
ait future offer src/order.c cond.yaml -m mine >/dev/null
ait contract pull >/dev/null
for i in $(seq 1 30); do grep -q ait-event-end "$T/events" && break; sleep 0.1; done
sleep 1.5
check "a contract that arrives by a merge goes out once, not again for the merge (9.4)" "test \"\$(grep -c '^contract D[0-9a-f]* create' $T/events)\" = 1"
kill $L; wait $L 2>/dev/null

cat > .git/hooks/reference-transaction <<'H'
#!/bin/sh
exit 0
H
AIT_SESSION=$BOB ait listen > "$T/events2" 2>/dev/null &
L=$!
sleep 1
ait future demand src/order.c src/stock.c:9-10 cond.yaml -m "no hook of ours" >/dev/null
for i in $(seq 1 25); do grep -q ait-event-end "$T/events2" && break; sleep 0.1; done
check "another hook leaves listen to polling every second (9.8)" "grep -q ait-event-end $T/events2"
kill $L; wait $L 2>/dev/null

mkfifo "$T/fifo"
(exec 3<"$T/fifo"; sleep 0.2) &
AIT_SESSION=$BOB ait listen > "$T/fifo" 2>/dev/null &
L=$!
sleep 1
ait future demand src/order.c src/stock.c:11-12 cond.yaml -m "nobody reads" >/dev/null
sleep 2
check "a reader gone ends listen (v14 9.5-3, 5.7)" "! kill -0 $L 2>/dev/null"
kill $L 2>/dev/null; wait $L 2>/dev/null

S="$(git rev-parse --git-common-dir)/ait/sessions/$BOB"
if test_have_prereq FLOCK "session close waits for the session's lock (v14 4.5)"
then
	perl -e 'use Fcntl ":flock"; open(F, ">>", $ARGV[0]) or die; flock(F, LOCK_EX); sleep 2' "$S.lock" &
	P=$!
	sleep 0.5
	AIT_SESSION=$BOB ait session close "$BOB" >/dev/null 2>&1 &
	C=$!
	sleep 0.5
	check "session close waits for the session's lock (v14 4.5)" "test -e \"$S\""
	wait $P; wait $C
	check "and closes once it has it" "! test -e \"$S\""
fi

done_testing
