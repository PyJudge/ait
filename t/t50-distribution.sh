#!/bin/sh
# A release carries the current executable, skill and working repository setup.
. "$(dirname "$0")/lib.sh"
setup
# the installer replaces a symlink and restores the execute bit; Windows has neither
test_have_prereq POSIXPERM "the release installs this build" || { done_testing; exit $?; }
test_have_prereq SYMLINKS "the release installs this build" || { done_testing; exit $?; }
make -C "$AIT_DIR" bundle >"$T/build" 2>&1 || { cat "$T/build"; exit 1; }
mkdir "$T/unpack"
tar -xzf "$AIT_DIR/dist/ait.tar.gz" -C "$T/unpack" || exit 1
package="$T/unpack/ait"
check 'the release has an explicit v1.1 version' "test -f '$package/VERSION' && grep -qx 1.1 '$package/VERSION' && test -f '$AIT_DIR/dist/ait-v1.1.tar.gz'"
prefix="$T/install prefix"
run env PREFIX="$prefix" sh "$package/install.sh"
check_rc 'install the executable in a path containing spaces' 0
check 'the installed executable matches this build' "cmp -s '$AIT_DIR/ait' '$prefix/bin/ait'"
check 'installation includes the skill and repository helper scripts' "cmp -s '$package/skills/ait/SKILL.md' '$prefix/share/ait/skills/ait/SKILL.md' && test -f '$prefix/share/ait/setup.sh' && test -f '$prefix/share/ait/try.sh'"
run env PREFIX="$prefix" sh "$package/install.sh"
check_rc 'installing the same release twice succeeds' 0
check 'the same ait is installed again without a warning' "! has err 'warning:'"
chmod 644 "$prefix/bin/ait"
run env PREFIX="$prefix" sh "$package/install.sh"
check 'installation restores execution permission for identical bytes' "test $rc = 0 && test -x '$prefix/bin/ait' && cmp -s '$AIT_DIR/ait' '$prefix/bin/ait'"
run env PREFIX="$package" sh "$package/install.sh"
check_rc 'installing over the extracted release itself succeeds' 0
mkdir -p "$T/link prefix/bin"
printf 'keep target\n' >"$T/link-target"
ln -s "$T/link-target" "$T/link prefix/bin/ait"
run env PREFIX="$T/link prefix" sh "$package/install.sh"
check 'installation replaces a link without changing its target' "test $rc = 0 && grep -qx 'keep target' '$T/link-target' && test ! -L '$T/link prefix/bin/ait' && cmp -s '$AIT_DIR/ait' '$T/link prefix/bin/ait'"
check 'replacing another ait warns that its hooks now run this one' "has err \"warning: replacing the ait at '$T/link prefix/bin/ait'\""
cp "$AIT_DIR/ait" "$T/link-same-target"
chmod 755 "$T/link-same-target"
rm "$T/link prefix/bin/ait"
ln -s "$T/link-same-target" "$T/link prefix/bin/ait"
run env PREFIX="$T/link prefix" sh "$package/install.sh"
check 'identical linked binaries are replaced without changing their target' "test $rc = 0 && test ! -L '$T/link prefix/bin/ait' && cmp -s '$AIT_DIR/ait' '$T/link-same-target' && test -x '$T/link-same-target'"
export PATH="$prefix/bin:$PATH"
git init -q "$T/empty repo"
run sh "$package/setup.sh" "$T/empty repo"
check_rc 'setup accepts an unborn code branch' 0
check 'setup with this release first on PATH warns nothing' "! has err 'warning:'"
check 'setup creates contracts without a code commit or .ait/init' "git -C '$T/empty repo' rev-parse --verify ait/master >/dev/null && ! git -C '$T/empty repo' rev-parse --verify HEAD >/dev/null 2>&1 && test ! -e '$T/empty repo/.ait/init'"
check 'both agent runtimes receive the current skill and entry point' "cmp -s '$package/skills/ait/SKILL.md' '$T/empty repo/.agents/skills/ait/SKILL.md' && cmp -s '$package/skills/ait/SKILL.md' '$T/empty repo/.claude/skills/ait/SKILL.md' && test -f '$T/empty repo/AGENTS.md' && test -f '$T/empty repo/CLAUDE.md'"
mkdir "$T/other-ait"
printf '#!/bin/sh\nexit 0\n' >"$T/other-ait/ait"
chmod 755 "$T/other-ait/ait"
git init -q "$T/other repo"
run env PATH="$T/other-ait:$PATH" sh "$package/setup.sh" "$T/other repo"
check 'setup warns when the agents would run another ait' "test $rc = 0 && has err \"warning: the ait on PATH ('$T/other-ait/ait') is not this release's\""
printf 'staged\n' >>src/stock.c
git add src/stock.c
head_before=$(git rev-parse HEAD)
index_before=$(git write-tree)
run sh "$prefix/share/ait/setup.sh" "$T/r"
check_rc 'setup works in an existing repository with staged code' 0
check 'setup preserves code HEAD and the user index' "test \"\$(git rev-parse HEAD)\" = $head_before && test \"\$(git write-tree)\" = $index_before"
run sh "$package/setup.sh" "$T/r"
check 'repeated setup does not duplicate entry sections' "test $rc = 0 && test \"\$(grep -c '^## ait$' AGENTS.md 2>/dev/null)\" = 1 && test \"\$(grep -c '^## ait$' CLAUDE.md)\" = 1"
printf 'keep skill\n' >.claude/skills/ait/SKILL.md
cp .claude/skills/ait/SKILL.md "$T/custom-skill"
cp CLAUDE.md "$T/custom-entry"
run env AIT_SESSION=invalid sh "$package/setup.sh" "$T/r"
check 'failed initialization preserves existing agent instructions' "test $rc = 128 && cmp -s .claude/skills/ait/SKILL.md '$T/custom-skill' && cmp -s CLAUDE.md '$T/custom-entry'"
run sh "$prefix/share/ait/try.sh" "$T/play ground"
check_rc 'the playground is created in a path containing spaces' 0
check 'both code worktrees inherit the current skill and agent entry points' "cmp -s '$package/skills/ait/SKILL.md' '$T/play ground/a/.agents/skills/ait/SKILL.md' && cmp -s '$package/skills/ait/SKILL.md' '$T/play ground/b/.claude/skills/ait/SKILL.md' && test -f '$T/play ground/a/AGENTS.md' && test -f '$T/play ground/b/CLAUDE.md'"
cd "$T/play ground/a" || exit 1
run ait session open --agent a
check_rc 'a playground agent can start its own session' 0
ait contract new spec cond.yaml exec >/dev/null
printf 'body: stock contract\nengine: exec\ncommand: [sh, -c, "exit 0"]\n' >cond.yaml
run ait future offer doc/spec.md cond.yaml -m 'distribution smoke'
check_rc 'the distributed instructions can create a contract' 0
O=$(newest O)
run sh "$prefix/share/ait/ait-graph.sh"
check 'the graph includes orphan contract branch history' "test $rc = 0 && has out 'distribution smoke'"
run ait status doc/spec.md
check 'the documented status command shows the contract' "test $rc = 0 && has out '^id $O$'"
run ait fulfill "$O" --force -m 'distribution smoke'
check_rc 'the installed executable records a fulfilment' 0
# Replay the spec-only guide from abstract promises through checked fulfilment.
cd "$T/play ground/b" || exit 1
ait session open --agent b >/dev/null
printf 'body: distribution smoke\nengine: exec\ncommand: [sh, src/stock.sh]\n' >cond.yaml
run ait future demand doc/spec.md / cond.yaml as d future offer / cond.yaml as o undertake d o -m abstract
check_rc 'prepare the abstract demand and whole offer from the guide' 0
D=$(newest D); O=$(newest O)
mkdir -p src
printf '#!/bin/sh\nexit 0\n' >src/stock.sh
git add src/stock.sh
run ait fulfill "$O=src/stock.sh" "$D=src/stock.sh" -m concrete
check_rc 'narrowing both abstract providers requests check results' 1
sh src/stock.sh || exit 1
result=$(pass_all)
run ait fulfill "$O=src/stock.sh" "$D=src/stock.sh" -r "$result" -m concrete
check_rc 'checked fulfilment from the guide completes both contracts' 0
# Prerequisites are checked before changing an installation.
REAL_GIT=$(command -v git); export REAL_GIT
mkdir "$T/old-git"
cat >"$T/old-git/git" <<'GIT'
#!/bin/sh
if [ "$1" = version ]; then echo 'git version 2.37.0'; else exec "$REAL_GIT" "$@"; fi
GIT
chmod +x "$T/old-git/git"
run env PATH="$T/old-git:$PATH" PREFIX="$T/old-install" sh "$package/install.sh"
check 'an unsupported Git version creates no installation' "test $rc = 128 && test ! -e '$T/old-install' && has err 'git 2.38 or later'"
cat >"$T/old-git/git" <<'GIT'
#!/bin/sh
exit 1
GIT
run env PATH="$T/old-git:$PATH" PREFIX="$T/failed-git-install" sh "$package/install.sh"
check 'a failed Git version query creates no installation' "test $rc = 128 && test ! -e '$T/failed-git-install'"
printf '#!/bin/sh\nexit 0\n' >"$T/old-git/git"
run env PATH="$T/old-git:$PATH" PREFIX="$T/empty-git-install" sh "$package/install.sh"
check 'an empty Git version response creates no installation' "test $rc = 128 && test ! -e '$T/empty-git-install'"
cp "$package/PLATFORM" "$T/platform"
printf 'different platform\n' >"$package/PLATFORM"
run env PREFIX="$T/wrong-platform" sh "$package/install.sh"
cp "$T/platform" "$package/PLATFORM"
check 'a wrong platform creates no installation' "test $rc = 128 && test ! -e '$T/wrong-platform'"
run make -C "$AIT_DIR" install "prefix=$T/make prefix"
check 'make install uses the same complete installer' "test $rc = 0 && cmp -s '$AIT_DIR/ait' '$T/make prefix/bin/ait' && cmp -s '$package/skills/ait/SKILL.md' '$T/make prefix/share/ait/skills/ait/SKILL.md'"
done_testing
