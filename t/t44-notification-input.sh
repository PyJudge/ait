#!/bin/sh
# spec 9: one receiving/ref snapshot supplies candidate, reason, ref and in-head.
. "$(dirname "$0")/lib.sh"
setup
# each round ends by a signal the fake git sends listen
test_have_prereq SIGNALS "one receiving snapshot supplies candidate, reason, ref and in-head" || { done_testing; exit $?; }
base=$(git rev-parse ait/master)
REAL_GIT=$(command -v git); export REAL_GIT
checkout=$(git rev-parse --path-format=absolute --git-path ait-branch)
export checkout
mkdir "$T/round-bin"
cat >"$T/round-bin/git" <<'GIT'
#!/bin/sh
sub=
for arg; do
	case $arg in -*|*=*|/*|[A-Za-z]:/*) continue ;; esac
	sub=$arg; break
done
if test "$sub" = for-each-ref && test -e "$receiver_file.listen" &&
   test "$MODE" = refs && ! test -e "$T/round-injected"; then
	touch "$T/round-injected"
	"$REAL_GIT" "$@" >"$T/round-refs" || exit $?
	"$REAL_GIT" update-ref refs/heads/ait/master "$base" || exit $?
	"$REAL_GIT" update-ref -d refs/remotes/origin/ait/master || exit $?
	"$REAL_GIT" config branch.ait/master.merge refs/heads/ordinary || exit $?
	cat "$T/round-refs"
	exit
fi
if test "$sub" = rev-parse && { test "$MODE" = checkout || test "$MODE" = presence; } && test -e "$receiver_file.listen"; then
	case " $* " in *' --git-path ait-branch '*)
		n=$(cat "$T/checkout-reads" 2>/dev/null || echo 0)
		n=$((n+1)); echo "$n" >"$T/checkout-reads"
		if test "$n" = 2; then
			if test "$MODE" = presence; then : >"$checkout"; else printf 'ait/side\n' >"$checkout"; fi
			touch "$T/round-injected"
		fi
	;; esac
fi
if test "$sub" = show && test "$MODE" = abort; then
	case " $* " in *" $second_commit "*)
		cp "$receiver_file.seen" "$T/seen-at-second"
		printf 'sub scope src/order.c\n' >>"$receiver_file"
		touch "$T/round-injected"
		kill -TERM "$PPID"
	;; esac
fi
if test "$sub" = rev-list; then
	if test "$MODE" = pipe; then
		for attempt in $(seq 1 100); do
			test -e "$T/pipe-closed" && break
			sleep .05
		done
	fi
	"$REAL_GIT" "$@" >"$T/round-candidates" || exit $?
	kill -TERM "$PPID"
	cat "$T/round-candidates"
	exit
fi
exec "$REAL_GIT" "$@"
GIT
chmod +x "$T/round-bin/git"
export base

reset_refs() {
	git update-ref refs/heads/ait/master "$base"
	git update-ref -d refs/heads/ait/side
	git update-ref -d refs/remotes/origin/ait/master
	git config --unset-all branch.ait/master.remote || :
	git config --unset-all branch.ait/master.merge || :
	printf 'ait/master\n' >"$checkout"
}
receiver() {
	BOB=$(AIT_SESSION=- ait session open --agent bob 2>/dev/null)
	receiver_file=$(git rev-parse --path-format=absolute --git-common-dir)/ait/sessions/$BOB
	export receiver_file
	AIT_SESSION="$BOB" ait sub "$1" >/dev/null
	grep '^tip ' "$receiver_file" >"$T/tips-before"
	rm -f "$T/round-injected" "$T/checkout-reads"
}
one_round() {
	run env AIT_SESSION="$BOB" PATH="$TP/round-bin:$PATH" MODE="$1" ait listen
}
no_seen() { ! test -s "$receiver_file.seen"; }
unchanged_tips() { grep '^tip ' "$receiver_file" >"$T/tips-after"; cmp -s "$T/tips-before" "$T/tips-after"; }

# The entire captured N and upstream selection survive later ref/config movement.
reset_refs
git update-ref refs/remotes/origin/ait/master "$base"
git config branch.ait/master.remote origin
git config branch.ait/master.merge refs/heads/ait/master
receiver /
run ait future offer src/stock.c cond.yaml -m captured
A=$(newest O); captured=$(git rev-parse ait/master)
git update-ref refs/remotes/origin/ait/master "$captured"
one_round refs
check_rc 'a captured round finishes despite later ref and upstream movement' 0
check 'the ref race actually rewinds local, deletes upstream and changes config' \
	"test -e '$T/round-injected' && test \"\$(git rev-parse ait/master)\" = $base && ! git show-ref --verify --quiet refs/remotes/origin/ait/master"
check 'the event comes from the initially captured candidate' \
	"has out '^commit $captured$' && has out '^contract $A create$' && test \"\$(grep -c '^ait-event-end$' '$T/out')\" = 1"
check 'ref, reason and in-head describe the same captured input' \
	"has out '^ref refs/heads/ait/master$' && has out '^ref refs/remotes/origin/ait/master$' && has out '^reason sub scope /$' && has out '^in-head yes$'"
check 'the finalized N is the captured listing, not a later one' \
	"grep -qx 'tip $captured refs/heads/ait/master' '$receiver_file' && grep -qx 'tip $captured refs/remotes/origin/ait/master' '$receiver_file'"

# A change after the initial V must abort; a fresh round then positively chooses B.
reset_refs
git update-ref refs/heads/ait/side "$base"
receiver /
run ait future offer src/stock.c cond.yaml -m branch-a
A=$(newest O); a_commit=$(git rev-parse ait/master)
printf 'ait/side\n' >"$checkout"
run ait future offer src/order.c cond.yaml -m branch-b
B=$(newest O); b_commit=$(git rev-parse ait/side)
printf 'ait/master\n' >"$checkout"
one_round checkout
check_rc 'a receiving-state change aborts the current round' 0
check 'the checkout race actually changes the selected branch' "test -e '$T/round-injected' && grep -qx ait/side '$checkout'"
check 'the aborted round emits no payload and commits no keys or N' \
	"! has out '^ait-event 1$' && no_seen && unchanged_tips"
one_round normal
check_rc 'a fresh round can use the new checkout' 0
check 'the fresh round delivers exactly the B event with B provenance' \
	"has out '^commit $b_commit$' && has out '^contract $B create$' && has out '^ref refs/heads/ait/side$' && has out '^reason sub scope /$' && has out '^in-head yes$' && ! has out '^commit $a_commit$' && test \"\$(grep -c '^ait-event-end$' '$T/out')\" = 1"

# An absent checkout and an empty file must not compare as the same V.
reset_refs
receiver /
run ait future offer src/stock.c cond.yaml -m checkout-presence
rm -f "$checkout"
one_round presence
check_rc 'a missing-to-empty checkout change aborts the round' 0
check 'the presence race ran and preserves keys and N' \
	"test -e '$T/round-injected' && test -f '$checkout' && ! test -s '$checkout' && ! has out '^ait-event 1$' && no_seen && unchanged_tips"

# Successful delivery is durable immediately; skipped/undelivered keys wait for final V.
reset_refs
receiver src/stock.c
run ait future offer src/order.c cond.yaml -m unrelated
U=$(newest O)
run env AIT_SESSION="$BOB" ait future offer src/stock.c cond.yaml -m own
S=$(newest O)
run ait future offer src/stock.c cond.yaml -m first
A=$(newest O); first_commit=$(git rev-parse ait/master)
run ait future offer src/stock.c cond.yaml -m second
B=$(newest O); second_commit=$(git rev-parse ait/master); export second_commit
one_round abort
check_rc 'a later V change stops delivery without undoing the first event' 0
check 'the first event was delivered, while the second was withheld' \
	"has out '^commit $first_commit$' && has out '^contract $A create$' && ! has out '^commit $second_commit$' && test \"\$(grep -c '^ait-event-end$' '$T/out')\" = 1"
check 'the first key is saved before constructing the second payload' \
	"grep -q '^version $A ' '$T/seen-at-second' && grep -q '^version $A ' '$receiver_file.seen'"
check 'V abort commits neither skipped/undelivered keys nor N' \
	"! grep -Eq '^version ($U|$S|$B) ' '$receiver_file.seen' && unchanged_tips"
grep -v '^sub scope src/order.c$' "$receiver_file" >"$T/restored-session"
mv "$T/restored-session" "$receiver_file"
one_round normal
check_rc 'a restarted round completes the remaining delivery' 0
check 'restart sends B without repeating A' \
	"has out '^commit $second_commit$' && ! has out '^commit $first_commit$' && test \"\$(grep -c '^ait-event-end$' '$T/out')\" = 1"
check 'the completed round commits skipped keys and the captured tip' \
	"grep -q '^version $U ' '$receiver_file.seen' && grep -q '^version $S ' '$receiver_file.seen' && grep -q '^version $B ' '$receiver_file.seen' && grep -qx 'tip $second_commit refs/heads/ait/master' '$receiver_file'"

# Output failures cannot commit any delivery state. Open fd1 read-only for EBADF.
reset_refs
receiver /
run ait future offer src/stock.c cond.yaml -m failed-output
: >"$T/read-only"
env AIT_SESSION="$BOB" PATH="$TP/round-bin:$PATH" MODE=normal ait listen 1<"$T/read-only" 2>"$T/err"
rc=$?
check_rc 'an ordinary output failure is E_IO' 128
check 'an ordinary output failure retains its stdout diagnostic' "has err '^fatal: write error: standard output:'"
check 'an ordinary output failure commits no keys or tip snapshot' 'no_seen && unchanged_tips'

# A FIFO reader closes before rev-list returns, making EPIPE deterministic.
receiver /
run ait future offer src/stock.c cond.yaml -m broken-pipe
mkfifo "$T/events-pipe"
(exec 3<"$T/events-pipe"; exec 3<&-; touch "$T/pipe-closed") &
reader=$!
env AIT_SESSION="$BOB" PATH="$TP/round-bin:$PATH" MODE=pipe ait listen >"$T/events-pipe" 2>"$T/err" &
listener=$!
wait "$reader"
wait "$listener"
rc=$?
check_rc 'a reader gone terminates listen with SIGPIPE' 141
check 'EPIPE commits no keys or tip snapshot' 'no_seen && unchanged_tips'
done_testing
