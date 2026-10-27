#!/bin/sh
# spec 2.3, 2.5, 4.3-3: keep raw paths and byte lengths until text is validated.
. "$(dirname "$0")/lib.sh"
setup
ulimit -c 0

branch_file() {
	GIT_INDEX_FILE="$T/branch-index" git read-tree ait/master
	if [ "$2" = remove ]; then
		GIT_INDEX_FILE="$T/branch-index" git update-index --force-remove -- "$1"
	else
		if [ "$2" = missing ]; then blob=1111111111111111111111111111111111111111; else blob=$(git hash-object -w "$2"); fi
		GIT_INDEX_FILE="$T/branch-index" git update-index --add --cacheinfo "100644,$blob,$1"
	fi
	tree=$(GIT_INDEX_FILE="$T/branch-index" git write-tree --missing-ok)
	changed=$(printf 'outside ait\n' | git commit-tree -p ait/master "$tree")
	git update-ref refs/heads/ait/master "$changed"
}

base=$(git rev-parse ait/master)
printf 'kind: demand\n' > "$T/malformed"
# a ':' in a path, which Git for Windows refuses to index (core.protectNTFS)
if test_have_prereq FUNNYNAMES "a malformed Git pathname is kept intact in the status diagnostic"
then
	path="bad': path.yaml"
	branch_file "$path" "$T/malformed"
	run ait status src/
	check "a malformed Git pathname is kept intact in the status diagnostic" \
		"grep -Fq \"warning: bad format: '$path' at 'ait/master': not a contract path\" $T/err"
	run ait future demand src/order.c src/stock.c cond.yaml -m blocked
	check_rc "the malformed path blocks a writer" 128
	check "the fatal diagnostic and repair hint use the same complete raw path" \
		"grep -Fq \"fatal: bad format: '$path' at 'ait/master': not a contract path\" $T/err && grep -Fq \"hint: fix '$path' in 'ait/master'\" $T/err"
	git update-ref refs/heads/ait/master "$base"
fi
branch_file D111111111111.yaml missing
run ait status src/
check "an unreadable branch blob is identified as missing, without claiming a NUL" \
	"has err \"bad format: 'D111111111111.yaml' at 'ait/master': could not read blob\" && ! has err 'NUL character'"
git update-ref refs/heads/ait/master "$base"

run ait future demand src/order.c src/stock.c cond.yaml as d future offer src/stock.c cond.yaml as o undertake d o -m pair
D=$(newest D); O=$(newest O)
ait fulfill "$O" --force -m current >/dev/null 2>&1
valid=$(git rev-parse ait/master)
attachment=$(yaml "$D" | sed -n 's/^- "\(.*\)"$/\1/p' | tail -1)
branch_file "$attachment" remove
run ait status src/
check "an invalid attached contract is skipped instead of entering relationship judgments" \
	"has err \"is not a fulfilment of '$D'\" && ! has out '^id $D$' && ! has err 'would leave'"
check "a valid whole offer ignores the skipped malformed demand" \
	"grep -A3 '^id $O$' $T/out | grep -qx 'state current'"
git update-ref refs/heads/ait/master "$valid"

printf 'body: checked\n\000ignored: bytes\n' > nul-cond.yaml
run ait future demand src/order.c src/stock.c nul-cond.yaml -m 'NUL condition'
check_rc "a condition file with raw NUL is refused before truncation" 128
check "the NUL condition diagnostic retains its source path" "has err \"bad format: 'nul-cond.yaml': a NUL character\""

run ait contract new demand
P=$(sed -n 's/^path //p' "$T/out")
ID=$(basename "$P" .yaml)
sed -i.bak 's#consumer: "<scope>"#consumer: "src/order.c"#; s#provider: "<scope>"#provider: "src/stock.c"#' "$P"
cp "$P" "$T/draft"
printf '\000ignored\n' >> "$P"
run ait contract add "$P"
check_rc "add refuses a draft with raw NUL" 128
cp "$T/draft" "$P"
ait contract add "$P" >/dev/null
stage=$(git rev-parse --git-dir)/ait/index
printf '\000ignored\n' >> "$T/draft"
blob=$(git hash-object -w "$T/draft")
GIT_INDEX_FILE="$stage" git update-index --cacheinfo "100644,$blob,$ID.yaml"
cp "$stage" "$T/stage"
run ait contract commit -m 'NUL staged blob'
check_rc "commit retains the staged blob length and refuses its raw NUL" 128
check "the corrupt staged blob is preserved for repair" "cmp -s $stage $T/stage"
GIT_INDEX_FILE="$stage" git update-index --cacheinfo "100644,1111111111111111111111111111111111111111,$ID.yaml"
cp "$stage" "$T/stage"
run ait contract commit -m 'missing staged blob'
check_rc "a missing staged blob is refused without crashing" 128
check "the missing-blob refusal identifies the contract stage entry" "has err \"could not read '$ID.yaml' from the contract stage\" && cmp -s $stage $T/stage"
rm -f "$stage"

run ait future demand src/order.c src/stock.c cond.yaml -m 'result target'
RESULT_D=$(newest D)
run ait fulfill "$RESULT_D" -m 'results needed'
result=$(pass_all)
printf '\000ignored\n' >> "$result"
run ait fulfill "$RESULT_D" -r "$result" -m 'NUL results'
check_rc "a result file with raw NUL is refused before any fulfillment" 128
check "the result parser identifies its actual source path" "grep -Fq \"bad format: '$result': a NUL character\" $T/err"

mkdir -p specs/environments
printf 'cwd: .\n\000ignored: bytes\n' > specs/environments/raw-nul.yaml
git add specs/environments/raw-nul.yaml
git commit -qm 'environment source'
printf 'body: condition\nengine: exec\ncommand: [sh, -c, "exit 0"]\nenvironment: raw-nul\n' > env-cond.yaml
run ait future demand src/order.c src/stock.c env-cond.yaml -m 'environment target'
ENV_D=$(newest D)
run ait fulfill "$ENV_D" -m 'NUL environment'
check_rc "an environment source with raw NUL is refused before embedding its text" 128
check "the environment parser identifies its source file" "has err \"bad format: 'specs/environments/raw-nul.yaml': a NUL character\""

printf 'prefix\000suffix\n' > "$T/message"
run ait future demand src/order.c src/stock.c cond.yaml -F "$T/message"
check_rc "a message file containing NUL is refused as Git refuses commit log NUL" 1
check "the message is refused instead of silently truncating it" "has err 'NUL byte in commit log message'"

done_testing
