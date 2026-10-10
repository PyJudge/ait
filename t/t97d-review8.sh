#!/bin/sh
# Regressions for the eighth review, grouped by cause; each case was reproduced first
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent a)
prompt_defs; exec_def pass
git add -A && tick && git commit -q --no-verify -m defs
H=$(git rev-parse HEAD)
bad() { kind=$1 d=$2; shift 2; tick; expect_unchanged expect_err "$d" "$kind" "$@"; }
# a filled result file for "ait offer src/stock.c specs/pass.yaml", the items after the first task's outcome given
result() {
	tick; run ait offer src/stock.c specs/pass.yaml -m o
	IN=$(sed -n 's/^input: //p' "$(out_value check-result)" | tr -d '"')
	printf "input: $IN\nresults:\n- task: +1:specs/pass.yaml\n  outcome: pass\n$1" > "$T/r.yaml"
}

# 8.4: a "|" block loses its trailing blank lines where its value stays; nothing else of the file changes
result '  basis: |\n    first\n\n      deeper\n    final\n\n\n  reason: "a\n\n  b"\n'
tick; run ait offer src/stock.c specs/pass.yaml -m o -r "$T/r.yaml"
git log -1 --format=%B > "$T/msg"
check "the block's inner blank line stays" sh -c "grep -A2 '^    first\$' '$T/msg' | sed -n 2p | grep -qx ''"
check "its trailing ones go" sh -c "grep -A1 '^    final\$' '$T/msg' | sed -n 2p | grep -qx '  reason: \"a'"
check "a quoted value keeps its lines" sh -c "grep -A2 '^  reason: \"a\$' '$T/msg' | sed -n 2p | grep -qx ''"
git reset -q --hard "$H"
result '  basis: |+\n    kept\n\n'
tick; run ait offer src/stock.c specs/pass.yaml -m o -r "$T/r.yaml"
check "a \"|+\" block keeps them, they are its value" sh -c "git log -1 --format=%B | grep -A1 '^    kept\$' | sed -n 2p | grep -qx ''"
git reset -q --hard "$H"

# 8.4: every result item names its task
result '  basis: ok\n- outcome: fail\n  reason: x\n'
bad E_FORMAT "a result without a task" ait offer src/stock.c specs/pass.yaml -m o -r "$T/r.yaml"
check "with its line and the hint" sh -c "grep -q 'a result without a task in line 6' '$T/err' && grep -q '^hint: fix ' '$T/err'"

# 5.8: E_FORMAT of a result file always carries its hint, and every E_FORMAT a line
printf 'input: "x"\000\nresults: []\n' > "$T/n.yaml"
bad E_FORMAT "a result file with a NUL" ait offer src/stock.c specs/pass.yaml -m o -r "$T/n.yaml"
check "with the hint" err_has "hint: fix '$T/n.yaml'"
printf 'results: []\n' > "$T/n.yaml"
bad E_FORMAT "a result file without input" ait offer src/stock.c specs/pass.yaml -m o -r "$T/n.yaml"
check "names the line of the map" err_has "no 'input' in line 1"
printf '# x\n{}\n' > specs/e.yaml
bad E_FORMAT "a definition without body" ait future offer src/stock.c specs/e.yaml -m o
check "names the line of the map" err_has "in line 2"
rm -f specs/e.yaml

# 5.13-1: the shapes of arguments come before the session
run env AIT_SESSION=S000000000000 ait todo /bad
check "todo: a bad scope before a bad session" sh -c "[ $rc = 128 ] && grep -q 'bad scope' '$T/err'"
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out"; O=$(new_id)
run env AIT_SESSION=- ait fulfill "$O=-" -m m
check "fulfill: '-' is no scope, before the session" sh -c "[ $rc = 128 ] && grep -q \"bad scope: '-'\" '$T/err'"
git reset -q --hard "$H"

# 5.9a: a spec file staged apart from HEAD is to be staged again once fixed
tick; ait offer specs/p.yaml specs/p.yaml --force -m o >"$T/out"; O=$(new_id)
echo 'body: changed' > specs/p.yaml && git add specs/p.yaml
tick; run ait amend $O - specs/p.yaml -m a; fill "$(out_value check-result)" fail
tick; run ait amend $O - specs/p.yaml -m a -r "$(git rev-parse --git-path ait/check-result.yaml)"
check "the hint says to stage it again" err_has "fix 'specs/p.yaml' and stage it again, then rerun this command"
git reset -q --hard "$H"

# 5.5-5: a refresh that fails after the commit is warned about
mkdir -p "$T/lockbin"
cat > "$T/lockbin/git" <<'GIT'
#!/bin/sh
real=$(PATH=$(echo "$PATH" | sed "s|$T/lockbin:||") command -v git)
for a; do [ "$a" = --refresh ] && { : > "$T/r/.git/index.lock"; "$real" "$@"; rc=$?; rm -f "$T/r/.git/index.lock"; exit $rc; }; done
exec "$real" "$@"
GIT
chmod +x "$T/lockbin/git"
tick; run env PATH="$T/lockbin:$PATH" ait future offer src/stock.c specs/p.yaml -m o
check "the request succeeds with the warning of 5.5-5" sh -c "[ $rc = 0 ] && grep -q '^warning: committed .* but the index and work tree were not updated' '$T/err' && grep -q '^hint: bring ' '$T/err'"
git reset -q --hard "$H"

# H8, 9.1: the commit where it newly became unverified, over every parent
tick; ait offer src/stock.c specs/pass.yaml --force -m o >"$T/out"; O=$(new_id)
git checkout -q -b side && git rm -q src/stock.c && tick && git commit -q --no-verify -m gone && G=$(git rev-parse HEAD)
git checkout -q master && tick && git commit -q --allow-empty -m pad && tick && git merge -q --no-ff --no-verify -m merge side 2>/dev/null
run ait todo $O
check "since names the commit on the side branch" err_has "(unverified since '$G')"
git reset -q --hard "$H"

# 7.3.3: spec files are staged as git add stages them, over the index's own versions
git config core.fileMode false && git update-index --chmod=+x specs/p.yaml
tick; expect_ok "a mode staged with core.fileMode false" ait offer src/stock.c specs/p.yaml --force -m o
check "is committed" sh -c "git ls-tree HEAD specs/p.yaml | grep -q '^100755'"
git config --unset core.fileMode; git reset -q --hard "$H"
echo 'body: x' > specs/x && git add specs/x && tick && git commit -q --no-verify -m x && H3=$(git rev-parse HEAD)
rm specs/x && mkdir specs/x && echo 'body: y' > specs/x/y.yaml
tick; expect_ok "a file that became a directory" ait offer src/stock.c specs/x/y.yaml --force -m o
git reset -q --hard "$H"; rm -rf specs/x
git sparse-checkout set --no-cone '/src/' '/tests/' '/specs/p.yaml' '/specs/pass.yaml'
echo 'body: q' > specs/q.yaml
bad E_GIT "a spec file outside the sparse checkout" ait offer src/stock.c specs/q.yaml --force -m o
check "stays in the work tree" [ -f specs/q.yaml ]
git sparse-checkout disable; rm -f specs/q.yaml; git reset -q --hard "$H"

# 5.12: git's output as ait parses it whatever i18n.logOutputEncoding says, and git's messages in English
tick; ait offer src/stock.c specs/pass.yaml --force -m o >"$T/out"; O=$(new_id)
git config i18n.logOutputEncoding UTF-16
run ait todo $O
check "a check is read under another log encoding" sh -c "! grep -q 'unverified yes' '$T/out'"
git config --unset i18n.logOutputEncoding; git reset -q --hard "$H"
echo 'specs/p.yaml filter=broken' > .gitattributes && git config filter.broken.clean false && git config filter.broken.required true
echo 'body: new' > specs/p.yaml
tick; run env LANG=de_DE.UTF-8 LC_ALL=de_DE.UTF-8 ait offer src/stock.c specs/p.yaml --force -m o
check "git's reason untranslated" sh -c "grep -q \"^fatal: git failed: 'git add': external filter\" '$T/err'"
git config --unset filter.broken.clean; git config --unset filter.broken.required; rm -f .gitattributes; git reset -q --hard "$H"

# 8.3: a submodule's commit is a change whatever submodule.<name>.ignore says
git init -q "$T/sub" && git -C "$T/sub" commit -q --allow-empty -m s1
git -c protocol.file.allow=always submodule add -q "$T/sub" sub >/dev/null 2>&1
git config -f .gitmodules submodule.sub.ignore all && git add .gitmodules && tick && git commit -q --no-verify -m sub
tick; ait offer src/stock.c specs/pass.yaml --force -m o >/dev/null; H4=$(git rev-parse HEAD)
echo x >> src/stock.c && git add src/stock.c
tick; run ait commit -m c; I1=$(sed -n 's/^input: //p' "$(git rev-parse --git-path ait/check-request.yaml)")
git -C sub commit -q --allow-empty -m s2 && git add sub
tick; run ait commit -m c; I2=$(sed -n 's/^input: //p' "$(git rev-parse --git-path ait/check-request.yaml)")
check "the submodule's change is in the check input" [ "$I1" != "$I2" ]
git reset -q --hard "$H"; rm -rf sub .gitmodules

# 7.3.3 and decision 5: a case twin in HEAD or the index is refused; when the two spell it apart, they are to agree
if [ "$(git config --bool core.ignorecase)" = true ]; then
	git mv specs/p.yaml specs/P.yaml
	bad E_INCOMPLETE "HEAD and the index spell it apart" ait offer src/stock.c specs/P.yaml --force -m o
	check "the hint asks for one spelling, not the other one" err_has "spell it the same in HEAD and the index"
	git reset -q --hard "$H"
fi

# 5.8: E_IO for a temporary directory that cannot be made; the missing hint names what declares the path
tick; run env TMPDIR="$T/nonexistent" ait future offer src/stock.c specs/p.yaml -m o
check "write error" sh -c "[ $rc = 128 ] && grep -q '^fatal: write error:' '$T/err'"
git rm -q --cached tests/pass.sh && rm tests/pass.sh
bad E_INCOMPLETE "a missing input" ait offer src/stock.c specs/pass.yaml --force -m o
check "restore it, or stop declaring it in its definition" err_has "restore 'tests/pass.sh' from HEAD, or stop declaring it in 'specs/pass.yaml'"
git reset -q --hard "$H"

# 5.8: a reader error names its line
printf 'body: b\n\ninputs: [a\001]\n' > specs/c.yaml
bad E_FORMAT "a control character" ait future offer src/stock.c specs/c.yaml -m o
check "in its line" err_has "in line 3"
rm -f specs/c.yaml

# 8.2: what ait writes, its reader reads: LS and PS in a body are escaped
printf 'body: "x\\Ly"\n' > specs/ls.yaml && git add specs/ls.yaml && tick && git commit -q --no-verify -m ls
tick; run ait offer src/stock.c specs/ls.yaml -m o
check "the body goes as a quoted string" grep -qF 'body: "x\Ly"' "$(git rev-parse --git-path ait/check-request.yaml)"
git reset -q --hard "$H"

# 8.4: with another input, the kinds of values are not checked; the result is not for this state
result '  basis: ok\n'
sed 's/^input: .*/input: 0123456789012345678901234567890123456789/; s/^  outcome: pass$/  outcome: [pass]/' "$T/r.yaml" > "$T/s.yaml"
bad E_NEEDS_RESULTS "a stale result with a list as outcome" ait offer src/stock.c specs/pass.yaml -m o -r "$T/s.yaml"

# 5.4: ait's trailer keys are read and written as they are, whatever trailer.<token>.key says (user decision)
git config trailer.ait-check.key X-Check
tick; expect_ok "a request with a check under a trailer alias" ait offer src/stock.c specs/pass.yaml --force -m o
O=$(new_id)
run ait todo $O
check "and its check is read" sh -c "! grep -q 'unverified yes' '$T/out'"
git config --unset trailer.ait-check.key; git reset -q --hard "$H"
done_testing
