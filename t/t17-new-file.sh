#!/bin/sh
# spec 6.5.1, 6.5.2, 6.5.2a: every new user file refuses replacement.
. "$(dirname "$0")/lib.sh"
setup
repo=$(git rev-parse --show-toplevel)

run ait contract new spec .ait/new/deep/condition.yaml prompt
check_rc "new spec creates its parent directories and file" 0
cp .ait/new/deep/condition.yaml "$T/first"
run ait contract new spec .ait/new/deep/condition.yaml exec
check_rc "new spec refuses an existing file" 1
check "an existing user file keeps its bytes" "cmp -s .ait/new/deep/condition.yaml $T/first"
ln -s "$T/absent" .ait/new/deep/dangling.yaml
run ait contract new spec .ait/new/deep/dangling.yaml prompt
check_rc "new spec refuses a dangling leaf symlink" 1
check "a dangling leaf and its absent target stay unchanged" "test -L .ait/new/deep/dangling.yaml && ! test -e $T/absent"
mkdir .ait/new/deep/directory.yaml
run ait contract new spec .ait/new/deep/directory.yaml prompt
check_rc "new spec refuses a directory at the leaf" 1
printf 'blocked parent\n' > .ait/blocked
run ait contract new spec .ait/blocked/condition.yaml prompt
check_rc "a file in the parent path reports an I/O failure" 128
check "a blocked parent keeps its bytes" "test \"\$(cat .ait/blocked)\" = 'blocked parent'"

case $(uname -s) in
Darwin)
	run "${CC:-cc}" -dynamiclib -Wall -Werror -o "$T/pause-new.dylib" "$AIT_DIR/t/pause-new-file.c"
	preload="DYLD_INSERT_LIBRARIES=$T/pause-new.dylib"
	;;
Linux)
	run "${CC:-cc}" -shared -fPIC -Wall -Werror -o "$T/pause-new.so" "$AIT_DIR/t/pause-new-file.c" -ldl
	preload="LD_PRELOAD=$T/pause-new.so"
	;;
*)
	echo '# SKIP: new-file scheduler supports macOS and Linux'
	done_testing
	exit $?
	;;
esac
check_rc "the native new-file scheduler builds" 0
if test "$rc" != 0
then
	done_testing
	exit $?
fi

run ait future demand src/order.c src/stock.c cond.yaml -g checkout -m shared
D=$(newest D)
for kind in spec draft checkout
do
	case $kind in
	spec) prefix="$repo/.ait/race-spec.yaml"; set -- contract new spec .ait/race-spec.yaml prompt ;;
	draft) prefix="$repo/.ait/race-draft/"; set -- contract new demand race-draft ;;
	checkout) prefix="$repo/.ait/checkout/$D.yaml"; set -- contract checkout "$D" ;;
	esac
	rm -f "$T/new-ready" "$T/new-proceed"
	env "$preload" AIT_TEST_NEW_PREFIX="$prefix" AIT_TEST_NEW_READY="$T/new-ready" AIT_TEST_NEW_PROCEED="$T/new-proceed" \
		ait "$@" > "$T/out" 2> "$T/err" &
	writer=$!
	for i in $(seq 1 1000)
	do
		test -e "$T/new-ready" && break
		sleep 0.01
	done
	if test -s "$T/new-ready"
	then
		target=$(cat "$T/new-ready")
		case "$kind:$target" in
		spec:"$prefix"|checkout:"$prefix"|draft:"$prefix"D????????????.yaml)
			say_ok "$kind scheduler stops at the final destination" ;;
		*) say_fail "$kind scheduler stops at the final destination" ;;
		esac
		printf 'another writer\n' > "$target"
		cp "$target" "$T/competing"
		touch "$T/new-proceed"
		wait "$writer"; rc=$?
		check_rc "$kind creation refuses a concurrently created target" 1
		check "$kind creation preserves the competing bytes" "cmp -s \"$target\" $T/competing"
		check "$kind creation reports no successful output" "! has out '^path '"
	else
		touch "$T/new-proceed"
		wait "$writer"
		say_fail "$kind creation reached the actual filesystem operation"
	fi
done

# git's has_symlink_leading_path: no new file goes through a linked directory.
printf 'body: |\n  x\n' >cond-link.yaml
run ait future demand src/order.c src/stock.c cond-link.yaml -m link-source
LINKED=$(git ls-tree --name-only ait/master | sed -n 's/\.yaml$//p' | head -1)
mv .ait "$T/ait-before-links"
mkdir "$T/away"
ln -s "$T/away" specs
run ait contract new spec specs/sub/x.yaml exec
check_rc "a spec below a linked directory is refused" 128
check "the refusal names the path and writes nothing away" \
	"has err \"fatal: 'specs/sub/x.yaml' is beyond a symbolic link\" && test -z \"\$(ls -A '$T/away')\""
rm specs
ln -s "$T/away" .ait
for args in "contract new demand" "contract new offer grp" "contract checkout $LINKED"
do
	run ait $args
	check_rc "'ait $args' below a linked .ait is refused" 128
	check "'ait $args' writes nothing away" "has err 'is beyond a symbolic link' && test -z \"\$(ls -A '$T/away')\""
done
rm .ait
mv "$T/ait-before-links" .ait

done_testing
