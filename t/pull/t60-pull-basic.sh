#!/bin/sh
# pull criteria 3.1 기본 (A1, A2, A3)
. "$(dirname "$0")/lib.sh"

# A1  a1: a new contract Q ; main has no Q
pbase
git checkout -q -b a; cond Q q-A; QA=$(rec Q); A1=$(c "a1: new Q"); git checkout -q master
fact "A1: Q exists only on a" test -z "$(recat HEAD Q)" -a "$(recat a Q)" = "$QA"
fact "A1: taking a1 changes only .ait/Q and specs/q.yaml" test "$(git diff --name-only HEAD a | tr '\n' ' ')" = ".ait/$Q specs/q.yaml "
fact "A1: a1 is the only new commit" test "$(git rev-list --count a ^HEAD)" = 1
pull "$Q"
want "A1: pull Q succeeds" test "$rc" = 0
want "A1: fast-forward to a1" test "$(git rev-parse HEAD)" = "$A1"
want "A1: 'included Q from ...'" grep -q "^included $Q from '" "$T/out"

# A2  again
pull "$Q"
want "A2: already Q" out_line "already $Q"

# A3  a1 changes D and Q ; b1 changes Q differently ; 'pull D Q'
q0() { cond Q q0; rec Q >/dev/null; }
pbase q0
git checkout -q -b a; cond D c-A; rec D >/dev/null; cond Q q-A; QA=$(rec Q); A1=$(c a1)
git checkout -q -b b init; cond Q q-B; QB=$(rec Q); B1=$(c b1); git checkout -q master
Q0=$(recat init Q)
fact "A3: both a and b changed Q since init" test "$QA" != "$Q0" -a "$QB" != "$Q0"
fact "A3: a's Q and b's Q have different bodies" not same_body "$QA" "$QB"
fact "A3: both contain init's Q" contains "$QA" "$Q0"
want "A3: stops, nothing changed" pull_unchanged "$D" "$Q"
want "A3: diverged error on Q" stops_with 1 "error: '$Q' has diverged between "
want "A3: hint offers Q=a1 and Q=b1 with D kept" grep -q "ait contract pull $D $Q=.* -m <why>" "$T/err"
# A4  D is already current at every ref; the helper still reopens it
DSTATE=current pbase
d0=$(recat HEAD D); h0=$(git rev-parse HEAD)
fact "A4: HEAD has the only version of D, current" test "$(state_of "$d0")" = current -a "$(git for-each-ref --format='%(refname)' refs/heads | wc -l | tr -d ' ')" = 1
pull "$D" future amend "$D" -m why
want "A4: pull with a helper succeeds" test "$rc" = 0
want "A4: the helper creates a commit instead of already" test "$(git rev-parse HEAD)" != "$h0"
want "A4: D is future" test "$(state_of "$(recat HEAD D)")" = future
want "A4: D includes its former current version" contains "$(recat HEAD D)" "$d0"
want "A4: a request is printed" grep -q '^request ' "$T/out"
# HC1  a current empty amend still asks for a check (decision 36)
DSTATE=current pbase
d0=$(recat HEAD D); h0=$(git rev-parse HEAD)
fact "HC1: D is current and its check is reported" test "$(state_of "$d0")" = current -a "$(git cat-file -p "$d0" | sed -n 's/^check //p' | cut -d ' ' -f 1)" = reported
want "HC1: the check request leaves the repository unchanged" pull_unchanged "$D" amend "$D" -m why
want "HC1: a current empty amend requires a check" test "$rc" = 1 -a "$(head -1 "$T/err" | cut -c 1-29)" = 'error: check results required:'
want "HC1: a task and check files are printed" test -n "$(grep "^task $D:specs/d.yaml prompt$" "$T/out")" -a -n "$(grep '^check-request /' "$T/out")" -a -n "$(grep '^check-result /' "$T/out")"
want "HC1: the hint adds force" grep -qF "ait contract pull $D amend $D --force -m <why>" "$T/err"
want "HC1: no unusable -r hint" not grep -q -- '-r ' "$T/err"
pull "$D" amend "$D" --force -m why
want "HC1: force confirms unchanged reported pins without a commit" test "$rc" = 0 -a "$(git rev-parse HEAD)" = "$h0" -a "$(recat HEAD D)" = "$d0"
want "HC1: after the check there is nothing to write" out_line "already $D"
done_testing
