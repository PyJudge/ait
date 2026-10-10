#!/bin/sh
# pull criteria 3.4 한쪽만 바뀌었으면 고르지 않는다 (T1, T1b, X1, MS2, MS3)
# Histories: scen4.py (T1, T1b, X1), scen5.py (MS2), scen6.py (MS3).
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
# compose NAME KEEP PREV...: write .ait/<ID> as KEEP's body with prev = PREV... and put the spec files KEEP pins back
# to their pinned blobs (what pull writes for a choice: scen4.py choose)
compose() {
	_f=.ait/$(id_of "$1") _k=$2; shift 2
	{ body "$_k"; printf 'prev %s\n' "$@" | sort; } > "$_f"
	git cat-file -p "$_k" | sed -n 's/^file \([^ ]*\) [0-9]* \([0-9a-f]*\) .*/\1 \2/p' | while read -r _p _b; do git cat-file -p "$_b" > "$_p"; done
}
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
# mt_seq A B...: merge A with B... one at a time (no octopus, as scen4.py mtx); print every conflicting path once
mt_seq() {
	_cur=$(git rev-parse "$1"); shift
	for _h; do
		git merge-tree --write-tree --name-only "$_cur" "$_h" > "$T/mts" 2>/dev/null
		awk 'NR==1{next} /^$/{exit} {print}' "$T/mts"
		_cur=$(git commit-tree "$(head -1 "$T/mts")" -p "$_cur" -p "$_h" -m tmp)
	done | sort -u | tr '\n' ' ' | sed 's/ $//'
}
err_re() { grep -qE -- "$1" "$T/err"; }
out_re() { grep -qE -- "$1" "$T/out"; }
stops_re() { [ "$rc" = "$1" ] && head -1 "$T/err" | grep -qE -- "$2"; }
# the choices a 'pick one' hint offers for ID, one per line
choices() { grep '^hint: pick one:' "$T/err" | grep -oE "'ait contract pull [^']*$1=[^ ']+ -m <why>'"; }
two_choices_no_head() { [ "$(choices "$1" | wc -l | tr -d ' ')" = 2 ] && ! choices "$1" | grep -q "$1=HEAD "; }
included() { out_re "^included $1 from '[^']+'\$"; }
# takes_together OLD END...: HEAD is a commit on OLD whose other parents are exactly END..., or HEAD is OLD in a merge
# state whose MERGE_HEAD lists exactly END...
takes_together() {
	_old=$1; shift
	parents_are "$_old" "$@" && return 0
	[ "$(git rev-parse HEAD)" = "$_old" ] && merging &&
		[ "$(sort "$(git rev-parse --git-path MERGE_HEAD)" | tr '\n' ' ')" = "$(printf '%s\n' "$@" | sort | tr '\n' ' ')" ]
}
# wt_is NAME BLOB / wt_result_is NAME KEEP PREV...: the work tree's record of NAME (pull writes it to the index and the
# work tree whether it commits or stops in a merge state)
wt_is() { [ "$(git hash-object ".ait/$(id_of "$1")" 2>/dev/null)" = "$2" ]; }
wt_result_is() { _n=$1 _k=$2; shift 2; [ "$(cat ".ait/$(id_of "$_n")" 2>/dev/null)" = "$({ body "$_k"; printf 'prev %s\n' "$@" | sort; })" ]; }

# T1  U1 (a1: D c-A ; main: D c-E), main chose 'pull D=a1' ; then a2: D c-A2 ; 'pull D'
pbase
git checkout -q -b a; cond D c-A; vA=$(rec D); cA1=$(c "a: D c-A"); git checkout -q master
cond D c-E; vE=$(rec D); c "e: D c-E" >/dev/null
git merge -q --no-commit --no-ff "$cA1" >/dev/null 2>&1; compose D "$vA" "$vA" "$vE"; c "pull D=a1" >/dev/null
vMine=$(recat HEAD D)
git checkout -q a; cond D c-A2; vA2=$(rec D); cA2=$(c "a: D c-A2"); git checkout -q master
fact "T1: my version is the choice: c-A's body, prev {D@A, D@E}" test "$(prevs "$vMine" | tr '\n' ' ')" = "$(sorted "$vA" "$vE") "
fact "T1: the latest versions are a2's and mine" test "$(latest "$vA2" "$vMine")" = "$(sorted "$vA2" "$vMine")"
fact "T1: the newest common version is D@A" test "$(newest_common "$vMine" "$vA2")" = "$vA"
fact "T1: mine is unchanged since D@A (c-A)" same_body "$vMine" "$vA"
fact "T1: a2's D moved forward from mine (3-way)" forward "$vMine" "$vA2"
fact "T1: mine did not move forward from a2's" not forward "$vA2" "$vMine"
fact "T1: a2 is one new commit" test "$(new_count a)" = 1
OLD=$(git rev-parse HEAD)
pull "$D"
want "T1: pull D does not ask, passes" test "$rc" = 0
want "T1: new version: c-A2's body, prev {a2's D, mine}" result_is D "$vA2" "$vA2" "$vMine"
want "T1: a commit on HEAD taking a2" parents_are "$OLD" "$cA2"
want "T1: specs/d.yaml is c-A2" file_is specs/d.yaml "body: c-A2"
want "T1: 'included D from ...'" included "$D"

# T1b  U1, main kept its own ('pull D=HEAD') ; then a2: D c-A2 ; 'pull D'
pbase
git checkout -q -b a; cond D c-A; vA=$(rec D); c "a: D c-A" >/dev/null; git checkout -q master
cond D c-E; vE=$(rec D); c "e: D c-E" >/dev/null
compose D "$vE" "$vA" "$vE"; c "pull D=HEAD" >/dev/null
vMine=$(recat HEAD D)
git checkout -q a; cond D c-A2; vA2=$(rec D); c "a: D c-A2" >/dev/null; git checkout -q master
fact "T1b: my version is the kept one: c-E's body, prev {D@A, D@E}" eval 'same_body "$vMine" "$vE" && test "$(prevs "$vMine" | tr "\n" " ")" = "$(sorted "$vA" "$vE") "'
fact "T1b: the latest versions are a2's and mine" test "$(latest "$vA2" "$vMine")" = "$(sorted "$vA2" "$vMine")"
fact "T1b: the newest common version is D@A" test "$(newest_common "$vMine" "$vA2")" = "$vA"
fact "T1b: both changed since D@A (c-E, c-A2)" eval 'not same_body "$vMine" "$vA" && not same_body "$vA2" "$vA"'
fact "T1b: neither moved forward" eval 'not forward "$vMine" "$vA2" && not forward "$vA2" "$vMine"'
fact "T1b: a's history is not in main (a1 and a2 are new)" test "$(new_count a)" = 2
want "T1b: 'pull D' stops, nothing changed" pull_unchanged "$D"
want "T1b: stops to choose" stops_re 1 "^error: '$D' has diverged between .*'HEAD'"
want "T1b: the hint offers D=HEAD" eval 'choices "$D" | grep -qx "'"'ait contract pull $D=HEAD -m <why>'"'"'
want "T1b: the hint offers a's version too" eval 'choices "$D" | grep -v "=HEAD " | grep -q .'

# X1  c1: Y, Z ; a1 (on c): Y again and D ; b1 (on c): Z again and O ; main changed Y and Z itself ; 'pull D O'
x1base() {
	cond Y y0; rec Y >/dev/null; cond Z z0; rec Z >/dev/null
	echo o > src/o.c; cond O o0; rec O future provider=src/o.c >/dev/null
}
pbase x1base
vD0=$(recat HEAD D); vO0=$(recat HEAD O); vY0=$(recat HEAD Y); vZ0=$(recat HEAD Z)
git checkout -q -b c; cond Y y-c; vYc=$(rec Y); cond Z z-c; vZc=$(rec Z); c "c: Y,Z" >/dev/null
git checkout -q -b a; cond Y y-a; vYa=$(rec Y); cond D d-a; vDa=$(rec D); cA1=$(c "a: Y,D")
git checkout -q c; git checkout -q -b b; cond Z z-b; vZb=$(rec Z); cond O o-b; vOb=$(rec O future provider=src/o.c); cB1=$(c "b: Z,O")
git checkout -q master; cond Y y-E; vYe=$(rec Y); cond Z z-E; vZe=$(rec Z); c "e: Y,Z" >/dev/null
fact "X1: D has one latest version, a1's" test "$(latest "$vDa" "$vD0")" = "$vDa"
fact "X1: O has one latest version, b1's" test "$(latest "$vOb" "$vO0")" = "$vOb"
fact "X1: Y's latest versions are a1's and mine" test "$(latest "$vYa" "$vYc" "$vYe")" = "$(sorted "$vYa" "$vYe")"
fact "X1: Z's latest versions are b1's and mine" test "$(latest "$vZb" "$vZc" "$vZe")" = "$(sorted "$vZb" "$vZe")"
fact "X1: Y: both changed since Y0" eval 'test "$(newest_common "$vYa" "$vYe")" = "$vY0" && not forward "$vYa" "$vYe" && not forward "$vYe" "$vYa"'
fact "X1: Z: both changed since Z0" eval 'test "$(newest_common "$vZb" "$vZe")" = "$vZ0" && not forward "$vZb" "$vZe" && not forward "$vZe" "$vZb"'
fact "X1: c's Y and Z are contained in a1's Y and b1's Z" eval 'contains "$vYa" "$vYc" && contains "$vZb" "$vZc"'
fact "X1: a1's tip holds c's Z, b1's tip holds c's Y" test "$(recat a Z) $(recat b Y)" = "$vZc $vYc"
fact "X1: merging a1 then b1 conflicts only in contract and spec files" test "$(mt_seq HEAD "$cA1" "$cB1")" = ".ait/$Y .ait/$Z specs/y.yaml specs/z.yaml"
want "X1: 'pull D O' stops, nothing changed" pull_unchanged "$D" "$O"
want "X1: stops at 3: Y and Z not named" eval 'stops_re 1 "^error: taking .* also changes contracts you did not name: " && head -1 "$T/err" | grep -q "'"'$Y'"'" && head -1 "$T/err" | grep -q "'"'$Z'"'"'
want "X1: hint names Y and Z too" err_re "^hint: name them too: 'ait contract pull $D $O ($Y $Z|$Z $Y)'\$"
want "X1: 'pull D O Y Z' stops, nothing changed" pull_unchanged "$D" "$O" "$Y" "$Z"
want "X1: stops to choose Y and Z" stops_re 1 "^error: '($Y|$Z)' has diverged between "
want "X1: the hint chooses Y and Z once in one command" err_re "^hint: pick one: .*'ait contract pull $D $O $Y=[^ ']+ $Z=[^ ']+ -m <why>'"
# record 115: a change that is not taken is given to git as the result's pin, so b1's merge base c1 sees no change
OLD=$(git rev-parse HEAD)
pull "$D" "$O" "$Y=$cA1" "$Z=$cB1" -m "pick a1 and b1"
want "X1: 'pull D O Y=a1 Z=b1 -m <why>' takes a1 and b1 together (one commit or one merge state)" takes_together "$OLD" "$cA1" "$cB1"
want "X1: D is a1's as is" wt_is D "$vDa"
want "X1: O is b1's as is" wt_is O "$vOb"
want "X1: Y: a1's body, prev {a1's Y, mine}" wt_result_is Y "$vYa" "$vYa" "$vYe"
want "X1: Z: b1's body, prev {b1's Z, mine}" wt_result_is Z "$vZb" "$vZb" "$vZe"
want "X1: specs/d.yaml d-a and specs/o.yaml o-b (one side changed each)" eval 'test "$(cat specs/d.yaml) $(cat specs/o.yaml)" = "body: d-a body: o-b"'
want "X1: one commit, no merge in progress, parents HEAD, a1, b1" eval '[ "$rc" = 0 ] && ! merging && [ "$(git rev-list --parents -n1 HEAD | cut -d" " -f2- | tr " " "\n" | sort | tr "\n" " ")" = "$(printf "%s\n" "$OLD" "$cA1" "$cB1" | sort | tr "\n" " ")" ]'
want "X1: specs/y.yaml as a1's Y pins it, specs/z.yaml as b1's Z pins it" eval '[ "$rc" = 0 ] && [ "$(at HEAD specs/y.yaml)" = "$(at "$cA1" specs/y.yaml)" ] && [ "$(at HEAD specs/z.yaml)" = "$(at "$cB1" specs/z.yaml)" ]'

# MS2  y1: c-K ; q1: c-M, q2: keeps M after seeing y1 (prev {M, K}) ; p2: picks y1 after seeing q1 (c-K, prev {M, K}) ;
#      q is squashed (qS) ; then a fresh clone
pbase
vD0=$(recat HEAD D)
git checkout -q -b y; cond D c-K; vK=$(rec D); c "y: c-K" >/dev/null; git checkout -q master
git checkout -q -b q; cond D c-M; vM=$(rec D); c "q: c-M" >/dev/null
cond D c-M; rec D future prev="$vM $vK" >/dev/null; c "q: pull D=HEAD (saw y1)" >/dev/null
git checkout -q master; git checkout -q -b p; git merge -q --ff-only y
cond D c-K; vP=$(rec D future prev="$vM $vK"); c "p: pull D=y1 (saw q1 and y1)" >/dev/null
git checkout -q q; git reset -q --soft master; c "q: squashed" >/dev/null; vQ=$(recat q D)
git checkout -q master
fact "MS2: the latest versions are p2's and qS's" test "$(latest "$vD0" "$vK" "$vP" "$vQ")" = "$(sorted "$vP" "$vQ")"
fact "MS2: both have prev {M, K}" test "$(prevs "$vP" | tr '\n' ' ')$(prevs "$vQ" | tr '\n' ' ')" = "$(sorted "$vM" "$vK") $(sorted "$vM" "$vK") "
fact "MS2: the newest common versions are M and K" test "$(newest_common "$vP" "$vQ")" = "$(sorted "$vM" "$vK")"
fact "MS2: M and K have different bodies, so neither moved forward" eval 'not same_body "$vM" "$vK" && not forward "$vP" "$vQ" && not forward "$vQ" "$vP"'
fact "MS2: q is squashed: q1 (holding M) is not reachable from q" test "$(git rev-list --count q ^master)" = 1
fact "MS2: new commits: p2 2, qS 1" test "$(new_count p) $(new_count q)" = "2 1"
want "MS2: 'pull D' stops, nothing changed" pull_unchanged "$D"
want "MS2: stops to choose" stops_re 1 "^error: '$D' has diverged between "
want "MS2: the hint offers the two versions, not HEAD" two_choices_no_head "$D"
git clone -q --no-local --no-single-branch "file://$T/r" "$T/z" && cd "$T/z" && ait init >/dev/null && ait session open --agent agent-z >/dev/null
fact "MS2 clone: M's blob is missing, K's is present" eval 'not has_obj "$vM" && has_obj "$vK"'
fact "MS2 clone: the same tips" test "$(recat origin/p D) $(recat origin/q D) $(recat origin/y D) $(recat HEAD D)" = "$vP $vQ $vK $vD0"
fact "MS2 clone: the latest versions are still p2's and qS's" test "$(latest "$vD0" "$vK" "$vP" "$vQ")" = "$(sorted "$vP" "$vQ")"
fact "MS2 clone: the newest common versions are M (missing) and K" test "$(newest_common "$vP" "$vQ")" = "$(sorted "$vM" "$vK")"
fact "MS2 clone: neither moved forward (M's body is not known)" eval 'not forward "$vP" "$vQ" && not forward "$vQ" "$vP"'
want "MS2 clone: 'pull D' stops, nothing changed" pull_unchanged "$D"
want "MS2 clone: stops to choose" stops_re 1 "^error: '$D' has diverged between "
want "MS2 clone: the hint offers the two versions, not HEAD" two_choices_no_head "$D"

# MS3  C's prev is a missing blob ; main: V (C's body, prev C) ; a1: W (changed, prev C) ; 'pull D'
pbase
gone=a35e538afcff9e0211e10d27139aef29b335ebe8
vC=$(rec D future prev=$gone); c "C (prev is a missing blob)" >/dev/null
git checkout -q -b a; cond D c-W; vW=$(rec D); cA1=$(c "a: W"); git checkout -q master
vV=$(rec D); c "e: same body, prev C" >/dev/null
fact "MS3: C's prev blob is missing" eval 'test "$(prevs "$vC")" = "$gone" && not has_obj "$gone"'
fact "MS3: V has C's body, prev C ; W changed, prev C" eval 'same_body "$vV" "$vC" && not same_body "$vW" "$vC" && test "$(prevs "$vV") $(prevs "$vW")" = "$vC $vC"'
fact "MS3: the newest common version is C (readable)" eval 'test "$(newest_common "$vV" "$vW")" = "$vC" && has_obj "$vC"'
fact "MS3: W moved forward from V (3-way)" forward "$vV" "$vW"
fact "MS3: V did not move forward from W" not forward "$vW" "$vV"
fact "MS3: a1 is one new commit" test "$(new_count a)" = 1
OLD=$(git rev-parse HEAD)
pull "$D"
want "MS3: pull D does not stop" test "$rc" = 0
want "MS3: new version: W's body, prev {V, W}" result_is D "$vW" "$vV" "$vW"
want "MS3: a commit on HEAD taking a1" parents_are "$OLD" "$cA1"
want "MS3: specs/d.yaml is c-W" file_is specs/d.yaml "body: c-W"
want "MS3: 'included D from ...'" included "$D"
done_testing
