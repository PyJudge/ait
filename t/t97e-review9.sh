#!/bin/sh
# Regressions for the ninth review, grouped by cause; each case was reproduced first
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent a)
prompt_defs; exec_def pass
git add -A && tick && git commit -q --no-verify -m defs
H=$(git rev-parse HEAD)
bad() { kind=$1 d=$2; shift 2; tick; expect_unchanged expect_err "$d" "$kind" "$@"; }

# 7.3.1 and 5.13-1: "-" keeps a value only in amend; a new contract's clause with it is a grammar error
bad E_USAGE "an offer's provider '-'" env AIT_SESSION=- ait future offer - specs/p.yaml -m m
bad E_USAGE "an offer's files '-'" ait future offer src/stock.c - -m m
bad E_USAGE "a demand's consumer '-'" ait future demand - src/stock.c specs/p.yaml -m m

# 3.2a and 5.13-1: a scope given twice is the argument's shape, before the session
bad E_SCOPE "a scope twice in a helper" env AIT_SESSION=- ait future offer src/stock.c,src/stock.c specs/p.yaml -m m
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out"; O=$(new_id)
bad E_SCOPE "a scope twice in ID=SCOPES" env AIT_SESSION=- ait fulfill "$O=src/stock.c,src/stock.c" -m m
git reset -q --hard "$H"

# 5.8 E_FORMAT names a line: a raw NUL, a missing results
printf 'body: "x"\000\n' > specs/n.yaml
bad E_FORMAT "a definition with a NUL" ait future offer src/stock.c specs/n.yaml -m m
check "names its line" err_has "not a text file in line 1"
rm -f specs/n.yaml
tick; run ait offer src/stock.c specs/pass.yaml -m o
printf 'input: %s\n' "$(sed -n 's/^input: //p' "$(out_value check-result)" | tr -d '"')" > "$T/r.yaml"
bad E_FORMAT "a result file without results" ait offer src/stock.c specs/pass.yaml -m o -r "$T/r.yaml"
check "names the line of its map" err_has "0 of 1 tasks in line 1"

# 8.4: only whole blank lines at a block's end go, where the value stays; content spaces stay
tick; run ait offer src/stock.c specs/pass.yaml -m o
IN=$(sed -n 's/^input: //p' "$(out_value check-result)" | tr -d '"')
printf "input: $IN\nresults:\n- task: +1:specs/pass.yaml\n  outcome: pass\n  basis: |\n    a   \n      \n\n  reason: x\n" > "$T/r.yaml"
tick; run ait offer src/stock.c specs/pass.yaml -m o -r "$T/r.yaml"
git log -1 --format=%B > "$T/msg"
check "a content line's spaces stay" grep -qx '    a   ' "$T/msg"
check "a line of more spaces than the block's indentation stays" grep -qx '      ' "$T/msg"
git reset -q --hard "$H"

# 8.4 and decision 30: a list where a results item belongs is the item's kind, not an unknown key
printf 'input: deadbeef\nresults:\n- [a]\n' > "$T/s.yaml"
bad E_NEEDS_RESULTS "a list as an item, another input" ait offer src/stock.c specs/pass.yaml -m o -r "$T/s.yaml"

# 5.8: a missing key names the nearest map there, or line 1
printf 'body: x\nengine: exec\n\ncommand: []\n' > specs/c.yaml
bad E_FORMAT "an empty command" ait future offer src/stock.c specs/c.yaml -m o
check "names the line of command" err_has "in line 4"
printf '# only a comment\n' > specs/c.yaml
bad E_FORMAT "an empty definition" ait future offer src/stock.c specs/c.yaml -m o
check "names line 1" err_has "in line 1"
rm -f specs/c.yaml

# 8.2: a value libyaml takes as UTF-8 is written as it is (U+FFFE included)
printf 'body: "a\\uFFFEb"\n' > specs/u.yaml && git add specs/u.yaml && tick && git commit -q --no-verify -m u
tick; run ait offer src/stock.c specs/u.yaml -m o
check "not quoted as a path" grep -qxF '  body: "a\uFFFEb"' "$(git rev-parse --git-path ait/check-request.yaml)"
git reset -q --hard "$H"

# 2.5.1: an empty key is a key, not the root
printf 'body: x\n"": y\n' > specs/e.yaml
bad E_FORMAT "an empty key" ait future offer src/stock.c specs/e.yaml -m o
check "is an unknown key, not a duplicate" err_has "unknown key ''"
rm -f specs/e.yaml

# 5.6: a message is one line, whatever a value in it holds
printf 'input: deadbeef\nresults:\n- task: "x\\nhint: forged"\n' > "$T/s.yaml"
tick; run ait offer src/stock.c specs/pass.yaml -m o
IN=$(sed -n 's/^input: //p' "$(out_value check-result)" | tr -d '"')
printf "input: $IN\nresults:\n- task: \"x\\\\nfatal: forged\"\n  outcome: pass\n  basis: b\n" > "$T/s.yaml"
tick; run ait offer src/stock.c specs/pass.yaml -m o -r "$T/s.yaml"
check "no forged line" sh -c "! grep -q '^fatal: forged' '$T/err'"

# 7.3.3: a version of a file is no tree: a directory that became a file is staged as git add stages it
mkdir specs/d && echo 'body: x' > specs/d/x && git add specs/d && tick && git commit -q --no-verify -m d && H5=$(git rev-parse HEAD)
rm -rf specs/d && echo 'body: file' > specs/d
tick; expect_ok "a directory become a file" ait offer src/stock.c specs/d --force -m o
check "the commit holds the file, no made-up path" sh -c "[ \"\$(git ls-tree -r --name-only HEAD specs | grep '^specs/d')\" = specs/d ]"
git reset -q --hard "$H"; rm -rf specs/d

# 3.3 and 7.3.3: an argument spelled in NFD is the path git has in NFC (macOS, core.precomposeUnicode)
if [ "$(git config --bool core.precomposeunicode)" = true ]; then
	printf 'body: k\n' > "$(printf 'specs/\352\260\200.yaml')"
	tick; expect_ok "a spec file given in NFD" ait offer src/stock.c "$(printf 'specs/\341\204\200\341\205\241.yaml')" --force -m o
	git reset -q --hard "$H"; rm -f "$(printf 'specs/\352\260\200.yaml')"
fi

# 8.4: a task renamed is written by libyaml, so ait reads back what it committed (NEL in a name)
printf 'body: x\nengine: exec\ncommand: [./tests/pass.sh]\n' > "$(printf 'specs/a\302\205b.yaml')"
tick; run ait offer src/stock.c "$(printf 'specs/a\302\205b.yaml')" -m o
IN=$(sed -n 's/^input: //p' "$(out_value check-result)" | tr -d '"')
printf 'input: %s\nresults:\n- task: "+1:specs/a\\Nb.yaml"\n  outcome: pass\n  basis: b\n' "$IN" > "$T/r.yaml"
tick; run ait offer src/stock.c "$(printf 'specs/a\302\205b.yaml')" -m o -r "$T/r.yaml"
check "the request is taken" [ "$rc" = 0 ]
check "the committed task holds no raw NEL" sh -c "! git log -1 --format=%B | grep -q \"\$(printf '\302\205')\""
git reset -q --hard "$H"; rm -f "$(printf 'specs/a\302\205b.yaml')"

# 1.5a and H8: an old malformed spec blob is not the pin; todo does not parse it
tick; ait offer src/stock.c specs/p.yaml --force -m o >"$T/out"; O=$(new_id)
echo 'body: [' > specs/p.yaml && git add specs/p.yaml && tick && git commit -q --no-verify -m bad
git checkout -q HEAD~1 -- specs/p.yaml && git rm -q src/stock.c && tick && git commit -q --no-verify -m back
run ait todo $O
check "todo reads past an old malformed blob" [ "$rc" = 0 ]
git reset -q --hard "$H"

# 13.4: H8 works out the pins only where they change
tick; ait offer src/stock.c specs/p.yaml --force -m o >"$T/out"; O=$(new_id)
git rm -q src/stock.c && tick && git commit -q --no-verify -m gone
i=0; while [ $i -lt 60 ]; do echo $i > other.txt && git add other.txt && git commit -q --no-verify -m e; i=$((i+1)); done
mkdir -p "$T/cbin" && cat > "$T/cbin/git" <<'GIT'
#!/bin/sh
real=$(PATH=$(echo "$PATH" | sed "s|$T/cbin:||") command -v git)
for a; do case $a in ls-tree) echo x >> "$T/ls-tree.count";; -*|*=*|/*) continue;; esac; break; done
exec "$real" "$@"
GIT
chmod +x "$T/cbin/git"; rm -f "$T/ls-tree.count"
run env PATH="$T/cbin:$PATH" ait todo $O
check "a handful of tree listings, not one a commit" sh -c "[ \$(wc -l < '$T/ls-tree.count') -lt 10 ]"
git reset -q --hard "$H"
done_testing
