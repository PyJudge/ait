# Test library for spec/ait-v1.1-spec.md, after git v2.53.0 t/test-lib.sh.
# A test script sources this, runs `setup`, then its checks.

AIT_DIR=$(cd "$(dirname "$0")/.." && pwd)
PATH="$AIT_DIR:$PATH"
export PATH
# Windows as git v2.53.0 t/test-lib.sh:1664-1679 runs: Windows-style paths, which
# Git Bash and the native ait both take (after PATH, where a colon would split
# them); Git Bash's own conversion off, so an argument such as '/' arrives as typed
case $(uname -s) in
*MINGW*)
	pwd() { builtin pwd -W; }
	AIT_DIR=$(cd "$AIT_DIR" && pwd)
	MSYS_NO_PATHCONV=1
	export MSYS_NO_PATHCONV
	;;
esac

n=0 failed=0
say_ok() { n=$((n+1)); echo "ok $n - $1"; }
say_fail() { n=$((n+1)); failed=$((failed+1)); echo "not ok $n - $1"; sed 's/^/#   /' "$T/out" "$T/err" 2>/dev/null | head -30; }
done_testing() {
	echo "# $failed of $n failed"
	[ "$failed" = 0 ] && rm -rf "$T"
	[ "$failed" = 0 ]
}

# what checks need from their environment (git v2.53.0 t/test-lib.sh:1732 PERL_TEST_HELPERS)
prereq_PERL() { command -v perl >/dev/null; }
# permission bits predict what the file system allows; not so for root (git v2.53.0 t/test-lib.sh:1816-1845)
prereq_SANITY() {
	mkdir "$T/sanity" && chmod -w "$T/sanity"
	! (: >"$T/sanity/x") 2>/dev/null; sane=$?
	chmod +w "$T/sanity" && rm -rf "$T/sanity"
	return $sane
}
# core.commentString came in Git 2.45 (RelNotes 2.45.0); an older Git ignores it
prereq_COMMENT_STRING() { test "$(printf 'x\n' | git -c core.commentString=';;' stripspace --comment-lines)" = ';; x'; }
# test_have_prereq PREREQ WHAT: PREREQ holds, or WHAT is reported skipped (git v2.53.0 t/test-lib.sh:1146)
test_have_prereq() {
	"prereq_$1" && return 0
	n=$((n+1)); echo "ok $n # skip $2 (missing $1)"
	return 1
}

# a repository with code and an ait branch; alice's session is the worktree's default
setup() {
	# T as ait takes it; TP as a PATH entry, which on Windows takes no drive colon
	T=$(cd "$(mktemp -d)" && pwd); TP=$(cd "$T" && builtin pwd); export T TP HOME=$T/home GIT_CONFIG_NOSYSTEM=1 GIT_EDITOR=: LANG=C LC_ALL=C; mkdir -p "$HOME"
	unset AIT_SESSION
	git config --global user.name t; git config --global user.email t@t; git config --global init.defaultBranch master
	git init -q --bare "$T/origin.git"
	git init -q "$T/r" && cd "$T/r" && git remote add origin "$T/origin.git"
	mkdir -p src
	for i in $(seq 1 50); do echo "o$i"; done > src/order.c
	for i in $(seq 1 50); do echo "s$i"; done > src/stock.c
	git add -A && git commit -qm base
	git push -q origin master
	ait init >/dev/null
	ALICE=$(ait session open --agent alice)
	printf 'body: |\n  stock never goes below zero\nengine: exec\ncommand: [sh, -c, "exit 0"]\n' > "$T/exec.yaml"
	printf 'body: |\n  orders are counted per day\n' > "$T/prompt.yaml"
	cp "$T/exec.yaml" cond.yaml
	echo /cond.yaml >> .git/info/exclude
}

# run CMD..., keeping its stdout, stderr and exit code
run() { "$@" >"$T/out" 2>"$T/err"; rc=$?; }
# check NAME RC: the last run ended with RC
check_rc() { if [ "$rc" = "$2" ]; then say_ok "$1"; else say_fail "$1 (rc $rc, want $2)"; fi; }
# has FILE PATTERN: a line of FILE (out or err) matches the basic regex
has() { grep -q -- "$2" "$T/$1"; }
check() { if eval "$2"; then say_ok "$1"; else say_fail "$1"; fi; }

# the ID of the contract of KIND letter the last run made (its "new" line)
newest() { sed -n "s/^new \($1[0-9a-f]*\).*/\1/p" "$T/out" | tail -1; }
# the contract file of ID on ait/master
yaml() { git show "ait/master:$(git ls-tree -r --name-only ait/master | grep "\(^\|/\)$1\.yaml$")"; }
# fill the result template: every task passes
pass_all() {
	res=$(git rev-parse --absolute-git-dir)/ait/check-result.yaml
	sed -i.bak 's/^  outcome:$/  outcome: pass/; s/^  basis: |$/  basis: |\n    checked/' "$res"
	echo "$res"
}

# a git that first runs "$T/before-SUB" once when it is about to run SUB (update-ref, commit-tree, ...):
# a deterministic race at that point; `run env PATH="$TP/bin:$PATH" ait ...` uses it
racing_git() {
	mkdir -p "$T/bin"
	cat > "$T/bin/git" <<'GIT'
#!/bin/sh
real=$(PATH=$(echo "$PATH" | sed "s|$TP/bin:||") command -v git)
for a; do
	case $a in -*|*=*|/*|[A-Za-z]:/*) continue ;; esac
	f="$T/before-$a"
	[ -f "$f" ] && { mv "$f" "$f.done"; sh "$f.done" || exit 1; }
	break
done
exec "$real" "$@"
GIT
	chmod +x "$T/bin/git"
}
