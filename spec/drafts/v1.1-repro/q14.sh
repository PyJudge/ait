#!/bin/bash
# Q14: per-worktree file via git-path vs config --worktree
D=$(mktemp -d "$1/q14.XXXX"); cd "$D"
git init -q r; cd r; git config user.name t; git config user.email t@t
echo a > a.c; git add a.c; git commit -qm 1
git worktree add -q ../w2 -b w2
echo "main git-path: $(git rev-parse --git-path ait-branch)"
echo "linked git-path: $(git -C ../w2 rev-parse --git-path ait-branch)"
echo ait/try > "$(git -C ../w2 rev-parse --git-path ait-branch)"
echo "linked reads: $(cat "$(git -C ../w2 rev-parse --git-path ait-branch)")"; echo "main has file: $(test -f "$(git rev-parse --git-path ait-branch)" && echo yes || echo no)"
git worktree remove ../w2 && echo "worktree remove ok (per-worktree dir cleaned: $(test -d .git/worktrees/w2 && echo no || echo yes))"
echo "repo format unchanged: extensions.worktreeConfig=$(git config --get extensions.worktreeConfig || echo unset)"
# bare + worktree case
cd "$D"; git clone -q --bare r b.git; git -C b.git worktree add -q ../bw1 master
echo "bare linked git-path: $(git -C bw1 rev-parse --git-path ait-branch)"; git -C bw1 status --short >/dev/null && echo "bw1 status ok"
