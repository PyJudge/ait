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
check "the request succeeds with the warning" sh -c "[ $rc = 0 ] && grep -q \"^warning: committed .* but git failed: 'git update-index'\" '$T/err'"
git reset -q --hard "$H"

# H8, 9.1: the commit where it newly became unverified, over every parent
tick; ait offer src/stock.c specs/pass.yaml --force -m o >"$T/out"; O=$(new_id)
git checkout -q -b side && git rm -q src/stock.c && tick && git commit -q --no-verify -m gone && G=$(git rev-parse HEAD)
git checkout -q master && tick && git commit -q --allow-empty -m pad && tick && git merge -q --no-ff --no-verify -m merge side 2>/dev/null
run ait todo $O
check "since names the commit on the side branch" err_has "(unverified since '$G')"
git reset -q --hard "$H"
done_testing
