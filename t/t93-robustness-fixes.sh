#!/bin/sh
# Regressions for the robustness review; each case was reproduced, then traced to its cause
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent a)
exec_def guard
tick; ait future offer src/stock.c specs/guard.yaml -m m >"$T/out"; O=$(new_id)
H=$(git rev-parse HEAD)
push_check() { echo "refs/heads/master $(git rev-parse HEAD) refs/heads/master 0000000000000000000000000000000000000000" | ait hook pre-push; }

# 5.9 a: a value is one word of its set, not a substring of the set
sed -i.bak 's/^state future$/state future current/' .ait/$O && rm .ait/$O.bak && git add -A && tick && git commit -q --no-verify -m multi
expect_err "a state of two words" E_FORMAT push_check
git reset -q --hard "$H"

# 7.8 and 7.1.2: what is shown of HEAD skips a record that does not hold there, instead of crashing
git rm -q specs/guard.yaml && tick && git commit -q --no-verify -m rmdef
expect_ok "todo with a definition gone from HEAD" ait todo
check "the record is reported" err_has "warning: skipping '.ait/$O'"
git reset -q --hard "$H"

# 5.5: a malformed AIT_EXPECT aborts the transaction instead of crashing
printf '%040d %040d refs/heads/master\n' 0 1 > "$T/tx"
run sh -c "AIT_EXPECT=nospace ait hook reference-transaction prepared < '$T/tx'"
check "a malformed AIT_EXPECT aborts" [ "$rc" = 1 ]

# 5.5: a failed commit changes nothing: no record left in the work tree, no spec file left staged
printf '#!/bin/sh\nexit 1\n' > "$(git rev-parse --git-path hooks)/prepare-commit-msg"; chmod +x "$(git rev-parse --git-path hooks)/prepare-commit-msg"
exec_def pass
before=$(snapshot)
tick; run ait future offer src/order.c specs/pass.yaml -m m
check "a failed commit is an error" [ "$rc" != 0 ]
check "a failed commit changes nothing" [ "$(snapshot)" = "$before" ]
rm "$(git rev-parse --git-path hooks)/prepare-commit-msg"
R=$(mktemp -d) && (cd "$R" && git init -q && tick && git commit -q --allow-empty -m base &&
	printf '#!/bin/sh\nexit 1\n' > .git/hooks/prepare-commit-msg && chmod +x .git/hooks/prepare-commit-msg &&
	ait init >/dev/null 2>"$T/err"; echo $? > "$T/rc"; git status --porcelain --untracked-files=all > "$T/out")
check "init whose commit fails reports git's reason" sh -c "[ \$(cat '$T/rc') = 128 ] && ! grep -q pathspec '$T/err'"
check "init whose commit fails leaves nothing" [ ! -s "$T/out" ]

# 5.5: the branch switched between ait's judgement and its commit
# (git runs prepare-commit-msg even with --no-verify, inside the commit: a deterministic race)
HK=$(git rev-parse --git-path hooks)
git branch elsewhere
printf '#!/bin/sh\ngit -C "%s" symbolic-ref HEAD refs/heads/elsewhere\n' "$T/r" > "$HK/prepare-commit-msg"; chmod +x "$HK/prepare-commit-msg"
tick; run ait future offer src/order.c specs/pass.yaml -m m
check "a switched branch is HEAD moved" sh -c "[ $rc = 1 ] && grep -qF 'error: HEAD moved while running; nothing changed' '$T/err'"
check "nothing landed on the other branch" [ "$(git rev-parse elsewhere)" = "$(git rev-parse master)" ]
rm "$HK/prepare-commit-msg"; git symbolic-ref HEAD refs/heads/master

# 5.5: a commit landing while git commit runs is refused by git's own ref lock, before any hook: HEAD moved too
printf '#!/bin/sh\n[ -e "%s/raced" ] && exit 0; touch "%s/raced"\nenv -u GIT_INDEX_FILE -u AIT_EXPECT git -C "%s" commit -q --allow-empty --no-verify -m race\n' "$T" "$T" "$T/r" > "$HK/prepare-commit-msg"; chmod +x "$HK/prepare-commit-msg"
tick; run ait future offer src/order.c specs/pass.yaml -m m
check "a commit landing meanwhile is HEAD moved" sh -c "[ $rc = 1 ] && grep -qF 'error: HEAD moved while running; nothing changed' '$T/err'"
rm "$HK/prepare-commit-msg" "$T/raced"

# 5.5: emptiness was judged against the HEAD ait read; a race landing the same tree is HEAD moved, not git's "nothing to commit"
mkdir -p "$T/bin"
cat > "$T/bin/git" <<'GIT'
#!/bin/sh
real=$(PATH=$(echo "$PATH" | sed "s|$T/bin:||") command -v git)
for a; do [ "$a" = commit ] && [ ! -e "$T/raced" ] && { touch "$T/raced"; env -u AIT_EXPECT "$real" -C "$T/r" commit -q --no-verify -m same; }; done
exec "$real" "$@"
GIT
chmod +x "$T/bin/git"
tick; run env PATH="$T/bin:$PATH" ait future offer src/order.c specs/pass.yaml -m m
check "a race landing the judged tree is HEAD moved" sh -c "[ $rc = 1 ] && grep -qF 'error: HEAD moved while running; nothing changed' '$T/err'"
git reset -q --hard HEAD^ && rm "$T/raced"

# 5.5: what is committed is what was judged, even if the index changes meanwhile
printf '#!/bin/sh\necho late > "%s/late.txt"\nGIT_INDEX_FILE="%s/.git/index" git -C "%s" add late.txt\n' "$T/r" "$T/r" "$T/r" > "$HK/prepare-commit-msg"; chmod +x "$HK/prepare-commit-msg"
echo y >> src/order.c && git add src/order.c
tick; run ait commit -m judged
check "the judged commit" [ "$rc" = 0 ]
check "a file staged meanwhile is not in it" sh -c "! git cat-file -e HEAD:late.txt 2>/dev/null"
check "and stays staged" sh -c "git diff --cached --name-only | grep -qx late.txt"
rm "$HK/prepare-commit-msg"; git reset -q late.txt; rm -f late.txt

# 7.1.3: a cherry-pick resolution is an integration: validated, not guarded
git checkout -q -b side && tick && ait future amend $O src/order.c - -m s >/dev/null 2>&1; S=$(git rev-parse HEAD)
git checkout -q master && tick && ait future cancel $O -m c >/dev/null 2>&1
check "the side changed the offer" [ "$S" != "$(git rev-parse master)" ]
git cherry-pick $S >/dev/null 2>&1
check "the cherry-pick conflicts on the record" sh -c "git status --porcelain | grep -q '^UU .ait/$O'"
git checkout --theirs .ait/$O && git add .ait/$O
tick; expect_ok "a resolved cherry-pick commits" git commit -q --no-edit
done_testing
