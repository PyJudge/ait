#!/bin/sh
# spec 6.1-4, 6.5.5-6, 7.2: Git receives operands and environment blob OIDs.
. "$(dirname "$0")/lib.sh"
setup

# A configured name beginning with '-' is data, even when it is a Git option.
git push -q origin refs/heads/ait/master
mv "$T/origin.git" "$T/contracts with spaces.git"
git remote remove origin
git remote add -- --dry-run "$T/contracts with spaces.git"
git update-ref -d refs/heads/ait/master
run ait init
check_rc 'init finds unfetched ait branches on an option-like remote' 128
check 'init reports the configured remote, without creating a local root' \
	"has err \"fatal: '--dry-run' has ait branches that are not fetched\" && ! git show-ref --verify --quiet refs/heads/ait/master"

# Restore through Git so later checks do not depend on the init result.
git update-ref -d refs/heads/ait/master
git fetch -q -- --dry-run '+refs/heads/ait/*:refs/remotes/--dry-run/ait/*'
git branch -q --track ait/master refs/remotes/--dry-run/ait/master
git remote add team/contracts "$T/contracts with spaces.git"
for remote in --dry-run team/contracts; do
	git config branch.ait/master.remote "$remote"
	git config branch.ait/master.merge refs/heads/ait/master
	git config branch.ait/master.pushRemote "$remote"
	for selection in explicit configured; do
		ait future offer src/stock.c cond.yaml -m 'publish a contract' >/dev/null
		local_tip=$(git rev-parse ait/master)
		if [ "$selection" = explicit ]; then
			run ait contract push "$remote"
		else
			run ait contract push
		fi
		check_rc "$selection push resolves remote '$remote'" 0
		check "$selection push updates the remote contract ref" \
			"test \"\$(git --git-dir=\"$T/contracts with spaces.git\" rev-parse refs/heads/ait/master)\" = $local_tip"

		# Restore through Git so a failed push cannot invalidate the pull fixture.
		git push -q -- "$T/contracts with spaces.git" "$local_tip:refs/heads/ait/master"
		# Push by URL to advance the remote without updating our tracking ref.
		git fetch -q -- "$remote"
		next=$(printf 'remote descendant\n' | git commit-tree -p "$local_tip" "$(git rev-parse "$local_tip^{tree}")")
		git push -q -- "$T/contracts with spaces.git" "$next:refs/heads/ait/master"
		check 'the pull fixture has an older tracking ref than the remote' \
			"test \"\$(git rev-parse refs/remotes/$remote/ait/master)\" = $local_tip"
		if [ "$selection" = explicit ]; then
			run ait contract pull "$remote"
		else
			run ait contract pull
		fi
		check_rc "$selection pull resolves remote '$remote'" 0
		check "$selection pull fetches and fast-forwards to the remote commit" \
			"test \"\$(git rev-parse ait/master)\" = $next && has out '^Fast-forward '"
		git update-ref refs/heads/ait/master "$next"
	done
done

# Native stderr/stdout and failure status survive the operand boundary.
ait future offer src/stock.c cond.yaml -m 'a rejected push' >/dev/null
cat >"$T/contracts with spaces.git/hooks/pre-receive" <<'HOOK'
#!/bin/sh
printf 'remote refuses this update\n' >&2
exit 1
HOOK
chmod +x "$T/contracts with spaces.git/hooks/pre-receive"
run git push -- --dry-run refs/heads/ait/master:refs/heads/ait/master
native_rc=$rc
cp "$T/out" "$T/native-out"
cp "$T/err" "$T/native-err"
run ait contract push --dry-run
check 'a rejected push preserves Git failure status and complete output' \
	"test $native_rc = 1 && test $rc = $native_rc && cmp -s '$T/native-out' '$T/out' && cmp -s '$T/native-err' '$T/err'"
rm "$T/contracts with spaces.git/hooks/pre-receive"

# The YAML spelling carries escaped bytes; Git stores the actual path bytes.
mkdir -p specs/environments
number=0
for spelling in normal 'space name' 'line\nbreak' 'tab\tname'; do
	number=$((number + 1))
	name=$(printf '%b' "$spelling")
	printf 'cwd: environment-%s\ntimeout_ms: 17\n' "$number" >"specs/environments/$name.yaml"
done
bad_name=$(printf 'bad\nname')
printf 'cwd: []\n' >"specs/environments/$bad_name.yaml"
git add specs/environments
git commit -qm environments
code=$(git rev-parse HEAD)
tip=$(git rev-parse ait/master)
number=0
for spelling in normal 'space name' 'line\nbreak' 'tab\tname'; do
	number=$((number + 1))
	printf 'body: condition\nengine: exec\ncommand: [true]\nenvironment: "%s"\n' "$spelling" >cond.yaml
	run ait offer src/stock.c cond.yaml -m 'read environment'
	check_rc "environment path case $number is read before requesting results" 1
	req=$(sed -n 's/^request //p' "$T/out")
	check "environment path case $number contributes its exact file values" \
		"test -n \"$req\" && grep -Fq 'cwd: \"environment-$number\"' \"$req\" && grep -Fq 'timeout_ms: 17' \"$req\""
	check 'requesting results changes neither code nor the contract ref' \
		"test \"\$(git rev-parse HEAD)\" = $code && test \"\$(git rev-parse ait/master)\" = $tip"
done
printf 'body: condition\nengine: exec\ncommand: [true]\nenvironment: "bad\\nname"\n' >cond.yaml
run ait offer src/stock.c cond.yaml -m 'invalid environment'
check_rc 'a newline-named environment still receives schema validation' 128
check 'the existing invalid environment is a format error, not a read error' \
	"has err '^fatal: bad format:' && has err \"'cwd'\""
printf 'body: condition\nengine: exec\ncommand: [true]\nenvironment: "missing\\nname"\n' >cond.yaml
run ait offer src/stock.c cond.yaml -m 'missing environment'
check_rc 'a missing newline-named environment remains a read error' 128
check 'a missing environment retains its specified read diagnostic' \
	"has err '^fatal: could not read'"

done_testing
