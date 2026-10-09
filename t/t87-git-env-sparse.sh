#!/bin/sh
# 5.12: ait works on the repository git finds from GIT_DIR and GIT_WORK_TREE; 5.5-4: a sparse checkout stays sparse
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent a)
prompt_defs
git add -A && tick && git commit -q --no-verify -m defs

# a worktree inside this one that GIT_DIR and GIT_WORK_TREE give to another repository
git init -q --bare "$T/b.git"
mkdir "$T/r/inner" && echo i > "$T/r/inner/f"
export GIT_DIR="$T/b.git" GIT_WORK_TREE="$T/r/inner"
git add f && tick && git commit -q -m inner-base
(cd "$T/r/inner" && tick && ait init >/dev/null 2>"$T/err"; echo $? > "$T/rc")
check "init runs on the repository the environment names" sh -c "[ \$(cat '$T/rc') = 0 ] && git cat-file -e HEAD:.ait/init"
unset GIT_DIR GIT_WORK_TREE
check "and leaves the outer one alone" sh -c "[ \$(git log -1 --format=%s) = defs ]"

# a sparse checkout without .ait/: the records go to the index, the work tree keeps out of .ait/
git sparse-checkout set --no-cone '/src/' '/specs/' '/tests/'
tick; expect_ok "a request in a sparse checkout" ait future offer src/stock.c specs/p.yaml -m o
O=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
check "the record is in the index, marked skip-worktree" sh -c "git ls-files -t -- .ait/$O | grep -qx 'S .ait/$O'"
check "and not in the work tree" [ ! -e .ait/$O ]
check "and nothing shows as changed" [ -z "$(git status --porcelain --untracked-files=no)" ]
done_testing
