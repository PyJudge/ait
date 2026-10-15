#!/bin/sh
# spec 2.1.3, 2.5, 5.3: parse stored code facts once, with their exact schema.
. "$(dirname "$0")/lib.sh"
setup
branch_file() {
	GIT_INDEX_FILE="$T/i" git read-tree "$valid"
	blob=$(git hash-object -w "$2")
	GIT_INDEX_FILE="$T/i" git update-index --add --cacheinfo "100644,$blob,$1"
	tree=$(GIT_INDEX_FILE="$T/i" git write-tree)
	commit=$(printf 'outside ait\n' | git commit-tree -p "$valid" "$tree")
	git update-ref refs/heads/ait/master "$commit"
}
run ait future demand src/order.c src/stock.c cond.yaml -m baseline
D=$(newest D)
ait fulfill "$D" --force -m checked >/dev/null 2>&1
valid=$(git rev-parse ait/master)
f=$(yaml "$D" | sed -n 's/^- "\(.*\)"$/\1/p')
git show "ait/master:$f" > "$T/original"
for kind in wrong-type wrong-mode nonhex-commit nonhex-oid wrong-outcome scalar-code null-code list-code nested-code trailing-code file-with-span abstract-key bad-placement wrong-filename-id; do
	cp "$T/original" "$T/changed"
	path=$f
	case $kind in
	wrong-type) sed -i.bak 's/100644 blob/100644 commit/' "$T/changed" ;;
	wrong-mode) sed -i.bak 's/100644 blob/100664 blob/' "$T/changed" ;;
	nonhex-commit) sed -i.bak 's/^commit: .*/commit: gggggggggggggggggggggggggggggggggggggggg/' "$T/changed" ;;
	nonhex-oid) sed -i.bak 's/100644 blob [^"]*/100644 blob gggggggggggggggggggggggggggggggggggggggg/' "$T/changed" ;;
	wrong-outcome) sed -i.bak 's/^outcome: .*/outcome: withdrawn/' "$T/changed" ;;
	scalar-code) sed -i.bak 's/^code:.*/code: "{}"/; /^  /d' "$T/changed" ;;
	null-code) sed -i.bak 's/^code:.*/code:/; /^  /d' "$T/changed" ;;
	list-code) sed -i.bak 's/^code:.*/code: []/; /^  /d' "$T/changed" ;;
	nested-code) sed -i.bak 's/^  "src\/stock.c": .*/  "src\/stock.c": {unexpected: value}/' "$T/changed" ;;
	trailing-code) sed -i.bak 's/100644 blob \([^"]*\)/100644 blob \1 extra/' "$T/changed" ;;
	file-with-span) sed -i.bak 's/100644 blob \([^"]*\)/100644 blob \1 1-2/' "$T/changed" ;;
	abstract-key) sed -i.bak 's/"src\/stock.c":/"new:src\/stock.c":/' "$T/changed" ;;
	bad-placement) path=fulfill/not-an-id.md ;;
	wrong-filename-id) path=fulfill/D000000000000-R000000000000.md ;;
	esac
	branch_file "$path" "$T/changed"
	tip=$(git rev-parse ait/master)
	run ait future offer src/stock.c cond.yaml -m blocked
	check_rc "a $kind fulfillment is a format error at the command boundary" 128
	run ait status src/
	check "the $kind refusal identifies its source and preserves the tip" \
		"grep -Fq \"bad format: '$path' at 'ait/master':\" $T/err && test \"\$(git rev-parse ait/master)\" = $tip"
done

# Commit and code OIDs are stored facts; missing objects do not invalidate their syntax.
sed 's/^commit: .*/commit: 1111111111111111111111111111111111111111/' "$T/original" > "$T/changed"
branch_file "$f" "$T/changed"
run ait status src/
check "a syntactically valid pruned commit OID does not make the record malformed" \
	"! has err 'bad format:' && grep -A3 '^id $D$' $T/out | grep -qx 'state current'"
git update-ref refs/heads/ait/master "$valid"

# Normal executable, symlink, gitlink and ranged entries retain their stored semantics.
ln -s stock.c src/link
chmod +x src/order.c
git add src/order.c src/link
git update-index --add --cacheinfo "160000,$(git rev-parse HEAD),src/module"
git commit -qm 'code entry kinds'
for scope in src/order.c src/link src/module src/stock.c:1-2; do
	run ait future offer "$scope" cond.yaml -m 'typed code entry'
	O=$(newest O)
	run ait fulfill "$O" --force -m checked
	check_rc "a valid $scope code entry is recorded" 0
	run ait status src/
	check "a valid $scope code entry remains current" "grep -A3 '^id $O$' $T/out | grep -qx 'state current'"
done

# Drop records have their own outcome and never record provider code.
run ait future drop "$D" -m 'drop format target'
X=$(newest X)
ait fulfill "$X" -m withdrawn >/dev/null 2>&1
valid=$(git rev-parse ait/master)
xf=$(yaml "$X" | sed -n 's/^- "\(.*\)"$/\1/p')
git show "ait/master:$xf" > "$T/drop-original"
for kind in drop-outcome drop-code; do
	cp "$T/drop-original" "$T/changed"
	if [ "$kind" = drop-outcome ]; then
		sed -i.bak 's/^outcome: .*/outcome: pass/' "$T/changed"
	else
		sed -i.bak 's/^code: {}/code: {"src\/stock.c": "100644 blob 1111111111111111111111111111111111111111"}/' "$T/changed"
	fi
	branch_file "$xf" "$T/changed"
	run ait future offer src/stock.c cond.yaml -m blocked
	check_rc "a $kind record is a format error" 128
	run ait status src/
	check "the malformed $kind file is identified" "grep -Fq \"bad format: '$xf' at 'ait/master':\" $T/err"
done

done_testing
