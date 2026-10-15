#!/bin/sh
# spec 2.5, 5.3-5.5: touch compares HEAD's verified code with the index.
. "$(dirname "$0")/lib.sh"
setup

state_is() { grep -A3 "^id $1$" "$T/out" | grep -qx "state $2"; }
is_touched() { awk -v id="$1" '$1 == "id" { selected = ($2 == id) } selected && $0 == "touch" { found=1 } END { exit !found }' "$T/out"; }

# The helper pins the staged blob, but the hook changes it before fulfillment.
# The original anchor has never belonged to a commit; the recorded blob has.
printf 'a\nb\nc\nd\ne\n' > p
git add p && git commit -qm 'touch fixture'
sed -i.bak 's/^b$/B/' p
rm p.bak
git add p
anchor=$(git rev-parse :p)
cat > .git/hooks/pre-commit <<'HOOK'
#!/bin/sh
echo tail >> p
git add p
HOOK
chmod +x .git/hooks/pre-commit
run ait offer p:1-1 cond.yaml --force -m 'record the committed blob'
check_rc 'a commit hook may change the pinned blob before it is recorded' 0
O=$(newest O)
rm .git/hooks/pre-commit
recorded=$(git rev-parse HEAD:p)
check 'the scope anchor differs from the recorded code blob' "test $anchor != $recorded && yaml $O | grep -q $anchor"
git gc --prune=now
check 'GC removes only the original anchor, while the recorded code remains' \
	"! git cat-file -e $anchor 2>/dev/null && git cat-file -e $recorded"
run ait status p
check_rc 'a collected scope anchor does not prevent status' 0
check 'the committed provider remains current without its original anchor (2.5)' "state_is $O current"
check 'unchanged HEAD and index do not touch the provider (5.5)' "! is_touched $O"

sed -i.bak 's/^e$/outside/' p
rm p.bak
git add p
run ait status p
check 'an edit outside the recorded range leaves it current' "state_is $O current"
check 'an edit outside the recorded range does not touch it' "! is_touched $O"
git restore --source=HEAD --staged --worktree p
sed -i.bak 's/^a$/CHANGED/' p
rm p.bak
git add p
run ait status p
check 'an edit inside the recorded range makes it unverified' "state_is $O 'current ?unverified'"
check 'touch survives collection of the original scope anchor (5.5)' "is_touched $O"
git restore --source=HEAD --staged --worktree p
git update-index --chmod=+x p
run ait status p
check 'a mode change touches the verified range without its original anchor' "state_is $O 'current ?unverified' && is_touched $O"
git restore --source=HEAD --staged --worktree p

# Two valid fulfillments have different provider contents. HEAD matches the
# later record and has moved the range, while the index matches the first.
printf 'q1\nq2\nq3\nq4\nq5\n' > q
git add q && git commit -qm 'two recorded versions'
sed -i.bak 's/^q3$/transient/' q
rm q.bak
git add q
anchor=$(git rev-parse :q)
cat > .git/hooks/pre-commit <<'HOOK'
#!/bin/sh
echo tail >> q
git add q
HOOK
chmod +x .git/hooks/pre-commit
run ait offer q:2-2 cond.yaml --force -m 'first recorded version'
check_rc 'the first version is fulfilled' 0
M=$(newest O)
rm .git/hooks/pre-commit
sed -i.bak 's/^q2$/second/' q
rm q.bak
git add q
run ait fulfill "$M" --force -m 'second recorded version'
check_rc 'a changed provider gets another valid fulfillment with the same condition' 0
check 'both records remain valid attachments' "test \"\$(yaml $M | grep -c \"$M-R\")\" = 2"
{ echo prefix; cat q; } > "$T/shifted"
cp "$T/shifted" q
git add q && git commit -qm 'move the verified range'
git gc --prune=now
check 'the multi-record fixture also loses only its original anchor' "! git cat-file -e $anchor 2>/dev/null"
run ait status q
check 'HEAD remains current through the later record after the range moves' "state_is $M current && ! is_touched $M"
sed -i.bak 's/^q5$/outside/' q
rm q.bak
git add q
run ait status q
check 'the matched record maps HEAD endpoints before an outside edit is compared' "state_is $M current && ! is_touched $M"
git restore --source=HEAD --staged --worktree q
sed -i.bak 's/^second$/q2/' q
rm q.bak
git add q
run ait status q
check 'the index is current through a different valid fulfillment (5.4)' "state_is $M current"
check 'different current versions still touch the HEAD provider (5.5)' "is_touched $M"
run ait status
check 'an otherwise current offer appears in unfiltered status because it is touched' "state_is $M current && is_touched $M"

# No HEAD proof may be assembled from different records for different scopes.
git restore --source=HEAD --staged --worktree q
printf 'u1\n' > u
printf 'v1\n' > v
git add u v
run ait offer u,v cond.yaml --force -m 'first complete provider'
check_rc 'a multi-scope provider is fulfilled as one record' 0
N=$(newest O)
first=$(git rev-parse HEAD)
printf 'u2\n' > u
printf 'v2\n' > v
git add u v
run ait fulfill "$N" --force -m 'second complete provider'
check_rc 'the second record changes both provider scopes' 0
run ait status u v
check 'the later record proves all HEAD scopes together' "state_is $N current && ! is_touched $N"
git restore --source="$first" --staged --worktree u
git commit -qm 'combine parts of two recorded versions'
printf 'v3\n' > v
git add v
run ait status u v
check 'HEAD scopes from different fulfillments cannot be mixed into a current proof' \
	"state_is $N 'current ?unverified' && ! is_touched $N"

done_testing
