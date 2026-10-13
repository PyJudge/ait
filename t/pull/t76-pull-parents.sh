#!/bin/sh
# Actual parents govern verification and request identity, independent of sources.
. "$(dirname "$0")/lib.sh"

pbase
git checkout -q -b a
rec D current >/dev/null
A1=$(c current-D)
line 1 already-unverified; cond Q q-A; rec Q >/dev/null
A2=$(c already-unverified-and-Q)
git checkout -q -b b init
cond K k-B; rec K >/dev/null; B1=$(c new-K)
git checkout -q master
H0=$(git rev-parse HEAD)
pull "$D" "$Q"
want "parents: a single end preserves existing unverified" test "$rc" = 0
want "parents: it comes in to no one but whose demand D is (9.3)" eval '! grep -q "comes in unverified" "$T/err"'
git reset --hard -q "$H0"
# the warning is for the session whose demand D is, as in t69 NU2 (7.12, 9.3)
ait session close "$MSID" && ASID=$(ait session open --agent agent-a)
pull "$K" "$D" "$Q"
want "parents: unrelated extra end preserves existing unverified" test "$rc" = 0
want "parents: existing unverified is warned" eval 'grep -q "comes in unverified" "$T/err"'
want "parents: reused contracts create no request" eval '! grep -q "^request " "$T/out" && ! git log -1 --format=%B | grep -q "^Ait-Request:"'
want "parents: default message names direct IDs in supplied order" test "$(git log -1 --format=%s)" = "[CONTRACT] pull $K $D $Q"

shared_current() { cond K6 k0; rec K6 current >/dev/null; }
pbase shared_current
git checkout -q -b a
cond D incoming; git mv src/stock.c src/inv.c; rec D provider=src/inv.c >/dev/null
A1=$(c already-unverified-missing-provider)
git checkout -q master
fact "parents: incoming K6 has the same pins as HEAD" test "$(recat "$A1" K6)" = "$(recat HEAD K6)"
fact "parents: provider exists only in the verified HEAD parent" eval 'test -n "$(at HEAD src/stock.c)" && test -z "$(at "$A1" src/stock.c)"'
pull "$D"
want "parents: inherited missing provider is already unverified, not new" test "$rc" = 0
want "parents: existing missing-provider record stays unchanged" test "$(recat HEAD K6)" = "$(recat "$A1" K6)"

pbase
git checkout -q -b a
cond Q q-A; rec Q >/dev/null; A1=$(c incoming-Q)
git checkout -q master
echo local > src/local.c; H0=$(c local-code)
ait session close "$MSID" >/dev/null
pull "$Q"
want "request: clean parent reuse works without session" test "$rc" = 0
want "request: reused record is byte-identical to incoming parent" test "$(recat HEAD Q)" = "$(recat "$A1" Q)"
want "request: no request provenance is invented" eval '! grep -q "^request " "$T/out" && ! git log -1 --format=%B | grep -q "^Ait-"'
want "request: default message identifies the contract" test "$(git log -1 --format=%s)" = "[CONTRACT] pull $Q"

pbase
git checkout -q -b a
cond D incoming; rec D >/dev/null; line 9 A; A1=$(c incoming)
git checkout -q master
line 9 E; c independent >/dev/null
OTHER=$(ait session open --agent other)
run env AIT_SESSION="$OTHER" ait contract pull "$D"
want "resolution: nondefault session is in the Git finishing hint" eval 'test "$rc" = 1 && grep -q "AIT_SESSION=$OTHER git commit --allow-empty --no-edit --cleanup=strip" "$T/err"'
# A new definition makes the finishing commit a request; use the given session.
printf 'body: resolved\n' > specs/d.yaml
git show HEAD:src/stock.c > src/stock.c; git add src/stock.c specs/d.yaml
run env AIT_SESSION="$OTHER" git commit --allow-empty --no-edit --cleanup=strip
want "resolution: final request records the instructed resolver" eval 'test "$rc" = 0 && test "$(git log -1 --format="%(trailers:key=Ait-Session,valueonly)" | sed "/^$/d")" = "$OTHER"'

done_testing
