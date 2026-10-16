#!/bin/sh
# spec 4.3: syntax before branch state; command judgments after it
. "$(dirname "$0")/lib.sh"
setup

good=$(git rev-parse ait/master)
git update-ref -d refs/heads/ait/master

run ait sub
check_rc "sub requires an argument before looking for a branch" 129
check "sub reports its syntax error" "has err 'sub names what to subscribe to'"
run ait listen --fetch 0
check_rc "listen parses fetch seconds before looking for a branch" 129
run ait listen --fetch
check_rc "listen requires the fetch value before branch checks" 129
run ait listen --fetch 2147483648
check_rc "listen refuses fetch seconds outside its integer range" 129
run ait listen --bogus
check_rc "listen refuses unknown options before branch checks" 129
run ait listen --fetch 1 --fetch 2
check_rc "listen refuses a repeated fetch option before branch checks" 129
run ait contract push a b
check_rc "push checks its argument count before branch checks" 129
run ait contract pull a b
check_rc "pull checks its argument count before branch checks" 129
run ait contract add .ait/notes.yaml
check_rc "add checks draft path syntax before branch checks" 129
run ait contract add .ait/Dbbbbbbbbbbbb.yaml .ait/notes.yaml
check_rc "add checks every path before branch checks or file reads" 129
run ait future offer src/stock.c cond.yaml --force --force -m repeated
check_rc "a repeated force option is a syntax error before branch checks" 129
run ait future offer src/stock.c cond.yaml -g a -g b -m repeated
check_rc "a repeated group option is a syntax error before branch checks" 129
run ait future offer src/stock.c cond.yaml as demand -m reserved
check_rc "reserved aliases are still refused before branch checks" 129
run ait fulfill Daaaaaaaaaaaa --force -m absent
check_rc "a valid fulfill command reaches the branch check before ID lookup" 128
check "the missing branch is the first refusal" "has err '^fatal: no ait branch'"

git update-ref refs/heads/ait/master "$good"
git switch -q ait/master
printf 'kind: demand\n' > Daaaaaaaaaaaa.yaml
git add Daaaaaaaaaaaa.yaml
git commit -qm malformed
git switch -q master

run ait sub
check_rc "sub syntax also precedes a malformed branch" 129
run ait listen --fetch 0
check_rc "listen syntax also precedes a malformed branch" 129
run ait contract pull a b
check_rc "pull syntax also precedes a malformed branch" 129
run ait contract add .ait/notes.yaml
check_rc "draft path syntax also precedes a malformed branch" 129
run ait sub Daaaaaaaaaaaa
check_rc "valid sub syntax reaches the common format check" 128
check "the format refusal is preserved" "has err '^fatal: bad format:'"
run env AIT_SESSION=Sdeadbeef0000 ait sub
check_rc "sub syntax precedes both malformed state and an invalid session" 129
run env AIT_SESSION=Sdeadbeef0000 ait sub Daaaaaaaaaaaa
check_rc "the branch format check precedes session validation" 128
check "format is still the first refusal" "has err '^fatal: bad format:'"

git update-ref refs/heads/ait/master "$good"
run ait future offer src/stock.c cond.yaml -m first -m second
check_rc "multiple message options still form paragraphs" 0
check "both message paragraphs are recorded" "git log -1 --format=%B ait/master | grep -Fxq '[CONTRACT] first' && git log -1 --format=%B ait/master | grep -qx second"

run env AIT_SESSION=Sdeadbeef0000 ait contract new spec .ait/not-created.yaml exec
check_rc "new spec validates an explicitly selected session" 128
check "new spec has no side effect on invalid session" "test ! -e .ait/not-created.yaml"
run env AIT_SESSION=Sdeadbeef0000 ait contract new demand
check_rc "new draft validates an explicitly selected session" 128
check "new draft prints no success on invalid session" "test ! -s $T/out"
run env AIT_SESSION=Sdeadbeef0000 ait contract new demand fulfill
check_rc "new draft group syntax precedes session validation" 129

done_testing
