#!/bin/sh
# Regressions for the ninth review, grouped by cause; each case was reproduced first
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent a)
prompt_defs; exec_def pass
git add -A && tick && git commit -q --no-verify -m defs
H=$(git rev-parse HEAD)
bad() { kind=$1 d=$2; shift 2; tick; expect_unchanged expect_err "$d" "$kind" "$@"; }

# 7.3.1 and 5.13-1: "-" keeps a value only in amend; a new contract's clause with it is a grammar error
bad E_USAGE "an offer's provider '-'" env AIT_SESSION=- ait future offer - specs/p.yaml -m m
bad E_USAGE "an offer's files '-'" ait future offer src/stock.c - -m m
bad E_USAGE "a demand's consumer '-'" ait future demand - src/stock.c specs/p.yaml -m m

# 3.2a and 5.13-1: a scope given twice is the argument's shape, before the session
bad E_SCOPE "a scope twice in a helper" env AIT_SESSION=- ait future offer src/stock.c,src/stock.c specs/p.yaml -m m
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out"; O=$(new_id)
bad E_SCOPE "a scope twice in ID=SCOPES" env AIT_SESSION=- ait fulfill "$O=src/stock.c,src/stock.c" -m m
git reset -q --hard "$H"

# 5.8 E_FORMAT names a line: a raw NUL, a missing results
printf 'body: "x"\000\n' > specs/n.yaml
bad E_FORMAT "a definition with a NUL" ait future offer src/stock.c specs/n.yaml -m m
check "names its line" err_has "not a text file in line 1"
rm -f specs/n.yaml
tick; run ait offer src/stock.c specs/pass.yaml -m o
printf 'input: %s\n' "$(sed -n 's/^input: //p' "$(out_value check-result)" | tr -d '"')" > "$T/r.yaml"
bad E_FORMAT "a result file without results" ait offer src/stock.c specs/pass.yaml -m o -r "$T/r.yaml"
check "names the line of its map" err_has "0 of 1 tasks in line 1"

# 8.4: only whole blank lines at a block's end go, where the value stays; content spaces stay
tick; run ait offer src/stock.c specs/pass.yaml -m o
IN=$(sed -n 's/^input: //p' "$(out_value check-result)" | tr -d '"')
printf "input: $IN\nresults:\n- task: +1:specs/pass.yaml\n  outcome: pass\n  basis: |\n    a   \n      \n\n  reason: x\n" > "$T/r.yaml"
tick; run ait offer src/stock.c specs/pass.yaml -m o -r "$T/r.yaml"
git log -1 --format=%B > "$T/msg"
check "a content line's spaces stay" grep -qx '    a   ' "$T/msg"
check "a line of more spaces than the block's indentation stays" grep -qx '      ' "$T/msg"
git reset -q --hard "$H"

# 8.4 and decision 30: a list where a results item belongs is the item's kind, not an unknown key
printf 'input: deadbeef\nresults:\n- [a]\n' > "$T/s.yaml"
bad E_NEEDS_RESULTS "a list as an item, another input" ait offer src/stock.c specs/pass.yaml -m o -r "$T/s.yaml"

# 5.8: a missing key names the nearest map there, or line 1
printf 'body: x\nengine: exec\n\ncommand: []\n' > specs/c.yaml
bad E_FORMAT "an empty command" ait future offer src/stock.c specs/c.yaml -m o
check "names the line of command" err_has "in line 4"
printf '# only a comment\n' > specs/c.yaml
bad E_FORMAT "an empty definition" ait future offer src/stock.c specs/c.yaml -m o
check "names line 1" err_has "in line 1"
rm -f specs/c.yaml

# 8.2: a value libyaml takes as UTF-8 is written as it is (U+FFFE included)
printf 'body: "a\\uFFFEb"\n' > specs/u.yaml && git add specs/u.yaml && tick && git commit -q --no-verify -m u
tick; run ait offer src/stock.c specs/u.yaml -m o
check "not quoted as a path" grep -qxF '  body: "a\uFFFEb"' "$(git rev-parse --git-path ait/check-request.yaml)"
git reset -q --hard "$H"

# 2.5.1: an empty key is a key, not the root
printf 'body: x\n"": y\n' > specs/e.yaml
bad E_FORMAT "an empty key" ait future offer src/stock.c specs/e.yaml -m o
check "is an unknown key, not a duplicate" err_has "unknown key ''"
rm -f specs/e.yaml

# 5.6: a message is one line, whatever a value in it holds
printf 'input: deadbeef\nresults:\n- task: "x\\nhint: forged"\n' > "$T/s.yaml"
tick; run ait offer src/stock.c specs/pass.yaml -m o
IN=$(sed -n 's/^input: //p' "$(out_value check-result)" | tr -d '"')
printf "input: $IN\nresults:\n- task: \"x\\\\nfatal: forged\"\n  outcome: pass\n  basis: b\n" > "$T/s.yaml"
tick; run ait offer src/stock.c specs/pass.yaml -m o -r "$T/s.yaml"
check "no forged line" sh -c "! grep -q '^fatal: forged' '$T/err'"
done_testing
