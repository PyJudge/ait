#!/bin/sh
# The input formats of the spec, each with what it accepts and what it refuses; a refused request changes nothing
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent a)
prompt_defs; exec_def pass
git add -A && tick && git commit -q --no-verify -m defs
H=$(git rev-parse HEAD)
bad() { kind=$1 d=$2; shift 2; tick; expect_unchanged expect_err "$d" "$kind" "$@"; }

# 3.1 PATH: not empty; no leading / or -; no blank, control character, ':' or ','; no empty, '.' or '..' piece; not .ait
for p in '/src/order.c' 'src//order.c' './src/order.c' 'src/../src/order.c' 'src/order .c' '.ait/x' '.ait' \
	"$(printf 'src/a\001b')" "$(printf 'src/a\177b')" "$(printf 'src/a\tb')"; do
	bad E_SCOPE "a provider '$p'" ait future offer "$p" specs/p.yaml -m m
	bad E_SCOPE "a definition '$p'" ait future offer src/order.c "$p" -m m
done
bad E_USAGE "a scope starting with '-'" ait future offer -x specs/p.yaml -m m
bad E_SCOPE "an empty piece of a scope list" ait future offer src/order.c, specs/p.yaml -m m

# 3.2 scope arguments
bad MINI "a line range" ait future offer src/order.c:1-2 specs/p.yaml -m m
for r in 0-2 3-1 a-b 1-; do bad E_SCOPE "a malformed range ':$r'" ait future offer "src/order.c:$r" specs/p.yaml -m m; done
bad E_SCOPE "new: for a file that exists" ait future offer new:src/order.c specs/p.yaml -m m
bad E_SCOPE "a file that is not there" ait future offer src/none.c specs/p.yaml -m m
bad E_SCOPE "an abstract scope on a current offer" ait offer src/ specs/p.yaml --force -m m
bad E_SCOPE "a directory without '/'" ait future offer src specs/p.yaml -m m
for ok in new:src/new.c src/ / src/order.c,src/stock.c; do
	tick; expect_ok "a future provider '$ok'" ait future offer "$ok" specs/p.yaml -m m
	git reset -q --hard "$H"
done

# 1.9 aliases and IDs
for a in Bad a-b 1a demand "$(printf 'a%.0s' $(seq 1 33))"; do
	bad E_USAGE "an alias '$a'" ait future offer src/order.c specs/p.yaml as "$a" -m m
done
bad E_USAGE "an alias used twice" ait future offer src/order.c specs/p.yaml as x future offer src/stock.c specs/p.yaml as x -m m
bad E_ID "an ID that does not exist" ait future amend O000000000000 -m m
bad E_ID "an alias from another command" ait future amend x -m m
bad E_USAGE "a clause that does not exist" ait future offer src/order.c specs/p.yaml bogus -m m

# 5.3 messages
bad E_USAGE "a blank message" ait future offer src/order.c specs/p.yaml -m '   '
bad E_READ "a message file that is not there" ait future offer src/order.c specs/p.yaml -F "$T/none"
printf 'a\000b\n' > "$T/nul"
bad E_FORMAT "a message file with a NUL" ait future offer src/order.c specs/p.yaml -F "$T/nul"

# 2.5 definitions
def() { printf "$1" > specs/f.yaml; bad E_FORMAT "$2" ait future offer src/order.c specs/f.yaml -m m; rm -f specs/f.yaml; }
def 'body: x\n\tinputs: []\n' "a tab"
def 'body: x\r\n' "a CR"
def 'body: x\nbody: y\n' "a duplicate key"
def 'body: x\nowner: me\n' "an unknown key"
def 'body: &a x\n' "an anchor"
def 'body: *a\n' "an alias"
def 'body: !t x\n' "a tag"
def 'body: >\n  x\n' "a folded block"
def 'body: x\n---\nbody: y\n' "a second document"
def "body: 'x\n" "an unterminated quote"
def 'body: "a\\qb"\n' "an escape other than \\\" and \\\\"
def 'body: \377\n' "a byte that is not UTF-8"
def 'body: x\nengine: exec\ncommand: {0: sh}\n' "a map as the command"
def 'body: x\nengine: exec\ncommand: sh\n' "a scalar as the command"
def 'body: x\nengine: exec\ncommand: [sh]\nsuccess: [zero]\n' "a success that is not an integer"
def 'body: [a]\n' "a list as the body"
def 'engine: exec\ncommand: [sh]\n' "no body"
def 'body:\n' "an empty body"
def 'body: x\nengine: shell\n' "an engine other than exec"
def 'body: x\nengine: exec\n' "exec without a command"
def 'body: x\ninputs: tests/pass.sh\n' "inputs that are not a list"
def 'body: x\ninputs: [../x]\n' "an input that is not a path"
def '' "an empty definition"
printf 'body: x\nenvironment: [a]\n' > specs/f.yaml
bad MINI "environment" ait future offer src/order.c specs/f.yaml -m m
rm -f specs/f.yaml
for ok in 'body: |\n  a\n  b\n' "body: 'it''s'\n" 'body: "a\\"b\\\\c"\n' 'body: x # note\n' 'body: x\ninputs: []\n' \
	'body: x\nengine: exec\ncommand:\n  - ./tests/pass.sh\nsuccess: [0, 1]\ninputs: [tests/pass.sh]\n'; do
	printf "$ok" > specs/f.yaml
	tick; expect_ok "a definition $(printf "$ok" | head -1)" ait future offer src/order.c specs/f.yaml -m m
	git reset -q --hard "$H"
done

# 2.1 and 2.2 records, written by hand past the hooks: todo skips them, a request refuses them
tick; ait offer src/stock.c specs/p.yaml --force -m o >"$T/out"; O=$(new_id)
R=$(git rev-parse HEAD)
rec_bad() {
	git reset -q --hard "$R"
	printf "$1" > "$T/rec"; sh -c "$2" 2>/dev/null; git add -A .ait && tick && git commit -q --no-verify -m bad
	run ait todo
	check "todo skips: $3" err_has "warning: skipping malformed"
	tick; expect_unchanged expect_err "a request refuses: $3" E_FORMAT ait future cancel $O -m m
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
rec_bad '' "echo 'version 2' > .ait/init" "an init file of another version"
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
