#!/bin/sh
# spec 2.4, 4.8, 6.5.4, 6.7.5: transitions and shared refusal words at the CLI.
. "$(dirname "$0")/lib.sh"
setup

stage=$(git rev-parse --git-dir)/ait/index
run ait future demand src/order.c src/stock.c cond.yaml -m demand
D=$(newest D)
run ait future offer src/order.c cond.yaml as o undertake "$D" o -m offer
O=$(newest O)
ait fulfill "$O" --force -m current >/dev/null 2>&1

# At step zero, fulfilment of X is planned; an existing Y -> X remains valid.
run ait amend "$D" - src/stock.c:1-3 - --force -m amend-and-fulfil
check_rc "amending and fulfilling a dependency passes step-zero pair checks" 0
check "only the specified dependency is fulfilled again" "has out '^fulfilled $D$' && ! has out '^fulfilled $O$'"
run ait status src/
check "the fulfilled dependency supports the existing offer" \
	"grep -A3 '^id $D$' $T/out | grep -qx 'state current' && grep -A3 '^id $O$' $T/out | grep -qx 'state current'"

run ait future demand src/order.c src/stock.c cond.yaml -m draft
F=$(newest D)
ait contract checkout "$F" >/dev/null
P=.ait/$F.yaml
cp "$P" "$T/original"
tip=$(git rev-parse ait/master)

refused_draft() {
	ait contract add "$P" >/dev/null
	cp "$stage" "$T/staged"
	run ait contract commit -m "$1"
	check_rc "$1 is refused by the shared transition check" 1
	check "$1 preserves the branch and stage" "test \"\$(git rev-parse ait/master)\" = $tip && cmp -s $stage $T/staged"
	rm -f "$stage"
}

sed 's/^creator: alice/creator: bob/' "$T/original" >"$P"
refused_draft 'changed creator'
check "creator identity has the specified E_STATE wording" "has err \"not allowed: '$F' changes its group, kind or creator\""

mkdir -p .ait/moved
P=.ait/moved/$F.yaml
cp "$T/original" "$P"
refused_draft 'changed group'
check "group identity uses the same E_STATE wording" "has err \"not allowed: '$F' changes its group, kind or creator\""

P=.ait/$F.yaml
sed 's/^state: future/state: withdrawn/' "$T/original" >"$P"
refused_draft 'direct withdrawal'
check "direct withdrawal names both states" "has err \"not allowed: '$F' goes from 'future' to 'withdrawn'\""

cp "$T/original" "$P"
printf -- '- "fulfill/%s-R111111111111.md"\n' "$F" >>"$P"
refused_draft 'forged fulfilment'
check "forged fulfilments have their specified E_STATE wording" "has err \"fulfilments are recorded only by 'ait fulfill'\""

blob=$(git rev-parse HEAD:src/order.c)
printf '0 0000000000000000000000000000000000000000\tsrc/order.c\n100644 %s 1\tsrc/order.c\n100644 %s 2\tsrc/order.c\n100644 %s 3\tsrc/order.c\n' "$blob" "$blob" "$blob" | git update-index --index-info
run ait fulfill "$F" --force -m unmerged
check_rc "unmerged code uses E_STATE" 1
check "unmerged code has the specified shared wording" "has err '^error: fulfilling is not possible because you have unmerged files$'"
git reset -q --hard HEAD

done_testing
