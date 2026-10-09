#!/bin/sh
# mini: the compare-and-swap of spec 5.5 through git commit and the reference-transaction hook
. "$(dirname "$0")/lib.sh"
setup

H=$(git rev-parse HEAD)
check "the hook is installed" test -x "$(git rev-parse --git-path hooks)/reference-transaction"

# a commit whose branch moved since ait read HEAD is aborted under git's ref lock
tick; run env AIT_EXPECT="0000000000000000000000000000000000000001 refs/heads/master" git commit -q --allow-empty -m stale
check "stale HEAD: git commit fails" [ "$rc" != 0 ]
check "stale HEAD: HEAD unchanged" [ "$(git rev-parse HEAD)" = "$H" ]

# the expected HEAD commits
tick; run env AIT_EXPECT="$H refs/heads/master" git commit -q --allow-empty -m fresh
check "expected HEAD: git commit succeeds" [ "$rc" = 0 ]
check "expected HEAD: HEAD moved" [ "$(git rev-parse HEAD^)" = "$H" ]

# without AIT_EXPECT the hook does nothing
tick; run git commit -q --allow-empty -m plain
check "no AIT_EXPECT: plain commits are not affected" [ "$rc" = 0 ]

# an ait request that loses a race: a wrapper git moves the branch just before ait's git commit
mkdir -p "$T/bin"
cat > "$T/bin/git" <<'GIT'
#!/bin/sh
real=$(PATH=$(echo "$PATH" | sed "s|$T/bin:||") command -v git)
for a; do [ "$a" = commit ] && [ ! -e "$T/raced" ] && { touch "$T/raced"; "$real" -C "$T/r" commit -q --allow-empty --no-verify -m race; }; done
exec "$real" "$@"
GIT
chmod +x "$T/bin/git"
A=$(ait session open --agent a)
prompt_defs
before=$(git rev-parse HEAD)
tick; PATH="$T/bin:$PATH" run ait future offer src/stock.c specs/p.yaml -m m
check "race: E_HEAD_MOVED" sh -c "[ $rc = 1 ] && head -1 '$T/err' | grep -qF 'error: HEAD moved while running; nothing changed'"
check "race: only the racing commit landed" [ "$(git rev-parse HEAD^)" = "$before" ]
check "race: .ait/ is as HEAD has it" [ -z "$(git status --porcelain -- .ait)" ]
done_testing
