#!/bin/sh
# The input formats of the spec (records), each with what it accepts and what it refuses; a refused request changes nothing
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent a)
prompt_defs; exec_def pass
git add -A && tick && git commit -q --no-verify -m defs
H=$(git rev-parse HEAD)
bad() { kind=$1 d=$2; shift 2; tick; expect_unchanged expect_err "$d" "$kind" "$@"; }

# 2.1 and 2.2 records, written by hand past the hooks: todo skips them, a request refuses them
tick; ait offer src/stock.c specs/p.yaml --force -m o >"$T/out"; O=$(new_id)
R=$(git rev-parse HEAD)
rec_bad() {
	git reset -q --hard "$R"
	printf "$1" > "$T/rec"; sh -c "$2" 2>/dev/null; git add -A .ait && tick && git commit -q --no-verify -m bad
	run ait todo
	check "todo skips: $3" err_has "warning: skipping malformed"
	tick; expect_unchanged expect_err "a request refuses: $3" E_FORMAT ait future amend $O -m m
}
for c in "s/^role offer/role demand/" "s/^state current/state bogus/" "s/^creator .*/creator a Sxyz/" "s/^prev .*/prev zz/" \
	"/^creator/d" "s/^provider \(.*\) .*/provider \1/" "s/^file \(.*\) def$/file \1 input/" "s/^file \(.*\) 100644/file \1 100600/" \
	"s/^role offer/role  offer/" "s/\$/ /" "1i\\
owner me" "/^state/{p;}"; do
	rec_bad '' "sed -i.bak '$c' .ait/$O && rm .ait/$O.bak" "$c"
done
rec_bad '' "printf '\\n' >> .ait/$O" "a blank line"
rec_bad '' "printf 'x' >> .ait/$O" "a last line without LF"
rec_bad '' "sed -i.bak 's/\$/\r/' .ait/$O && rm .ait/$O.bak" "a CR"
rec_bad '' "echo 'version 3' > .ait/init" "an init file of another version"
rec_bad '' "echo x > .ait/notes" "a path under .ait/ that is no record"
rec_bad '' "mkdir -p .ait/undertake && printf 'state bogus\nprev -\n' > .ait/undertake/D000000000000-$O" "a link to a demand that is not there"
git reset -q --hard "$R"

# 8.4 result files
tick; ait future offer src/order.c specs/pass.yaml -m o >"$T/out"; P=$(new_id)
echo y >> src/order.c && git add src/order.c
W=$(git rev-parse --git-path ait/check-result.yaml)
ait fulfill $P -m f >/dev/null 2>&1
IN=$(sed -n 's/^input: //p' "$W")
res() { printf "$1" "$IN" > "$T/res"; bad E_FORMAT "$2" ait fulfill $P -m f -r "$T/res"; }
res 'input: %s\nresults:\n  - task: x\n    outcome: pass\n    basis: b\n' "an unknown task"
res "input: %s\nresults:\n  - task: $P:specs/pass.yaml\n    outcome: maybe\n" "an outcome that is not pass, fail or unknown"
res "input: %s\nresults:\n  - task: $P:specs/pass.yaml\n    outcome: pass\n" "a pass without a basis"
res "input: %s\nresults:\n  - task: $P:specs/pass.yaml\n    outcome: pass\n    basis: b\n  - task: $P:specs/pass.yaml\n    outcome: pass\n    basis: b\n" "a task twice"
res 'input: %s\nresults: []\n' "a task left out"
res "input: %s\nresults:\n  - task: $P:specs/pass.yaml\n    outcome: pass\n    basis: b\n    note: n\n" "an unknown key"
res "input: %s\nresults: {a: b}\n" "results that are a map"
bad E_FORMAT "results without an input" sh -c "printf 'results: []\n' > '$T/res'; ait fulfill $P -m f -r '$T/res'"
printf "input: %s\nresults:\n  - task: $P:specs/pass.yaml\n    outcome: pass\n    basis: b\n" "$IN" > "$T/res"
tick; expect_ok "a well-formed result" ait fulfill $P -m f -r "$T/res"

# 7.2.1 agent names
for name in '' unknown 'a b' a/b "$(printf 'a%.0s' $(seq 1 65))" "$(printf 'caf\303\251')"; do
	run ait session open --agent "$name"
	check "an agent name '$name'" [ "$rc" = 129 ]
done
done_testing
