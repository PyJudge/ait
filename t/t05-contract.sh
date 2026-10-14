#!/bin/sh
# spec 6.5: contract new, new spec, checkout, add, commit, push, pull
. "$(dirname "$0")/lib.sh"
setup

run ait contract new spec .ait/specs/s.yaml exec
check_rc "new spec writes the template (6.5.1)" 0
check "under .ait/" "grep -q 'engine: exec' .ait/specs/s.yaml"
run ait contract new spec .ait/specs/s.yaml exec
check_rc "it does not overwrite" 1

run ait contract new spec .ait/z.yaml bogus
check_rc "new spec takes exec or prompt (6.5.1)" 129
run ait contract new spec ../x.yaml exec
check_rc "new spec takes a repository path (3.1)" 128
run ait contract new demand fulfill
check_rc "a group may not be named fulfill (2.1.3)" 129
mkdir -p .ait/a; echo x > .ait/a/notes.yaml
run ait contract add .ait/a/notes.yaml
check_rc "add takes .ait/<group>/<ID>.yaml only (6.5.3)" 129

run ait contract new demand user
check_rc "new demand writes a draft in its group (6.5.2)" 0
P=$(sed -n 's/^path //p' "$T/out")
check "at .ait/user/<ID>.yaml" "case $P in .ait/user/D*.yaml) true ;; *) false ;; esac"
sed -i.bak 's#consumer: "<scope>"#consumer: "src/order.c"#; s#provider: "<scope>"#provider: "src/stock.c"#; s#<condition>#counted per day#' "$P"
run ait contract new offer user
P2=$(sed -n 's/^path //p' "$T/out")
sed -i.bak 's#provider: "<scope>"#provider: "src/stock.c"#; s#<condition>#holds stock#' "$P2"
run ait contract add "$P" "$P2"
check_rc "add stages the drafts (6.5.3)" 0
run ait contract commit -m "two at once"
check_rc "commit checks them in as one commit (6.5.4)" 0
check "two new lines" "test \"\$(grep -c '^new ' $T/out)\" = 2"
check "one commit on the branch for both" "test \"\$(git diff-tree -r --name-only --no-commit-id ait/master | wc -l | tr -d ' ')\" = 2"
D=$(basename "$P" .yaml)
check "the creator is the session's agent" "yaml $D | grep -qx 'creator: alice'"
check "the IDs are the request's" "yaml $D | grep -q '^condition-id: R'"

rm -rf .ait/user
run ait contract checkout "$D"
check_rc "checkout brings a contract into .ait/ (6.5.2a)" 0
check "the file" "test -f .ait/user/$D.yaml"
run ait future amend "$D" - src/stock.c:1-5 - -m "someone else"
sed -i.bak 's#consumer: "src/order.c"#consumer: "src/order.c:1-3"#' ".ait/user/$D.yaml"
ait contract add ".ait/user/$D.yaml" >/dev/null
run ait contract commit -m "stale"
check_rc "a stale draft is refused (6.5.4)" 1
check "the message" "has err \"'$D' changed at 'ait/master' since '.ait/user/$D.yaml' was written\""
check "the other change stands" "yaml $D | grep -q 'src/stock.c:1-5'"
rm -f "$(git rev-parse --git-dir)/ait/index"

ait fulfill "$D" --force -m now >/dev/null 2>&1
run ait future drop "$D" -m retire; X=$(newest X)
run ait future demand src/order.c src/stock.c cond.yaml -m other; D9=$(newest D)
ait contract checkout "$X" >/dev/null
sed -i.bak "s/^target: .*/target: $D9/" ".ait/$X.yaml"
ait contract add ".ait/$X.yaml" >/dev/null
run ait contract commit -m "retarget"
check_rc "a drop retargeted by commit needs a current target too (2.4)" 1
check "the message" "has err \"not allowed: '$X' targets '$D9', which is not current\""
rm -f "$(git rev-parse --git-dir)/ait/index" ".ait/$X.yaml"

run ait contract push
check_rc "push sends the branch (6.5.5)" 0
check "the remote has it" "git --git-dir=$T/origin.git rev-parse -q --verify refs/heads/ait/master >/dev/null"

git clone -q "$T/origin.git" "$T/c"
cd "$T/c"; ait init >/dev/null; BOB=$(ait session open --agent bob)
cp "$T/r/cond.yaml" cond.yaml
run ait future demand src/order.c src/stock.c:40-45 cond.yaml -m "from bob"
ait contract push >/dev/null 2>&1
cd "$T/r"
git branch -q --set-upstream-to=origin/ait/master ait/master
run ait contract pull
check_rc "pull fast-forwards (6.5.6)" 0
check "it says so" "has out 'Fast-forward'"

cd "$T/c"; run ait future amend "$D" src/order.c:2-4 - - -m "bob edits"; ait contract push >/dev/null 2>&1
cd "$T/r"; run ait future amend "$D" src/order.c:5-6 - - -m "alice edits"
run ait contract pull
check_rc "the same contract changed on both sides conflicts (6.5.6)" 1
check "the message" "has err 'contract branches conflict in'"
check "the hint enters the branch" "has err \"ait switch ait/master\""
git update-ref refs/heads/ait/master origin/ait/master

cd "$T/c"; run ait future demand src/order.c src/stock.c:20-22 cond.yaml -m "bob again"; ait contract push >/dev/null 2>&1
cd "$T/r"; run ait future demand src/order.c src/stock.c:23-24 cond.yaml -m "alice again"
run ait contract pull
check_rc "different contracts merge (6.5.6-4)" 0
check "the merge commit has both parents" "test \"\$(git rev-list --parents -1 ait/master | wc -w | tr -d ' ')\" = 3"
check "its message" "has out \"\\[CONTRACT\\] merge 'origin/ait/master'\""
ait contract push >/dev/null 2>&1

run ait future demand src/order.c src/stock.c:30-31 cond.yaml -m d2; D2=$(newest D)
run ait future offer src/stock.c cond.yaml -m o; O=$(newest O)
run ait future offer src/stock.c cond.yaml -m o3; O3=$(newest O)
ait contract push >/dev/null 2>&1
cd "$T/c"; ait contract pull >/dev/null 2>&1; run ait undertake -p "$D2" "$O" -m "bob shares"; ait contract push >/dev/null 2>&1
cd "$T/r"; run ait undertake "$D2" "$O3" -m "alice wholly"
tip=$(git rev-parse ait/master)
run ait contract pull
check_rc "a clean merge that mixes undertakes is refused (4.10)" 1
check "the message" "has err \"merging 'origin/ait/master' breaks contract rules: \""
check "the hint" "has err 'resolve the conflicts there'"
check "nothing changed" "test \"\$(git rev-parse ait/master)\" = $tip"
git switch -q ait/master; git merge -q --no-edit origin/ait/master >/dev/null; git switch -q master
run env AIT_SESSION=- ait status
check "a merge made by git outside ait: status warns of the violation (4.10)" "has err \"warning: '$D2' is already undertaken \""
check "and shows the contract it names (6.3-4)" "grep -qx \"id $D2\" $T/out"

done_testing
