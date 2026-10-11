#!/bin/sh
# T01 초기화 (7.1.1, 2.1.2a)
. "$(dirname "$0")/lib.sh"

# local helpers
# 0.1 up to (not including) `ait init`, in a fresh $T
setup_noinit() {
	T=$(mktemp -d); export T HOME=$T/home GIT_CONFIG_NOSYSTEM=1 GIT_EDITOR=:; mkdir -p "$HOME"
	unset AIT_SESSION
	git config --global user.name t; git config --global user.email t@t; git config --global init.defaultBranch master
	N=0
	git init -q --bare "$T/origin.git"
	git init -q "$T/r" && cd "$T/r" && git remote add origin "$T/origin.git"
	mkdir src; for i in $(seq 1 50); do echo "o$i"; done > src/order.c
	for i in $(seq 1 50); do echo "s$i"; done > src/stock.c
	tick; git add -A && git commit -qm base
}
# start over: drop the old $T when everything passed so far
again() { cd /; [ "$failed" = 0 ] && rm -rf "$T"; setup_noinit; }
hook_body() { printf '#!/bin/sh\ncommand -v ait >/dev/null 2>&1 || exit 0\nexec ait hook %s "$@"\n' "$1"; }
out_is() { printf '%s\n' "$@" | cmp -s - "$T/out"; }
hookstate() { for h in pre-commit pre-merge-commit commit-msg pre-push post-commit post-merge; do ls -l "$1/$h" 2>&1; cat "$1/$h" 2>&1; done; }
LINE='if command -v ait >/dev/null 2>&1; then ait hook pre-commit "$@" || exit $?; fi'
# the insertion lines of 7.1.1-1 for hook NAME, as hint lines
hook_lines() {
	case $1 in
	pre-push)
		echo 'hint: t=$(mktemp) && cat > "$t" || exit 1'
		echo 'hint: if command -v ait >/dev/null 2>&1; then ait hook pre-push "$@" < "$t" || { s=$?; rm -f "$t"; exit $s; }; fi'
		echo 'hint: exec < "$t"; rm -f "$t"' ;;
	*) echo "hint: if command -v ait >/dev/null 2>&1; then ait hook $1 \"\$@\" || exit \$?; fi" ;;
	esac
}
# hint of 7.1.1-1 for hook NAME at PATH: the line naming it followed by the lines to insert, and
# "then rerun this command" (written once for all hooks, 5.9a)
has_hook_hint() {
	case $(cat "$T/err") in
	*"hint: make '$1' run 'ait hook $2' with its arguments and its stdin, and stop when that fails; git runs a hook only if it is executable. In a shell hook, these lines after the first line do it ('#!/bin/sh' first if the file is new):
$(hook_lines "$2")"*) err_has "hint: then rerun this command" ;;
	*) false ;;
	esac
}

setup_noinit
H=$(git rev-parse --git-path hooks)
RT=$(pwd -P)
BASE=$(git rev-parse HEAD)

# 1
tick; expect_ok "1: ait init" ait init
OID=$(git rev-parse HEAD)
check "1: stdout is exactly 'init created <new HEAD>'" out_is "init created $OID"
check "1: the new commit's parent is base" test "$(git rev-parse HEAD^)" = "$BASE"
check "1: the commit adds only .ait/init" test "$(git diff-tree -r --no-commit-id --name-status HEAD)" = "A	.ait/init"
check "1: .ait/init is 'version 2\\n'" test "$(git cat-file blob HEAD:.ait/init | od -c | head -1)" = "$(printf 'version 2\n' | od -c | head -1)"
check "1: .ait/init mode 100644" sh -c "git ls-tree HEAD .ait/init | grep -q '^100644 '"
check "1: subject is [CONTRACT] ait init" test "$(git log -1 --format=%s)" = "[CONTRACT] ait init"
check "1: no trailers (message is the subject only)" test "$(git log -1 --format=%B | git stripspace)" = "[CONTRACT] ait init"
for h in pre-commit pre-merge-commit commit-msg pre-push post-commit post-merge; do
	check "1: hook $h has the 7.1.1-1 body" sh -c "hook_body() { printf '#!/bin/sh\ncommand -v ait >/dev/null 2>&1 || exit 0\nexec ait hook %s \"\$@\"\n' \"\$1\"; }; hook_body $h | cmp -s - '$H/$h'"
	check "1: hook $h is executable" test -x "$H/$h"
done
# not yet: `ait init` does not install the merge driver; the attributes line and merge.ait.* config are not checked

# 2
before=$(hookstate "$H"; git config --list --local)
expect_ok "2: ait init again" ait init
check "2: stdout is exactly 'init exists'" out_is "init exists"
check "2: no new commit" test "$(git rev-parse HEAD)" = "$OID"
check "2: hooks and config unchanged" test "$(hookstate "$H"; git config --list --local)" = "$before"

# 3: an existing hook is not overwritten
again
H=$(git rev-parse --git-path hooks); RT=$(pwd -P); P="$RT/.git/hooks/pre-commit"
printf '#!/bin/sh\necho legacy >&2\n' > "$H/pre-commit"; chmod 755 "$H/pre-commit"
legacy=$(cat "$H/pre-commit")
B=$(git rev-parse HEAD)
tick; expect_err "3: existing pre-commit is E_STATE" E_STATE ait init
check "3: first line names the hook" sh -c "head -1 '$T/err' | grep -qxF \"error: not allowed: hook '$P' is not set up for ait\""
check "3: hint to make it run ait hook" has_hook_hint "$P" pre-commit
# 3 (7.1.1-1): '.git/hooks' is in the git common directory, not in the worktree: nothing to commit
check "3: no 'then commit it' for a hook in the git directory" sh -c "! grep -qF 'then commit it' '$T/err'"
check "3: stdout empty" test ! -s "$T/out"
check "3: no init commit" test "$(git rev-parse HEAD)" = "$B"
check "3: existing file untouched" test "$(cat "$H/pre-commit")" = "$legacy"
for h in pre-merge-commit commit-msg pre-push post-commit post-merge; do
	check "3: other hook $h installed" sh -c "printf '#!/bin/sh\ncommand -v ait >/dev/null 2>&1 || exit 0\nexec ait hook %s \"\$@\"\n' $h | cmp -s - '$H/$h' && test -x '$H/$h'"
done

# 3: insert the line as the second line, init again
printf '#!/bin/sh\n%s\necho legacy >&2\n' "$LINE" > "$H/pre-commit"
tick; expect_ok "3: init after inserting the line" ait init
check "3: init created <OID>" out_is "init created $(git rev-parse HEAD)"
# H12 needs a record in HEAD: make one and see no H12
S=$(ait session open --agent agent-a); prompt_defs
tick; expect_ok "3: a record for H12" ait future offer src/stock.c specs/p.yaml -m m
expect_ok "3: todo" ait todo
check "3: no H12 with the inserted line" sh -c "! grep -qF 'not set up for ait' '$T/err'"

# chmod -x: git would not call it, so there is no ait hook
chmod -x "$H/pre-commit"
expect_err "3: non-executable hook is E_STATE" E_STATE ait init
check "3: same first line (chmod -x)" sh -c "head -1 '$T/err' | grep -qxF \"error: not allowed: hook '$P' is not set up for ait\""
check "3: same hint (chmod -x)" has_hook_hint "$P" pre-commit
chmod +x "$H/pre-commit"

# git commit runs both ait's pre-commit and legacy
echo x >> src/order.c; git add src/order.c
tick; run git commit -qm code
check "3: plain code commit ok" test "$rc" = 0
check "3: legacy ran" err_has legacy
# ait rejects (.ait/ in a plain commit): the hook ends with ait's exit code
echo 'version 3' > .ait/init; git add .ait/init
B=$(git rev-parse HEAD)
tick; run git commit -qm bad
check "3: ait's E_GUARD stops the commit" sh -c "[ $rc = 1 ] && grep -qF 'error: contract files cannot be changed by a plain commit:' '$T/err'"
check "3: legacy did not run after ait failed" sh -c "! grep -qF legacy '$T/err'"
check "3: no commit" test "$(git rev-parse HEAD)" = "$B"
# without ait on PATH only legacy runs and it commits
NOAIT=$(dirname "$(command -v git)"):/usr/bin:/bin
tick; run env PATH="$NOAIT" git commit -qm bad
check "3: no ait on PATH: committed" sh -c "[ $rc = 0 ] && [ \"\$(git rev-parse HEAD)\" != '$B' ]"
check "3: no ait on PATH: legacy ran" err_has legacy

# 3: a non-shell commit-msg
again
H=$(git rev-parse --git-path hooks); RT=$(pwd -P); P="$RT/.git/hooks/commit-msg"
printf '#!/usr/bin/env python3\nimport sys\n' > "$H/commit-msg"; chmod 755 "$H/commit-msg"
tick; expect_err "3: python commit-msg is E_STATE" E_STATE ait init
check "3: python commit-msg first line" sh -c "head -1 '$T/err' | grep -qxF \"error: not allowed: hook '$P' is not set up for ait\""
check "3: python commit-msg same hint" has_hook_hint "$P" commit-msg
printf '#!/usr/bin/env python3\nimport subprocess, sys\nsubprocess.run(["ait", "hook", "commit-msg", *sys.argv[1:]], check=True)\n' > "$H/commit-msg"
tick; expect_ok "3: python commit-msg calling ait hook is seen" ait init
check "3: python: init created" out_matches '^init created [0-9a-f]{40}$'

# 3: an existing shell pre-push gets the three lines; ait and it get the same stdin
again
H=$(git rev-parse --git-path hooks); RT=$(pwd -P); P="$RT/.git/hooks/pre-push"
printf '#!/bin/sh\ncat > "$T/legacy.in"\n' > "$H/pre-push"; chmod 755 "$H/pre-push"
tick; expect_err "3: existing pre-push is E_STATE" E_STATE ait init
check "3: pre-push first line" sh -c "head -1 '$T/err' | grep -qxF \"error: not allowed: hook '$P' is not set up for ait\""
# 3 (7.1.1-1): the hint has the three pre-push lines to insert
check "3: pre-push hint" has_hook_hint "$P" pre-push
{
	echo '#!/bin/sh'
	echo 't=$(mktemp) && cat > "$t" || exit 1'
	echo 'if command -v ait >/dev/null 2>&1; then ait hook pre-push "$@" < "$t" || { s=$?; rm -f "$t"; exit $s; }; fi'
	echo 'exec < "$t"; rm -f "$t"'
	echo 'cat > "$T/legacy.in"'
} > "$H/pre-push"
tick; expect_ok "3: init after inserting the pre-push lines" ait init
run git push origin master
check "3: push ok" test "$rc" = 0
check "3: legacy pre-push got the push line" grep -qE "^refs/heads/master $(git rev-parse HEAD) refs/heads/master [0-9a-f]{40}$" "$T/legacy.in"
# a malformed record committed around the hooks: ait's pre-push rejects with E_FORMAT (128)
echo junk > .ait/Odeadbeefdead; git add .ait/Odeadbeefdead
tick; git commit -q --no-verify -m bad
rm -f "$T/legacy.in"
run git push origin master
check "3: pre-push ends with ait's code (E_FORMAT, 128)" sh -c "grep -qF 'fatal: bad format:' '$T/err' && [ \"\$(git rev-parse origin/master)\" != \"\$(git rev-parse HEAD)\" ]"
# git reports a failed pre-push as its own failure (exit 1); check the hook itself
run sh -c "printf 'refs/heads/master %s refs/heads/master %s\n' \$(git rev-parse HEAD) \$(git rev-parse origin/master) | '$H/pre-push' origin '$T/origin.git'"
check "3: pre-push hook exit code is 128" test "$rc" = 128
check "3: legacy did not run after ait failed" test ! -e "$T/legacy.in"

# 3: core.hooksPath .husky inside the repository
again
RT=$(pwd -P)
mkdir .husky; printf '#!/bin/sh\necho husky\n' > .husky/pre-commit; chmod 755 .husky/pre-commit
git config core.hooksPath .husky
tick; expect_err "3: .husky: E_STATE" E_STATE ait init
check "3: .husky: nothing written under .husky" test "$(ls -A .husky)" = "pre-commit"
for h in pre-commit pre-merge-commit commit-msg pre-push post-commit post-merge; do
	check "3: .husky: error for $h" err_has "error: not allowed: hook '$RT/.husky/$h' is not set up for ait"
	# 3 (7.1.1-1): '.husky' is in the worktree and not ignored, so the hint ends with "then commit it"
	check "3: .husky: hint for $h" has_hook_hint "$RT/.husky/$h" "$h"
	check "3: .husky: then commit it for $h" err_has "hint: then commit it: stage '$RT/.husky/$h' and run 'ait commit -m <why>'"
done
check "3: .husky: no init commit" test "$(git log --format=%s -1)" = base

# 3: core.hooksPath .husky/_ that is ignored
again
RT=$(pwd -P)
mkdir -p .husky/_; echo '*' > .husky/_/.gitignore
git config core.hooksPath .husky/_
tick; expect_err "3: .husky/_: E_STATE" E_STATE ait init
for h in pre-commit pre-merge-commit commit-msg pre-push post-commit post-merge; do
	check "3: .husky/_: error for $h" err_has "error: not allowed: hook '$RT/.husky/_/$h' is not set up for ait"
done
check "3: .husky/_: no 'then commit it'" sh -c "! grep -qF 'then commit it' '$T/err'"

# 3: ait hook only in a comment line
again
H=$(git rev-parse --git-path hooks); RT=$(pwd -P); P="$RT/.git/hooks/pre-commit"
printf '#!/bin/sh\n# ait hook pre-commit "$@"\necho legacy >&2\n' > "$H/pre-commit"; chmod 755 "$H/pre-commit"
tick; expect_err "3: commented-out ait hook is E_STATE" E_STATE ait init
check "3: commented: first line" sh -c "head -1 '$T/err' | grep -qxF \"error: not allowed: hook '$P' is not set up for ait\""
check "3: commented: hint" has_hook_hint "$P" pre-commit

# 4: outside a git repository
mkdir "$T/outside"; cd "$T/outside"
expect_err "4: outside a repository is E_GIT" E_GIT ait init
check "4: hint" err_has "hint: run this command inside a git repository"
check "4: no git command in the hints" sh -c "! grep '^hint:' '$T/err' | grep -qF \"'git \""
check "4: no repository made" test ! -e "$T/outside/.git"
cd "$T/r"

# 5: a clone without hooks (H12)
again
tick; ait init >/dev/null
git push -q origin master
git clone -q "$T/origin.git" "$T/c1"; cd "$T/c1"
# not yet: no peek; the scenario's `ait peek` becomes `ait todo` (5.2a covers every ait command but init)
expect_err "5: peek is not supported yet" UNSUPPORTED ait peek
expect_ok "5: c1 (no records): todo without a session" ait todo
check "5: c1: no hint" sh -c "! grep -q '^hint:' '$T/err'"
cd "$T/r"
S=$(ait session open --agent agent-a); prompt_defs
tick; ait future offer src/stock.c specs/p.yaml -m m >/dev/null; git push -q origin master
git clone -q "$T/origin.git" "$T/c2"; cd "$T/c2"
expect_ok "5: c2 (records, no hooks): todo" ait todo
check "5: c2: H12" err_has "hint: this clone is not set up for ait; run 'ait init'"
tick; ait init >/dev/null
expect_ok "5: c2 after init: todo" ait todo
check "5: c2 after init: no H12" sh -c "! grep -qF 'not set up for ait' '$T/err'"

done_testing
