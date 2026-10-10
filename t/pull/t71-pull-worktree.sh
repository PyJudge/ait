#!/bin/sh
# pull criteria 3.12 작업 폴더와 shallow clone (WT1, WT2, NEW2, IDX1, HD1, SH1)
. "$(dirname "$0")/lib.sh"

# git without the ait hooks, as an agent's plain git command (c uses --no-verify the same way)
nohook() { git -c core.hooksPath=/dev/null "$@"; }
# re TEMPLATE: an ERE for the text; <c> stands for a commit name ('a1' in the criteria), <p> for one in parentheses
re() { printf '%s' "$1" | sed -e 's/[][\.*^$(){}?+|]/\\&/g' -e "s/<c>/[^']+/g" -e 's/<p>/[^)]+/g'; }
# stops RC TEMPLATE: exit code RC, and the first stderr line that is not a warning starts with TEMPLATE
stops() { [ "$rc" = "$1" ] && grep -v '^warning: ' "$T/err" | head -1 | grep -qE -- "^$(re "$2")"; }
# a line of git's merge output, on either stream
said() { grep -qxF -- "$1" "$T/out" "$T/err"; }
oids() { printf '%s\n' "$@" | sort -u | tr '\n' ' '; }
pinned() { git cat-file -p "$(recat "$1" "$2")" | awk -v p="$3" '$1 == "file" && $2 == p { print $4 }'; }
no_git_hint() { ! grep '^hint:' "$T/err" | grep -qF "'git "; }

# WT1  a1 changes D's provider only ; main changed D's condition (c-E) and has an uncommitted draft in specs/d.yaml ;
#      'pull D=a1 -m <why>'
pbase
git checkout -q -b a; line 3 A; DA=$(rec D); A1=$(c a1); git checkout -q master
cond D c-E; DE=$(rec D); E1=$(c e1); echo 'body: draft' > specs/d.yaml
D0=$(recat init D)
fact "WT1: D@A and D@E both contain D0, neither the other (D diverged)" test -n "$(contains "$DA" "$D0" && contains "$DE" "$D0" && echo y)" -a -z "$(contains "$DA" "$DE" && echo y)$(contains "$DE" "$DA" && echo y)"
fact "WT1: a1 changes only .ait/D and src/stock.c" test "$(git diff --name-only init "$A1" | tr '\n' ' ')" = ".ait/$D src/stock.c "
fact "WT1: only .ait/D conflicts in git's merge" test "$(mt HEAD "$A1")" = ".ait/$D"
fact "WT1: specs/d.yaml is pinned by HEAD's D (a contract path)" test "$(pinned HEAD D specs/d.yaml)" = "$(at HEAD specs/d.yaml)"
fact "WT1: D@A pins specs/d.yaml otherwise (pull has to write it)" test "$(pinned "$A1" D specs/d.yaml)" != "$(at HEAD specs/d.yaml)"
before=$(snapshot); run nohook merge --no-commit --no-ff "$A1"
fact "WT1: git merge starts: .ait/D in conflict, the draft left alone" test "$rc" = 1 -a "$(unmerged)" = ".ait/$D" -a "$(cat specs/d.yaml)" = "body: draft"
nohook merge --abort
fact "WT1: after git merge --abort, the same state with the draft" test "$(snapshot)" = "$before" -a "$(cat specs/d.yaml)" = "body: draft"
want "WT1: pull D=a1 stops before changing anything" pull_unchanged "$D=$A1" -m why
want "WT1: the local-changes error" test "$rc" = 1 -a "$(first_err)" = "error: your local changes to 'specs/d.yaml' would be overwritten by this pull"
want "WT1: hint: commit it with ait or set it aside (one line, decision 31)" err_line "hint: commit your changes with 'ait contract add specs/d.yaml' and 'ait contract commit -m <why>', or set them aside, before you pull"
want "WT1: the draft is still there" test "$(cat specs/d.yaml)" = "body: draft"

# WT2  main took b1 (D@B) ; b reverts b1 and changes Q and x.c (b3) ; a1: Q2 ; main: x.c differently,
#      an uncommitted draft in specs/d.yaml ; 'pull Q Q2'
wt2() { cond Q q0; rec Q >/dev/null; cond Q2 p0; rec Q2 >/dev/null; echo x=0 > src/x.c; }
pbase wt2
git checkout -q -b b; cond D c-B; DB=$(rec D); B1=$(c "b: D@B"); git checkout -q master; nohook merge -q --ff-only b
git checkout -q b; tick; nohook revert --no-edit "$B1" >/dev/null; cond Q q-B; QB=$(rec Q); echo x=B > src/x.c; B3=$(c "b: Q, x.c")
git checkout -q master; git checkout -q -b a; cond Q2 p-A; Q2A=$(rec Q2); A1=$(c "a: Q2")
git checkout -q master; echo x=E > src/x.c; E1=$(c "e: x.c"); echo 'body: my draft' > specs/d.yaml
D0=$(recat init D)
fact "WT2: D has nothing to take: b3's D is D0, contained in my D@B ; a1's is mine" test "$(recat "$B3" D)" = "$D0" -a "$(recat "$A1" D)" = "$DB" -a -n "$(contains "$DB" "$D0" && echo y)"
fact "WT2: a1 conflicts nowhere ; b3 conflicts in src/x.c" test -z "$(mt HEAD "$A1")" -a "$(mt HEAD "$B3")" = src/x.c
M1=$(git commit-tree "$(git merge-tree --write-tree HEAD "$A1")" -p HEAD -p "$A1" -m tmp)
fact "WT2: after a1, b3 conflicts in src/x.c last" test "$(mt "$M1" "$B3")" = src/x.c
# the received result: git's merge with .ait/D and specs/d.yaml as mine (b3's D is an old version)
git merge-tree --write-tree "$M1" "$B3" > "$T/mt"
GIT_INDEX_FILE=$T/idx git read-tree "$(head -1 "$T/mt")"
for p in ".ait/$D" specs/d.yaml; do GIT_INDEX_FILE=$T/idx git update-index --add --cacheinfo "100644,$(at HEAD "$p"),$p"; done
R=$(GIT_INDEX_FILE=$T/idx git write-tree); rm -f "$T/idx"
fact "WT2: the paths pull writes leave out specs/d.yaml" test "$(git diff-tree -r --name-only HEAD "$R" | tr '\n' ' ')" = "$(printf '%s\n' ".ait/$Q" ".ait/$Q2" specs/q.yaml specs/q2.yaml src/x.c | sort | tr '\n' ' ')"
git worktree add -q --detach "$T/wt" HEAD
fact "WT2: writing only those paths (read-tree -m -u) keeps the draft" sh -c "cd '$T/wt' && echo 'body: my draft' > specs/d.yaml && git read-tree -m -u HEAD $R && test \"\$(cat specs/d.yaml)\" = 'body: my draft'"
git worktree remove --force "$T/wt"
pull "$Q" "$Q2"
want "WT2: does not stop at 0: stops like git on the code conflict" test "$rc" = 1
want "WT2: 'CONFLICT (content): Merge conflict in src/x.c'" said "CONFLICT (content): Merge conflict in src/x.c"
want "WT2: 'Automatic merge failed; ...'" said "Automatic merge failed; fix conflicts and then commit the result."
want "WT2: the finishing hint" grep -qE "^hint: commit the resolution first: '(AIT_SESSION=[^ ]+ )?git commit --allow-empty --no-edit --cleanup=strip', then continue as git says\$" "$T/err"
want "WT2: HEAD stays" test "$(git rev-parse HEAD)" = "$E1"
want "WT2: MERGE_HEAD lists a1 and b3" test "$(merging && oids $(cat .git/MERGE_HEAD))" = "$(oids "$A1" "$B3")"
want "WT2: MERGE_MODE no-ff" test "$(cat .git/MERGE_MODE 2>/dev/null)" = no-ff
want "WT2: MERGE_MSG is written" test -s .git/MERGE_MSG
want "WT2: src/x.c is unmerged" test "$(unmerged)" = src/x.c
want "WT2: Q is b3's, Q2 is a1's, D is mine (index)" test "$(git rev-parse -q --verify ":.ait/$Q"):$(git rev-parse -q --verify ":.ait/$Q2"):$(git rev-parse -q --verify ":.ait/$D")" = "$QB:$Q2A:$DB"
want "WT2: ... and the work tree" test "$(git hash-object ".ait/$Q"):$(git hash-object ".ait/$Q2"):$(git hash-object ".ait/$D")" = "$QB:$Q2A:$DB"
want "WT2: specs/q.yaml and specs/q2.yaml are b3's and a1's" test "$(git rev-parse -q --verify :specs/q.yaml):$(git rev-parse -q --verify :specs/q2.yaml)" = "$(at "$B3" specs/q.yaml):$(at "$A1" specs/q2.yaml)"
want "WT2: the draft in specs/d.yaml is kept" test "$(cat specs/d.yaml)" = "body: my draft" -a "$(git rev-parse -q --verify :specs/d.yaml)" = "$(at HEAD specs/d.yaml)"

# NEW2  a1 makes Q (specs/q.yaml) ; main has an untracked specs/q.yaml
pbase
git checkout -q -b a; cond Q q; QN=$(rec Q); A1=$(c a1); git checkout -q master; echo 'my data' > specs/q.yaml
fact "NEW2: specs/q.yaml is untracked on main and tracked in a1" test "$(git status --porcelain specs/q.yaml)" = "?? specs/q.yaml" -a -n "$(at "$A1" specs/q.yaml)"
before=$(snapshot); run nohook merge --ff-only "$A1"
fact "NEW2: git refuses the merge" test "$rc" != 0 -a "$(snapshot)" = "$before" -a -n "$(grep 'untracked working tree files would be overwritten' "$T/err")"
want "NEW2: pull Q stops before changing anything" pull_unchanged "$Q"
want "NEW2: an error" stops 1 "error: "
want "NEW2: hint: move the untracked files" err_line "hint: move or remove 'specs/q.yaml' before you pull"
want "NEW2: the untracked file is still there" test "$(cat specs/q.yaml)" = "my data"

# IDX1  main has its own commit and a staged new file src/util.c ; 'pull D' (not a fast-forward)
pbase
git checkout -q -b a; cond D c-A; DA=$(rec D); A1=$(c a1); git checkout -q master
echo m > src/m.c; E1=$(c "e: code"); echo u > src/util.c; git add src/util.c
fact "IDX1: not a fast-forward" not is_ancestor HEAD "$A1"
fact "IDX1: src/util.c is staged" test "$(git diff --cached --name-only)" = src/util.c
before=$(snapshot); run nohook merge --no-commit --no-ff "$A1"
fact "IDX1: git refuses the merge" test "$rc" != 0 -a "$(snapshot)" = "$before" -a -z "$(merging && echo y)"
want "IDX1: pull D stops, nothing changed" pull_unchanged "$D"
want "IDX1: E_DIRTY (index)" test "$rc" = 1 -a "$(first_err)" = "error: uncommitted changes in the index"
want "IDX1: E_DIRTY's hint for the index (v14 5.8)" err_line "hint: first commit or set aside your changes, then rerun this command"

# HD1  main edits its own D's pinned spec in a plain commit (no re-pin) ; then 'pull Q'
pbase
echo 'body: edited by hand' > specs/d.yaml; E1=$(c "e1: plain edit of a pinned spec")
fact "HD1: HEAD's D pins specs/d.yaml otherwise than HEAD has it" test "$(pinned HEAD D specs/d.yaml)" = "$(at init specs/d.yaml)" -a "$(at HEAD specs/d.yaml)" != "$(at init specs/d.yaml)"
want "HD1: pull Q stops at 0, nothing changed" pull_unchanged "$Q"
want "HD1: an error (HEAD's error, 5.10)" stops 1 "error: "
want "HD1: main's edit of specs/d.yaml is kept" test "$(cat specs/d.yaml)" = "body: edited by hand" -a "$(at HEAD specs/d.yaml)" = "$(git hash-object specs/d.yaml)"

# SH1  a: D c-A (a1) then a code-only commit (a2) ; main is a depth=1 clone of a
pbase
git checkout -q -b a; cond D c-A; DA=$(rec D); A1=$(c "a: D"); echo z > src/z.c; A2=$(c "a: code"); git checkout -q master
NA=$(git rev-list --count a)
git clone -q --depth=1 --no-single-branch "file://$T/r" "$T/sh"; cd "$T/sh"
tick; ait init >/dev/null; ait session open --agent agent-m >/dev/null
fact "SH1: the clone sees one commit of a (a has more)" test "$(git rev-list --count origin/a)" = 1 -a "$NA" -gt 1
fact "SH1: a2 is a shallow boundary with a's D, so the clone sees a2 bring D" test -n "$(grep -x "$A2" .git/shallow)" -a "$(git rev-list --parents -n1 origin/a)" = "$A2" -a "$(recat origin/a D)" = "$DA"
fact "SH1: the real introducing commit of a's D is a1" test "$(git -C "$T/r" rev-parse -q --verify "$A1:.ait/$D")" = "$DA" -a "$(git -C "$T/r" rev-parse -q --verify "$A1^:.ait/$D")" != "$DA"
run git merge-tree --write-tree HEAD origin/a
fact "SH1: merge-tree refuses: unrelated histories" test "$rc" = 128 -a -n "$(grep 'refusing to merge unrelated histories' "$T/err")"
want "SH1: pull D stops, nothing changed" pull_unchanged "$D"
want "SH1: an error with the state hint" test -n "$(stops 1 "error: " && grep "^hint: '[^']*' can be taken once this clone has its history down to the merge base with HEAD$" "$T/err")"
want "SH1: no git command in the hints" no_git_hint
done_testing
