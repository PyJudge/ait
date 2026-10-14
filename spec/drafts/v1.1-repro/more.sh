#!/bin/bash
D=$(mktemp -d "$1/more.XXXX"); cd "$D"; git init -q r; cd r; git config user.name t; git config user.email t@t
mkdir -p src/api tests; echo 'int n=50;' > src/api/a.c; echo 'test' > tests/t.sh; git add .; git commit -qm 1
echo "== dry-run + update-ref closes the rewind gap"
R=$(git commit-tree $(git mktree </dev/null) -m root); git update-ref refs/heads/ait/x $R
N=$(git commit-tree $(git mktree </dev/null) -p $R -m n); git update-ref refs/heads/ait/x $N $R
N2=$(git commit-tree $(git mktree </dev/null) -p $N -m n2)
git update-ref refs/heads/ait/x $R                      # rewound by someone
git fetch -q --dry-run --no-write-fetch-head . $N2:refs/heads/ait/x; echo "dry-run exit=$? (0: not checked out)"
git update-ref refs/heads/ait/x $N2 $N 2>&1 | tail -1; echo "update-ref CAS exit=${PIPESTATUS[0]} (rewind caught)"
echo "== Q11: ignored file inside provider"
echo 'src/api/gen.h' > .gitignore; git add .gitignore; git commit -qm ign; echo '#define N 500' > src/api/gen.h
git update-index -q --refresh; git diff-files --quiet -- src/api; echo "diff-files exit=$?"
echo "others: [$(git ls-files --others --exclude-standard -- src/api)]  others+ignored: [$(git ls-files --others --ignored --exclude-standard -- src/api)]"
echo "== assume-unchanged hides a work-tree change"
git update-index --assume-unchanged src/api/a.c; echo 'int n=999;' > src/api/a.c
git update-index -q --refresh; git diff-files --quiet -- src/api/a.c; echo "diff-files exit=$? ; ls-files -v: $(git ls-files -v -- src/api/a.c)"
git update-index --no-assume-unchanged src/api/a.c; git checkout -q -- src/api/a.c
echo "== temp-index commit keeps another session's staging out, hooks run"
mkdir -p .git/hooks; printf '#!/bin/sh\necho "pre-commit saw index: $GIT_INDEX_FILE" >&2\n' > .git/hooks/pre-commit; chmod +x .git/hooks/pre-commit
echo 'int n=40;' > src/api/a.c; git add src/api/a.c; RT=$(git write-tree)      # tested tree
echo 'other' > tests/t.sh; git add tests/t.sh                                   # other session stages after
TMP=$D/tmpidx; GIT_INDEX_FILE=$TMP git read-tree $RT; GIT_INDEX_FILE=$TMP git commit -q -m fulfil
echo "HEAD tree == tested: $([ "$(git rev-parse HEAD^{tree})" = "$RT" ] && echo yes)"; echo "real index status: $(git status --short | tr '\n' ' ')"
echo "== 9.20: worktreeConfig on bare+worktree"
cd "$D"; git clone -q --bare r b.git; git -C b.git worktree add -q ../bw1 master; git -C b.git config extensions.worktreeConfig true
git -C bw1 status --short 2>&1 | tail -1; echo "exit=${PIPESTATUS[0]}"
