#!/bin/sh
# spec 4.8, 6.4.2: schema reasons retain their source node's file line.
. "$(dirname "$0")/lib.sh"
setup

at_line() {
	line=$(grep -F "bad format: '$2'" "$T/err" | head -1)
	if printf '%s\n' "$line" | grep -Eq "in line $3$" &&
	   ! printf '%s\n' "$line" | grep -Eq 'in line .*in line '; then
		say_ok "$1"
	else
		say_fail "$1 (want line $3)"
	fi
}
cat > "$T/spec" <<'YAML'
# source positions

body: condition
engine: exec
command: [sh, -c, "exit 0"]
success: [0]
environment: node
YAML
for defect in body engine command command-entry success success-entry environment prompt missing-body unknown syntax; do
	cp "$T/spec" bad.yaml
	case $defect in
	body) sed -i.bak 's/^body:.*/body: []/' bad.yaml; nline=3 ;;
	engine) sed -i.bak 's/^engine:.*/engine: invalid/' bad.yaml; nline=4 ;;
	command) sed -i.bak 's/^command:.*/command: scalar/' bad.yaml; nline=5 ;;
	command-entry) sed 's/^command:.*/command:\n- []/' "$T/spec" > bad.yaml; nline=6 ;;
	success) sed -i.bak 's/^success:.*/success:/' bad.yaml; nline=6 ;;
	success-entry) sed 's/^success:.*/success:\n- invalid/' "$T/spec" > bad.yaml; nline=7 ;;
	environment) sed -i.bak 's/^environment:.*/environment: {bad: value}/' bad.yaml; nline=7 ;;
	prompt) sed -i.bak 's/^engine:.*/engine:/' bad.yaml; nline=5 ;;
	missing-body) sed -i.bak '/^body:/d' bad.yaml; nline=3 ;;
	unknown) printf 'unknown: value\n' >> bad.yaml; nline=8 ;;
	syntax) printf 'engine: exec\n' >> bad.yaml; nline=8 ;;
	esac
	run ait future offer src/stock.c bad.yaml -m "bad spec $defect"
	check_rc "a spec $defect remains a format refusal" 128
	at_line "a spec $defect points to its node exactly once" bad.yaml "$nline"
done

P=.ait/O111111111111.yaml
mkdir -p .ait
cat > "$T/contract" <<'YAML'
# source positions
condition-id: ""
change-id: ""
kind: offer
creator: ""
provider: "src/stock.c"
state: future
specs:
- body: condition
  engine: exec
  command: [sh, -c, "exit 0"]
  success: [0]
undertakes: []
undertakes-partially: []
fulfilled:
YAML
for defect in id creator state scope empty-scope missing-key undertaking fulfilled specs nested missing-body missing-command; do
	cp "$T/contract" "$P"
	case $defect in
	id) sed -i.bak 's/^change-id:.*/change-id: invalid/' "$P"; nline=3 ;;
	creator) sed -i.bak 's/^creator:.*/creator: []/' "$P"; nline=5 ;;
	state) sed -i.bak 's/^state:.*/state: invalid/' "$P"; nline=7 ;;
	scope) sed -i.bak 's#^provider:.*#provider: "src/stock.c:bad"#' "$P"; nline=6 ;;
	empty-scope) sed -i.bak 's/^provider:.*/provider: ""/' "$P"; nline=6 ;;
	missing-key) sed -i.bak '/^creator:/d' "$P"; nline=2 ;;
	undertaking) sed -i.bak 's/^undertakes:.*/undertakes: [bad]/' "$P"; nline=13 ;;
	fulfilled) printf '%s\n' '- "bad/path"' >> "$P"; nline=16 ;;
	specs) sed -i.bak '/^specs:/,$d' "$P"; printf 'specs: []\nundertakes: []\nundertakes-partially: []\nfulfilled:\n' >> "$P"; nline=8 ;;
	nested) sed -i.bak 's/^  engine:.*/  engine: invalid/' "$P"; nline=10 ;;
	missing-body) awk 'NR == 9 { print "- engine: exec" } NR != 9 && NR != 10 { print }' "$T/contract" > "$P"; nline=9 ;;
	missing-command) sed -i.bak '/^  command:/d' "$P"; nline=9 ;;
	esac
	run ait contract add "$P"
	check_rc "a draft $defect remains a format refusal" 128
	at_line "a draft $defect retains the responsible field or parent line" O111111111111.yaml "$nline"
done

run ait future demand src/order.c src/stock.c cond.yaml -m 'fulfilment lines'
D=$(newest D)
ait fulfill "$D" --force -m checked >/dev/null 2>&1
valid=$(git rev-parse ait/master)
F=$(yaml "$D" | sed -n 's/^- "\(.*\)"$/\1/p')
git show "ait/master:$F" > "$T/fulfilment"
branch_entry() {
	GIT_INDEX_FILE="$T/branch-index" git read-tree "$valid"
	GIT_INDEX_FILE="$T/branch-index" git update-index --add --cacheinfo "$2,$3,$1"
	tree=$(GIT_INDEX_FILE="$T/branch-index" git write-tree --missing-ok)
	changed=$(printf 'outside ait\n' | git commit-tree -p "$valid" "$tree")
	git update-ref refs/heads/ait/master "$changed"
}
for defect in commit outcome code-entry unknown syntax missing-key; do
	cp "$T/fulfilment" "$T/changed"
	case $defect in
	commit) sed -i.bak 's/^commit:.*/commit: invalid/' "$T/changed"; nline=6 ;;
	outcome) sed -i.bak 's/^outcome:.*/outcome: withdrawn/' "$T/changed"; nline=7 ;;
	code-entry) sed -i.bak 's/100644 blob/100664 blob/' "$T/changed"; nline=9 ;;
	unknown) awk 'NR == 10 { print "unknown: value" } { print }' "$T/fulfilment" > "$T/changed"; nline=10 ;;
	syntax) awk 'NR == 3 { print "contract: duplicate" } { print }' "$T/fulfilment" > "$T/changed"; nline=3 ;;
	missing-key) sed -i.bak '/^by:/d' "$T/changed"; nline=2 ;;
	esac
	obj=$(git hash-object -w "$T/changed")
	branch_entry "$F" 100644 "$obj"
	run ait status src/
	at_line "a fulfilment $defect uses its original file line" "$F" "$nline"
done
git update-ref refs/heads/ait/master "$valid"

# Late attachment validation keeps the parsed row position, too.
yaml "$D" > "$T/attached"
nline=$(grep -nF "\"$F\"" "$T/attached" | cut -d: -f1)
sed "s#\"$F\"#\"fulfill/$D-R000000000000.md\"#" "$T/attached" > "$T/missing-attachment"
branch_entry "$D.yaml" 100644 "$(git hash-object -w "$T/missing-attachment")"
run ait status src/
at_line "a missing attachment retains its contract list-item line" "$D.yaml" "$nline"

# File-level failures have line one; known byte offsets retain their actual line.
printf 'first\nsecond\n\000tail\n' > "$T/nul-bytes"
branch_entry O222222222222.yaml 100644 "$(git hash-object -w "$T/nul-bytes")"
run ait status src/
at_line "the byte boundary locates a raw NUL in the complete blob" O222222222222.yaml 3
branch_entry not-a-blob.yaml 160000 "$(git rev-parse HEAD)"
run ait status src/
at_line "a non-blob tree entry is a whole-file error" not-a-blob.yaml 1
branch_entry O333333333333.yaml 100644 1111111111111111111111111111111111111111
run ait status src/
at_line "an unavailable blob is a whole-file error" O333333333333.yaml 1
sed '/^- "/d' "$T/attached" > "$T/duplicate"
branch_entry "dup/$D.yaml" 100644 "$(git hash-object -w "$T/duplicate")"
run ait status src/
at_line "duplicate ID placement is a whole-file error" "dup/$D.yaml" 1
git update-ref refs/heads/ait/master "$valid"

mkdir -p .ait/one .ait/two
cp "$T/contract" .ait/one/O111111111111.yaml
cp "$T/contract" .ait/two/O111111111111.yaml
ait contract add .ait/one/O111111111111.yaml .ait/two/O111111111111.yaml >/dev/null
run ait contract commit -m 'staged duplicate positions'
check_rc "staged duplicate IDs still fail as format" 128
at_line "staged duplicate ID placement uses the same whole-file boundary" two/O111111111111.yaml 1

done_testing
