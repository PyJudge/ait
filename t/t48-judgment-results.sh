#!/bin/sh
# One immutable view/tree yields one result; scope matches do not make a whole witness.
. "$(dirname "$0")/lib.sh"
setup
run ait future offer src/stock.c:2-2 cond.yaml -m range
R=$(newest O)
ait fulfill "$R" --force -m witness >/dev/null 2>&1
sed -i.bak '4s/.*/outside/' src/stock.c
git add src/stock.c
REAL_GIT=$(command -v git); export REAL_GIT
mkdir "$T/diff-bin"
cat >"$T/diff-bin/git" <<'GIT'
#!/bin/sh
case " $* " in *' -U0 '*) printf '%s\n' "$*" >>"$T/diff.trace" ;; esac
if test "$ONE_ROUND" = 1; then
	sub=
	for arg; do
		case $arg in -*|*=*|/*) continue ;; esac
		sub=$arg; break
	done
	if test "$sub" = rev-list; then
		"$REAL_GIT" "$@"
		status=$?
		kill -TERM "$PPID"
		exit "$status"
	fi
fi
exec "$REAL_GIT" "$@"
GIT
chmod +x "$T/diff-bin/git"
run env PATH="$T/diff-bin:$PATH" ait status src/stock.c
check_rc 'one result serves selection, display and touch' 0
check 'one ordered blob pair is diffed once' "test \"\$(wc -l <\"$T/diff.trace\" | tr -d ' ')\" = 1"
check 'outside-range edits preserve current state and coordinates' "has out '^state current$' && has out '^provider src/stock.c:2-2$' && ! has out '^touch$'"
git reset --hard -q HEAD
printf 'u1\n' >u; printf 'v1\n' >v
git add u v && git commit -qm 'first code pair'
run ait future offer u,v cond.yaml -m pairs
O=$(newest O)
ait fulfill "$O" --force -m first >/dev/null 2>&1
printf 'u2\n' >u; printf 'v2\n' >v
git add u v
ait fulfill "$O" --force -m second >/dev/null 2>&1
block() { awk -v id="$O" '$0 == "id " id { emit=1 } emit { print; if ($0 == "") exit }' "$T/out" >"$T/block"; }
printf 'u1\n' >u; git add u
run ait status u
block
check 'matching scopes from different witnesses remain unverified' "has block '^state current ?unverified$' && has block '^provider u,v$'"
printf 'v3\n' >v; git add v
run ait status u
block
check 'matches-any marks only the scope with no matching witness' "has block '^state current ?unverified$' && has block '^provider u,v ?changed$'"
printf 'v1\n' >v; git add v
run ait status u
block
check 'HEAD witness changes count as touch even when index is also current' "has block '^state current$' && has block '^provider u,v$' && has block '^touch$'"
tip=$(git rev-parse ait/master)
run ait future drop "$O" as x future amend "$O" future drop "$O" -m 'mutable overlay'
check_rc 'each clause sees the current mutable overlay' 1
check 'the last failed drop publishes none of the earlier clauses' "test \"\$(git rev-parse ait/master)\" = $tip && ! has out '^new '"
run ait future demand src/order.c src/stock.c cond.yaml as leaf \
	future demand src/order.c src/stock.c cond.yaml as root \
	future offer src/stock.c cond.yaml as one future offer src/stock.c cond.yaml as two \
	undertake leaf one undertake leaf two undertake -p root one undertake -p root two -m diamond
check_rc 'prepare two dependency paths sharing a leaf' 0
DROOT=$(sed -n 's/^new \(D[0-9a-f]*\) root$/\1/p' "$T/out")
DLEAF=$(sed -n 's/^new \(D[0-9a-f]*\) leaf$/\1/p' "$T/out")
run ait fulfill "$DROOT" "$DLEAF" --force -m 'shared dependency'
check_rc 'a completed shared dependency is not a recursive cycle' 0
run ait status src/
check 'the diamond root is current' "awk '/^id $DROOT$/ { getline; getline; getline; if (\$0 == \"state current\") ok=1 } END { exit !ok }' $T/out"
mkdir -p src/sub
printf 'child\n' >src/sub/child.c
git add src/sub/child.c
run ait future offer src/ cond.yaml -m directory
DIR=$(newest O)
run ait status src/sub/
check 'directory overlap is symmetric for a narrower filter' "has out '^id $DIR$'"
for version in 2 3 4 5 6; do
	sed -i.bak "4s/.*/version$version/" src/stock.c
	git add src/stock.c
	ait fulfill "$R" --force -m later >/dev/null 2>&1
done
: >"$T/diff.trace"
run env PATH="$T/diff-bin:$PATH" ait future drop "$R" -m 'first witness suffices'
check_rc 'an early whole witness satisfies verdict-only callers' 0
check 'later witnesses cannot add matches after a whole match' "test \"\$(wc -l <\"$T/diff.trace\" | tr -d ' ')\" = 1"
# Index validation and committed-code validation are distinct; recording reuses the latter.
: >"$T/diff.trace"
run env PATH="$T/diff-bin:$PATH" ait fulfill "$R" --force -m 'record mapped code once'
check_rc 'recording consumes its validated provider positions' 0
check 'one map at index and one at committed code suffice' "test \"\$(wc -l <\"$T/diff.trace\" | tr -d ' ')\" = 2"
for spec_number in 1 2 3; do printf 'body: condition %s\n' "$spec_number" >"c$spec_number.yaml"; done
run ait future offer src/stock.c:2-2 c1.yaml,c2.yaml,c3.yaml -m 'three specs'
THREE=$(newest O)
sed -i.bak '4s/.*/one more outside change/' src/stock.c
git add src/stock.c && git commit -qm outside
: >"$T/diff.trace"
run env PATH="$T/diff-bin:$PATH" ait fulfill "$THREE" -m 'prepare tasks'
check_rc 'all three tasks consume the same prepared scope positions' 1
check 'validation and all spec displays share one map' "test \"\$(wc -l <\"$T/diff.trace\" | tr -d ' ')\" = 1 && has err 'check results required: 3 task(s)'"
RECEIVER=$(AIT_SESSION=- ait session open --agent alice 2>/dev/null)
AIT_SESSION="$RECEIVER" ait sub "$R" >/dev/null
for event_number in 1 2 3; do
	printf 'body: notification condition %s\n' "$event_number" >cond.yaml
	ait future amend "$R" - cond.yaml --force -m "event $event_number" >/dev/null
done
: >"$T/diff.trace"
run env AIT_SESSION="$RECEIVER" ONE_ROUND=1 PATH="$T/diff-bin:$PATH" ait listen
check 'one round delivers exactly three captured events' "test $rc = 0 && test \"\$(grep -c '^ait-event 1$' '$T/out')\" = 3"
check 'one receiving snapshot is judged once across its events' "test \"\$(wc -l <\"$T/diff.trace\" | tr -d ' ')\" = 1"
done_testing
