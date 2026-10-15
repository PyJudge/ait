#!/bin/sh
# spec 4.4, 6.4.4, 6.5.4, 6.7.8: successful rows precede the summaries.
. "$(dirname "$0")/lib.sh"
setup

# A branch removed after start() exercises the root checkin through the real CAS.
racing_git
printf 'git update-ref -d refs/heads/ait/master\n' >"$T/before-fetch"
run env PATH="$T/bin:$PATH" ait future demand src/order.c src/stock.c cond.yaml as d -m root
D=$(newest D)
check_rc "a root request succeeds" 0
cat >"$T/expect" <<EOF
new $D d
[ait/master $(git rev-parse --short ait/master) (root-commit)] [CONTRACT] root
EOF
check "root request prints new before its single root summary" "cmp -s $T/expect $T/out"
check "the root checkin has no parent" "test \"\$(git rev-list --count ait/master)\" = 1"

run ait offer src/order.c cond.yaml as o --force -m immediate
O=$(newest O)
check_rc "a request can create and fulfill together" 0
cat >"$T/expect" <<EOF
new $O o
fulfilled $O
[ait/master $(git rev-parse --short ait/master)] [FULFILL] immediate
EOF
check "a request prints new then fulfilled then its summary" "cmp -s $T/expect $T/out"

cat >.git/hooks/pre-commit <<'HOOK'
#!/bin/sh
printf 'hook stdout\n'
printf 'hook stderr\n' >&2
HOOK
chmod +x .git/hooks/pre-commit
echo changed >>src/stock.c
git add src/stock.c
run ait fulfill "$D" --force -m staged
check_rc "fulfill commits staged code" 0
cat >"$T/expect" <<EOF
fulfilled $D
[master $(git rev-parse --short HEAD)] staged
 1 file changed, 1 insertion(+)
[ait/master $(git rev-parse --short ait/master)] [FULFILL] staged
EOF
check "fulfill prints its row, Git's complete stdout, then the checkin summary" "cmp -s $T/expect $T/out"
printf 'hook stdout\nhook stderr\n' >"$T/expect-err"
check "Git still sends hook stdout and stderr unchanged to stderr" "cmp -s $T/expect-err $T/err"
rm .git/hooks/pre-commit

code=$(git rev-parse HEAD)
run ait fulfill "$D" --force -m no-stage
check_rc "fulfill without staged code succeeds" 0
cat >"$T/expect" <<EOF
fulfilled $D
[ait/master $(git rev-parse --short ait/master)] [FULFILL] no-stage
EOF
check "no-stage fulfillment has only the fulfilled row and checkin summary" "cmp -s $T/expect $T/out"
check "no-stage fulfillment keeps HEAD and warns" "test \"\$(git rev-parse HEAD)\" = $code && has err '^warning: nothing to commit;'"

run ait future drop "$D" -m retirement
X=$(newest X)
run ait fulfill "$X" -m withdrawn
check_rc "a drop fulfills its target" 0
cat >"$T/expect" <<EOF
fulfilled $X
withdrawn $D
[ait/master $(git rev-parse --short ait/master)] [FULFILL] withdrawn
EOF
check "withdrawn immediately follows its fulfilled drop before the summary" "cmp -s $T/expect $T/out"

# A batch reports both existing and new contracts before one checkin summary.
ait contract checkout "$O" >/dev/null
sed -i.bak 's/stock never goes below zero/revised condition/' ".ait/$O.yaml"
P=.ait/O111111111111.yaml
cat >"$P" <<'YAML'
condition-id: ""
change-id: ""
kind: offer
creator: ""
provider: "src/stock.c"
state: future
specs:
- body: |
    new staged condition
undertakes: []
undertakes-partially: []
fulfilled:
YAML
ait contract add ".ait/$O.yaml" "$P" >/dev/null
run ait contract commit -m batch
check_rc "a draft batch commits" 0
printf 'change %s\nnew O111111111111\n' "$O" | sort -k2 >"$T/expect"
printf '[ait/master %s] [CONTRACT] batch\n' "$(git rev-parse --short ait/master)" >>"$T/expect"
check "a draft batch prints new and change rows before its single summary" "cmp -s $T/expect $T/out"

git show "ait/master:$O.yaml" >".ait/$O.yaml"
ait contract add ".ait/$O.yaml" >/dev/null
tip=$(git rev-parse ait/master)
run ait contract commit -m unchanged
check_rc "an unchanged draft is a successful noop" 0
printf 'change %s\n' "$O" >"$T/expect"
check "an unchanged draft reports its contract without inventing a summary" "cmp -s $T/expect $T/out && test \"\$(git rev-parse ait/master)\" = $tip"

ait future cancel O111111111111 -m cancelled >/dev/null
tip=$(git rev-parse ait/master)
run ait future cancel O111111111111 -m again
check_rc "an already cancelled contract is a successful noop" 0
check "a noop request emits no success rows or summary" "! test -s $T/out && test \"\$(git rev-parse ait/master)\" = $tip"

# Let Git generate failure stdout: the stage empties just before its commit.
cat >.git/hooks/pre-commit <<'HOOK'
#!/bin/sh
printf 'hook stdout\n'
printf 'hook stderr\n' >&2
HOOK
chmod +x .git/hooks/pre-commit
echo rejected >>src/order.c
git commit -m rejected >"$T/git-out" 2>"$T/git-err"
git_rc=$?
git add src/order.c
code=$(git rev-parse HEAD)
tip=$(git rev-parse ait/master)
printf 'git reset -q\n' >"$T/before-commit"
run env PATH="$T/bin:$PATH" ait fulfill "$O" --force -m rejected
check_rc "a Git commit failure returns E_COMMIT" 1
check "a failed Git commit retains its complete stdout and prints no success rows" "test $git_rc = 1 && test -s $T/git-out && cmp -s $T/git-out $T/out && ! has out '^fulfilled ' && ! has out '^\\[ait/'"
cat "$T/git-err" >"$T/expect-err"
printf "error: commit failed; '%s' not fulfilled\n" "$O" >>"$T/expect-err"
check "Git's failure diagnostics precede the ait refusal unchanged" "cmp -s $T/expect-err $T/err"
check "a Git commit failure moves neither branch" "test \"\$(git rev-parse HEAD)\" = $code && test \"\$(git rev-parse ait/master)\" = $tip"
rm .git/hooks/pre-commit

# Obtain Git's exact stdout independently, then replay the same commit with ait.
git add src/order.c
GIT_AUTHOR_DATE='2001-01-01T00:00:00 +0000' GIT_COMMITTER_DATE='2001-01-01T00:00:00 +0000' \
	git commit -m cas-refused >"$T/git-out" 2>"$T/git-err"
committed=$(git rev-parse HEAD)
git reset -q --soft HEAD^
cat >.git/hooks/reference-transaction <<'HOOK'
#!/bin/sh
test "$1" = prepared || exit 0
while read old new ref
do
	test "$ref" != refs/heads/ait/master || exit 1
done
HOOK
chmod +x .git/hooks/reference-transaction
run env GIT_AUTHOR_DATE='2001-01-01T00:00:00 +0000' GIT_COMMITTER_DATE='2001-01-01T00:00:00 +0000' \
	ait fulfill "$O" --force -m cas-refused
check_rc "final CAS refusal after a code commit returns E_NOT_RECORDED" 1
check "final CAS refusal retains all Git commit stdout without successful ait rows" "cmp -s $T/git-out $T/out && ! has out '^fulfilled ' && ! has out '^\\[ait/'"
check "the failure names the committed code and leaves the contract branch alone" "test \"\$(git rev-parse HEAD)\" = $committed && test \"\$(git rev-parse ait/master)\" = $tip && has err \"error: committed '$(git rev-parse --short HEAD)' but '$O' not fulfilled:\""

run ait fulfill "$O" --force -m cas-no-stage
check_rc "final CAS refusal without staged code returns E_NOT_RECORDED" 1
check "no-stage CAS refusal emits no success output" "! test -s $T/out && has err \"error: '$O' not fulfilled:\""

run ait future offer src/stock.c cond.yaml -m cas-request
check_rc "final CAS refusal rejects a request" 128
check "a refused request emits neither new nor summary" "! test -s $T/out"
git show "ait/master:$O.yaml" >".ait/$O.yaml"
sed -i.bak 's/revised condition/refused draft/' ".ait/$O.yaml"
ait contract add ".ait/$O.yaml" >/dev/null
run ait contract commit -m cas-draft
check_rc "final CAS refusal rejects a draft commit" 128
check "a refused draft commit emits neither change nor summary" "! test -s $T/out"

done_testing
