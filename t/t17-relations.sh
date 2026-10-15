#!/bin/sh
# spec 1.7, 1.8, 4.10, 5.4, 6.7.2: the same live relationships in every view.
. "$(dirname "$0")/lib.sh"
setup

state_is() {
	grep -A3 "^id $1$" "$T/out" | grep -qx "state $2"
}

run ait future demand src/order.c src/stock.c:1-2 cond.yaml -m partial-demand
D=$(newest D)
run ait future demand src/order.c src/stock.c:3-4 cond.yaml -m whole-demand
E=$(newest D)
run ait future offer src/order.c:1-2 cond.yaml as o undertake -p "$D" o undertake "$E" o -m offer
O=$(newest O)
ait fulfill "$O" --force -m fulfil-offer >/dev/null 2>&1
ait fulfill "$D" --force -m fulfil-demand >/dev/null 2>&1

# Removing a whole condition reopens O before deciding whether D must cover O.
run ait release "$E" "$O" amend "$D" - - - --force -m release-and-fulfil
check_rc "a pending whole release includes the reopened partial offer in demand coverage" 0
check "the release request fulfils the demand and its reopened partial offer" \
	"has out '^fulfilled $D$' && has out '^fulfilled $O$' && ! has out '^fulfilled $E$'"
run ait status src/
check "the demand and partial offer are current after the release" \
	"state_is $D current && state_is $O current"

# A pending cancellation removes the dependency while coverage is computed.
run ait future demand src/order.c src/stock.c:7-8 cond.yaml -m pending-demand
P=$(newest D)
run ait future demand src/order.c src/stock.c:9-10 cond.yaml -m pending-whole
Q=$(newest D)
run ait future offer src/order.c:7-8 cond.yaml as o undertake -p "$P" o undertake "$Q" o -m pending-offer
R=$(newest O)
ait fulfill "$R" --force -m fulfil-offer >/dev/null 2>&1
ait fulfill "$P" --force -m fulfil-demand >/dev/null 2>&1
sed -i.bak 's/^s9$/changed/' src/stock.c
git add src/stock.c
run ait future cancel "$Q" amend "$P" - - - --force -m cancel-and-fulfil
check_rc "a pending cancellation removes an unverified dependency" 0
check "a now-current partial offer is omitted from the demand coverage" \
	"has out '^fulfilled $P$' && ! has out '^fulfilled $R$' && ! has out '^fulfilled $Q$'"
run ait status src/
check "cancelling the dependency restores current for its live dependents" \
	"state_is $Q cancelled && state_is $R current && state_is $P current"

# A newly made whole demand is visible while judging an existing partial offer.
run ait future offer src/order.c:11-12 cond.yaml -m existing-offer
N=$(newest O)
ait fulfill "$N" --force -m fulfil-existing >/dev/null 2>&1
run ait demand src/order.c src/stock.c:11-12 cond.yaml as d \
	demand src/order.c src/stock.c:13-14 cond.yaml as e \
	undertake -p d "$N" undertake e "$N" -m new-dependency
check_rc "a request with new dependencies asks for results" 1
check "new demands and the reopened partial offer are in the covered set" \
	"has out '^task +1:1$' && has out '^task +2:1$' && has out '^task $N:1$'"

run ait future demand src/order.c src/stock.c:15-16 cond.yaml -m newly-partial-demand
ND=$(newest D)
run ait future offer src/order.c:15-16 cond.yaml as p undertake -p "$ND" p \
	amend "$ND" - - - --force -m newly-partial-offer
check_rc "a demand can be fulfilled with an offer made partially responsible in the same request" 0
NP=$(newest O)
check "the newly made partial offer is covered together with its demand" \
	"has out '^fulfilled $ND$' && has out '^fulfilled $NP$'"
run ait status src/
check "both contracts made current by the new partial relationship are visible" \
	"state_is $ND current && state_is $NP current"

# Code comparisons follow every leaning edge; partial offers do not lean on D.
run ait future demand src/order.c src/stock.c:17-18 cond.yaml -m directional-partial
CD=$(newest D)
run ait future demand src/order.c src/stock.c:19-20 cond.yaml -m directional-whole
CE=$(newest D)
run ait future offer src/order.c:17-18 cond.yaml as o undertake -p "$CD" o undertake "$CE" o -m directional-offer
CO=$(newest O)
ait fulfill "$CO" --force -m fulfil-offer >/dev/null 2>&1
ait fulfill "$CD" --force -m fulfil-demand >/dev/null 2>&1
sed -i.bak 's/^s19$/dependency-code/' src/stock.c
git add src/stock.c
run ait status src/
check "a whole dependency with changed code makes every leaning contract unverified" \
	"state_is $CE 'current ?unverified' && state_is $CO 'current ?unverified' && state_is $CD 'current ?unverified'"
run ait future amend "$CE" src/order.c:19-20 - - -m valid-proof-with-changed-code
check_rc "pair invariants require valid fulfilments even when code is unverified" 0
run ait fulfill "$CE" --force -m restore-dependency
check_rc "refiling a dependency restores current without refiling its dependents" 0
run ait status src/
check "the original valid fulfilments become current again when the dependency does" \
	"state_is $CE current && state_is $CO current && state_is $CD current"
sed -i.bak 's/^s17$/partial-demand-code/' src/stock.c
git add src/stock.c
run ait status src/
check "a partial offer stays current when its demand code changes" \
	"state_is $CD 'current ?unverified' && state_is $CO current && state_is $CE current"
run ait fulfill "$CO" --force -m whole-coverage
check "an offer covers its whole demands and leaves partial demands unfulfilled" \
	"has out '^fulfilled $CO$' && has out '^fulfilled $CE$' && ! has out '^fulfilled $CD$'"
run ait status src/
check "refiling the partial offer does not refile its demand" "state_is $CD 'current ?unverified'"
run ait fulfill "$CD" --force -m partial-coverage
check "a demand skips its already current partial offer" \
	"has out '^fulfilled $CD$' && ! has out '^fulfilled $CO$' && ! has out '^fulfilled $CE$'"

# Reopening follows the same directed pairs, while simultaneous reopenings are valid.
tip=$(git rev-parse ait/master)
run ait future amend "$CE" -m reopen-whole-dependency
check_rc "a whole demand cannot reopen under a valid offer fulfilment" 1
check "a refused whole reopening preserves the tip and names the leaning offer" \
	"test \"\$(git rev-parse ait/master)\" = $tip && has err \"would leave '$CO' fulfilled on top of an unfulfilled '$CE'\""
run ait future amend "$CE" future amend "$CO" future amend "$CD" -m reopen-chain
check_rc "all fulfilled contracts along the leaning chain may reopen in one request" 0
run ait status src/
check "every reopened contract is future" \
	"state_is $CE future && state_is $CO future && state_is $CD future"
run ait fulfill "$CO" --force -m whole-refill
check "whole coverage refills the offer and its whole demand only" \
	"has out '^fulfilled $CO$' && has out '^fulfilled $CE$' && ! has out '^fulfilled $CD$'"
run ait fulfill "$CD" --force -m demand-refill
check "the demand refills independently when its partial offer is current" \
	"has out '^fulfilled $CD$' && ! has out '^fulfilled $CO$'"
run ait amend "$CE" - src/stock.c:19-21 - --force -m change-and-fulfil-dependency
check_rc "a changed dependency fulfilled by the same request satisfies its existing leaning pair" 0
check "changing and fulfilling the dependency preserves the existing offer fulfilment" \
	"has out '^fulfilled $CE$' && ! has out '^fulfilled $CO$'"
run ait future amend "$CO" -m reopen-partial-dependency
check_rc "a partial offer cannot reopen under a valid demand fulfilment" 1
check "the partial refusal identifies the opposite leaning direction" \
	"has err \"would leave '$CD' fulfilled on top of an unfulfilled '$CO'\""

# A transient mix between clauses is acceptable when the final graph replaces it.
run ait future demand src/order.c src/stock.c:23-24 cond.yaml -m replace-demand
SD=$(newest D)
run ait future offer src/order.c:23-24 cond.yaml as p undertake -p "$SD" p -m replace-partial
SP=$(newest O)
run ait future offer src/order.c:25-26 cond.yaml -m replace-whole
SW=$(newest O)
run ait undertake "$SD" "$SW" release "$SD" "$SP" -m add-then-release
check_rc "undertake then release accepts a valid final whole graph" 0
check "the old partial edge is removed and the new whole edge remains" \
	"yaml $SP | grep -qx 'undertakes-partially: \[\]' && yaml $SW | grep -qx 'undertakes: \[$SD\]'"
run ait release "$SD" "$SW" undertake -p "$SD" "$SP" -m release-then-add
check_rc "release then undertake accepts a valid final partial graph" 0
check "the old whole edge is removed and the new partial edge remains" \
	"yaml $SW | grep -qx 'undertakes: \[\]' && yaml $SP | grep -qx 'undertakes-partially: \[$SD\]'"

# Coverage takes one step, even when its covered offer leans on another demand.
run ait future demand src/order.c src/stock.c:27-28 cond.yaml -m one-step-demand
HD=$(newest D)
run ait future demand src/order.c src/stock.c:29-30 cond.yaml -m one-step-whole
HE=$(newest D)
run ait future offer src/order.c:27-28 cond.yaml as o undertake -p "$HD" o undertake "$HE" o -m one-step-offer
HO=$(newest O)
tip=$(git rev-parse ait/master)
run ait fulfill "$HD" --force -m one-step
check_rc "one-step demand coverage cannot fulfil its partial offer's whole dependency" 1
check "the uncovered whole dependency is refused before any fulfilment is recorded" \
	"has err \"would leave '$HO' fulfilled on top of an unfulfilled '$HE'\" && ! has out '^fulfilled ' && test \"\$(git rev-parse ait/master)\" = $tip"

# A pair hint must reopen every fulfilled contract leaning on the changed demand.
run ait future demand src/order.c src/stock.c:31-32 cond.yaml -m hint-demand
YD=$(newest D)
run ait future offer src/order.c:31-32 cond.yaml as o undertake "$YD" o -m hint-first
Y1=$(newest O)
run ait future offer src/order.c:33-34 cond.yaml as o undertake "$YD" o -m hint-second
Y2=$(newest O)
ait fulfill "$Y1" --force -m fulfil-first >/dev/null 2>&1
ait fulfill "$Y2" --force -m fulfil-second >/dev/null 2>&1
tip=$(git rev-parse ait/master)
scope_head=$(git rev-parse HEAD)
sed -i.bak 's/^o40$/scope-must-not-commit/' src/order.c
rm -f src/order.c.bak
git add src/order.c
run ait fulfill "$YD=src/stock.c:31-33" --force -m scope-before-fulfil
check_rc "a fulfil scope amend must reopen its fulfilled leaning offers before recording code" 1
check "the scope-amend refusal preserves both tips and staged code, and gives the preparatory amend" \
	"test \"\$(git rev-parse ait/master)\" = $tip && test \"\$(git rev-parse HEAD)\" = $scope_head && ! git diff --cached --quiet && has err 'hint: ait future amend $YD - src/stock.c:31-33 -' && has err 'future amend $Y1' && has err 'future amend $Y2'"
hint=$(sed -n 's/^hint: //p' "$T/err" | head -1)
run sh -c "$hint"
check_rc "the preparatory scope-amend hint can be replayed successfully" 0
run ait status src/
check "the scope-amend hint changes the provider and reopens every leaning offer" \
	"state_is $YD future && state_is $Y1 future && state_is $Y2 future && yaml $YD | grep -q 'provider: \"src/stock.c:31-33 '"
# Restore the fixture so the general multi-pair hint below is independent.
git reset -q --hard "$scope_head"
git update-ref refs/heads/ait/master "$tip"
run ait future amend "$YD" -m reopen-many
check_rc "reopening a demand held by two fulfilled offers is refused" 1
check "the pair hint reopens both leaning offers in one request" \
	"has err 'future amend $Y1' && has err 'future amend $Y2'"
hint=$(sed -n 's/^hint: //p' "$T/err" | head -1)
run sh -c "$hint"
check_rc "the complete pair hint can be replayed successfully" 0
run ait status src/
check "replaying the pair hint reopens the demand and both offers" \
	"state_is $YD future && state_is $Y1 future && state_is $Y2 future"

# A changed old offer may still hold the opposite edge; its hint must release it.
run ait future demand src/order.c src/stock.c:35-36 cond.yaml -m mix-hint-demand
ZD=$(newest D)
run ait future offer src/order.c:35-36 cond.yaml as p undertake -p "$ZD" p -m mix-hint-partial
ZP=$(newest O)
run ait future offer src/order.c:37-38 cond.yaml as w undertake "$ZD" w \
	future amend "$ZP" - - -m mix-changed-holder
check_rc "a harmless amend of the old partial offer does not allow a mixed whole offer" 1
check "the mix hint releases the opposite edge still present in the pending view" \
	"has err 'release $ZD $ZP'"
hint=$(sed -n 's/^hint: //p' "$T/err" | head -1)
run sh -c "$hint"
check_rc "the mix hint with a changed opposite holder can be replayed successfully" 0
ZW=$(newest O)
check "replaying the mix hint leaves the intended whole relationship only" \
	"yaml $ZP | grep -qx 'undertakes-partially: \[\]' && yaml $ZW | grep -qx 'undertakes: \[$ZD\]'"

# A real merge makes a cycle from two individually valid relationship graphs.
run ait future demand src/order.c src/stock.c cond.yaml -m merge-cycle-demand-one
MC_D1=$(newest D)
run ait future demand src/order.c src/stock.c cond.yaml -m merge-cycle-demand-two
MC_D2=$(newest D)
run ait future offer src/stock.c cond.yaml -m merge-cycle-offer-one
MC_O1=$(newest O)
run ait future offer src/stock.c cond.yaml -m merge-cycle-offer-two
MC_O2=$(newest O)
git branch ait/phase4-cycle ait/master
ait checkout ait/master >/dev/null 2>&1
ait undertake "$MC_D1" "$MC_O1" undertake -p "$MC_D2" "$MC_O1" -m merge-cycle-left >/dev/null
ait fulfill "$MC_O1" "$MC_D2" --force -m merge-cycle-left-fulfil >/dev/null 2>&1
ait checkout ait/phase4-cycle >/dev/null 2>&1
ait undertake "$MC_D2" "$MC_O2" undertake -p "$MC_D1" "$MC_O2" -m merge-cycle-right >/dev/null
ait fulfill "$MC_O2" "$MC_D1" --force -m merge-cycle-right-fulfil >/dev/null 2>&1
ait checkout ait/master >/dev/null 2>&1
git switch -q ait/master
# Both sides append valid demand fulfilments. Resolve those attachment-list
# conflicts with ours; each offer and both relationship lists merge unchanged.
run git merge -q -X ours --no-edit ait/phase4-cycle
check_rc "git merges two valid branches into a relationship cycle" 0
check "the cycle came from a commit with two parents" \
	"test \"\$(git rev-list --parents -1 ait/master | wc -w | tr -d ' ')\" = 3"
git branch -d -q ait/phase4-cycle
git switch -q master
run env AIT_SESSION=- ait status src/stock.c
check_rc "status terminates when valid fulfilments form a cycle after git merge" 0
check "every member of the merged cycle is unverified" \
	"state_is $MC_D1 'current ?unverified' && state_is $MC_D2 'current ?unverified' && state_is $MC_O1 'current ?unverified' && state_is $MC_O2 'current ?unverified'"
check "status reports both mixed demands from the real merge" \
	"has err \"warning: '$MC_D1' is already undertaken \" && has err \"warning: '$MC_D2' is already undertaken \""

# Existing violations do not prevent a change to the affected contract.
run ait future amend "$MC_D1" src/order.c:21-22 - - -m preserve-existing-mix
check_rc "a contract with an existing mixed relationship can be amended" 0
run env AIT_SESSION=- ait status src/stock.c
check "an unrelated row change preserves both existing mix warnings" \
	"has err \"warning: '$MC_D1' is already undertaken \" && has err \"warning: '$MC_D2' is already undertaken \""
run ait release "$MC_D2" "$MC_O1" -m repair-one-existing-mix
check_rc "a request may remove one existing mix while retaining another" 0
run env AIT_SESSION=- ait status src/stock.c
check "breaking the cycle restores current while its other mix stays visible" \
	"state_is $MC_D1 current && state_is $MC_D2 current && state_is $MC_O1 current && state_is $MC_O2 current && has err \"warning: '$MC_D1' is already undertaken \" && ! has err \"warning: '$MC_D2' is already undertaken \""
run ait release "$MC_D1" "$MC_O2" -m repair-last-existing-mix
check_rc "the remaining mixed relationship can be repaired" 0
run env AIT_SESSION=- ait status src/stock.c
check "repairing both mixed relationships removes their warnings" \
	"state_is $MC_D1 current && state_is $MC_D2 current && state_is $MC_O1 current && state_is $MC_O2 current && ! has err \"warning: '$MC_D1' is already undertaken \" && ! has err \"warning: '$MC_D2' is already undertaken \""

# This clean merge combines a reopened D with a separately fulfilled whole O.
run ait future demand src/order.c src/stock.c cond.yaml -m merge-pair-demand
MP_D=$(newest D)
run ait future offer src/stock.c cond.yaml -m merge-pair-offer
MP_O=$(newest O)
ait fulfill "$MP_D" --force -m merge-pair-base >/dev/null 2>&1
git branch ait/phase4-pair ait/master
ait future amend "$MP_D" -m merge-pair-left >/dev/null
ait checkout ait/phase4-pair >/dev/null 2>&1
ait undertake "$MP_D" "$MP_O" -m merge-pair-right >/dev/null
ait fulfill "$MP_O" --force -m merge-pair-right-fulfil >/dev/null 2>&1
ait checkout ait/master >/dev/null 2>&1
git switch -q ait/master
run git merge -q --no-edit ait/phase4-pair
check_rc "git cleanly merges a fulfilled offer onto a reopened demand" 0
check "the pair violation came from a commit with two parents" \
	"test \"\$(git rev-list --parents -1 ait/master | wc -w | tr -d ' ')\" = 3"
git branch -d -q ait/phase4-pair
git switch -q master
run env AIT_SESSION=- ait status src/stock.c
check "status reports the existing pair violation and its different verdicts" \
	"state_is $MP_D future && state_is $MP_O 'current ?unverified' && has err \"warning: changing '$MP_D' would leave '$MP_O' fulfilled on top of an unfulfilled '$MP_D'\""
run ait future amend "$MP_D" src/order.c:23-24 - - -m preserve-existing-pair
check_rc "a contract with an existing pair violation can be amended" 0
run env AIT_SESSION=- ait status src/stock.c
check "the permitted row change leaves the existing pair visible" \
	"has err \"warning: changing '$MP_D' would leave '$MP_O' fulfilled on top of an unfulfilled '$MP_D'\""
run ait fulfill "$MP_D" --force -m repair-existing-pair
check_rc "fulfilling the dependency repairs an existing pair violation" 0
check "the repair fulfils only the demand" \
	"has out '^fulfilled $MP_D$' && ! has out '^fulfilled $MP_O$'"
run env AIT_SESSION=- ait status src/stock.c
check "the repaired pair is current and its warning is gone" \
	"state_is $MP_D current && state_is $MP_O current && ! has err \"warning: changing '$MP_D' would leave '$MP_O' fulfilled on top of an unfulfilled '$MP_D'\""

done_testing
