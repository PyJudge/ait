#!/bin/sh
# spec 1.5, 1.7, 6.3, 9.5: status and notification use the receiving live graph.
. "$(dirname "$0")/lib.sh"
setup

listener=
stop_listener() {
	if [ -n "$listener" ]; then
		kill "$listener" 2>/dev/null
		wait "$listener" 2>/dev/null
		listener=
	fi
}
trap stop_listener 0

# A fresh receiving session starts after each fixture, excluding its setup events.
start_listener() {
	LSID=$(AIT_SESSION=- ait session open --agent bob 2>/dev/null)
	AIT_SESSION=$LSID ait sub "$2" >"$T/$1.sub" 2>"$T/$1.sub.err"
	EVENTS="$T/$1.events"
	AIT_SESSION=$LSID ait listen >"$EVENTS" 2>"$T/$1.listen.err" &
	listener=$!
}

wait_event() {
	for i in $(seq 1 100); do
		grep -q '^ait-event-end$' "$EVENTS" && break
		kill -0 "$listener" 2>/dev/null || break
		sleep 0.05
	done
	stop_listener
}

state_is() {
	grep -A3 "^id $1$" "$T/out" | grep -qx "state $2"
}

# An ended demand is absent from its still-live whole offer's relationships.
run ait future demand src/order.c src/stock.c cond.yaml -m ended-demand
ED=$(newest D)
BOB=$(AIT_SESSION=- ait session open --agent bob 2>/dev/null)
run env AIT_SESSION="$BOB" ait future offer src/order.c cond.yaml as o undertake "$ED" o -m whole-offer
EO=$(newest O)
start_listener ended-demand "$ED"
run ait future cancel "$ED" -m cancel-demand
check_rc "a demand can end while its whole offer remains a promise" 0
wait_event
check "the demand cancellation reaches its explicit ID subscriber" \
	"grep -qx 'reason sub id $ED' $EVENTS && grep -qx 'contract $ED cancelled' $EVENTS && grep -qx ait-event-end $EVENTS"
check "an ended demand does not notify its old whole offer as a counterpart" \
	"! grep -qx 'reason future offer $EO' $EVENTS"
run env AIT_SESSION="$BOB" ait status src/
check "status retains the ended demand and live offer states" "state_is $ED cancelled && state_is $EO future"
check "status omits the ended demand from its offer's undertakings" "! has out '^undertakes $ED '"
check "status omits the offer from the ended demand's holders" "! has out '^undertaken-by $EO '"

# The reverse direction uses a partial undertaking and a still-live demand.
run env AIT_SESSION="$BOB" ait future demand src/order.c src/stock.c cond.yaml -m live-demand
LD=$(newest D)
run ait future offer src/order.c cond.yaml as o undertake -p "$LD" o -m ended-partial-offer
LO=$(newest O)
start_listener ended-offer "$LO"
run ait future cancel "$LO" -m cancel-partial-offer
check_rc "a partial offer can end while its demand remains a promise" 0
wait_event
check "the offer cancellation reaches its explicit ID subscriber" \
	"grep -qx 'reason sub id $LO' $EVENTS && grep -qx 'contract $LO cancelled' $EVENTS && grep -qx ait-event-end $EVENTS"
check "an ended partial offer does not notify its old demand as a counterpart" \
	"! grep -qx 'reason future demand $LD' $EVENTS"
run env AIT_SESSION="$BOB" ait status src/
check "status retains the live demand and ended offer states" "state_is $LD future && state_is $LO cancelled"
check "status omits the ended partial offer from its demand's holders" "! has out '^undertaken-by $LO '"
check "status omits the demand from the ended partial offer's undertakings" "! has out '^undertakes $LD '"

# Promise eligibility is evaluated at the receiver's HEAD and local tip.
# Bob owns both the changed demand and its whole offer, so the event exercises
# the direct promise reason and the live counterpart reason independently.
run env AIT_SESSION="$BOB" ait future demand src/order.c src/stock.c cond.yaml -m promise-demand
PD=$(newest D)
run env AIT_SESSION="$BOB" ait future offer src/order.c cond.yaml as o undertake "$PD" o -m promise-offer
PO=$(newest O)
start_listener future "$PD"
run ait future amend "$PD" src/order.c:1-2 - - -m edit-future-consumer
check_rc "a future demand's consumer can change without changing its condition" 0
wait_event
check "the future demand edit reaches its explicit subscriber" \
	"grep -qx 'reason sub id $PD' $EVENTS && grep -qx ait-event-end $EVENTS"
check "a future demand receives its own promise reason" "grep -qx 'reason future demand $PD' $EVENTS"
check "a future offer receives its live counterpart's change" "grep -qx 'reason future offer $PO' $EVENTS"

run env AIT_SESSION="$BOB" ait fulfill "$PO" --force -m fulfil-whole-pair
check_rc "fulfilling the whole offer makes both contracts current" 0
run env AIT_SESSION="$BOB" ait status src/
check "the receiving fixture has a current demand and offer" "state_is $PD current && state_is $PO current"
start_listener current "$PD"
run ait future amend "$PD" src/order.c:3-4 - - -m edit-current-consumer
check_rc "a current demand's consumer can change without reopening the pair" 0
wait_event
check "the current demand edit reaches its explicit subscriber" \
	"grep -qx 'reason sub id $PD' $EVENTS && grep -qx ait-event-end $EVENTS"
check "a current demand receives the demand reason" "grep -qx 'reason demand $PD' $EVENTS"
check "a current demand is not classified as a promise" "! grep -qx 'reason future demand $PD' $EVENTS"
check "a current offer is not classified as a promise" "! grep -qx 'reason future offer $PO' $EVENTS"

sed -i.bak 's/^o45$/changed-order/' src/order.c
sed -i.bak 's/^s45$/changed-stock/' src/stock.c
rm -f src/order.c.bak src/stock.c.bak
git add src/order.c src/stock.c
git commit -qm changed-provider-code
run env AIT_SESSION="$BOB" ait status src/
check "changed provider code makes both receiving contracts unverified" \
	"state_is $PD 'current ?unverified' && state_is $PO 'current ?unverified'"
start_listener unverified "$PD"
run ait future amend "$PD" src/order.c:5-6 - - -m edit-unverified-consumer
check_rc "an unverified demand's consumer can change without reopening the pair" 0
wait_event
check "the unverified demand edit reaches its explicit subscriber" \
	"grep -qx 'reason sub id $PD' $EVENTS && grep -qx ait-event-end $EVENTS"
check "an unverified demand receives its own promise reason" "grep -qx 'reason future demand $PD' $EVENTS"
check "an unverified offer receives its live counterpart's change" "grep -qx 'reason future offer $PO' $EVENTS"
check "an unverified demand does not receive the current-demand reason" "! grep -qx 'reason demand $PD' $EVENTS"

# Events and scope subscriptions use the remote event's contract; personal
# relationships must still come exclusively from the receiving local tip.
run env AIT_SESSION="$BOB" ait future demand src/order.c src/stock.c cond.yaml -m upstream-demand
UD=$(newest D)
git push -q origin master ait/master
git branch -q --set-upstream-to=origin/ait/master ait/master
git clone -q "$T/origin.git" "$T/remote"
(
	cd "$T/remote" || exit 1
	ait init >/dev/null &&
	AIT_SESSION=- ait session open --agent carol >/dev/null &&
	cp "$T/r/cond.yaml" cond.yaml &&
	ait future offer src/order.c cond.yaml as o undertake "$UD" o -m upstream-only >"$T/remote-new.out" &&
	ait contract push >/dev/null 2>"$T/remote-push.err"
)
rc=$?
check_rc "an upstream worktree can create an offer for the demand" 0
UO=$(sed -n 's/^new \(O[0-9a-f]*\).*/\1/p' "$T/remote-new.out")
check "the new offer is absent from the receiving local tip" \
	"test -n '$UO' && ! git cat-file -e ait/master:$UO.yaml 2>/dev/null"
start_listener upstream src/order.c
git fetch -q origin
wait_event
check "an explicit scope subscription receives the upstream-only offer" \
	"grep -qx 'reason sub scope src/order.c' $EVENTS && grep -qx 'contract $UO create' $EVENTS && grep -qx ait-event-end $EVENTS"
check "the upstream-only offer is identified as absent from the local tip" "grep -qx 'in-head no' $EVENTS"
check "an upstream-only offer does not create a local counterpart reason" \
	"! grep -qx 'reason future demand $UD' $EVENTS"

done_testing
