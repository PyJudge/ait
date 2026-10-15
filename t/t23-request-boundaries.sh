#!/bin/sh
# spec 3.2, 6.4, 6.7 and 7: request grammar, scopes and supplied result boundaries.
. "$(dirname "$0")/lib.sh"
setup

# Every fulfilling helper pins newly supplied ranges at the staged code.
for verb in demand offer amend; do
	path=src/index-$verb.c
	printf 'old\n' > "$path"
	git add "$path" && git commit -qm 'one line at HEAD'
	if [ "$verb" = amend ]; then
		run ait future demand src/order.c "$path" cond.yaml -m 'future amendment'
		D=$(newest D)
	fi
	printf 'old\nadded\nlast\n' > "$path"
	git add "$path"
	blob=$(git rev-parse ":$path")
	case $verb in
		demand) run ait demand src/order.c "$path:2-3" cond.yaml --force -m 'staged demand range' ;;
		offer) run ait offer "$path:2-3" cond.yaml --force -m 'staged offer range' ;;
		amend) run ait amend "$D" - "$path:2-3" - --force -m 'staged amended range' ;;
	esac
	check_rc "$verb pins its fulfilling range at the index (3.2)" 0
	if [ "$verb" != amend ]; then D=$(newest '[DO]'); fi
	check "$verb stores the staged range base and fulfills it" \
		"test -n '$D' && yaml '$D' | grep -Fq '$path:2-3 $blob' && has out '^fulfilled $D$'"
	git reset -q --hard HEAD
done
printf 'new staged file\n' > src/index-new.c
git add src/index-new.c
run ait offer src/index-new.c cond.yaml --force -m 'fulfill newly staged provider'
check_rc 'a fulfilling helper accepts a file present only in the index' 0
check 'a fulfilling helper commits the newly staged provider' "git cat-file -e HEAD:src/index-new.c"
git reset -q --hard HEAD

# Future clauses continue to use HEAD, independently of uncommitted code changes.
printf 'first\nsecond\nthird\n' > src/future.c
git add src/future.c && git commit -qm 'future range base'
blob=$(git rev-parse HEAD:src/future.c)
printf 'first\n' > src/future.c
git add src/future.c
run ait future offer src/future.c:2-3 cond.yaml -m 'future uses HEAD'
F=$(newest O)
check_rc 'a future helper pins at HEAD even when the index is shorter' 0
check 'the future range records the HEAD blob' "yaml '$F' | grep -Fq 'src/future.c:2-3 $blob'"
git reset -q --hard HEAD

# The amend keep-field marker is not a scope for new contracts or fulfill.
run ait future demand src/order.c src/stock.c cond.yaml -m 'boundary target'
D=$(newest D)
ait fulfill "$D" --force -m current >/dev/null 2>&1
run ait future drop "$D" -m 'boundary drop'
X=$(newest X)
for bad in new-offer new-consumer drop-rescope keep-rescope; do
	tip=$(git rev-parse ait/master)
	case $bad in
		new-offer) run ait future offer - cond.yaml -m invalid ;;
		new-consumer) run ait future demand - src/stock.c cond.yaml -m invalid ;;
		drop-rescope) run ait fulfill "$X=src/stock.c" -m invalid ;;
		keep-rescope) run ait fulfill "$D=-" --force -m invalid ;;
	esac
	check_rc "$bad is rejected as usage before a request is applied" 129
	check "$bad leaves the contract branch unchanged" "test \"\$(git rev-parse ait/master)\" = $tip"
	git update-ref refs/heads/ait/master "$tip"
done
# The explicitly selected contracts form a union, so repetition records K once.
run ait fulfill "$D" "$D" --force -m 'repeated selection'
check_rc 'repeated fulfill IDs retain union semantics (6.7.1)' 0
check 'the repeated ID is fulfilled once' "test \"\$(grep -c '^fulfilled $D$' $T/out)\" = 1"
# Repeated messages remain paragraphs.
run ait future offer src/stock.c cond.yaml -m first -m second
check_rc 'multiple message options remain valid paragraphs (4.5)' 0
check 'both message paragraphs are committed' "git log -1 --format=%B ait/master | grep -qx second"

# Supplied results are checked even when a drop has no spec tasks.
cid=$(yaml "$X" | sed -n 's/^condition-id: //p')
for bad in stale missing-results; do
	tip=$(git rev-parse ait/master)
	if [ "$bad" = stale ]; then
		printf 'condition-ids:\n  %s: R000000000000\nresults: []\n' "$X" > "$T/result.yaml"
		want=1
	else
		printf 'condition-ids:\n  %s: %s\n' "$X" "$cid" > "$T/result.yaml"
		want=128
	fi
	run ait fulfill "$X" -r "$T/result.yaml" -m 'invalid result'
	check_rc "a drop rejects supplied $bad results" "$want"
	check "rejected $bad results do not withdraw the target" "test \"\$(git rev-parse ait/master)\" = $tip"
	git update-ref refs/heads/ait/master "$tip"
done

# Result collections retain their schema; a numeric map is not a sequence.
run ait future demand src/order.c src/stock.c cond.yaml -m 'result boundary target'
R=$(newest D)
cid=$(yaml "$R" | sed -n 's/^condition-id: //p')
for bad in root-key task-key results-map reason-list; do
	printf 'condition-ids:\n  %s: %s\nresults:\n  - task: %s:1\n    outcome: pass\n    basis: checked\n' "$R" "$cid" "$R" > "$T/result.yaml"
	case $bad in
		root-key) echo 'surprise: accepted' >> "$T/result.yaml" ;;
		task-key) echo '    surprise: accepted' >> "$T/result.yaml" ;;
		results-map) sed -i.bak 's/  - task:/  0:\n    task:/' "$T/result.yaml" ;;
		reason-list) echo '    reason: [one, two]' >> "$T/result.yaml" ;;
	esac
	tip=$(git rev-parse ait/master)
	run ait fulfill "$R" -r "$T/result.yaml" -m 'invalid result shape'
	check_rc "$bad is a result format error" 128
	check "$bad cannot create an accepted fulfillment" "test \"\$(git rev-parse ait/master)\" = $tip"
	git update-ref refs/heads/ait/master "$tip"
done
printf 'results:\n- task: "+1:1"\n  outcome: pass\n  basis: checked\n' > "$T/result.yaml"
run ait offer src/stock.c cond.yaml -r "$T/result.yaml" -m 'missing condition IDs'
check_rc 'a new-only request still requires the condition-ids field' 128
# A condition-id mismatch precedes task-content validation (7.3).
printf 'condition-ids:\n  %s: R000000000000\nresults:\n- task: wrong\n  outcome: invalid\n' "$R" > "$T/result.yaml"
run ait fulfill "$R" -r "$T/result.yaml" -m 'stale results'
check_rc 'stale condition IDs request fresh results before task validation' 1

# remote_of must pass the complete format argument and the intended branch.
git init -q --bare "$T/chosen.git"
git remote remove origin
git remote add chosen "$T/chosen.git"
git config branch.ait/master.remote chosen
git config branch.ait/master.merge refs/heads/ait/master
run ait contract push
check_rc 'contract push uses the configured non-origin remote' 0
check 'the configured remote receives the contract branch' \
	"test \"\$(git --git-dir=$T/chosen.git rev-parse refs/heads/ait/master)\" = \"\$(git rev-parse ait/master)\""
run ait contract pull
check_rc 'contract pull uses the configured non-origin upstream remote' 0

done_testing
