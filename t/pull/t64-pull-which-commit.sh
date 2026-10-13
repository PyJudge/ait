#!/bin/sh
# pull criteria 3.5 어느 커밋을 들이나 (G1, G2, G3, G4, G4b, TIE, CP1, M1, KB1, KB2, KB3, IND2, IND3, IND4, CP3)
# histories: scen.py, scen4.py (G, KB), scen5.py (IND2, CP3), scen6.py (IND3), scen8.py (IND4)
. "$(dirname "$0")/lib.sh"

# ---- helpers of this file
# newc REV: how many commits of REV are not in HEAD
newc() { git rev-list --count "$1" ^HEAD; }
# vtext KEEP STATE PREV...: the version with KEEP's body, state STATE and prev PREV... (sorted); KEEP's check line
# stays only on a current version (a choice keeps the check of decision 22, a reopen drops it)
vtext() {
	k=$1 s=$2; shift 2
	git cat-file -p "$k" | grep -v '^prev ' | sed "s/^state .*/state $s/" | { if [ "$s" = current ]; then cat; else grep -v '^check '; fi; }
	for p in $(printf '%s\n' "$@" | sort -u); do echo "prev $p"; done
}
vexp() { vtext "$@" | git hash-object --stdin; }
# take_as OTHER NAME MSG FILE: merge OTHER without committing, put FILE as the record and the spec files it pins
# (as an ait resolution or a pull would), commit; prints the commit
take_as() {
	git merge -q --no-commit --no-ff "$1" >/dev/null 2>&1
	cp "$4" ".ait/$(id_of "$2")"
	grep '^file ' "$4" | while read -r _ p _ b _; do git cat-file -p "$b" > "$p"; done
	c "$3"
}
# keep_merge OTHER NAME KEEP MSG: merge OTHER and resolve the record to KEEP's body, prev = both sides' versions
keep_merge() { vtext "$3" "$(state_of "$3")" "$(recat HEAD "$2")" "$(recat "$1" "$2")" > "$T/v"; take_as "$1" "$2" "$4" "$T/v"; }
# nlatest V...: how many of V... are contained in no other of them
nlatest() {
	_n=0
	for _v; do _top=1; for _w; do [ "$_w" != "$_v" ] && contains "$_w" "$_v" && _top=0; done; _n=$((_n + _top)); done
	echo $_n
}
# mt_paths A B: the paths git's merge of A and B changes against A
mt_paths() { _t=$(git merge-tree --write-tree "$1" "$2" 2>/dev/null | head -1); git diff-tree -r --name-only "$1" "$_t" | tr '\n' ' ' | sed 's/ $//'; }
# parents_are OLD X...: HEAD's first parent is OLD and its other parents are X... (any order)
parents_are() {
	_o=$1; shift
	[ "$(git rev-parse -q --verify HEAD^1)" = "$(git rev-parse "$_o")" ] &&
	[ "$(git rev-list --parents -n1 HEAD | tr ' ' '\n' | sed 1,2d | sort)" = "$(for x; do git rev-parse "$x"; done | sort)" ]
}
# holds K V...: HEAD's version of K contains every V (4절: no version is lost)
holds() { _k=$1; shift; _h=$(recat HEAD "$_k"); [ -n "$_h" ] || return 1; for _x; do contains "$_h" "$_x" || return 1; done; }
# pins_ok REV NAME: every spec file the record pins is in REV's tree as pinned (2.4.1)
pins_ok() { git cat-file -p "$(recat "$1" "$2")" | sed -n 's/^file //p' | while read -r p _ b _; do [ "$(at "$1" "$p")" = "$b" ] || echo bad; done | grep -q bad && return 1; return 0; }
stops_err() { [ "$rc" = 1 ] && head -1 "$T/err" | grep -q '^error: '; }
err_re() { head -1 "$T/err" | grep -qE -- "$1"; }
# hint_cmd CMD: a hint line gives the command 'CMD'; pick_re ERE: a hint line gives a choice matching ERE other than HEAD
hint_cmd() { grep '^hint: ' "$T/err" | grep -qF -- "'$1'"; }
pick_re() { grep '^hint: ' "$T/err" | grep -oE -- "$1" | grep -vq '=HEAD '; }
trailer() { git log -1 --format='%(trailers:key=Ait-Agent,valueonly)' "$1" | sed '/^$/d'; }
# a clone where an agent runs ait (ait init installs the hooks; a session of its own)
aclone() { git clone -q --no-local "$T/r" "$1" && cd "$1" && ait init >/dev/null 2>&1 && ait session open --agent agent-m >/dev/null; }

# G1  main has D@B (from b) ; a: D@A ; b merged a and kept B (bM)
g1() {
	pbase
	git checkout -q -b b; cond D c-B; VB=$(rec D); c "b: D c-B" >/dev/null
	git checkout -q master; git merge -q --ff-only b
	git checkout -q -b a init; cond D c-A; VA=$(rec D); A1=$(c "a: D c-A")
	git checkout -q b; BM=$(keep_merge a D "$VB" "b: merge a, keep B"); VBM=$(recat b D)
	git checkout -q master
}
g1
fact "G1: main has b's D@B" test "$(recat HEAD D)" = "$VB"
fact "G1: bM's version contains a's D@A and main's D@B" eval 'contains "$VBM" "$VA" && contains "$VBM" "$VB"'
fact "G1: so it is the one latest version" test "$(nlatest "$VA" "$VB" "$VBM")" = 1
fact "G1: bM is its introducing commit, 2 new commits" test "$(newc "$BM")" = 2 -a "$(recat "$BM^1" D)" != "$VBM" -a "$(recat "$BM^2" D)" != "$VBM"
fact "G1: git merge of a1 conflicts on .ait/D and specs/d.yaml" test "$(mt HEAD "$A1")" = ".ait/$D specs/d.yaml"
fact "G1: bM merges without a conflict and changes only .ait/D" test "$(mt HEAD "$BM")" = "" -a "$(mt_paths HEAD "$BM")" = ".ait/$D"
fact "G1: bM's spec file is as its D pins it" pins_ok "$BM" D
pull "$D"
want "G1: pull D succeeds" test "$rc" = 0
want "G1: takes bM (fast-forward: HEAD is its ancestor and the result is its tree)" test "$(git rev-parse HEAD)" = "$BM"
want "G1: D is bM's version" test "$(recat HEAD D)" = "$VBM"
want "G1: 'included D from ...'" grep -q "^included $D from '" "$T/out"
want "G1: no version is lost" holds D "$VB" "$VBM"

# G2  b and f each merged a and kept A ; main: D0
pbase
git checkout -q -b a; cond D c-A; VA=$(rec D); c "a: D c-A" >/dev/null
git checkout -q -b b init; echo b > src/b.c; c "b: code" >/dev/null; cond D c-B; rec D >/dev/null; c "b: D c-B" >/dev/null
BM=$(keep_merge a D "$VA" "b: merge a, keep A"); VBM=$(recat HEAD D)
git checkout -q -b f init; cond D c-F; rec D >/dev/null; c "f: D c-F" >/dev/null
FM=$(keep_merge a D "$VA" "f: merge a, keep A"); VFM=$(recat HEAD D)
git checkout -q master; D0=$(recat HEAD D); H0=$(git rev-parse HEAD)
fact "G2: b's and f's versions have one body, D@A's" eval 'same_body "$VBM" "$VFM" && same_body "$VBM" "$VA"'
fact "G2: both are still D@A's body since their newest common version D@A" eval 'contains "$VBM" "$VA" && contains "$VFM" "$VA"'
fact "G2: neither contains the other: 2 latest versions" test "$(nlatest "$D0" "$VA" "$VBM" "$VFM")" = 2
fact "G2: introducing commits bM (4 new) and fM (3 new)" test "$(newc "$BM")" = 4 -a "$(newc "$FM")" = 3
pull "$D"
want "G2: does not stop" test "$rc" = 0
want "G2: takes fM (fewer new commits), in a commit on HEAD" parents_are "$H0" "$FM"
want "G2: .ait/D is D@A's body with prev {D@B', D@F'}" test "$(recat HEAD D)" = "$(vexp "$VA" future "$VBM" "$VFM")"
want "G2: specs/d.yaml is c-A, as D pins it" eval 'test "$(git show HEAD:specs/d.yaml)" = "body: c-A" && pins_ok HEAD D'
want "G2: 'included D from ...'" grep -q "^included $D from '" "$T/out"
want "G2: no version is lost" holds D "$D0" "$VBM" "$VFM"
pull "$D"
want "G2: again: already D" out_line "already $D"

# G3  main has D@A (from a) ; b: D@B then merged a and kept B (bM)
pbase
git checkout -q -b a; cond D c-A; VA=$(rec D); c "a: D c-A" >/dev/null
git checkout -q master; git merge -q --ff-only a
git checkout -q -b b init; cond D c-B; VB=$(rec D); B1=$(c "b: D c-B")
BM=$(keep_merge a D "$VB" "b: merge a, keep B"); VBM=$(recat HEAD D)
git checkout -q master
fact "G3: main has a's D@A" test "$(recat HEAD D)" = "$VA"
fact "G3: bM's version is the one latest version" test "$(nlatest "$VA" "$VB" "$VBM")" = 1
fact "G3: git merge of b1 conflicts" test "$(mt HEAD "$B1")" = ".ait/$D specs/d.yaml"
fact "G3: bM merges without a conflict" test "$(mt HEAD "$BM")" = ""
pull "$D"
want "G3: pull D succeeds" test "$rc" = 0
want "G3: takes bM (fast-forward)" test "$(git rev-parse HEAD)" = "$BM"
want "G3: D is bM's version" test "$(recat HEAD D)" = "$VBM"
want "G3: 'included D from ...'" grep -q "^included $D from '" "$T/out"

# G4  a: code a0, then D c-A (a1) ; main: D c-A made separately (e1)
pbase
git checkout -q -b a; echo a > src/a.c; c "a: code" >/dev/null; cond D c-A; VA=$(rec D); c "a: D c-A" >/dev/null
git checkout -q master; cond D c-A; VE=$(rec D); c "e: D c-A" >/dev/null
fact "G4: a's D and main's D are the same blob" test "$VA" = "$VE"
want "G4: nothing changes" pull_unchanged "$D"
want "G4: exit 0" test "$rc" = 0
want "G4: already D" out_line "already $D"

# G4b  a: c-x (a1) then c-A (a2) ; main: c-A directly (e1)
pbase
git checkout -q -b a; cond D c-x; rec D >/dev/null; c "a: D c-x" >/dev/null; cond D c-A; VA=$(rec D); A2=$(c "a: D c-A")
git checkout -q master; cond D c-A; VE=$(rec D); E1=$(c "e: D c-A")
fact "G4b: a's D and main's D have one body" same_body "$VA" "$VE"
fact "G4b: they are different versions, neither contains the other" test "$VA" != "$VE" -a "$(nlatest "$VA" "$VE")" = 2
fact "G4b: a2 introduces a's version, 2 new commits" test "$(newc "$A2")" = 2
pull "$D"
want "G4b: pull D succeeds" test "$rc" = 0
want "G4b: a commit on HEAD, nothing taken" parents_are "$E1"
want "G4b: a's commits are not taken" not is_ancestor "$A2" HEAD
want "G4b: D is the same body with prev {both versions}" test "$(recat HEAD D)" = "$(vexp "$VE" future "$VA" "$VE")"
want "G4b: kept D" out_line "kept $D"
want "G4b: no version is lost" holds D "$VA" "$VE"
pull "$D"
want "G4b: again: already D" out_line "already $D"

# TIE  a: x (a1) then same with src/a.c (a2) ; b: y (b1) then same with src/b.c (b2)
pbase
git checkout -q -b a; cond D x; rec D >/dev/null; c "a: x" >/dev/null; cond D same; VA=$(rec D); echo A > src/a.c; A2=$(c "a: same")
git checkout -q -b b init; cond D y; rec D >/dev/null; c "b: y" >/dev/null; cond D same; VB=$(rec D); echo B > src/b.c; B2=$(c "b: same")
git checkout -q master; D0=$(recat HEAD D); H0=$(git rev-parse HEAD)
SMALL=$(printf '%s\n' "$A2" "$B2" | sort | head -1)
if [ "$SMALL" = "$A2" ]; then IN=src/a.c OUT=src/b.c; else IN=src/b.c OUT=src/a.c; fi
fact "TIE: a's and b's versions have one body" same_body "$VA" "$VB"
fact "TIE: 2 latest versions" test "$(nlatest "$D0" "$VA" "$VB")" = 2
fact "TIE: both contain my version (decision 30 does not decide)" eval 'contains "$VA" "$D0" && contains "$VB" "$D0"'
fact "TIE: both introducing commits have 2 new commits" test "$(newc "$A2")" = 2 -a "$(newc "$B2")" = 2
pull "$D"
want "TIE: pull D succeeds" test "$rc" = 0
want "TIE: takes the introducing commit with the smaller OID" parents_are "$H0" "$SMALL"
want "TIE: its code comes in" test -n "$(at HEAD "$IN")"
want "TIE: the other side's code does not" test -z "$(at HEAD "$OUT")"
want "TIE: D is the one body with prev {both versions}" test "$(recat HEAD D)" = "$(vexp "$VA" future "$VA" "$VB")"
want "TIE: 'included D from ...'" grep -q "^included $D from '" "$T/out"

# CP1  a1: D@A ; b: code b0, then cherry-pick of a1 (b1, the same blob)
pbase
git checkout -q -b a; cond D c-A; VA=$(rec D); A1=$(c "a: D c-A")
git checkout -q -b b init; echo b > src/b.c; c "b: code" >/dev/null; tick; git cherry-pick a >/dev/null 2>&1; B1=$(git rev-parse HEAD)
git checkout -q master
fact "CP1: b1 holds the same blob as a1" test "$(recat "$B1" D)" = "$VA" -a "$(recat "$B1^" D)" != "$VA"
fact "CP1: introducing commits b1 (2 new) and a1 (1 new)" test "$(newc "$B1")" = 2 -a "$(newc "$A1")" = 1
pull "$D"
want "CP1: pull D succeeds" test "$rc" = 0
want "CP1: takes a1 (fast-forward)" test "$(git rev-parse HEAD)" = "$A1"
want "CP1: b's code does not come in" test -z "$(at HEAD src/b.c)"
want "CP1: 'included D from ...'" grep -q "^included $D from '" "$T/out"

# M1  a: D@B (a1) then D@B2 (a2) ; a's history rewritten (a') so no commit holds D@B ; main: D0 ; a fresh clone too
pbase
git checkout -q -b a; cond D c-B; VB=$(rec D); c "a: D@B" >/dev/null; cond D c-B2; VB2=$(rec D); c "a: D@B2" >/dev/null
AR=$(git commit-tree "HEAD^{tree}" -p init -m "a: rewritten"); git update-ref refs/heads/a "$AR"; git reflog expire --expire=now --all
git checkout -q master; D0=$(recat HEAD D)
aclone "$T/m1"; git checkout -q -b e origin/master
fact "M1: the fresh clone has no D@B" not git cat-file -e "$VB"
fact "M1: there D@B2 does not contain D0 (the chain is cut)" not contains "$VB2" "$D0"
fact "M1: there are 2 latest versions with different bodies" eval 'test "$(nlatest "$D0" "$VB2")" = 2 && ! same_body "$D0" "$VB2"'
cd "$T/r"
fact "M1: the same repository still has D@B's blob" git cat-file -e "$VB"
fact "M1: no commit of any ref holds D@B" test -z "$(git rev-list --all | while read -r x; do [ "$(recat "$x" D)" = "$VB" ] && echo "$x"; done)"
fact "M1: so D@B2 contains D0 here" contains "$VB2" "$D0"
fact "M1: a' introduces D@B2, 1 new commit" test "$(newc "$AR")" = 1 -a "$(recat "$AR" D)" = "$VB2"
pull "$D"
want "M1: the same repository takes D@B2" test "$rc" = 0 -a "$(recat HEAD D)" = "$VB2"
want "M1: by fast-forward to a'" test "$(git rev-parse HEAD)" = "$AR"
cd "$T/m1"
want "M1: the fresh clone stops, nothing changed" pull_unchanged "$D"
want "M1: diverged error" stops_with 1 "error: '$D' has diverged between "
want "M1: hint offers D=HEAD" hint_cmd "ait contract pull $D=HEAD -m <why>"
want "M1: hint offers a's version" pick_re "'ait contract pull $D=[^ ']+ -m <why>'"
cd "$T/r"

# KB1  G1, then 'pull D=a1 -m <why>'
g1
fact "KB1: a1's version is not a remaining version (bM's contains it)" eval 'contains "$VBM" "$VA" && test "$(nlatest "$VA" "$VB" "$VBM")" = 1'
want "KB1: stops, nothing changed" pull_unchanged "$D=$A1" -m why
want "KB1: an error (the criteria give no wording)" stops_err
want "KB1: the wording of the output table (record 117)" err_re "^error: '[^']+' has no version of '$D' to choose: a newer one includes it\$"
want "KB1: the hint wording" err_line "hint: take it as it is: 'ait contract pull $D'"
want "KB1: not diverged, so the hint is 'pull D'" hint_cmd "ait contract pull $D"

# KB2  a1: c-A ; b1: c-B (the hint was 'D=a1 or D=b1') ; then b merged a and kept B (bM) ; 'pull D=a1 -m <why>'
kb2() {
	pbase
	git checkout -q -b a; cond D c-A; VA=$(rec D); A1=$(c "a: c-A")
	git checkout -q -b b init; cond D c-B; VB=$(rec D); c "b: c-B" >/dev/null
	BM=$(keep_merge a D "$VB" "b: merge a, keep B"); VBM=$(recat HEAD D)
	git checkout -q master; D0=$(recat HEAD D)
}
kb2
fact "KB2: before bM, a1 and b1 both changed D: 2 latest versions" eval 'test "$(nlatest "$D0" "$VA" "$VB")" = 2 && ! same_body "$VA" "$VB"'
fact "KB2: now bM's version is the one latest version" test "$(nlatest "$D0" "$VA" "$VB" "$VBM")" = 1
fact "KB2: bM introduces it, 3 new commits" test "$(newc "$BM")" = 3
want "KB2: stops, nothing changed" pull_unchanged "$D=$A1" -m why
want "KB2: an error (the criteria give no wording)" stops_err
want "KB2: the wording of the output table (record 117)" err_re "^error: '[^']+' has no version of '$D' to choose: a newer one includes it\$"
want "KB2: the hint is 'pull D'" hint_cmd "ait contract pull $D"

# KB3  KB2, then main takes bM ; 'pull D=HEAD -m <why>'
kb2
git merge -q --ff-only b
fact "KB3: my version is bM's, the one latest version" test "$(recat HEAD D)" = "$VBM" -a "$(nlatest "$D0" "$VA" "$VB" "$VBM")" = 1
want "KB3: no new version, nothing changed" pull_unchanged "$D=HEAD" -m why
want "KB3: exit 0" test "$rc" = 0
want "KB3: already D" out_line "already $D"

# IND2  a1 fulfils D (C) ; main: c-E (E) ; main 'pull D=a1 future amend D -m <why>' ; then a clone at init pulls D
pbase
git checkout -q -b a; line 1 A; C=$(rec D current); A1=$(c "a: fulfil D")
git checkout -q master; cond D c-E; E=$(rec D); E1=$(c "e: c-E")
V1=$(vexp "$C" current "$C" "$E")
aclone "$T/ind2f"; git checkout -q -B b init; git branch -q -D master; D0=$(recat HEAD D)
W4=$(vtext "$C" future "$V1" | git hash-object -w --stdin); W=$(vtext "$C" future "$V1" "$C" "$E" | git hash-object -w --stdin)
fact "IND2: a's D is current" test "$(state_of "$C")" = current
fact "IND2: the version pull decides (V1) is not in another clone" not git cat-file -e "$V1"
fact "IND2: a result with prev V1 alone leaves 3 latest versions there (a's, main's old one, the result)" test "$(nlatest "$D0" "$C" "$E" "$W4")" = 3
fact "IND2: with V1's prev {a's D, main's D} too, the result is the one latest version there" test "$(nlatest "$D0" "$C" "$E" "$W")" = 1
cd "$T/r"; rm -rf "$T/ind2f"
pull "$D=$A1" future amend "$D" -m why
want "IND2: pull succeeds" test "$rc" = 0
want "IND2: takes a1 in a commit on HEAD" parents_are "$E1" "$A1"
want "IND2: D is future, C's body, prev {V1, a's D, main's D}" test "$(recat HEAD D)" = "$(vexp "$C" future "$V1" "$C" "$E")"
want "IND2: specs/d.yaml is c0, as D pins it" eval 'test "$(git show HEAD:specs/d.yaml)" = "body: c0" && pins_ok HEAD D'
want "IND2: 'included D from ...'" grep -q "^included $D from '" "$T/out"
want "IND2: no version is lost" holds D "$E" "$C"
R=$(recat HEAD D)
aclone "$T/ind2c"; git checkout -q -B b init; git branch -q -D master
pull "$D"
want "IND2: the clone at init does not stop" test "$rc" = 0
want "IND2: it takes main's result as is" test "$(recat HEAD D)" = "$R" -a "$R" != "$E"
cd "$T/r"

# IND3  a1 fulfils D ; main: c-E ; A: 'pull D=a1 future amend D' (agA) ; B: 'pull D=a1' (agB) ; each pulls the other ; a clone without B
pbase
git checkout -q -b a; line 1 A; C=$(rec D current); c "a: fulfil D" >/dev/null
git checkout -q master; cond D c-E; E=$(rec D); E1=$(c "e: c-E")
vtext "$C" current "$C" "$E" > "$T/v1"; V1=$(git hash-object "$T/v1")
git checkout -q -b agB; B1=$(take_as a D "B: pull D=a1" "$T/v1")
git checkout -q -b agA master; vtext "$C" future "$V1" "$C" "$E" > "$T/w"; W=$(git hash-object "$T/w"); AA=$(take_as a D "A: pull D=a1 future amend D" "$T/w")
git checkout -q master
fact "IND3: B's result is A's intermediate version V1" test "$(recat agB D)" = "$V1"
fact "IND3: A's version holds V1 and V1's prev" eval 'test "$(prevs "$W")" = "$(printf "%s\n" "$V1" "$C" "$E" | sort)"'
fact "IND3: from A and from B, A's version is the one latest version" test "$(nlatest "$C" "$E" "$V1" "$W")" = 1
aclone "$T/ind3c"; git checkout -q -B x origin/master; git branch -q -D master; git update-ref -d refs/remotes/origin/agB
git reflog expire --expire=now --all; git gc -q --prune=now
fact "IND3: a clone without B has no V1" not git cat-file -e "$V1"
fact "IND3: there A's version still contains a's D and main's D" eval 'contains "$W" "$C" && contains "$W" "$E"'
fact "IND3: so it is the one latest version there" test "$(nlatest "$E" "$C" "$W")" = 1
cd "$T/r"
git checkout -q agA
want "IND3: A: nothing changes" pull_unchanged "$D"
want "IND3: A: already D" eval 'test "$rc" = 0 && out_line "already $D"'
git checkout -q agB
pull "$D"
want "IND3: B pulls D" test "$rc" = 0
want "IND3: B takes A's version as is" test "$(recat HEAD D)" = "$W"
want "IND3: B takes A's commit" parents_are "$B1" "$AA"
want "IND3: B: 'included D from ...'" grep -q "^included $D from '" "$T/out"
pull "$D"
want "IND3: B again: already D" out_line "already $D"
cd "$T/ind3c"
pull "$D"
want "IND3: the clone without B takes A's version" test "$rc" = 0 -a "$(recat HEAD D)" = "$W"
want "IND3: by fast-forward to A's commit" test "$(git rev-parse HEAD)" = "$AA"
cd "$T/r"

# IND4  a1 fulfils D (C) ; main: c-E (E) ; 'pull D=a1 future amend D -m <why>'
pbase
git checkout -q -b a; line 1 A; C=$(rec D current); A1=$(c "a1: fulfil D")
git checkout -q master; cond D c-E; E=$(rec D); E1=$(c "e1: c-E")
V1=$(vexp "$C" current "$C" "$E"); W=$(vtext "$C" future "$V1" "$C" "$E" | git hash-object -w --stdin)
fact "IND4: parent a1's version C is current" test "$(state_of "$C")" = current
fact "IND4: the result is future and new to both parents, with C in its prev" eval 'test "$(state_of "$W")" = future && ! contains "$C" "$W" && ! contains "$E" "$W" && prevs "$W" | grep -qx "$C"'
fact "IND4: the other parent's future version E holds neither C nor C's body" eval '! contains "$E" "$C" && ! same_body "$E" "$C" && ! same_body "$(recat init D)" "$C"'
fact "IND4: the result's body differs from the future start version E" not same_body "$W" "$E"
pull "$D=$A1" future amend "$D" -m why
want "IND4: pull succeeds" test "$rc" = 0
want "IND4: the commit takes a1" parents_are "$E1" "$A1"
want "IND4: D is the future version W" test "$(recat HEAD D)" = "$W"
want "IND4: reopened warning without a reason" err_line "warning: '$D' is reopened by this pull"
want "IND4: H10" err_line "hint: when ready, stage the code and run 'ait fulfill $D -m <why>'"

# CP3  a1 and b1 each write the same D version by hand (Ait-Agent agent-a, agent-b) ; main: c-E
pbase
git checkout -q -b a; cond D c-A; VA=$(rec D); A1=$(c "a: c-A

Ait-Agent: agent-a")
git checkout -q -b b init; cond D c-A; VB=$(rec D); B1=$(c "b: c-A

Ait-Agent: agent-b")
git checkout -q master; cond D c-E; E=$(rec D); E1=$(c "e: c-E")
fact "CP3: a1 and b1 hold the same blob" test "$VA" = "$VB"
fact "CP3: both introduce it, 1 new commit each" test "$(newc "$A1")" = 1 -a "$(newc "$B1")" = 1
fact "CP3: their Ait-Agent trailers" test "$(trailer "$A1")" = agent-a -a "$(trailer "$B1")" = agent-b
fact "CP3: 2 latest versions with different bodies" eval 'test "$(nlatest "$VA" "$E")" = 2 && ! same_body "$VA" "$E"'
want "CP3: stops, nothing changed" pull_unchanged "$D"
want "CP3: diverged error" stops_with 1 "error: '$D' has diverged between "
want "CP3: hint offers D=HEAD" hint_cmd "ait contract pull $D=HEAD -m <why>"
want "CP3: hint offers the copied version" pick_re "'ait contract pull $D=[^ ']+ -m <why>'"
pull "$D=HEAD" -m why
want "CP3: 'D=HEAD -m <why>' succeeds" test "$rc" = 0
want "CP3: a commit on HEAD, nothing taken" parents_are "$E1"
want "CP3: D is c-E's body with prev {the copy, mine}" test "$(recat HEAD D)" = "$(vexp "$E" future "$VA" "$E")"
want "CP3: kept D" out_line "kept $D"
done_testing
