#!/bin/sh
# spec 2.6.4, 6.5.3-4: the contract index is a snapshot, separate from code
. "$(dirname "$0")/lib.sh"
setup
racing_git

stage=$(git rev-parse --absolute-git-dir)/ait/index
export stage
mkdir -p .ait/stage
P=.ait/stage/D111111111111.yaml
Q=.ait/stage/D222222222222.yaml
R=.ait/stage/D333333333333.yaml
export P Q
cat >"$P" <<'YAML'
condition-id: ""
change-id: ""
kind: demand
creator: ""
consumer: "src/order.c"
provider: "src/stock.c"
state: future
specs:
- body: |
    added content
fulfilled:
YAML
cp "$P" "$Q"
printf 'kind: demand\n' >"$R"

# Real Git seeds the stage so a broken ait add cannot hide commit regressions.
seed_stage() {
	git hash-object -w "$1" >"$T/blob"
	GIT_INDEX_FILE="$stage" git update-index --add --cacheinfo "100644,$(cat "$T/blob"),${1#.ait/}"
}

echo staged-code >>src/order.c
git add src/order.c
git ls-files --stage >"$T/code-before"
code_head=$(git rev-parse HEAD)
tip=$(git rev-parse ait/master)
run ait contract add "$P" "$Q"
check_rc "one add accepts multiple valid drafts" 0
check "add reports both drafts after staging them" "test \"\$(grep -c '^add ' $T/out)\" = 2"
check "both added blobs are in the contract stage" "test \"\$(GIT_INDEX_FILE=$stage git ls-files --stage | wc -l | tr -d ' ')\" = 2"
check "add leaves no owned index lock" "! test -e $stage.lock"
check "add does not move either branch" "test \"\$(git rev-parse HEAD)\" = $code_head && test \"\$(git rev-parse ait/master)\" = $tip"
git ls-files --stage >"$T/code-after"
check "add preserves the staged code" "cmp -s $T/code-before $T/code-after"

rm -f "$stage" "$stage.lock"
seed_stage "$P"
cp "$stage" "$T/stage-before"
run ait contract add "$Q" "$R"
check_rc "a bad second draft rejects the whole add" 128
check "a rejected add preserves the existing contract stage" "cmp -s $T/stage-before $stage"
check "a rejected add reports no successful additions" "! has out '^add '"
check "validation failure leaves no index lock" "! test -e $stage.lock"
run ait contract add "$Q" .ait/stage/D444444444444.yaml
check_rc "an unreadable second draft rejects the whole add" 128
check "a read failure preserves the stage and reports no additions" "cmp -s $T/stage-before $stage && ! has out '^add '"

# Helpers must not consume an unrelated manually staged contract or code.
rm -f "$stage" "$stage.lock"
seed_stage "$P"
cp "$stage" "$T/stage-before"
run ait future demand src/order.c src/stock.c cond.yaml -m helper
check_rc "future demand checks in while another draft is staged" 0
check "future demand leaves the contract stage untouched" "cmp -s $T/stage-before $stage"
check "future demand did not commit the staged draft" "! git cat-file -e ait/master:stage/D111111111111.yaml 2>/dev/null"
git ls-files --stage >"$T/code-after"
check "future demand leaves code staged and HEAD unchanged" "cmp -s $T/code-before $T/code-after && test \"\$(git rev-parse HEAD)\" = $code_head"

# The stage contains add-time bytes, even when the draft is edited later.
sed -i.bak 's/added content/worktree edit/' "$P"
run ait contract commit -m snapshot
check_rc "commit records the staged snapshot" 0
check "commit uses add-time content and leaves the draft edit" "yaml D111111111111 | grep -q 'added content' && grep -q 'worktree edit' $P"
check "successful commit empties the contract stage and releases its lock" "! test -e $stage && ! test -e $stage.lock"
git ls-files --stage >"$T/code-after"
check "contract commit preserves the code stage and code branch" "cmp -s $T/code-before $T/code-after && test \"\$(git rev-parse HEAD)\" = $code_head"

rm -f "$stage" "$stage.lock"
seed_stage "$P"
cp "$stage" "$T/stage-before"
tip=$(git rev-parse ait/master)
run ait contract commit -m stale
check_rc "a stale staged draft is refused by the judge" 1
check "judge failure preserves the stage and branch and releases the lock" "cmp -s $T/stage-before $stage && test \"\$(git rev-parse ait/master)\" = $tip && ! test -e $stage.lock"
run ait contract add "$Q"
check_rc "add works after the judge refuses a commit" 0

# The lock belongs to the process which created it; a foreign lock survives.
rm -f "$stage" "$stage.lock"
seed_stage "$Q"
cp "$stage" "$T/stage-before"
printf 'foreign lock\n' >"$stage.lock"
cp "$stage.lock" "$T/foreign-lock"
tip=$(git rev-parse ait/master)
run ait contract commit -m locked
check_rc "commit refuses a pre-existing index lock" 128
check "commit neither consumes the stage nor removes a foreign lock" "cmp -s $T/stage-before $stage && cmp -s $T/foreign-lock $stage.lock && test \"\$(git rev-parse ait/master)\" = $tip"
run ait contract add "$P"
check_rc "add also respects a pre-existing index lock" 128
check "failed add preserves the foreign lock and stage" "cmp -s $T/foreign-lock $stage.lock && cmp -s $T/stage-before $stage && ! has out '^add '"
rm -f "$stage" "$stage.lock"
git update-ref refs/heads/ait/master "$tip"
seed_stage "$Q"

# Both the first read and the CAS must be inside the same lock lifetime.
# A watchdog makes a blocking lock in a synchronous hook fail, not deadlock.
cat >"$T/try-add" <<'HOOK'
perl -e 'alarm 5; exec @ARGV; die $!' ait contract add "$P" >"$T/nested.out" 2>"$T/nested.err"
echo $? >"$T/nested.rc"
exit 0
HOOK
cat >"$T/before-ls-files" <<'HOOK'
if test "$GIT_INDEX_FILE" != "$stage"
then
	cp "$T/before-ls-files.done" "$T/before-ls-files"
	exit 0
fi
sh "$T/try-add"
HOOK
rm -f "$T/nested.rc" "$T/nested.out" "$T/nested.err"
run env PATH="$T/bin:$PATH" ait contract commit -m before-read
check_rc "commit succeeds while an add at its stage read is refused" 0
check "stage is locked before its first read, without blocking the add" "test \"\$(cat $T/nested.rc)\" = 128 && grep -q 'index.lock' $T/nested.err && ! grep -q '^add ' $T/nested.out"
check "the rejected concurrent draft was not committed" "yaml D111111111111 | grep -q 'added content'"
check "the lock is released after the commit" "! test -e $stage.lock"

rm -f "$stage" "$stage.lock"
git show ait/master:stage/D222222222222.yaml >"$Q"
sed -i.bak 's/added content/next version/' "$Q"
seed_stage "$Q"
cp "$T/try-add" "$T/before-update-ref"
rm -f "$T/nested.rc" "$T/nested.out" "$T/nested.err"
run env PATH="$T/bin:$PATH" ait contract commit -m before-cas
check_rc "commit succeeds while an add at its CAS is refused" 0
check "the stage stays locked through the CAS without blocking add" "test \"\$(cat $T/nested.rc)\" = 128 && grep -q 'index.lock' $T/nested.err"
git show ait/master:stage/D111111111111.yaml >"$P"
sed -i.bak 's/added content/worktree edit/' "$P"
run ait contract add "$P"
check_rc "add works immediately after commit releases its lock" 0

# A Git refusal must preserve the exact staged data and release the lock.
rm -f "$stage" "$stage.lock"
seed_stage "$P"
cp "$stage" "$T/stage-before"
tip=$(git rev-parse ait/master)
cat >.git/hooks/reference-transaction <<'HOOK'
#!/bin/sh
test "$1" != prepared
HOOK
chmod +x .git/hooks/reference-transaction
run ait contract commit -m rejected
check_rc "a real reference-transaction hook can refuse the checkin" 128
check "a refused checkin preserves stage and branch, and removes its lock" "cmp -s $T/stage-before $stage && test \"\$(git rev-parse ait/master)\" = $tip && ! test -e $stage.lock"
rm .git/hooks/reference-transaction
run ait contract commit -m retry
check_rc "the same staged data can be committed after fixing the refusal" 0
check "retry consumes the stage and releases the lock" "! test -e $stage && ! test -e $stage.lock"

# Hold a child at a known point, terminate ait, and then let the child exit.
rm -f "$stage" "$stage.lock"
git show ait/master:stage/D222222222222.yaml >"$Q"
sed -i.bak 's/next version/interrupted version/' "$Q"
seed_stage "$Q"
cp "$stage" "$T/stage-before"
cat >"$T/before-commit-tree" <<'HOOK'
: >"$T/ready"
for i in $(seq 1 100)
do
	test -e "$T/continue" && exit 0
	sleep 0.05
done
exit 1
HOOK
env PATH="$T/bin:$PATH" ait contract commit -m interrupted >"$T/out" 2>"$T/err" &
committer=$!
for i in $(seq 1 100)
do
	test -e "$T/ready" && break
	sleep 0.05
done
if test -e "$T/ready"
then
	kill -TERM "$committer"
	wait "$committer" 2>/dev/null
	rc=$?
	: >"$T/continue"
else
	kill -TERM "$committer" 2>/dev/null
	wait "$committer" 2>/dev/null
	rc=0
fi
check "SIGTERM terminates the committing process" "test $rc -gt 128"
check "signal cleanup removes the owned lock and preserves the stage" "! test -e $stage.lock && cmp -s $T/stage-before $stage"
run ait contract commit -m after-signal
check_rc "commit can be retried after interruption" 0
run ait contract commit -m empty
check_rc "an empty stage is refused" 1
check "an empty stage refusal releases the lock" "! test -e $stage.lock"

done_testing
