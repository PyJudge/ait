#!/bin/sh
# pull criteria 3.10 확인 검사 (C1, C2, F2, C3, C4, S2, S3, S4, IND1, CK4, CK5, CK6, CK7, S5, RV3b, NU1, NU2)
# Histories ported from spec/drafts/pull-criteria-runs: scen.py C() (C1-C3, S1/S3), scen4.py (NU1, IND1, U6),
# scen5.py (CK4-CK6, F2, NU2), scen6.py (S5, CK7, RV3b), scen8.py (RV3b'), flows.py (C2, S3: follow the hint).
# The step-5 messages and hints are the ones of 2절 5. Notifications (decision 16, 18) are left out.
. "$(dirname "$0")/lib.sh"

# local helpers
SE="agent-e S000000000009"	# main's other agent ('agent-e S9' in the Python scripts)
# the tree git's merge-tree gives for A and B (conflicted files carry markers)
mtree() { git merge-tree --write-tree "$1" "$2" 2>/dev/null | head -1; }
changed() { git diff-tree -r --name-only "$1" "$2" | tr '\n' ' ' | sed 's/ $//'; }
# a version's PINS (1절: its provider and file rows, LF-ended, hashed), its check line, the blob it pins for PATH
pins_b() { git cat-file -p "$1" | grep -E '^(provider|file) ' | git hash-object --stdin; }
check_of() { git cat-file -p "$1" | sed -n 's/^check //p'; }
pin_of() { git cat-file -p "$1" | awk -v p="$2" '$1=="provider"&&$2==p{print $3} $1=="file"&&$2==p{print $4}'; }
# compose V CHECK P...: V's body with the check line CHECK (before creator, decision 17) and prev P..., as a blob
compose() {
	v=$1 ck=$2; shift 2
	{ git cat-file -p "$v" | grep -v -e '^prev ' -e '^check ' | awk -v ck="$ck" '/^creator /&&ck!=""{print "check " ck} {print}'
	  for p in $(for x; do echo "$x"; done | sort); do echo "prev $p"; done; } | git hash-object -w --stdin
}
# verified REV NAME: every provider row of NAME at REV pins the blob REV has there (not unverified)
verified() {
	git cat-file -p "$(recat "$1" "$2")" | awk '$1=="provider"{print $2, $3}' > "$T/pv"
	while read -r p b; do [ "$(at "$1" "$p")" = "$b" ] || return 1; done < "$T/pv"
}
# on the last run
err_re() { head -1 "$T/err" | grep -qE -- "$1"; }
hint_re() { grep '^hint: ' "$T/err" | grep -qE -- "$1"; }
first_hint_re() { grep '^hint: ' "$T/err" | head -1 | grep -qE -- "$1"; }
only_hint() { [ "$(grep -c '^hint: ' "$T/err")" = 1 ] && hint_re "$1"; }
out_re() { grep -qE -- "$1" "$T/out"; }
warns() { grep -qE -- "^warning: $1" "$T/err"; }
# git merge prints its conflict lines on stdout; the criteria give the lines, not the stream
said() { cat "$T/out" "$T/err" | grep -qxF -- "$1"; }
FINISH="^hint: commit the resolution first: '(AIT_SESSION=[^ ]+ )?git commit --allow-empty --no-edit --cleanup=strip', then continue as git says\$"
# the merge state of a code conflict (2절 6: like 'git merge --no-ff')
merge_heads() { tr '\n' ' ' < "$(git rev-parse --git-dir)/MERGE_HEAD" | sed 's/ $//'; }
merge_mode() { cat "$(git rev-parse --git-dir)/MERGE_MODE"; }
idx() { git rev-parse -q --verify ":$1" 2>/dev/null; }
wtb() { git hash-object "$1"; }
SCOPE="'[^']+'"	# a commit name in a message ('a1' in the criteria is a placeholder)

# C1  a1 fulfils D (stock.c line 1 -> A, pins it gA, check line) ; main did not touch stock.c
pbase
git checkout -q -b a; line 1 A; VA=$(rec D current); A1=$(c "a: fulfil D"); git checkout -q master
GA=$(at a src/stock.c); M=$(mtree HEAD a)
fact "C1: a1's D is current, pins stock.c as gA, check reported" test "$(state_of "$VA")" = current -a "$(pin_of "$VA" src/stock.c)" = "$GA" -a "$(check_of "$VA")" = "reported $(pins_b "$VA")"
fact "C1: merge-tree HEAD a1 has no conflict" test -z "$(mt HEAD a)"
fact "C1: merging changes .ait/D and src/stock.c" test "$(changed HEAD "$M")" = ".ait/$D src/stock.c"
fact "C1: merged stock.c is gA" test "$(git rev-parse "$M:src/stock.c")" = "$GA"
pull "$D"
want "C1: pull D succeeds" test "$rc" = 0
want "C1: fast-forward to a1 (2절 6)" test "$(git rev-parse HEAD)" = "$A1"
want "C1: 'included D from ...'" out_re "^included $D from '.+'\$"
want "C1: D is current and not unverified" eval 'test "$(state_of "$(recat HEAD D)")" = current && verified HEAD D'

# C2  as C1, but main changed stock.c line 9
pbase
git checkout -q -b a; line 1 A; VA=$(rec D current); A1=$(c "a: fulfil D"); git checkout -q master
line 9 E; E1=$(c "e: line9")
GA=$(at a src/stock.c); M=$(mtree HEAD a)
fact "C2: merge-tree HEAD a1 has no conflict" test -z "$(mt HEAD a)"
fact "C2: merging changes .ait/D and src/stock.c" test "$(changed HEAD "$M")" = ".ait/$D src/stock.c"
fact "C2: merged stock.c is not a1's pin gA" test "$(git rev-parse "$M:src/stock.c")" != "$GA" -a "$(pin_of "$VA" src/stock.c)" = "$GA"
want "C2: stops, nothing changed" pull_unchanged "$D"
want "C2: exit 1" test "$rc" = 1
want "C2: 'taking ... merges your changes into what D checked: src/stock.c'" err_re "^error: taking $SCOPE merges your changes into what '$D' checked: 'src/stock.c'\$"
want "C2: hint: take it reopened" err_line "hint: take it reopened: 'ait contract pull $D future amend $D -m <why>'"
want "C2: hint: or --force" err_line "hint: or, if your changes are trivial for '$D': 'ait contract pull $D --force -m <why>'"
# follow the first hint (flows.py C2: validate passes, nothing unverified)
pull "$D" future amend "$D" -m why
want "C2 follow: 'pull D future amend D' succeeds" test "$rc" = 0
want "C2 follow: a commit with parents HEAD and a1" test "$(parents)" = 2 -a "$(git rev-parse HEAD^1)" = "$E1" -a "$(git rev-parse HEAD^2 2>/dev/null)" = "$A1"
want "C2 follow: 'included D from ...'" out_re "^included $D from '.+'\$"
want "C2 follow: D is future with no check line, on top of a1's D" test "$(state_of "$(recat HEAD D)")" = future -a -z "$(check_of "$(recat HEAD D)")" -a "$(prevs "$(recat HEAD D)")" = "$VA"
want "C2 follow: stock.c is the merged one (line 1 A and line 9 E)" test "$(at HEAD src/stock.c)" = "$(git rev-parse "$M:src/stock.c")"
want "C2 follow: 'D' is reopened by this pull" warns "'$D' is reopened by this pull"

# F2  C2 and both change src/other.c (no contract checks it)
o0() { echo o0 > src/other.c; }
pbase o0
git checkout -q -b a; line 1 A; VA=$(rec D current); echo oA > src/other.c; A1=$(c "a: fulfil D"); git checkout -q master
line 9 E; echo oE > src/other.c; E1=$(c "e: code")
M=$(mtree HEAD a)
fact "F2: merge-tree conflicts on src/other.c only" test "$(mt HEAD a)" = src/other.c
fact "F2: merged stock.c is not D's pin" test "$(git rev-parse "$M:src/stock.c")" != "$(pin_of "$VA" src/stock.c)"
fact "F2: no contract in HEAD or a names src/other.c" not git grep -q src/other.c HEAD a -- .ait
want "F2: stops, nothing changed" pull_unchanged "$D"
want "F2: exit 1" test "$rc" = 1
want "F2: 'taking ... merges your changes into what D checked: src/stock.c'" err_re "^error: taking $SCOPE merges your changes into what '$D' checked: 'src/stock.c'\$"
want "F2: the only hint is 'ait contract pull D future amend D -m <why>' (no --force: the pull would conflict)" only_hint "'ait contract pull $D future amend $D -m <why>'\$"
# follow the hint: D comes in future and the code conflict stops like git
pull "$D" future amend "$D" -m why
want "F2 follow: stops like git (exit 1)" test "$rc" = 1
want "F2 follow: CONFLICT line" said "CONFLICT (content): Merge conflict in src/other.c"
want "F2 follow: 'Automatic merge failed' line" said "Automatic merge failed; fix conflicts and then commit the result."
want "F2 follow: the finishing-commit hint" grep -qE "$FINISH" "$T/err"
want "F2 follow: MERGE_HEAD is a1, MERGE_MODE no-ff" test "$(merge_heads 2>/dev/null)" = "$A1" -a "$(merge_mode 2>/dev/null)" = no-ff
want "F2 follow: only src/other.c is unmerged" test "$(unmerged)" = src/other.c
want "F2 follow: .ait/D is future, on top of a1's D (index and work tree)" test "$(state_of "$(idx ".ait/$D")")" = future -a "$(prevs "$(idx ".ait/$D")")" = "$VA" -a "$(wtb ".ait/$D")" = "$(idx ".ait/$D")"
want "F2 follow: stock.c merged without conflict" test "$(idx src/stock.c)" = "$(git rev-parse "$M:src/stock.c")"

# C3  as C1, but main changed stock.c line 1 differently
pbase
git checkout -q -b a; line 1 A; VA=$(rec D current); A1=$(c "a: fulfil D"); git checkout -q master
line 1 E; E1=$(c "e: line1")
fact "C3: merge-tree conflicts on src/stock.c" test "$(mt HEAD a)" = src/stock.c
fact "C3: merging changes .ait/D and src/stock.c" test "$(changed HEAD "$(mtree HEAD a)")" = ".ait/$D src/stock.c"
fact "C3: a1's D is current and checks src/stock.c" test "$(state_of "$VA")" = current -a -n "$(pin_of "$VA" src/stock.c)"
want "C3: stops, nothing changed" pull_unchanged "$D"
# no wording in the criteria (record 114): the exit code and 'error:' only
want "C3: first stderr line is 'error: ...'" err_re '^error: '
want "C3: the wording of the output table (record 117)" err_re "^error: taking '[^']+' conflicts in what '$D' checked: 'src/stock.c'\$"
want "C3: exit code 1" test "$rc" = 1
want "C3: the only hint is 'ait contract pull D future amend D -m <why>'" only_hint "'ait contract pull $D future amend $D -m <why>'\$"
# follow the hint: D comes in future, then the code conflict stops like git
pull "$D" future amend "$D" -m why
want "C3 follow: stops like git (exit 1)" test "$rc" = 1
want "C3 follow: CONFLICT line" said "CONFLICT (content): Merge conflict in src/stock.c"
want "C3 follow: 'Automatic merge failed' line" said "Automatic merge failed; fix conflicts and then commit the result."
want "C3 follow: the finishing-commit hint" grep -qE "$FINISH" "$T/err"
want "C3 follow: MERGE_HEAD is a1, MERGE_MODE no-ff" test "$(merge_heads 2>/dev/null)" = "$A1" -a "$(merge_mode 2>/dev/null)" = no-ff
want "C3 follow: only src/stock.c is unmerged" test "$(unmerged)" = src/stock.c
want "C3 follow: .ait/D is future, on top of a1's D (index and work tree)" test "$(state_of "$(idx ".ait/$D")")" = future -a "$(prevs "$(idx ".ait/$D")")" = "$VA" -a "$(wtb ".ait/$D")" = "$(idx ".ait/$D")"

# C4  a1 changes D (future, c-A) and src/other.c ; main changed other.c differently ; no contract checks other.c
x0() { echo x=0 > src/other.c; }
pbase x0
git checkout -q -b a; echo x=A > src/other.c; cond D c-A; VA=$(rec D); A1=$(c "a: D, other"); git checkout -q master
echo x=E > src/other.c; E1=$(c "e: other")
D0=$(recat init D); SA1=$(at a specs/d.yaml)
fact "C4: merge-tree conflicts on src/other.c only" test "$(mt HEAD a)" = src/other.c
fact "C4: merging changes .ait/D, specs/d.yaml and src/other.c" test "$(changed HEAD "$(mtree HEAD a)")" = ".ait/$D specs/d.yaml src/other.c"
fact "C4: a1's D is future and contains init's D" test "$(state_of "$VA")" = future -a "$(prevs "$VA")" = "$D0"
pull "$D"
want "C4: stops like git (exit 1)" test "$rc" = 1
want "C4: CONFLICT line" said "CONFLICT (content): Merge conflict in src/other.c"
want "C4: 'Automatic merge failed' line" said "Automatic merge failed; fix conflicts and then commit the result."
want "C4: the finishing-commit hint" grep -qE "$FINISH" "$T/err"
want "C4: HEAD did not move" test "$(git rev-parse HEAD)" = "$E1"
want "C4: MERGE_HEAD is a1, MERGE_MODE no-ff, MERGE_MSG written" test "$(merge_heads 2>/dev/null)" = "$A1" -a "$(merge_mode 2>/dev/null)" = no-ff -a -s "$(git rev-parse --git-dir)/MERGE_MSG"
want "C4: only src/other.c is unmerged" test "$(unmerged)" = src/other.c
want "C4: .ait/D is a1's D (index and work tree)" test "$(idx ".ait/$D")" = "$VA" -a "$(wtb ".ait/$D")" = "$VA"
want "C4: specs/d.yaml is a1's c-A (index and work tree)" test "$(idx specs/d.yaml)" = "$SA1" -a "$(wtb specs/d.yaml)" = "$SA1"
# the receiving side resolves other.c and finishes with the hint's commit
rc=none; if merging; then echo x=M > src/other.c; git add src/other.c; tick; run git commit -q --allow-empty --no-edit --cleanup=strip; fi
want "C4 finish: the finishing commit succeeds" test "$rc" = 0
want "C4 finish: its parents are HEAD and a1" test "$(parents)" = 2 -a "$(git rev-parse HEAD^1)" = "$E1" -a "$(git rev-parse HEAD^2 2>/dev/null)" = "$A1"
want "C4 finish: D is a1's D, specs/d.yaml c-A, other.c resolved" test "$(recat HEAD D)" = "$VA" -a "$(at HEAD specs/d.yaml)" = "$SA1" -a "$(git show HEAD:src/other.c 2>/dev/null)" = x=M

# S2  S1 with K5 current: D and main's K5 share specs/shared.yaml (K5 made after a branched) ; a1 changes it
s0() { echo 'body: s0' > specs/shared.yaml; }
pbase s0
rec D spec=specs/shared.yaml >/dev/null; c "D declares shared" >/dev/null
git checkout -q -b a; echo 'body: s-A' > specs/shared.yaml; VA=$(rec D spec=specs/shared.yaml); A1=$(c "a: shared spec"); git checkout -q master
K5V=$(rec K5 current spec=specs/shared.yaml provider=src/order.c creator="$SE"); E1=$(c "e: K5")
M=$(mtree HEAD a); SHARED_A=$(at a specs/shared.yaml)
fact "S2: K5 is current and pins shared.yaml as s0" test "$(state_of "$K5V")" = current -a "$(pin_of "$K5V" specs/shared.yaml)" = "$(at HEAD specs/shared.yaml)"
fact "S2: merge-tree HEAD a1 has no conflict" test -z "$(mt HEAD a)"
fact "S2: merging changes .ait/D and specs/shared.yaml" test "$(changed HEAD "$M")" = ".ait/$D specs/shared.yaml"
fact "S2: merged shared.yaml (s-A) differs from K5's pin" test "$(git rev-parse "$M:specs/shared.yaml")" = "$SHARED_A" -a "$SHARED_A" != "$(pin_of "$K5V" specs/shared.yaml)"
want "S2: stops, nothing changed" pull_unchanged "$D"
want "S2: exit 1" test "$rc" = 1
want "S2: 'taking ... changes what contract K5 checked: specs/shared.yaml'" err_re "^error: taking $SCOPE changes what contract '$K5' checked: 'specs/shared.yaml'\$"
want "S2: hint: reopen K5 in the same request" err_line "hint: reopen it in the same request: 'ait contract pull $D future amend $K5 -m <why>'"
want "S2: hint: or --force" err_line "hint: or, if the change is trivial for '$K5': 'ait contract pull $D --force -m <why>'"
# follow the first hint (flows.py S1/S2: K5 re-pinned, validate passes)
pull "$D" future amend "$K5" -m why
want "S2 follow: succeeds" test "$rc" = 0
want "S2 follow: D is a1's D, shared.yaml s-A" test "$(recat HEAD D)" = "$VA" -a "$(at HEAD specs/shared.yaml)" = "$SHARED_A"
want "S2 follow: K5 is future, pins s-A, contains my K5" eval 'test "$(state_of "$(recat HEAD K5)")" = future -a "$(pin_of "$(recat HEAD K5)" specs/shared.yaml)" = "$SHARED_A" && contains "$(recat HEAD K5)" "$K5V"'
want "S2 follow: 'K5' is reopened by this pull" warns "'$K5' is reopened by this pull"

# S3  main's current K6 provides src/stock.c (pins g0) ; a1 fulfils D changing stock.c line 1
pbase
fact "S3: the previous fixture did not change the default creator" test "$(git cat-file -p "$(recat HEAD D)" | sed -n 's/^creator //p')" = 'agent-a S00000000000a'
git checkout -q -b a; line 1 A; VA=$(rec D current); A1=$(c "a: fulfil D"); git checkout -q master
cond K6 k6; K6V=$(rec K6 current provider=src/stock.c creator="$SE"); E1=$(c "e: K6")
M=$(mtree HEAD a); G0=$(at HEAD src/stock.c)
fact "S3: K6 is current and pins stock.c as g0" test "$(state_of "$K6V")" = current -a "$(pin_of "$K6V" src/stock.c)" = "$G0"
fact "S3: merge-tree HEAD a1 has no conflict" test -z "$(mt HEAD a)"
fact "S3: merging changes .ait/D and src/stock.c" test "$(changed HEAD "$M")" = ".ait/$D src/stock.c"
fact "S3: merged stock.c differs from K6's pin g0" test "$(git rev-parse "$M:src/stock.c")" != "$G0"
want "S3: stops, nothing changed" pull_unchanged "$D"
want "S3: exit 1" test "$rc" = 1
want "S3: 'taking ... changes what contract K6 checked: src/stock.c'" err_re "^error: taking $SCOPE changes what contract '$K6' checked: 'src/stock.c'\$"
want "S3: hint: reopen K6 in the same request" err_line "hint: reopen it in the same request: 'ait contract pull $D future amend $K6 -m <why>'"
want "S3: hint: or --force" err_line "hint: or, if the change is trivial for '$K6': 'ait contract pull $D --force -m <why>'"
# follow the first hint (flows.py S3: validate passes, nothing unverified)
pull "$D" future amend "$K6" -m why
want "S3 follow: succeeds" test "$rc" = 0
want "S3 follow: a commit with parents HEAD and a1" test "$(parents)" = 2 -a "$(git rev-parse HEAD^2 2>/dev/null)" = "$A1"
want "S3 follow: D is a1's D and not unverified" eval 'test "$(recat HEAD D)" = "$VA" && verified HEAD D'
want "S3 follow: K6 is future, on top of my K6" test "$(state_of "$(recat HEAD K6)")" = future -a -z "$(check_of "$(recat HEAD K6)")" -a "$(prevs "$(recat HEAD K6)")" = "$K6V"
want "S3 follow: 'K6' is reopened by this pull" warns "'$K6' is reopened by this pull"

# S4  S3, and main's current offer O6 undertakes K6
pbase
git checkout -q -b a; line 1 A; VA=$(rec D current); A1=$(c "a: fulfil D"); git checkout -q master
cond K6 k6; K6V=$(rec K6 current provider=src/stock.c creator="$SE")
echo o > src/o.c; cond O6 o0; O6V=$(rec O6 current provider=src/o.c creator="$SE"); link K6 O6 >/dev/null; E1=$(c "e: K6, O6")
M=$(mtree HEAD a); G0=$(at HEAD src/stock.c)
fact "S4: O6 is a current offer undertaking K6" test "$(state_of "$O6V")" = current -a "$(git show "HEAD:.ait/undertake/$K6-$O6" | sed -n 's/^state //p')" = undertaken
fact "S4: merge-tree HEAD a1 has no conflict" test -z "$(mt HEAD a)"
fact "S4: merged stock.c differs from K6's pin g0" test "$(git rev-parse "$M:src/stock.c")" != "$G0" -a "$(pin_of "$K6V" src/stock.c)" = "$G0"
want "S4: stops, nothing changed" pull_unchanged "$D"
want "S4: exit 1" test "$rc" = 1
want "S4: 'taking ... changes what contract K6 checked: src/stock.c'" err_re "^error: taking $SCOPE changes what contract '$K6' checked: 'src/stock.c'\$"
want "S4: the first hint's clauses are 'future amend K6 future amend O6' (5.9a: the demand, then the offers undertaking it)" first_hint_re "'ait contract pull $D future amend $K6 future amend $O6 -m <why>'\$"
want "S4: hint: or --force" hint_re "'ait contract pull $D --force -m <why>'\$"
pull "$D" future amend "$O6" future amend "$K6" -m why
want "S4 follow: succeeds" test "$rc" = 0
want "S4 follow: K6 and O6 are future, D is a1's D" test "$(state_of "$(recat HEAD K6)")" = future -a "$(state_of "$(recat HEAD O6)")" = future -a "$(recat HEAD D)" = "$VA"

# IND1  D current, undertaken by main's current offer O ; a1 re-checks D with line 1 ; main re-checked with line 9
ind1() { echo o > src/o.c; cond O o0; rec O current provider=src/o.c >/dev/null; link D O >/dev/null; }
DSTATE=current pbase ind1; unset DSTATE	# sh (bash 3.2) keeps DSTATE after a function call
git checkout -q -b a; line 1 A; VA=$(rec D current); A1=$(c "a: recheck"); git checkout -q master
line 9 E; VE=$(rec D current); E1=$(c "e: recheck")
D0=$(recat init D); M=$(mtree HEAD a)
fact "IND1: O is a current offer undertaking D" test "$(state_of "$(recat HEAD O)")" = current -a "$(git show "HEAD:.ait/undertake/$D-$O" | sed -n 's/^state //p')" = undertaken
fact "IND1: D diverged: a's and mine both on D0, neither contains the other, bodies differ" eval 'test "$(prevs "$VA")" = "$D0" -a "$(prevs "$VE")" = "$D0" && ! contains "$VA" "$VE" && ! contains "$VE" "$VA" && ! same_body "$VA" "$VE" && ! same_body "$VA" "$D0" && ! same_body "$VE" "$D0"'
fact "IND1: merge-tree HEAD a1 conflicts on .ait/D only" test "$(mt HEAD a)" = ".ait/$D"
fact "IND1: merged stock.c differs from a1's pin" test "$(git rev-parse "$M:src/stock.c")" != "$(pin_of "$VA" src/stock.c)"
want "IND1: 'pull D=a1' stops at 5, nothing changed" pull_unchanged "$D=$A1" -m why
want "IND1: exit 1, error:" eval 'test "$rc" = 1 && err_re "^error: "'
want "IND1: the first hint's clauses are 'future amend D future amend O' (5.9a)" first_hint_re "'ait contract pull $D=[^ ]+ future amend $D future amend $O -m <why>'\$"
want "IND1: hint: or --force (no code conflict)" hint_re "'ait contract pull $D=[^ ]+ --force -m <why>'\$"
pull "$D=$A1" future amend "$O" future amend "$D" -m why
want "IND1 follow: succeeds" test "$rc" = 0
want "IND1 follow: D and O are future" test "$(state_of "$(recat HEAD D)")" = future -a "$(state_of "$(recat HEAD O)")" = future
V1=$(compose "$VA" "reported $(pins_b "$VA")" "$VA" "$VE")	# the result version before the clauses
want "IND1 follow: D's prev is V1 and V1's prev {a1's D, mine} (1절, record 72)" eval 'test -n "$(recat HEAD D)" && test "$(prevs "$(recat HEAD D)" | tr "\n" " ")" = "$(printf "%s\n" "$V1" "$VA" "$VE" | sort | tr "\n" " ")"'

# CK4  U6 (D current ; a1 and b1 each change the code and re-check), then 'pull D=a1'
DSTATE=current pbase; unset DSTATE
git checkout -q -b a; line 1 A; VA=$(rec D current); A1=$(c "a: recheck"); git checkout -q master
git checkout -q -b b; line 1 B; VB=$(rec D current); B1=$(c "b: recheck"); git checkout -q master
D0=$(recat init D); NEW=$(compose "$VA" "reported $(pins_b "$VA")" "$VA" "$VB")
fact "CK4: latest are a's and b's D, both on D0, bodies differ" eval 'test "$(prevs "$VA")" = "$D0" -a "$(prevs "$VB")" = "$D0" && ! same_body "$VA" "$VB"'
fact "CK4: a1's check line is reported with a1's PINS" test "$(check_of "$VA")" = "reported $(pins_b "$VA")"
fact "CK4: a new version with a1's body, prev {a, b} has 'check reported' of a1's PINS" test "$(check_of "$NEW")" = "reported $(pins_b "$NEW")" -a "$(pins_b "$NEW")" = "$(pins_b "$VA")"
pull "$D=$A1" -m why
want "CK4: succeeds" test "$rc" = 0
want "CK4: takes a1 (parents HEAD and a1)" test "$(parents)" = 2 -a "$(git rev-parse HEAD^2 2>/dev/null)" = "$A1"
want "CK4: 'included D from ...'" out_re "^included $D from '.+'\$"
want "CK4: D is a1's body, check reported, prev {a, b}; not unverified" eval 'test "$(recat HEAD D)" = "$NEW" && verified HEAD D'

# CK5  D current, checked with --force ; a1: the same pins checked again with -r ; main: consumer only (forced)
DSTATE=current pbase; unset DSTATE
rec D current check=forced prev=- >/dev/null; git add -A; tick; git commit -q --no-verify --amend --no-edit; git tag -f init >/dev/null 2>&1
D0=$(recat init D)
git checkout -q -b a; VA=$(rec D current check=reported); A1=$(c "a: recheck -r"); git checkout -q master
echo 'order E' > src/order.c; VE=$(rec D current check=forced); E1=$(c "e: consumer")
NEW=$(compose "$VE" "reported $(pins_b "$VE")" "$VA" "$VE")
fact "CK5: init's D is current with 'check forced'" test "$(check_of "$D0")" = "forced $(pins_b "$D0")" -a "$(prevs "$D0")" = ""
fact "CK5: a1's body is D0's" same_body "$VA" "$D0"
fact "CK5: a1 and e1 have the same PINS" test "$(pins_b "$VA")" = "$(pins_b "$VE")" -a "$(check_of "$VA")" = "reported $(pins_b "$VA")" -a "$(check_of "$VE")" = "forced $(pins_b "$VE")"
fact "CK5: main moved forward (common ancestor D0 has a1's body, e1's differs)" eval 'test "$(prevs "$VA")" = "$D0" -a "$(prevs "$VE")" = "$D0" && same_body "$VA" "$D0" && ! same_body "$VE" "$D0"'
pull "$D"
want "CK5 main: succeeds" test "$rc" = 0
want "CK5 main: 'kept D'" out_line "kept $D"
want "CK5 main: a commit on top of HEAD, nothing taken" test "$(parents)" = 1 -a "$(git rev-parse HEAD~1 2>/dev/null)" = "$E1"
want "CK5 main: D is e1's body, 'check reported', prev {a1, e1}" test "$(recat HEAD D)" = "$NEW"
pull "$D"
want "CK5 main: pull again is 'already D'" out_line "already $D"
# a pulls from the same start (main at e1), as the Python script computes both sides
git checkout -q a; git branch -f master "$E1"
pull "$D"
want "CK5 a: succeeds" test "$rc" = 0
want "CK5 a: takes main's e1" test "$(parents)" = 2 -a "$(git rev-parse HEAD^2 2>/dev/null)" = "$E1"
want "CK5 a: 'included D from ...'" out_re "^included $D from '.+'\$"
want "CK5 a: the same version as main's, byte for byte" test "$(recat HEAD D)" = "$NEW"
pull "$D"
want "CK5 a: pull again is 'already D'" out_line "already $D"

# CK6  a1 fulfils D with -r, b1 fulfils D with --force, same code ; main: D0
pbase
git checkout -q -b a; line 1 A; VA=$(rec D current check=reported); A1=$(c "a: fulfil -r"); git checkout -q master
git checkout -q -b b; line 1 A; VB=$(rec D current check=forced); B1=$(c "b: fulfil --force"); git checkout -q master
D0=$(recat init D); NEW=$(compose "$VA" "reported $(pins_b "$VA")" "$VA" "$VB")
FIRST=$(printf '%s\n' "$A1" "$B1" | sort | head -1)
fact "CK6: init's D is future" test "$(state_of "$D0")" = future
fact "CK6: a's and b's D are different blobs with one body" eval 'test "$VA" != "$VB" && same_body "$VA" "$VB"'
fact "CK6: both on D0, neither contains the other" eval 'test "$(prevs "$VA")" = "$D0" -a "$(prevs "$VB")" = "$D0" && ! contains "$VA" "$VB" && ! contains "$VB" "$VA"'
fact "CK6: a1 is reported, b1 forced, same PINS" test "$(check_of "$VA")" = "reported $(pins_b "$VA")" -a "$(check_of "$VB")" = "forced $(pins_b "$VA")"
fact "CK6: a1 and b1 are one new commit each" test "$(git rev-list --count a ^HEAD)" = 1 -a "$(git rev-list --count b ^HEAD)" = 1
pull "$D"
want "CK6: does not stop" test "$rc" = 0
want "CK6: D is the body, 'check reported', prev {a, b}; not unverified" eval 'test "$(recat HEAD D)" = "$NEW" && verified HEAD D'
want "CK6: takes the end with the smaller commit OID (TIE, 1절)" test "$(parents)" = 2 -a "$(git rev-parse HEAD^2 2>/dev/null)" = "$FIRST"
want "CK6: 'included D from ...'" out_re "^included $D from '.+'\$"

# CK7  D current, reported ; a1: consumer only, check line written forced by hand (same PINS), prev mine
DSTATE=current pbase; unset DSTATE
git checkout -q -b a; echo 'order A' > src/order.c; V=$(rec D current check=forced); A1=$(c a1); git checkout -q master
D0=$(recat init D); NEW=$(compose "$V" "reported $(pins_b "$V")" "$V")
fact "CK7: the only latest is a1's D, and it contains mine" eval 'test "$(prevs "$V")" = "$D0" && ! same_body "$V" "$D0"'
fact "CK7: a1's check is forced with my PINS; mine is reported" test "$(check_of "$V")" = "forced $(pins_b "$D0")" -a "$(check_of "$D0")" = "reported $(pins_b "$D0")"
pull "$D"
want "CK7: succeeds" test "$rc" = 0
want "CK7: not a1's version as it is but a new one: a1's body, 'check reported', prev {a1's D}" test "$(recat HEAD D)" = "$NEW"
want "CK7: takes a1" test "$(parents)" = 2 -a "$(git rev-parse HEAD^2 2>/dev/null)" = "$A1" -a "$(git show HEAD:src/order.c 2>/dev/null)" = 'order A'
want "CK7: 'included D from ...'" out_re "^included $D from '.+'\$"

# S5  main's current K6 pins src/stock.c ; a1 changes D and moves src/stock.c to src/inv.c ; 'pull D'
pbase
git checkout -q -b a; cond D c-A; git mv src/stock.c src/inv.c; VA=$(rec D provider=src/inv.c); A1=$(c "a: D, move stock.c"); git checkout -q master
cond K6 k0; K6V=$(rec K6 current provider=src/stock.c consumer=src/order.c); c "main: K6 after a branched" >/dev/null
M=$(mtree HEAD a)
fact "S5: incoming parent has no K6 already unverified" test -z "$(recat "$A1" K6)"
fact "S5: K6 is current and provides src/stock.c" test "$(state_of "$K6V")" = current -a -n "$(pin_of "$K6V" src/stock.c)"
fact "S5: merge-tree HEAD a1 has no conflict" test -z "$(mt HEAD a)"
fact "S5: the merged tree has no stock.c and has inv.c" eval '! git rev-parse -q --verify "$M:src/stock.c" >/dev/null && git rev-parse -q --verify "$M:src/inv.c" >/dev/null'
fact "S5: HEAD has no inv.c" test -z "$(at HEAD src/inv.c)"
want "S5: stops at 5, nothing changed" pull_unchanged "$D"
want "S5: exit 1" test "$rc" = 1
want "S5: 'taking ... changes what contract K6 checked: src/stock.c'" err_re "^error: taking $SCOPE changes what contract '$K6' checked: 'src/stock.c'\$"
want "S5: the only hint is 'ait contract pull D future amend K6 -m <why>'" only_hint "'ait contract pull $D future amend $K6 -m <why>'\$"
pull "$D" future amend "$K6" -m why
want "S5 follow: succeeds" test "$rc" = 0
want "S5 follow: K6 is future, D is a1's D, the code moved" test "$(state_of "$(recat HEAD K6)")" = future -a "$(recat HEAD D)" = "$VA" -a -n "$(at HEAD src/inv.c)" -a -z "$(at HEAD src/stock.c)"

# RV3b  a1 fulfils D ; main fast-forwards to a1, then 'git revert a1' ; 'pull D'
pbase
git checkout -q -b a; line 1 A; VA=$(rec D current); A1=$(c "a: fulfil D"); git checkout -q master
git merge -q --ff-only a; git revert --no-commit HEAD; RV=$(c "Revert \"a: fulfil D\"")
D0=$(recat init D)
fact "RV3b: the only latest is a1's D (it contains HEAD's D)" eval 'test "$(recat a D)" = "$VA" -a "$(recat HEAD D)" = "$D0" && contains "$VA" "$D0" && ! contains "$D0" "$VA"'
fact "RV3b: a1 is an ancestor of HEAD" is_ancestor "$A1" HEAD
fact "RV3b: HEAD's stock.c differs from a1's pin" test "$(at HEAD src/stock.c)" != "$(pin_of "$VA" src/stock.c)"
# RV3b' (rev8.log): the revert commit went back to an old version, so it is not a reopen commit (1절 condition 1)
fact "RV3b': HEAD's D is init's D0, contained in its parent's D" eval 'test "$(recat HEAD D)" = "$D0" && contains "$(recat HEAD~1 D)" "$(recat HEAD D)"'
want "RV3b: stops at 5, nothing changed" pull_unchanged "$D"
want "RV3b: exit 1" test "$rc" = 1
want "RV3b: 'D at a1 checked code your branch no longer has: src/stock.c'" err_re "^error: '$D' at $SCOPE checked code your branch no longer has: 'src/stock.c'\$"
want "RV3b: hint 'pull D future amend D'" hint_re "'ait contract pull $D future amend $D -m <why>'\$"
want "RV3b: hint 'pull D --force'" hint_re "'ait contract pull $D --force -m <why>'\$"
pull "$D" future amend "$D" -m why
want "RV3b follow: succeeds" test "$rc" = 0
want "RV3b follow: a commit on top of HEAD (nothing to take)" test "$(parents)" = 1 -a "$(git rev-parse HEAD~1 2>/dev/null)" = "$RV"
want "RV3b follow: D is future and contains a1's D; the code stays" eval 'test "$(state_of "$(recat HEAD D)")" = future -a "$(at HEAD src/stock.c)" = "$(at "$RV" src/stock.c)" && contains "$(recat HEAD D)" "$VA"'

# NU1  a1 fulfils D ; a2 and a3: plain commits change D's code one after another
# Only notifications are expected here (decision 18: one unverified-event notice each for a2 and a3); they are
# left out for now, so this row has facts only.
pbase
git checkout -q -b a; line 1 A; rec D current >/dev/null; A1=$(c "a: fulfil"); line 2 X; A2=$(c "a: plain code change")
line 2 Y; A3=$(c "a: plain code change again"); git checkout -q master
fact "NU1: a1-a3 have the same .ait/D blob" test "$(recat "$A1" D)" = "$(recat "$A2" D)" -a "$(recat "$A2" D)" = "$(recat "$A3" D)"
fact "NU1: stock.c differs in a1, a2 and a3" test "$(at "$A1" src/stock.c)" != "$(at "$A2" src/stock.c)" -a "$(at "$A2" src/stock.c)" != "$(at "$A3" src/stock.c)" -a "$(at "$A1" src/stock.c)" != "$(at "$A3" src/stock.c)"
fact "NU1: D is current in a1 and verified there, unverified in a2 and a3" eval 'test "$(state_of "$(recat "$A1" D)")" = current && verified "$A1" D && ! verified "$A2" D && ! verified "$A3" D'

# NU2  NU1, and a4 changes Q ; main changes stock.c line 9 ; 'pull D Q' (run by D's creator agent-a)
q0() { cond Q q0; rec Q >/dev/null; }
pbase q0
git checkout -q -b a; line 1 A; VA=$(rec D current); A1=$(c "a: fulfil"); line 2 X; c "a: code" >/dev/null
line 2 Y; c "a: code again" >/dev/null; cond Q q-A; QA=$(rec Q); A4=$(c "a: Q"); git checkout -q master
line 9 E; E1=$(c "e: code")
M=$(mtree HEAD a); PIN=$(pin_of "$VA" src/stock.c)
fact "NU2: a4's D is a1's (current), already unverified in a4" eval 'test "$(recat a D)" = "$VA" -a "$(state_of "$VA")" = current && ! verified a D'
fact "NU2: merge-tree HEAD a4 has no conflict" test -z "$(mt HEAD a)"
fact "NU2: merged stock.c differs from a4's and from the pin" test "$(git rev-parse "$M:src/stock.c")" != "$(at a src/stock.c)" -a "$(git rev-parse "$M:src/stock.c")" != "$PIN"
fact "NU2: D is future on main (not unverified in the old HEAD)" test "$(state_of "$(recat HEAD D)")" = future
# the warning is for the session whose demand D is (2절 5, v14 7.12.1-7): main is worked on by D's creator agent-a
ait session close "$MSID" && ASID=$(ait session open --agent agent-a)
fact "NU2: this worktree's session is agent-a's" test -n "$ASID"
pull "$D" "$Q"
want "NU2: not stopped" test "$rc" = 0
want "NU2: warning: 'D' comes in unverified" err_line "warning: '$D' comes in unverified"
want "NU2: takes a4 (parents HEAD and a4)" test "$(parents)" = 2 -a "$(git rev-parse HEAD^2 2>/dev/null)" = "$A4"
want "NU2: D is a1's, Q is a4's, stock.c the merged one" test "$(recat HEAD D)" = "$VA" -a "$(recat HEAD Q)" = "$QA" -a "$(at HEAD src/stock.c)" = "$(git rev-parse "$M:src/stock.c")"
want "NU2: 'included D from ...'" out_re "^included $D from '.+'\$"
want "NU2: 'included Q from ...'" out_re "^included $Q from '.+'\$"
done_testing
