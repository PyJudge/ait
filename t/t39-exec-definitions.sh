#!/bin/sh
# T39 확인 요청에 실리는 exec 정의와 환경 (1.2, 2.5, 8.2)
#
# not yet: no `environment` in a spec definition. Steps that use the environment `e` are run
# once to see that they stop with "not supported"; their other expectations
# are checked on a definition without `environment` where the point survives.
. "$(dirname "$0")/lib.sh"

not() { ! "$@"; }
id_of() { sed -n "s/^new \([^ ]*\) $1\$/\1/p" "$T/out"; }
# qhas LINE: the check-request file has LINE (after leading spaces)
qhas() { sed 's/^ *//; s/^- //' "$Q" | grep -qxF -- "$1"; }

setup
A=$(ait session open --agent agent-a)

# setup of the scenario: specs/environments/e.yaml, sub/conf.txt, specs/envt.yaml (environment: e)
mkdir -p specs/environments sub
cat >specs/environments/e.yaml <<'EOF'
cwd: sub
env:
  inherit: [PATH, NEEDED]
  set: {MODE: test}
  required: [NEEDED]
prepare:
  - [sh, -c, "echo ready > prepared"]
cleanup:
  - [rm, -f, prepared]
EOF
echo conf >sub/conf.txt; git add sub/conf.txt; tick; git commit -qm conf
printf 'body: envt\nengine: exec\ncommand: [sh, -c, "test -f prepared && test -f conf.txt"]\nenvironment: e\n' >specs/envt.yaml

# 1-2 with environment: not supported yet
before=$(snapshot)
expect_err "1: a definition with environment" UNSUPPORTED ait offer src/stock.c specs/envt.yaml -m m
check "1: unchanged" test "$(snapshot)" = "$before"
rm -rf specs/environments specs/envt.yaml

# 1-2 without environment: what Q carries for an exec definition
exec_def pass
before=$(snapshot)
expect_err "1: offer needs results" E_NEEDS_RESULTS ait offer src/stock.c specs/pass.yaml -m m
check "1: unchanged" test "$(snapshot)" = "$before"
Q=$(out_value check-request) W=$(out_value check-result)
check "1: Q has the task +1:specs/pass.yaml" qhas 'task: "+1:specs/pass.yaml"'
check "1: Q has engine exec" qhas 'engine: "exec"'
check "1: Q has the command" qhas 'command: ["./tests/pass.sh"]'
check "1: Q has success [0]" qhas 'success: [0]'
check "1: Q has the tree of the candidate" sh -c "grep -q '^tree: ' '$Q'"
check "1: Q has no environment" not grep -q 'environment' "$Q"
check "1: ait ran nothing (no file appeared)" test -z "$(git status --porcelain --untracked-files=all | grep -v -e specs/pass.yaml -e tests/pass.sh)"
fill "$W" pass
tick; run ait offer src/stock.c specs/pass.yaml -m m -r "$W"
check "2: ok" test $rc = 0
O=$(out_value new | cut -d' ' -f1)
check "2: check O reported" out_has "check $O reported"
check "2: file rows are the def and its input" test "$(rec $O | grep '^file ')" = "$(printf 'file specs/pass.yaml 100644 %s def\nfile tests/pass.sh 100755 %s input' "$(blob HEAD:specs/pass.yaml)" "$(blob HEAD:tests/pass.sh)")"

# 3. the format is always checked
mkdir -p specs/environments
printf 'cwd: sub\nfoo: 1\n' >specs/environments/e2.yaml
printf 'body: e2\nengine: exec\ncommand: [./tests/pass.sh]\nenvironment: e2\n' >specs/e2.yaml
expect_err "3: an environment with an unknown key" UNSUPPORTED ait future offer src/stock.c specs/e2.yaml -m m
rm -rf specs/environments specs/e2.yaml
printf 'body: pc\ncommand: [./tests/pass.sh]\n' >specs/pc.yaml
before=$(snapshot)
expect_err "3: a prompt definition with command" E_FORMAT ait future offer src/stock.c specs/pc.yaml -m m
check "3: unchanged" test "$(snapshot)" = "$before"
printf 'body: j\nengine: judge\ncommand: [./tests/pass.sh]\n' >specs/j.yaml
expect_err "3: engine judge" E_FORMAT ait future offer src/stock.c specs/j.yaml -m m
rm -f specs/pc.yaml specs/j.yaml

# 4. a project file commits; a spec file is guarded
# not yet: the environment file of the scenario is replaced by the exec definition's input
echo conf2 >sub/conf.txt; git add sub/conf.txt
tick; expect_ok "4: a plain commit of a project file" git commit -qm conf2
echo '# x' >>tests/pass.sh; git add tests/pass.sh
tick; run git commit -qm guard
check "4: a plain commit of a spec file is refused" test $rc != 0
check "4: first line is E_GUARD's" sh -c "head -1 '$T/err' | grep -qF 'error: contract files cannot be changed by a plain commit:'"
git reset -q --hard

# 5. a file that is not pinned is warned about
mkdir -p tests
printf '#!/bin/sh\nexit 0\n' >tests/u.sh; chmod 755 tests/u.sh
printf 'body: u\nengine: exec\ncommand: [sh, tests/u.sh]\n' >specs/u.yaml
tick; run ait future offer src/stock.c specs/u.yaml -m m
check "5: ok" test $rc = 0
U=$(out_value new | cut -d' ' -f1)
check "5: warning: 'tests/u.sh' is not pinned" err_has "warning: 'tests/u.sh' is not pinned"
check "5: hint: add it to 'inputs'" err_has "add it to 'inputs'"
check "5: the only file row is specs/u.yaml" test "$(rec $U | grep '^file ' | cut -d' ' -f2)" = "specs/u.yaml"

# 6. arguments are normalized ('./') before comparing
tick; run ait future offer src/stock.c specs/pass.yaml -m m
check "6: shorthand definition: ok" test $rc = 0
check "6: no 'is not pinned' warning" not err_has "is not pinned"
# not yet: the `specs/cw.yaml` part reads the argument in the environment's cwd

done_testing
