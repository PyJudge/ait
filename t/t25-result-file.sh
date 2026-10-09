#!/bin/sh
# T25 결과 파일 (8.4)
. "$(dirname "$0")/lib.sh"

# the preparation (each row of the table starts from it): sets O, Q, W
prep() {
	setup
	A=$(ait session open --agent agent-a)
	prompt_defs; exec_def pass
	tick; run ait future offer src/stock.c specs/p.yaml,specs/pass.yaml as o -m m
	O=$(new_id)
	sed -i.bak '1s/.*/s1 z/' src/stock.c && rm -f src/stock.c.bak; git add src/stock.c
	run ait fulfill "$O" -m m
	Q=$(out_value check-request); W=$(out_value check-result)
}
# write W by hand: wres INPUT TASK=OUTCOME...
wres() {
	in=$1; shift
	{ [ "$in" = - ] || echo "input: $in"; echo 'results:'
	  for a in "$@"; do printf '  - task: %s\n    outcome: %s\n    basis: t\n    reason:\n' "${a%=*}" "${a##*=}"; done; } > "$W"
}
inval() { sed -n 's/^input: "\{0,1\}\([^"]*\)"\{0,1\}$/\1/p' "$W"; }
bad_input=0123456789012345678901234567890123456789
# fulfill with W and expect an error, unchanged
reject() { desc=$1; expect_unchanged expect_err "$desc" "$2" ait fulfill "$O" -m m -r "$W"; }

prep
check "prepare: the tasks are O:specs/p.yaml and O:specs/pass.yaml" test "$(grep '^task ' "$T/out")" = "$(printf 'task %s:specs/p.yaml prompt\ntask %s:specs/pass.yaml exec' "$O" "$O")"
fill "$W" pass
tick; expect_ok "all pass: ok" ait fulfill "$O" -m m -r "$W"

prep; fill "$W" pass "$O:specs/p.yaml=unknown"; sed -i.bak 's/^    reason: t$/    reason:/' "$W" && rm -f "$W.bak"
reject "one unknown without reason: E_CHECK_FAILED" E_CHECK_FAILED
check "unknown: stderr names the task" err_has "$O:specs/p.yaml"
check "unknown: hint check it where its environment is available" err_has "check it where its environment is available (see '$Q') and rerun with -r '$W'"
check "unknown: hint if it is trivial, add --force" err_has "if it is trivial, add --force (recorded as forced)"
check "unknown: no reopen hint" sh -c "! grep -qF 'ait future amend' '$T/err'"

prep; wres "$bad_input" "$O:specs/p.yaml=pass" "$O:specs/pass.yaml=pass"
reject "changed input: E_NEEDS_RESULTS" E_NEEDS_RESULTS

prep; in=$(inval); wres "$bad_input" "$O:specs/pass.yaml=pass"
reject "changed input and a task removed: E_NEEDS_RESULTS" E_NEEDS_RESULTS

prep; wres - "$O:specs/p.yaml=pass" "$O:specs/pass.yaml=pass"
reject "no input line: E_FORMAT" E_FORMAT

prep; in=$(inval); wres "$in" "$O:specs/pass.yaml=pass" "$O:specs/p.yaml=pass"
tick; expect_ok "tasks in another order, all pass: ok" ait fulfill "$O" -m m -r "$W"

prep; fill "$W" pass; printf '    note: x\n' >> "$W"
reject "unknown key note in results: E_FORMAT" E_FORMAT

prep; fill "$W" pass
awk '/^    basis: t$/ && !d {print "    basis: &a t"; d=1; next} {print}' "$W" > "$T/w.n" && mv "$T/w.n" "$W"
reject "an anchor (outside the YAML subset): E_FORMAT" E_FORMAT

done_testing
