#!/bin/sh
# spec 4.3, 4.5 and 6.4: syntax before state; messages survive Git's trailer reader.
. "$(dirname "$0")/lib.sh"
setup

run ait future offer src/stock.c cond.yaml -m seed
check_rc 'prepare a valid contract branch' 0
good=$(git rev-parse ait/master)
blob=$(printf 'not a contract\n' | git hash-object -w --stdin)
tree=$(printf '100644 blob %s\tO000000000000.yaml\n' "$blob" | git mktree)
bad=$(printf 'malformed\n' | git commit-tree -p "$good" "$tree")
cut='# ------------------------ >8 ------------------------'
message=$(printf 'explain\n\n%s\nhidden\n' "$cut")
printf '%s\n' "$message" >"$T/message"

# A known kind determines amend's grammar without loading a contract.
for state in no-branch malformed invalid-session; do
	session=$ALICE
	case $state in
	no-branch) git update-ref -d refs/heads/ait/master ;;
	malformed) git update-ref refs/heads/ait/master "$bad" ;;
	invalid-session)
		git update-ref refs/heads/ait/master "$good"
		session=S000000000000
		;;
	esac
	run env AIT_SESSION="$session" ait future amend D000000000000 - - -m grammar
	check_rc "demand arity precedes $state" 129
	run env AIT_SESSION="$session" ait future amend O000000000000 - -m grammar
	check_rc "offer arity precedes $state" 129
	run env AIT_SESSION="$session" ait future amend X000000000000 - - -m grammar
	check_rc "drop arity precedes $state" 129
	run env AIT_SESSION="$session" ait future amend O000000000000 -p - -m grammar
	check_rc "partial belongs to undertake, not an amend word, before $state" 129
	run env AIT_SESSION="$session" ait future offer src/stock.c cond.yaml as item future amend item - -m grammar
	check_rc "an earlier offer alias determines arity before $state" 129
	run env AIT_SESSION="$session" ait future demand src/order.c src/stock.c cond.yaml as item future amend item - - -m grammar
	check_rc "an earlier demand alias determines arity before $state" 129
	run env AIT_SESSION="$session" ait future drop O000000000000 as item future amend item - - -m grammar
	check_rc "an earlier drop alias determines arity before $state" 129
	run env AIT_SESSION="$session" ait future offer src/stock.c cond.yaml -m "$message"
	check_rc "a scissors message precedes $state" 129
	forged=$(printf 'explain\n\nAit-Request: R000000000000\nAit-Agent: ait\nAit-Session: S000000000000\n\n%s\nhidden\n' "$cut")
	run env AIT_SESSION="$session" ait future offer src/stock.c cond.yaml -m "$forged"
	check_rc "an earlier trailer block cannot impersonate the appended probe before $state" 129
	check "scissors reports its actual line before $state" "has err \"the message hides the trailers: '$cut'\" && has err '^hint: remove that line from the message$'"
	# A valid empty amend still reaches the common state checks.
	run env AIT_SESSION="$session" ait future amend O000000000000 -m grammar
	check_rc "valid amend grammar retains $state priority" 128
	done


# Every readable sentinel value must be unique, even if config hides the RID key.
git config core.commentString Ait-Request
partial=$(printf 'explain\n\nAit-Agent: ait\nAit-Session: S000000000000\n\nAit-Request ------------------------ >8 ------------------------\nhidden\n')
git update-ref -d refs/heads/ait/master
run ait future offer src/stock.c cond.yaml -m "$partial"
check_rc 'a partially hidden probe cannot be impersonated before common state' 129
check 'a partially hidden probe reports the actual scissors line' "has err \"the message hides the trailers: 'Ait-Request ------------------------ >8 ------------------------'\""
git config --unset-all core.commentString

# Git writes and decodes the message; the raw input bytes are not the history view.
git config i18n.commitEncoding ISO-8859-1
git config core.commentString 'é'
printf 'explain\n\n\351 ------------------------ >8 ------------------------\nhidden\n' >"$T/encoded-message"
for state in no-branch valid; do
	case $state in
	no-branch) git update-ref -d refs/heads/ait/master ;;
	valid) git update-ref refs/heads/ait/master "$good" ;;
	esac
	run ait future offer src/stock.c cond.yaml -F "$T/encoded-message"
	check_rc "a cutoff created by Git decoding is usage before $state" 129
	check "a decoded cutoff reports the exact history LINE before $state" "has err \"the message hides the trailers: 'é ------------------------ >8 ------------------------'\""
done
check 'an encoded rejected message preserves the contract ref' "test \"\$(git rev-parse ait/master)\" = $good"
printf 'caf\351\n' >"$T/encoded-readable"
run ait future offer src/stock.c cond.yaml -F "$T/encoded-readable"
check_rc 'a readable non-UTF8 commit encoding remains supported' 0
check 'the real encoded commit retains its three metadata values' \
	"test \"\$(git -c i18n.logOutputEncoding=UTF-8 log -1 --format='%(trailers:key=Ait-Agent,valueonly)' ait/master)\" = alice && test \"\$(git -c i18n.logOutputEncoding=UTF-8 log -1 --format='%(trailers:key=Ait-Session,valueonly)' ait/master)\" = $ALICE && git cat-file commit ait/master | grep -qx 'encoding ISO-8859-1'"
git config --unset-all i18n.commitEncoding
run ait future offer src/stock.c cond.yaml -F "$T/encoded-message"
check_rc 'Git UTF8 repair cannot create an unchecked cutoff either' 129
check 'the repaired cutoff reports the decoded LINE' "has err \"the message hides the trailers: 'é ------------------------ >8 ------------------------'\""
git config --unset-all core.commentString

git update-ref refs/heads/ait/master "$good"
for source in argument file stdin; do
	case $source in
	argument) run ait future offer src/stock.c cond.yaml -m "$message" ;;
	file) run ait future offer src/stock.c cond.yaml -F "$T/message" ;;
	stdin) run ait future offer src/stock.c cond.yaml -F - <"$T/message" ;;
	esac
	check_rc "scissors is rejected from $source" 129
	check "a rejected $source message leaves the contract ref unchanged" "test \"\$(git rev-parse ait/master)\" = $good"
	git update-ref refs/heads/ait/master "$good"
done


# The parser decides; a failed Git parser cannot be replaced by a C cutoff verdict.
racing_git
printf '%s\n' "git config core.commentString ''" >"$T/before-interpret-trailers"
run env PATH="$T/bin:$PATH" ait future offer src/stock.c cond.yaml -m "$message"
check_rc 'an actual Git parser failure precedes a scissors diagnosis' 128
check 'a parser failure retains E_GIT rather than a guessed usage error' "has err \"git failed: 'git interpret-trailers'\""
git config --unset-all core.commentString || :
rm -f "$T/before-interpret-trailers" "$T/before-interpret-trailers.done"
git update-ref refs/heads/ait/master "$good"

# Git 2.53 is the minimum runtime; the shim changes the reported version only.
printf '%s\n' "printf 'git version 2.52.0\\n'" >"$T/before-version"
run env PATH="$T/bin:$PATH" ait future offer src/stock.c cond.yaml -m version
check_rc 'Git 2.52 is below the supported runtime' 128
check 'the upgrade hint matches the supported Git 2.53 floor' "has err '^fatal: git 2.52 is too old for ait$' && has err '^hint: install git 2.53 or later$'"
rm -f "$T/before-version" "$T/before-version.done"
git update-ref refs/heads/ait/master "$good"

# auto in trailer readers is '#'; commit's automatic candidate choice is unrelated.
git config core.commentChar auto
run ait future offer src/stock.c cond.yaml -m "$(printf '# user\n\n%s\nhidden\n' "$cut")"
check_rc 'auto keeps hash as the trailer cutoff prefix' 129
run ait future offer src/stock.c cond.yaml -m "$(printf '# user\n\n; ------------------------ >8 ------------------------\nvisible\n')"
check_rc 'auto does not choose a semicolon prefix from the user message' 0
git update-ref refs/heads/ait/master "$good"
git config --unset-all core.commentChar


# A normal message cannot hide metadata through user configuration.
for setting in separators-empty separators-equals comment-prefix key-with-colon key-renamed; do
	case $setting in
	separators-empty) key=trailer.separators; value='' ;;
	separators-equals) key=trailer.separators; value='=' ;;
	comment-prefix) key=core.commentstring; value=Ait- ;;
	key-with-colon) key=trailer.request.key; value='Ait-Request: ' ;;
	key-renamed) key=trailer.Ait-Request.key; value=Hidden-Request ;;
	esac
	git config "$key" "$value"
	run ait future offer src/stock.c cond.yaml -m configuration
	check_rc "unreadable metadata is a configuration error: $setting" 128
	check "the configuration error names the missing key and setting: $setting" \
		"has err '^fatal: git config hides the trailers:' && has err \"'Ait-Request'\" && has err \"'$key'\" && ! has err '^hint:'"
	check "refusing $setting preserves the contract ref" "test \"\$(git rev-parse ait/master)\" = $good"
	git config --unset-all "$key"
	git update-ref refs/heads/ait/master "$good"
done

# Output separator normalization does not make these readable settings invalid.
for setting in mixed-separators canonical-key; do
	case $setting in
	mixed-separators) key=trailer.separators; value='=:' ;;
	canonical-key) key=trailer.request.key; value=Ait-Request ;;
	esac
	git config "$key" "$value"
	run ait future offer src/stock.c cond.yaml -m readable
	check_rc "readable metadata settings are accepted: $setting" 0
	check "the actual history reader sees the values: $setting" \
		"test \"\$(git log -1 --format='%(trailers:key=Ait-Agent,valueonly)' ait/master)\" = alice && test \"\$(git log -1 --format='%(trailers:key=Ait-Session,valueonly)' ait/master)\" = $ALICE"
	git config --unset-all "$key"
	git update-ref refs/heads/ait/master "$good"
done

# The effective prefix comes from Git's config ordering, including environment overrides.
git config core.commentString //
slash=$(printf 'explain\n\n// ------------------------ >8 ------------------------\nhidden\n')
run ait future offer src/stock.c cond.yaml -m "$slash"
check_rc 'a multi-character prefix supplies the actual scissors line' 129
check 'the multi-character LINE is reported exactly' "has err \"the message hides the trailers: '// ------------------------ >8 ------------------------'\""
run env GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=core.commentChar GIT_CONFIG_VALUE_0=';' \
	ait future offer src/stock.c cond.yaml -m "$(printf 'explain\n\n; ------------------------ >8 ------------------------\nhidden\n')"
check_rc 'environment commentChar overrides the earlier local commentString' 129
git config --unset-all core.commentString

# Git chooses only the appended block, even after a divider or a user trailer paragraph.
run ait future offer src/stock.c cond.yaml -m 'explain' -m '---' -m 'Ait-Request: user'
check_rc 'a normal divider and earlier user trailers do not hide the appended block' 0
check 'the request value is the generated RID, without the earlier user value' \
	"git log -1 --format='%(trailers:key=Ait-Request,valueonly)' ait/master | grep -Eq '^R[0-9a-f]{12}$' && ! git log -1 --format='%(trailers:key=Ait-Request,valueonly)' ait/master | grep -q user"
git update-ref refs/heads/ait/master "$good"

# Config refusal happens after common state checks and before command-specific checks.
git config trailer.separators =
git update-ref -d refs/heads/ait/master
run ait future offer src/stock.c missing.yaml -m configuration
check_rc 'a missing branch precedes the config check' 128
check 'the missing branch keeps its prescribed diagnostic' "has err '^fatal: no ait branch$'"
git update-ref refs/heads/ait/master "$good"
run ait future offer src/stock.c missing.yaml -m configuration
check_rc 'config is checked before reading the command condition file' 128
check 'the command file error does not mask a config conflict' "has err '^fatal: git config hides the trailers:'"
git config --unset-all trailer.separators

# Both writers reject before a code commit or clearing the contract stage.
draft=$(git ls-tree -r --name-only "$good" | sed -n '/O[0-9a-f]*\.yaml$/p' | head -1)
id=$(basename "$draft" .yaml)
mkdir -p ".ait/$(dirname "$draft")"
git show "$good:$draft" >".ait/$draft"
ait contract add ".ait/$draft" >/dev/null
stage=$(git rev-parse --path-format=absolute --git-path ait/index)
cp "$stage" "$T/contract-stage"
printf 'changed\n' >>src/stock.c
git add src/stock.c
code=$(git rev-parse HEAD)
git ls-files -s -z >"$T/code-stage"
git config trailer.separators =
run ait fulfill "$id" --force -m configuration
check_rc 'fulfill rejects a config conflict before committing code' 128
check 'a config refusal preserves code HEAD, contract ref and staged entries' \
	"test \"\$(git rev-parse HEAD)\" = $code && test \"\$(git rev-parse ait/master)\" = $good && git ls-files -s -z >'$T/code-stage-after' && cmp -s '$T/code-stage' '$T/code-stage-after'"
# Restore through Git so a failed assertion cannot corrupt the next fixture.
git reset --soft "$code"
git update-ref refs/heads/ait/master "$good"
run ait contract commit -m configuration
check_rc 'contract commit rejects a config conflict before consuming drafts' 128
check 'the contract stage survives the config refusal byte for byte' "cmp -s '$T/contract-stage' \"$stage\""
cp "$T/contract-stage" "$stage"
git config --unset-all trailer.separators

# A no-op checkin must not accumulate synthetic validation commits in the real ODB.
# Preparing the staged-code tree is legitimate; isolate the message probe from that write.
git write-tree >/dev/null
git count-objects -v >"$T/objects-before"
run ait contract commit -m 'identical staged draft'
check_rc 'a readable config permits an identical staged draft' 0
check 'validation adds no real repository objects for a no-op checkin' \
	"git count-objects -v >'$T/objects-after' && cmp -s '$T/objects-before' '$T/objects-after'"

# A failure of the actual metadata reader cleans its private temporary resources.
mkdir "$T/probe-tmp"
printf '%s\n' "git config core.commentString ''" >"$T/before-log"
run env PATH="$T/bin:$PATH" TMPDIR="$T/probe-tmp" ait future offer src/stock.c cond.yaml -m reader
check_rc 'an actual metadata reader failure remains E_GIT' 128
check 'a reader failure leaves no temporary validation files' "test -z \"\$(find '$T/probe-tmp' -mindepth 1 -print -quit)\""
git config --unset-all core.commentString || :
rm -f "$T/before-log" "$T/before-log.done"



# A real failed loose-object writer leaves tmp_obj_* before the parent cleans the ODB.
mkdir "$T/writer-bin" "$T/writer-tmp"
REAL_GIT=$(command -v git); export REAL_GIT
cat >"$T/writer-bin/git" <<'GIT'
#!/bin/sh
if test -n "$GIT_OBJECT_DIRECTORY"; then
	(ulimit -f 0; trap '' XFSZ; "$REAL_GIT" "$@")
	result=$?
	find "$GIT_OBJECT_DIRECTORY" -name 'tmp_obj_*' >"$T/partial-object"
	if test -n "$WAIT_FOR_CLEANUP"; then
		trap 'touch "$T/writer-terminating"; while ! test -e "$T/writer-exit"; do sleep .05; done; exit 143' TERM
		echo "$$" >"$T/writer-ready"
		while :; do sleep .05; done
	fi
	exit "$result"
fi
exec "$REAL_GIT" "$@"
GIT
chmod +x "$T/writer-bin/git"
run env PATH="$T/writer-bin:$PATH" TMPDIR="$T/writer-tmp" ait future offer src/stock.c cond.yaml -m writer
check_rc 'a failed real object writer is fatal' 128
check 'the fixture actually creates an incomplete Git loose object' 'test -s "$T/partial-object"'
check 'partial-object cleanup preserves the original Git error and has no IO hint' \
	"has err \"fatal: git failed: 'git \" && ! has err '^warning:' && ! has err '^hint:'"
check 'a failed writer leaves no private object directory or files' "test -z \"\$(find '$T/writer-tmp' -mindepth 1 -print -quit)\""
# Retain failed-fixture evidence above, but isolate the signal case.
rm -rf "$T/writer-tmp"
mkdir "$T/writer-tmp"
env PATH="$T/writer-bin:$PATH" TMPDIR="$T/writer-tmp" WAIT_FOR_CLEANUP=1 \
	ait future offer src/stock.c cond.yaml -m interrupted >"$T/out" 2>"$T/err" &
parent=$!
for attempt in $(seq 1 100); do
	test -e "$T/writer-ready" && break
	sleep .05
done
if test -e "$T/writer-ready"; then
	child=$(cat "$T/writer-ready")
	kill -TERM "$parent"
	for attempt in $(seq 1 100); do
		test -e "$T/writer-terminating" && break
		sleep .05
	done
	check 'signal cleanup waits for the writer before removing its private ODB' \
		"test -e '$T/writer-terminating' && kill -0 $child && test -n \"\$(find '$T/writer-tmp' -name 'tmp_obj_*' -print -quit)\""
	touch "$T/writer-exit"
	wait "$parent"
	rc=$?
	check 'SIGTERM reaps the writer and removes its whole private ODB' \
		"test $rc -gt 128 && ! kill -0 $child 2>/dev/null && test -z \"\$(find '$T/writer-tmp' -mindepth 1 -print -quit)\""
else
	kill -TERM "$parent" 2>/dev/null || :
	wait "$parent"
	say_fail 'the signal fixture reaches the real partial object writer'
fi

# A relative TMPDIR is interpreted at the caller, even when Git runs at the repository root.
mkdir relative-probes
run sh -c 'cd src && TMPDIR=../relative-probes ait future offer src/stock.c cond.yaml -m relative'
check_rc 'message validation works with a relative TMPDIR from a subdirectory' 0
check 'successful validation removes the relative temporary object directories' \
	"test -z \"\$(find relative-probes -mindepth 1 -print -quit)\""

done_testing
