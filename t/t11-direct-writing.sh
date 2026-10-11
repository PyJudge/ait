#!/bin/sh
# T11 직접 작성: contract new · add · commit (6, 7.4–7.6)
# Every step and row of the scenario; hints are spec 5.8, 5.9a and 5.10.
. "$(dirname "$0")/lib.sh"

# replace FILE's content by sed ARGS..., keeping its mode
edit() { f=$1; shift; sed "$@" "$f" > "$T/edit" && cat "$T/edit" > "$f"; }
staged_none() { [ -z "$(git diff --cached --name-only)" ]; }
clean_tree() { [ -z "$(git status --porcelain --untracked-files=all)" ]; }
rec_line() { git show "HEAD:.ait/$1" | grep -qxF -- "$2"; }
# stderr has $1 before $2 (5.9a: hints in order)
err_before() {
	a=$(grep -bo -F -- "$1" "$T/err" | head -1 | cut -d: -f1)
	b=$(grep -bo -F -- "$2" "$T/err" | head -1 | cut -d: -f1)
	[ -n "$a" ] && [ -n "$b" ] && [ "$a" -lt "$b" ]
}
q_def() { printf 'body: %s\nengine: exec\ncommand: [./tests/q.sh]\ninputs: [tests/q.sh]\n' "$1" > specs/q.yaml; }

setup; A=$(ait session open --agent agent-a)

# 1: the demand frame of 7.4.1
expect_ok "1: contract new demand" ait contract new demand
check "1: one line, path .ait/D<12hex>" sh -c "[ \$(wc -l < '$T/out') = 1 ] && grep -qxE 'path \.ait/D[0-9a-f]{12}' '$T/out'"
DN=$(out_value path | sed 's|^\.ait/||')
printf 'role demand\nstate future\nconsumer PATH\nprovider PATH\nfile PATH\n' > "$T/frame"
check "1: the frame" cmp -s "$T/frame" ".ait/$DN"
check "1: nothing staged" staged_none

# 2: the writing form of 2.2.3: keys in another order, no BLOB, creator or prev
printf 'file specs/q.yaml\nconsumer src/order.c:1-5\nprovider src/stock.c\nstate future\nrole demand\n' > ".ait/$DN"
mkdir -p specs tests
q_def q
printf '#!/bin/sh\nexit 0\n' > tests/q.sh; chmod 755 tests/q.sh
expect_ok "2: contract add .ait/Dn specs/q.yaml tests/q.sh" ait contract add ".ait/$DN" specs/q.yaml tests/q.sh
check "2: three staged lines" sh -c "[ \$(grep -c '^staged ' '$T/out') = 3 ] && grep -qx 'staged .ait/$DN' '$T/out' && grep -qx 'staged specs/q.yaml' '$T/out' && grep -qx 'staged tests/q.sh' '$T/out'"

# 3: the gate fills the canonical form (6.2)
tick; expect_ok "3: contract commit" ait contract commit -m "직접 작성"
check "3: request" out_matches '^request R[0-9a-f]{12}$'
check "3: new Dn" out_has "new $DN"
printf '%s\n' "role demand" "state future" \
	"consumer src/order.c:1-5 $(blob HEAD:src/order.c)" "provider src/stock.c $(blob HEAD:src/stock.c)" \
	"file specs/q.yaml 100644 $(blob HEAD:specs/q.yaml) def" "file tests/q.sh 100755 $(blob HEAD:tests/q.sh) input" \
	"creator agent-a $A" "prev -" > "$T/want"
rec "$DN" > "$T/rec"
check "3: rec(Dn) is canonical" cmp -s "$T/want" "$T/rec"
check "3: the work tree's Dn is canonical too" cmp -s "$T/want" ".ait/$DN"
check "3: the work tree is clean" clean_tree

# 4: a spec file edited directly is pinned again (6.2)
q_def q4
P=$(git rev-parse "HEAD:.ait/$DN")
expect_ok "4: contract add specs/q.yaml" ait contract add specs/q.yaml
tick; expect_ok "4: contract commit" ait contract commit -m m
check "4: changed Dn" out_has "changed $DN"
check "4: specs/q.yaml pinned again" rec_line "$DN" "file specs/q.yaml 100644 $(blob HEAD:specs/q.yaml) def"
check "4: prev is the last version" rec_line "$DN" "prev $P"

# 4a: the spec definition frames of 7.4.1a
expect_ok "4a: contract new spec specs/t.yaml exec" ait contract new spec specs/t.yaml exec
check "4a: stdout is path specs/t.yaml" [ "$(cat "$T/out")" = "path specs/t.yaml" ]
cat > "$T/exec" <<'EOF'
# ait spec definition (exec): the checker runs 'command'; it passes if the exit code is in 'success'.
# Keys: body, engine, command, success, environment, inputs. Any other key is a format error.
body: |
  <the condition the code must keep>
engine: exec
command: [<program>, <arg>]   # argv, run without a shell
success: [0]                  # exit codes that count as pass
inputs: []                    # files pinned with the contract: scripts in 'command', test data. Not the code under check
# environment: <name>         # optional: specs/environments/<name>.yaml (cwd, timeout_ms, env, prepare, cleanup)
EOF
cat > "$T/prompt" <<'EOF'
# ait spec definition (prompt): the checker reads the code and judges whether 'body' holds.
# Keys: body, inputs. Any other key is a format error.
body: |
  <the condition the code must keep>
inputs: []                    # files pinned with the contract, such as test data. Not the code under check
EOF
check "4a: the exec frame" cmp -s "$T/exec" specs/t.yaml
check "4a: nothing staged" staged_none
edit specs/t.yaml -e 's/^  <the condition the code must keep>$/  t/' -e 's/^command: \[<program>, <arg>\]/command: [.\/tests\/t.sh]/' \
	-e 's/^inputs: \[\]/inputs: [tests\/t.sh]/'
printf '#!/bin/sh\nexit 0\n' > tests/t.sh; chmod 755 tests/t.sh
tick; expect_ok "4a: future offer src/stock.c specs/t.yaml with the comments kept" ait future offer src/stock.c specs/t.yaml -m t
cp specs/t.yaml "$T/t.yaml"
expect_err "4a: contract new spec on an existing path" E_STATE ait contract new spec specs/t.yaml prompt
check "4a: the file is kept" cmp -s "$T/t.yaml" specs/t.yaml
expect_ok "4a: contract new spec specs/u.yaml prompt" ait contract new spec specs/u.yaml prompt
check "4a: the prompt frame" cmp -s "$T/prompt" specs/u.yaml
H4A=$(git rev-parse HEAD)

# 5: each row starts from 4a's HEAD
back() { git reset -q --hard "$H4A"; git clean -fdq; }
back

# a: code is not a contract path (7.5.1)
expect_err "a: contract add src/stock.c" E_MIXED ait contract add src/stock.c
check "a: nothing staged" staged_none
back

# b: adding only the given path would leave a related path unstaged (7.5.2)
echo '# b' >> tests/q.sh; q_def b
expect_err "b: contract add specs/q.yaml" E_INCOMPLETE ait contract add specs/q.yaml
check "b: unstaged tests/q.sh" out_has "unstaged tests/q.sh"
check "b: nothing staged" staged_none
check "b: hint stages them together" err_has "stage them together: 'ait contract add specs/q.yaml tests/q.sh'"
back

# c: staged code in a contract commit (6.1)
q_def c; echo s51 >> src/stock.c
expect_ok "c: contract add specs/q.yaml" ait contract add specs/q.yaml
git add src/stock.c
expect_err "c: contract commit" E_MIXED ait contract commit -m m
check "c: impl src/stock.c" out_has "impl src/stock.c"
check "c: hint unstages the code" err_has "unstage the code ('src/stock.c'), then rerun this command; when the code is ready, stage it and run 'ait commit -m <why>'"
back

# d: an untracked file under .ait/ (7.6.1)
echo 'role demand' > .ait/D222222222222
expect_err "d: contract commit with an untracked .ait/D222222222222" E_INCOMPLETE ait contract commit -m m
back

# e: future to current written directly (2.3, 5.9a transition hint 4)
edit ".ait/$DN" 's/^state future$/state current/'
expect_ok "e: contract add .ait/Dn" ait contract add ".ait/$DN"
expect_err "e: contract commit" E_STATE ait contract commit -m m
check "e: hint fulfills it instead" err_has "fulfill it instead: restore '.ait/$DN' as in 'HEAD', then 'ait fulfill $DN -m <why>' to get the tasks"
back
# e, with another line changed too: set the state back instead of restoring
edit ".ait/$DN" -e 's/^state future$/state current/' -e 's/^provider .*/provider src\/stock.c:1-3/'
expect_ok "e (provider too): contract add .ait/Dn" ait contract add ".ait/$DN"
expect_err "e (provider too): contract commit" E_STATE ait contract commit -m m
check "e (provider too): hint sets the state back" err_has "set 'state future' back in '.ait/$DN', then 'ait contract add .ait/$DN' and 'ait contract commit -m <why>'"
check "e (provider too): no restore" sh -c "! grep -qF \"restore '.ait/$DN'\" '$T/err'"
back

# f: creator changed (6.2)
edit ".ait/$DN" "s/^creator .*/creator agent-b $A/"
expect_ok "f: contract add .ait/Dn" ait contract add ".ait/$DN"
expect_err "f: contract commit" E_STATE ait contract commit -m m
check "f: hint restores it" err_has "restore '.ait/$DN' as in 'HEAD'"
back

# f2: creator and provider changed: drop the creator line (5.9a transition hint 2, 2.2.3)
edit ".ait/$DN" -e "s/^creator .*/creator agent-b $A/" -e 's/^provider .*/provider src\/stock.c:1-3/'
expect_ok "f2: contract add .ait/Dn" ait contract add ".ait/$DN"
expect_err "f2: contract commit" E_STATE ait contract commit -m m
check "f2: hint removes the creator line" err_has "remove the 'creator' line from '.ait/$DN' (HEAD's value is kept), then 'ait contract add .ait/$DN' and 'ait contract commit -m <why>'"
check "f2: no restore it" sh -c "! grep -qF \"restore '.ait/$DN'\" '$T/err'"
edit ".ait/$DN" '/^creator /d'
expect_ok "f2: contract add .ait/Dn as the hint says" ait contract add ".ait/$DN"
tick; expect_ok "f2: contract commit as the hint says" ait contract commit -m m
check "f2: changed Dn" out_has "changed $DN"
check "f2: the creator is HEAD's" rec_line "$DN" "creator agent-a $A"
back

# g: a deleted record is judged by the gate (7.6.1, 2.1.5)
git rm -q ".ait/$DN"
expect_err "g: contract commit after git rm .ait/Dn" E_STATE ait contract commit -m m
check "g: hint restores it" err_has "restore '.ait/$DN' as in 'HEAD'"
back

# h: nothing staged
expect_ok "h: contract commit with nothing staged" ait contract commit -m m
check "h: stdout noop" [ "$(cat "$T/out")" = noop ]
back

# h2: a declared spec file changed only in the work tree (7.6.1)
q_def h2
expect_ok "h2: contract commit" ait contract commit -m m
check "h2: stdout noop and unstaged specs/q.yaml" [ "$(cat "$T/out")" = "$(printf 'noop\nunstaged specs/q.yaml')" ]
check "h2: hint stages it" err_has "hint: stage it: 'ait contract add specs/q.yaml', then rerun this command"
check "h2: no warning" sh -c "! grep -q '^warning:' '$T/err'"
back

# i: a path under .ait/ that is not a record path (2.1.3, 5.10)
echo 'role demand' > .ait/Dxyz
expect_ok "i: contract add .ait/Dxyz" ait contract add .ait/Dxyz
expect_err "i: contract commit" E_FORMAT ait contract commit -m m
check "i: hint removes it" err_has "remove '.ait/Dxyz'"
check "i: no contract commit after it (not in HEAD)" sh -c "! grep -qF \"remove '.ait/Dxyz', then\" '$T/err'"
git rm -qf .ait/Dxyz
expect_ok "i: contract commit as the hint says" ait contract commit -m m
check "i: noop" [ "$(cat "$T/out")" = noop ]
back

# u: the wrong version is only in this request (5.10)
edit ".ait/$DN" 's/^provider .*/provider src\/stock.c:1-3/'
printf '%s' "$(cat ".ait/$DN")" > "$T/edit" && cat "$T/edit" > ".ait/$DN"
expect_ok "u: contract add .ait/Dn" ait contract add ".ait/$DN"
expect_err "u: contract commit" E_FORMAT ait contract commit -m m
check "u: hint fixes it in this request" err_has "fix it in this request (see the error line), then 'ait contract add .ait/$DN' and 'ait contract commit -m <why>'"
check "u: no restore" sh -c "! grep -qF \"restore '.ait/$DN'\" '$T/err'"
back

# j: the init file (2.1.2a, 5.10)
echo 'version 3' > .ait/init
expect_ok "j: contract add .ait/init" ait contract add .ait/init
expect_err "j: contract commit" E_FORMAT ait contract commit -m m
check "j: hint rewrites .ait/init" err_has "rewrite '.ait/init' as the single line 'version 2' with mode 100644, then 'ait contract add .ait/init' and 'ait contract commit -m <why>'"
back

# k: a path under .ait/ that is not a record path, in HEAD (5.10, 6.6)
prompt_defs
echo 'role demand' > .ait/Dxyz; git add .ait/Dxyz; tick; git commit -q --no-verify -m bad 2>/dev/null
desc="k: future offer src/order.c p"
expect_unchanged expect_err "$desc" E_FORMAT ait future offer src/order.c specs/p.yaml -m m
check "k: hint removes it, then contract commit" err_has "remove '.ait/Dxyz', then 'ait contract commit -m <why>'"
git rm -q .ait/Dxyz
tick; expect_ok "k: contract commit as the hint says" ait contract commit -m m
check "k: the commit deletes .ait/Dxyz" [ "$(git diff --name-status HEAD^ HEAD)" = "$(printf 'D\t.ait/Dxyz')" ]
back

# l: a dirty .ait/ blocks a helper; the hint's path is from the top (5.9a)
prompt_defs
echo '# x' >> ".ait/$DN"
desc="l: future offer from src/ with .ait/Dn dirty"
expect_unchanged expect_err "$desc" E_DIRTY sh -c 'cd src && exec ait future offer src/stock.c specs/p.yaml -m m'
check "l: hint drops them" err_has "or drop them: restore '.ait/' to HEAD's version and remove the new files there"
git checkout -q HEAD -- .ait
tick; expect_ok "l: the same command after the restore" sh -c 'cd src && exec ait future offer src/stock.c specs/p.yaml -m m'
back

# m: a plain commit with a spec file and code staged (5.8 E_GUARD)
q_def m; echo s51 >> src/stock.c; git add specs/q.yaml src/stock.c
tick; expect_err "m: git commit from src/" E_GUARD sh -c 'cd src && exec git commit -qm x'
check "m: hint unstages the spec file" err_has "unstage 'specs/q.yaml' and commit the rest; "
back

# n: a dirty .ait/ blocks ait commit; the staged code goes first (5.9a local changes)
edit ".ait/$DN" 's/^consumer .*/consumer src\/order.c:1-6/'
echo s51 >> src/stock.c; git add src/stock.c
desc="n: ait commit with .ait/Dn dirty"
expect_unchanged expect_err "$desc" E_DIRTY ait commit -m x
check "n: uncommitted changes in .ait/" err_has "error: uncommitted changes in .ait/"
check "n: unstage the code before the keep hint" err_before "unstage 'src/stock.c' first" "ait contract add .ait/$DN"
back

# o: a file taken out of the definition is no related path (7.5.2) but is in the commit (6.1)
edit specs/q.yaml 's/^inputs: .*/inputs: []/'
git rm -q tests/q.sh
expect_ok "o: contract add specs/q.yaml" ait contract add specs/q.yaml
check "o: staged specs/q.yaml" out_has "staged specs/q.yaml"
tick; expect_ok "o: contract commit" ait contract commit -m x
check "o: changed Dn" out_has "changed $DN"
check "o: rec(Dn) has no tests/q.sh line" sh -c "! git show HEAD:.ait/$DN | grep -q ' tests/q.sh '"
check "o: the commit deletes tests/q.sh" sh -c "git diff --name-status HEAD^ HEAD | grep -qx 'D	tests/q.sh'"
back

# p: a declared input is missing (7.5.2, 5.8)
rm tests/q.sh
expect_err "p: contract add specs/q.yaml" E_INCOMPLETE ait contract add specs/q.yaml
check "p: missing tests/q.sh" out_has "missing tests/q.sh"
check "p: nothing staged" staged_none
check "p: hint restores it or stops declaring it" err_has "restore 'tests/q.sh' from HEAD, or stop declaring it in 'specs/q.yaml'"
check "p: no contract add tests/q.sh" sh -c "! grep -qF 'ait contract add tests/q.sh' '$T/err'"
back

# q: git commit -a commits with a temporary index (5.8 E_GUARD)
q_def q2; echo s51 >> src/stock.c
tick; expect_err "q: git commit -qam x" E_GUARD git commit -qam x
check "q: hint commits without -a" err_has "commit without -a or paths: stage the code ('src/stock.c') and commit it, then commit them through ait: 'ait contract add specs/q.yaml' and 'ait contract commit -m <why>'"
back

# q2: as q, with .ait/Dn staged in this worktree's index
edit ".ait/$DN" 's/^consumer .*/consumer src\/order.c:1-6/'
git add ".ait/$DN"
q_def q2; echo s51 >> src/stock.c
tick; expect_err "q2: git commit -qam x" E_GUARD git commit -qam x
check "q2: unstage .ait/Dn before commit without -a" err_before "unstage '.ait/$DN' first" "commit without -a or paths"
ADD=$(sed -n "s/.*then commit them through ait: 'ait contract add \([^']*\)'.*/\1/p" "$T/err" | head -1)
git reset -q -- ".ait/$DN"
git add src/stock.c; tick; run git commit -qm code
check "q2: the code commit" sh -c "[ $rc = 0 ] && [ \"\$(git diff --name-only HEAD^ HEAD)\" = src/stock.c ]"
expect_ok "q2: contract add as the hint says" ait contract add $ADD
tick; expect_ok "q2: contract commit as the hint says" ait contract commit -m m
check "q2: a separate contract commit" sh -c "[ \"\$(git diff --name-only HEAD^ HEAD | tr '\n' ' ')\" = '.ait/$DN specs/q.yaml ' ] && [ \"\$(git diff --name-only HEAD~2 HEAD^)\" = src/stock.c ]"
back

# s: a drop of a future contract (5.9a transition hint 8)
expect_ok "s: contract new drop" ait contract new drop
X=$(out_value path | sed 's|^\.ait/||')
edit ".ait/$X" "s/^target .*/target $DN/"
expect_ok "s: contract add .ait/X" ait contract add ".ait/$X"
expect_err "s: contract commit" E_STATE ait contract commit -m m
check "s: Dn is not current" err_has "'$DN' is not current; a drop withdraws a current contract"
check "s: remove the drop" err_has "remove '.ait/$X'"
check "s: cancel it if no longer needed" err_has "if it is no longer needed, cancel it"
check "s: set state cancelled" err_has "set 'state cancelled' in '.ait/$DN'"
back

# r (last): a closed contract is not changed; move the change to a new contract (5.9a transition hint 3)
tick; expect_ok "r: future cancel Dn" ait future cancel "$DN" -m m
edit ".ait/$DN" -e 's/^state cancelled$/state future/' -e 's/^provider .*/provider src\/stock.c:1-3/'
expect_ok "r: contract add .ait/Dn" ait contract add ".ait/$DN"
expect_err "r: contract commit" E_STATE ait contract commit -m m
check "r: Dn is closed" err_has "'$DN' is closed"
check "r: move the change to a new contract" err_has "to need it again, move your change to a new contract in this request: 'ait contract new demand', fill it with 'consumer src/order.c:1-5', 'provider src/stock.c:1-3', 'file specs/q.yaml', then 'ait contract add' it"
check "r: then restore Dn" err_has "restore '.ait/$DN' as in 'HEAD'"
check "r: closed, then the new contract" err_before "'$DN' is closed" "to need it again"
check "r: the new contract, then the restore" err_before "to need it again" "restore '.ait/$DN' as in 'HEAD'"
expect_ok "r: contract new demand" ait contract new demand
NEW=$(out_value path | sed 's|^\.ait/||')
printf 'role demand\nstate future\nconsumer src/order.c:1-5\nprovider src/stock.c:1-3\nfile specs/q.yaml\n' > ".ait/$NEW"
expect_ok "r: contract add the new contract" ait contract add ".ait/$NEW"
git checkout -q HEAD -- ".ait/$DN"
tick; expect_ok "r: contract commit as the hint says" ait contract commit -m x
check "r: request" out_matches '^request R[0-9a-f]{12}$'
check "r: new <new ID>" out_has "new $NEW"
check "r: the new contract's provider is src/stock.c:1-3" rec_line "$NEW" "provider src/stock.c:1-3 $(blob HEAD:src/stock.c)"
check "r: rec(Dn) is still cancelled" rec_line "$DN" "state cancelled"

# j2: a repository whose init file never was right (2.1.2a, 5.10)
setup_j2() {
	T=$(mktemp -d); export T HOME=$T/home GIT_CONFIG_NOSYSTEM=1 GIT_EDITOR=:; mkdir -p "$HOME"
	unset AIT_SESSION
	git config --global user.name t; git config --global user.email t@t; git config --global init.defaultBranch master
	N=0
	git init -q --bare "$T/origin.git"
	git init -q "$T/r" && cd "$T/r" && git remote add origin "$T/origin.git"
	mkdir src; for i in $(seq 1 50); do echo "o$i"; done > src/order.c
	for i in $(seq 1 50); do echo "s$i"; done > src/stock.c
	tick; git add -A && git commit -qm base
	mkdir .ait; echo 'version 1' > .ait/init; git add .ait/init; tick; git commit -qm bad
	git push -q origin master
}
setup_j2
A=$(ait session open --agent agent-a); prompt_defs
desc="j2: future offer src/stock.c p"
expect_unchanged expect_err "$desc" E_FORMAT ait future offer src/stock.c specs/p.yaml -m m
check "j2: hint rewrites .ait/init" err_has "rewrite '.ait/init' as the single line 'version 2' with mode 100644, then 'ait contract add .ait/init' and 'ait contract commit -m <why>'"
echo 'version 2' > .ait/init
expect_ok "j2: contract add .ait/init as the hint says" ait contract add .ait/init
tick; expect_ok "j2: contract commit as the hint says" ait contract commit -m m
check "j2: HEAD's init file is version 2" [ "$(git show HEAD:.ait/init)" = "version 2" ]

done_testing
