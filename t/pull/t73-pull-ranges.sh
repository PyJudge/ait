#!/bin/sh
# Numeric scope order and the common range-location/repinning boundary (3.2a, 3.7, 1.5c/e).
. "$(dirname "$0")/lib.sh"

# Replace only the provider rows; preserve prev, file pins and creator.
ranges() {
	b=$(wblob src/stock.c)
	awk '$1 != "provider"' ".ait/$D" > "$T/rest"
	awk -v b="$b" -v scopes="$1" '
		$1 == "file" && !done {
			n = split(scopes, a, ",")
			for (i = 1; i <= n; i++) print "provider " a[i] " " b
			done = 1
		} {print}' "$T/rest" > ".ait/$D"
}

pbase
for i in $(seq 10 20); do echo "line$i v=0"; done >> src/stock.c
cp src/stock.c src/order.c
c extend >/dev/null
git checkout -q -b incoming
rec D >/dev/null
ranges 'src/stock.c,src/stock.c:2-3,src/stock.c:2-10,src/stock.c:10-12'
ob=$(wblob src/order.c)
awk -v b="$ob" '$1 == "consumer" {print "consumer src/order.c " b; print "consumer src/order.c:2-3 " b; print "consumer src/order.c:2-10 " b; print "consumer src/order.c:10-12 " b; next} {print}' ".ait/$D" > "$T/new"
cat "$T/new" > ".ait/$D"
A1=$(c 'numeric provider order')
git checkout -q master
pull "$D=$A1" -m why
want 'whole file, start and end sort numerically in an incoming record' test "$rc" = 0
want 'numeric consumer rows survive unchanged' test "$(git show "HEAD:.ait/$D" | grep '^consumer ')" = "$(git show "$A1:.ait/$D" | grep '^consumer ')"
want 'numeric provider rows survive unchanged' test "$(git show "HEAD:.ait/$D" | grep '^provider ')" = "$(git show "$A1:.ait/$D" | grep '^provider ')"

pbase
git checkout -q -b incoming
rec D >/dev/null
ranges 'src/stock.c:2-3,src/stock.c:4-5'
c 'two checked ranges' >/dev/null
tick; ait fulfill "$D" --force -m 'check ranges' >/dev/null
A1=$(git rev-parse HEAD)
git checkout -q master
awk 'NR == 2 {print "replacement"} NR < 2 || NR > 5' src/stock.c > "$T/new"
cat "$T/new" > src/stock.c
c 'collapse ranges' >/dev/null
pull "$D" --force -m why
want 'collapsed mapped scopes can be checked' test "$rc" = 0
want 'repinning combines identical resulting provider rows' test "$(git show "HEAD:.ait/$D" | grep -c '^provider ')" = 1
want 'the kept range remains a range' test "$(git show "HEAD:.ait/$D" | awk '$1 == "provider" {print $2}')" = src/stock.c:2-2
want 'CHECK hashes the normalized provider and file rows' test "$(git show "HEAD:.ait/$D" | awk '$1 == "check" {print $3}')" = "$(pins HEAD "$D")"

for location in deleted missing nobase; do
	pbase
	git checkout -q -b incoming
	tick; ait fulfill "$D=src/stock.c:3-5" --force -m 'check range' >/dev/null
	if [ "$location" = nobase ]; then
		# An otherwise valid current record references a base blob absent in this clone.
		awk '$1 == "provider" {$3 = "1111111111111111111111111111111111111111"} $1 != "check"' ".ait/$D" > "$T/new"
		awk -v p="$(grep -E '^(provider|file) ' "$T/new" | git hash-object --stdin)" '$1 == "creator" {print "check forced " p} {print}' "$T/new" > ".ait/$D"
		c 'missing range base' >/dev/null
	fi
	git checkout -q master
	case "$location" in
	deleted) awk 'NR < 3 || NR > 5' src/stock.c > "$T/new"; cat "$T/new" > src/stock.c ;;
	missing) rm src/stock.c ;;
	nobase) line 9 edited ;;
	esac
	c "$location checked range" >/dev/null
	before=$(snapshot)
	if [ "$location" = nobase ]; then
		pull "$D" amend "$D" --force -m why
	else
		pull "$D" --force -m why
	fi
	want "$location provider is not force-confirmable" test "$rc" = 1
	want "$location refusal preserves refs, index and worktree" test "$(snapshot)" = "$before"
	if [ "$location" = nobase ]; then
		want "$location refusal explains the missing provider" err_has 'no code under the contract'
	else
		want "$location refusal explains the missing provider" err_has 'its provider cannot be found'
	fi
done

# The same absence logic must drive helper hints: retain mapped pieces and ask for a new scope.
pbase
git checkout -q -b incoming
rec D >/dev/null
ranges 'src/stock.c:3-5,src/stock.c:7-8'
c 'split moved scope' >/dev/null
tick; ait fulfill "$D" --force -m 'check pieces' >/dev/null
git checkout -q master
awk 'NR < 3 || NR > 5' src/stock.c > "$T/new"
cat "$T/new" > src/stock.c
c 'move one checked piece' >/dev/null
before=$(snapshot)
pull "$D" amend "$D" --force -m why
want 'helper refuses to check a partly deleted provider' test "$rc" = 1
want 'helper refusal preserves refs, index and worktree' test "$(snapshot)" = "$before"
want 'moved hint retains the mapped surviving range only' err_has 'src/stock.c:4-5,<new scope>'
want 'unmappable range does not suggest staging an already staged file' not grep -q 'hint:.*stage ' "$T/err"

# Distinct surviving ranges can collapse even when another piece needs moving.
pbase
git checkout -q -b incoming
rec D >/dev/null
ranges 'src/stock.c:2-3,src/stock.c:4-5,src/stock.c:7-8'
c 'three checked pieces' >/dev/null
tick; ait fulfill "$D" --force -m 'check pieces' >/dev/null
git checkout -q master
awk 'NR == 2 {print "replacement"} NR < 2 || NR == 6 || NR > 8' src/stock.c > "$T/new"
cat "$T/new" > src/stock.c
c 'collapse surviving pieces and move one' >/dev/null
pull "$D" amend "$D" --force -m why
want 'helper refuses the deleted piece after other ranges collapse' test "$rc" = 1
want 'moved hint combines identical surviving mapped ranges' err_has 'src/stock.c:2-2,<new scope>'
want 'moved hint does not repeat the surviving scope' not grep -q 'src/stock.c:2-2,src/stock.c:2-2' "$T/err"

done_testing
