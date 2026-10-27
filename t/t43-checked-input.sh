#!/bin/sh
# spec 4.3: read commands consume the same contract snapshot they checked.
. "$(dirname "$0")/lib.sh"
setup
printf 'body: |\n  the old checked body\n' >cond.yaml
run ait future offer src/order.c cond.yaml -m old
OLD=$(newest O)
oldtip=$(git rev-parse ait/master)
git show "$oldtip:$OLD.yaml" >"$T/old-bytes"
printf 'body: |\n  the new unchecked body\n' >cond.yaml
run ait future offer src/order.c cond.yaml -m new
NEW=$(newest O)
GIT_INDEX_FILE="$T/changed-index" git read-tree ait/master
GIT_INDEX_FILE="$T/changed-index" git update-index --force-remove "$OLD.yaml"
blob=$(printf 'not a contract\n' | git hash-object -w --stdin)
GIT_INDEX_FILE="$T/changed-index" git update-index --add --cacheinfo "100644,$blob,O000000000000.yaml"
tree=$(GIT_INDEX_FILE="$T/changed-index" git write-tree)
newtip=$(printf 'different contracts and malformed entry\n' | git commit-tree -p "$oldtip" "$tree")
racing_git
session_file=$(git rev-parse --git-common-dir)/ait/sessions/$ALICE
race() {
	git update-ref refs/heads/ait/master "$oldtip"
	rm -f "$T/before-ls-tree.done"
	printf '%s\n' "git update-ref refs/heads/ait/master $newtip" >"$T/before-ls-tree"
	run env PATH="$TP/bin:$PATH" "$@"
}

race ait status src/order.c
check_rc 'status consumes the branch object checked at start' 0
check 'status positively displays the checked ID and body' \
	"has out '^id $OLD$' && has out '^spec prompt the old checked body$' && ! has out '^id $NEW$'"
check 'the status race really moved the live branch after its OIDs were read' \
	"test -e '$T/before-ls-tree.done' && test \"\$(git rev-parse ait/master)\" = $newtip"
run ait status src/order.c
check_rc 'a fresh status can warn and read the newer snapshot' 0
check 'the fresh control displays the newer ID/body and malformed warning' \
	"has out '^id $NEW$' && has out '^spec prompt the new unchecked body$' && has err 'bad format' && ! has out '^id $OLD$'"

race ait contract checkout "$OLD"
check_rc 'draft checkout consumes the checked snapshot' 0
check 'draft checkout writes exactly the checked contract bytes' \
	"has out '^path .ait/$OLD.yaml$' && cmp -s '.ait/$OLD.yaml' '$T/old-bytes'"
run ait contract checkout "$NEW"
check_rc 'fresh draft checkout rejects the newer malformed snapshot' 128
check 'fresh checkout reports the malformed contract before its own checks' "has err 'bad format'"

race ait sub "$OLD"
check_rc 'subscription ID validation consumes the checked snapshot' 0
check 'subscription saves and reports the checked ID' "has out '^sub id $OLD$' && grep -qx 'sub id $OLD' '$session_file'"
ait unsub "$OLD" >/dev/null
run ait sub "$NEW"
check_rc 'a fresh subscription rejects the newer malformed snapshot' 128
check 'a refused fresh subscription is not saved' "! grep -qx 'sub id $NEW' '$session_file'"

git update-ref refs/heads/ait/master "$oldtip"
rm -f "$T/before-ls-tree.done"
printf '%s\n' "git update-ref refs/heads/ait/master $newtip" >"$T/before-ls-tree"
env PATH="$TP/bin:$PATH" ait listen "$OLD" >"$T/out" 2>"$T/err" &
listener=$!
for attempt in $(seq 1 100); do
	grep -qx "sub id $OLD" "$session_file" && break
	kill -0 "$listener" 2>/dev/null || break
	sleep .05
done
check 'listen initial subscription uses the checked ID and reports it' \
	"grep -qx 'sub id $OLD' '$session_file' && has err '^sub id $OLD$'"
kill -TERM "$listener" 2>/dev/null || :
wait "$listener" 2>/dev/null || :
ait unsub "$OLD" >/dev/null
run ait listen "$NEW"
check_rc 'a fresh listen rejects the newer malformed snapshot' 128
check 'a refused fresh listen does not subscribe the newer ID' "! grep -qx 'sub id $NEW' '$session_file'"

# Writers still capture their own T later; deletion after that capture is E_GIT.
git update-ref refs/heads/ait/master "$oldtip"
printf '%s\n' 'git update-ref -d refs/heads/ait/master' >"$T/before-update-ref"
run env PATH="$TP/bin:$PATH" ait future offer src/order.c cond.yaml -m late-delete
check_rc 'deletion just before CAS remains a Git failure' 128
check 'late deletion does not recreate the branch or print success' \
	"! git show-ref --verify --quiet refs/heads/ait/master && ! has out '^new ' && has err 'git failed'"
done_testing
