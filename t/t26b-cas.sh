#!/bin/sh
# spec 5.5: commit-tree, then update-ref R C H as the compare-and-swap
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent a)
prompt_defs
racing_git

check "no reference-transaction hook is installed (7.1.1)" test ! -e "$(git rev-parse --git-path hooks)/reference-transaction"

# a commit lands on the branch between ait's commit-tree and its update-ref
echo 'git -C "$T/r" commit -q --allow-empty --no-verify -m race' > "$T/before-update-ref"
before=$(git rev-parse HEAD)
tick; run env PATH="$T/bin:$PATH" ait future offer src/stock.c specs/p.yaml -m m
check "race: E_HEAD_MOVED" sh -c "[ $rc = 1 ] && head -1 '$T/err' | grep -qF 'error: HEAD moved while running; nothing changed'"
check "race: only the racing commit landed" [ "$(git rev-parse HEAD^)" = "$before" ]
check "race: .ait/ and the index are as they were" sh -c '[ -z "$(git status --porcelain -- .ait)" ] && git diff --cached --quiet'

# no user hook runs on ait's commit (11.2)
printf '#!/bin/sh\necho hooked >> "%s/hooks.log"\n' "$T" > "$(git rev-parse --git-path hooks)/prepare-commit-msg"
cp "$(git rev-parse --git-path hooks)/prepare-commit-msg" "$(git rev-parse --git-path hooks)/post-commit"
chmod +x "$(git rev-parse --git-path hooks)/prepare-commit-msg" "$(git rev-parse --git-path hooks)/post-commit"
git reset -q --hard "$before"
tick; expect_ok "a request commits" ait future offer src/stock.c specs/p.yaml -m m
check "without the user's hooks" [ ! -e "$T/hooks.log" ]
done_testing
