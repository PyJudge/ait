#!/bin/sh
# Regressions for the seventh review, grouped by cause; each case was reproduced first
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent a)
prompt_defs; exec_def pass
git add -A && tick && git commit -q --no-verify -m defs
H=$(git rev-parse HEAD)
bad() { kind=$1 d=$2; shift 2; tick; expect_unchanged expect_err "$d" "$kind" "$@"; }

# 7.3.3: the index holds HEAD's version or the work tree's, mode and blob, absence being a version
chmod +x specs/p.yaml && git add specs/p.yaml && echo 'body: new' > specs/p.yaml
bad E_INCOMPLETE "a staged mode with an unstaged edit" ait future offer src/stock.c specs/p.yaml -m o
git reset -q --hard "$H"
git rm -q --cached specs/p.yaml && echo 'body: new' > specs/p.yaml
bad E_INCOMPLETE "a staged deletion with the file in the work tree" ait future offer src/stock.c specs/p.yaml -m o
git reset -q --hard "$H"

# 5.8: git failing while it stages is E_GIT, not a mismatch of the spec files
echo 'specs/p.yaml filter=broken' > .gitattributes
git config filter.broken.clean 'exit 42' && git config filter.broken.required true
echo 'body: new' > specs/p.yaml
bad E_GIT "a required clean filter that fails" ait future offer src/stock.c specs/p.yaml -m o
check "names the git command" err_has "git failed: 'git add'"
git config --unset filter.broken.clean; git config --unset filter.broken.required; rm -f .gitattributes; git reset -q --hard "$H"

# 5.13-1 and 7.7.1: fulfill without an ID is a grammar error before the session
run env AIT_SESSION=- ait fulfill -m m
check "E_USAGE before the session" sh -c "[ $rc = 129 ] && grep -q 'nothing to fulfill' '$T/err'"

# 2.5.1: where a tab may stand is YAML's
printf 'body: "x\ty"\n' > specs/t.yaml
tick; expect_ok "a tab inside a quoted scalar" ait future offer src/stock.c specs/t.yaml -m o
git reset -q --hard "$H"; rm -f specs/t.yaml

# 5.8 E_FORMAT names the file and the line
printf 'body: x\nowner: me\n' > specs/f.yaml
bad E_FORMAT "an unknown key" ait future offer src/stock.c specs/f.yaml -m o
check "with its line" err_has "unknown key 'owner' in line 2"
rm -f specs/f.yaml

# 5.8 E_FORMAT of a result file always carries its hint
tick; run ait offer src/stock.c specs/pass.yaml -m o
printf 'input: [\n' > "$T/r.yaml"
bad E_FORMAT "a result file that is no YAML" ait offer src/stock.c specs/pass.yaml -m o -r "$T/r.yaml"
check "with the result file's hint" err_has "fix '$T/r.yaml': every task needs an outcome"

# 5.5-4/5: what can fail is done before the commit is published
echo z >> src/order.c && git add src/order.c
tick; run env TMPDIR="$T/nonexistent" ait commit -m x
check "a temporary directory that cannot be made stops before the commit" sh -c "[ $rc = 128 ] && [ \$(git rev-parse HEAD) = $H ]"
git reset -q --hard "$H"
tick; ait future offer src/stock.c specs/p.yaml -m o >/dev/null
check "git sees the followed files as up to date" git diff-files --quiet
git reset -q --hard "$H"

# 5.12: git's output as ait parses it, whatever the config adds (log.showSignature)
if command -v ssh-keygen >/dev/null; then
	ssh-keygen -q -t ed25519 -N '' -f "$T/k" && echo "t@t $(cat "$T/k.pub")" > "$T/allowed"
	git config gpg.format ssh && git config user.signingkey "$T/k.pub" && git config gpg.ssh.allowedSignersFile "$T/allowed"
	tick; ait offer src/stock.c specs/p.yaml --force -m o >"$T/out"; O=$(new_id)
	git config commit.gpgSign true && git config log.showSignature true
	git rm -q src/stock.c && tick && git commit -q -m gone
	expect_ok "todo with signed commits shown with their signatures" ait todo $O
	git config --unset commit.gpgSign; git config --unset log.showSignature; git reset -q --hard "$H"
fi

# 7.1.6 and 5.6: an object that is no commit is not checked, quietly; a message is one line
TT=$(printf 'object %s\ntype tree\ntag tt\ntagger t <t@t> 0 +0000\n\nx\n' "$(git rev-parse HEAD^{tree})" | git mktag)
Z=0000000000000000000000000000000000000000
run sh -c "printf 'refs/tags/tt %s refs/tags/tt %s\n' $TT $Z | ait hook pre-push"
check "a tag on a tree passes without git's note" sh -c "[ $rc = 0 ] && [ ! -s '$T/err' ]"
run sh -c "printf 'garbage\nmore\n' | ait hook pre-push"
check "bad input in one line" sh -c "[ $rc = 128 ] && [ \$(wc -l < '$T/err') = 1 ]"

# 8.4: +SEQ becomes the ID in place where the text spells the value, else the scalar is written anew
tick; run ait offer src/stock.c specs/pass.yaml -m o
W=$(out_value check-result); IN=$(sed -n 's/^input: //p' "$W" | tr -d '"')
for form in "\357\273\277input: $IN\nresults:\n- task: +1:specs/pass.yaml\n" "input: $IN\nresults:\n- task: |-\n    +1:specs/pass.yaml\n" \
	"input: $IN\nresults:\n- task: \"\\\\x2b1:specs/pass.yaml\"\n"; do
	printf "$form  outcome: pass\n  basis: |\n    seen\n\n\n" > "$T/r.yaml"
	tick; run ait offer src/stock.c specs/pass.yaml -m o -r "$T/r.yaml"; O=$(new_id)
	check "renamed: $(printf "$form" | sed -n 3p)" sh -c "git log -1 --format=%B | grep -Eq '^- task: \"?$O:specs/pass.yaml\"?\$'"
	check "and the block's trailing blank lines left out" sh -c "git log -1 --format=%B | grep -A2 '^    seen\$' | sed -n 3p | grep -q '^Ait-Request'"
	git reset -q --hard "$H"
done

# 2.5.1: a tag ("!") is a tag; LF is the only line break; integers are plain decimals
for d in 'body: ! x\n' 'body: a\302\205  b\n' 'body: x\nengine: exec\ncommand: [sh]\nsuccess: ["0"]\n' 'body: x\nengine: exec\ncommand: [sh]\nsuccess: [010]\n'; do
	printf "$d" > specs/f.yaml
	bad E_FORMAT "refused: $(printf "$d" | tr '\n' ' ')" ait future offer src/stock.c specs/f.yaml -m o
done
rm -f specs/f.yaml

# 8.4: the subset, unknown keys too, before the input is compared
printf 'input: 0123456789012345678901234567890123456789\nnote: x\nresults: []\n' > "$T/r.yaml"
bad E_FORMAT "an unknown key in a result file of another input" ait offer src/stock.c specs/pass.yaml -m o -r "$T/r.yaml"

# 7.3.3: what is there but is no file is not a regular file
mkfifo tests/ff && printf 'body: x\ninputs: [tests/ff]\n' > specs/ff.yaml
bad E_INCOMPLETE "a FIFO as an input" ait future offer src/stock.c specs/ff.yaml -m o
rm -f tests/ff specs/ff.yaml

# 8.2: a path that is not UTF-8 is written in the check files as git shows it (core.quotePath)
f=$(printf 'src/\377x.c'); B=$(echo x | git hash-object -w --stdin)
git update-index --add --cacheinfo 100644,$B,"$f" && tick && git commit -q --no-verify -m nonutf
tick; run ait offer "$f" specs/pass.yaml -m o
check "the check request quotes it" grep -qF 'provider: "\"src/\\377x.c\""' "$(git rev-parse --git-path ait/check-request.yaml)"
fill "$(out_value check-result)" pass
tick; run ait offer "$f" specs/pass.yaml -m o -r "$(git rev-parse --git-path ait/check-result.yaml)"
check "and the filled result is taken" sh -c "[ $rc = 0 ] && grep -q '^check ' '$T/out'"
git reset -q --hard "$H"

# 7.3.3 and 3.3: a path that differs only in case from one git has is refused, as git add stages nothing for it
if [ "$(git config --bool core.ignorecase)" = true ]; then
	bad E_INCOMPLETE "a spec file in another case" ait future offer src/stock.c SPECS/P.yaml -m o
	check "names git's path" err_has "give it as git has it: 'specs/p.yaml'"
	bad E_SCOPE "a scope in another case" ait future offer SRC/stock.c specs/p.yaml -m o
	bad E_SCOPE "a new scope in another case" ait future offer new:SRC/stock.c specs/p.yaml -m o
fi

# H8: the commit where it became unverified, however far back
tick; ait offer src/stock.c specs/pass.yaml --force -m o >"$T/out"; O=$(new_id)
git rm -q src/stock.c && tick && git commit -q -m gone && G=$(git rev-parse HEAD)
i=0; while [ $i -lt 105 ]; do git commit -q --allow-empty -m e; i=$((i+1)); done
run ait todo $O
check "unverified since, past a hundred commits" err_has "(unverified since '$G')"
git reset -q --hard "$H"
done_testing
