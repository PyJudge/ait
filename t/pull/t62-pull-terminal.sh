#!/bin/sh
# pull criteria 3.3 끝난 쪽이 이긴다 (U4, U4b, U7, TF2, PV1, PV2, PV3, TT1, REV1, REV2, REV3, REV4)
# Histories: scen4.py (U4, U7, TF2), scen5.py (U4b, PV1, PV2, PV3, TT1, REV1), scen6.py (REV2, REV3), scen7.py (REV4),
# scen9.py (PV1-9). Notifications are left out (PV2's alert to agent-b).
. "$(dirname "$0")/lib.sh"

# ---- helpers of this file
has_obj() { git cat-file -e "$1" 2>/dev/null; }
sorted() { printf '%s\n' "$@" | sort | tr '\n' ' ' | sed 's/ $//'; }
# latest V...: of the given versions, those no other of them contains
latest() {
	for _l in "$@"; do _k=1; for _m in "$@"; do [ "$_l" != "$_m" ] && contains "$_m" "$_l" && _k=0; done; [ $_k = 1 ] && echo "$_l"; done |
		sort | tr '\n' ' ' | sed 's/ $//'
}
# anc V: V and every version reached from it through prev (a missing blob is listed but ends that branch)
anc() {
	echo "$1" > "$T/atodo"; : > "$T/aseen"
	while [ -s "$T/atodo" ]; do
		_o=$(head -1 "$T/atodo"); sed -i.bak 1d "$T/atodo"
		grep -qx "$_o" "$T/aseen" && continue; echo "$_o" >> "$T/aseen"
		git cat-file -p "$_o" 2>/dev/null | sed -n 's/^prev //p' | grep -v '^-$' >> "$T/atodo"
	done
	sort "$T/aseen"
}
# newest_common V W: the newest common versions of V and W (1절 "앞으로 나감")
newest_common() {
	anc "$1" > "$T/anc1"; anc "$2" > "$T/anc2"; comm -12 "$T/anc1" "$T/anc2" > "$T/ancc"
	for _l in $(cat "$T/ancc"); do
		_k=1; for _m in $(cat "$T/ancc"); do [ "$_l" != "$_m" ] && contains "$_m" "$_l" && _k=0; done; [ $_k = 1 ] && echo "$_l"
	done | sort | tr '\n' ' ' | sed 's/ $//'
}
# forward V W: W moved forward from V (decision 15): the newest common versions all have one body B (and can be
# read), V's body is B and W's is not
forward() {
	_nc=$(newest_common "$1" "$2"); [ -n "$_nc" ] || return 1
	_b=; _first=1
	for _l in $_nc; do
		has_obj "$_l" || return 1
		[ $_first = 1 ] || [ "$(body "$_l")" = "$_b" ] || return 1
		_b=$(body "$_l"); _first=0
	done
	[ "$(body "$1")" = "$_b" ] && [ "$(body "$2")" != "$_b" ]
}
# none_forward V...: no version moved forward from another (3-way removes none)
none_forward() { for _v in "$@"; do for _w in "$@"; do [ "$_v" != "$_w" ] && forward "$_v" "$_w" && return 1; done; done; return 0; }
# would_be KEEP PREV...: the blob of a version with KEEP's body and prev = PREV... (stored)
would_be() { _k=$1; shift; { body "$_k"; printf 'prev %s\n' "$@" | sort; } | git hash-object -w --stdin; }
# result_is NAME KEEP PREV...: HEAD's record of NAME is KEEP's body with prev = PREV... (no check line)
result_is() {
	_n=$1 _k=$2; shift 2
	[ "$(git cat-file -p "$(recat HEAD "$_n")" 2>/dev/null)" = "$({ body "$_k"; printf 'prev %s\n' "$@" | sort; })" ]
}
# parents_are FIRST [OTHER...]: HEAD's first parent is FIRST and its other parents are exactly OTHER...
parents_are() {
	_got=$(git rev-list --parents -n1 HEAD | cut -s -d' ' -f2-)
	[ "${_got%% *}" = "$1" ] || return 1; shift
	[ "$(echo "$_got" | tr ' ' '\n' | sed 1d | sort | tr '\n' ' ')" = "$( [ $# -gt 0 ] && printf '%s\n' "$@" | sort | tr '\n' ' ')" ]
}
# file_is PATH TEXT: HEAD and the work tree hold TEXT at PATH
file_is() { [ "$(git cat-file -p "HEAD:$1" 2>/dev/null)" = "$2" ] && [ "$(cat "$1" 2>/dev/null)" = "$2" ]; }
new_count() { git rev-list --count "$1" ^HEAD; }
# mt_changed A B: the paths where git's merge of A and B differs from A
mt_changed() { git diff --name-only "$1" "$(git merge-tree --write-tree "$1" "$2" 2>/dev/null | head -1)" | tr '\n' ' ' | sed 's/ $//'; }
err_re() { grep -qE -- "$1" "$T/err"; }
out_re() { grep -qE -- "$1" "$T/out"; }
stops_re() { [ "$rc" = "$1" ] && head -1 "$T/err" | grep -qE -- "$2"; }
stopped_re() { [ "$rc" = "$1" ] && err_re "$2"; }
# both_changed A B PATH: A and B both changed PATH since their merge base
both_changed() { _mb=$(git merge-base "$1" "$2"); [ "$(at "$_mb" "$3")" != "$(at "$1" "$3")" ] && [ "$(at "$_mb" "$3")" != "$(at "$2" "$3")" ]; }
# pins_with V PATH BLOB: version V pins PATH as BLOB (a def row)
pins_with() { git cat-file -p "$1" | grep -qx "file $2 100644 $3 def"; }
warnings() { grep -c '^warning:' "$T/err"; }
no_warning() { ! grep -q '^warning:' "$T/err"; }
# the choices a 'pick one' hint offers for ID, one per line
choices() { grep '^hint: pick one:' "$T/err" | grep -oE "'ait contract pull [^']*$1=[^ ']+[^']* -m <why>'"; }
two_choices_no_head() { [ "$(choices "$1" | wc -l | tr -d ' ')" = 2 ] && ! choices "$1" | grep -q "$1=HEAD "; }
overridden_mine() { err_re "^warning: your change to '$D' is overridden: '$D' was $1 in '[^']+'\$"; }
overridden_other() { err_re "^warning: the change to '$D' in '[^']+' is overridden: '$D' was $1 in '[^']+'\$"; }
skipping_b() { err_re "^warning: skipping '$D' at 'b' \\([^)]+\\): it was changed outside ait's rules\$"; }

# U4  a1: cancel D ; main: D c-E ; 'pull D'
pbase
vD0=$(recat HEAD D)
git checkout -q -b a; vT=$(rec D cancelled); cA1=$(c "a: cancel D"); git checkout -q master
cond D c-E; vE=$(rec D); c "e: D c-E" >/dev/null
fact "U4: the latest versions are a's D and mine" test "$(latest "$vT" "$vE")" = "$(sorted "$vT" "$vE")"
fact "U4: 3-way removes none (both changed since D0)" none_forward "$vT" "$vE"
fact "U4: the newest common version is D0" test "$(newest_common "$vT" "$vE")" = "$vD0"
fact "U4: only a's D is ended" test "$(state_of "$vT") $(state_of "$vE")" = "cancelled future"
fact "U4: a1 is one new commit" test "$(new_count a)" = 1
OLD=$(git rev-parse HEAD)
pull "$D"
want "U4: pull D does not stop" test "$rc" = 0
want "U4: 'your change to D is overridden: D was cancelled'" overridden_mine cancelled
want "U4: new version: a's cancelled body, prev {a's D, mine}" result_is D "$vT" "$vT" "$vE"
want "U4: a commit on HEAD taking a1" parents_are "$OLD" "$cA1"
want "U4: 'included D from ...'" out_re "^included $D from '[^']+'\$"
want "U4: my c-E is the spec of my overridden version, not taken (specs/d.yaml c0)" file_is specs/d.yaml "body: c0"

# U4b  main: D c-tmp, then back to c0 ; a1: cancel D ; 'pull D'
pbase
vD0=$(recat HEAD D)
git checkout -q -b a; vT=$(rec D cancelled); cA1=$(c "a: cancel D"); git checkout -q master
cond D c-tmp; rec D >/dev/null; c "e: c-tmp" >/dev/null; cond D c0; vE=$(rec D); c "e: back to c0" >/dev/null
fact "U4b: my body is D0's" same_body "$vE" "$vD0"
fact "U4b: my version is not D0 itself" test "$vE" != "$vD0"
fact "U4b: the latest versions are a's D and mine" test "$(latest "$vT" "$vE")" = "$(sorted "$vT" "$vE")"
fact "U4b: a's D moved forward from mine (3-way)" forward "$vE" "$vT"
fact "U4b: mine did not move forward from a's" not forward "$vT" "$vE"
OLD=$(git rev-parse HEAD)
pull "$D"
want "U4b: pull D does not stop" test "$rc" = 0
want "U4b: no warning (3-way removed mine before the terminal rule)" no_warning
want "U4b: new version: a's cancelled body, prev {a's D, mine}" result_is D "$vT" "$vT" "$vE"
want "U4b: a commit on HEAD taking a1" parents_are "$OLD" "$cA1"
want "U4b: 'included D from ...'" out_re "^included $D from '[^']+'\$"

# U7  D current ; a1: drop X ; a2: fulfil X (D withdrawn) ; main: reopens D ; 'pull D', then the hint 'pull D X'
DSTATE=current pbase
git checkout -q -b a; rec X future target=D >/dev/null; c "a: drop X" >/dev/null
vX=$(rec X fulfilled target=D); vW=$(rec D withdrawn); cA2=$(c "a: fulfil X"); git checkout -q master
vE=$(rec D future); c "e: reopen D" >/dev/null
fact "U7: the latest versions are a's D and mine" test "$(latest "$vW" "$vE")" = "$(sorted "$vW" "$vE")"
fact "U7: 3-way removes none" none_forward "$vW" "$vE"
fact "U7: only a2's D is ended (withdrawn)" test "$(state_of "$vW") $(state_of "$vE")" = "withdrawn future"
fact "U7: a2 is two new commits" test "$(new_count a)" = 2
fact "U7: merging a2 conflicts only on .ait/D" test "$(mt HEAD a)" = ".ait/$D"
fact "U7: merging a2 also changes .ait/X" test "$(mt_changed HEAD a)" = ".ait/$D .ait/$X"
fact "U7: a2's X is fulfilled and targets D" test "$(state_of "$vX") $(git cat-file -p "$vX" | sed -n 's/^target //p')" = "fulfilled $D"
want "U7: 'pull D' stops, nothing changed" pull_unchanged "$D"
want "U7: stops at 3: X not named" stops_re 1 "^error: taking '[^']+' also changes contracts you did not name: '$X'\$"
want "U7: hint 'pull D X'" err_line "hint: name them too: 'ait contract pull $D $X'"
OLD=$(git rev-parse HEAD)
pull "$D" "$X"
want "U7: 'pull D X' passes" test "$rc" = 0
want "U7: 'your change to D is overridden: D was withdrawn'" overridden_mine withdrawn
want "U7: D: new version, a2's withdrawn body, prev {a2's D, mine}" result_is D "$vW" "$vW" "$vE"
want "U7: X: a2's fulfilled X as is" test "$(recat HEAD X)" = "$vX"
want "U7: a commit on HEAD taking a2" parents_are "$OLD" "$cA2"
want "U7: 'included D from ...'" out_re "^included $D from '[^']+'\$"
want "U7: 'included X from ...'" out_re "^included $X from '[^']+'\$"

# TF2  D and main's future K5 share specs/shared.yaml ; a1: cancel D ; main: D s-E (K5 re-pinned) ; 'pull D'
shared0() { echo 'body: s0' > specs/shared.yaml; }
pbase shared0
rec D future spec=specs/shared.yaml >/dev/null; c "D shares" >/dev/null
git checkout -q -b a; vT=$(rec D cancelled spec=specs/shared.yaml); cA1=$(c "a: cancel D"); git checkout -q master
rec K5 future spec=specs/shared.yaml provider=src/order.c >/dev/null; c "e: K5" >/dev/null
echo 'body: s-E' > specs/shared.yaml
vE=$(rec D future spec=specs/shared.yaml); vK5=$(rec K5 future spec=specs/shared.yaml provider=src/order.c); c "e: D s-E" >/dev/null
fact "TF2: the latest versions are a1's D and mine" test "$(latest "$vT" "$vE")" = "$(sorted "$vT" "$vE")"
fact "TF2: 3-way removes none" none_forward "$vT" "$vE"
fact "TF2: only a1's D is ended" test "$(state_of "$vT") $(state_of "$vE")" = "cancelled future"
# (the Python ran git with the v14 merge driver on .ait/; plain git merges these two records by text without a conflict)
fact "TF2: merging a1: no conflict outside .ait/D" test -z "$(mt HEAD a | tr ' ' '\n' | grep -vx ".ait/$D")"
fact "TF2: both sides changed .ait/D since the merge base" both_changed HEAD a ".ait/$D"
fact "TF2: merging a1 changes nothing else (shared.yaml stays s-E)" test "$(mt_changed HEAD a)" = ".ait/$D"
fact "TF2: my K5 is future and pins shared.yaml as s-E" eval 'test "$(state_of "$vK5")" = future && pins_with "$vK5" specs/shared.yaml "$(at HEAD specs/shared.yaml)"'
OLD=$(git rev-parse HEAD)
pull "$D"
want "TF2: pull D passes" test "$rc" = 0
want "TF2: 'your change to D is overridden: D was cancelled'" overridden_mine cancelled
want "TF2: D: new version, a1's cancelled body, prev {a1's D, mine}" result_is D "$vT" "$vT" "$vE"
want "TF2: a commit on HEAD taking a1" parents_are "$OLD" "$cA1"
want "TF2: shared.yaml stays s-E (K5's pin)" file_is specs/shared.yaml "body: s-E"
want "TF2: K5 is not changed" test "$(recat HEAD K5)" = "$vK5"
want "TF2: 'included D from ...'" out_re "^included $D from '[^']+'\$"

# PV1  a: c-A, then cancel (a2) ; b: c-x, c-A, then cancel (b3) ; main: c-E (also at 'om', origin/main's stand-in) ; 'pull D'
pbase
git checkout -q -b a; cond D c-A; rec D >/dev/null; c "a: c-A" >/dev/null; vT1=$(rec D cancelled); cA2=$(c "a: cancel")
git checkout -q -b b master
for x in c-x c-A; do cond D "$x"; rec D >/dev/null; c "b: $x" >/dev/null; done
vT2=$(rec D cancelled); c "b: cancel" >/dev/null
git checkout -q master; cond D c-E; vE=$(rec D); c "e: c-E" >/dev/null; git branch om
fact "PV1: the two ended versions have one body" same_body "$vT1" "$vT2"
fact "PV1: both are cancelled, mine is future" test "$(state_of "$vT1") $(state_of "$vT2") $(state_of "$vE")" = "cancelled cancelled future"
fact "PV1: the latest versions are a's, b's and mine" test "$(latest "$vT1" "$vT2" "$vE")" = "$(sorted "$vT1" "$vT2" "$vE")"
fact "PV1: 3-way removes none" none_forward "$vT1" "$vT2" "$vE"
fact "PV1: neither ended version contains mine" eval 'not contains "$vT1" "$vE" && not contains "$vT2" "$vE"'
fact "PV1: new commits: a2 2, b3 3" test "$(new_count a) $(new_count b)" = "2 3"
fact "PV1: a result with prev = the three latest leaves itself the only latest" eval 'vR=$(would_be "$vT1" "$vT1" "$vT2" "$vE"); test "$(latest "$vR" "$vT1" "$vT2" "$vE")" = "$vR"'
fact "PV1-9: git alone conflicts on .ait/D and specs/d.yaml" test "$(mt HEAD a)" = ".ait/$D specs/d.yaml"
fact "PV1-9: my D (future) pins my c-E" pins_with "$vE" specs/d.yaml "$(at HEAD specs/d.yaml)"
fact "PV1-9: a2 changed specs/d.yaml to c-A and its D is ended" test "$(git cat-file -p a:specs/d.yaml) $(state_of "$vT1")" = "body: c-A cancelled"
OLD=$(git rev-parse HEAD)
pull "$D"
want "PV1: pull D does not stop for a choice" test "$rc" = 0
want "PV1: takes a2 (2 new commits < 3)" parents_are "$OLD" "$cA2"
want "PV1: one warning" test "$(warnings)" = 1
want "PV1: 'your change to D is overridden: D was cancelled'" overridden_mine cancelled
want "PV1: new version: cancelled body, prev = the three latest" result_is D "$vT1" "$vT1" "$vT2" "$vE"
want "PV1: specs/d.yaml is a's c-A (my c-E not taken, a's change is code)" file_is specs/d.yaml "body: c-A"
want "PV1: no conflict" eval 'not merging && test -z "$(unmerged)"'
want "PV1: 'included D from ...'" out_re "^included $D from '[^']+'\$"
pull "$D"
want "PV1: pulling again: already D" eval 'test "$rc" = 0 && out_line "already $D"'
want "PV1: pulling again: no warning" no_warning

# PV2  a1: cancel ; b1: c-B (Ait-Agent: agent-b) ; main: c-E ; 'pull D'
pbase
git checkout -q -b a; vT=$(rec D cancelled); cA1=$(c "a: cancel"); git checkout -q -b b master
cond D c-B; vB=$(rec D); c "$(printf 'b: c-B\n\nAit-Agent: agent-b')" >/dev/null; git checkout -q master
cond D c-E; vE=$(rec D); c "e: c-E" >/dev/null
fact "PV2: the latest versions are a's, b's and mine" test "$(latest "$vT" "$vB" "$vE")" = "$(sorted "$vT" "$vB" "$vE")"
fact "PV2: 3-way removes none" none_forward "$vT" "$vB" "$vE"
fact "PV2: only a's D is ended" test "$(state_of "$vT") $(state_of "$vB") $(state_of "$vE")" = "cancelled future future"
fact "PV2: a1 is one new commit" test "$(new_count a)" = 1
OLD=$(git rev-parse HEAD)
pull "$D"
want "PV2: pull D does not stop" test "$rc" = 0
want "PV2: warning on my version" overridden_mine cancelled
want "PV2: warning on b's version" overridden_other cancelled
want "PV2: two warnings" test "$(warnings)" = 2
want "PV2: new version: cancelled body, prev {a's, b's, mine}" result_is D "$vT" "$vT" "$vB" "$vE"
want "PV2: a commit on HEAD taking a1" parents_are "$OLD" "$cA1"
want "PV2: my c-E is the spec of my overridden version, not taken (specs/d.yaml c0)" file_is specs/d.yaml "body: c0"
pull "$D"
want "PV2: pulling again: already D" eval 'test "$rc" = 0 && out_line "already $D"'
want "PV2: pulling again: no warning" no_warning

# PV3  D current, drop X on D ; main: fulfil X (D withdrawn) ; a1: D's consumer only ; 'pull D'
x0() { rec X future target=D >/dev/null; }
DSTATE=current pbase x0
git checkout -q -b a; echo 'order A' > src/order.c; vA=$(rec D current); cA1=$(c "a: consumer"); git checkout -q master
rec X fulfilled target=D >/dev/null; vW=$(rec D withdrawn); c "e: fulfil X" >/dev/null
vX=$(recat HEAD X)
fact "PV3: the latest versions are a's D and mine" test "$(latest "$vA" "$vW")" = "$(sorted "$vA" "$vW")"
fact "PV3: 3-way removes none" none_forward "$vA" "$vW"
fact "PV3: only mine is ended (withdrawn)" test "$(state_of "$vA") $(state_of "$vW")" = "current withdrawn"
fact "PV3: mine does not contain a's" not contains "$vW" "$vA"
OLD=$(git rev-parse HEAD)
pull "$D"
want "PV3: pull D passes" test "$rc" = 0
want "PV3: 'kept D'" out_line "kept $D"
want "PV3: a commit on HEAD only (a1 not taken)" eval 'parents_are "$OLD" && not is_ancestor "$cA1" HEAD'
want "PV3: new version: my withdrawn body, prev {mine, a's}" result_is D "$vW" "$vW" "$vA"
want "PV3: warning on a's version" overridden_other withdrawn
want "PV3: a's code is not taken" file_is src/order.c order
want "PV3: X is not changed" test "$(recat HEAD X)" = "$vX"
pull "$D"
want "PV3: pulling again: already D" eval 'test "$rc" = 0 && out_line "already $D"'
want "PV3: pulling again: no warning" no_warning

# TT1  a1: cancel D ; b1: fulfil D, b2: drop X, b3: fulfil X (D withdrawn) ; main: c-E ; 'pull D', then 'pull D=a1'
pbase
git checkout -q -b a; vT=$(rec D cancelled); cA1=$(c "a: cancel"); git checkout -q -b b master
line 1 B; rec D current >/dev/null; c "b: fulfil D" >/dev/null
rec X future target=D >/dev/null; c "b: drop X" >/dev/null
rec X fulfilled target=D >/dev/null; vWb=$(rec D withdrawn); c "b: fulfil X" >/dev/null
git checkout -q master; cond D c-E; vE=$(rec D); c "e: c-E" >/dev/null
fact "TT1: the latest versions are a's, b's and mine" test "$(latest "$vT" "$vWb" "$vE")" = "$(sorted "$vT" "$vWb" "$vE")"
fact "TT1: 3-way removes none" none_forward "$vT" "$vWb" "$vE"
fact "TT1: two different ended versions remain, mine is not ended" test "$(state_of "$vT") $(state_of "$vWb") $(state_of "$vE")" = "cancelled withdrawn future"
fact "TT1: the ended versions have different bodies" not same_body "$vT" "$vWb"
fact "TT1: new commits: a1 1, b3 3" test "$(new_count a) $(new_count b)" = "1 3"
want "TT1: 'pull D' stops, nothing changed" pull_unchanged "$D"
want "TT1: stops to choose" stopped_re 1 "^error: '$D' has diverged between '[^']+' \\([^)]+\\) and '[^']+' \\([^)]+\\)$"
want "TT1: the hint offers the two ended versions, not HEAD" two_choices_no_head "$D"
OLD=$(git rev-parse HEAD)
pull "$D=$cA1" -m "pick a1"
want "TT1: 'pull D=a1 -m <why>' passes" test "$rc" = 0
want "TT1: cancelled body, prev {a's, b's, mine}" result_is D "$vT" "$vT" "$vWb" "$vE"
want "TT1: a commit on HEAD taking a1 only" parents_are "$OLD" "$cA1"
want "TT1: X does not come in" test -z "$(recat HEAD X)"
want "TT1: my c-E is the spec of my overridden version, not taken (specs/d.yaml c0)" file_is specs/d.yaml "body: c0"

# REV1  main took a1 (D cancel) ; b: c-E, then git merge main, .ait/D resolved by hand as c-E future, prev {T, E} (bM)
rev1() {
	git checkout -q -b a; vT=$(rec D cancelled); c "a: cancel" >/dev/null
	git checkout -q master; git merge -q --ff-only a
	git checkout -q -b b init; cond D c-E; vE=$(rec D); cB1=$(c "b: c-E")
	git merge -q --no-commit --no-ff master >/dev/null 2>&1
	cond D c-E; vN=$(rec D future prev="$vT $vE"); c "b: merge main, keep c-E" >/dev/null
}
pbase; rev1; git checkout -q master
# (the Python's git merge ran the v14 merge driver on .ait/ and conflicted; plain git merges these two records by text)
fact "REV1: merging main into b1: no conflict outside .ait/D" test -z "$(mt "$cB1" master | tr ' ' '\n' | grep -vx ".ait/$D")"
fact "REV1: both main and b1 changed .ait/D since the merge base" both_changed "$cB1" master ".ait/$D"
fact "REV1: main holds the cancelled D" test "$(recat HEAD D) $(state_of "$vT")" = "$vT cancelled"
fact "REV1: b's version has the cancelled version in prev" test "$(prevs "$vN" | tr '\n' ' ')" = "$(sorted "$vT" "$vE") "
fact "REV1: b's version is future, its body differs from the cancelled one" eval 'test "$(state_of "$vN")" = future && not same_body "$vN" "$vT"'
want "REV1: nothing changes" pull_unchanged "$D"
want "REV1: already D" eval 'test "$rc" = 0 && out_line "already $D"'
want "REV1: warning: skipping D at b" skipping_b
want "REV1: D stays cancelled" test "$(recat HEAD D)" = "$vT"

# REV2  REV1, then b2 writes c-F on top of the accident (prev N)
pbase; rev1
cond D c-F; vN2=$(rec D); c "b: c-F on top" >/dev/null; git checkout -q master
fact "REV2: b2's version has only REV1's version in prev" test "$(prevs "$vN2")" = "$vN"
fact "REV2: REV1's version holds the cancelled one in prev with another body" eval 'contains "$vN" "$vT" && not same_body "$vN" "$vT"'
fact "REV2: b2's version contains the cancelled one, its body differs" eval 'contains "$vN2" "$vT" && not same_body "$vN2" "$vT"'
want "REV2: nothing changes" pull_unchanged "$D"
want "REV2: already D" eval 'test "$rc" = 0 && out_line "already $D"'
want "REV2: warning: skipping D at b" skipping_b
want "REV2: D stays cancelled" test "$(recat HEAD D)" = "$vT"

# REV3  main holds only the cancelled D (C) ; b1: P, future from C by hand ; b2: W, c-W on top (prev P)
pbase
vC=$(rec D cancelled); c "c1: cancel" >/dev/null
git checkout -q -b b; vP=$(rec D future); c "b1: future by hand" >/dev/null
cond D c-W; vW=$(rec D); c "b2: on top" >/dev/null; git checkout -q master
fact "REV3: main holds the cancelled D" test "$(recat HEAD D) $(state_of "$vC")" = "$vC cancelled"
fact "REV3: P's prev is the cancelled version, P is future" test "$(prevs "$vP") $(state_of "$vP")" = "$vC future"
fact "REV3: W's prev is P only" test "$(prevs "$vW")" = "$vP"
fact "REV3: W contains the cancelled version, its body differs" eval 'contains "$vW" "$vC" && not same_body "$vW" "$vC"'
want "REV3: nothing changes" pull_unchanged "$D"
want "REV3: already D" eval 'test "$rc" = 0 && out_line "already $D"'
want "REV3: warning: skipping D at b" skipping_b
want "REV3: D stays cancelled" test "$(recat HEAD D)" = "$vC"

# REV4  main holds cancelled C ; b1: A, future by hand from C ; m1 (from init): M c-M ; b2: hand merge V, prev {A, M}
pbase
vC=$(rec D cancelled); c "c1: cancel D" >/dev/null
git checkout -q -b m init; cond D c-M; vM=$(rec D); c "m1: c-M" >/dev/null
git checkout -q -b b master; vA=$(rec D future); c "b1: future by hand" >/dev/null
git merge -q --no-commit m >/dev/null 2>&1
cond D c-M; vV=$(rec D future prev="$vA $vM"); c "b2: hand merge, prev {A, M}" >/dev/null
git checkout -q master; git branch -q -D m
fact "REV4: main holds the cancelled D" test "$(recat HEAD D) $(state_of "$vC")" = "$vC cancelled"
fact "REV4: A's prev is the cancelled version, A is future" test "$(prevs "$vA") $(state_of "$vA")" = "$vC future"
fact "REV4: M is future from D0 (init)" test "$(prevs "$vM") $(state_of "$vM")" = "$(recat init D) future"
fact "REV4: V's prev is {A, M}" test "$(prevs "$vV" | tr '\n' ' ')" = "$(sorted "$vA" "$vM") "
fact "REV4: V contains the cancelled version through A, its body differs" eval 'contains "$vV" "$vC" && not same_body "$vV" "$vC" && not contains "$vM" "$vC"'
want "REV4: nothing changes" pull_unchanged "$D"
want "REV4: already D" eval 'test "$rc" = 0 && out_line "already $D"'
want "REV4: warning: skipping D at b" skipping_b
want "REV4: D stays cancelled" test "$(recat HEAD D)" = "$vC"
# REV5  the hand-edited result is itself cancelled, but differs from the earlier cancelled version
pbase
vC=$(rec D cancelled); c "c1: cancel D" >/dev/null
git checkout -q -b m init; cond D c-M; vM=$(rec D); c "m1: c-M" >/dev/null
git checkout -q -b b master; cond D c-V; vV=$(rec D cancelled prev="$vC $vM"); c "b1: different cancelled body" >/dev/null
git checkout -q master; git branch -q -D m
fact "REV5: V is cancelled and has a normal future source" test "$(state_of "$vV") $(state_of "$vM")" = "cancelled future"
fact "REV5: V contains the earlier cancelled C but has a different body" eval 'contains "$vV" "$vC" && not same_body "$vV" "$vC"'
fact "REV5: V includes itself, which must not satisfy the terminal check" contains "$vV" "$vV"
want "REV5: nothing changes" pull_unchanged "$D"
want "REV5: already D" eval 'test "$rc" = 0 && out_line "already $D"'
want "REV5: warning: skipping D at b" skipping_b
want "REV5: D remains the original cancelled version" test "$(recat HEAD D)" = "$vC"
done_testing
