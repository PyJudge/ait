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

# 7.8 (user decision 53): a definition todo cannot read stops it with that definition's error
git rm -q specs/guard.yaml && tick && git commit -q --no-verify -m rmdef
expect_err "todo with a definition gone from HEAD" E_INCOMPLETE ait todo
check "the error names the definition" err_has "missing 'specs/guard.yaml'"
git reset -q --hard "$H"

# 5.5: a failed commit changes nothing: no record left in the work tree, no spec file left staged
racing_git
exec_def pass
before=$(snapshot)
echo 'exit 1' > "$T/before-update-ref"
tick; run env PATH="$T/bin:$PATH" ait future offer src/order.c specs/pass.yaml -m m
check "a failed commit is an error" [ "$rc" = 128 ]
check "a failed commit changes nothing" [ "$(snapshot)" = "$before" ]
R=$(mktemp -d) && echo 'exit 1' > "$T/before-update-ref" && (cd "$R" && git init -q && tick && git commit -q --allow-empty -m base &&
	env PATH="$T/bin:$PATH" ait init >/dev/null 2>"$T/err"; echo $? > "$T/rc"; git status --porcelain --untracked-files=all > "$T/out")
check "init whose commit fails is an error" [ "$(cat "$T/rc")" = 128 ]
check "init whose commit fails leaves nothing" [ ! -s "$T/out" ]

# 5.5-4/5: the commit lands on the branch ait judged; HEAD switched meanwhile gets the warning, not the update
git branch elsewhere
echo 'git -C "$T/r" symbolic-ref HEAD refs/heads/elsewhere' > "$T/before-update-ref"
tick; run env PATH="$T/bin:$PATH" ait future offer src/order.c specs/pass.yaml -m m
check "the request succeeds" [ "$rc" = 0 ]
check "on the branch it judged" [ "$(git rev-parse master^)" = "$(git rev-parse elsewhere)" ]
check "with the warning of 5.5-5" err_has "but the index and work tree were not updated"
git symbolic-ref HEAD refs/heads/master && git reset -q --hard

# 5.5-4: what is committed is what was judged, and a file staged meanwhile stays staged
echo 'echo late > "$T/r/late.txt" && git -C "$T/r" add late.txt' > "$T/before-update-ref"
echo y >> src/order.c && git add src/order.c
tick; run env PATH="$T/bin:$PATH" ait commit -m judged
check "the judged commit" [ "$rc" = 0 ]
check "a file staged meanwhile is not in it" sh -c "! git cat-file -e HEAD:late.txt 2>/dev/null"
check "and stays staged" sh -c "git diff --cached --name-only | grep -qx late.txt"
git reset -q late.txt; rm -f late.txt

# 5.5-4: a deletion that ait commit lands is in the index already; no warning, no crash
git rm -q src/order.c
tick; run ait commit -m gone
check "a committed deletion" [ "$rc" = 0 ]
check "has no warning" [ ! -s "$T/err" ]
git reset -q --hard HEAD^

# 7.1.3: a cherry-pick resolution is an integration: validated, not guarded
git checkout -q -b side && tick && ait future amend $O src/order.c - -m s >/dev/null 2>&1; S=$(git rev-parse HEAD)
git checkout -q master && tick && ait future amend $O src/stock.c,src/order.c - -m c >/dev/null 2>&1; M=$(git rev-parse HEAD)
check "the side changed the offer" [ "$S" != "$M" ]
git cherry-pick $S >/dev/null 2>&1
check "the cherry-pick conflicts on the record" sh -c "git status --porcelain | grep -q '^UU .ait/$O'"
git checkout --theirs .ait/$O && git add .ait/$O
tick; expect_ok "a resolved cherry-pick commits" git commit -q --no-edit
# 2.3.2: a resolution that brings a cancelled contract back is refused
git reset -q --hard "$M"
tick && ait future cancel $O -m c >/dev/null 2>&1
git cherry-pick $S >/dev/null 2>&1
git checkout --theirs .ait/$O && git add .ait/$O
tick; expect_err "a resolution reviving the cancelled offer is refused" E_STATE git commit -q --no-edit
git cherry-pick --abort
done_testing
