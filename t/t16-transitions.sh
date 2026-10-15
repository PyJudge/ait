#!/bin/sh
# spec 2.4, 6.4.1, 6.5.4: validate each action against its immediate predecessor.
. "$(dirname "$0")/lib.sh"
setup

stage=$(git rev-parse --git-dir)/ait/index
new_draft() {
	run ait contract new "$1"
	P=$(sed -n 's/^path //p' "$T/out")
	K=$(basename "$P" .yaml)
	sed -i.bak 's#consumer: "<scope>"#consumer: "src/order.c"#; s#provider: "<scope>"#provider: "src/stock.c"#' "$P"
}
unchanged_refusal() {
	check "$1 leaves the branch, draft, and contract stage unchanged" \
		"test \"\$(git rev-parse ait/master)\" = $tip && cmp -s $P $T/draft && cmp -s $stage $T/stage"
	rm -f "$stage" "$P" "$P.bak"
}

# A new draft must take absent -> future; accepting it by replacing its state is data loss.
for kind in demand offer; do
	for state in cancelled withdrawn; do
		new_draft "$kind"
		sed -i.bak "s/^state: future/state: $state/" "$P"
		ait contract add "$P" >/dev/null
		tip=$(git rev-parse ait/master)
		cp "$P" "$T/draft"; cp "$stage" "$T/stage"
		run ait contract commit -m "invalid new $kind $state"
		check_rc "a new $state $kind draft is refused (2.4)" 1
		check "the new $state $kind refusal names its transition" "has err \"not allowed: '$K' goes from 'absent' to '$state'\""
		unchanged_refusal "a new $state $kind refusal"
	done
done

run ait future demand src/order.c src/stock.c cond.yaml -m target
TARGET=$(newest D)
ait fulfill "$TARGET" --force -m current >/dev/null 2>&1
run ait future demand src/order.c src/stock.c cond.yaml -m future
FUTURE=$(newest D)

new_draft drop
sed -i.bak "s/<ID>/$TARGET/; s/^state: future/state: cancelled/" "$P"
ait contract add "$P" >/dev/null
tip=$(git rev-parse ait/master)
cp "$P" "$T/draft"; cp "$stage" "$T/stage"
run ait contract commit -m 'invalid new cancelled drop'
check_rc "a new cancelled drop draft is refused (2.4)" 1
unchanged_refusal 'a new cancelled drop refusal'

# Final validity cannot make an earlier clause valid retroactively.
tip=$(git rev-parse ait/master)
run ait future drop "$FUTURE" as x future amend x "$TARGET" -m 'invalid creation restored'
check_rc "a drop clause checks its target before a later retarget" 1
check "the invalid creation names the non-current target and preserves the branch" \
	"has err \"targets '$FUTURE', which is not current\" && test \"\$(git rev-parse ait/master)\" = $tip"
run ait future drop "$TARGET" -m drop
DROP=$(newest X)
tip=$(git rev-parse ait/master)
run ait future amend "$DROP" "$FUTURE" future amend "$DROP" "$TARGET" -m 'invalid retarget restored'
check_rc "an amend clause checks its target before a later restoration" 1
check "the invalid retarget preserves the branch" "has err \"targets '$FUTURE', which is not current\" && test \"\$(git rev-parse ait/master)\" = $tip"

tip=$(git rev-parse ait/master)
run ait future amend "$FUTURE" - src/stock.c:1-2 - future amend "$FUTURE" - src/stock.c - --force -m 'provider restored'
check_rc "--force checks each provider change before its restoration" 1
check "the provider refusal preserves the branch and supplies the retry without --force" \
	"has err \"cannot change the provider of '$FUTURE' with --force\" && has err '^hint: ait future amend ' && ! has err '^hint: .*--force' && test \"\$(git rev-parse ait/master)\" = $tip"

# Conditions from prior clauses must already affect the next clause's current judgment.
cp "$T/prompt.yaml" changed-cond.yaml
tip=$(git rev-parse ait/master)
run ait future amend "$TARGET" - - changed-cond.yaml future drop "$TARGET" -m 'reopened before drop'
check_rc "a changed condition is visible to the following drop clause" 1
check "the newly unfulfilled target preserves the branch" "has err \"targets '$TARGET', which is not current\" && test \"\$(git rev-parse ait/master)\" = $tip"
run ait future drop "$TARGET" as x future amend "$TARGET" -m 'drop before reopen'
check_rc "a valid drop birth is not judged again after its target is reopened" 0
ait fulfill "$TARGET" --force -m current >/dev/null 2>&1

tip=$(git rev-parse ait/master)
run ait future amend "$FUTURE" - src/stock.c:3-4 - amend "$TARGET" - - - --force -m 'mixed force'
check_rc "a future --force amend stays trivial beside a fulfilling clause" 1
check "the mixed force refusal happens before any code or contract commit" "has err \"cannot change the provider of '$FUTURE' with --force\" && test \"\$(git rev-parse ait/master)\" = $tip"

run ait future offer src/stock.c cond.yaml -m 'offer for ordered conditions'
ORDERED=$(newest O)
ait fulfill "$ORDERED" --force -m current >/dev/null 2>&1
before_condition=$(yaml "$ORDERED" | sed -n 's/^condition-id: //p')
run ait future amend "$ORDERED" - changed-cond.yaml undertake "$FUTURE" "$ORDERED" --force -m 'trivial then whole'
check_rc "a whole undertaking after a trivial amend updates the condition normally (C68)" 0
check "the whole undertaking no longer counts the offer's old fulfillment" "yaml $ORDERED | grep -q \"undertakes: \[$FUTURE\]\" && test \"\$(yaml $ORDERED | sed -n 's/^condition-id: //p')\" != $before_condition"

run ait future offer src/stock.c cond.yaml -m 'trivial with temporary undertaking'
TEMPORARY=$(newest O)
ait fulfill "$TEMPORARY" --force -m current >/dev/null 2>&1
before_condition=$(yaml "$TEMPORARY" | sed -n 's/^condition-id: //p')
run ait future amend "$TEMPORARY" - changed-cond.yaml undertake "$FUTURE" "$TEMPORARY" release "$FUTURE" "$TEMPORARY" --force -m 'trivial after whole undo'
check_rc "temporary whole changes do not make the remaining trivial spec change ordinary" 0
check "a whole add then release leaves only the trivial specs and retains condition-id" "test \"\$(yaml $TEMPORARY | sed -n 's/^condition-id: //p')\" = $before_condition && yaml $TEMPORARY | grep -qx 'undertakes: \[\]'"
run ait future drop "$TEMPORARY" -m 'drop after trivial remaining'
check_rc "a later drop sees the retained trivial fulfillment" 0

# A target's current judgment sees relations changed by earlier clauses, too.
run ait future demand src/order.c src/order.c cond.yaml -m 'dependent target'
DEPENDENT=$(newest D)
run ait future offer src/stock.c cond.yaml as p undertake -p "$DEPENDENT" p -m 'partial dependency'
PARTIAL=$(newest O)
ait fulfill "$DEPENDENT" --force -m 'current pair' >/dev/null 2>&1
echo changed >> src/stock.c
git add src/stock.c
run ait future cancel "$PARTIAL" future drop "$DEPENDENT" -m 'cancel dependency before drop'
check_rc "a drop sees a partial dependency cancelled by the preceding clause" 0
check "the drop names the still-current demand" "yaml \$(newest X) | grep -qx 'target: '$DEPENDENT"
git restore --staged src/stock.c
git restore src/stock.c

# Drop withdrawal is part of the candidate's final state even at step zero.
run ait future demand src/order.c src/stock.c cond.yaml as d future offer src/stock.c cond.yaml as o undertake d o -m 'retiring whole pair'
RETIRED=$(newest D)
WHOLE=$(newest O)
ait fulfill "$WHOLE" --force -m 'current whole pair' >/dev/null 2>&1
run ait future offer src/stock.c cond.yaml -m 'other current offer'
OTHER=$(newest O)
ait fulfill "$OTHER" --force -m current >/dev/null 2>&1
run ait future drop "$RETIRED" -m 'planned drop'
RETIRE=$(newest X)
run ait undertake -p "$RETIRED" "$OTHER" amend "$RETIRE" "$RETIRED" -m 'withdraw mixed target'
check_rc "a drop's planned withdrawal removes a mix before step-zero invariants" 0
check "the final drop and target states agree" "has out '^fulfilled '$RETIRE && has out '^withdrawn '$RETIRED && yaml $RETIRED | grep -qx 'state: withdrawn'"

# Explicit =SCOPES retains the prescribed preflight refusal for fulfilled dependents.
run ait future demand src/order.c src/stock.c cond.yaml as d future offer src/stock.c cond.yaml as o undertake d o -m 'narrowing whole pair'
NARROW=$(newest D)
DEPENDENT_OFFER=$(newest O)
ait fulfill "$DEPENDENT_OFFER" --force -m 'current narrowing pair' >/dev/null 2>&1
tip=$(git rev-parse ait/master)
code=$(git rev-parse HEAD)
run ait fulfill "$NARROW=src/stock.c:1-2" --force -m 'explicit narrowing'
check_rc "explicit =SCOPES still refuses stranding a fulfilled whole offer (6.7.5-0)" 1
check "explicit narrowing is refused before either commit" "has err \"would leave '$DEPENDENT_OFFER' fulfilled\" && test \"\$(git rev-parse ait/master)\" = $tip && test \"\$(git rev-parse HEAD)\" = $code"
run ait amend "$NARROW" - src/stock.c:1-2 - --force -m 'ordinary narrowing'
check_rc "a fulfilling amend is checked with its planned fulfillment" 0
check "the ordinary amend records the changed provider and fulfillment" "has out '^fulfilled '$NARROW && yaml $NARROW | grep -q 'provider: \"src/stock.c:1-2 '"

# An explicit rescope is an amend even when it names the same provider.
run ait future demand src/order.c src/stock.c cond.yaml -m 'identical rescope'
RESCOPE=$(newest D)
ait fulfill "$RESCOPE" --force -m current >/dev/null 2>&1
before_condition=$(yaml "$RESCOPE" | sed -n 's/^condition-id: //p')
before_change=$(yaml "$RESCOPE" | sed -n 's/^change-id: //p')
run ait fulfill "$RESCOPE=src/stock.c" --force -m 'same scopes'
check_rc "an identical explicit rescope is fulfilled" 0
check "identical =SCOPES updates both IDs before recording (6.7.1)" "test \"\$(yaml $RESCOPE | sed -n 's/^condition-id: //p')\" != $before_condition && test \"\$(yaml $RESCOPE | sed -n 's/^change-id: //p')\" != $before_change"
run ait future drop "$RESCOPE" -m 'keep target'
KEEP=$(newest X)
tip=$(git rev-parse ait/master)
run ait future amend "$KEEP" - -m 'keep target unchanged'
check_rc "a future drop amend accepts '-' as keep-target (6.4)" 0
check "the keep-target future amend leaves its contract and tip unchanged" "test \"\$(git rev-parse ait/master)\" = $tip && yaml $KEEP | grep -qx 'target: '$RESCOPE"
run ait amend "$KEEP" - -m 'fulfill kept target'
check_rc "a non-future drop amend with '-' fulfills the existing target" 0
check "fulfilling the kept target withdraws it" "has out '^fulfilled '$KEEP && yaml $RESCOPE | grep -qx 'state: withdrawn'"

# Appending a fulfillment and withdrawing that target in the same request remains valid.
run ait future drop "$NARROW" -m 'overlapping target'
OVERLAP=$(newest X)
run ait fulfill "$NARROW" "$OVERLAP" --force -m 'fulfill then withdraw'
check_rc "a target can be fulfilled and then withdrawn by the same request" 0
check "overlapping fulfill and drop records both before withdrawing" "has out '^fulfilled '$NARROW && has out '^fulfilled '$OVERLAP && has out '^withdrawn '$NARROW && yaml $NARROW | grep -qx 'state: withdrawn'"

# An action on an ended contract is refused even if its resulting bytes would be identical.
run ait future cancel "$FUTURE" future amend "$FUTURE" - - - -m 'cancel then identical amend'
check_rc "cancel followed by an identical amend is refused in clause order" 1
check "the ended amend does not leave the preceding cancel" "has err \"'$FUTURE' has ended\" && yaml $FUTURE | grep -qx 'state: future'"
run ait future demand src/order.c src/stock.c cond.yaml as n future cancel n future cancel n -m 'new then cancelled'
check_rc "a newly created alias may be cancelled and cancellation repeated" 0
CANCELLED=$(newest D)
check "new then cancelled is not mistaken for an invalid new draft" "yaml $CANCELLED | grep -qx 'state: cancelled'"

# An unchanged ended draft is allowed; writing different bytes must leave its input and stage intact.
ait contract checkout "$CANCELLED" >/dev/null
P=.ait/$CANCELLED.yaml
ait contract add "$P" >/dev/null
tip=$(git rev-parse ait/master)
run ait contract commit -m 'identical ended draft'
check_rc "an identical ended draft is a no-op" 0
check "the no-op keeps the tip and clears only the contract stage" "test \"\$(git rev-parse ait/master)\" = $tip && test -f $P && ! test -e $stage"
sed -i.bak 's#consumer: "src/order.c"#consumer: "src/order.c:1-2"#' "$P"
ait contract add "$P" >/dev/null
cp "$P" "$T/draft"; cp "$stage" "$T/stage"
run ait contract commit -m 'changed ended draft'
check_rc "a changed ended draft is refused" 1
unchanged_refusal 'a changed ended draft refusal'

done_testing
