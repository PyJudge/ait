#!/bin/sh
# spec 4.3-4.4, 4.10: the checkin, its races, and what it refuses first
. "$(dirname "$0")/lib.sh"
setup
racing_git

run ait future demand src/order.c src/stock.c cond.yaml -m d1
D=$(newest D)

# another request lands on the branch just before this one's CAS: different files, so it is judged again and lands
echo "cd $T/r && PATH=\"$(echo "$PATH" | sed "s|$T/bin:||")\" ait future demand src/order.c src/stock.c:1-2 cond.yaml -m other >/dev/null" > "$T/before-update-ref"
run env PATH="$T/bin:$PATH" ait future offer src/stock.c cond.yaml -m o1
check_rc "a lost CAS on other files is judged again and checks in (4.4-4)" 0
check "both requests are on the branch" "test \"\$(git rev-list --count ait/master)\" = 4"

# the same contract changed meanwhile: stop
echo "cd $T/r && PATH=\"$(echo "$PATH" | sed "s|$T/bin:||")\" ait future amend $D - src/stock.c:3-4 - -m meanwhile >/dev/null" > "$T/before-update-ref"
run env PATH="$T/bin:$PATH" ait future amend "$D" - src/stock.c:5-6 - -m mine
check_rc "a lost CAS on the same file stops (4.4-4)" 1
check "the message" "has err \"'ait/master' moved while running; nothing changed\""
check "the other change stands" "yaml $D | grep -q 'src/stock.c:3-4'"

# a drop's fulfilment, and its target changes meanwhile: nothing recorded; the drop itself did not change, so fulfill again
ait fulfill "$D" --force -m now >/dev/null 2>&1
X=$(ait future drop "$D" -m retire | sed -n 's/^new //p')
echo "cd $T/r && PATH=\"$(echo "$PATH" | sed "s|$T/bin:||")\" ait future amend $D src/order.c:1-2 - - --force -m meanwhile >/dev/null" > "$T/before-update-ref"
run env PATH="$T/bin:$PATH" ait fulfill "$X" -m withdraw
check_rc "a lost CAS after the code step records nothing (6.7.5-3)" 1
check "the message says what is not fulfilled" "has err \"error: '$X' not fulfilled: 'ait/master' moved while running; nothing changed\""
check "the hint fulfills again, as the drop is as it was" "has err \"hint: ait fulfill $X -m 'withdraw'\""

good=$(git rev-parse ait/master)
git switch -q ait/master
f=$(git ls-tree -r --name-only HEAD | grep "$D.yaml"); printf 'x\000y\n' >> "$f"; git commit -qam nul
git switch -q master
run ait status
check "a NUL byte in a contract is a format error, not a cut file (2.3)" "has err \"bad format: '$f' at 'ait/master': a NUL character\""
git update-ref refs/heads/ait/master "$good"

git worktree add -q "$T/w" ait/master
run ait future demand src/order.c src/stock.c cond.yaml -m blocked
check_rc "a branch another worktree has checked out is refused by git (4.4-3a)" 128
check "git's message" "has err \"refusing to fetch into branch 'refs/heads/ait/master' checked out at\""
echo x >> src/order.c; git add src/order.c
run ait fulfill "$D" --force -m "used"
check_rc "fulfill asks git first, before it commits (4.3-4, 6.7.5-0)" 128
check "nothing was committed" "test \"\$(git rev-list --count HEAD)\" = 1"
git reset -q --hard
git worktree remove --force "$T/w"

git switch -q ait/master
printf 'kind: demand\n' > "$D.yaml.tmp"; mv "$D.yaml.tmp" "$(git ls-tree -r --name-only HEAD | grep "$D.yaml")"
git commit -qam "break it"
git switch -q master
run ait future demand src/order.c src/stock.c cond.yaml -m x
check_rc "a malformed contract on the branch is refused first (4.3-3)" 128
check "the message names the file, then the branch, then why (4.3-3)" "has err \"^fatal: bad format: '$D.yaml' at 'ait/master': .\""
check "the hint enters the branch to fix it" "has err \"ait switch ait/master\""
run ait status
check_rc "status shows it as a warning and goes on" 0
check "the warning" "has err 'warning: bad format'"

done_testing
