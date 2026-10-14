#!/bin/bash
# checkin via git fetch .: checked-out refusal (HEAD, rebase), ff check, rewind gap, dry-run
D=$(mktemp -d "$1/ck.XXXX"); cd "$D"; git init -q r; cd r; git config user.name t; git config user.email t@t
echo a > a.c; git add a.c; git commit -qm code
mk(){ local I=$D/idx.$RANDOM; GIT_INDEX_FILE=$I git read-tree ${1:-$(git mktree </dev/null)}; b=$(printf "$3" | git hash-object -w --stdin); GIT_INDEX_FILE=$I git update-index --add --cacheinfo 100644,$b,$2; t=$(GIT_INDEX_FILE=$I git write-tree); rm -f "$I"; if [ -n "$1" ]; then git commit-tree $t -p $1 -m ck; else git commit-tree $t -m root; fi; }
R=$(mk "" K1.yaml 'rev: r0\n'); git update-ref refs/heads/ait/x $R
F="git fetch -q --no-write-fetch-head --no-recurse-submodules ."
N=$(mk $R K2.yaml 'rev: r1\n'); $F $N:refs/heads/ait/x; echo "plain ff exit=$? tip=$(git rev-parse --short ait/x)"
git worktree add -q ../w ait/x
N2=$(mk $N K3.yaml 'x\n'); $F --dry-run $N2:refs/heads/ait/x 2>&1 | tail -1; echo "dry-run checked-out exit=${PIPESTATUS[0]}"
$F $N2:refs/heads/ait/x 2>&1 | tail -1; echo "checked-out exit=${PIPESTATUS[0]}"
git worktree remove --force ../w
git worktree add -q --detach ../w2 ait/x; cd ../w2; git checkout -q ait/x; git -c core.editor=true rebase -q --exec false HEAD~1 2>/dev/null; cd ../r
$F $N2:refs/heads/ait/x 2>&1 | tail -1; echo "rebasing worktree exit=${PIPESTATUS[0]}"
git -C ../w2 rebase --abort; git worktree remove --force ../w2
S1=$(mk $N K4.yaml 'a\n'); git update-ref refs/heads/ait/x $S1   # someone else moved tip
$F $N2:refs/heads/ait/x 2>&1 | tail -1; echo "non-ff exit=${PIPESTATUS[0]}"
git update-ref refs/heads/ait/x $R    # rewind to ancestor of N
$F $N2:refs/heads/ait/x; echo "rewind gap exit=$? (0 = accepted)"
echo "reflog: $(git reflog -1 --format=%gs ait/x)"
