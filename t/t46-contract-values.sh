#!/bin/sh
# spec 2.3.3, 4.4, 4.10: IDs from the final difference from T, the final view judged.
. "$(dirname "$0")/lib.sh"
setup
run ait future demand src/order.c src/stock.c cond.yaml -m values
D=$(newest D)
run ait future offer src/order.c cond.yaml as holder undertake "$D" holder -m whole
O=$(newest O)
tip=$(git rev-parse ait/master)
run ait undertake "$D" "$O" -m again
check_rc 'undertaking what is already undertaken is a noop (6.4.1)' 0
check 'the same set keeps the contract and the tip' "test \"\$(git rev-parse ait/master)\" = $tip"
run ait fulfill "$O" --force -m verified
check_rc 'a whole undertaking fulfils both contracts' 0
run ait amend "$D" --force -m 'reopen and fulfil in one request'
check_rc 'new typed D fulfilment satisfies the final pair invariant' 0
tip=$(git rev-parse ait/master)
run ait future amend "$D" -m 'reopen only'
check_rc 'a final unfulfilled demand under a fulfilled whole offer is refused' 1
check 'pair refusal preserves the published tip' "test \"\$(git rev-parse ait/master)\" = $tip && has err \"'$O' fulfilled on top of an unfulfilled '$D'\""
run ait future amend "$O" future amend "$D" -m 'reopen both'
check_rc 'the final view permits both contracts to reopen together' 0
run ait future amend "$O" src/order.c cond.yaml future amend "$O" src/order.c cond.yaml -m 'same condition twice'
check_rc 'ordinary repeated edits preserve candidate ownership' 0
run ait future cancel "$O" -m end
check_rc 'cancel the holder' 0
tip=$(git rev-parse ait/master)
run ait future cancel "$O" -m 'cancel again'
check_rc 'repeated cancellation remains a noop' 0
check 'noop preserves the tip' "test \"\$(git rev-parse ait/master)\" = $tip"
done_testing
