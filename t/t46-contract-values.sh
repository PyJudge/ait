#!/bin/sh
# spec 2.3.3, 4.4, 4.10: fixed input, owned candidates and typed fulfilment view.
. "$(dirname "$0")/lib.sh"
setup
run ait future demand src/order.c src/stock.c cond.yaml -m values
D=$(newest D)
set -- -std=gnu99 -O1 -Wall -Werror -Wdeclaration-after-statement -I"$AIT_DIR" -I"$AIT_DIR/libyaml" -finstrument-functions
case $(uname -s) in Linux) set -- "$@" -DHAVE_GETRANDOM ;; Darwin) set -- "$@" -DHAVE_ARC4RANDOM -DPRECOMPOSE_UNICODE ;; esac
compiler=${CC:-cc}
"$compiler" "$@" -Dmain=ait_command_main -c "$AIT_DIR/ait.c" -o "$T/ait.o" || exit 1
for source in "$AIT_DIR"/*.c; do
	case $source in */ait.c|*/request.c) continue ;; esac
	set -- "$@" "$source"
done
set -- "$@" "$AIT_DIR/t/helper-contract-values.c" "$T/ait.o" "$AIT_DIR"/libyaml/*.o
case $(uname -s) in Darwin) set -- "$@" -liconv ;; esac
"$compiler" "$@" -o "$T/contract-values" || exit 1
run "$T/contract-values" copy
check_rc 'internal copy and equality avoid YAML IO' 0
run "$T/contract-values" sets
check_rc 'relation equality preserves canonical set semantics' 0
run "$T/contract-values" specs
check_rc 'two candidates own their specs independently of fixed input' 0
run ait future offer src/order.c cond.yaml as holder undertake "$D" holder -m whole
O=$(newest O)
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
