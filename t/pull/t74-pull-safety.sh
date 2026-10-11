#!/bin/sh
# 7.12.0: Git protects the index and work tree; ait also rejects dirty write paths. Ignored files are Git's to overwrite.
. "$(dirname "$0")/lib.sh"

absent_in_index() { ! git rev-parse --verify ":$1" >/dev/null 2>&1; }

incoming() {
	pbase
	git checkout -q -b incoming
	cond D incoming; rec D >/dev/null
	echo incoming > src/new.c
	A1=$(c incoming)
	git checkout -q master
}
protected() {
	before=$(snapshot)
	idx=$(git hash-object "$(git rev-parse --git-path index)")
	pull "$D"
	[ "$rc" = 1 ] && [ "$(snapshot)" = "$before" ] &&
	[ "$(git hash-object "$(git rev-parse --git-path index)")" = "$idx" ] && ! merging
}

incoming
cond D draft; git add specs/d.yaml
staged=$(git rev-parse :specs/d.yaml)
git show HEAD:specs/d.yaml > specs/d.yaml
want 'index-only change is preserved when the work file matches HEAD' protected
want 'the staged draft blob is still in the index' test "$(git rev-parse :specs/d.yaml)" = "$staged"

incoming
git rm -q --cached specs/d.yaml
want 'a staged deletion with the original work file is preserved' protected
want 'staged deletion remains staged' absent_in_index specs/d.yaml

incoming
ln -s absent src/new.c
want 'a dangling untracked symlink is protected by unpack-trees' protected
want 'dangling symlink target is preserved' test "$(readlink src/new.c)" = absent

incoming
mkdir src/new.c; echo valuable > src/new.c/draft
want 'a directory containing untracked data cannot become a file' protected
want 'untracked directory data is preserved' test "$(cat src/new.c/draft)" = valuable

incoming
echo src/new.c > .git/info/exclude
echo expendable > src/new.c
pull "$D"
want 'an ignored file on a write path is overwritten, as git merge does (user decision 38)' test "$rc" = 0 -a "$(git rev-parse HEAD)" = "$A1"
want 'the ignored file now has the incoming content' test "$(cat src/new.c)" = incoming

incoming
cond D draft
want 'an unstaged write-path change is preserved' protected
want 'unstaged file contents are preserved' test "$(cat specs/d.yaml)" = 'body: draft'

incoming
echo unrelated > src/order.c; git add src/order.c
saved=$(git rev-parse :src/order.c)
pull "$D"
want 'fast-forward keeps unrelated staged changes' test "$rc" = 0 -a "$(git rev-parse :src/order.c)" = "$saved" -a "$(cat src/order.c)" = unrelated
want 'Git updates the incoming files and HEAD' test "$(git rev-parse HEAD)" = "$A1" -a "$(cat src/new.c)" = incoming

done_testing
