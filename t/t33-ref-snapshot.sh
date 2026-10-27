#!/bin/sh
# spec 2.1.2, 4.2, 6.3, 9.1: one classified ref snapshot and actual branch choice.
. "$(dirname "$0")/lib.sh"
setup

# A ref created after branch selection cannot rewrite how that selection occurred.
racing_git
cat > "$T/before-ls-tree" <<'HOOK'
git branch ait/later ait/master
HOOK
run env PATH="$TP/bin:$PATH" AIT_SESSION=- ait status
check_rc "status keeps its selected branch when another ref appears during loading" 0
check "the controlled race really creates the second local contract branch" \
	"git show-ref --verify --quiet refs/heads/ait/later"
check "status describes its actual implicit sole-branch choice" \
	"has out '^On contract branch ait/master (the only one)$'"
git branch -D -q ait/later

run ait checkout ait/master
check_rc "the same branch can be chosen explicitly" 0
run env AIT_SESSION=- ait status
check "an explicit choice does not carry the implicit-choice suffix" \
	"has out '^On contract branch ait/master$'"
rm -f "$(git rev-parse --git-path ait-branch)"

# The longest configured remote prefix determines the name. Here the short
# prefix would invent another name, while the long prefix names local master.
git remote add office "$T/origin.git"
git config remote.office/ait/team.url "$T/origin.git"
git config remote.office/ait/team.fetch '+refs/heads/*:refs/remotes/office/ait/team/*'
ref=refs/remotes/office/ait/team/ait/master
git update-ref "$ref" "$(git rev-parse ait/master)"
git update-ref refs/remotes/not-configured/ait/ghost "$(git rev-parse ait/master)"
git update-ref refs/remotes/office/ordinary "$(git rev-parse HEAD)"
run env AIT_SESSION=- ait status
check_rc "overlapping configured remote names retain the existing longest-prefix rule" 0
check "the local and classified remote master count as one name" \
	"has out '^On contract branch ait/master (the only one)$'"
SID=$(AIT_SESSION=- ait session open --agent snapshot)
snapshot="$(git rev-parse --git-common-dir)/ait/sessions/$SID"
check "the session snapshot includes the same classified long-prefix remote" \
	"grep -qx 'tip [0-9a-f]* $ref' $snapshot"
check "the session snapshot excludes unconfigured and non-contract tracking refs" \
	"! grep -q 'not-configured/ait/ghost' $snapshot && ! grep -q 'office/ordinary' $snapshot"

# A complete ref listing is captured first. Before returning it to ait, change
# the local tip and add a classified remote ref in one reference transaction.
# The stored snapshot must describe either complete state, never a mixture.
snapshot_old=$(git rev-parse refs/heads/ait/master)
snapshot_new=$(printf 'ref snapshot race\n' | git commit-tree -p "$snapshot_old" "$(git rev-parse 'ait/master^{tree}')")
snapshot_late=refs/remotes/office/ait/team/ait/late
snapshot_real_git=$(command -v git)
printf '%s\n' "$snapshot_real_git" > "$T/tip-race-real-git"
printf 'update refs/heads/ait/master %s %s\ncreate %s %s\n' \
	"$snapshot_new" "$snapshot_old" "$snapshot_late" "$snapshot_old" > "$T/tip-race-update"
mkdir -p "$T/tip-race-bin"
: > "$T/tip-race-ready"
cat > "$T/tip-race-bin/git" <<'GIT'
#!/bin/sh
real=$(cat "$T/tip-race-real-git")
for arg; do
	if [ "$arg" = for-each-ref ] && [ -f "$T/tip-race-ready" ]; then
		mv "$T/tip-race-ready" "$T/tip-race-injected"
		"$real" "$@" > "$T/tip-race-first-list"
		rc=$?
		[ "$rc" = 0 ] || exit "$rc"
		"$real" update-ref --stdin < "$T/tip-race-update" || exit $?
		cat "$T/tip-race-first-list"
		exit $?
	fi
done
exec "$real" "$@"
GIT
chmod +x "$T/tip-race-bin/git"
run env PATH="$TP/tip-race-bin:$PATH" AIT_SESSION=- ait session open --agent snapshot-race
check_rc "session open succeeds when refs change after its first complete listing" 0
race_sid=$(cat "$T/out")
race_snapshot="$(git rev-parse --path-format=absolute --git-common-dir)/ait/sessions/$race_sid"
check "the controlled race atomically changes the local tip and adds the remote ref" \
	"test \"\$(git rev-parse refs/heads/ait/master)\" = '$snapshot_new' && test \"\$(git rev-parse $snapshot_late)\" = '$snapshot_old'"
cat "$race_snapshot" > "$T/out"
check "session tips form one coherent classified ref snapshot" \
	"{ grep -qx 'tip $snapshot_old refs/heads/ait/master' $race_snapshot && ! grep -q ' $snapshot_late\$' $race_snapshot; } || { grep -qx 'tip $snapshot_new refs/heads/ait/master' $race_snapshot && grep -qx 'tip $snapshot_old $snapshot_late' $race_snapshot; }"
git update-ref refs/heads/ait/master "$snapshot_old"
git update-ref -d "$snapshot_late"

done_testing
