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

# decision 5: a new scope whose file came in another case is refused, at the helper and at fulfill
if [ "$(git config --bool core.ignorecase)" = true ]; then
	echo x > Foo.c && git add Foo.c
	bad E_SCOPE "a new scope with a twin in the index" ait future offer new:foo.c specs/p.yaml -m o
	git reset -q --hard "$H"; rm -f Foo.c
	tick; ait future offer new:foo.c specs/p.yaml -m o >"$T/out"; O=$(new_id)
	echo x > Foo.c && git add Foo.c && tick && git commit -q --no-verify -m code
	bad E_SCOPE "fulfil a new scope that came in another case" ait fulfill $O --force -m f
	check "names git's spelling" err_has "give it as git has it: 'Foo.c'"
	git reset -q --hard "$H"; rm -f Foo.c
fi

# rule 3: a spec file where git has a code file as its directory would delete that file in a contract commit
echo code > specs/y && git add specs/y && tick && git commit -q --no-verify -m y
rm specs/y && mkdir specs/y && echo 'body: y' > specs/y/z.yaml
bad E_INCOMPLETE "a spec file under a code file" ait future offer src/stock.c specs/y/z.yaml -m o
check "the hint commits the removal with ait first" err_has "remove 'specs/y' and run 'ait commit -m <why>' first"
git reset -q --hard "$H"; rm -rf specs/y

# 7.1.1: a user's hook in the work tree that git does not ignore is to be committed with ait
git config core.hooksPath .githooks && mkdir -p .githooks && printf '#!/bin/sh\nexit 0\n' > .githooks/pre-commit && chmod +x .githooks/pre-commit
run ait init
# 7.1.1-1: PATH is the file's absolute path, as in the error line
check "init names how to commit the hook" err_has "then commit it: stage '$(pwd -P)/.githooks/pre-commit' and run 'ait commit -m <why>'"
printf '#!/bin/sh\nexit 0\n' > .githooks/pre-push && chmod +x .githooks/pre-push
run ait init
check "the pre-push lines are the three of 7.1.1" sh -c "grep -qxF 'hint: if command -v ait >/dev/null 2>&1; then ait hook pre-push \"\$@\" < \"\$t\" || { s=\$?; rm -f \"\$t\"; exit \$s; }; fi' '$T/err'"
git config --unset core.hooksPath; rm -rf .githooks
done_testing
