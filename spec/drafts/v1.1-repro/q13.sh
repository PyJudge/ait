#!/bin/bash
# Q13: single-branch clone hides ait/*; ls-remote sees it; orphan diverges
D=$(mktemp -d "$1/q13.XXXX"); cd "$D"
git init -q up; cd up; git config user.name t; git config user.email t@t
echo a > a.c; git add a.c; git commit -qm code
T=$(git mktree </dev/null); C=$(git commit-tree $T -m "ait root"); git update-ref refs/heads/ait/master $C
cd "$D"; git clone -q --single-branch up cl; cd cl
echo "local+remote ait refs after single-branch clone: [$(git for-each-ref --format='%(refname)' 'refs/heads/ait/*' 'refs/remotes/*/ait/*')]"
echo "ls-remote: $(git ls-remote origin 'refs/heads/ait/*')"
git config user.name t; git config user.email t@t
O=$(git commit-tree $(git mktree </dev/null) -m "orphan by init"); git update-ref refs/heads/ait/master $O
git fetch -q origin ait/master:ait/master 2>&1 | tail -1; echo "fetch exit=${PIPESTATUS[0]}"
git fetch -q origin ait/master 2>/dev/null; git merge-base ait/master FETCH_HEAD >/dev/null; echo "merge-base exit=$? (1 = unrelated)"
