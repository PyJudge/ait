#!/bin/sh
# pull criteria 3.8 명세 파일은 pull이 정한다 (KS1 S1 SF1 CP2 RN1 SP1 CP4 SP2 SP3 SP4 DL1 DL2 SP5 SP6 SP7 SP8 SF2 SP4c
# RN2 SP9 SP10 SP11 SP12 SP13 MF3). The rule: 1절 받는 결과, "명세 파일: ait는 받지 않을 변경만 정하고, 합치기는
# git이 한다"(기록 107). Conflicts on a pinned file stop like git with the merge state (2절 6, 결정 29).
# Histories: scen4.py (KS1), scen.py (S1, SF1), scen5.py (CP2, RN1), scen6.py (SP1, CP4), scen7.py (SP2-SP4, DL1),
# scen8.py (SP5-SP8, SF2, SP4c, DL2, RN2), scen9.py (SP9-SP13, MF3). Without the v14 merge driver, git's textual
# merge of .ait/ files differs from the logs; facts about git look at code and spec paths only.
. "$(dirname "$0")/lib.sh"

# ---------------------------------------------------------------- local helpers
SE="agent-e S00000000000e" SK="agent-k S000000000007"
nohook() { git -c core.hooksPath=/dev/null "$@"; }
# reinit: fold the work tree into the commit init (for a fixture whose init D is not pbase's D)
reinit() { git add -A && git commit -q --no-verify --amend --no-edit && git tag -f init >/dev/null; }
pinof() { git cat-file -p "$1" | awk -v p="$2" '$1 == "file" && $2 == p { print $4 }'; }
mtree() { git merge-tree --write-tree "$1" "$2" 2>/dev/null | head -1; }
# codeconf A B: the paths outside .ait/ that git's merge-tree leaves in conflict
codeconf() { mt "$1" "$2" | tr ' ' '\n' | grep -v -e '^\.ait/' -e '^$' | tr '\n' ' ' | sed 's/ $//'; }
has_line() { git cat-file -p "$1:$2" 2>/dev/null | grep -qxF -- "$3"; }
blob_of() { printf '%s\n' "$1" | git hash-object --stdin; }
# newv SRC PREV...: the version with SRC's body and these prev rows (canonical order)
newv() { nv=$1; shift; { body "$nv"; printf 'prev %s\n' "$@" | sort; } | git hash-object -w --stdin; }
diverged() { contains "$1" "$3" && contains "$2" "$3" && ! contains "$1" "$2" && ! contains "$2" "$1" && ! same_body "$1" "$2"; }
# hist_mentions TIP PATH: record rows naming PATH in the commits of TIP that HEAD does not have
hist_mentions() { for hr in $(git rev-list "$1" ^HEAD); do git grep -h -F -e "$2" "$hr" -- .ait; done; }
# taken_out REV PATH BLOB: a child of REV with PATH set to BLOB (1절: a change not taken, put back on that side)
taken_out() {
	GIT_INDEX_FILE=$T/to.idx git read-tree "$1" && GIT_INDEX_FILE=$T/to.idx git update-index --cacheinfo "100644,$3,$2" &&
	git commit-tree "$(GIT_INDEX_FILE=$T/to.idx git write-tree)" -p "$1" -m "taken out"
}
# seqmerge END...: git merges HEAD and the ends one by one; prints the tree, code conflicts go to $T/sconf
seqmerge() {
	sacc=$(git rev-parse HEAD); : > "$T/sconf"
	for se; do codeconf "$sacc" "$se" >> "$T/sconf"; st=$(mtree "$sacc" "$se"); sacc=$(git commit-tree "$st" -p "$sacc" -p "$se" -m tmp); done
	echo "$st"
}
said() { grep -qxF -- "$1" "$T/out" "$T/err"; }
err_re() { grep -qE -- "$1" "$T/err"; }
out_re() { grep -qE -- "$1" "$T/out"; }
first_is() { [ "$rc" = 1 ] && head -1 "$T/err" | grep -qE -- "$1"; }
passed_without() { [ "$rc" = 0 ] && ! err_re "$1"; }
# included_is ID REV: the line 'included ID from C' names REV
included_is() { ic=$(sed -n "s/^included $1 from '\(.*\)'\$/\1/p" "$T/out" | head -1); [ -n "$ic" ] && [ "$(git rev-parse -q --verify "$ic^{commit}")" = "$2" ]; }
# parents_are FIRST REST...: HEAD's first parent and the set of the others
parents_are() {
	[ "$(git rev-parse -q --verify HEAD^1)" = "$1" ] || return 1; shift
	[ "$(git rev-list --parents -n1 HEAD | cut -d' ' -f3- | tr ' ' '\n' | grep -v '^$' | sort)" = "$(printf '%s\n' "$@" | sort)" ]
}
mheads_are() { merging && [ "$(sort "$(git rev-parse --git-path MERGE_HEAD)")" = "$(printf '%s\n' "$@" | sort)" ]; }
noff() { grep -qx no-ff "$(git rev-parse --git-path MERGE_MODE)" 2>/dev/null; }
msg_has() { grep -qF -- "$1" "$(git rev-parse --git-path MERGE_MSG)" 2>/dev/null; }
idx_is() { [ "$(git rev-parse -q --verify ":$1" 2>/dev/null)" = "$2" ]; }
markers() { grep -q '^<<<<<<< ' "$1" 2>/dev/null; }
# repinned ID PATH BLOB: HEAD has BLOB at PATH and ID's version pins PATH at BLOB
repinned() { [ -n "$3" ] && [ "$(at HEAD "$2")" = "$3" ] && [ "$(pinof "$(recat HEAD "$1")" "$2")" = "$3" ]; }
# on_top ID OLD: ID's version at HEAD is another future version whose prev reaches OLD
on_top() { ot=$(recat HEAD "$1"); [ -n "$ot" ] && [ "$ot" != "$2" ] && [ "$(state_of "$ot")" = future ] && contains "$ot" "$2"; }
FIN_HINT="hint: commit the resolution first: 'git commit --allow-empty --no-edit --cleanup=strip', then continue as git says"
# finish PATH CONTENT: resolve PATH as CONTENT and make the finishing commit (only if pull left a merge)
finish() { if merging; then printf '%s\n' "$2" > "$1" && git add "$1" && run git commit --allow-empty --no-edit --cleanup=strip; else rc=none; fi; }
# cmds: the commands of the hint lines, in order, each ID=REV with REV resolved to its commit (HEAD stays)
cmds() {
	grep '^hint: ' "$T/err" | grep -o "'ait contract pull [^']*'" | tr -d "'" | while read -r cl; do
		set -- $cl; shift 3; cn=pull
		for ct; do case $ct in ?*=?*) cv=${ct#*=}
			[ "$cv" = HEAD ] || cv=$(git rev-parse -q --verify "$cv^{commit}" || echo "?$cv"); ct=${ct%%=*}=$cv ;; esac
			cn="$cn $ct"; done
		echo "$cn"
	done
}
cmds_are() { [ "$(cmds)" = "$(printf '%s\n' "$@")" ]; }
# git-like stop on a code conflict (2절 6): exit 1, git's lines, the finishing hint, HEAD unchanged, the merge state
conflict_stop() {
	cs_path=$1 cs_head=$2; shift 2
	want "$ROW: exit 1" test "$rc" = 1
	want "$ROW: 'CONFLICT (content): Merge conflict in $cs_path'" said "CONFLICT (content): Merge conflict in $cs_path"
	want "$ROW: 'Automatic merge failed; ...'" said "Automatic merge failed; fix conflicts and then commit the result."
	want "$ROW: the finishing hint" err_line "$FIN_HINT"
	want "$ROW: HEAD unchanged" test "$(git rev-parse HEAD)" = "$cs_head"
	want "$ROW: MERGE_HEAD lists every end" mheads_are "$@"
	want "$ROW: MERGE_MODE no-ff" noff
	want "$ROW: only $cs_path unmerged" test "$(unmerged)" = "$cs_path"
	want "$ROW: $cs_path has conflict markers in the work tree" markers "$cs_path"
}

# ---------------------------------------------------------------- fixtures shared by rows
q0() { cond Q q0; rec Q >/dev/null; }
shared() { echo 'body: s0' > specs/shared.yaml; }
dsh_t0() { mkdir -p tests; echo t0 > tests/d.sh; q0; }
dsh_t10() { mkdir -p tests; lines10 t > tests/d.sh; q0; }
dsh_l10() { mkdir -p tests; lines10 l > tests/d.sh; }
fsh_k() { mkdir -p tests; lines10 f > tests/f.sh; cond K k0; rec K >/dev/null; }
pin_input() { printf 'body: %s\ninputs: [%s]\n' "$2" "$3" > "$1"; }

# ================================================================ KS1
# KS1  a1: Q's spec gains an input (tests/qa.sh) ; main: Q q-E, then 'pull Q=HEAD' (kept) ; a2: D c-A ; 'pull D'
ROW=KS1
pbase q0
git checkout -q -b a; printf 'body: q0\ninputs: [tests/qa.sh]\n' > specs/q.yaml; mkdir -p tests; echo true > tests/qa.sh
QA=$(rec Q); a1=$(c "a1: Q input")
git checkout -q master; cond Q q-E; QE=$(rec Q); c "e1: Q q-E" >/dev/null
QK=$(rec Q future prev="$QA $QE"); c "pull Q=HEAD (kept)" >/dev/null
git checkout -q a; cond D c-A; DA=$(rec D); a2=$(c "a2: D c-A"); git checkout -q master
fact "KS1: my Q contains a's Q (nothing to take for Q)" contains "$(recat HEAD Q)" "$(recat a Q)"
fact "KS1: a2 holds a1's Q, which pins a's spec" test "$(recat a Q)" = "$QA" -a "$(pinof "$QA" specs/q.yaml)" = "$(at a specs/q.yaml)"
fact "KS1: git's merge with a2 conflicts on specs/q.yaml" test "$(codeconf HEAD a)" = specs/q.yaml
fact "KS1: git's merged specs/q.yaml differs from Q's pin" test "$(at "$(mtree HEAD a)" specs/q.yaml)" != "$(pinof "$QK" specs/q.yaml)"
h0=$(git rev-parse HEAD)
pull "$D"
want "KS1: pull D succeeds" test "$rc" = 0
want "KS1: 'included D from ...'" out_re "^included $D from '[^']+'\$"
want "KS1: one commit with parents HEAD and a2" parents_are "$h0" "$a2"
want "KS1: D is a2's" test "$(recat HEAD D)" = "$DA"
want "KS1: Q is mine" test "$(recat HEAD Q)" = "$QK"
want "KS1: specs/q.yaml is Q's pin (q-E)" test "$(at HEAD specs/q.yaml)" = "$(pinof "$QK" specs/q.yaml)"
want "KS1: tests/qa.sh comes in as code" test "$(at HEAD tests/qa.sh)" = "$(at "$a2" tests/qa.sh)"

# ================================================================ S1
# S1  D and main's future K5 share specs/shared.yaml (K5 made after a branched) ; a1 edits it and re-pins D ; 'pull D'
ROW=S1
pbase shared
rec D future spec=specs/shared.yaml >/dev/null; c "s: D declares shared" >/dev/null
git checkout -q -b a; echo 'body: s-A' > specs/shared.yaml; DA=$(rec D future spec=specs/shared.yaml); a1=$(c "a1: shared spec")
git checkout -q master; K5v=$(rec K5 future spec=specs/shared.yaml provider=src/order.c creator="$SE"); c "e1: K5" >/dev/null
fact "S1: a's D contains mine" contains "$DA" "$(recat HEAD D)"
fact "S1: git merges a1 without conflict" test -z "$(mt HEAD a)"
fact "S1: git's shared.yaml is a's s-A" test "$(at "$(mtree HEAD a)" specs/shared.yaml)" = "$(at a specs/shared.yaml)"
fact "S1: git's shared.yaml differs from K5's pin" test "$(at "$(mtree HEAD a)" specs/shared.yaml)" != "$(pinof "$K5v" specs/shared.yaml)"
h0=$(git rev-parse HEAD)
pull "$D"
want "S1: pull D succeeds" test "$rc" = 0
want "S1: 'included D from ...'" out_re "^included $D from '[^']+'\$"
want "S1: one commit with parents HEAD and a1" parents_are "$h0" "$a1"
want "S1: D is a1's" test "$(recat HEAD D)" = "$DA"
want "S1: specs/shared.yaml is s-A" test "$(at HEAD specs/shared.yaml)" = "$(at "$a1" specs/shared.yaml)"
want "S1: K5 is re-pinned to s-A in the same commit (6.2)" repinned K5 specs/shared.yaml "$(at "$a1" specs/shared.yaml)"
want "S1: K5's new version is future, on top of mine" on_top K5 "$K5v"

# ================================================================ SF1
# SF1  D and Q share specs/shared.yaml ; a1 and b1 edit it differently and re-pin both ; 'pull D Q'
ROW=SF1
pbase shared
rec D future spec=specs/shared.yaml >/dev/null; rec Q future spec=specs/shared.yaml >/dev/null; c "s: D, Q share" >/dev/null
git checkout -q -b a; echo 'body: s-A' > specs/shared.yaml
DA=$(rec D future spec=specs/shared.yaml); QA=$(rec Q future spec=specs/shared.yaml); a1=$(c a1)
git checkout -q -b b master; echo 'body: s-B' > specs/shared.yaml
DB=$(rec D future spec=specs/shared.yaml); QB=$(rec Q future spec=specs/shared.yaml); b1=$(c b1)
git checkout -q master
fact "SF1: D has two latest versions with different bodies (a1, b1)" diverged "$DA" "$DB" "$(recat HEAD D)"
fact "SF1: Q has two latest versions with different bodies (a1, b1)" diverged "$QA" "$QB" "$(recat HEAD Q)"
fact "SF1: a1 and b1 together conflict on .ait/D, .ait/Q and specs/shared.yaml" test "$(mt a b)" = ".ait/$D .ait/$Q specs/shared.yaml"
fact "SF1: a1 alone merges without conflict" test -z "$(mt HEAD a)"
sf1hint() {
	cmds > "$T/cmds"
	[ "$(wc -l < "$T/cmds" | tr -d ' ')" = 4 ] &&
	[ "$(sed -n 1,2p "$T/cmds" | sort)" = "$(printf '%s\n' "$1" "$2" | sort)" ] &&
	[ "$(sed -n 3,4p "$T/cmds" | sort)" = "$(printf '%s\n' "$3" "$4" | sort)" ]
}
want "SF1: stops, nothing changed" pull_unchanged "$D" "$Q"
want "SF1: an error (exit 1)" stops_with 1 "error: "
want "SF1: hints D=a1 Q=a1 and D=b1 Q=b1 first, then the two mixed choices" sf1hint \
	"pull $D=$a1 $Q=$a1 -m <why>" "pull $D=$b1 $Q=$b1 -m <why>" "pull $D=$a1 $Q=$b1 -m <why>" "pull $D=$b1 $Q=$a1 -m <why>"
h0=$(git rev-parse HEAD)
pull "$D=$a1" "$Q=$a1" -m why
want "SF1: following 'D=a1 Q=a1' succeeds" test "$rc" = 0
want "SF1: 'included D from ...'" out_re "^included $D from '[^']+'\$"
want "SF1: 'included Q from ...'" out_re "^included $Q from '[^']+'\$"
want "SF1: one commit with parents HEAD and a1" parents_are "$h0" "$a1"
want "SF1: D is a1's body with prev {a1's, b1's}" test "$(recat HEAD D)" = "$(newv "$DA" "$DA" "$DB")"
want "SF1: Q is a1's body with prev {a1's, b1's}" test "$(recat HEAD Q)" = "$(newv "$QA" "$QA" "$QB")"
want "SF1: specs/shared.yaml is s-A" test "$(at HEAD specs/shared.yaml)" = "$(at "$a1" specs/shared.yaml)"

# ================================================================ CP2
# CP2  tests/d.sh is code at init (t0) ; a0: tA ; a1: Q ; main: D pins tests/d.sh as an input (t0) ; 'pull Q'
cp2() {
	pbase dsh_t0
	git checkout -q -b a; echo tA > tests/d.sh; c "a0: code" >/dev/null; cond Q q-A; QA=$(rec Q); a1=$(c "a1: Q")
	git checkout -q master; pin_input specs/d.yaml c0 tests/d.sh; DE=$(rec D "$1" inputs=tests/d.sh); c "e1: D pins tests/d.sh" >/dev/null
}
ROW=CP2
cp2 future
fact "CP2: my D pins tests/d.sh at t0" test "$(pinof "$DE" tests/d.sh)" = "$(at init tests/d.sh)"
fact "CP2: git merges tests/d.sh as tA without conflict" test -z "$(codeconf HEAD a)" -a "$(at "$(mtree HEAD a)" tests/d.sh)" = "$(at a tests/d.sh)"
fact "CP2: no version in the taken history names tests/d.sh" test -z "$(hist_mentions a tests/d.sh)"
fact "CP2: a1's Q contains mine" contains "$QA" "$(recat HEAD Q)"
h0=$(git rev-parse HEAD)
pull "$Q"
want "CP2: pull Q succeeds" test "$rc" = 0
want "CP2: 'included Q from ...'" out_re "^included $Q from '[^']+'\$"
want "CP2: one commit with parents HEAD and a1" parents_are "$h0" "$a1"
want "CP2: Q is a1's" test "$(recat HEAD Q)" = "$QA"
want "CP2: tests/d.sh is tA" test "$(at HEAD tests/d.sh)" = "$(at "$a1" tests/d.sh)"
want "CP2: D is re-pinned to tA (6.2)" repinned D tests/d.sh "$(at "$a1" tests/d.sh)"
want "CP2: D's new version is future, on top of mine" on_top D "$DE"
ROW="CP2 (D current)"
cp2 current
fact "CP2 (D current): my D is current and pins tests/d.sh at t0" test "$(state_of "$DE")" = current -a "$(pinof "$DE" tests/d.sh)" = "$(at init tests/d.sh)"
want "CP2 (D current): stops at 5, nothing changed" pull_unchanged "$Q"
want "CP2 (D current): 'error: taking ... changes what contract 'D' checked: 'tests/d.sh''" first_is "^error: taking '[^']+' changes what contract '$D' checked: 'tests/d.sh'\$"
want "CP2 (D current): reopen hint" err_line "hint: reopen it in the same request: 'ait contract pull $Q future amend $D -m <why>'"
want "CP2 (D current): --force hint" err_line "hint: or, if the change is trivial for '$D': 'ait contract pull $Q --force -m <why>'"

# ================================================================ RN1
# RN1  a: D cancelled, specs/d.yaml moved to src/moved.c ; main: row15 of the spec edited, D re-pinned ; 'pull D'
ROW=RN1
pbase
rn=0; while [ $rn -lt 30 ]; do echo "row$rn: 0"; rn=$((rn + 1)); done > specs/d.yaml; Ds=$(rec D); c "s: long spec" >/dev/null
git checkout -q -b a; DC=$(rec D cancelled); git mv specs/d.yaml src/moved.c; a1=$(c "a1: cancel D, move spec")
git checkout -q master; edit specs/d.yaml 'row15: 0' 'row15: E'; DE=$(rec D); c "e1: spec row15" >/dev/null
fact "RN1: a's D is cancelled" test "$(state_of "$DC")" = cancelled
fact "RN1: both sides changed D since s (both bodies differ from it)" diverged "$DC" "$DE" "$Ds"
fact "RN1: no path outside .ait/ conflicts in git's merge" test -z "$(codeconf HEAD a)"
fact "RN1: git's src/moved.c has main's row15" has_line "$(mtree HEAD a)" src/moved.c 'row15: E'
fact "RN1: git's merge has no specs/d.yaml" test -z "$(at "$(mtree HEAD a)" specs/d.yaml)"
h0=$(git rev-parse HEAD)
pull "$D"
want "RN1: pull D succeeds" test "$rc" = 0
want "RN1: D is a's cancelled body with prev {a's, mine}" test "$(recat HEAD D)" = "$(newv "$DC" "$DC" "$DE")"
want "RN1: the terminal-side warning on my change" err_re "^warning: your change to '$D' is overridden: '$D' was cancelled in '[^']+'\$"
want "RN1: the cancelled D places no spec file (no specs/d.yaml)" test "$rc" = 0 -a -z "$(at HEAD specs/d.yaml)"
want "RN1: src/moved.c is a's content: my row15 edit is the spec of my overridden D, not taken (record 107)" test "$rc" = 0 -a -n "$(at HEAD src/moved.c)" -a "$(at HEAD src/moved.c)" = "$(at a src/moved.c)"
want "RN1: one commit with parents HEAD and a1" parents_are "$h0" "$a1"

# ================================================================ SP1
# SP1  a1: a plain commit edits Q's pinned specs/q.yaml (q1, no re-pin) ; a2: D c-A ; main: code ; 'pull D'
ROW=SP1
pbase q0
git checkout -q -b a; echo 'body: q1' > specs/q.yaml; a1=$(c "a1: plain edit of specs/q.yaml")
cond D c-A; DA=$(rec D); a2=$(c "a2: D c-A")
git checkout -q master; echo x > src/x.c; c "e1: code" >/dev/null
fact "SP1: at a2, Q pins specs/q.yaml at q0" test "$(pinof "$(recat a Q)" specs/q.yaml)" = "$(at init specs/q.yaml)"
fact "SP1: at a2, specs/q.yaml is q1" test "$(at a specs/q.yaml)" = "$(blob_of 'body: q1')"
fact "SP1: git merges specs/q.yaml as q1 without conflict" test -z "$(codeconf HEAD a)" -a "$(at "$(mtree HEAD a)" specs/q.yaml)" = "$(at a specs/q.yaml)"
fact "SP1: a's Q is mine" test "$(recat a Q)" = "$(recat HEAD Q)"
h0=$(git rev-parse HEAD)
pull "$D"
want "SP1: pull D succeeds" test "$rc" = 0
want "SP1: 'included D from ...'" out_re "^included $D from '[^']+'\$"
want "SP1: one commit with parents HEAD and a2" parents_are "$h0" "$a2"
want "SP1: D is a2's" test "$(recat HEAD D)" = "$DA"
want "SP1: specs/q.yaml is Q's pin (q0)" test "$(at HEAD specs/q.yaml)" = "$(at init specs/q.yaml)"
want "SP1: the accident warning" err_re "^warning: skipping 'specs/q.yaml' at '[^']+': it was changed outside ait's rules\$"

# ================================================================ CP4
# CP4  tests/d.sh is code at init (t0) ; a0: tA ; a1: Q ; main: tE and D pins tests/d.sh as an input ; 'pull Q'
cp4() {
	pbase dsh_t0
	git checkout -q -b a; echo tA > tests/d.sh; c "a0: code" >/dev/null; cond Q q-A; QA=$(rec Q); a1=$(c "a1: Q")
	git checkout -q master; echo tE > tests/d.sh; pin_input specs/d.yaml c0 tests/d.sh
	DE=$(rec D "$1" inputs=tests/d.sh); c "e1: tE, D pins tests/d.sh" >/dev/null
}
ROW=CP4
cp4 future
fact "CP4: tests/d.sh conflicts in git" test "$(codeconf HEAD a)" = tests/d.sh
fact "CP4: no contract on the incoming side names tests/d.sh" test -z "$(hist_mentions a tests/d.sh)"
fact "CP4: my D pins tests/d.sh at tE" test "$(pinof "$DE" tests/d.sh)" = "$(at HEAD tests/d.sh)"
h0=$(git rev-parse HEAD)
pull "$Q"
conflict_stop tests/d.sh "$h0" "$a1"
want "CP4: .ait/Q in the index is a1's" idx_is ".ait/$Q" "$QA"
want "CP4: .ait/D in the index is mine" idx_is ".ait/$D" "$DE"
finish tests/d.sh tM
want "CP4: the finishing commit succeeds" test "$rc" = 0
want "CP4: the finishing commit has parents HEAD and a1" parents_are "$h0" "$a1"
want "CP4: tests/d.sh is the resolution" test "$(at HEAD tests/d.sh)" = "$(blob_of tM)"
want "CP4: D is re-pinned to the resolution (v14 7.1.3)" repinned D tests/d.sh "$(blob_of tM)"
want "CP4: D's new version is future, on top of mine" on_top D "$DE"
ROW="CP4 (D current)"
cp4 current
want "CP4 (D current): stops at 5, nothing changed" pull_unchanged "$Q"
want "CP4 (D current): an error (exit 1)" stops_with 1 "error: "
want "CP4 (D current): the only hint is the reopen command" cmds_are "pull $Q future amend $D -m <why>"

# ================================================================ SP2
# SP2  a's D newly pins tests/d.sh and edits its line 1 ; main edits line 9 of it as code ; 'pull D'
sp2() {
	pbase dsh_l10
	git checkout -q -b a; edit tests/d.sh l1 'l1 A'; pin_input specs/d.yaml c0 tests/d.sh; DA=$(rec D "$1" inputs=tests/d.sh); a1=$(c "a1: D pins tests/d.sh")
	git checkout -q master; edit tests/d.sh l9 'l9 E'; c "e1: code edit" >/dev/null
}
ROW=SP2
sp2 future
fact "SP2: git merges tests/d.sh without conflict" test -z "$(codeconf HEAD a)"
fact "SP2: git's tests/d.sh has both edits" has_line "$(mtree HEAD a)" tests/d.sh 'l1 A'
fact "SP2: git's tests/d.sh has my line 9" has_line "$(mtree HEAD a)" tests/d.sh 'l9 E'
fact "SP2: git's tests/d.sh differs from a's D pin" test "$(at "$(mtree HEAD a)" tests/d.sh)" != "$(pinof "$DA" tests/d.sh)"
fact "SP2: my side pins no tests/d.sh" test -z "$(pinof "$(recat HEAD D)" tests/d.sh)"
merged=$(at "$(mtree HEAD a)" tests/d.sh)
h0=$(git rev-parse HEAD)
pull "$D"
want "SP2: pull D succeeds" test "$rc" = 0
want "SP2: 'included D from ...'" out_re "^included $D from '[^']+'\$"
want "SP2: one commit with parents HEAD and a1" parents_are "$h0" "$a1"
want "SP2: tests/d.sh is git's merge (my line 9 stays)" test "$(at HEAD tests/d.sh)" = "$merged"
want "SP2: D is re-pinned to it" repinned D tests/d.sh "$merged"
want "SP2: D's new version is future, on top of a's" on_top D "$DA"
ROW="SP2 (D current)"
sp2 current
want "SP2 (D current): stops at 5, nothing changed" pull_unchanged "$D"
want "SP2 (D current): 'error: taking ... merges your changes into what 'D' checked: 'tests/d.sh''" first_is "^error: taking '[^']+' merges your changes into what '$D' checked: 'tests/d.sh'\$"
want "SP2 (D current): reopen hint" err_line "hint: take it reopened: 'ait contract pull $D future amend $D -m <why>'"
want "SP2 (D current): --force hint" err_line "hint: or, if your changes are trivial for '$D': 'ait contract pull $D --force -m <why>'"

# ================================================================ SP3
# SP3  a's D pins tests/d.sh without changing it ; main edits it as code ; 'pull D'
ROW=SP3
pbase dsh_l10
git checkout -q -b a; pin_input specs/d.yaml c0 tests/d.sh; DA=$(rec D future inputs=tests/d.sh); a1=$(c "a1: D pins tests/d.sh")
git checkout -q master; edit tests/d.sh l9 'l9 E'; c "e1: code edit" >/dev/null
fact "SP3: git's tests/d.sh is main's" test -z "$(codeconf HEAD a)" -a "$(at "$(mtree HEAD a)" tests/d.sh)" = "$(at HEAD tests/d.sh)"
fact "SP3: git's tests/d.sh differs from a's D pin" test "$(at "$(mtree HEAD a)" tests/d.sh)" != "$(pinof "$DA" tests/d.sh)"
h0=$(git rev-parse HEAD); mine=$(at HEAD tests/d.sh)
pull "$D"
want "SP3: pull D succeeds" test "$rc" = 0
want "SP3: one commit with parents HEAD and a1" parents_are "$h0" "$a1"
want "SP3: tests/d.sh keeps main's change" test "$(at HEAD tests/d.sh)" = "$mine"
want "SP3: D is re-pinned to it" repinned D tests/d.sh "$mine"
want "SP3: D's new version is future, on top of a's" on_top D "$DA"

# ================================================================ SP4
# SP4  tests/f.sh is code at init ; a's D newly pins it and edits line 1 ; main's K newly pins it and edits line 9 ; 'pull D'
ROW=SP4
pbase fsh_k
git checkout -q -b a; edit tests/f.sh f1 'f1 A'; pin_input specs/d.yaml c0 tests/f.sh; DA=$(rec D future inputs=tests/f.sh); a1=$(c "a1: D pins tests/f.sh")
git checkout -q master; edit tests/f.sh f9 'f9 E'; pin_input specs/k.yaml k0 tests/f.sh; KE=$(rec K future inputs=tests/f.sh); c "e1: K pins tests/f.sh" >/dev/null
merged=$(at "$(mtree HEAD a)" tests/f.sh)
fact "SP4: git merges tests/f.sh without conflict, with both edits" test -z "$(codeconf HEAD a)" -a -n "$merged"
fact "SP4: git's tests/f.sh has line 1 A" has_line "$(mtree HEAD a)" tests/f.sh 'f1 A'
fact "SP4: git's tests/f.sh has line 9 E" has_line "$(mtree HEAD a)" tests/f.sh 'f9 E'
fact "SP4: it differs from both pins" test "$merged" != "$(pinof "$DA" tests/f.sh)" -a "$merged" != "$(pinof "$KE" tests/f.sh)"
fact "SP4: D takes a's version (it contains mine)" contains "$DA" "$(recat HEAD D)"
fact "SP4: K is mine (it contains a's)" contains "$KE" "$(recat a K)"
h0=$(git rev-parse HEAD)
pull "$D"
want "SP4: pull D succeeds" test "$rc" = 0
want "SP4: one commit with parents HEAD and a1" parents_are "$h0" "$a1"
want "SP4: tests/f.sh is git's merge (both edits)" test "$(at HEAD tests/f.sh)" = "$merged"
want "SP4: D is re-pinned to it" repinned D tests/f.sh "$merged"
want "SP4: K is re-pinned to it" repinned K tests/f.sh "$merged"
want "SP4: D's new version is future, on top of a's" on_top D "$DA"
want "SP4: K's new version is future, on top of mine" on_top K "$KE"

# ================================================================ DL1
# DL1  tests/d.sh is code at init ; main's D newly pins it (input) ; a1 deletes it as code and changes Q ; 'pull Q'
ROW=DL1
pbase dsh_t0
git checkout -q -b a; rm tests/d.sh; cond Q q-A; QA=$(rec Q); a1=$(c "a1: delete tests/d.sh, Q")
git checkout -q master; pin_input specs/d.yaml c0 tests/d.sh; DE=$(rec D future inputs=tests/d.sh); c "e1: D pins tests/d.sh" >/dev/null
fact "DL1: git's merge deletes tests/d.sh (no conflict)" test -z "$(codeconf HEAD a)" -a -z "$(at "$(mtree HEAD a)" tests/d.sh)"
fact "DL1: no contract on a's side names tests/d.sh" test -z "$(hist_mentions a tests/d.sh)"
fact "DL1: my D pins tests/d.sh" test "$(pinof "$DE" tests/d.sh)" = "$(at HEAD tests/d.sh)"
h0=$(git rev-parse HEAD)
pull "$Q"
want "DL1: pull Q succeeds" test "$rc" = 0
want "DL1: one commit with parents HEAD and a1" parents_are "$h0" "$a1"
want "DL1: Q is a1's" test "$(recat HEAD Q)" = "$QA"
want "DL1: tests/d.sh stays as D pins it" test "$(at HEAD tests/d.sh)" = "$(pinof "$DE" tests/d.sh)"
want "DL1: D is mine" test "$(recat HEAD D)" = "$DE"
want "DL1: the removal warning" err_re "^warning: skipping the removal of 'tests/d.sh' in '[^']+': '$D' pins it\$"

# ================================================================ DL2
# DL2  tests/d.sh is code at init ; main edits its line 9 and D (future, then current) pins it ; a1 deletes it, Q ; 'pull Q'
for st in future current; do
	ROW="DL2 (D $st)"
	pbase dsh_t10
	git checkout -q -b a; rm tests/d.sh; cond Q q-A; QA=$(rec Q); a1=$(c "a1: delete tests/d.sh, Q")
	git checkout -q master; edit tests/d.sh t9 't9 E'; pin_input specs/d.yaml c0 tests/d.sh; DE=$(rec D $st inputs=tests/d.sh); c "e1: D pins tests/d.sh" >/dev/null
	fact "$ROW: merge-tree says CONFLICT (modify/delete) on tests/d.sh" sh -c 'git merge-tree --write-tree HEAD a | grep -q "^CONFLICT (modify/delete): tests/d.sh deleted in a and modified in HEAD"'
	fact "$ROW: my D ($st) pins my tests/d.sh" test "$(state_of "$DE")" = $st -a "$(pinof "$DE" tests/d.sh)" = "$(at HEAD tests/d.sh)"
	h0=$(git rev-parse HEAD); mine=$(at HEAD tests/d.sh)
	pull "$Q"
	want "$ROW: pull Q succeeds (no code conflict, no stop at 5)" test "$rc" = 0
	want "$ROW: not left merging" test "$rc" = 0 -a ! -f "$(git rev-parse --git-path MERGE_HEAD)"
	want "$ROW: one commit with parents HEAD and a1" parents_are "$h0" "$a1"
	want "$ROW: Q is a1's" test "$(recat HEAD Q)" = "$QA"
	want "$ROW: tests/d.sh is mine" test "$(at HEAD tests/d.sh)" = "$mine"
	want "$ROW: D is mine" test "$(recat HEAD D)" = "$DE"
	want "$ROW: the removal warning" err_re "^warning: skipping the removal of 'tests/d.sh' in '[^']+': '$D' pins it\$"
done

# ================================================================ U1 history (SP5, SP6)
u1() {
	pbase
	git checkout -q -b a; cond D c-A; DA=$(rec D); a1=$(c "a1: D c-A")
	git checkout -q master; cond D c-E; DE=$(rec D); c "e1: D c-E" >/dev/null
}

# SP5  U1 ; 'pull D=a1 -m <why>' : the result is a new version V (body of D@A, prev {D@A, D@E})
ROW=SP5
u1
V=$(newv "$DA" "$DA" "$DE")
fact "SP5: V is not D@A's blob" test "$V" != "$DA"
fact "SP5: V pins specs/d.yaml as D@A does" test "$(pinof "$V" specs/d.yaml)" = "$(pinof "$DA" specs/d.yaml)"
fact "SP5: specs/d.yaml conflicts in git" test "$(codeconf HEAD a)" = specs/d.yaml
h0=$(git rev-parse HEAD)
pull "$D=$a1" -m why
want "SP5: pull D=a1 succeeds" test "$rc" = 0
want "SP5: 'included D from ...'" out_re "^included $D from '[^']+'\$"
want "SP5: one commit with parents HEAD and a1" parents_are "$h0" "$a1"
want "SP5: D is V (not re-pinned)" test "$(recat HEAD D)" = "$V"
want "SP5: specs/d.yaml is c-A" test "$(at HEAD specs/d.yaml)" = "$(at "$a1" specs/d.yaml)"

# SP6  T1: after U1's 'pull D=a1', a writes c-A2 (a2) ; 'pull D' (a2 moved forward ; the result is a new version)
ROW=SP6
u1
# main's 'pull D=a1' as the criteria define it: a1 merged, .ait/D = V, specs/d.yaml as D@A pins it
nohook merge -q --no-commit --no-ff "$a1" >/dev/null 2>&1
V=$(newv "$DA" "$DA" "$DE"); git cat-file -p "$V" > ".ait/$D"; cond D c-A; pick=$(c "pull D=a1")
git checkout -q a; cond D c-A2; DA2=$(rec D); a2=$(c "a2: D c-A2"); git checkout -q master
fact "SP6: HEAD's D is V" test "$(recat HEAD D)" = "$V"
fact "SP6: V kept D@A's body" same_body "$V" "$DA"
fact "SP6: a2's D contains D@A" contains "$DA2" "$DA"
fact "SP6: V contains D@A" contains "$V" "$DA"
fact "SP6: neither of V and a2's D contains the other" test "$(contains "$V" "$DA2" && echo y)$(contains "$DA2" "$V" && echo y)" = ""
fact "SP6: a2's body differs from D@A's" not same_body "$DA2" "$DA"
fact "SP6: the result (c-A2 body, prev {a2's, V}) is another blob than a2's D" test "$(newv "$DA2" "$DA2" "$V")" != "$DA2"
fact "SP6: merge-base(HEAD, a2) is a1" test "$(git merge-base HEAD "$a2")" = "$a1"
fact "SP6: my side's specs/d.yaml is the merge base's" test "$(at HEAD specs/d.yaml)" = "$(at "$a1" specs/d.yaml)"
pull "$D"
want "SP6: pull D succeeds" test "$rc" = 0
want "SP6: one commit with parents HEAD and a2" parents_are "$pick" "$a2"
want "SP6: D is c-A2's body with prev {a2's, V} (not re-pinned)" test "$(recat HEAD D)" = "$(newv "$DA2" "$DA2" "$V")"
want "SP6: specs/d.yaml is c-A2" test "$(at HEAD specs/d.yaml)" = "$(at "$a2" specs/d.yaml)"

# ================================================================ SP7
# SP7  E1: b1 D@B ; b2 Q ; b3 git-reverts b1 ; 'pull D Q' takes b2 : the side is the taken commit, not the ref tip
ROW=SP7
pbase q0
git checkout -q -b b; cond D c-B; DB=$(rec D); b1=$(c "b1: D@B"); cond Q q-B; QB=$(rec Q); b2=$(c "b2: Q")
nohook revert --no-edit "$b1" >/dev/null; git checkout -q master
fact "SP7: b's tip specs/d.yaml is the common ancestor's" test "$(at b specs/d.yaml)" = "$(at init specs/d.yaml)"
fact "SP7: b's tip D is init's" test "$(recat b D)" = "$(recat init D)"
fact "SP7: b2's specs/d.yaml is c-B, D@B's pin" test "$(at "$b2" specs/d.yaml)" = "$(pinof "$DB" specs/d.yaml)"
fact "SP7: D@B contains my D" contains "$DB" "$(recat HEAD D)"
pull "$D" "$Q"
want "SP7: pull D Q succeeds" test "$rc" = 0
want "SP7: fast-forward to b2 (the taken commit)" test "$(git rev-parse HEAD)" = "$b2"
want "SP7: 'included D from' b2" included_is "$D" "$b2"
want "SP7: 'included Q from' b2" included_is "$Q" "$b2"
want "SP7: specs/d.yaml is c-B" test "$(at HEAD specs/d.yaml)" = "$(pinof "$DB" specs/d.yaml)"

# ================================================================ SP8
# SP8  init's D pins specs/shared.yaml ; a1 cancels D and edits shared.yaml ; main: new K5 pins it ; 'pull D'
sp8() {
	pbase shared; rec D future spec=specs/shared.yaml prev=- >/dev/null; reinit
	git checkout -q -b a; DC=$(rec D cancelled spec=specs/shared.yaml); echo 'body: s-A' > specs/shared.yaml; a1=$(c "a1: cancel D, edit shared.yaml")
	git checkout -q master; K5v=$(rec K5 "$1" spec=specs/shared.yaml); c "e1: K5 pins shared.yaml" >/dev/null
}
ROW=SP8
sp8 future
fact "SP8: a1's cancelled D keeps a file row for shared.yaml at s0" test "$(state_of "$DC")" = cancelled -a "$(pinof "$DC" specs/shared.yaml)" = "$(at init specs/shared.yaml)"
fact "SP8: git merges shared.yaml as s-A without conflict" test -z "$(codeconf HEAD a)" -a "$(at "$(mtree HEAD a)" specs/shared.yaml)" = "$(at a specs/shared.yaml)"
fact "SP8: a's cancelled D contains mine" contains "$DC" "$(recat HEAD D)"
h0=$(git rev-parse HEAD)
pull "$D"
want "SP8: pull D succeeds" test "$rc" = 0
want "SP8: one commit with parents HEAD and a1" parents_are "$h0" "$a1"
want "SP8: D is a1's cancelled version" test "$(recat HEAD D)" = "$DC"
want "SP8: specs/shared.yaml is s-A (a's change is code)" test "$(at HEAD specs/shared.yaml)" = "$(at "$a1" specs/shared.yaml)"
want "SP8: no accident warning" passed_without "changed outside ait's rules"
want "SP8: K5 is re-pinned to s-A" repinned K5 specs/shared.yaml "$(at "$a1" specs/shared.yaml)"
want "SP8: K5's new version is future, on top of mine" on_top K5 "$K5v"
ROW="SP8 (K5 current)"
sp8 current
want "SP8 (K5 current): stops at 5, nothing changed" pull_unchanged "$D"
want "SP8 (K5 current): 'error: taking ... changes what contract 'K5' checked: 'specs/shared.yaml''" first_is "^error: taking '[^']+' changes what contract '$K5' checked: 'specs/shared.yaml'\$"
want "SP8 (K5 current): reopen hint" err_line "hint: reopen it in the same request: 'ait contract pull $D future amend $K5 -m <why>'"
want "SP8 (K5 current): --force hint" err_line "hint: or, if the change is trivial for '$K5': 'ait contract pull $D --force -m <why>'"

# ================================================================ SF2
# SF2  SF1 with the mixed choice 'pull D=a1 Q=b1 -m <why>' : D and Q both pin specs/shared.yaml (scen8.py's history)
sf2_init() { shared; rec Q future spec=specs/shared.yaml creator="$SQ" >/dev/null; }
sf2() {
	pbase sf2_init; rec D future spec=specs/shared.yaml prev=- >/dev/null; reinit
	git checkout -q -b a; echo 'body: s-A' > specs/shared.yaml
	DA=$(rec D future spec=specs/shared.yaml); QA=$(rec Q future spec=specs/shared.yaml creator="$SQ"); a1=$(c a1)
	git checkout -q -b b master; echo 'body: s-B' > specs/shared.yaml
	DB=$(rec D future spec=specs/shared.yaml); QB=$(rec Q "$1" spec=specs/shared.yaml creator="$SQ"); b1=$(c b1)
	git checkout -q master
}
ROW=SF2
sf2 future
fact "SF2: D diverged (a1, b1)" diverged "$DA" "$DB" "$(recat HEAD D)"
fact "SF2: Q diverged (a1, b1)" diverged "$QA" "$QB" "$(recat HEAD Q)"
fact "SF2: a1's D pins s-A, b1's Q pins s-B" test "$(pinof "$DA" specs/shared.yaml)" = "$(at "$a1" specs/shared.yaml)" -a "$(pinof "$QB" specs/shared.yaml)" = "$(at "$b1" specs/shared.yaml)"
fact "SF2: the two taken changes conflict on shared.yaml" test "$(codeconf a b)" = specs/shared.yaml
h0=$(git rev-parse HEAD)
pull "$D=$a1" "$Q=$b1" -m why
conflict_stop specs/shared.yaml "$h0" "$a1" "$b1"
want "SF2: .ait/D in the index is a1's body with prev {a1's, b1's}" idx_is ".ait/$D" "$(newv "$DA" "$DA" "$DB")"
want "SF2: .ait/Q in the index is b1's body with prev {a1's, b1's}" idx_is ".ait/$Q" "$(newv "$QB" "$QA" "$QB")"
want "SF2: MERGE_MSG carries -m" msg_has why
finish specs/shared.yaml 'body: s-M'
want "SF2: the finishing commit succeeds" test "$rc" = 0
want "SF2: the finishing commit has parents HEAD, a1 and b1" parents_are "$h0" "$a1" "$b1"
want "SF2: D is re-pinned to the resolution" repinned D specs/shared.yaml "$(blob_of 'body: s-M')"
want "SF2: Q is re-pinned to the resolution" repinned Q specs/shared.yaml "$(blob_of 'body: s-M')"
want "SF2: shared.yaml is the resolution" test "$(at HEAD specs/shared.yaml)" = "$(blob_of 'body: s-M')"
want "SF2: D is future, on top of a1's" on_top D "$DA"
want "SF2: D contains b1's" contains "$(recat HEAD D)" "$DB"
want "SF2: Q contains a1's" contains "$(recat HEAD Q)" "$QA"
want "SF2: Q is future, on top of b1's" on_top Q "$QB"
ROW="SF2 (Q current at b1)"
sf2 current
fact "SF2 (Q current at b1): b1's Q is current" test "$(state_of "$QB")" = current
want "SF2 (Q current at b1): stops at 5, nothing changed" pull_unchanged "$D=$a1" "$Q=$b1" -m why
want "SF2 (Q current at b1): an error (exit 1)" stops_with 1 "error: "
want "SF2 (Q current at b1): the only hint is the choice with Q reopened" cmds_are "pull $D=$a1 $Q=$b1 future amend $Q -m <why>"

# ================================================================ SP4c
# SP4c  as SP4, but a's D and main's K edit the same line of tests/f.sh ; 'pull D'
ROW=SP4c
pbase fsh_k
git checkout -q -b a; edit tests/f.sh f1 'f1 A'; pin_input specs/d.yaml c0 tests/f.sh; DA=$(rec D future inputs=tests/f.sh); a1=$(c "a1: D pins tests/f.sh")
git checkout -q master; edit tests/f.sh f1 'f1 E'; pin_input specs/k.yaml k0 tests/f.sh; KE=$(rec K future inputs=tests/f.sh); c "e1: K pins tests/f.sh" >/dev/null
fact "SP4c: tests/f.sh conflicts in git" test "$(codeconf HEAD a)" = tests/f.sh
fact "SP4c: both sides' contents are pinned by the result (a's D, my K)" test "$(pinof "$DA" tests/f.sh)" = "$(at "$a1" tests/f.sh)" -a "$(pinof "$KE" tests/f.sh)" = "$(at HEAD tests/f.sh)"
fact "SP4c: D's one latest version is a's (it contains mine)" contains "$DA" "$(recat HEAD D)"
fact "SP4c: K is mine (it contains a's)" contains "$KE" "$(recat a K)"
h0=$(git rev-parse HEAD)
pull "$D"
conflict_stop tests/f.sh "$h0" "$a1"
want "SP4c: .ait/D in the index is a1's" idx_is ".ait/$D" "$DA"
want "SP4c: .ait/K in the index is mine" idx_is ".ait/$K" "$KE"
resolved=$(lines10 f | sed 's/^f1$/f1 M/'); finish tests/f.sh "$resolved"
want "SP4c: the finishing commit succeeds" test "$rc" = 0
want "SP4c: the finishing commit has parents HEAD and a1" parents_are "$h0" "$a1"
want "SP4c: D is re-pinned to the resolution" repinned D tests/f.sh "$(blob_of "$resolved")"
want "SP4c: K is re-pinned to the resolution" repinned K tests/f.sh "$(blob_of "$resolved")"
want "SP4c: D is future, on top of a1's" on_top D "$DA"
want "SP4c: K is future, on top of mine" on_top K "$KE"

# ================================================================ RN2
# RN2  a1 moves specs/d.yaml to specs/d2.yaml and edits its line 6 (D@A pins d2.yaml) ; main edits line 1 (D@E) ; 'pull D=a1'
ROW=RN2
rn2_init() { printf 'body: c0\n# l2\n# l3\n# l4\n# l5\n# l6\n' > specs/d.yaml; }
pbase rn2_init
git checkout -q -b a; git mv specs/d.yaml specs/d2.yaml; edit specs/d2.yaml '# l6' '# l6 A'; DA=$(rec D future spec=specs/d2.yaml); a1=$(c "a1: move to d2.yaml, edit l6")
git checkout -q master; edit specs/d.yaml 'body: c0' 'body: c-E'; DE=$(rec D); c "e1: c-E" >/dev/null
fact "RN2: git's specs/d2.yaml has main's 'body: c-E' (rename followed)" has_line "$(mtree HEAD a)" specs/d2.yaml 'body: c-E'
fact "RN2: git's specs/d2.yaml has a's line 6" has_line "$(mtree HEAD a)" specs/d2.yaml '# l6 A'
fact "RN2: git's specs/d2.yaml differs from D@A's pin" test "$(at "$(mtree HEAD a)" specs/d2.yaml)" != "$(pinof "$DA" specs/d2.yaml)"
fact "RN2: git's merge has no specs/d.yaml" test -z "$(at "$(mtree HEAD a)" specs/d.yaml)"
fact "RN2: D diverged (D@A, D@E)" diverged "$DA" "$DE" "$(recat init D)"
h0=$(git rev-parse HEAD)
pull "$D=$a1" -m why
want "RN2: pull D=a1 succeeds" test "$rc" = 0
want "RN2: one commit with parents HEAD and a1" parents_are "$h0" "$a1"
want "RN2: specs/d2.yaml is D@A's pin" test "$(at HEAD specs/d2.yaml)" = "$(pinof "$DA" specs/d2.yaml)"
want "RN2: no specs/d.yaml" test "$rc" = 0 -a -z "$(at HEAD specs/d.yaml)"
want "RN2: D is D@A's body with prev {D@A, D@E} (not re-pinned)" test "$(recat HEAD D)" = "$(newv "$DA" "$DA" "$DE")"

# ================================================================ SP9
# SP9  main's D pins tests/d.sh (t0) ; a1: tests/d.sh line 1 as code, and Q ; b1: Q2 only ; 'pull Q Q2' ; both commit-ID orders
sp9_init() { dsh_t10; cond Q2 p0; rec Q2 >/dev/null; }
for order in a1b1 b1a1; do
	ROW="SP9 ($order)"
	pbase sp9_init
	pin_input specs/d.yaml c0 tests/d.sh; DE=$(rec D future inputs=tests/d.sh); c "e1: D pins tests/d.sh" >/dev/null
	git checkout -q -b b init; cond Q2 p-B; rec Q2 >/dev/null; b1=$(c "b1: Q2")
	k=0
	while :; do
		git checkout -q -B a init; edit tests/d.sh t1 't1 A'; cond Q q-A; rec Q >/dev/null; a1=$(c "a1: tests/d.sh, Q$k")
		lo=$(printf '%s\n' "$a1" "$b1" | sort | head -1)
		{ [ $order = a1b1 ] && [ "$lo" = "$a1" ]; } || { [ $order = b1a1 ] && [ "$lo" = "$b1" ]; } && break
		k=$((k + 1))
	done
	git checkout -q master
	fact "$ROW: the commit IDs are in this order" test "$(printf '%s\n' "$a1" "$b1" | sort | tr '\n' ' ')" = "$(if [ $order = a1b1 ]; then echo "$a1 $b1 "; else echo "$b1 $a1 "; fi)"
	fact "$ROW: merge-base(HEAD, a1) is init" test "$(git merge-base HEAD "$a1")" = "$(git rev-parse init)"
	fact "$ROW: merge-base(HEAD, b1) is init" test "$(git merge-base HEAD "$b1")" = "$(git rev-parse init)"
	fact "$ROW: no version at a1 pins tests/d.sh" not git grep -q -e '^file tests/d.sh ' "$a1" -- .ait
	h0=$(git rev-parse HEAD)
	pull "$Q" "$Q2"
	want "$ROW: pull Q Q2 succeeds" test "$rc" = 0
	want "$ROW: one commit with parents HEAD, a1 and b1" parents_are "$h0" "$a1" "$b1"
	want "$ROW: tests/d.sh is 't1 A'" test "$(at HEAD tests/d.sh)" = "$(lines10 t | sed 's/^t1$/t1 A/' | git hash-object --stdin)"
	want "$ROW: D is re-pinned to it (6.2)" repinned D tests/d.sh "$(lines10 t | sed 's/^t1$/t1 A/' | git hash-object --stdin)"
	want "$ROW: D's new version is future, on top of mine" on_top D "$DE"
	want "$ROW: no accident warning" passed_without "changed outside ait's rules"
done

# ================================================================ SP10
# SP10  a1: D c-A (ait) ; a2: a plain commit edits specs/d.yaml (c-X) ; a3: Q ; 'pull D Q' (end a3), and 'pull D' then 'pull Q'
sp10() {
	pbase q0
	git checkout -q -b a; cond D c-A; DA=$(rec D "$1"); a1=$(c "a1: D c-A")
	cond D c-X; a2=$(c "a2: plain edit of specs/d.yaml"); cond Q q-A; QA=$(rec Q); a3=$(c "a3: Q")
	git checkout -q master
}
ROW=SP10
sp10 future
fact "SP10: a3's D is a1's D@A" test "$(recat "$a3" D)" = "$DA" -a "$(recat "$a1" D)" = "$DA"
fact "SP10: a3's specs/d.yaml is c-X, not D@A's pin" test "$(at "$a3" specs/d.yaml)" = "$(blob_of 'body: c-X')" -a "$(pinof "$DA" specs/d.yaml)" = "$(blob_of 'body: c-A')"
fact "SP10: a1 is an ancestor of a3 (one end, a3)" is_ancestor "$a1" "$a3"
h0=$(git rev-parse HEAD); tpre=$(git rev-parse HEAD^{tree})
pull "$D" "$Q"
want "SP10: pull D Q succeeds" test "$rc" = 0
want "SP10: one commit with parents HEAD and a3" parents_are "$h0" "$a3"
want "SP10: specs/d.yaml is c-A" test "$(at HEAD specs/d.yaml)" = "$(pinof "$DA" specs/d.yaml)"
want "SP10: D is D@A (not re-pinned)" test "$(recat HEAD D)" = "$DA"
want "SP10: Q is a3's" test "$(recat HEAD Q)" = "$QA"
want "SP10: the accident warning on a2's edit" err_re "^warning: skipping 'specs/d.yaml' at '[^']+': it was changed outside ait's rules\$"
tboth=$(git rev-parse HEAD^{tree})
ROW="SP10 (pull D, then pull Q)"
sp10 future
pull "$D"; rcd=$rc
pull "$Q"
want "SP10: 'pull D' then 'pull Q' succeed and give the tree of 'pull D Q'" test "$rcd" = 0 -a "$rc" = 0 -a "$tboth" != "$tpre" -a "$(git rev-parse HEAD^{tree})" = "$tboth"
ROW="SP10 (D current)"
sp10 current
fact "SP10 (D current): D@A is current" test "$(state_of "$DA")" = current
pull "$D" "$Q"
want "SP10 (D current): pull D Q succeeds (no stop at 5)" test "$rc" = 0
want "SP10 (D current): D is D@A" test "$(recat HEAD D)" = "$DA"
want "SP10 (D current): specs/d.yaml is c-A" test "$(at HEAD specs/d.yaml)" = "$(pinof "$DA" specs/d.yaml)"

# ================================================================ SP11
# SP11  as SP10, but a1's D newly pins tests/t.sh with line 1 A ; a2 edits line 1 to X by a plain commit ; main edits line 9
ROW=SP11
sp11_init() { mkdir -p tests; lines10 t > tests/t.sh; q0; }
pbase sp11_init
git checkout -q -b a; edit tests/t.sh t1 't1 A'; pin_input specs/d.yaml c-A tests/t.sh; DA=$(rec D future inputs=tests/t.sh); a1=$(c a1)
edit tests/t.sh 't1 A' 't1 X'; a2=$(c "a2: plain edit"); cond Q q-A; QA=$(rec Q); a3=$(c "a3: Q")
git checkout -q master; edit tests/t.sh t9 't9 E'; c "e1: code" >/dev/null
tk=$(taken_out "$a3" tests/t.sh "$(pinof "$DA" tests/t.sh)"); tt=$(mtree HEAD "$tk")
fact "SP11: a3's D is D@A ; a3's tests/t.sh (line 1 X) is not its pin" test "$(recat "$a3" D)" = "$DA" -a "$(at "$a3" tests/t.sh)" != "$(pinof "$DA" tests/t.sh)"
fact "SP11: with a's side read as its pin, git merges without conflict" test -z "$(codeconf HEAD "$tk")"
fact "SP11: that merge has line 1 A" has_line "$tt" tests/t.sh 't1 A'
fact "SP11: that merge has line 9 E" has_line "$tt" tests/t.sh 't9 E'
h0=$(git rev-parse HEAD)
pull "$D" "$Q"
want "SP11: pull D Q succeeds" test "$rc" = 0
want "SP11: one commit with parents HEAD and a3" parents_are "$h0" "$a3"
want "SP11: tests/t.sh is line 1 A and line 9 E" test "$(at HEAD tests/t.sh)" = "$(at "$tt" tests/t.sh)"
want "SP11: D is re-pinned to it" repinned D tests/t.sh "$(at "$tt" tests/t.sh)"
want "SP11: D's new version is future, on top of D@A" on_top D "$DA"
want "SP11: the accident warning on a2's edit" err_re "^warning: skipping 'tests/t.sh' at '[^']+': it was changed outside ait's rules\$"

# ================================================================ SP12
# SP12  new A, B, C on a, b, c each pin tests/shared.sh and edit lines 1, 10, 20 ; 'pull A B C'
ROW=SP12
sp12_init() { mkdir -p tests; sl=1; while [ $sl -le 20 ]; do echo "line$sl"; sl=$((sl + 1)); done > tests/shared.sh; }
sp12_end() { git checkout -q -b "$1" init; edit tests/shared.sh "line$3" "line$3 $1"; pin_input "specs/$(echo "$2" | tr 'A-Z' 'a-z').yaml" x tests/shared.sh; rec "$2" future inputs=tests/shared.sh; }
pbase sp12_init
vA=$(sp12_end a A 1); a1=$(c a1)
vB=$(sp12_end b B 10); b1=$(c b1)
vC=$(sp12_end c C 20); c1=$(c c1)
git checkout -q master
st3=$(seqmerge "$a1" "$b1" "$c1")
fact "SP12: each end's tests/shared.sh is its contract's pin" test "$(pinof "$vA" tests/shared.sh)" = "$(at "$a1" tests/shared.sh)" -a "$(pinof "$vB" tests/shared.sh)" = "$(at "$b1" tests/shared.sh)" -a "$(pinof "$vC" tests/shared.sh)" = "$(at "$c1" tests/shared.sh)"
fact "SP12: git merges the three in turn without conflict" test ! -s "$T/sconf"
fact "SP12: the merge has line1 a" has_line "$st3" tests/shared.sh 'line1 a'
fact "SP12: the merge has line10 b" has_line "$st3" tests/shared.sh 'line10 b'
fact "SP12: the merge has line20 c" has_line "$st3" tests/shared.sh 'line20 c'
h0=$(git rev-parse HEAD)
pull "$A" "$B" "$C"
want "SP12: pull A B C succeeds" test "$rc" = 0
want "SP12: one commit with parents HEAD, a1, b1 and c1" parents_are "$h0" "$a1" "$b1" "$c1"
want "SP12: tests/shared.sh has all three changes" test "$(at HEAD tests/shared.sh)" = "$(at "$st3" tests/shared.sh)"
want "SP12: A is re-pinned to it" repinned A tests/shared.sh "$(at "$st3" tests/shared.sh)"
want "SP12: B is re-pinned to it" repinned B tests/shared.sh "$(at "$st3" tests/shared.sh)"
want "SP12: C is re-pinned to it" repinned C tests/shared.sh "$(at "$st3" tests/shared.sh)"
want "SP12: A is future, on top of a's" on_top A "$vA"
want "SP12: B is future, on top of b's" on_top B "$vB"
want "SP12: C is future, on top of c's" on_top C "$vC"

# ================================================================ SP13
# SP13  U1, where main's new K5 shares specs/d.yaml and was re-pinned with c-E ; 'pull D=a1 -m <why>'
ROW=SP13
pbase
git checkout -q -b a; cond D c-A; DA=$(rec D); a1=$(c "a1: D c-A")
git checkout -q master; rec K5 future spec=specs/d.yaml creator="$SK" prev=- >/dev/null; c "e0: K5 shares specs/d.yaml" >/dev/null
cond D c-E; DE=$(rec D); K5v=$(rec K5 future spec=specs/d.yaml creator="$SK"); c "e1: D c-E (K5 re-pinned)" >/dev/null
fact "SP13: my c-E is K5's pin (K5 stays in the result)" test "$(pinof "$K5v" specs/d.yaml)" = "$(at HEAD specs/d.yaml)"
fact "SP13: a's c-A is D@A's pin (the chosen D)" test "$(pinof "$DA" specs/d.yaml)" = "$(at "$a1" specs/d.yaml)"
fact "SP13: specs/d.yaml conflicts in git" test "$(codeconf HEAD a)" = specs/d.yaml
h0=$(git rev-parse HEAD)
pull "$D=$a1" -m why
conflict_stop specs/d.yaml "$h0" "$a1"
want "SP13: .ait/D in the index is D@A's body with prev {D@A, D@E}" idx_is ".ait/$D" "$(newv "$DA" "$DA" "$DE")"
want "SP13: .ait/K5 in the index is mine" idx_is ".ait/$K5" "$K5v"
want "SP13: MERGE_MSG carries -m" msg_has why
finish specs/d.yaml 'body: c-M'
want "SP13: the finishing commit succeeds" test "$rc" = 0
want "SP13: the finishing commit has parents HEAD and a1" parents_are "$h0" "$a1"
want "SP13: D is re-pinned to the resolution (decision 29)" repinned D specs/d.yaml "$(blob_of 'body: c-M')"
want "SP13: K5 is re-pinned to the resolution (decision 29)" repinned K5 specs/d.yaml "$(blob_of 'body: c-M')"
want "SP13: D is future, on top of the pull's version" on_top D "$(newv "$DA" "$DA" "$DE")"
want "SP13: K5 is future, on top of mine" on_top K5 "$K5v"

# ================================================================ MF3
# MF3  main's D pins tests/f.sh (base) ; a1: Q, and a malformed W (two state rows) whose file row pins a's tests/f.sh ; 'pull Q'
ROW=MF3
mf3_init() { mkdir -p tests; echo 'echo base' > tests/f.sh; q0; pin_input specs/d.yaml c0 tests/f.sh; }
pbase mf3_init; rec D future inputs=tests/f.sh prev=- >/dev/null; reinit
git checkout -q -b a; cond Q q-A; QA=$(rec Q); echo 'echo incoming' > tests/f.sh; cond W w0; rec W future inputs=tests/f.sh >/dev/null
awk '{ print } $0 == "state future" { print "state cancelled" }' ".ait/$W" > "$T/w" && cat "$T/w" > ".ait/$W"; a1=$(c "a1: Q, malformed W")
git checkout -q master
fact "MF3: my D pins tests/f.sh at base" test "$(pinof "$(recat HEAD D)" tests/f.sh)" = "$(at HEAD tests/f.sh)"
fact "MF3: a's W has two state rows (malformed)" test "$(git cat-file -p "$(recat a W)" | grep -c '^state ')" = 2
fact "MF3: W's file row pins tests/f.sh at a's content" test "$(pinof "$(recat a W)" tests/f.sh)" = "$(at a tests/f.sh)"
fact "MF3: git alone would take a's tests/f.sh" test -z "$(codeconf HEAD a)" -a "$(at "$(mtree HEAD a)" tests/f.sh)" = "$(at a tests/f.sh)"
h0=$(git rev-parse HEAD); base=$(at HEAD tests/f.sh)
pull "$Q"
want "MF3: pull Q succeeds" test "$rc" = 0
want "MF3: one commit with parents HEAD and a1" parents_are "$h0" "$a1"
want "MF3: Q is a1's" test "$(recat HEAD Q)" = "$QA"
want "MF3: tests/f.sh is base" test "$(at HEAD tests/f.sh)" = "$base"
want "MF3: W does not come in" test "$rc" = 0 -a -z "$(recat HEAD W)"
want "MF3: specs/w.yaml (pinned only by W) does not come in" test "$rc" = 0 -a -z "$(at HEAD specs/w.yaml)"
done_testing
