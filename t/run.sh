#!/bin/sh
# Run every t/t[0-9]*.sh in parallel (each works in its own temporary repository),
# as git runs its own suite with "make -j" or "prove --jobs"; report the failures.
cd "$(dirname "$0")" || exit 1
jobs=${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)}
out=$(mktemp -d)
ls t[0-9]*.sh | xargs -P "$jobs" -I{} sh -c 'sh "$1" > "$2/$1.out" 2>&1; echo $? > "$2/$1.rc"' _ {} "$out"
failed=0
for t in t[0-9]*.sh; do
	if [ "$(cat "$out/$t.rc")" != 0 ]; then
		failed=$((failed + 1))
		echo "FAIL $t"
		grep -A3 '^not ok' "$out/$t.out" | head -20
	fi
	grep -h 'AddressSanitizer\|runtime error:' "$out/$t.out" | head -3
done
echo "# $failed of $(ls t[0-9]*.sh | wc -l | tr -d ' ') scripts failed"
rm -rf "$out"
[ "$failed" = 0 ]
