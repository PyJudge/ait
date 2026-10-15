#!/bin/sh
# spec 6.7.1-2, 6.7.5, 7.4: union effects and check-template identity.
. "$(dirname "$0")/lib.sh"
setup
run ait future demand src/order.c src/stock.c cond.yaml -m target
D=$(newest D)
ait fulfill "$D" --force -m current >/dev/null 2>&1
run ait future drop "$D" -m 'first withdrawal'
X=$(newest X)
run ait future drop "$D" -m 'second withdrawal'
Y=$(newest X)
run ait fulfill "$X" "$Y" -m 'both withdrawal records'
check_rc 'two live drops for one current target can be fulfilled together' 0
check 'each selected drop is fulfilled' "has out '^fulfilled $X$' && has out '^fulfilled $Y$'"
check 'the shared target is withdrawn in the resulting contract' "yaml $D | grep -qx 'state: withdrawn'"
check 'each drop has its own fulfillment record' \
	"test \"\$(git ls-tree -r --name-only ait/master fulfill/ | grep -c /$X-)\" = 1 && test \"\$(git ls-tree -r --name-only ait/master fulfill/ | grep -c /$Y-)\" = 1"
# The ended-drop control is independent of the multi-drop result above.
run ait future demand src/order.c src/stock.c cond.yaml -m 'ended control target'
D=$(newest D)
ait fulfill "$D" --force -m current >/dev/null 2>&1
run ait future drop "$D" -m 'ended control drop'
X=$(newest X)
ait fulfill "$X" -m 'first fulfillment' >/dev/null 2>&1
tip=$(git rev-parse ait/master)
run ait fulfill "$X" -m 'already fulfilled'
check_rc 'an already fulfilled drop remains ended' 1
check 'rejected repeat fulfillment preserves its records' "test \"\$(git rev-parse ait/master)\" = $tip"
# YAML mapping order is not part of the condition-ID map or task-list identity.
run ait future demand src/order.c src/stock.c cond.yaml -m 'template demand'
D=$(newest D)
run ait future offer src/stock.c cond.yaml as o undertake "$D" o -m 'template offer'
O=$(newest O)
run ait fulfill "$O" -m 'check template'
check_rc 'a whole offer requests the two covered tasks' 1
res=$(pass_all)
awk '
/^results:/ { results=1; next }
results { r[++nr]=$0; next }
/^condition-ids:/ { next }
{ ids[++ni]=$0 }
END {
 print "results:"
 for (i=1; i<=nr; i++) print r[i]
 print "condition-ids:"
 for (i=ni; i>=1; i--) print ids[i]
}' "$res" > "$T/reordered-map"
cp "$T/reordered-map" "$res"
run ait fulfill "$O" -m 'check template'
check_rc 'the same covered tasks still ask for the filled result file' 1
check 'reordering YAML mappings preserves the filled result file byte-for-byte' \
 "cmp -s $res $T/reordered-map"

# Sequence identity still contains task positions; swapping tasks is a different template.
awk '
/^results:/ { next }
/^condition-ids:/ { condition=1 }
condition { ids=ids $0 "\n"; next }
/^- task:/ { n++ }
{ blocks[n]=blocks[n] $0 "\n" }
END {
 printf "%sresults:\n", ids
 for (i=n; i>=1; i--) printf "%s", blocks[i]
}' "$T/reordered-map" > "$res"
run ait fulfill "$O" -m 'check template'
check_rc 'a reordered task sequence asks for current tasks' 1
check 'changed task order refreshes the result template' \
 "! grep -q 'outcome: pass' $res && test \"\$(sed -n 's/^- task: \"\(.*\)\"/\1/p' $res | head -1)\" = '$D:1'"

done_testing
