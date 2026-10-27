#!/bin/sh
# Each snapshot has one owner, including repeated reads of the same object.
. "$(dirname "$0")/lib.sh"
setup
{
	printf 'body: |\n'
	awk 'BEGIN { for (i = 0; i < 2048; i++) print "  abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijkl" }'
} >cond.yaml
run ait future offer src/stock.c cond.yaml -m snapshot
check_rc 'prepare a stored snapshot' 0
test_have_prereq RLIMIT "each snapshot has one owner, measured by the helper" || { done_testing; exit $?; }
set -- -std=gnu99 -O1 -Wall -Werror -Wdeclaration-after-statement -I"$AIT_DIR" -I"$AIT_DIR/libyaml"
case $(uname -s) in Linux) set -- "$@" -DHAVE_GETRANDOM ;; Darwin) set -- "$@" -DHAVE_ARC4RANDOM -DPRECOMPOSE_UNICODE ;;
*MINGW*) set -- "$@" -DWIN32 -DHAVE_RTLGENRANDOM -DNO_POLL -DNO_SETENV -DNO_OPENSSL -DNOGDI -D__USE_MINGW_ANSI_STDIO=0 \
	-DDETECT_MSYS_TTY -I"$AIT_DIR/git" -I"$AIT_DIR/git/compat/win32" -I"$AIT_DIR/git/compat/poll" ;;
esac
# macOS compact allocator bookkeeping grows during warmup; use its standard zone for live-allocation checks.
compiler=${CC:-cc}
"$compiler" "$@" -Dmain=ait_command_main -c "$AIT_DIR/ait.c" -o "$T/ait.o" || exit 1
for source in "$AIT_DIR"/*.c; do
	case $source in */ait.c) continue ;; esac
	set -- "$@" "$source"
done
set -- "$@" "$AIT_DIR/t/helper-snapshot-lifetime.c" "$T/ait.o" "$AIT_DIR"/libyaml/*.o
case $(uname -s) in Darwin) set -- "$@" -liconv ;; *MINGW*) set -- "$@" "$AIT_DIR"/git/compat/*.o "$AIT_DIR"/git/compat/*/*.o "$AIT_DIR"/git/*.o -municode -lws2_32 -lntdll ;; esac
"$compiler" "$@" -o "$T/snapshot-lifetime" || exit 1
run "$T/snapshot-lifetime" branch
check_rc 'two branch owners hold independent contracts' 0
run "$T/snapshot-lifetime" tree
check_rc 'two tree owners hold independent entries' 0
run env MallocNanoZone=0 "$T/snapshot-lifetime" repeat
check_rc '200 reads of a fixed 128 KiB contract release raw and parsed values' 0
set -- -std=gnu99 -O1 -Wall -Werror -Wdeclaration-after-statement -I"$AIT_DIR" -I"$AIT_DIR/libyaml"
case $(uname -s) in Linux) set -- "$@" -DHAVE_GETRANDOM ;; Darwin) set -- "$@" -DHAVE_ARC4RANDOM -DPRECOMPOSE_UNICODE ;; esac
for source in "$AIT_DIR"/*.c; do
	case $source in */ait.c|*/notify.c) continue ;; esac
	set -- "$@" "$source"
done
set -- "$@" "$AIT_DIR/t/helper-round-lifetime.c" "$AIT_DIR"/libyaml/*.o
case $(uname -s) in Darwin) set -- "$@" -liconv ;; esac
"$compiler" "$@" -o "$T/round-lifetime" || exit 1
run env MallocNanoZone=0 "$T/round-lifetime"
check_rc '200 empty rounds retain no session, checkout or reference helper buffers' 0
cat >"$T/minimal.yaml" <<'YAML'
condition-id: R000000000000
change-id: R000000000000
kind: offer
creator: alice
provider: "src/stock.c"
state: future
specs:
- body: tiny
undertakes: []
undertakes-partially: []
fulfilled:
YAML
blob=$(git hash-object -w "$T/minimal.yaml")
for i in $(seq 1 100); do printf '100644 blob %s\tO%012x.yaml\n' "$blob" "$i"; done >"$T/many-tree"
tree=$(git mktree <"$T/many-tree")
commit=$(printf 'fixed many-contract snapshot\n' | git commit-tree -p ait/master "$tree")
git update-ref refs/heads/ait/master "$commit"
run env MallocNanoZone=0 "$T/snapshot-lifetime" many
check_rc '100 fixed small contracts retain no parser scratch after 200 reads' 0
done_testing
