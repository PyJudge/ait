#!/bin/sh
# Regressions for judgments made in several places and merged into one; each failed before
. "$(dirname "$0")/lib.sh"
setl() { f=$1 l=$2; shift 2; awk -v n="$l" -v t="$*" 'NR==n{print t; next}1' "$f" >"$f.tmp" && cat "$f.tmp" >"$f" && rm "$f.tmp"; }
not() { ! "$@"; }
fails() { [ "$rc" = "$1" ] && grep -qF -- "$2" "$T/err"; }
prep() {
	setup
	A=$(ait session open --agent a)
	prompt_defs
	git add -A && tick && git commit -q --no-verify -m defs
}

# 2.1.4 (choice 257): a spec file beyond a symbolic link is missing, as one spec-file judgment says for every entry
prep
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out" 2>"$T/err"; K=$(sed -n 's/^new \([^ ]*\).*/\1/p' "$T/out")
mv specs real; ln -s real specs
echo 'body: changed' > real/p.yaml
run ait contract add ".ait/$K"
check "contract add: the spec file is missing, not read through the link" sh -c "[ $rc = 1 ] && grep -qx 'missing specs/p.yaml' '$T/out'"
check "with the restore hint" err_has "hint: restore 'specs/p.yaml' from HEAD, or stop declaring it in '.ait/$K'"
run ait contract commit -m w
check "contract commit does not crash either" test "$rc" -lt 128

done_testing
