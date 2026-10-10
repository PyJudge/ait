#!/bin/sh
# Regressions for the tenth review; each case was reproduced first
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent a)
prompt_defs; exec_def pass
git add -A && tick && git commit -q --no-verify -m defs
H=$(git rev-parse HEAD)
bad() { kind=$1 d=$2; shift 2; tick; expect_unchanged expect_err "$d" "$kind" "$@"; }

# 8.2 and decision 27: a path libyaml's reader would refuse (a surrogate) is quoted as git shows it, so the result reads back
f=$(printf 'src/\355\240\200.c'); B=$(echo x | git hash-object -w --stdin)
git update-index --add --cacheinfo 100644,$B,"$f" && tick && git commit -q --no-verify -m sur
tick; run ait offer "$f" specs/pass.yaml -m o
if command -v ruby >/dev/null; then
	check "the check request reads as YAML" ruby -ryaml -e 'YAML.load_file(ARGV[0])' "$(git rev-parse --git-path ait/check-request.yaml)"
fi
check "the path is quoted as git shows it" grep -qF 'provider: "\"src/\\355\\240\\200.c\""' "$(git rev-parse --git-path ait/check-request.yaml)"
git reset -q --hard "$H"

# 8.4: a block's blank lines go one at a time while its value stays
tick; run ait offer src/stock.c specs/pass.yaml -m o
IN=$(sed -n 's/^input: //p' "$(out_value check-result)" | tr -d '"')
printf "input: $IN\nresults:\n- task: +1:specs/pass.yaml\n  outcome: pass\n  basis: |\n    a   \n      \n\n\n  reason: x\n" > "$T/r.yaml"
tick; run ait offer src/stock.c specs/pass.yaml -m o -r "$T/r.yaml"
check "the blank lines after a line of content spaces go" sh -c "git log -1 --format=%B | grep -A1 -x '      ' | sed -n 2p | grep -qx '  reason: x'"
git reset -q --hard "$H"

# 2.5.1: a key is a plain or quoted scalar
printf '? |-\n  body\n: hello\n' > specs/k.yaml
bad E_FORMAT "a block as a key" ait future offer src/stock.c specs/k.yaml -m o
rm -f specs/k.yaml
done_testing
