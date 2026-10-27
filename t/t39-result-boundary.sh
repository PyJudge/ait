#!/bin/sh
# spec 6.7.5-0, 7.3 and v14 8.4: input identity, provider, then task results.
. "$(dirname "$0")/lib.sh"
setup

result_ids() { printf 'condition-ids:\n  %s: %s\nresults: []\n' "$1" "$2" > "$T/result.yaml"; }
no_template() { ! test -e .git/ait/check-request.yaml && ! test -e .git/ait/check-result.yaml && ! has out '^task '; }

for kind in abstract missing deleted; do
	case $kind in
	abstract) scope=/ ;;
	missing) scope=src/stock.c ;;
	deleted) scope=src/stock.c:5-9 ;;
	esac
	run ait future offer "$scope" cond.yaml -m "$kind provider"
	K=$(newest O)
	cid=$(yaml "$K" | sed -n 's/^condition-id: //p')
	case $kind in
	missing) git rm -q --cached src/stock.c ;;
	deleted) sed '5,9d' src/stock.c > "$T/short"; cp "$T/short" src/stock.c; git add src/stock.c ;;
	esac
	code=$(git rev-parse HEAD)
	tip=$(git rev-parse ait/master)
	rm -f .git/ait/check-request.yaml .git/ait/check-result.yaml
	result_ids "$K" R000000000000
	run ait fulfill "$K" -r "$T/result.yaml" -m 'stale input'
	check_rc "stale condition IDs precede the $kind provider error" 1
	check "stale $kind input reports results and creates no unusable template" "has err '^error: check results required:' && ! has err '^fatal:' && no_template"
	check "stale $kind input changes neither code nor contract commits" "test \"\$(git rev-parse HEAD)\" = $code && test \"\$(git rev-parse ait/master)\" = $tip"
	result_ids "$K" "$cid"
	run ait fulfill "$K" -r "$T/result.yaml" -m 'matching input with missing task'
	check_rc "matching condition IDs leave the $kind provider error before missing task validation" 128
	check "the matching $kind result identifies its provider" "has err '^fatal: bad scope:'"
	printf 'results: []\n' > "$T/result.yaml"
	run ait fulfill "$K" -r "$T/result.yaml" -m 'missing result header'
	check "the mandatory input header precedes the $kind provider check" "test $rc = 128 && has err \"'condition-ids' is missing or not a map\" && ! has err '^hint:'"
	git restore --source=HEAD --staged --worktree src/stock.c
done

# A helper's pre-commit hook leaves the initially pinned blob unreachable.
sed -i.bak 's/^s40$/transient/' src/stock.c
rm src/stock.c.bak
git add src/stock.c
anchor=$(git rev-parse :src/stock.c)
cat > .git/hooks/pre-commit <<'HOOK'
#!/bin/sh
echo tail >> src/stock.c
git add src/stock.c
HOOK
chmod +x .git/hooks/pre-commit
run ait offer src/stock.c:1-2 cond.yaml --force -m 'record a different blob'
check_rc 'the no-base fixture records code after the hook' 0
K=$(newest O)
rm .git/hooks/pre-commit
git gc --prune=now
check 'the fixture loses its original scope base' "! git cat-file -e $anchor 2>/dev/null"
rm -f .git/ait/check-request.yaml .git/ait/check-result.yaml
result_ids "$K" R000000000000
run ait fulfill "$K" -r "$T/result.yaml" -m 'stale without base'
check_rc 'stale IDs also precede a collected scope base' 1
check 'a collected base cannot replace the stale-result error or write a template' "has err '^error: check results required:' && ! has err '^fatal:' && no_template"

# An unusable environment cannot turn the already established stale result
# into a different error while trying to regenerate the template.
mkdir -p specs/environments
for kind in missing malformed; do
	printf 'body: check\nengine: exec\ncommand: [true]\nenvironment: %s\n' "$kind" > env-cond.yaml
	if [ "$kind" = malformed ]; then
		printf 'cwd: []\n' > specs/environments/malformed.yaml
		git add specs && git commit -qm 'malformed environment fixture'
	fi
	run ait future offer src/stock.c env-cond.yaml -m "$kind environment"
	K=$(newest O)
	rm -f .git/ait/check-request.yaml .git/ait/check-result.yaml
	result_ids "$K" R000000000000
	run ait fulfill "$K" -r "$T/result.yaml" -m 'stale before environment'
	check_rc "a $kind environment cannot override stale condition IDs" 1
	check "the $kind environment leaves no partial task output or template" "has err '^error: check results required:' && ! has err '^fatal:' && no_template"
	run ait fulfill "$K" -m 'request a usable template'
	check_rc "without a stale result, a $kind environment is still rejected" 128
	check "strict template preparation identifies the $kind environment" "has err \"specs/environments/$kind.yaml\""
done

run ait future offer src/stock.c cond.yaml -m 'task error locations'
K=$(newest O)
cid=$(yaml "$K" | sed -n 's/^condition-id: //p')
for kind in unknown duplicate outcome missing-outcome basis missing-basis missing-task; do
	printf '# result fixture\ncondition-ids:\n  %s: %s\nresults:\n- task: %s:1\n  outcome: pass\n  basis: checked\n' "$K" "$cid" "$K" > "$T/result.yaml"
	case $kind in
	unknown) sed -i.bak 's/^- task:.*/- task: unknown/' "$T/result.yaml"; line=5 ;;
	duplicate) printf '%s\n' "- task: $K:1" '  outcome: pass' '  basis: repeated' >> "$T/result.yaml"; line=8 ;;
	outcome) sed -i.bak 's/outcome: pass/outcome: invalid/' "$T/result.yaml"; line=6 ;;
	missing-outcome) sed -i.bak '/outcome:/d' "$T/result.yaml"; line=5 ;;
	basis) sed -i.bak 's/basis: checked/basis:/' "$T/result.yaml"; line=7 ;;
	missing-basis) sed -i.bak '/basis:/d' "$T/result.yaml"; line=5 ;;
	missing-task) sed -i.bak 's/^results:.*/results: []/; /^- task:/,$d' "$T/result.yaml"; line=4 ;;
	esac
	run ait fulfill "$K" -r "$T/result.yaml" -m 'invalid task'
	check_rc "$kind task result remains a format error" 128
	check "$kind result identifies its file and source line" "has err \"^fatal: bad format: '$T/result.yaml': .* in line $line$\" && ! has err '^hint:'"
done
# v14 5.8: the advice follows why a task did not pass, and --force only when nothing failed
fail_hint="hint: make the code meet the failed task(s) and stage it, check them again, and rerun with -r $T/result.yaml"
unknown_hint="hint: check the unknown task(s) where their environment is available and rerun with -r $T/result.yaml"
force_hint='hint: if it is trivial, add --force instead (recorded as forced)'
for outcome in fail unknown; do
	printf 'condition-ids:\n  %s: %s\nresults:\n- task: %s:1\n  outcome: %s\n  reason: unavailable\n' "$K" "$cid" "$K" "$outcome" > "$T/result.yaml"
	run ait fulfill "$K" -r "$T/result.yaml" -m 'reported failure'
	check_rc "$outcome remains E_CHECK_FAILED" 1
	check "$outcome names its task" "has err '^$K:1$' && has err '^error: check failed: 1 task(s)$'"
	case $outcome in
	fail) check "a failed task is advised to fix the code, not to force" \
		"grep -qxF -- '$fail_hint' '$T/err' && ! grep -qF -- '$unknown_hint' '$T/err' && ! grep -qxF -- '$force_hint' '$T/err'" ;;
	unknown) check "an unknown task is advised to check where its environment is, or to force" \
		"grep -qxF -- '$unknown_hint' '$T/err' && grep -qxF -- '$force_hint' '$T/err' && ! grep -qF -- '$fail_hint' '$T/err'" ;;
	esac
done
printf 'body: |\n  orders are counted per day\n' > cond-prompt.yaml
run ait future offer src/stock.c cond.yaml,cond-prompt.yaml -m 'two tasks'
K2=$(newest O)
cid2=$(yaml "$K2" | sed -n 's/^condition-id: //p')
printf 'condition-ids:\n  %s: %s\nresults:\n- task: %s:1\n  outcome: fail\n  reason: wrong\n- task: %s:2\n  outcome: unknown\n  reason: unavailable\n' "$K2" "$cid2" "$K2" "$K2" > "$T/result.yaml"
run ait fulfill "$K2" -r "$T/result.yaml" -m 'failed and unknown'
check "a failed and an unknown task get both pieces of advice, and no --force" \
	"has err '^error: check failed: 2 task(s)$' && grep -qxF -- '$fail_hint' '$T/err' && grep -qxF -- '$unknown_hint' '$T/err' && ! grep -qxF -- '$force_hint' '$T/err'"
rm -f cond-prompt.yaml


# Parsing input once fixes the request even when locating a provider runs Git
# after that read, and another process changes the result file at that point.
run ait future offer src/stock.c:1-2 cond.yaml -m 'single result snapshot'
K=$(newest O)
cid=$(yaml "$K" | sed -n 's/^condition-id: //p')
sed -i.bak 's/^s30$/outside/' src/stock.c
rm src/stock.c.bak
git add src/stock.c
printf 'condition-ids:\n  %s: %s\nresults:\n- task: %s:1\n  outcome: pass\n  basis: first snapshot\n' "$K" "$cid" "$K" > "$T/result.yaml"
racing_git
cat > "$T/before-diff" <<'RACE'
printf 'condition-ids: {}\nresults: []\n' > "$T/result.yaml"
RACE
run env PATH="$TP/bin:$PATH" ait fulfill "$K" -r "$T/result.yaml" -m 'one parsed input'
check_rc 'provider location and task validation share the first parsed result' 0
check 'the race ran, but the fulfillment uses the first result body' \
	"test -e $T/before-diff.done && git show ait/master:fulfill/\$(git ls-tree --name-only ait/master fulfill/ | sed -n \"s|fulfill/\\($K-.*\\)|\\1|p\") | grep -q 'first snapshot'"

# Result contents have no second encoded task identity to cut apart. Only +n
# becomes the new ID; all accepted outcome/basis/reason values remain present.
cat > "$T/result.yaml" <<'RESULT'
condition-ids: {}
results:
- task: "+1:1"
  outcome: pass
  basis: |
    first basis line
    second basis line
  reason: |
    first reason line
    second reason line
RESULT
run ait offer src/stock.c cond.yaml -r "$T/result.yaml" -m 'new task identity'
check_rc 'a new contract accepts a result identified by +1' 0
K=$(newest O)
git show "ait/master:fulfill/$(git ls-tree --name-only ait/master fulfill/ | sed -n "s|fulfill/\($K-.*\)|\1|p")" > "$T/fulfillment"
check 'the fulfillment emits the actual task ID once' \
	"test \"\$(grep -c '^- task: $K:1$' $T/fulfillment)\" = 1 && ! grep -q '+1' $T/fulfillment"
check 'the accepted outcome, multiline basis and reason survive task renaming' \
	"grep -qx '  outcome: pass' $T/fulfillment && grep -qx '    first basis line' $T/fulfillment && grep -qx '    second basis line' $T/fulfillment && grep -qx '    first reason line' $T/fulfillment && grep -qx '    second reason line' $T/fulfillment"

done_testing
