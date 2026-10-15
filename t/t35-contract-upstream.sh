#!/bin/sh
# spec 2.1.2, 6.5.6-2: an upstream must be a classified remote ait ref.
. "$(dirname "$0")/lib.sh"
setup

base=$(git rev-parse ait/master)
ordinary=$(printf 'ordinary descendant\n' | git commit-tree -p "$base" "$(git rev-parse 'ait/master^{tree}')")
git push -q origin "$ordinary:refs/heads/ordinary"
git config branch.ait/master.remote origin
git config branch.ait/master.merge refs/heads/ordinary
run ait contract pull
check_rc "a normal remote branch is not an upstream for contract pull" 128
check "the ordinary upstream is refused with the specified branch error" \
	"has err \"fatal: 'ait/master' has no upstream\""
check "refusing an ordinary upstream cannot fast-forward the contract branch" \
	"test \"\$(git rev-parse ait/master)\" = $base && ! has out '^Fast-forward '"
git update-ref refs/heads/ait/master "$base"

git push -q origin "$ordinary:refs/heads/ait/master"
git config branch.ait/master.merge refs/heads/ait/master
run ait contract pull
check_rc "a configured remote ait upstream remains valid" 0
check "a valid contract upstream can fast-forward" \
	"test \"\$(git rev-parse ait/master)\" = $ordinary && has out '^Fast-forward '"

# A nested remote name must use the same longest-prefix classification as names.
git init -q --bare "$T/contracts.git"
git remote add team "$T/contracts.git"
git config remote.team/ait/zone.url "$T/contracts.git"
git config remote.team/ait/zone.fetch '+refs/heads/*:refs/remotes/team/ait/zone/*'
nested=$(printf 'nested remote descendant\n' | git commit-tree -p "$ordinary" "$(git rev-parse 'ait/master^{tree}')")
git push -q "$T/contracts.git" "$nested:refs/heads/ait/master"
git config branch.ait/master.remote team/ait/zone
git config branch.ait/master.merge refs/heads/ait/master
run ait contract pull team/ait/zone
check_rc "an ait upstream under the longest configured remote prefix remains valid" 0
check "nested-remote pull advances to its contract tip" \
	"test \"\$(git rev-parse ait/master)\" = $nested && has out '^Fast-forward '"
run env AIT_SESSION=- ait status
check_rc "the nested upstream and local branch still count as one contract name" 0
check "the shared longest-prefix classification retains automatic selection" \
	"has out '^On contract branch ait/master (the only one)$'"

# Local Git upstream configuration can name an arbitrary remote-tracking ref;
# the namespace alone does not make its remote a configured contract remote.
ghost=$(printf 'unconfigured remote descendant\n' | git commit-tree -p "$nested" "$(git rev-parse 'ait/master^{tree}')")
git update-ref refs/remotes/ghost/ait/master "$ghost"
git config branch.ait/master.remote .
git config branch.ait/master.merge refs/remotes/ghost/ait/master
check "Git resolves the unconfigured-remote fixture as an existing upstream ref" \
	"test \"\$(git rev-parse --symbolic-full-name 'ait/master@{upstream}')\" = refs/remotes/ghost/ait/master"
run ait contract pull origin
check_rc "an ait-shaped ref under an unconfigured remote is not a contract upstream" 128
check "an unconfigured remote gets the same no-upstream error" \
	"has err \"fatal: 'ait/master' has no upstream\""
check "refusing an unconfigured upstream preserves the contract tip" \
	"test \"\$(git rev-parse ait/master)\" = $nested && ! has out '^Fast-forward '"

done_testing
