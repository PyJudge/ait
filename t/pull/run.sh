#!/bin/sh
# Run the pull criteria tests (t/pull/t*.sh). They stay out of `make test` until pull is implemented:
# `fact` checks must pass now; `want` checks fail until then. Reports both counts per script.
cd "$(dirname "$0")" || exit 1
out=$(mktemp -d)
jobs=${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)}
ls t[0-9]*.sh | xargs -P "$jobs" -I{} sh -c 'sh "$1" > "$2/$1.out" 2>&1' _ {} "$out"
bad=0
for t in t[0-9]*.sh; do
	fo=$(grep -c '^ok .* - fact:' "$out/$t.out"); ff=$(grep -c '^not ok .* - fact:' "$out/$t.out")
	wo=$(grep -c '^ok .* - want:' "$out/$t.out"); wf=$(grep -c '^not ok .* - want:' "$out/$t.out")
	echo "$t: fact $fo ok, $ff failed ; want $wo ok, $wf failed"
	[ "$ff" = 0 ] || { bad=$((bad + 1)); grep -A3 '^not ok .* - fact:' "$out/$t.out" | head -20; }
	grep -h 'Traceback\|syntax error\|not found' "$out/$t.out" | head -3
done
rm -rf "$out"
[ "$bad" = 0 ]
