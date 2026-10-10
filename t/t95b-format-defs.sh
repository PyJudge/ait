#!/bin/sh
# The input formats of the spec (defs), each with what it accepts and what it refuses; a refused request changes nothing
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent a)
prompt_defs; exec_def pass
git add -A && tick && git commit -q --no-verify -m defs
H=$(git rev-parse HEAD)
bad() { kind=$1 d=$2; shift 2; tick; expect_unchanged expect_err "$d" "$kind" "$@"; }

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
# YAML 1.2: a flow map may be the whole document, and keys may be quoted scalars
for ok in '{"body":"x"}\n' '{body: x, inputs: []}\n' '# a note\n{"body": "x"}\n' "'body': x\n" '"body": x\ninputs: [tests/pass.sh]\n' \
	'{"body": x, '"'"'inputs'"'"': [tests/pass.sh]}\n' 'body: see http://x.y/z\n'; do
	printf "$ok" > specs/f.yaml
	tick; expect_ok "standard YAML: $(printf "$ok" | tr '\n' ' ')" ait future offer src/order.c specs/f.yaml -m m
	git reset -q --hard "$H"
done
def '{body: x}\nmore: y\n' "text after a flow map document"
def 'body: x\nengine: exec\ncommand: []\n"command.0": sh\n' "a key holding '.' that would read as a path"
def 'body: x\n inputs: []\n' "entries of one map at two indentations"
def 'body: a: b\n' "': ' inside a plain scalar"
# libyaml reads 'body: "x"#c' as "x" and a comment, though YAML asks for a blank before '#'; ait reads YAML as libyaml does
def 'body: x\nengine: exec\ncommand: [sh,,x]\n' "an empty item in a flow list"
def '"body: x\n' "an unterminated quoted key"
for ok in 'body: |\n  a\n  b\n' "body: 'it''s'\n" 'body: "a\\"b\\\\c"\n' 'body: x # note\n' 'body: x\ninputs: []\n' \
	'body: x\nengine: exec\ncommand:\n  - ./tests/pass.sh\nsuccess: [0, 1]\ninputs: [tests/pass.sh]\n'; do
	printf "$ok" > specs/f.yaml
	tick; expect_ok "a definition $(printf "$ok" | head -1)" ait future offer src/order.c specs/f.yaml -m m
	git reset -q --hard "$H"
done

done_testing
