#!/bin/sh
# The stage lock outlives every child which could publish its checkin.
. "$(dirname "$0")/lib.sh"
setup

stage=$(git rev-parse --absolute-git-dir)/ait/index
stage_test_git=$(command -v git)
export stage stage_test_git
mkdir -p .ait/stage "$T/slow-bin"
P=.ait/stage/D111111111111.yaml
Q=.ait/stage/D222222222222.yaml
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
    child lifetime snapshot
fulfilled:
YAML
cp "$P" "$Q"
run ait contract add "$P"
check_rc "the child-lifetime test stages a valid snapshot" 0
cp "$stage" "$T/stage-before"
tip=$(git rev-parse ait/master)

# A delayed termination makes 'signal the child' distinct from 'reap the child'.
# Other Git commands still use the real executable; this child never writes a ref.
cat >"$T/slow-bin/git" <<'PERL'
#!/usr/bin/perl
use strict;
use warnings;
if (grep { $_ eq 'update-ref' } @ARGV) {
	my $terminated;
	$SIG{TERM} = sub { $terminated = 1 };
	open(my $ready, '>', "$ENV{T}/child-ready") or die $!;
	close($ready);
	for (1..500) {
		last if $terminated;
		select(undef, undef, undef, 0.01);
	}
	exit 1 unless $terminated;
	open(my $stopping, '>', "$ENV{T}/child-stopping") or die $!;
	close($stopping);
	for (1..500) {
		exit 143 if -e "$ENV{T}/child-release";
		select(undef, undef, undef, 0.01);
	}
	exit 1;
}
exec $ENV{stage_test_git}, @ARGV;
die $!;
PERL
chmod +x "$T/slow-bin/git"
env PATH="$T/slow-bin:$PATH" ait contract commit -m child-lifetime >"$T/out" 2>"$T/err" &
committer=$!
for i in $(seq 1 100)
do
	test -e "$T/child-ready" && break
	sleep 0.05
done
if test -e "$T/child-ready"
then
	kill -TERM "$committer"
	for i in $(seq 1 100)
	do
		test -e "$T/child-stopping" && break
		sleep 0.01
	done
	check "termination reaches the active publishing child" "test -e $T/child-stopping"
	# A second signal must not reenter cleanup and release the lock during wait.
	kill -HUP "$committer" 2>/dev/null
	for i in $(seq 1 50)
	do
		test -e "$stage.lock" || break
		sleep 0.01
	done
	check "the lock stays held until the child exits, even under another signal" "test -e $stage.lock && cmp -s $T/stage-before $stage"
	run ait contract add "$Q"
	check_rc "a child still stopping keeps concurrent add out" 128
	: >"$T/child-release"
	wait "$committer" 2>/dev/null
	rc=$?
	check "the parent terminates after its child exits" "test $rc -gt 128"
	check "completed signal cleanup preserves the stage and branch" "cmp -s $T/stage-before $stage && test \"\$(git rev-parse ait/master)\" = $tip && ! test -e $stage.lock"
else
	kill -TERM "$committer" 2>/dev/null
	: >"$T/child-release"
	wait "$committer" 2>/dev/null
	say_fail "the publishing child reached its pause"
fi
run ait contract commit -m retry
check_rc "the same snapshot can be retried after child cleanup" 0
check "retry consumes the snapshot and releases the lock" "! test -e $stage && ! test -e $stage.lock"

done_testing
