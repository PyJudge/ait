#!/bin/sh
# spec 3.1 (U67, v2 v16 3.1 and 2.2.2): a path may hold SP; output quotes such a path as git does
. "$(dirname "$0")/lib.sh"
setup

state_of() { grep -A4 "^id $1$" "$T/out" | sed -n 's/^state //p'; }
fulfil_file() { git show "ait/master:$(git ls-tree -r --name-only ait/master | grep "fulfill/$1-" | tail -1)"; }

mkdir -p "doc/법령 문안" "tests/a b"
printf 'one\ntwo\nthree\n' > "doc/법령 문안/spec.md"
printf 'x\n' > "tests/a b/case.csv"
printf 'y\n' > "src/new file.c"
git add doc tests src; git commit -qm 'spaced paths'
printf 'body: |\n  the case holds\nfixtures: ["tests/a b/case.csv"]\n' > fix.yaml

# 3.1: SP is a path byte; the scope and the fixture are taken and written as they are
run ait future demand "doc/법령 문안/spec.md" "src/new file.c" fix.yaml -m 'spaced demand'
check_rc "a demand with spaced scopes and a spaced fixture is taken" 0
D=$(newest D)
check "the contract keeps the paths as given" \
	"yaml $D | grep -qxF 'consumer: \"doc/법령 문안/spec.md\"' && yaml $D | grep -qxF 'provider: \"src/new file.c\"' && yaml $D | grep -qxF '  fixtures: [\"tests/a b/case.csv\"]'"

# 3.2: a line range of a spaced path keeps its base blob after the range, and reads back
run ait future offer "doc/법령 문안/spec.md:2-2" fix.yaml -m 'spaced range'
check_rc "a line range of a spaced path is taken" 0
R=$(newest O)
check "it is written with its base blob after the range" "yaml $R | grep -qE '^provider: \"doc/법령 문안/spec.md:2-2 [0-9a-f]{40}\"$'"

# 6.3: a spaced path selects, and its display is quoted as git quotes a path with SP
run ait status "doc/법령 문안/spec.md"
check "status reads the spaced contracts without a warning" "! has err '^warning:' && grep -qx 'id $D' '$T/out' && grep -qx 'id $R' '$T/out'"
check "the consumer line quotes the spaced path" "grep -A6 '^id $D$' '$T/out' | grep -qxF 'consumer \"doc/법령 문안/spec.md\"'"
check "a range keeps its numbers inside the quotes" "grep -A6 '^id $R$' '$T/out' | grep -qxF 'provider \"doc/법령 문안/spec.md:2-2\"'"

# 2.5, 5.4: the fulfilment records the spaced paths; a change shows ?changed after the quoted path
run ait fulfill "$D" --force -m 'checked'
check_rc "a spaced contract is fulfilled" 0
check "its code names the spaced paths" "fulfil_file $D | grep -qF '  \"src/new file.c\": ' && fulfil_file $D | grep -qF '  \"tests/a b/case.csv\": '"
echo z >> "tests/a b/case.csv"; git add "tests/a b/case.csv"
run ait status "src/new file.c"
check "a changed spaced fixture is unverified" "test \"\$(state_of $D)\" = 'current ?unverified'"
check "its mark follows the quoted path" "grep -A8 '^id $D$' '$T/out' | grep -qxF 'fixtures \"tests/a b/case.csv\" ?changed'"
git reset -q; git checkout -q -- "tests/a b/case.csv"

# 6.8: sub shows the quoted display; unsub takes the path as the shell gives it
run ait sub "doc/법령 문안/spec.md"
check "sub prints the quoted scope" "grep -qxF 'sub scope \"doc/법령 문안/spec.md\"' '$T/out'"
run ait unsub "doc/법령 문안/spec.md"
check "unsub removes it by its path" "grep -qxF 'unsub scope \"doc/법령 문안/spec.md\"' '$T/out'"

# 3.1: a path may not start with '"' (a quoted field starts with it), and a group has no SP
printf 'q\n' > '"quoted.c'; git add '"quoted.c'; git commit -qm quote
run ait future offer '"quoted.c' fix.yaml -m 'leading quote'
check "a path starting with a double quote is refused" "test \$rc = 128 && has err \"^fatal: bad scope: '\\\"quoted.c' is not a scope\$\""
run ait future offer "src/new file.c" fix.yaml -g 'a b' -m 'spaced group'
check "a group with SP is refused" "test \$rc = 129 && has err \"^error: bad group 'a b'\$\""

rm -f fix.yaml
done_testing
