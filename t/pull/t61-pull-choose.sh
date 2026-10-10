#!/bin/sh
# pull criteria 3.2 고르기: 둘 다 바꿨을 때
# (U1, U1b, U2, U3, U6, U8, NT2, NT3, CYC1, CYC2, KC2, U1c, T1r, U1d, RP3, RP4, RP5, RP6, U1z, RP7, U1m)
. "$(dirname "$0")/lib.sh"

# ---- helpers of this file
# choose KEEP "PREV..." MSG [MERGE]: what pull writes for a choice, as choose() in scen4.py does. Merge MERGE without
# committing (if given); .ait/D = KEEP's body with the check line of decision 22 (pool: PREV... and HEAD's D),
# prev = PREV...; the spec files it pins; commit. Prints the commit. For histories that hold an earlier pull.
choose() {
	ch_keep=$1 ch_prevs=$2 ch_msg=$3 ch_head=$(recat HEAD D)
	[ -n "$4" ] && { git merge -q --no-commit --no-ff "$4" >/dev/null 2>&1 || :; }
	git cat-file -p "$ch_keep" | grep -v -e '^prev ' -e '^check ' > "$T/ch.body"
	ch_ck=
	if grep -qx 'state current' "$T/ch.body" && grep -qE '^role (demand|offer)$' "$T/ch.body"; then
		ch_pins=$(grep -E '^(provider|file) ' "$T/ch.body" | git hash-object --stdin) ch_kind=forced
		for v in $ch_prevs $ch_head; do git cat-file -p "$v" | grep -qx "check reported $ch_pins" && ch_kind=reported; done
		ch_ck="check $ch_kind $ch_pins"
	fi
	{ awk -v ck="$ch_ck" '/^creator /&&ck!=""{print ck} {print}' "$T/ch.body"
	  for v in $(echo $ch_prevs | tr ' ' '\n' | sort -u); do echo "prev $v"; done; } > ".ait/$D"
	grep '^file ' "$T/ch.body" | while read -r _ fp _ fb _; do git cat-file -p "$fb" > "$fp"; done
	c "$ch_msg"
}
# anc V: the versions V contains (V and all reached through prev), sorted; a missing blob ends that branch
anc() {
	echo "$1" > "$T/an.todo"; : > "$T/an.seen"
	while [ -s "$T/an.todo" ]; do
		o=$(head -1 "$T/an.todo"); sed -i.bak 1d "$T/an.todo"
		grep -qx "$o" "$T/an.seen" && continue; echo "$o" >> "$T/an.seen"
		git cat-file -p "$o" 2>/dev/null | sed -n 's/^prev //p' | grep -v '^-$' >> "$T/an.todo"
	done
	sort "$T/an.seen"
}
# newest_common V W: the newest common versions of V and W (1절 앞으로 나감)
newest_common() {
	anc "$1" > "$T/an.v"; anc "$2" > "$T/an.w"; comm -12 "$T/an.v" "$T/an.w" > "$T/an.c"
	for x in $(cat "$T/an.c"); do nc_keep=1
		for y in $(cat "$T/an.c"); do [ "$x" != "$y" ] && contains "$y" "$x" && nc_keep=0; done
		[ $nc_keep = 1 ] && echo "$x"; done
}
# moved_forward V W: W moved forward from V (3-way, decision 15): the newest common versions all have one body B,
# V's body is B and W's is not; a newest common version that cannot be read makes it unknown (not forward)
moved_forward() {
	mf_c=$(newest_common "$1" "$2"); [ -n "$mf_c" ] || return 1
	for x in $mf_c; do git cat-file -e "$x" 2>/dev/null || return 1; done
	mf_b=$(echo $mf_c | cut -d' ' -f1)
	for x in $mf_c; do same_body "$x" "$mf_b" || return 1; done
	same_body "$1" "$mf_b" && not same_body "$2" "$mf_b"
}
two_latest() { not contains "$1" "$2" && not contains "$2" "$1"; }
both_changed() { not moved_forward "$1" "$2" && not moved_forward "$2" "$1"; }
prevs_are() { pa_v=$1; shift; [ "$(prevs "$pa_v" | tr '\n' ' ')" = "$(for x; do echo "$x"; done | sort -u | tr '\n' ' ')" ]; }
checked() { git cat-file -p "$1" | grep -qx "check reported $(git cat-file -p "$1" | grep -E '^(provider|file) ' | git hash-object --stdin)"; }
hd() { recat HEAD D; }
newc() { git rev-list --count "$1" ^HEAD; }
has_parent() { git rev-list --parents -n1 HEAD | cut -d' ' -f2- | tr ' ' '\n' | grep -qx "$(git rev-parse "$1")"; }
spec_is() { [ "$(git show HEAD:specs/d.yaml 2>/dev/null)" = "body: $1" ]; }
# the result of the last pull
ok_pull() { [ "$rc" = 0 ]; }
included() { [ "$rc" = 0 ] && grep -q "^included $D from '[^']*'\$" "$T/out"; }
kept() { [ "$rc" = 0 ] && out_line "kept $D"; }
already() { [ "$rc" = 0 ] && out_line "already $D"; }
# diverged N PART...: the stop of 2절 1 'error: 'D' has N latest versions: …'; each PART ('(a)', "'HEAD'") is in it
diverged() {
	dv_n=$1; shift; [ "$rc" = 1 ] || return 1
	dv_l=$(head -1 "$T/err")
	case $dv_l in "error: '$D' has $dv_n latest versions: "*) ;; *) return 1 ;; esac
	for x; do case $dv_l in *"$x"*) ;; *) return 1 ;; esac; done
}
# picks C...: the 'hint: pick one: …' line offers exactly 'ait contract pull D=C -m <why>' for these C (HEAD or a
# commit; a commit name in the hint is a placeholder, so it is matched by the commit it names)
picks() {
	grep '^hint: pick one: ' "$T/err" | head -1 | grep -o "'ait contract pull $D=[^ ']* -m <why>'" |
		sed "s/^'ait contract pull $D=//; s/ -m <why>'\$//" > "$T/picks"
	[ -s "$T/picks" ] || return 1
	pk_got=$(while read -r x; do if [ "$x" = HEAD ]; then echo HEAD; else git rev-parse -q --verify "$x^{commit}" || echo "?$x"; fi; done < "$T/picks" | sort | tr '\n' ' ')
	[ "$pk_got" = "$(for x; do echo "$x"; done | sort | tr '\n' ' ')" ]
}
reopened_warning() { grep -q "^warning: '$D' is reopened by this pull" "$T/err"; }
not_reopened() { [ "$rc" = 0 ] && ! grep -q "is reopened by this pull" "$T/err"; }
h10() { grep -qE "^hint: when ready, stage the code and run '(AIT_SESSION=[^ ]+ )?ait fulfill $D -m <why>' to get the tasks\$" "$T/err"; }
no_h10() { [ "$rc" = 0 ] && ! grep -qF "ait fulfill $D -m <why>" "$T/err"; }

# U1  a1: D c-A ; main: D c-E
u1_hist() {
	D0=$(recat init D)
	git checkout -q -b a; cond D c-A; VA=$(rec D); A1=$(c "a: D c-A"); git checkout -q master
	cond D c-E; VE=$(rec D); E1=$(c "e: D c-E")
}
pbase; u1_hist
fact "U1: a's D and mine are the two latest versions" two_latest "$VA" "$VE"
fact "U1: both contain init's D" eval 'contains "$VA" "$D0" && contains "$VE" "$D0"'
fact "U1: both changed since init (3-way leaves both)" both_changed "$VA" "$VE"
fact "U1: two bodies" not same_body "$VA" "$VE"
fact "U1: a1 is the one new commit" test "$(newc a)" = 1
fact "U1: merge-tree with a1 conflicts on .ait/D and specs/d.yaml" test "$(mt HEAD "$A1")" = ".ait/$D specs/d.yaml"
want "U1: 'pull D' stops, nothing changed" pull_unchanged "$D"
want "U1: error: 'D' has 2 latest versions: '…' (a), 'HEAD'" diverged 2 "(a)" "'HEAD'"
want "U1: hint picks D=a1 or D=HEAD" picks HEAD "$A1"
pull "$D=$A1" -m why
want "U1: 'pull D=a1 -m <why>': included D from '…'" included
want "U1: a1 is taken (parents e1 and a1)" eval '[ "$(parents)" = 2 ] && has_parent "$E1" && has_parent "$A1"'
want "U1: .ait/D is D@A's body with prev {D@A, D@E}" eval 'same_body "$(hd)" "$VA" && prevs_are "$(hd)" "$VA" "$VE"'
want "U1: specs/d.yaml as D@A pins it (c-A)" spec_is c-A
pull "$D"
want "U1: pulling again: already D" already

# U1b  U1, then 'pull D=HEAD -m <why>'
pbase; u1_hist
fact "U1b: a1 is not in main" not is_ancestor "$A1" HEAD
pull "$D=HEAD" -m why
want "U1b: kept D" kept
want "U1b: one commit on HEAD (parent e1 only)" eval '[ "$(parents)" = 1 ] && has_parent "$E1"'
want "U1b: a's commit does not come into main" eval 'ok_pull && not is_ancestor "$A1" HEAD'
want "U1b: .ait/D is D@E's body with prev {D@A, D@E}" eval 'same_body "$(hd)" "$VE" && prevs_are "$(hd)" "$VA" "$VE"'
want "U1b: specs/d.yaml stays c-E" eval 'ok_pull && spec_is c-E'
pull "$D"
want "U1b: pulling again: already D" already

# U2  a1: D c-A ; b1: c-B, bM merges a keeping c-B (D@B') ; f1: c-F, fM merges a keeping c-F (D@F') ; main: D0
pbase
D0=$(recat init D)
git checkout -q -b a; cond D c-A; VA=$(rec D); A1=$(c "a: D c-A"); git checkout -q master
git checkout -q -b b; cond D c-B; VB=$(rec D); c "b: D c-B" >/dev/null; BM=$(choose "$VB" "$VB $VA" "b: merge a, keep B" a)
git checkout -q master; git checkout -q -b f; cond D c-F; VF=$(rec D); c "f: D c-F" >/dev/null; FM=$(choose "$VF" "$VF $VA" "f: merge a, keep F" a)
git checkout -q master
BP=$(recat b D) FP=$(recat f D)
fact "U2: D@B' has c-B's body and prev {D@A, D@B}" eval 'same_body "$BP" "$VB" && prevs_are "$BP" "$VA" "$VB"'
fact "U2: D@F' has c-F's body and prev {D@A, D@F}" eval 'same_body "$FP" "$VF" && prevs_are "$FP" "$VA" "$VF"'
fact "U2: D@B' and D@F' are the two latest (both contain my D0)" eval 'two_latest "$BP" "$FP" && contains "$BP" "$D0" && contains "$FP" "$D0"'
fact "U2: both changed since D@A (3-way leaves both)" both_changed "$BP" "$FP"
fact "U2: two bodies" not same_body "$BP" "$FP"
fact "U2: bM and fM bring 3 new commits each" test "$(newc "$BM") $(newc "$FM")" = "3 3"
want "U2: 'pull D' stops, nothing changed" pull_unchanged "$D"
want "U2: error: 'D' has 2 latest versions (b, f)" diverged 2 "(b)" "(f)"
want "U2: hint picks D=bM or D=fM" picks "$BM" "$FM"

# U3  a1 fulfils D (current) ; main: D c-E
pbase
D0=$(recat init D)
git checkout -q -b a; line 1 A; VA=$(rec D current); A1=$(c "a: fulfil D"); git checkout -q master
cond D c-E; VE=$(rec D); E1=$(c "e: D c-E")
fact "U3: a's D is current with a check line whose PINS match" eval '[ "$(state_of "$VA")" = current ] && checked "$VA"'
fact "U3: a's D and mine are the two latest" two_latest "$VA" "$VE"
fact "U3: both changed since init (3-way leaves both)" both_changed "$VA" "$VE"
want "U3: 'pull D' stops, nothing changed" pull_unchanged "$D"
want "U3: error: 'D' has 2 latest versions: '…' (a), 'HEAD'" diverged 2 "(a)" "'HEAD'"
want "U3: hint picks D=a1 or D=HEAD" picks HEAD "$A1"

# U6  D current ; a1 and b1 each change the code and re-check it (same condition) ; main: D0
DSTATE=current pbase; unset DSTATE
D0=$(recat init D)
git checkout -q -b a; line 1 A; VA=$(rec D current); A1=$(c "a: recheck"); git checkout -q master
git checkout -q -b b; line 1 B; VB=$(rec D current); B1=$(c "b: recheck"); git checkout -q master
fact "U6: init's D is current and checked" eval '[ "$(state_of "$D0")" = current ] && checked "$D0"'
fact "U6: a's D and b's D are current and checked" eval 'checked "$VA" && checked "$VB"'
fact "U6: they are the two latest (both contain my D0)" eval 'two_latest "$VA" "$VB" && contains "$VA" "$D0" && contains "$VB" "$D0"'
fact "U6: both changed since D0 (3-way leaves both)" both_changed "$VA" "$VB"
fact "U6: only the pins (the provider row) differ" eval '[ "$(body "$VA" | grep -v "^provider ")" = "$(body "$VB" | grep -v "^provider ")" ] && not same_body "$VA" "$VB"'
want "U6: 'pull D' stops, nothing changed" pull_unchanged "$D"
want "U6: error: 'D' has 2 latest versions (a, b)" diverged 2 "(a)" "(b)"
want "U6: hint picks D=a1 or D=b1" picks "$A1" "$B1"

# U8  a1: D@A ; main takes a1 ; a amends a1 (a1': D@A2, prev D0)
pbase
D0=$(recat init D)
git checkout -q -b a; cond D c-A; VA=$(rec D); A1=$(c "a: D c-A"); git checkout -q master; git merge -q --ff-only a
git checkout -q a; cond D c-A2; VA2=$(rec D future prev="$D0"); git add -A; git commit -q --amend --no-verify -m "a: D c-A2"
A1P=$(git rev-parse HEAD); git checkout -q master
fact "U8: main is at a1 (D@A, prev D0)" eval '[ "$(git rev-parse HEAD)" = "$A1" ] && [ "$(hd)" = "$VA" ] && prevs_are "$VA" "$D0"'
fact "U8: a's tip a1' has D@A2 with prev D0, on init" eval '[ "$(recat a D)" = "$VA2" ] && prevs_are "$VA2" "$D0" && [ "$(git rev-parse a~1)" = "$(git rev-parse init)" ]'
fact "U8: my D@A and D@A2 are the two latest" two_latest "$VA" "$VA2"
fact "U8: both changed since D0 (newest common D0)" eval '[ "$(newest_common "$VA" "$VA2")" = "$D0" ] && both_changed "$VA" "$VA2"'
want "U8: 'pull D' stops, nothing changed" pull_unchanged "$D"
want "U8: error: 'D' has 2 latest versions: '…' (a), 'HEAD'" diverged 2 "(a)" "'HEAD'"
want "U8: hint picks D=a1' or D=HEAD" picks HEAD "$A1P"

# NT2  a1: D c-A, b1: D c-B ; A and B at once each 'pull D=HEAD' (a2, b2)
nt2_hist() {
	D0=$(recat init D)
	git checkout -q -b a; cond D c-A; VA=$(rec D); c "a: c-A" >/dev/null; git checkout -q master
	git checkout -q -b b; cond D c-B; VB=$(rec D); c "b: c-B" >/dev/null
	git checkout -q a; A2=$(choose "$VA" "$VA $VB" "a: pull D=HEAD")
	git checkout -q b; B2=$(choose "$VB" "$VA $VB" "b: pull D=HEAD")
	git checkout -q master
	A2V=$(recat a D) B2V=$(recat b D)
}
pbase; nt2_hist
fact "NT2: a2's D (c-A) and b2's D (c-B) both have prev {D@A, D@B}" eval 'prevs_are "$A2V" "$VA" "$VB" && prevs_are "$B2V" "$VA" "$VB" && same_body "$A2V" "$VA" && same_body "$B2V" "$VB"'
fact "NT2: seen from main they are the two latest" two_latest "$A2V" "$B2V"
fact "NT2: the newest common versions are D@A and D@B, with different bodies" eval '[ "$(newest_common "$A2V" "$B2V" | tr "\n" " ")" = "$(printf "%s\n%s\n" "$VA" "$VB" | sort | tr "\n" " ")" ] && not same_body "$VA" "$VB"'
fact "NT2: neither moved forward from the other" both_changed "$A2V" "$B2V"
want "NT2: main's 'pull D' stops, nothing changed" pull_unchanged "$D"
want "NT2: error: 'D' has 2 latest versions (a, b)" diverged 2 "(a)" "(b)"
want "NT2: hint picks D=a2 or D=b2" picks "$A2" "$B2"

# NT3  NT2, then A keeps its own again (a3), then B pulls D
pbase; nt2_hist
git checkout -q a; A3=$(choose "$A2V" "$A2V $B2V" "a: pull D=HEAD again"); A3V=$(recat a D)
git checkout -q b
fact "NT3: a3's D (c-A) has prev {a2's D, b2's D}" eval 'same_body "$A3V" "$VA" && prevs_are "$A3V" "$A2V" "$B2V"'
fact "NT3: a3's D contains B's version and main's" eval 'contains "$A3V" "$B2V" && contains "$A3V" "$D0"'
pull "$D"
want "NT3: B: 'pull D' includes D from a3" included
want "NT3: B takes a3 (a3 is a parent)" has_parent "$A3"
want "NT3: B's D is a3's version as is: the two meet" eval 'ok_pull && [ "$(hd)" = "$A3V" ] && [ "$(recat a D)" = "$(hd)" ]'
want "NT3: specs/d.yaml as a3's D pins it (c-A)" eval 'ok_pull && spec_is c-A'
pull "$D"
want "NT3: pulling again: already D" already

# CYC1  a1 c-P, b1 c-Q, c1 c-R (each from init) ; a keeps P after seeing c1 (a2), b keeps Q after seeing a1 (b2),
#       c keeps R after seeing b1 (c2)
cyc1_hist() {
	D0=$(recat init D)
	git checkout -q -b a; cond D c-P; VP=$(rec D); c "a: D c-P" >/dev/null; git checkout -q master
	git checkout -q -b b; cond D c-Q; VQ=$(rec D); c "b: D c-Q" >/dev/null; git checkout -q master
	git checkout -q -b c; cond D c-R; VR=$(rec D); c "c: D c-R" >/dev/null
	git checkout -q a; A2=$(choose "$VP" "$VP $VR" "a: pull D=HEAD (saw c1)")
	git checkout -q b; B2=$(choose "$VQ" "$VP $VQ" "b: pull D=HEAD (saw a1)")
	git checkout -q c; C2=$(choose "$VR" "$VQ $VR" "c: pull D=HEAD (saw b1)")
	git checkout -q master
	A2V=$(recat a D) B2V=$(recat b D) C2V=$(recat c D)
}
pbase; cyc1_hist
fact "CYC1: three latest versions" eval 'two_latest "$A2V" "$B2V" && two_latest "$B2V" "$C2V" && two_latest "$C2V" "$A2V"'
fact "CYC1: b2 moved forward from a2" moved_forward "$A2V" "$B2V"
fact "CYC1: c2 moved forward from b2" moved_forward "$B2V" "$C2V"
fact "CYC1: a2 moved forward from c2 (a cycle)" moved_forward "$C2V" "$A2V"
want "CYC1: 'pull D' stops, nothing changed" pull_unchanged "$D"
want "CYC1: error: 'D' has 3 latest versions (a, b, c)" diverged 3 "(a)" "(b)" "(c)"
want "CYC1: hint picks D=a2, D=b2 or D=c2" picks "$A2" "$B2" "$C2"
pull "$D=$B2" -m why
want "CYC1: following the hint D=b2 includes D" included
want "CYC1: D=b2 takes b2" has_parent "$B2"
want "CYC1: D=b2: c-Q's body with prev {a2's D, b2's D, c2's D}" eval 'same_body "$(hd)" "$B2V" && prevs_are "$(hd)" "$A2V" "$B2V" "$C2V"'

# CYC2  CYC1, then main picks D=b2 (pick) ; then main, a, b and c each 'pull D'
pbase; cyc1_hist
PICK=$(choose "$B2V" "$A2V $B2V $C2V" "main: pull D=b2" b); PV=$(recat master D)
fact "CYC2: the pick's D has c-Q's body and prev {a2's D, b2's D, c2's D}" eval 'same_body "$PV" "$B2V" && prevs_are "$PV" "$A2V" "$B2V" "$C2V"'
fact "CYC2: it contains the D of a, b and c (the only latest version for each)" eval 'contains "$PV" "$A2V" && contains "$PV" "$B2V" && contains "$PV" "$C2V"'
pull "$D"
want "CYC2: main: already D" already
for br in a b c; do
	git checkout -q $br; pull "$D"
	want "CYC2: $br: 'pull D' does not stop" ok_pull
	want "CYC2: $br: takes the result version as is" eval 'ok_pull && [ "$(hd)" = "$PV" ] && is_ancestor "$PICK" HEAD'
	want "CYC2: $br: specs/d.yaml c-Q" eval 'ok_pull && spec_is c-Q'
done
git checkout -q b
want "CYC2: b fast-forwards to the pick" test "$(git rev-parse HEAD)" = "$PICK"
git checkout -q c; pull "$D"
want "CYC2: c: pulling again: already D" already

# KC2  T1's history (U1, main 'pull D=a1', then a2: D c-A2) ; 'pull D=a2 -m <why>'
pbase; u1_hist
choose "$VA" "$VA $VE" "pull D=a1" "$A1" >/dev/null; MINE=$(hd)
git checkout -q a; cond D c-A2; VA2=$(rec D); A2=$(c "a: D c-A2"); git checkout -q master
fact "KC2: my D is D@A's body with prev {D@A, D@E}" eval 'same_body "$MINE" "$VA" && prevs_are "$MINE" "$VA" "$VE"'
fact "KC2: a2's D moved forward from mine (the only remaining version)" eval 'moved_forward "$MINE" "$VA2" && not moved_forward "$VA2" "$MINE" && two_latest "$MINE" "$VA2"'
fact "KC2: a2's D does not contain mine" not contains "$VA2" "$MINE"
pull "$D=$A2" -m why
want "KC2: 'pull D=a2 -m <why>' includes D" included
want "KC2: a2 is taken" has_parent "$A2"
want "KC2: new version: c-A2's body with prev {a2's D, my D}" eval 'same_body "$(hd)" "$VA2" && prevs_are "$(hd)" "$VA2" "$MINE"'
want "KC2: specs/d.yaml c-A2" eval 'ok_pull && spec_is c-A2'

# U1c  a1 fulfils D (C, current) ; main: D c-E (E) ; main 'pull D=HEAD -m <why>'
u1c_hist() {
	D0=$(recat init D)
	git checkout -q -b a; line 1 A; VC=$(rec D current); A1=$(c "a: fulfil D"); git checkout -q master
	cond D c-E; VE=$(rec D); E1=$(c "e: c-E")
}
pbase; u1c_hist
fact "U1c: a's C is current and checked ; my E is future" eval 'checked "$VC" && [ "$(state_of "$VE")" = future ]'
fact "U1c: C and E are the two latest, both changed since init" eval 'two_latest "$VC" "$VE" && both_changed "$VC" "$VE"'
fact "U1c: my E (HEAD's, the pull commit's parent) contains neither C nor a version with C's body" eval 'not contains "$VE" "$VC" && not same_body "$VE" "$VC" && not same_body "$D0" "$VC"'
pull "$D=HEAD" -m why
want "U1c: kept D" kept
want "U1c: one commit on HEAD (parent e1 only)" eval 'ok_pull && [ "$(parents)" = 1 ] && has_parent "$E1"'
want "U1c: the new version is future, E's body, prev {C, E}" eval '[ "$(state_of "$(hd)")" = future ] && same_body "$(hd)" "$VE" && prevs_are "$(hd)" "$VC" "$VE"'
want "U1c: warning: 'D' is reopened by this pull: it keeps the future version over the current one" err_line "warning: '$D' is reopened by this pull: it keeps the future version over the current one"
want "U1c: H10 for D" h10

# T1r  D current ; a1: re-check ; a2: 'future amend D' (R) ; main changed only code (e1) ; 'pull D'
DSTATE=current pbase; unset DSTATE
D0=$(recat init D)
git checkout -q -b a; line 1 A; VC1=$(rec D current); A1=$(c "a: recheck"); VR=$(rec D future); A2=$(c "a: reopen D (future amend)")
git checkout -q master; echo 'order E' > src/order.c; E1=$(c "e: code only")
fact "T1r: main did not change D" test "$(hd)" = "$D0"
fact "T1r: a2's R is the only latest version and contains mine" eval 'contains "$VR" "$D0" && not contains "$D0" "$VR"'
fact "T1r: a2's R is future with prev a1's current version (a reopened it)" eval '[ "$(state_of "$VR")" = future ] && prevs_are "$VR" "$VC1" && [ "$(state_of "$VC1")" = current ] && [ "$(recat "$A1" D)" = "$VC1" ]'
pull "$D"
want "T1r: 'pull D' includes D" included
want "T1r: a2 is taken" has_parent "$A2"
want "T1r: .ait/D is a2's R as is" eval 'ok_pull && [ "$(hd)" = "$VR" ]'
want "T1r: main is not the reopener: no 'is reopened by this pull'" not_reopened

# U1d  a1 fulfils D (C) ; main changes D's condition (H) ; 'pull D=HEAD future amend D (consumer) -m <why>'
other() { echo o > src/other.c; }
pbase other
git checkout -q -b a; line 1 A; VC=$(rec D current); A1=$(c "a: fulfil D"); git checkout -q master
cond D c-H; VH=$(rec D); E1=$(c "e: c-H")
fact "U1d: a's C is current ; my H is future" eval 'checked "$VC" && [ "$(state_of "$VH")" = future ]'
fact "U1d: H (HEAD's, the only parent's) does not contain C" not contains "$VH" "$VC"
fact "U1d: C and H are the two latest, both changed" eval 'two_latest "$VC" "$VH" && both_changed "$VC" "$VH"'
pull "$D=HEAD" future amend "$D" src/other.c - - -m why
want "U1d: the pull succeeds" ok_pull
want "U1d: the pull commit's only parent is e1" eval 'ok_pull && [ "$(parents)" = 1 ] && has_parent "$E1"'
want "U1d: D is future with consumer src/other.c" eval '[ "$(state_of "$(hd)")" = future ] && git cat-file -p "$(hd)" | grep -q "^consumer src/other.c "'
want "U1d: prev is the middle version and its prev {C, H}" eval 'ok_pull && [ "$(prevs "$(hd)" | wc -l | tr -d " ")" = 3 ] && prevs "$(hd)" | grep -qx "$VC" && prevs "$(hd)" | grep -qx "$VH"'
want "U1d: the middle version is in no commit" eval 'ok_pull && v1=$(prevs "$(hd)" | grep -vx -e "$VC" -e "$VH") && [ -n "$v1" ] && ! git rev-list --all | while read -r x; do recat "$x" D; done | grep -qx "$v1"'
want "U1d: a reopen commit: warning: 'D' is reopened by this pull" reopened_warning
want "U1d: H10 for D" h10

# RP3  main took D=a1 (V1: C's body, prev {C, E}) ; then a reopened C (R, a2) ; main 'pull D'
rp3_hist() {
	git checkout -q -b a; line 1 A; VC=$(rec D current); A1=$(c "a: fulfil D"); git checkout -q master
	cond D c-E; VE=$(rec D); E1=$(c "e: c-E")
	choose "$VC" "$VC $VE" "pull D=a1" a >/dev/null; V1=$(hd)
	git checkout -q a; VR=$(rec D future); A2=$(c "a: reopen D"); git checkout -q master
}
pbase; rp3_hist
fact "RP3: my V1 is current, C's body, prev {C, E}" eval '[ "$(state_of "$V1")" = current ] && checked "$V1" && same_body "$V1" "$VC" && prevs_are "$V1" "$VC" "$VE"'
fact "RP3: a2's R is future with prev C" eval '[ "$(state_of "$VR")" = future ] && prevs_are "$VR" "$VC"'
fact "RP3: R moved forward from V1" eval 'moved_forward "$V1" "$VR" && two_latest "$V1" "$VR"'
fact "RP3: the parent a2's R contains C (V1's body)" contains "$VR" "$VC"
pull "$D"
want "RP3: 'pull D' includes D" included
want "RP3: a2 is taken" has_parent "$A2"
want "RP3: new version: R's body with prev {R, V1}" eval 'same_body "$(hd)" "$VR" && prevs_are "$(hd)" "$VR" "$V1"'
want "RP3: not a reopen commit (it carries a's reopen)" not_reopened

# RP4  D0 current, 'check forced' ; main reopens it (R) ; a1 re-checks the same code with -r (C', 'check reported')
pbase
rec D current check=forced prev=- >/dev/null; git add -A; git commit -q --amend --no-verify -m init; git tag -f init >/dev/null
D0=$(recat init D)
git checkout -q -b a; VCP=$(rec D current check=reported); A1=$(c "a: re-check with -r"); git checkout -q master
VR=$(rec D future); E1=$(c "e: reopen D")
fact "RP4: D0 is current with 'check forced'" eval 'git cat-file -p "$D0" | grep -q "^check forced " && [ "$(state_of "$D0")" = current ]'
fact "RP4: C' is current with 'check reported', prev D0" eval 'checked "$VCP" && prevs_are "$VCP" "$D0"'
fact "RP4: C' and D0 differ only in the check line (same body)" eval 'same_body "$VCP" "$D0" && [ "$VCP" != "$D0" ]'
fact "RP4: main's R is future with prev D0 (main reopened D in e1)" eval '[ "$(state_of "$VR")" = future ] && prevs_are "$VR" "$D0"'
fact "RP4: R moved forward from C'" eval 'moved_forward "$VCP" "$VR" && two_latest "$VCP" "$VR"'
fact "RP4: HEAD's R contains D0, which has C''s body" eval 'contains "$VR" "$D0" && same_body "$D0" "$VCP"'
pull "$D"
want "RP4: 'pull D' succeeds" ok_pull
want "RP4: new version: R's body with prev {C', R}" eval 'same_body "$(hd)" "$VR" && prevs_are "$(hd)" "$VCP" "$VR"'
want "RP4: not a reopen commit (it covered a re-check of the same body)" not_reopened
want "RP4: kept D (the result's body is mine, so no commit is taken)" kept
want "RP4: a commit on HEAD alone" test "$rc" = 0 -a "$(git rev-list --parents -n1 HEAD | wc -w | tr -d ' ')" = 2

# RP5  RP3's pull commit: v14 1.5d as written counts it, the one formula does not
pbase; rp3_hist
fact "RP5: the parent HEAD's V1 is current (v14 1.5d's old formula would count the pull commit)" eval '[ "$(state_of "$V1")" = current ]'
fact "RP5: the parent a2's R is future and contains C" eval '[ "$(state_of "$VR")" = future ] && contains "$VR" "$VC"'
pull "$D"; RP5RC=$rc
want "RP5: not a reopen commit: no 'is reopened by this pull'" not_reopened
want "RP5: no H10 for D in the pull" no_h10
run ait todo
want "RP5: main's todo gives no H10 for D afterwards" eval '[ "$RP5RC" = 0 ] && ! grep -qF "ait fulfill $D -m <why>" "$T/err"'

# RP6  a1 fulfils D (C, code as is), main takes it ; x1 reopens, x2 c-B ; y1 (from init) c-B ; 'pull D'
pbase
D0=$(recat init D)
git checkout -q -b a; VC=$(rec D current); A1=$(c "a1: fulfil D (code as is)"); git checkout -q master; git merge -q --ff-only a
git checkout -q -b x; VRX=$(rec D future); X1=$(c "x1: reopen D"); cond D c-B; VFX=$(rec D); X2=$(c "x2: c-B")
git checkout -q -b y init; cond D c-B; VFY=$(rec D); Y1=$(c "y1: c-B")
git checkout -q master
fact "RP6: main holds C (current)" eval '[ "$(hd)" = "$VC" ] && checked "$VC"'
fact "RP6: x2's D and y1's D are the latest, with one body" eval 'two_latest "$VFX" "$VFY" && contains "$VFX" "$VC" && same_body "$VFX" "$VFY"'
fact "RP6: x2's D contains C (through x1), y1's does not" eval 'contains "$VFX" "$VC" && prevs_are "$VFX" "$VRX" && not contains "$VFY" "$VC"'
fact "RP6: x1 reopened C (x1's D future, prev C)" eval '[ "$(state_of "$VRX")" = future ] && prevs_are "$VRX" "$VC"'
fact "RP6: new commits: x2 2, y1 1" test "$(newc x) $(newc y)" = "2 1"
pull "$D"
want "RP6: 'pull D' includes D" included
want "RP6: TIE takes x2 (it contains my version), not y1" eval 'has_parent "$X2" && not is_ancestor "$Y1" HEAD'
want "RP6: new version: c-B's body with prev {x2's D, y1's D}" eval 'same_body "$(hd)" "$VFX" && prevs_are "$(hd)" "$VFX" "$VFY"'
want "RP6: not a reopen commit (x is the reopener)" not_reopened

# U1z  U1c, then a clone z that holds main only
pbase; u1c_hist
KEPT=$(choose "$VE" "$VC $VE" "pull D=HEAD"); KV=$(hd)
git clone -q --no-local --single-branch -b master "$T/r" "$T/z"
fact "U1z: here the pull commit's D is future with prev {C, E} ; C is current" eval '[ "$(state_of "$KV")" = future ] && prevs_are "$KV" "$VC" "$VE" && [ "$(state_of "$VC")" = current ]'
fact "U1z: here its only parent's D (E) is future and does not contain C" eval '[ "$(recat "$KEPT~1" D)" = "$VE" ] && not contains "$VE" "$VC"'
fact "U1z: z holds the pull commit" test "$(cd "$T/z" && git rev-parse HEAD)" = "$KEPT"
fact "U1z: z does not hold C's blob" eval '(cd "$T/z" && not git cat-file -e "$VC" 2>/dev/null)'
fact "U1z: z does not hold a1" eval '(cd "$T/z" && not git rev-parse -q --verify "$A1^{commit}" >/dev/null)'
# no want: see the report (which command shows, in z, that the commit is not counted)

# RP7  a1 fulfils D (C), main takes it ; x1 reopens D and writes c-B in one commit ; y1 (from init) c-B ; 'pull D' ;
#      twice, with x1 < y1 and with y1 < x1
for order in x1y1 y1x1; do
	pbase
	D0=$(recat init D)
	git checkout -q -b a; VC=$(rec D current); A1=$(c "a1: fulfil D"); git checkout -q master; git merge -q --ff-only a
	git checkout -q -b y init; cond D c-B; VFY=$(rec D); Y1=$(c "y1: c-B")
	dots=
	while :; do
		git checkout -q -B x master; rec D future >/dev/null; cond D c-B; VFX=$(rec D future prev="$VC"); X1=$(c "x1: reopen and c-B$dots")
		first=$(printf '%s\n%s\n' "$X1" "$Y1" | sort | head -1)
		{ [ $order = x1y1 ] && [ "$first" = "$X1" ]; } || { [ $order = y1x1 ] && [ "$first" = "$Y1" ]; } && break
		dots="$dots."
	done
	git checkout -q master
	fact "RP7 ($order): the commit IDs are in this order" test "$first" = "$(if [ $order = x1y1 ]; then echo "$X1"; else echo "$Y1"; fi)"
	fact "RP7 ($order): x1's D and y1's D are the latest, with one body" eval 'two_latest "$VFX" "$VFY" && same_body "$VFX" "$VFY"'
	fact "RP7 ($order): x1's D is future and contains C ; y1's does not" eval '[ "$(state_of "$VFX")" = future ] && prevs_are "$VFX" "$VC" && not contains "$VFY" "$VC"'
	fact "RP7 ($order): one new commit each" test "$(newc x) $(newc y)" = "1 1"
	pull "$D"
	want "RP7 ($order): 'pull D' includes D" included
	want "RP7 ($order): x1 is taken whatever the commit IDs, not y1" eval 'has_parent "$X1" && not is_ancestor "$Y1" HEAD'
	want "RP7 ($order): new version: c-B's body with prev {x1's D, y1's D}" eval 'same_body "$(hd)" "$VFX" && prevs_are "$(hd)" "$VFX" "$VFY"'
	want "RP7 ($order): not a reopen commit (x is the reopener)" not_reopened
done

# U1m  a1 fulfils D (C), main takes it ; a: reopen (R1, a2), then c-R (R2, a3), squashed into a2s ;
#      clone z lacks R1 ; z: 'pull D=a2s -m <why>'
pbase
D0=$(recat init D)
git checkout -q -b a; VC=$(rec D current); A1=$(c "a1: fulfil D"); git checkout -q master; git merge -q --ff-only a
git checkout -q a; VR1=$(rec D future); c "a2: future amend D" >/dev/null; cond D c-R; VR2=$(rec D); c "a3: c-R" >/dev/null
git reset -q --soft "$A1"; A2S=$(c "a2s: squash"); git checkout -q master
fact "U1m: a2s is on a1 and holds R2 (prev R1) ; R1 is future with prev C" eval '[ "$(git rev-parse "$A2S~1")" = "$A1" ] && [ "$(recat "$A2S" D)" = "$VR2" ] && prevs_are "$VR2" "$VR1" && prevs_are "$VR1" "$VC" && [ "$(state_of "$VR1")" = future ]'
fact "U1m: here (R1 readable) R2 contains C" contains "$VR2" "$VC"
git clone -q --no-local --no-single-branch "$T/r" "$T/z"
cd "$T/z"; ait init >/dev/null; ZSID=$(ait session open --agent agent-z)
fact "U1m: z does not hold R1's blob" not git cat-file -e "$VR1" 2>/dev/null
fact "U1m: in z, R2 cannot be followed to C" not contains "$VR2" "$VC"
fact "U1m: in z, C (HEAD's) and R2 are two latest with different bodies" eval '[ "$(hd)" = "$VC" ] && two_latest "$VC" "$VR2" && not same_body "$VC" "$VR2"'
fact "U1m: in z, no readable newest common version: R2 is not forward of C" both_changed "$VC" "$VR2"
want "U1m: z: 'pull D' stops, nothing changed" pull_unchanged "$D"
want "U1m: z: error: 'D' has 2 latest versions" diverged 2 "'HEAD'"
want "U1m: z: hint picks D=a2s or D=HEAD" picks HEAD "$A2S"
pull "$D=$A2S" -m why
want "U1m: z: 'pull D=a2s -m <why>' includes D" included
want "U1m: z: a2s is taken" has_parent "$A2S"
want "U1m: z: new version: R2's body with prev {C, R2}" eval 'same_body "$(hd)" "$VR2" && prevs_are "$(hd)" "$VC" "$VR2"'
want "U1m: z: not a reopen commit (a blob it needs is missing)" not_reopened
cd "$T/r"

done_testing
