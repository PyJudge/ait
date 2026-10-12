#!/bin/sh
# Production version 2: native confirmations, range contacts and pull option validation.
. "$(dirname "$0")/lib.sh"
pins_b() { git cat-file -p "$1" | grep -E '^(provider|file) ' | git hash-object --stdin; }
pinned() { git cat-file -p "$(recat "$1" "$2")" | awk -v p="$3" '$1 == "file" && $2 == p { print $4 }'; }

pbase
want "version 2: init keeps the format" eval 'run ait init; test "$rc" = 0 && test "$(git show HEAD:.ait/init)" = "version 2"'
tick; run ait fulfill "$D=src/stock.c:3-5" --force -m 'check the range'
want "version 2: fulfill accepts a line range" test "$rc" = 0
want "version 2: confirmation lives in the record" eval 'git show "HEAD:.ait/$D" | grep -q "^check forced " && ! git log -1 --format=%B | grep -q "^Ait-Check:"'
want "version 2: provider keeps its range" eval 'git show "HEAD:.ait/$D" | grep -q "^provider src/stock.c:3-5 "'
OLD=$(git rev-parse HEAD)
{ echo inserted; cat src/stock.c; } > "$T/shift"; cat "$T/shift" > src/stock.c
c 'outside the checked range' >/dev/null
run ait todo "$D"
want "range: insertion before it maps both endpoints" eval 'test "$rc" = 0 && grep -qx "provider src/stock.c:4-6" "$T/out"'
want "range: an outside edit keeps it verified" eval '! grep -q "unverified yes" "$T/out"'
line 5 inside
git add src/stock.c
run ait todo "$D"
want "range: an inside edit is unverified" eval 'test "$rc" = 0 && grep -q "unverified yes" "$T/out"'
tick; run ait commit "$D=src/stock.c:4-6" --force -m 'check the moved range'
want "range: confirmation maps and re-pins the range" eval 'test "$rc" = 0 && git show "HEAD:.ait/$D" | grep -q "^provider src/stock.c:4-6 "'
want "range: the new confirmation matches its pins" eval 'r=$(recat HEAD D); test "$(git cat-file -p "$r" | sed -n "s/^check forced //p")" = "$(pins_b "$r")"'
pull "$D" amend "$D" -m why
want "pull helper: an empty current amend requests confirmation" eval 'test "$rc" = 1 && grep -q "^error: check results required:" "$T/err"'
want "pull helper: the hint uses force on this pull" eval 'grep -q "ait contract pull $D amend $D --force -m <why>" "$T/err" && ! grep -q "rerun with -r" "$T/err"'
pull "$D" amend "$D" --force -m why
want "pull helper: forced unchanged confirmation is already" eval 'test "$rc" = 0 && grep -qx "already $D" "$T/out"'
pull "$D" -m why -F absent
want "pull options: m and F cannot be mixed" eval 'test "$rc" = 129 && grep -q "cannot be used together" "$T/err"'
pull "$D" -m '   '
want "pull options: an empty message is rejected" test "$rc" = 129
pull "$D" -m '# ------------------------ >8 ------------------------'
want "pull options: scissors cannot hide the trailers" eval 'test "$rc" = 129 && grep -q "hides the trailers" "$T/err"'
pull "$D" amend --force "$D" -m why
want "pull options: an option cannot split a helper clause" test "$rc" = 129
want "production: the original fulfillment remains in history" is_ancestor "$OLD" HEAD

q0() { cond Q q0; rec Q >/dev/null; }
pbase q0
tick; ait fulfill "$D=src/stock.c:3-5" --force -m 'check the range' >/dev/null
git checkout -q -b a; line 9 A; cond Q qA; rec Q >/dev/null; A1=$(c 'outside conflict'); git checkout -q master
line 9 E; cond Q qE; rec Q >/dev/null; H0=$(c 'my outside edit')
pull "$Q=$A1" -m why
want "range pull: a code conflict outside the checked range reaches Git" eval 'test "$rc" = 1 && test -s "$(git rev-parse --git-path MERGE_HEAD)" && grep -q "CONFLICT (content): Merge conflict in src/stock.c" "$T/out"'
want "range pull: the current confirmation stays unchanged" eval 'test "$(recat HEAD D)" = "$(git rev-parse :0:.ait/$D)" && test "$(git rev-parse HEAD)" = "$H0"'
git -c core.hooksPath=/dev/null merge --abort
pull "$Q=$A1" --force -m why
want "range pull: force cannot hide a code conflict" eval 'test "$rc" = 1 && grep -q "^error: --force cannot take a pull whose code conflicts:" "$T/err" && ! test -f "$(git rev-parse --git-path MERGE_HEAD)"'
pull "$Q=$A1" amend "$D" -m why
want "range pull with a clause to check: no --force hint, as --force takes no code conflict" eval 'test "$rc" = 1 && grep -q "^error: check results required" "$T/err" && ! grep -q -- "--force" "$T/err"'

pull "$Q=$A1" -m why
git show HEAD:src/stock.c > src/stock.c; git add src/stock.c
cat >> "$(git rev-parse --git-path MERGE_MSG)" <<'TRAILERS'

---
resolution notes

Ait-Request: R000000000001
Ait-Request: R000000000002
Ait-Agent: wrong
Ait-Session: S000000000001
Co-Authored-By: fixture <fixture@example.test>
TRAILERS
run git commit --allow-empty --no-edit --cleanup=strip
want "finishing hook: the resolution commits" test "$rc" = 0
want "finishing hook: the actual resolver supplies one request" eval 'test "$(git log -1 --format="%(trailers:key=Ait-Request,valueonly)" | grep -c "^R")" = 1 && test "$(git log -1 --format="%(trailers:key=Ait-Agent,valueonly)" | sed "/^$/d")" = agent-m && test "$(git log -1 --format="%(trailers:key=Ait-Session,valueonly)" | sed "/^$/d")" = "$MSID"'
want "finishing hook: other trailers survive" eval 'git log -1 --format="%(trailers:key=Co-Authored-By,valueonly)" | grep -qx "fixture <fixture@example.test>"'

pbase
BEFORE=$(recat HEAD D)
git checkout -q -b a; cond D incoming; A1=$(c 'change the definition without its pins'); git checkout -q master
line 9 mine; H0=$(c 'my independent code')
run git merge --no-ff --no-edit a
want "automatic merge: validation rejects unmatched spec pins" eval 'test "$rc" != 0 && grep -q "^error: spec files do not match the contract:" "$T/err" && test "$(git rev-parse HEAD)" = "$H0"'
want "automatic merge: the hint finishes the normalized resolution" eval 'grep -q "fix it in this merge and run.*git commit --allow-empty --no-edit --cleanup=strip" "$T/err"'
want "automatic merge: its hook only validates the record" test "$(git rev-parse ":0:.ait/$D")" = "$BEFORE"
ait session close "$MSID" >/dev/null
run git commit --allow-empty --no-edit --cleanup=strip
want "finishing hook: session absence does not block normalization" test "$rc" = 0
want "finishing hook: absent resolver has unknown provenance" eval 'test "$(git log -1 --format="%(trailers:key=Ait-Agent,valueonly)" | sed "/^$/d")" = unknown && test "$(git log -1 --format="%(trailers:key=Ait-Session,valueonly)" | sed "/^$/d")" = -'
want "finishing hook: the new record pins the resolved definition" test "$(pinned HEAD D specs/d.yaml)" = "$(at HEAD specs/d.yaml)"

o0() { echo o > src/o.c; cond O o0; rec O future provider=src/o.c >/dev/null; }
pbase o0
git checkout -q -b a; cond D incoming; rec D >/dev/null; LK=$(link D O); A1=$(c 'introduce D and its link')
rm ".ait/undertake/$D-$O"; A2=$(c 'hide the link at the branch tip'); git checkout -q master
fact "hidden link: the introducing end has the link and the branch tip does not" eval 'test "$(at "$A1" ".ait/undertake/$D-$O")" = "$LK" && test -z "$(at "$A2" ".ait/undertake/$D-$O")"'
pull "$D" "$O"
want "hidden link: receiving the introducing end succeeds" test "$rc" = 0
want "hidden link: closure includes links on actual incoming ends" test "$(at HEAD ".ait/undertake/$D-$O")" = "$LK"

done_testing
