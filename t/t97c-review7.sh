#!/bin/sh
# Regressions for the seventh review, grouped by cause; each case was reproduced first
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent a)
prompt_defs; exec_def pass
git add -A && tick && git commit -q --no-verify -m defs
H=$(git rev-parse HEAD)
bad() { kind=$1 d=$2; shift 2; tick; expect_unchanged expect_err "$d" "$kind" "$@"; }

# 7.3.3: the index holds HEAD's version or the work tree's, mode and blob, absence being a version
chmod +x specs/p.yaml && git add specs/p.yaml && echo 'body: new' > specs/p.yaml
bad E_INCOMPLETE "a staged mode with an unstaged edit" ait future offer src/stock.c specs/p.yaml -m o
git reset -q --hard "$H"
git rm -q --cached specs/p.yaml && echo 'body: new' > specs/p.yaml
bad E_INCOMPLETE "a staged deletion with the file in the work tree" ait future offer src/stock.c specs/p.yaml -m o
git reset -q --hard "$H"

# 5.8: git failing while it stages is E_GIT, not a mismatch of the spec files
echo 'specs/p.yaml filter=broken' > .gitattributes
git config filter.broken.clean 'exit 42' && git config filter.broken.required true
echo 'body: new' > specs/p.yaml
bad E_GIT "a required clean filter that fails" ait future offer src/stock.c specs/p.yaml -m o
check "names the git command" err_has "git failed: 'git update-index'"
git config --unset filter.broken.clean; git config --unset filter.broken.required; rm -f .gitattributes; git reset -q --hard "$H"

# 5.13-1 and 7.7.1: fulfill without an ID is a grammar error before the session
run env AIT_SESSION=- ait fulfill -m m
check "E_USAGE before the session" sh -c "[ $rc = 129 ] && grep -q 'nothing to fulfill' '$T/err'"

# 2.5.1: where a tab may stand is YAML's
printf 'body: "x\ty"\n' > specs/t.yaml
tick; expect_ok "a tab inside a quoted scalar" ait future offer src/stock.c specs/t.yaml -m o
git reset -q --hard "$H"; rm -f specs/t.yaml

# 5.8 E_FORMAT names the file and the line
printf 'body: x\nowner: me\n' > specs/f.yaml
bad E_FORMAT "an unknown key" ait future offer src/stock.c specs/f.yaml -m o
check "with its line" err_has "unknown key 'owner' in line 2"
rm -f specs/f.yaml

# 5.8 E_FORMAT of a result file always carries its hint
tick; run ait offer src/stock.c specs/pass.yaml -m o
printf 'input: [\n' > "$T/r.yaml"
bad E_FORMAT "a result file that is no YAML" ait offer src/stock.c specs/pass.yaml -m o -r "$T/r.yaml"
check "with the result file's hint" err_has "fix '$T/r.yaml': every task needs an outcome"
done_testing
