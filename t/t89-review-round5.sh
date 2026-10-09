#!/bin/sh
# Regressions for the fifth review; each case was reproduced, then traced to its cause
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent a)
prompt_defs; exec_def pass
git add -A && tick && git commit -q --no-verify -m defs
H=$(git rev-parse HEAD)
bad() { kind=$1 d=$2; shift 2; tick; expect_unchanged expect_err "$d" "$kind" "$@"; }
def() { printf "$1" > specs/f.yaml; bad E_FORMAT "$2" ait future offer src/order.c specs/f.yaml -m m; rm -f specs/f.yaml; }

# 2.5.1: "" and a block string are scalars, never a list; a CR is refused inside a block string too
def 'body: x\ninputs: ""\n' 'an empty string as inputs'
def 'body: x\nengine: exec\ncommand: [sh]\nsuccess: ""\n' 'an empty string as success'
def 'body: x\ninputs: |\n  a\n' 'a block string as inputs'
def 'body: |\n  x\r\n' 'a CR inside a block string'
# a flow list may run over lines and hold comments
for ok in 'body: x\nengine: exec\ncommand: [./tests/pass.sh,\n  a]\n' 'body: x\nengine: exec\ncommand: [./tests/pass.sh, # first\n  a]\n'; do
	printf "$ok" > specs/f.yaml
	tick; expect_ok "a flow list over lines" ait future offer src/order.c specs/f.yaml -m m
	git reset -q --hard "$H"
done

# 5.4: an Ait-Check that is not exactly "ID reported|forced PINS" does not count
tick; ait offer src/stock.c specs/p.yaml --force -m o >"$T/out"; O=$(new_id)
tick; git commit -q --amend --no-verify -m "$(git log -1 --format=%B | sed 's/ forced / forced  /')"
run ait todo $O
check "a check with two blanks does not count" out_has "unverified yes"
git reset -q --hard "$H"

# 2.2.2: creator is "AGENT SID" with an agent name of 7.2.1, or "unknown -"
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out"; O=$(new_id)
sed -i.bak 's/^creator a /creator a\/b /' .ait/$O && rm .ait/$O.bak && git add .ait && tick && git commit -q --no-verify -m bad
run ait todo
check "a creator that is no agent name is malformed" err_has "warning: skipping malformed '.ait/$O'"
git reset -q --hard "$H"

# H10 is for a promise to fulfil: my future offer, not a demand I made
tick; ait future demand src/order.c src/stock.c specs/p.yaml -m d >"$T/out"; D=$(new_id)
run ait todo $D
check "no fulfil hint for my future demand" sh -c "! grep -q 'ait fulfill $D' '$T/err'"
git reset -q --hard "$H"

# H7 and H8 are judged at HEAD, where the amend runs: code staged only is no code yet
tick; ait offer src/stock.c specs/p.yaml --force -m o >"$T/out"; O=$(new_id)
git rm -q src/stock.c && tick && git commit -q -m gone 2>/dev/null; GONE=$(git rev-parse HEAD)
echo r > README && git add README && tick && git commit -q -m later 2>/dev/null
git checkout -q $GONE^ -- src/stock.c && echo changed >> src/stock.c && git add src/stock.c
run ait todo $O
check "H8 at HEAD, since the commit that made it unverified" err_has "'$O' has no code under its scope (unverified since '$GONE'); if it was moved, run 'ait amend $O <new scope> - -m <why>'"
check "then the reopen hint" err_has "reopen it: 'ait future amend $O -m <why>'"
# 9.1: a commit that changes only prev is not where it became unverified
git reset -q src/stock.c && git checkout -q src/stock.c 2>/dev/null
OLD=$(git rev-parse HEAD:.ait/$O)
sed -i.bak "s/^prev .*/prev $OLD/" .ait/$O && rm .ait/$O.bak && git add .ait/$O && tick && git commit -q --no-verify -m prev-only
run ait todo $O
check "the since commit stays the one that lost the code" err_has "(unverified since '$GONE')"
git reset -q --hard "$H"

# 5.5-5: once update-ref moved the branch, a failure to bring the index is a warning, not an error
racing_git
echo 'touch "$T/r/.git/index.lock"' > "$T/before-update-ref"
tick; run env PATH="$T/bin:$PATH" ait future offer src/stock.c specs/p.yaml -m m
check "the request succeeds" sh -c "[ $rc = 0 ] && grep -q '^request R' '$T/out'"
check "with the warning" err_has "but the index and work tree were not updated"
rm -f .git/index.lock; git reset -q --hard "$H"

# 7.3.3: a definition reached through a symlinked directory is not a work tree file
mkdir real && echo 'body: x' > real/s.yaml && ln -s real link && git add real link && tick && git commit -q -m link 2>/dev/null
bad E_INCOMPLETE "a definition through a symlink" ait future offer src/stock.c link/s.yaml -m m
git reset -q --hard "$H"; rm -rf real link

# 7.1.6: a bare repository that git finds only by its directory still runs pre-push
G=$T/shared-hooks
git config core.hooksPath "$G" && ait init >/dev/null 2>&1 && git config --unset core.hooksPath
git clone -q --bare "$T/r" "$T/b.git"
git -C "$T/b.git" config core.hooksPath "$G"
git init -q --bare "$T/dst.git"
run git -c safe.bareRepository=explicit --git-dir="$T/b.git" push -q "$T/dst.git" master
check "pre-push runs under safe.bareRepository=explicit" [ "$rc" = 0 ]
done_testing
