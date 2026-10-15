#!/bin/sh
# spec 2.4, 6.5.4: staged drafts form one unordered candidate, not command clauses.
. "$(dirname "$0")/lib.sh"
setup
stage=$(git rev-parse --git-dir)/ait/index

current_demand() {
	run ait future demand src/order.c "$2" cond.yaml -g "$1" -m 'current batch target'
	D=$(newest D)
	ait fulfill "$D" --force -m current >/dev/null 2>&1
}
drop_draft() {
	if [ "$1" = new ]; then
		run ait contract new drop "$2"
		XP=$(sed -n 's/^path //p' "$T/out")
		X=$(basename "$XP" .yaml)
		sed -i.bak "s/<ID>/$3/" "$XP"
	else
		current_demand good src/order.c
		run ait future drop "$D" -g "$2" -m 'existing drop to retarget'
		X=$(newest X)
		ait contract checkout "$X" >/dev/null
		XP=.ait/$2/$X.yaml
		sed -i.bak "s/^target: .*/target: $3/" "$XP"
	fi
}

# Either group order must reject a target ended or reopened by another draft.
for kind in new retarget; do
	for effect in cancel reopen; do
		for order in drop-first target-first; do
			if [ "$order" = drop-first ]; then tg=z; xg=a; else tg=a; xg=z; fi
			current_demand "$tg" src/stock.c
			TARGET=$D
			drop_draft "$kind" "$xg" "$TARGET"
			ait contract checkout "$TARGET" >/dev/null
			DP=.ait/$tg/$TARGET.yaml
			if [ "$effect" = cancel ]; then
				sed -i.bak 's/^state: future/state: cancelled/' "$DP"
			else
				sed -i.bak 's#provider: "src/stock.c"#provider: "src/stock.c:1-2"#' "$DP"
			fi
			ait contract add "$XP" "$DP" >/dev/null
			tip=$(git rev-parse ait/master)
			cp "$stage" "$T/stage"; cp "$XP" "$T/drop"; cp "$DP" "$T/target"
			run ait contract commit -m "$kind $effect $order"
			check_rc "$kind drop checks the $effect target in the complete batch ($order)" 1
			check "the $kind $effect $order refusal names its final target" "has err \"'$X' targets '$TARGET', which is not current\""
			check "the $kind $effect $order refusal preserves branch, drafts, and stage" \
				"test \"\$(git rev-parse ait/master)\" = $tip && cmp -s $stage $T/stage && cmp -s $XP $T/drop && cmp -s $DP $T/target"
			rm -f "$stage" "$XP" "$XP.bak" "$DP" "$DP.bak"
		done
	done
done

# Removing an unverified partial dependency makes D current in the complete batch.
for kind in new retarget; do
	for order in drop-first dependency-first; do
		if [ "$order" = drop-first ]; then og=z; xg=a; else og=a; xg=z; fi
		current_demand target src/order.c
		TARGET=$D
		run ait future offer src/stock.c cond.yaml -g "$og" -m 'batch partial dependency'
		O=$(newest O)
		ait fulfill "$O" --force -m 'current partial offer' >/dev/null 2>&1
		ait undertake -p "$TARGET" "$O" -m 'current dependent pair' >/dev/null
		drop_draft "$kind" "$xg" "$TARGET"
		ait contract checkout "$O" >/dev/null
		OP=.ait/$og/$O.yaml
		sed -i.bak 's/^state: future/state: cancelled/' "$OP"
		echo changed >> src/stock.c
		git add src/stock.c
		ait contract add "$XP" "$OP" >/dev/null
		run ait contract commit -m "$kind final current $order"
		check_rc "$kind drop sees the dependency removed by the complete batch ($order)" 0
		check "the $kind $order batch stores the drop and cancels its old dependency" \
			"yaml $X | grep -qx 'target: '$TARGET && yaml $O | grep -qx 'state: cancelled' && test -f $XP && test -f $OP && ! test -e $stage"
		rm -f "$stage" "$XP" "$XP.bak" "$OP" "$OP.bak"
		git restore --staged src/stock.c
		git restore src/stock.c
	done
done

# ID-keyed pending storage must not overwrite a second staged group silently.
run ait contract new demand dup-a
DP=$(sed -n 's/^path //p' "$T/out")
DUP=$(basename "$DP" .yaml)
sed -i.bak 's#consumer: "<scope>"#consumer: "src/order.c"#; s#provider: "<scope>"#provider: "src/stock.c"#' "$DP"
mkdir -p .ait/dup-z
DUPP=.ait/dup-z/$DUP.yaml
cp "$DP" "$DUPP"
ait contract add "$DUPP" "$DP" >/dev/null
tip=$(git rev-parse ait/master)
cp "$stage" "$T/stage"; cp "$DP" "$T/duplicate"
run ait contract commit -m 'duplicate staged identity'
check_rc "two staged groups with one new ID are a format error (2.1.3)" 128
check "the duplicate-ID refusal names both paths" "has err \"bad format: 'dup-z/$DUP.yaml': '$DUP' is also at 'dup-a/$DUP.yaml'\""
check "duplicate-ID refusal preserves the branch, both drafts, and stage" \
	"test \"\$(git rev-parse ait/master)\" = $tip && cmp -s $stage $T/stage && cmp -s $DP $T/duplicate && cmp -s $DUPP $T/duplicate"
rm -f "$stage" "$DP" "$DP.bak" "$DUPP"

# A single existing contract placed under another group remains an identity refusal.
run ait future demand src/order.c src/order.c cond.yaml -g original -m 'existing future identity'
D=$(newest D)
ait contract checkout "$D" >/dev/null
mkdir -p .ait/moved
DP=.ait/moved/$D.yaml
cp ".ait/original/$D.yaml" "$DP"
ait contract add "$DP" >/dev/null
run ait contract commit -m 'move existing identity'
check_rc "a single moved existing draft still gets the transition identity refusal" 1
check "the moved-draft refusal names its immutable identity" "has err \"'$D' changes its group, kind or creator\""
rm -f "$stage" "$DP"

# --force applies to the entire draft's confirmation conditions, including whole undertakings.
for dependent in no yes; do
	run ait future demand src/order.c src/stock.c cond.yaml -g force-end -m 'whole endpoint'
	E=$(newest D)
	run ait future offer src/stock.c cond.yaml as o undertake "$E" o -g force-offer -m 'whole offer'
	O=$(newest O)
	ait fulfill "$O" --force -m 'current whole offer' >/dev/null 2>&1
	if [ "$dependent" = yes ]; then
		current_demand force-demand src/order.c
		ait undertake -p "$D" "$O" -m 'dependent demand' >/dev/null
	fi
	ait contract checkout "$O" >/dev/null
	OP=.ait/force-offer/$O.yaml
	sed -i.bak 's/^undertakes: .*/undertakes: []/' "$OP"
	before_condition=$(yaml "$O" | sed -n 's/^condition-id: //p')
	ait contract add "$OP" >/dev/null
	run ait contract commit --force -m "trivial whole change dependent=$dependent"
	check_rc "a forced whole-undertaking draft remains trivial (dependent=$dependent)" 0
	check "the forced whole-undertaking draft retains its condition ID (dependent=$dependent)" \
		"test \"\$(yaml $O | sed -n 's/^condition-id: //p')\" = $before_condition && yaml $O | grep -qx 'undertakes: \[\]'"
	run ait status src/
	check "the forced draft keeps the current offer current (dependent=$dependent)" "grep -A3 '^id $O$' $T/out | grep -qx 'state current'"
	rm -f "$stage" "$OP" "$OP.bak"
done

done_testing
