#!/bin/sh
# spec 6.8, 9.7 and v14 7.2.4, 9.7: saved subscriptions and event payloads.
. "$(dirname "$0")/lib.sh"
setup

session_file=".git/ait/sessions/$ALICE"
cp "$session_file" "$T/session.before"
run ait sub fresh-example-path Daaaaaaaaaaaa
check_rc "sub rejects a later unknown ID" 128
check "a rejected sub prints no successful addition" "! test -s '$T/out'"
check "a rejected sub leaves the session byte-for-byte unchanged" \
	"cmp '$T/session.before' '$session_file'"
run ait listen another-fresh-path Daaaaaaaaaaaa
check_rc "listen arguments share sub's complete validation" 128
check "a rejected listen prints neither events nor successful additions" \
	"! test -s '$T/out' && ! grep -q '^sub ' '$T/err'"
check "rejected listen arguments leave the session byte-for-byte unchanged" \
	"cmp '$T/session.before' '$session_file'"
run ait sub src/stock.c src/order.c src/stock.c
check_rc "valid subscriptions are saved together" 0
printf 'sub scope src/stock.c\nsub scope src/order.c\n' >"$T/sub.expected"
check "successful subscription output retains argument order without duplicates" \
	"cmp '$T/sub.expected' '$T/out' && grep -qx 'sub scope src/stock.c' '$session_file' && grep -qx 'sub scope src/order.c' '$session_file'"
run ait sub src/stock.c src/order.c
check "subscriptions already saved do not print another successful addition" \
	"test '$rc' = 0 && ! test -s '$T/out'"

if test_have_prereq SANITY "sub and unsub report a failed session write"
then
	cp "$session_file" "$T/session.before"
	trap 'chmod u+w .git/ait/sessions' 0
	chmod a-w .git/ait/sessions
	run ait sub unsaved-example-path
	chmod u+w .git/ait/sessions
	check_rc "sub reports a failed session write" 128
	check "a failed sub write prints no successful addition" "! test -s '$T/out'"
	check "a failed sub write preserves the previous subscriptions" \
		"cmp '$T/session.before' '$session_file'"
	chmod a-w .git/ait/sessions
	run ait unsub src/stock.c
	chmod u+w .git/ait/sessions
	check_rc "unsub reports a failed session write" 128
	check "a failed unsub write prints no successful removal" "! test -s '$T/out'"
	check "a failed unsub write preserves the previous subscriptions" \
		"cmp '$T/session.before' '$session_file'"
fi
run ait unsub src/order.c src/stock.c
check_rc "valid subscription removals are saved together" 0
printf 'unsub scope src/stock.c\nunsub scope src/order.c\n' >"$T/unsub.expected"
check "successful removal output retains session order and matches saved removals" \
	"cmp '$T/unsub.expected' '$T/out' && ! grep -q '^sub ' '$session_file'"
run ait unsub src/stock.c
check "an unmatched removal prints no successful removal" "test '$rc' = 0 && ! test -s '$T/out'"

listener=
stop_listener() {
	if [ -n "$listener" ]; then
		kill "$listener" 2>/dev/null
		wait "$listener" 2>/dev/null
		listener=
	fi
}
trap stop_listener 0

# Start after the request: the session's earlier snapshot retains the event.
drain() {
	EVENTS="$T/$2.events"
	AIT_SESSION=$1 ait listen >"$EVENTS" 2>"$T/$2.err" &
	listener=$!
	tip=$(git rev-parse ait/master)
	for i in $(seq 1 100); do
		grep -q "^tip $tip refs/heads/ait/master$" ".git/ait/sessions/$1" && break
		kill -0 "$listener" 2>/dev/null || break
		sleep 0.05
	done
	stop_listener
}

run ait future demand src/order.c src/stock.c cond.yaml -m current-demand
CD=$(newest D)
run ait fulfill "$CD" --force -m fulfilled-demand
check_rc "the reason fixture has a current demand" 0
run ait future offer src/order.c cond.yaml as o undertake "$CD" o -m future-offer
CO=$(newest O)
run ait future drop "$CD" -m future-drop
DX=$(newest X)
run ait future demand src/order.c src/stock.c cond.yaml -m first-future-demand
FD1=$(newest D)
run ait future demand src/order.c src/stock.c cond.yaml -m second-future-demand
FD2=$(newest D)
run ait future offer src/order.c cond.yaml as o undertake "$FD1" o -m second-future-offer
FO=$(newest O)
check_rc "the reason fixture includes a second live offer" 0
RECEIVER=$(AIT_SESSION=- ait session open --agent alice 2>/dev/null)
AIT_SESSION=$RECEIVER ait sub "$FD2" "$CD" "$FD1" src/stock.c src/order.c >/dev/null
SENDER=$(AIT_SESSION=- ait session open --agent bob 2>/dev/null)
run env AIT_SESSION=$SENDER ait future amend "$FD2" src/order.c:3-4 - - \
	future amend "$CD" src/order.c:1-2 - - future amend "$FD1" src/order.c:5-6 - - -m reason-order
check_rc "one request changes contracts with every applicable reason category" 0
drain "$RECEIVER" reasons
check "the mixed-reason request produces exactly one complete event" \
	"test \"\$(grep -c '^ait-event-end$' '$EVENTS')\" = 1"
{
	printf 'reason future demand %s\n' "$FD1" "$FD2" | sort
	printf 'reason future offer %s\n' "$CO" "$FO" | sort
	printf 'reason future drop %s\n' "$DX"
	printf 'reason demand %s\n' "$CD"
	printf 'reason sub id %s\n' "$CD" "$FD1" "$FD2" | sort
	printf 'reason sub scope src/order.c\nreason sub scope src/stock.c\n'
} >"$T/reasons.expected"
sed -n '/^reason /p' "$EVENTS" >"$T/reasons.actual"
check "reasons follow category order, then ID or display, with each reason once" \
	"cmp '$T/reasons.expected' '$T/reasons.actual'"

RECEIVER=$(AIT_SESSION=- ait session open --agent bob 2>/dev/null)
AIT_SESSION=$RECEIVER ait sub src/stock.c >/dev/null
run ait offer src/stock.c cond.yaml --force -m new-fulfilled
NOW=$(newest O)
check_rc "an offer can be created and fulfilled in one request" 0
drain "$RECEIVER" created-fulfilled
printf 'contract %s create\ncontract %s fulfil\n' "$NOW" "$NOW" >"$T/actions.expected"
sed -n '/^contract /p' "$EVENTS" >"$T/actions.actual"
check "an immediately fulfilled new offer emits create followed by fulfil" \
	"cmp '$T/actions.expected' '$T/actions.actual'"

run ait future offer src/stock.c cond.yaml -m new-future
LATER=$(newest O)
check_rc "a future offer can be created without fulfilment" 0
drain "$RECEIVER" created-future
printf 'contract %s create\n' "$LATER" >"$T/actions.expected"
sed -n '/^contract /p' "$EVENTS" >"$T/actions.actual"
check "a new future offer emits only create" "cmp '$T/actions.expected' '$T/actions.actual'"

run ait fulfill "$LATER" --force -m later-fulfilled
check_rc "an existing future offer can be fulfilled" 0
drain "$RECEIVER" existing-fulfilled
printf 'contract %s fulfil\n' "$LATER" >"$T/actions.expected"
sed -n '/^contract /p' "$EVENTS" >"$T/actions.actual"
check "fulfilling an existing offer emits fulfil without a contract-row edit" \
	"cmp '$T/actions.expected' '$T/actions.actual'"

run ait future amend "$LATER" -m reopen
check_rc "the fulfilled offer can be reopened" 0
drain "$RECEIVER" reopened
printf 'contract %s amend\n' "$LATER" >"$T/actions.expected"
sed -n '/^contract /p' "$EVENTS" >"$T/actions.actual"
check "reopening emits amend without a duplicate edit or old fulfilment" \
	"cmp '$T/actions.expected' '$T/actions.actual'"

done_testing
