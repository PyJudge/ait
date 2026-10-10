#!/bin/sh
# pull criteria 3.7 옛 판, 지운 레코드, 사고 판, 형식이 틀린 판
# (RV2, DEL1, RV3, R1, R2, R3, CK1, CK2, CK3, MF1, ACC1, ID1, MF2)
. "$(dirname "$0")/lib.sh"

# git without the ait hooks, as an agent's plain git command (c uses --no-verify the same way)
nohook() { git -c core.hooksPath=/dev/null "$@"; }
# re TEMPLATE: an ERE for the text; <c> stands for a commit name ('a1' in the criteria), <p> for one in parentheses
re() { printf '%s' "$1" | sed -e 's/[][\.*^$(){}?+|]/\\&/g' -e "s/<c>/[^']+/g" -e 's/<p>/[^)]+/g'; }
err_re() { grep -qE -- "^$(re "$1")\$" "$T/err"; }
out_re() { grep -qE -- "^$(re "$1")\$" "$T/out"; }
first_re() { head -1 "$T/err" | grep -qE -- "^$(re "$1")\$"; }
# stops RC TEMPLATE: exit code RC, and the first stderr line that is not a warning starts with TEMPLATE
stops() { [ "$rc" = "$1" ] && grep -v '^warning: ' "$T/err" | head -1 | grep -qE -- "^$(re "$2")"; }
# par N: the N-th parent of HEAD
par() { git rev-parse -q --verify "HEAD^$1"; }
has_check() { git cat-file -p "$1" | grep -q '^check '; }
# the check line's PINS are the PINS of the version's provider and file rows (1절 확인 행)
pins_ok() { [ "$(git cat-file -p "$1" | sed -n 's/^check [a-z]* //p')" = "$(git cat-file -p "$1" | grep -E '^(provider|file) ' | git hash-object --stdin)" ]; }
has_cr() { [ "$(git cat-file -p "$1" | tr -d '\r' | git hash-object --stdin)" != "$1" ]; }
# pinned REV ID PATH: the blob the version of ID at REV pins PATH at (the words of its file row)
pinned() { git cat-file -p "$(recat "$1" "$2")" | awk -v p="$3" '$1 == "file" && $2 == p { print $4 }'; }
q0() { cond Q q0; rec Q >/dev/null; }

# RV2  main took b1 (D@B) ; b reverted b1 (b2) and changed Q (b3) ; 'pull Q'
pbase q0
git checkout -q -b b; cond D c-B; DB=$(rec D); B1=$(c "b: D c-B"); git checkout -q master; nohook merge -q --ff-only b
git checkout -q b; tick; nohook revert --no-edit "$B1" >/dev/null; cond Q q-B; QB=$(rec Q); B3=$(c "b: Q"); git checkout -q master
D0=$(recat init D) Q0=$(recat init Q)
fact "RV2: HEAD is b1, an ancestor of b3 (git would fast-forward)" test "$(git rev-parse HEAD)" = "$B1" -a -n "$(is_ancestor HEAD "$B3" && echo y)"
fact "RV2: b3's D is init's D0, an old version contained in my D@B" test "$(recat "$B3" D)" = "$D0" -a "$(recat HEAD D)" = "$DB"
fact "RV2: my D@B contains b3's D" contains "$DB" "$D0"
fact "RV2: b3's Q contains my Q" contains "$QB" "$Q0"
fact "RV2: merging b3 conflicts nowhere" test -z "$(mt HEAD "$B3")"
fact "RV2: merging takes .ait/D and specs/d.yaml back to the old ones" test "$(git diff --name-only HEAD "$B3" | tr '\n' ' ')" = ".ait/$D .ait/$Q specs/d.yaml specs/q.yaml " -a "$(at "$B3" specs/d.yaml)" = "$(at init specs/d.yaml)"
pull "$Q"
want "RV2: pull Q passes" test "$rc" = 0
want "RV2: 'included Q from ...'" out_re "included $Q from '<c>'"
want "RV2: Q is b3's version" test "$(recat HEAD Q)" = "$QB"
want "RV2: D is not incoming: D@B as it was" test "$(recat HEAD D)" = "$DB"
want "RV2: specs/d.yaml stays D@B's pin" test "$(at HEAD specs/d.yaml)" = "$(at "$B1" specs/d.yaml)"
want "RV2: specs/q.yaml is b3's" test "$(at HEAD specs/q.yaml)" = "$(at "$B3" specs/q.yaml)"
want "RV2: not a fast-forward but a merge commit of b1 and b3" test "$(parents)" = 2 -a "$(par 1) $(par 2)" = "$B1 $B3"

# DEL1  a1: git rm .ait/Q ; a2: D ; 'pull D'
pbase q0
git checkout -q -b a; git rm -q ".ait/$Q"; A1=$(c "a: rm Q"); cond D c-A; DA=$(rec D); A2=$(c "a: D"); git checkout -q master
D0=$(recat init D) Q0=$(recat init Q)
fact "DEL1: a2 has no .ait/Q" test -z "$(recat "$A2" Q)"
fact "DEL1: merging a2 conflicts nowhere and removes .ait/Q" test -z "$(mt HEAD "$A2")" -a "$(git diff --name-only HEAD "$A2" | tr '\n' ' ')" = ".ait/$D .ait/$Q specs/d.yaml "
pull "$D"
want "DEL1: pull D passes" test "$rc" = 0
want "DEL1: Q stays as mine" test "$(recat HEAD Q)" = "$Q0"
want "DEL1: D is a2's version" test "$(recat HEAD D)" = "$DA"
want "DEL1: warning on the removal" err_re "warning: skipping the removal of '$Q' in '<c>': records are never removed"
want "DEL1: 'included D from ...'" out_re "included $D from '<c>'"
want "DEL1: the result is not a2's tree: a merge commit of HEAD and a2" test "$(parents)" = 2 -a "$(par 2)" = "$A2"

# RV3  main took a1 (D@A) then git-reverted it ; 'pull D'
pbase
git checkout -q -b a; cond D c-A; DA=$(rec D); A1=$(c "a: D c-A"); git checkout -q master; nohook merge -q --ff-only a
tick; nohook revert --no-edit HEAD >/dev/null; E=$(git rev-parse HEAD)
D0=$(recat init D)
fact "RV3: main is back at D0" test "$(recat HEAD D)" = "$D0"
fact "RV3: a's D@A contains my D0 (the remaining version is D@A)" contains "$DA" "$D0"
fact "RV3: its introducing commit a1 is an ancestor of HEAD" is_ancestor "$A1" HEAD
pull "$D"
want "RV3: pull D passes" test "$rc" = 0
want "RV3: 'included D'" grep -qE "^included $D( |\$)" "$T/out"
want "RV3: a commit on top of HEAD, nothing taken" test "$(parents)" = 1 -a "$(par 1)" = "$E"
want "RV3: D is D@A" test "$(recat HEAD D)" = "$DA"
want "RV3: specs/d.yaml is D@A's pin" test "$(at HEAD specs/d.yaml)" = "$(pinned "$A1" D specs/d.yaml)"
want "RV3: a request commit (record 74)" grep -q '^request ' "$T/out"

# RV4  both introducing commits are ancestors after reverting their resolved merge
rv4_init() { echo 'body: s0' > specs/shared.yaml; rec Q future spec=specs/shared.yaml >/dev/null; }
pbase rv4_init
rec D future spec=specs/shared.yaml prev=- >/dev/null; c "D and Q share a specification" >/dev/null
BASE=$(git rev-parse HEAD)
git checkout -q -b a; cond D s-A specs/shared.yaml
DA=$(rec D future spec=specs/shared.yaml); QA=$(rec Q future spec=specs/shared.yaml); A1=$(c a1)
git checkout -q -b b "$BASE"; cond D s-B specs/shared.yaml
DB=$(rec D future spec=specs/shared.yaml); QB=$(rec Q future spec=specs/shared.yaml); B1=$(c b1)
git checkout -q master; cond D s-M specs/shared.yaml
rec D future spec=specs/shared.yaml prev="$DA $DB" >/dev/null
rec Q future spec=specs/shared.yaml prev="$QA $QB" >/dev/null
git add -A; TREE=$(git write-tree)
M=$(git commit-tree "$TREE" -p "$BASE" -p "$A1" -p "$B1" -m resolved)
nohook reset --hard "$M" >/dev/null; tick; nohook revert --no-edit -m 1 "$M" >/dev/null
E=$(git rev-parse HEAD)
fact "RV4: both introducing commits are ancestors, so there are no ends" is_ancestor "$A1" "$E"
fact "RV4: b1 is also an ancestor" is_ancestor "$B1" "$E"
fact "RV4: the revert restored the old records and shared file" test "$(recat HEAD D) $(recat HEAD Q) $(at HEAD specs/shared.yaml)" = "$(recat "$BASE" D) $(recat "$BASE" Q) $(at "$BASE" specs/shared.yaml)"
fact "RV4: the selected versions require different blobs at one path" test "$(pinned "$A1" D specs/shared.yaml)" != "$(pinned "$B1" Q specs/shared.yaml)"
nohook merge "$A1" "$B1" >"$T/git-merge" 2>&1
fact "RV4: git does not merge ancestors again" test "$(git rev-parse HEAD)" = "$E" -a "$(cat "$T/git-merge")" = 'Already up to date.'
rv4_hints() {
	grep '^hint: ' "$T/err" | grep -o "'ait contract pull [^']*'" | tr -d "'" | while read -r command; do
		set -- $command; shift 3
		for arg; do
			case $arg in ?*=?*) printf '%s=%s ' "${arg%%=*}" "$(git rev-parse -q --verify "${arg#*=}^{commit}")" ;; *) printf '%s ' "$arg" ;; esac
		done
		echo
	done | sort > "$T/rv4-hints"
	[ "$(cat "$T/rv4-hints")" = "$(printf '%s\n' "$D=$A1 $Q=$A1 -m <why> " "$D=$B1 $Q=$B1 -m <why> " | sort)" ]
}
want "RV4: conflicting pins stop without changing anything" pull_unchanged "$D=$A1" "$Q=$B1" -m why
want "RV4: the failure is bundle integrity" stops_with 1 'error: spec files do not match the contract:'
want "RV4: hints contain only the two consistent choices" rv4_hints
want "RV4: no merge state is created" not merging
want "RV4: force cannot bypass conflicting pins" pull_unchanged "$D=$A1" "$Q=$B1" --force -m why
want "RV4: force still stops at bundle integrity" stops_with 1 'error: spec files do not match the contract:'
pull "$D=$A1" "$Q=$A1" -m why
want "RV4: choosing consistent pins succeeds" test "$rc" = 0
want "RV4: the result has HEAD as its only parent" test "$(parents)" = 1 -a "$(par 1)" = "$E"
want "RV4: the shared file is restored to the selected pin" test "$(at HEAD specs/shared.yaml)" = "$(pinned "$A1" D specs/shared.yaml)"
want "RV4: consistent restoration does not create merge state" not merging

# R1  a: cancels D (a1), then hand-edits it back to future with prev a1 (a2)
pbase
git checkout -q -b a; DX=$(rec D cancelled); A1=$(c "a: cancel"); DH=$(rec D future); A2=$(c "a: hand edit"); git checkout -q master
D0=$(recat init D)
fact "R1: a2's D is future and its prev is a1's cancelled D" test "$(state_of "$DH")" = future -a "$(prevs "$DH")" = "$DX" -a "$(state_of "$DX")" = cancelled
fact "R1: a2's D contains the cancelled version" contains "$DH" "$DX"
fact "R1: ... and its body is not the cancelled one" not same_body "$DH" "$DX"
want "R1: already D, nothing changed" pull_unchanged "$D"
want "R1: 'already D'" test "$rc" = 0 -a -n "$(out_line "already $D" && echo y)"
want "R1: skips a's D with a warning" err_re "warning: skipping '$D' at 'a' (<p>): it was changed outside ait's rules"
pull "$D"
want "R1: again: the skip warning comes until a is fixed" err_re "warning: skipping '$D' at 'a' (<p>): it was changed outside ait's rules"

# R2  a1: D@A ; main takes a1 ; a: git revert a1 (back to D0)
pbase
git checkout -q -b a; cond D c-A; DA=$(rec D); A1=$(c "a: D c-A"); git checkout -q master; nohook merge -q --ff-only a
git checkout -q a; tick; nohook revert --no-edit HEAD >/dev/null; git checkout -q master
D0=$(recat init D)
fact "R2: a's D after the revert is init's D0" test "$(recat a D)" = "$D0"
fact "R2: my D@A contains a's D0" contains "$(recat HEAD D)" "$D0"
want "R2: already D, nothing changed" pull_unchanged "$D"
want "R2: 'already D'" test "$rc" = 0 -a -n "$(out_line "already $D" && echo y)"

# R3  R2, then a undoes the revert (a3) and changes the condition back to c0 with ait (a4)
pbase
git checkout -q -b a; cond D c-A; DA=$(rec D); A1=$(c "a: D c-A"); git checkout -q master; nohook merge -q --ff-only a
git checkout -q a; tick; nohook revert --no-edit HEAD >/dev/null
tick; nohook revert --no-edit HEAD >/dev/null; cond D c0; D4=$(rec D); A4=$(c "a: amend D back to c0 (ait)"); git checkout -q master
D0=$(recat init D)
fact "R3: a3 brings D@A back" test "$(recat "$A4^" D)" = "$DA"
fact "R3: a4's D contains my D@A" contains "$D4" "$DA"
fact "R3: a4's D has D0's body (prev D@A)" test "$(prevs "$D4")" = "$DA" -a -n "$(same_body "$D4" "$D0" && echo y)"
fact "R3: a4 brings 3 new commits" test "$(git rev-list --count "$A4" ^HEAD)" = 3
fact "R3: merging a4 conflicts nowhere and changes .ait/D and specs/d.yaml" test -z "$(mt HEAD "$A4")" -a "$(git diff --name-only HEAD "$A4" | tr '\n' ' ')" = ".ait/$D specs/d.yaml "
pull "$D"
want "R3: pull D passes" test "$rc" = 0
want "R3: takes a4 (a fast-forward)" test "$(git rev-parse HEAD)" = "$A4"
want "R3: D is a4's version" test "$(recat HEAD D)" = "$D4"
want "R3: 'included D from ...'" out_re "included $D from '<c>'"

# CK1  a1: D made current by hand, no check line
pbase
git checkout -q -b a; line 1 A; DC=$(rec D current check=none); A1=$(c "a: current without check"); git checkout -q master
fact "CK1: a's D is current without a check line" test "$(state_of "$DC")" = current -a -z "$(has_check "$DC" && echo y)"
want "CK1: already D, nothing changed" pull_unchanged "$D"
want "CK1: 'already D'" test "$rc" = 0 -a -n "$(out_line "already $D" && echo y)"
want "CK1: skips a's D with a warning" err_re "warning: skipping '$D' at 'a' (<p>): it was changed outside ait's rules"
pull "$D"
want "CK1: again: the skip warning comes until a is fixed" err_re "warning: skipping '$D' at 'a' (<p>): it was changed outside ait's rules"

# CK2  a1: fulfil D with -r (check line), then the message is rewritten (trailer gone)
pbase
git checkout -q -b a; line 1 A; DC=$(rec D current); c "a: fulfil

Ait-Check: $D reported x" >/dev/null
tick; git commit -q --amend --no-verify -m "a: fulfil"; A1P=$(git rev-parse HEAD); git checkout -q master
D0=$(recat init D)
fact "CK2: no Ait-Check trailer left" test -z "$(git log -1 --format='%(trailers:key=Ait-Check,valueonly)' a)"
fact "CK2: a1' has D current with a check line whose PINS match" test "$(recat "$A1P" D)" = "$DC" -a -n "$(pins_ok "$DC" && echo y)"
fact "CK2: a1' is the only new commit" test "$(git rev-list --count "$A1P" ^HEAD)" = 1
pull "$D"
want "CK2: pull D passes" test "$rc" = 0
want "CK2: takes a1' (a fast-forward)" test "$(git rev-parse HEAD)" = "$A1P"
want "CK2: D is a1''s current version" test "$(recat HEAD D)" = "$DC"
want "CK2: 'included D from ...'" out_re "included $D from '<c>'"

# CK3  D current ; a1: spec and pins changed by hand, old check line kept
DSTATE=current pbase
D0=$(recat init D); OLD=$(grep '^check ' ".ait/$D")
git checkout -q -b a; cond D c-hand; rec D current check=none >/dev/null
awk -v ck="$OLD" '/^creator /{ print ck } 1' ".ait/$D" > "$T/l" && cat "$T/l" > ".ait/$D"; A1=$(c "a: hand edit"); git checkout -q master
DK=$(recat "$A1" D)
fact "CK3: a's D is current and keeps init's check line" test "$(state_of "$DK")" = current -a "$(git cat-file -p "$DK" | grep '^check ')" = "$OLD"
fact "CK3: its PINS do not match its pins" not pins_ok "$DK"
fact "CK3: init's D is current with matching PINS" pins_ok "$D0"
want "CK3: already D, nothing changed" pull_unchanged "$D"
want "CK3: 'already D'" test "$rc" = 0 -a -n "$(out_line "already $D" && echo y)"
want "CK3: skips a's D with a warning" err_re "warning: skipping '$D' at 'a' (<p>): it was changed outside ait's rules"
pull "$D"
want "CK3: again: the skip warning comes until a is fixed" err_re "warning: skipping '$D' at 'a' (<p>): it was changed outside ait's rules"

# MF1  a1 writes .ait/D with CRLF line ends
pbase
git checkout -q -b a; cond D c-A; rec D >/dev/null; awk '{ printf "%s\r\n", $0 }' ".ait/$D" > "$T/l" && cat "$T/l" > ".ait/$D"; A1=$(c "a: CRLF"); git checkout -q master
DM=$(recat "$A1" D)
fact "MF1: a1's .ait/D has CR line ends" has_cr "$DM"
want "MF1: already D, nothing changed" pull_unchanged "$D"
want "MF1: 'already D'" test "$rc" = 0 -a -n "$(out_line "already $D" && echo y)"
want "MF1: skips a1's D as malformed" err_re "warning: skipping malformed '.ait/$D' at '<c>'"
pull "$D"
want "MF1: again: the skip warning comes until a is fixed" err_re "warning: skipping malformed '.ait/$D' at '<c>'"

# ACC1  D current, drop X ; a1 fulfils X (D withdrawn) ; a2 turns D back to current by hand ; a3 changes Q ; 'pull Q'
acc() { rec X future target=D >/dev/null; q0; }
DSTATE=current pbase acc
git checkout -q -b a; XF=$(rec X fulfilled target=D); DW=$(rec D withdrawn); A1=$(c "a: fulfil X")
DR=$(rec D current); A2=$(c "a: D back to current by hand"); cond Q q-A; QA=$(rec Q); A3=$(c "a: Q"); git checkout -q master
fact "ACC1: a3 has X fulfilled" test "$(recat "$A3" X)" = "$XF" -a "$(state_of "$XF")" = fulfilled
fact "ACC1: a's D is current on top of a1's withdrawn D" test "$(recat "$A3" D)" = "$DR" -a "$(state_of "$DR")" = current -a "$(prevs "$DR")" = "$DW" -a "$(state_of "$DW")" = withdrawn
fact "ACC1: a's D contains the withdrawn version" contains "$DR" "$DW"
fact "ACC1: ... and its body is not the withdrawn one" not same_body "$DR" "$DW"
fact "ACC1: merging a3 also changes .ait/X" test -z "$(mt HEAD "$A3")" -a -n "$(git diff --name-only HEAD "$A3" | grep -x ".ait/$X")"
fact "ACC1: the received result (X fulfilled, my D current) breaks 5.9 d" test "$(state_of "$(recat HEAD D)")" = current
want "ACC1: pull Q stops at 3, nothing changed" pull_unchanged "$Q"
want "ACC1: names X" stops 1 "error: taking '<c>' also changes contracts you did not name: '$X'"
want "ACC1: hint 'pull Q X'" err_line "hint: name them too: 'ait contract pull $Q $X'"
want "ACC1: 'pull Q X' stops at 4, nothing changed" pull_unchanged "$Q" "$X"
want "ACC1: the accident is the error" stops 1 "error: taking '<c>' needs '$D' as 'a' has it, but '$D' at 'a' (<p>) was changed outside ait's rules"
want "ACC1: hint: fix it on a" err_line "hint: '$D' has to be fixed on branch 'a' first"

# ID1  a2 writes a new contract W as current without a check line ; main has no W ; 'pull W'
pbase
git checkout -q -b a; cond W w-A; WC=$(rec W current check=none); A2=$(c "a: W current, no check"); git checkout -q master
fact "ID1: main has no W" test -z "$(recat HEAD W)"
fact "ID1: a's W is new, current, without a check line" test "$(state_of "$WC")" = current -a -z "$(prevs "$WC")" -a -z "$(has_check "$WC" && echo y)"
want "ID1: stops, nothing changed" pull_unchanged "$W"
want "ID1: not E_ID: the accident error" stops 1 "error: '$W' at 'a' (<p>) was changed outside ait's rules"
want "ID1: skips a's W with a warning first" first_re "warning: skipping '$W' at 'a' (<p>): it was changed outside ait's rules"
want "ID1: hint: fix it on a" err_line "hint: '$W' has to be fixed on branch 'a' first"
want "ID1: no hint gives the same command again" not grep -q "^hint: .*ait contract pull $W'" "$T/err"

# MF2  a0: Z in CRLF ; a1: Y made current by hand ; a2: new W (current, no check) and .ait/notes.txt ; a3: Q ; 'pull Q'
#      (the spec files: rev9.log MF2-9)
mf2() { for k in Z Y Q; do cond $k "$(echo $k | tr 'A-Z' 'a-z')0"; rec $k >/dev/null; done; }
pbase mf2
git checkout -q -b a
cond Z z-A; rec Z >/dev/null; awk '{ printf "%s\r\n", $0 }' ".ait/$Z" > "$T/l" && cat "$T/l" > ".ait/$Z"; A0=$(c "a0: Z CRLF")
line 1 Y; YC=$(rec Y current check=none); A1=$(c "a1: Y current by hand")
cond W w-A; WC=$(rec W current check=none); echo hello > .ait/notes.txt; A2=$(c "a2: W, notes")
cond Q q-A; QA=$(rec Q); A3=$(c "a3: Q"); git checkout -q master
Z0=$(recat init Z) Y0=$(recat init Y) Q0=$(recat init Q)
fact "MF2: merging a3 conflicts nowhere" test -z "$(mt HEAD "$A3")"
fact "MF2: merging a3 also changes Z, Y, W, notes.txt, specs/z.yaml and specs/w.yaml" test "$(git diff --name-only HEAD "$A3" | tr '\n' ' ')" = "$(printf '%s\n' .ait/$Q .ait/$W .ait/$Y .ait/$Z .ait/notes.txt specs/q.yaml specs/w.yaml specs/z.yaml src/stock.c | sort | tr '\n' ' ')"
fact "MF2: a's Z is malformed (CRLF)" has_cr "$(recat "$A3" Z)"
fact "MF2: a's Y is current without a check line" test "$(recat "$A3" Y)" = "$YC" -a "$(state_of "$YC")" = current -a -z "$(has_check "$YC" && echo y)"
fact "MF2: a's W is new, current, without a check line" test "$(recat "$A3" W)" = "$WC" -a "$(state_of "$WC")" = current -a -z "$(has_check "$WC" && echo y)"
fact "MF2: at a3, Z pins specs/z.yaml at a's content" test "$(pinned "$A3" Z specs/z.yaml)" = "$(at "$A3" specs/z.yaml)"
fact "MF2: at a3, W pins specs/w.yaml at a's content" test "$(pinned "$A3" W specs/w.yaml)" = "$(at "$A3" specs/w.yaml)"
fact "MF2: a3's Q contains my Q" contains "$QA" "$Q0"
pull "$Q"
want "MF2: pull Q passes" test "$rc" = 0
want "MF2: Q is a3's version" test "$(recat HEAD Q)" = "$QA"
want "MF2: 'included Q from ...'" out_re "included $Q from '<c>'"
want "MF2: Z and Y stay as mine" test "$(recat HEAD Z)" = "$Z0" -a "$(recat HEAD Y)" = "$Y0"
want "MF2: no W and no notes.txt" test -z "$(recat HEAD W)" -a -z "$(at HEAD .ait/notes.txt)"
want "MF2: warning on notes.txt" err_re "warning: skipping '.ait/notes.txt' in '<c>': not a contract file"
want "MF2: a warning for each skipped contract (Z, Y, W)" sh -c "grep '^warning: ' '$T/err' | grep -q '$Z' && grep '^warning: ' '$T/err' | grep -q '$Y' && grep '^warning: ' '$T/err' | grep -q '$W'"
want "MF2: specs/q.yaml is a3's" test "$(at HEAD specs/q.yaml)" = "$(at "$A3" specs/q.yaml)"
want "MF2: specs/z.yaml stays my Z's pin" test "$(at HEAD specs/z.yaml)" = "$(pinned init Z specs/z.yaml)"
want "MF2: no specs/w.yaml" test -z "$(at HEAD specs/w.yaml)"
want "MF2: the code comes (src/stock.c is a3's)" test "$(at HEAD src/stock.c)" = "$(at "$A3" src/stock.c)"
want "MF2: a merge commit of HEAD and a3" test "$(parents)" = 2 -a "$(par 2)" = "$A3"
done_testing
