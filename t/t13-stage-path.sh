#!/bin/sh
# Git's index reader, writer and the commit lock must use one resolved path.
. "$(dirname "$0")/lib.sh"
setup
racing_git

stage=$(git rev-parse --absolute-git-dir)/ait/index
backing=$(dirname "$stage")/alternate-index
mkdir -p .ait/stage "$(dirname "$stage")"
P=.ait/stage/D111111111111.yaml
Q=.ait/stage/D222222222222.yaml
R=.ait/stage/D333333333333.yaml
cat >"$P" <<'YAML'
condition-id: ""
change-id: ""
kind: demand
creator: ""
consumer: "src/order.c"
provider: "src/stock.c"
state: future
specs:
- body: |
    index path snapshot
fulfilled:
YAML
cp "$P" "$Q"
cp "$P" "$R"

for shape in absolute chain
do
	# Each case starts with a native, valid index at the alias target.
	rm -f "$stage" "$backing"
	test "$shape" = absolute && draft=$P || draft=$Q
	test "$shape" = absolute && extra=$Q || extra=$R
	export extra
	blob=$(git hash-object -w "$draft")
	GIT_INDEX_FILE="$backing" git update-index --add --cacheinfo "100644,$blob,${draft#.ait/}"
	if test "$shape" = absolute
	then
		ln -s "$backing" "$stage"
	else
		ln -s hop1 "$stage"
		for i in $(seq 1 7)
		do
			ln -s "hop$((i+1))" "$(dirname "$stage")/hop$i"
		done
		ln -s alternate-index "$(dirname "$stage")/hop8"
	fi
	cat >"$T/before-update-ref" <<'HOOK'
ait contract add "$extra" >"$T/nested.out" 2>"$T/nested.err"
echo $? >"$T/nested.rc"
exit 0
HOOK
	rm -f "$T/nested.rc" "$T/nested.out" "$T/nested.err"
	run env PATH="$T/bin:$PATH" ait contract commit -m "$shape index"
	check_rc "$shape index alias commits its snapshot" 0
	check "$shape index alias excludes a concurrent add" "test \"\$(cat $T/nested.rc)\" = 128 && ! grep -q '^add ' $T/nested.out"
	check "$shape index alias remains while its consumed target is empty" "test -L $stage && ! test -e $stage && ! test -e $backing"
	check "$shape index alias commits only the original snapshot" "git cat-file -e ait/master:${draft#.ait/} && ! git cat-file -e ait/master:${extra#.ait/} 2>/dev/null"
	run ait contract add "$extra"
	check_rc "$shape dangling alias can stage the next draft" 0
	GIT_INDEX_FILE="$stage" git show ":${extra#.ait/}" >"$T/next-draft"
	check "$shape alias stores the next draft at its original target" "test -L $stage && test -f $backing && cmp -s $extra $T/next-draft"
done

# A pre-existing lock at the resolved target belongs to another writer.
cp "$backing" "$T/stage-before"
printf 'foreign lock\n' >"$backing.lock"
cp "$backing.lock" "$T/foreign-lock"
tip=$(git rev-parse ait/master)
run ait contract commit -m foreign
check_rc "commit respects a foreign lock at the alias target" 128
check "foreign target lock, stage and branch are preserved" "cmp -s $T/foreign-lock $backing.lock && cmp -s $T/stage-before $backing && test \"\$(git rev-parse ait/master)\" = $tip"
rm "$backing.lock"
run ait contract commit -m retry
check_rc "the alias snapshot can be retried after the foreign lock is gone" 0
check "retry consumes the alias target and retains its path" "test -L $stage && ! test -e $stage && ! test -e $backing"

done_testing
