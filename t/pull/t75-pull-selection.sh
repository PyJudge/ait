#!/bin/sh
# Contract entry validity and choices discovered by the incoming closure.
. "$(dirname "$0")/lib.sh"

pbase
git checkout -q -b incoming
cond D incoming; rec D >/dev/null
chmod +x ".ait/$D"
BAD=$(c 'executable contract record')
git checkout -q master
before=$(snapshot)
pull "$D=$BAD" -m why
want "entry mode: explicit pick of malformed entry is rejected without changes" eval 'test "$rc" != 0 && test "$(snapshot)" = "$before"'
# The same blob remains usable from a valid entry; invalid mode must not poison its cache.
git checkout -q -b valid "$BAD"
chmod -x ".ait/$D"
GOOD=$(c 'regular contract record')
git checkout -q master
pull "$D=$GOOD" -m why
want "entry mode: the same blob in a regular entry can be selected" eval 'test "$rc" = 0 && test "$(git show HEAD:specs/d.yaml)" = "body: incoming"'

pbase
git checkout -q -b incoming
cond D malformed; rec D >/dev/null
chmod +x ".ait/$D"
cond Q incoming; rec Q >/dev/null
BAD=$(c 'valid Q beside malformed D')
git checkout -q master
OLD_D=$(recat HEAD D)
pull "$Q" -m why
want "entry mode: malformed D and its pinned change are excluded together" eval 'test "$rc" = 0 && test "$(recat HEAD D)" = "$OLD_D" && test "$(git show HEAD:specs/d.yaml)" = "body: c0"'

offer() { cond O offer; rec O >/dev/null; }
pbase offer
git checkout -q -b incoming
cond D incoming; rec D >/dev/null
link D O >/dev/null
chmod +x ".ait/undertake/$D-$O"
BAD=$(c 'malformed incoming link')
git checkout -q master
pull "$D" -m why
want "entry mode: malformed linked entry is excluded from the automatic closure" eval 'test "$rc" = 0 && ! git cat-file -e "HEAD:.ait/undertake/$D-$O" 2>/dev/null && grep -q "skipping malformed" "$T/err"'

pbase
git checkout -q -b a
cond D d-A; rec D >/dev/null
cond Q q-A; rec Q >/dev/null
A1=$(c D-and-new-Q-A)
git checkout -q -b b init
cond D d-B; rec D >/dev/null
cond Q q-B; rec Q >/dev/null
B1=$(c D-and-new-Q-B)
git checkout -q master
pull "$D"
cp "$T/err" "$T/hints"
# The choices name what was named; Q, which choosing D brings, is that pull's to ask for (user decision 54).
want "closure: the choice hint picks D alone" eval 'test "$rc" = 1 && grep "^hint: pick one:" "$T/hints" | grep -q "$D=" && ! grep "^hint: pick one:" "$T/hints" | grep -q "$Q"'
grep "^hint: pick one:" "$T/hints" | grep -o "'ait contract pull [^']*'" |
	sed "s/^'//; s/ -m <why>'$//" > "$T/commands"
HINT_HEAD=$(git rev-parse HEAD)
# a suggested pull, then the first pull each refusal's hint gives, until one passes (at most four)
all_hints_work() {
	while read -r command; do
		git reset -q --hard "$HINT_HEAD" || return 1
		for step in 1 2 3 4; do
			run sh -c "$command -m why"
			[ "$rc" = 0 ] && break
			command=$(grep '^hint: ' "$T/err" | grep -o "'ait contract pull [^']*'" | head -1 | sed "s/^'//; s/ -m <why>'$//")
			[ -n "$command" ] || return 1
		done
		[ "$rc" = 0 ] || return 1
	done < "$T/commands"
	[ -s "$T/commands" ]
}
want "closure: every suggested choice completes the pull as its refusals say" all_hints_work

done_testing
