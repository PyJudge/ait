# Test library for spec/ait-phase1-scenarios.md, after git v2.53.0 t/test-lib.sh.
# A test script sources this, then runs `setup` (0.1) and its checks.

AIT_DIR=$(cd "$(dirname "$0")/.." && pwd)
PATH="$AIT_DIR:$PATH"
export PATH

n=0 failed=0
say_ok() { n=$((n+1)); echo "ok $n - $1"; }
say_fail() { n=$((n+1)); failed=$((failed+1)); echo "not ok $n - $1"; sed 's/^/#   /' "$T/out" "$T/err" 2>/dev/null | head -20; }
done_testing() {
	echo "# $failed of $n failed"
	[ "$failed" = 0 ] && rm -rf "$T"
	[ "$failed" = 0 ]
}

# 0.1 fixture
setup() {
	T=$(mktemp -d); export T HOME=$T/home GIT_CONFIG_NOSYSTEM=1 GIT_EDITOR=:; mkdir -p "$HOME"
	unset AIT_SESSION
	git config --global user.name t; git config --global user.email t@t; git config --global init.defaultBranch master
	N=0
	git init -q --bare "$T/origin.git"
	git init -q "$T/r" && cd "$T/r" && git remote add origin "$T/origin.git"
	mkdir src; for i in $(seq 1 50); do echo "o$i"; done > src/order.c
	for i in $(seq 1 50); do echo "s$i"; done > src/stock.c
	tick; git add -A && git commit -qm base
	tick; ait init >/dev/null
	git push -q origin master
}
tick() { N=$((N+60)); export GIT_COMMITTER_DATE="@$((1700000000+N)) +0000" GIT_AUTHOR_DATE="@$((1700000000+N)) +0000"; }

# the prompt and exec definitions of 0.1; `exec_def X` makes specs/X.yaml and tests/X.sh (0.2)
prompt_defs() {
	mkdir -p specs
	echo 'body: 재고가 0이면 예약 요청을 거절한다.' > specs/p.yaml
	echo 'body: 최저가가 보장될 것.' > specs/low.yaml
	echo 'body: 주문 집계는 하루 단위다.' > specs/w.yaml
}
exec_def() {
	mkdir -p specs tests
	case $1 in
	pass) printf '#!/bin/sh\nexit 0\n' ;;
	guard) printf '#!/bin/sh\ngrep -q guard src/stock.c\n' ;;
	nobroken) printf '#!/bin/sh\n! grep -rq BROKEN src/\n' ;;
	long) printf '#!/bin/sh\n'; for i in $(seq 2 11); do echo "# l$i"; done; echo 'exit 0' ;;
	lowest) printf '#!/bin/sh\ngrep -q lowest src/stock.c\n' ;;
	highest) printf '#!/bin/sh\ngrep -q highest src/stock.c\n' ;;
	both) printf '#!/bin/sh\ngrep -q lowest src/stock.c && grep -q highest src/stock.c\n' ;;
	esac > "tests/$1.sh"
	chmod 755 "tests/$1.sh"
	printf 'body: %s\nengine: exec\ncommand: [./tests/%s.sh]\ninputs: [tests/%s.sh]\n' "$1" "$1" "$1" > "specs/$1.yaml"
}

# a git that first runs "$T/before-SUB" once when it is about to run SUB (commit-tree, update-ref, ...):
# a deterministic race or failure at that point; `run env PATH="$T/bin:$PATH" ait ...` uses it
racing_git() {
	mkdir -p "$T/bin"
	cat > "$T/bin/git" <<'GIT'
#!/bin/sh
real=$(PATH=$(echo "$PATH" | sed "s|$T/bin:||") command -v git)
for a; do
	case $a in -*|*=*|/*) continue ;; esac
	f="$T/before-$a"
	[ -f "$f" ] && { mv "$f" "$f.done"; sh "$f.done" || exit 1; }
	break
done
exec "$real" "$@"
GIT
	chmod +x "$T/bin/git"
}

# AIT_REPLAY=1 (5.9a, user decision 52): each hint that gives one command, 'ait ...' and nothing to fill in,
# runs as given in a copy of the repository and is taken: it succeeds, or asks for results itself
replay() {
	rp_top=$(git rev-parse --show-toplevel 2>/dev/null) && [ -d "$rp_top/.git" ] || return 0
	grep "^hint: .*'ait [^']*'\$" "$T/err" | grep -v "then\|restore\|push\|<[^w]" >"$T/replay.hints"
	while IFS= read -r rp_line; do
		rp_cmd=$(printf '%s\n' "$rp_line" | grep -o "'ait [^']*'" | tr -d "'" | sed 's/<why>/x/')
		[ "$(printf '%s\n' "$rp_cmd" | wc -l)" = 1 ] || continue
		rm -rf "$T/replay"; cp -R "$rp_top" "$T/replay"
		rp_new=$(cd "$T/replay" && git rev-parse --show-toplevel)
		for rp_f in "$T/replay/.git/ait/sessions/"*; do
			[ -f "$rp_f" ] && sed "s#^worktree $rp_top\$#worktree $rp_new#" "$rp_f" >"$rp_f.new" && mv "$rp_f.new" "$rp_f"
		done
		rp_key=$(printf %s "$rp_top" | git hash-object --stdin)
		[ -f "$T/replay/.git/ait/worktrees/$rp_key" ] &&
			mv "$T/replay/.git/ait/worktrees/$rp_key" "$T/replay/.git/ait/worktrees/$(printf %s "$rp_new" | git hash-object --stdin)"
		(cd "$T/replay" && sh -c "$rp_cmd" >"$T/replay.out" 2>"$T/replay.err")
		rp_r=$?
		[ $rp_r = 0 ] || grep -q '^error: check results required' "$T/replay.err" && continue
		n=$((n+1)); failed=$((failed+1)); echo "not ok $n - replay: $rp_line"; sed 's/^/#   /' "$T/replay.err" | head -5
	done <"$T/replay.hints"
}

# run a command, keeping stdout, stderr and the exit code
run() { "$@" >"$T/out" 2>"$T/err"; rc=$?; [ -z "$AIT_REPLAY" ] || replay; }

# 0.2 `ok` and `err X` (5.8)
code_of() {
	case $1 in
	E_USAGE) echo "129 error:" ;;
	E_READ) echo "128 fatal: could not read" ;;
	E_SESSION) echo "128 fatal: no usable ait session:" ;;
	E_BUSY) echo "128 fatal: cannot run now:" ;;
	E_DIRTY) echo "1 error: uncommitted changes in" ;;
	E_ID) echo "128 fatal: no such contract:" ;;
	E_STATE) echo "1 error: not allowed:" ;;
	E_SCOPE) echo "128 fatal: bad scope:" ;;
	E_FORMAT) echo "128 fatal: bad format:" ;;
	E_INCOMPLETE) echo "1 error: spec files do not match the contract:" ;;
	E_MIXED) echo "1 error: contract changes and code changes are mixed:" ;;
	E_HEAD_MOVED) echo "1 error: HEAD moved while running; nothing changed" ;;
	E_CODE_REQUIRED) echo "1 error: no code under the contract's scope:" ;;
	E_NEEDS_RESULTS) echo "1 error: check results required:" ;;
	E_CHECK_FAILED) echo "1 error: check failed:" ;;
	E_GUARD) echo "1 error: contract files cannot be changed by a plain commit:" ;;
	E_GIT) echo "128 fatal: git failed:" ;;
	E_IO) echo "128 fatal: write error:" ;;
	UNSUPPORTED) echo "128 fatal: not supported:" ;;
	esac
}
expect_ok() { desc=$1; shift; run "$@"; [ $rc = 0 ] && say_ok "$desc" || say_fail "$desc (exit $rc)"; }
# the error is the first line after the warnings and their hints, which git writes before it too
expect_err() {
	desc=$1 kind=$2; shift 2; run "$@"
	want=$(code_of "$kind")
	if [ "$rc" = "${want%% *}" ] && grep -v '^warning: \|^hint: ' "$T/err" | head -1 | grep -qF -- "${want#* }"; then say_ok "$desc"; else say_fail "$desc (want $kind, exit $rc)"; fi
}
# 0.2 `불변`: HEAD, the index and the status are the same before and after
snapshot() { { git rev-parse HEAD; git ls-files -s; git status --porcelain --untracked-files=all; } 2>&1; }
expect_unchanged() { before=$(snapshot); "$@"; [ "$(snapshot)" = "$before" ] && say_ok "unchanged: $desc" || say_fail "unchanged: $desc"; }

# checks on the last run
check() { desc=$1; shift; if "$@"; then say_ok "$desc"; else say_fail "$desc"; fi; }
out_has() { grep -qxF -- "$1" "$T/out"; }
out_matches() { grep -qE -- "$1" "$T/out"; }
err_has() { grep -qF -- "$1" "$T/err"; }
out_value() { sed -n "s/^$1 //p" "$T/out" | head -1; }

# 0.2 rec(ID), blob(REV:PATH), pins(REV, ID)
rec() { git show "HEAD:.ait/$1"; }
blob() { git rev-parse "$1"; }
pins() { git show "$1:.ait/$2" | grep -E '^(provider|file) ' | git hash-object --stdin; }
# check_line REV ID: the check line of ID's record at REV without its key ("reported|forced PINS", 1.5a)
check_line() { git show "$1:.ait/$2" | sed -n 's/^check //p'; }
new_id() { out_value new | cut -d' ' -f1; }

# 0.2 `fill W OUT [TASK=OUT2]...`
fill() {
	w=$1 out=$2; shift 2
	{
		grep '^input: ' "$w"
		echo 'results:'
		sed -n 's/^ *- task: "\{0,1\}\([^"]*\)"\{0,1\}$/\1/p' "$w" | while read -r task; do
			o=$out
			for spec in "$@"; do [ "${spec%=*}" = "$task" ] && o=${spec##*=}; done
			printf '  - task: %s\n    outcome: %s\n    basis: t\n    reason: t\n' "$task" "$o"
		done
	} > "$w.new" && mv "$w.new" "$w"
}

# 0.2 `CMD -r pass|fail`: run without -r (E_NEEDS_RESULTS, unchanged, a check-result path),
# fill it, then run with -r; the last run is what the caller checks
with_r() {
	outcome=$1; shift
	wr_before=$(snapshot); run "$@"
	W=$(out_value check-result); Q=$(out_value check-request)
	if [ "$rc" = 1 ] && grep -v '^warning: \|^hint: ' "$T/err" | head -1 | grep -qF "error: check results required:" && [ -n "$W" ] &&
	   [ "$(snapshot)" = "$wr_before" ]; then :; else say_fail "first run of '$*' asks for results"; return; fi
	fill "$W" "$outcome"
	run "$@" -r "$W"
}
