#!/bin/sh
# spec 5.13: the first condition that blocks is the error, in the order grammar, session, 5.2, clause, validate, code, results
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent a)
prompt_defs
git add -A && tick && git commit -q --no-verify -m defs
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out"; O=$(new_id)
H=$(git rev-parse HEAD)

# 1 before 2: grammar and the shapes of paths come before the session
for c in "offer src/stock.c specs/p.yaml extra" "offer /bad specs/p.yaml" "offer src/stock.c specs/p.yaml as Bad" \
	"future offer src/stock.c specs/p.yaml as x future offer src/order.c specs/p.yaml as x" "contract"; do
	run env AIT_SESSION=S000000000000 ait $c -m x
	check "'$c' is a usage or scope error before the session" sh -c "[ $rc = 129 ] || grep -q '^fatal: bad scope' '$T/err'"
done
run env AIT_SESSION=S000000000000 ait fulfill $O=/bad -m x
check "a fulfill scope's shape before the session" err_has "fatal: bad scope: '/bad'"

# 1 before 3: before the state of the repository
mkdir -p .ait && echo x > .ait/junk
run ait fulfill $O=a//b -m x
check "a fulfill scope's shape before uncommitted .ait/" err_has "fatal: bad scope: 'a//b'"
rm .ait/junk

# 4 before 6: a malformed record in HEAD does not hide the clause's own error
git rm -q --cached .ait/$O && echo bogus > .ait/$O && git add .ait/$O && tick && git commit -q --no-verify -m bogus
tick; expect_err "fulfill an ID that is not there" E_ID ait fulfill O000000000000 -m x
tick; expect_err "then the malformed record" E_FORMAT ait fulfill $O -m x

# 5.10: a commit with nothing to check skips the malformed record
echo z >> src/order.c && git add src/order.c
tick; expect_ok "a commit with nothing to check" ait commit -m z
check "warns the record it skipped" err_has "warning: skipping malformed '.ait/$O' at 'index'"
git reset -q --hard "$H"

# 7 before 8: commit reports what blocks before it asks for the session
tick; ait offer src/stock.c specs/p.yaml --force -m o >"$T/out"; K=$(new_id)
git rm -q --cached src/stock.c
run env AIT_SESSION=- ait commit -m x
check "code-required before the session" err_has "error: no code under the contract's scope: '$K'"
check "with its report" out_has "code-required $K"
done_testing
