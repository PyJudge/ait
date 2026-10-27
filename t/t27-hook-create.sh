#!/bin/sh
# spec 6.1-1a: installing the notification hook never replaces an existing path.
. "$(dirname "$0")/lib.sh"
setup
hooks="$T/custom hooks"
hook="$hooks/reference-transaction"
git config core.hooksPath "$hooks"
hook=$(git rev-parse --path-format=absolute --git-path hooks/reference-transaction)
(umask 077; ait init >"$T/out" 2>"$T/err"); rc=$?
check_rc 'init creates a missing hook through nested directories' 0
check 'a newly created hook is executable even with a restrictive umask' "test -x \"$hook\" && test -n \"\$(find \"$hook\" -perm 0755)\""
cp "$hook" "$T/ait-hook"

printf '#!/bin/sh\n# a foreign hook\nexit 0\n' > "$hook"
chmod 0644 "$hook"
cp "$hook" "$T/foreign"
run ait init
check_rc 'init accepts a foreign hook without changing it' 0
test_have_prereq POSIXPERM 'the foreign hook keeps its bytes and permissions' &&
check 'the foreign hook keeps its bytes and permissions' "cmp \"$hook\" $T/foreign && ! test -x \"$hook\""
check 'a foreign hook reports polling fallback' "has err 'wakes by polling only'"

rm "$hook"
if test_have_prereq SYMLINKS 'init accepts a dangling hook symlink'
then
	ln -s "$T/missing-hook-target" "$hook"
	run ait init
	check_rc 'init accepts a dangling hook symlink' 0
	check 'a dangling hook symlink and its absent target stay unchanged' "test -L \"$hook\" && ! test -e $T/missing-hook-target"
	check 'a dangling hook symlink reports polling fallback' "has err 'wakes by polling only'"
	rm "$hook"
fi

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
	echo '# SKIP: hook creation scheduler supports macOS and Linux'
	done_testing
	exit $?
	;;
esac
check_rc 'the native hook creation scheduler builds' 0
if test "$rc" != 0
then
	done_testing
	exit $?
fi

for other in foreign ait-nonexecutable
do
	rm -f "$hook" "$T/new-ready" "$T/new-proceed"
	env "$preload" AIT_TEST_NEW_PREFIX="$hook" AIT_TEST_NEW_READY="$T/new-ready" AIT_TEST_NEW_PROCEED="$T/new-proceed" \
		ait init >"$T/out" 2>"$T/err" &
	writer=$!
	for i in $(seq 1 1000)
	do
		test -s "$T/new-ready" && break
		sleep 0.01
	done
	if test -s "$T/new-ready"
	then
		case $other in
		foreign) cp "$T/foreign" "$hook" ;;
		ait-nonexecutable) cp "$T/ait-hook" "$hook" ;;
		esac
		chmod 0644 "$hook"
		cp "$hook" "$T/competing"
		touch "$T/new-proceed"
		wait "$writer"; rc=$?
		check_rc "$other concurrent installation leaves init successful" 0
		check "$other concurrent hook keeps its bytes and mode" "cmp \"$hook\" $T/competing && ! test -x \"$hook\""
		check "$other concurrent hook reports polling fallback" "has err 'wakes by polling only'"
	else
		touch "$T/new-proceed"
		wait "$writer"
		say_fail "$other installation reaches the actual filesystem operation"
	fi
done

done_testing
