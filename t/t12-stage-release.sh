#!/bin/sh
# Signal cleanup must not remove the next owner's index.lock at handoff.
. "$(dirname "$0")/lib.sh"
setup

case $(uname -s) in
Darwin)
	run "${CC:-cc}" -dynamiclib -Wall -Werror -o "$T/pause-unlink.dylib" "$AIT_DIR/t/pause-stage-unlink.c"
	preload="DYLD_INSERT_LIBRARIES=$T/pause-unlink.dylib"
	;;
Linux)
	run "${CC:-cc}" -shared -fPIC -Wall -Werror -o "$T/pause-unlink.so" "$AIT_DIR/t/pause-stage-unlink.c" -ldl
	preload="LD_PRELOAD=$T/pause-unlink.so"
	;;
*)
	echo '# SKIP: the lock-release interposer supports macOS and Linux'
	done_testing
	exit $?
	;;
esac
check_rc "the native lock-release scheduler builds" 0
if test "$rc" != 0
then
	done_testing
	exit $?
fi

stage=$(git rev-parse --absolute-git-dir)/ait/index
mkdir -p .ait/stage
P=.ait/stage/D111111111111.yaml
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
    stage lock handoff
fulfilled:
YAML
run ait contract add "$P"
check_rc "the handoff test stages a valid snapshot" 0

# Deliver TERM synchronously after a real successful unlink and replacement
# acquisition. Blocking it until deactivation must preserve the replacement.
env "$preload" AIT_TEST_RELEASE_LOCK="$stage.lock" \
	AIT_TEST_RELEASE_READY="$T/release-ready" AIT_TEST_RELEASE_PROCEED="$T/release-proceed" \
	ait contract commit -m handoff >"$T/commit.out" 2>"$T/commit.err" &
committer=$!
for i in $(seq 1 100)
do
	test -e "$T/release-ready" && break
	sleep 0.05
done
if test -e "$T/release-ready"
then
	check "commit reaches lock handoff after consuming its snapshot" "! test -e $stage && ! test -e $stage.lock && git cat-file -e ait/master:${P#.ait/}"
	printf 'next owner\n' >"$T/expected-lock"
	run perl -MFcntl=:DEFAULT -e '
		sysopen(my $lock, $ARGV[0], O_WRONLY | O_CREAT | O_EXCL, 0666) or die $!;
		print {$lock} "next owner\n" or die $!;
		close($lock) or die $!;
	' "$stage.lock"
	check_rc "the next owner exclusively acquires the released lock" 0
	: >"$T/release-proceed"
	wait "$committer" 2>/dev/null
	rc=$?
	check "the handoff signal terminates the original committer" "test $rc -gt 128"
	check "signal cleanup preserves the next owner's lock bytes" "cmp -s $T/expected-lock $stage.lock"
else
	: >"$T/release-proceed"
	kill -TERM "$committer" 2>/dev/null
	wait "$committer" 2>/dev/null
	say_fail "the commit reached its lock-release boundary"
fi

done_testing
