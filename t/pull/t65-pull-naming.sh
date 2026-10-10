#!/bin/sh
# pull criteria 3.6 들어오는 계약은 모두 지정한다 (N1, W1, W2, K1, K6, K7, E1, E1b, K8, CY1)
# histories: scen.py (N1, W1, W2, K1, K6, E1), scen4.py (K7, E1b, CY1), scen6.py (K8), cycle3.py (CY1), flows.py (K6)
. "$(dirname "$0")/lib.sh"

# ---- helpers of this file
D2=D00000000000f
SE="agent-e S00000000000e"
newc() { git rev-list --count "$1" ^HEAD; }
# vexp KEEP STATE PREV...: the blob of KEEP's body with state STATE and prev PREV... (sorted); the check line stays
# only on a current version
vexp() {
	k=$1 s=$2; shift 2
	{ git cat-file -p "$k" | grep -v '^prev ' | sed "s/^state .*/state $s/" | { if [ "$s" = current ]; then cat; else grep -v '^check '; fi; }
	  for p in $(printf '%s\n' "$@" | sort -u); do echo "prev $p"; done; } | git hash-object --stdin
}
# nlatest V...: how many of V... are contained in no other of them
nlatest() {
	_n=0
	for _v; do _top=1; for _w; do [ "$_w" != "$_v" ] && contains "$_w" "$_v" && _top=0; done; _n=$((_n + _top)); done
	echo $_n
}
# mt_paths A B: the paths git's merge of A and B changes against A ; mt_tree A B: that merged tree
mt_tree() { git merge-tree --write-tree "$1" "$2" 2>/dev/null | head -1; }
mt_paths() { git diff-tree -r --name-only "$1" "$(mt_tree "$1" "$2")" | tr '\n' ' ' | sed 's/ $//'; }
# parents_are OLD X...: HEAD's first parent is OLD and its other parents are X... (any order)
parents_are() {
	_o=$1; shift
	[ "$(git rev-parse -q --verify HEAD^1)" = "$(git rev-parse "$_o")" ] &&
	[ "$(git rev-list --parents -n1 HEAD | tr ' ' '\n' | sed 1,2d | sort)" = "$(for x; do git rev-parse "$x"; done | sort)" ]
}
holds() { _k=$1; shift; _h=$(recat HEAD "$_k"); [ -n "$_h" ] || return 1; for _x; do contains "$_h" "$_x" || return 1; done; }
link_at() { at "$1" ".ait/undertake/$(id_of "$2")-$(id_of "$3")"; }
stops_err() { [ "$rc" = 1 ] && head -1 "$T/err" | grep -q '^error: '; }
err_re() { head -1 "$T/err" | grep -qE -- "$1"; }
hint_cmd() { grep '^hint: ' "$T/err" | grep -qF -- "'$1'"; }
included() { grep -q "^included $1 from '" "$T/out"; }
pick_re() { grep '^hint: ' "$T/err" | grep -oE -- "$1" | grep -vq '=HEAD '; }
# named_hint ID...: one 'name them too' hint whose command names exactly ID... (the criteria's order is not fixed:
# K1 'pull O' gives 'pull D O', K8 'pull D' gives 'pull D O1 D2')
named_hint() {
	_l=$(grep "^hint: name them too: 'ait contract pull [^']*'$" "$T/err") || return 1
	[ "$(echo "$_l" | wc -l | tr -d ' ')" = 1 ] || return 1
	[ "$(echo "$_l" | sed "s/^hint: name them too: 'ait contract pull //; s/'$//" | tr ' ' '\n' | sort)" = "$(printf '%s\n' "$@" | sort)" ]
}
# the start of the stop of 3: "error: taking 'C' also changes contracts you did not name: ..."
taking_re="^error: taking '[^']+' also changes contracts you did not name: "
q0() { cond Q q0; rec Q >/dev/null; }

# N1  a1: Q q-A ; a2: D c-A ; 'pull D'
pbase q0
git checkout -q -b a; cond Q q-A; rec Q >/dev/null; c "a: Q" >/dev/null; cond D c-A; VA=$(rec D); A2=$(c "a: D")
git checkout -q master; D0=$(recat HEAD D)
fact "N1: D's one latest version is a2's, 2 new commits" test "$(nlatest "$D0" "$VA")" = 1 -a "$(newc "$A2")" = 2
fact "N1: merging a2 has no conflict and changes .ait/Q too" test "$(mt HEAD "$A2")" = "" -a "$(mt_paths HEAD "$A2")" = ".ait/$D .ait/$Q specs/d.yaml specs/q.yaml"
want "N1: stops, nothing changed" pull_unchanged "$D"
want "N1: Q was not named" err_re "$taking_re'$Q'$"
want "N1: exit 1" test "$rc" = 1
want "N1: hint 'pull D Q'" err_line "hint: name them too: 'ait contract pull $D $Q'"

# W1  a0: code only (src/util.c) ; a1: D c-A
pbase
git checkout -q -b a; echo util > src/util.c; c "a: util" >/dev/null; cond D c-A; VA=$(rec D); A1=$(c "a: D")
git checkout -q master; D0=$(recat HEAD D)
fact "W1: a1 introduces D's latest version, 2 new commits" test "$(nlatest "$D0" "$VA")" = 1 -a "$(newc "$A1")" = 2
fact "W1: merging brings src/util.c, and D is the only contract that changes" test "$(mt HEAD "$A1")" = "" -a "$(mt_paths HEAD "$A1")" = ".ait/$D specs/d.yaml src/util.c"
pull "$D"
want "W1: pull D succeeds" test "$rc" = 0
want "W1: takes a1 (fast-forward)" test "$(git rev-parse HEAD)" = "$A1"
want "W1: the code comes in without being named" test "$(git show HEAD:src/util.c 2>/dev/null)" = util
want "W1: 'included D from ...'" included "$D"
want "W1: no version is lost" holds D "$D0" "$VA"

# W2  b1: offer O2 ; b2: O2 fulfilled ; b3: D c-B ; 'pull D'
pbase
git checkout -q -b b; echo o2 > src/o2.c; cond O2 o2; rec O2 future provider=src/o2.c >/dev/null; c "b: O2" >/dev/null
echo 'o2 impl' > src/o2.c; VO2=$(rec O2 current provider=src/o2.c); c "b: fulfil O2

Ait-Check: O2 reported p" >/dev/null
cond D c-B; VB=$(rec D); B3=$(c "b: D")
git checkout -q master
fact "W2: b3 introduces D's latest version, 3 new commits" test "$(newc "$B3")" = 3 -a "$(recat "$B3^" D)" != "$VB"
fact "W2: O2 is made and fulfilled before it" test "$(state_of "$VO2")" = current -a "$(recat "$B3" O2)" = "$VO2" -a -z "$(recat HEAD O2)"
fact "W2: merging b3 changes .ait/O2 too" test "$(mt HEAD "$B3")" = "" -a "$(mt_paths HEAD "$B3")" = ".ait/$D .ait/$O2 specs/d.yaml specs/o2.yaml src/o2.c"
want "W2: stops, nothing changed" pull_unchanged "$D"
want "W2: O2 was not named" err_re "$taking_re'$O2'$"
want "W2: exit 1" test "$rc" = 1
want "W2: hint 'pull D O2'" err_line "hint: name them too: 'ait contract pull $D $O2'"

# K1  a1: offer O, undertakes D ; a2: fulfils O and the D it undertook (1.6) ; 'pull O'
pbase
git checkout -q -b a; echo o > src/o.c; cond O o; rec O future provider=src/o.c >/dev/null; LK=$(link D O); c "a: offer O, undertake D" >/dev/null
echo 'o impl' > src/o.c; VO=$(rec O current provider=src/o.c); VD=$(rec D current); A2=$(c "a: fulfil O and the D it undertook")
git checkout -q master
fact "K1: O's one version is a2's, 2 new commits" test -z "$(recat HEAD O)" -a "$(recat "$A2" O)" = "$VO" -a "$(newc "$A2")" = 2
fact "K1: merging a2 changes .ait/D and .ait/undertake/D-O too" test "$(mt HEAD "$A2")" = "" -a "$(mt_paths HEAD "$A2")" = ".ait/$D .ait/$O .ait/undertake/$D-$O specs/o.yaml src/o.c"
fact "K1: a2's state passes 5.9 d (current O undertakes current D)" test "$(state_of "$VO")" = current -a "$(state_of "$VD")" = current -a "$(state_of "$LK")" = undertaken -a "$(link_at "$A2" D O)" = "$LK"
want "K1: stops, nothing changed" pull_unchanged "$O"
want "K1: D was not named; D comes in directly, so no undertake note (record 116)" err_re "$taking_re'$D'\$"
want "K1: exit 1" test "$rc" = 1
want "K1: hint 'pull D O'" named_hint "$D" "$O"
pull "$D" "$O"
want "K1: 'pull D O' succeeds" test "$rc" = 0
want "K1: takes a2 (fast-forward)" test "$(git rev-parse HEAD)" = "$A2"
want "K1: D, O and the undertake record are a2's" test "$(recat HEAD D)" = "$VD" -a "$(recat HEAD O)" = "$VO" -a "$(link_at HEAD D O)" = "$LK"
want "K1: 'included D from ...' and 'included O from ...'" eval 'included "$D" && included "$O"'

# K6  O is main's offer (future at init) ; a1: undertakes D with O ; a2: D c-A ; main: fulfils O (e1) ; 'pull D'
o_e() { echo o > src/o.c; cond O o; rec O future provider=src/o.c "creator=$SE" >/dev/null; }
pbase o_e
git checkout -q -b a; LK=$(link D O); c "a: undertake D with O" >/dev/null; cond D c-A; VA=$(rec D); A2=$(c "a: D c-A")
git checkout -q master; echo 'o impl' > src/o.c; VO=$(rec O current provider=src/o.c "creator=$SE"); E1=$(c "e: fulfil O

Ait-Check: O reported p")
D0=$(recat HEAD D); MT=$(mt_tree HEAD "$A2")
fact "K6: D's one latest version is a2's, 2 new commits" test "$(nlatest "$D0" "$VA")" = 1 -a "$(newc "$A2")" = 2
fact "K6: merging a2 changes .ait/D and the undertake record" test "$(mt HEAD "$A2")" = "" -a "$(mt_paths HEAD "$A2")" = ".ait/$D .ait/undertake/$D-$O specs/d.yaml"
fact "K6: merged: O current, D future, D-O undertaken (5.9 d: current O undertakes future D)" test "$(state_of "$MT:.ait/$O")" = current -a "$(state_of "$MT:.ait/$D")" = future -a "$(state_of "$MT:.ait/undertake/$D-$O")" = undertaken
want "K6: 'pull D' stops, nothing changed" pull_unchanged "$D"
want "K6: O comes in through the undertake record" err_re "$taking_re'$O' \(undertake '$D-$O'\)$"
want "K6: exit 1" test "$rc" = 1
want "K6: hint 'pull D O'" err_line "hint: name them too: 'ait contract pull $D $O'"
want "K6: 'pull D O' stops at 4 (5.9 d), nothing changed" pull_unchanged "$D" "$O"
want "K6: an error (the criteria give no wording)" stops_err
want "K6: hint 'pull D O future amend O -m <why>'" hint_cmd "ait contract pull $D $O future amend $O -m <why>"
pull "$D" "$O" future amend "$O" -m why
want "K6: 'pull D O future amend O -m <why>' succeeds" test "$rc" = 0
want "K6: takes a2 in a commit on HEAD" parents_are "$E1" "$A2"
want "K6: D is a2's, the undertake record is in" test "$(recat HEAD D)" = "$VA" -a "$(link_at HEAD D O)" = "$LK"
want "K6: O is reopened: future, prev main's current O" test "$(state_of "$(recat HEAD O)")" = future -a "$(prevs "$(recat HEAD O)")" = "$VO"
want "K6: 'included D from ...'" included "$D"

# K7  main has D and O ; a1: only undertakes D with O ; 'pull D'
o_a() { echo o > src/o.c; cond O o; rec O future provider=src/o.c >/dev/null; }
pbase o_a
git checkout -q -b a; LK=$(link D O); A1=$(c "a: undertake")
git checkout -q master
fact "K7: D's and O's versions are mine" test "$(recat a D)" = "$(recat HEAD D)" -a "$(recat a O)" = "$(recat HEAD O)"
fact "K7: the undertake record D-O is a1's, not in HEAD, 1 new commit" test -z "$(link_at HEAD D O)" -a "$(link_at a D O)" = "$LK" -a "$(newc "$A1")" = 1
want "K7: 'pull D' stops, nothing changed" pull_unchanged "$D"
want "K7: O comes in through the undertake record" err_re "$taking_re'$O' \(undertake '$D-$O'\)$"
want "K7: exit 1" test "$rc" = 1
want "K7: hint 'pull D O'" err_line "hint: name them too: 'ait contract pull $D $O'"
pull "$D" "$O"
want "K7: 'pull D O' succeeds" test "$rc" = 0
want "K7: takes a1 (fast-forward)" test "$(git rev-parse HEAD)" = "$A1"
want "K7: the undertake record is in" test "$(link_at HEAD D O)" = "$LK"

# E1  b1: D c-B ; b2: Q q-B ; b3: git revert of b1 (b's tip has D0) ; 'pull Q'
e1() {
	pbase q0
	git checkout -q -b b; cond D c-B; VB=$(rec D); B1=$(c "b: D c-B"); cond Q q-B; VQ=$(rec Q); B2=$(c "b: Q")
	tick; git revert --no-edit "$B1" >/dev/null 2>&1
	git checkout -q master; D0=$(recat init D)
}
e1
fact "E1: at the ref tips D is mine alone (b's tip has D0)" test "$(recat b D)" = "$D0" -a "$(recat HEAD D)" = "$D0"
fact "E1: b2 holds D@B, which contains my D0" eval 'test "$(recat "$B2" D)" = "$VB" -a "$VB" != "$D0" && contains "$VB" "$D0"'
fact "E1: b2 introduces Q's one latest version, 2 new commits" test "$(recat b Q)" = "$VQ" -a "$(nlatest "$(recat HEAD Q)" "$VQ")" = 1 -a "$(newc "$B2")" = 2
fact "E1: merging b2 changes .ait/D too" test "$(mt_paths HEAD "$B2")" = ".ait/$D .ait/$Q specs/d.yaml specs/q.yaml"
want "E1: 'pull Q' stops, nothing changed" pull_unchanged "$Q"
want "E1: D was not named" err_re "$taking_re'$D'$"
want "E1: exit 1" test "$rc" = 1
want "E1: hint 'pull D Q'" named_hint "$D" "$Q"
pull "$D" "$Q"
want "E1: 'pull D Q' succeeds" test "$rc" = 0
want "E1: takes b2 (fast-forward)" test "$(git rev-parse HEAD)" = "$B2"
want "E1: D is D@B, Q is b2's" test "$(recat HEAD D)" = "$VB" -a "$(recat HEAD Q)" = "$VQ"

# E1b  E1, and main: D c-E (e1) ; 'pull Q'
e1
cond D c-E; E=$(rec D); E1=$(c "e: D c-E")
fact "E1b: at the ref tips D is mine alone (nothing to take)" test "$(nlatest "$(recat b D)" "$E")" = 1
fact "E1b: with b2's D@B added, D@B and my c-E are 2 latest versions with different bodies" eval 'test "$(nlatest "$D0" "$E" "$VB")" = 2 && ! same_body "$E" "$VB"'
fact "E1b: b1 introduces D@B, 1 new commit" test "$(newc "$B1")" = 1 -a "$(recat "$B1" D)" = "$VB"
want "E1b: 'pull D Q' stops, nothing changed" pull_unchanged "$D" "$Q"
want "E1b: diverged error" stops_with 1 "error: '$D' has diverged between "
want "E1b: hint offers D=HEAD with Q" hint_cmd "ait contract pull $D=HEAD $Q -m <why>"
want "E1b: hint offers D=<commit> with Q" pick_re "'ait contract pull $D=[^ ']+ $Q -m <why>'"
want "E1b: ... the commit is b1, D@B's introducing commit (record 116)" eval 'cn=$(sed -n "s/.*'"'"'ait contract pull $D=\([^ '"'"']*\) $Q -m <why>'"'"'.*/\1/p" "$T/err" | grep -v "^HEAD$" | head -1); [ -n "$cn" ] && [ "$(git rev-parse -q --verify "$cn^{commit}")" = "$B1" ]'
pull "$D=$B2" "$Q" -m why
want "E1b: 'pull D=b2 Q' is not refused (b2's version remains)" test "$rc" = 0
want "E1b: takes b2 in a commit on HEAD" parents_are "$E1" "$B2"
want "E1b: D is D@B's body with prev {D@B, c-E}" test "$(recat HEAD D)" = "$(vexp "$VB" future "$VB" "$E")"
want "E1b: Q is b2's" test "$(recat HEAD Q)" = "$VQ"

# K8  c1: undertakes D with O1 ; d1: undertakes D2 with O1 (another branch) ; 'pull D'
k8() { echo o > src/o.c; cond O1 o0; rec O1 future provider=src/o.c >/dev/null; cond D2 d0; rec D2 >/dev/null; }
pbase k8
git checkout -q -b c; L1=$(link D O1); C1=$(c c1)
git checkout -q -b d init; L2=$(link D2 O1); DD1=$(c d1)
git checkout -q master; H0=$(git rev-parse HEAD)
fact "K8: neither undertake record is in HEAD" test -z "$(link_at HEAD D O1)" -a -z "$(link_at HEAD D2 O1)"
fact "K8: c1 has D-O1, d1 has D2-O1, 1 new commit each" test "$(link_at c D O1)" = "$L1" -a "$(link_at d D2 O1)" = "$L2" -a "$(newc "$C1")" = 1 -a "$(newc "$DD1")" = 1
fact "K8: O1, the other end of D-O1, is also an end of D2-O1" eval 'test -n "$(at d ".ait/undertake/$D2-$O1")" && test -n "$(at c ".ait/undertake/$D-$O1")"'
want "K8: 'pull D' stops, nothing changed" pull_unchanged "$D"
want "K8: the stop of 3: two commits, two names in ID order (record 116)" err_re "^error: taking '[^']+' and '[^']+' also changes contracts you did not name: '$D2', '$O1'\$"
want "K8: exit 1" test "$rc" = 1
want "K8: one hint names D, O1 and D2" named_hint "$D" "$O1" "$D2"
want "K8: ... in contract-ID order (record 116)" err_line "hint: name them too: 'ait contract pull $D $D2 $O1'"
pull "$D" "$O1" "$D2"
want "K8: 'pull D O1 D2' succeeds" test "$rc" = 0
want "K8: takes c1 and d1 in one commit" parents_are "$H0" "$C1" "$DD1"
want "K8: both undertake records are in" test "$(link_at HEAD D O1)" = "$L1" -a "$(link_at HEAD D2 O1)" = "$L2"

# CY1  c1: undertakes D with O ; a1 (on c): D c-A, stock.c line 1 ; b1 (on c): O o-B, stock.c line 9 ; 'pull D'
o0() { echo o > src/o.c; cond O o0; rec O future provider=src/o.c >/dev/null; }
pbase o0
git checkout -q -b c; LK=$(link D O); c "c: undertake D with O" >/dev/null
git checkout -q -b a; cond D c-A; VA=$(rec D); line 1 A; A1=$(c "a: amend D")
git checkout -q -b b c; cond O o-B; VO=$(rec O future provider=src/o.c); line 9 B; B1=$(c "b: amend O")
git checkout -q master; H0=$(git rev-parse HEAD); TMP=$(git commit-tree "$(mt_tree HEAD "$A1")" -p HEAD -p "$A1" -m tmp)
fact "CY1: a1 has D's latest version, b1 O's, c1 the undertake record's" test "$(nlatest "$(recat HEAD D)" "$VA")" = 1 -a "$(nlatest "$(recat HEAD O)" "$VO")" = 1 -a "$(link_at a D O)" = "$LK" -a "$(link_at b D O)" = "$LK"
fact "CY1: 'pull D' alone merges a1 and brings the undertake record" test "$(mt HEAD "$A1")" = "" -a "$(mt_paths HEAD "$A1")" = ".ait/$D .ait/undertake/$D-$O specs/d.yaml src/stock.c"
fact "CY1: a1 and b1 are not ancestors of each other" eval '! is_ancestor "$A1" "$B1" && ! is_ancestor "$B1" "$A1"'
fact "CY1: merging a1, then b1, has no conflict" test "$(mt HEAD "$A1")" = "" -a "$(mt "$TMP" "$B1")" = ""
want "CY1: 'pull D' stops, nothing changed" pull_unchanged "$D"
want "CY1: O comes in through the undertake record" err_re "$taking_re'$O' \(undertake '$D-$O'\)$"
want "CY1: exit 1" test "$rc" = 1
want "CY1: hint 'pull D O'" err_line "hint: name them too: 'ait contract pull $D $O'"
pull "$D" "$O"
want "CY1: 'pull D O' succeeds" test "$rc" = 0
want "CY1: one commit with three parents: HEAD, a1, b1" parents_are "$H0" "$A1" "$B1"
want "CY1: D is a1's, O is b1's, the undertake record is in" test "$(recat HEAD D)" = "$VA" -a "$(recat HEAD O)" = "$VO" -a "$(link_at HEAD D O)" = "$LK"
want "CY1: both code changes are in" eval 'git show HEAD:src/stock.c | grep -qx "line1 v=A" && git show HEAD:src/stock.c | grep -qx "line9 v=B"'
done_testing
