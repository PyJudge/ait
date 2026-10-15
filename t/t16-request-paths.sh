#!/bin/sh
# spec 2.1.3, 6.5.3-4 and 6.5.6: Git path lists preserve valid group bytes.
. "$(dirname "$0")/lib.sh"
setup

for group in 'a"b' 'a\b' '한글'
do
	run ait contract new demand "$group"
	P=$(sed -n 's/^path //p' "$T/out")
	D=$(basename "$P" .yaml)
	sed -i.bak 's#consumer: "<scope>"#consumer: "src/order.c"#; s#provider: "<scope>"#provider: "src/stock.c"#; s#<condition>#staged group#' "$P"
	run ait contract add "$P"
	check_rc "add accepts the valid group '$group'" 0
	run ait contract commit -m staged
	check_rc "commit preserves the staged group '$group'" 0
	check "the staged bytes reach '$group/$D.yaml'" "git cat-file -e 'ait/master:$group/$D.yaml' 2>/dev/null"
	rm -f "$(git rev-parse --absolute-git-dir)/ait/index" "$(git rev-parse --absolute-git-dir)/ait/index.lock"
done

# Both sides edit each of three valid paths; merge-tree must report their bytes.
for group in 'a"b' 'a\b' '한글'
do
	run ait future demand src/order.c src/stock.c cond.yaml -g "$group" -m shared
	printf '%s %s\n' "$(newest D)" "$group" >> "$T/shared"
done
ait contract push > /dev/null 2>&1
git clone -q "$T/origin.git" "$T/c"
cd "$T/c"
ait init > /dev/null
ait session open --agent bob > /dev/null
while read -r id group
do
	ait future amend "$id" src/order.c:2-4 - - -m remote > /dev/null
done < "$T/shared"
ait contract push > /dev/null 2>&1
cd "$T/r"
git branch -q --set-upstream-to=origin/ait/master ait/master
while read -r id group
do
	ait future amend "$id" src/order.c:5-6 - - -m local > /dev/null
done < "$T/shared"
tip=$(git rev-parse ait/master)
# A real merge-tree failure must remain a Git failure, never an empty path list.
racing_git
id=$(sed -n '1s/ .*//p' "$T/shared")
blob=$(git rev-parse "ait/master:a\"b/$id.yaml")
blobfile="$(git rev-parse --absolute-git-dir)/objects/$(printf '%s' "$blob" | cut -c1-2)/$(printf '%s' "$blob" | cut -c3-)"
printf 'mv "%s" "%s"\n' "$blobfile" "$T/removed-blob" > "$T/before-merge-tree"
run env PATH="$T/bin:$PATH" ait contract pull
check_rc "a native merge failure is a fatal Git error" 128
check "a native merge failure identifies Git instead of crashing" "has err \"fatal: git failed: 'git merge-tree':\""
check "a native merge failure keeps the contract tip and stage" "test \"\$(git rev-parse ait/master)\" = $tip && ! test -e \"\$(git rev-parse --absolute-git-dir)/ait/index\""
mv "$T/removed-blob" "$blobfile"
run ait contract pull
check_rc "conflicting paths refuse a contract pull" 1
while read -r id group
do
	if grep -Fq "$group/$id.yaml" "$T/err"
	then say_ok "pull names the conflicting path '$group/$id.yaml'"
	else say_fail "pull names the conflicting path '$group/$id.yaml'"
	fi
done < "$T/shared"
check "a refused pull keeps the contract tip" "test \"\$(git rev-parse ait/master)\" = $tip"

# A result path printed as a shell word must survive an apostrophe in the repository path.
cp -R "$T/r" "$T/repo'quoted"
cd "$T/repo'quoted"
ait session open --agent charlie > /dev/null
id=$(sed -n '1s/ .*//p' "$T/shared")
run ait fulfill "$id" -m check
check_rc "a contract still asks for its results in a quoted repository path" 1
result=$(sed -n 's/^result //p' "$T/out")
rerun=$(sed -n 's/^hint: .*rerun with //p' "$T/err")
printf '%s\n' "set -- $rerun" 'test "$#" = 2 && test "$1" = -r && test "$2" = "$result"' > "$T/replay"
if result="$result" sh "$T/replay" > "$T/replay.out" 2> "$T/replay.err"
then say_ok "the result hint quotes its complete path as one shell word"
else say_fail "the result hint quotes its complete path as one shell word"
fi

done_testing
