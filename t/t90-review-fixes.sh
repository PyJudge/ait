#!/bin/sh
# Regressions for the defects found in the efficiency review (each was reproduced before the fix)
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent a)
prompt_defs
tick; ait offer src/stock.c specs/p.yaml --force -m m >/dev/null

# 6.1 and 7.1.2: a spec file replaced by a directory of the same name is still a contract path
H=$(git rev-parse HEAD)
git rm -q specs/p.yaml && mkdir specs/p.yaml && echo x > specs/p.yaml/x && git add specs/p.yaml/x
tick; expect_err "a spec file turned into a directory is guarded" E_GUARD git commit -qm dir
git reset -q --hard "$H" && rm -rf specs/p.yaml && git checkout -q -- specs/p.yaml

# 2.1.3: a gitlink under .ait/ is a bad record path, not a git failure
git update-index --add --cacheinfo 160000,$(git rev-parse HEAD),.ait/Odeadbeefdead
tick; git commit -q --no-verify -m link
expect_ok "todo with a gitlink under .ait/" ait todo
check "the gitlink is skipped as malformed (5.10)" err_has "warning: skipping malformed '.ait/Odeadbeefdead' at 'HEAD'"
git reset -q --hard HEAD~1

# 2.5.1: blanks after a closing quote are not text
printf 'body: "x" \n' > specs/q.yaml
tick; expect_ok "a quoted scalar may be followed by blanks" ait future offer src/stock.c specs/q.yaml -m m

# 2.5.1: a definition nested deeper than ait reads is a bad format, never a crash
python3 -c "
s='body: x\n'
for i in range(15):
    s+=' '*(2*i)+'k%d:\n'%i
s+=' '*30+'- item: v\n'
open('specs/deep.yaml','w').write(s)"
tick; expect_err "a too deeply nested definition is a bad format" E_FORMAT ait future offer src/stock.c specs/deep.yaml -m m

# 7.8: todo skips a link whose offer does not exist instead of crashing
rm -f specs/q.yaml specs/deep.yaml
tick; expect_ok "a demand to link" ait future demand src/order.c src/stock.c specs/p.yaml as d -m m; D=$(new_id)
check "the demand has an ID" test -n "$D"
printf 'state undertaken\nprev -\n' > .ait/undertake/$D-O000000000000 && git add .ait && tick && git commit -q --no-verify -m dangling
expect_ok "todo with a dangling link" ait todo $D
check "the dangling link is reported as malformed" err_has "warning: skipping malformed '.ait/undertake/$D-O000000000000' at 'HEAD'"
git reset -q --hard HEAD~1

# 7.1.6: only what peels to a commit is validated; a tag of a blob pushes
git tag blobtag $(git rev-parse HEAD:src/stock.c)
expect_ok "a blob tag pushes" git push -q origin refs/tags/blobtag

# 7.1.1: core.hooksPath .git-hooks is inside the worktree and outside .git, so ait does not write it
git config core.hooksPath .git-hooks
expect_err "a repository hook directory next to .git is not written" E_STATE ait init
check "no hook was written there" test ! -e .git-hooks/pre-commit
git config --unset core.hooksPath

# 7.2.3 and 5.7: a session that cannot be removed is not reported closed
B=$(ait session open --agent b 2>/dev/null)
chmod 555 "$(git rev-parse --git-common-dir)/ait/sessions"
expect_err "close fails when the session file stays" E_IO ait session close "$B"
chmod 755 "$(git rev-parse --git-common-dir)/ait/sessions"
done_testing
