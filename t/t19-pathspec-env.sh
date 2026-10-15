#!/bin/sh
# Ait's repository paths are literal; native Git keeps the caller's pathspec environment.
. "$(dirname "$0")/lib.sh"
setup
printf 'original\n' > src/f
printf 'original\n' > 'src/name*'
printf 'original\n' > src/nameX
git add -- src/f 'src/name*' src/nameX
git commit -qm paths
printf 'changed\n' > src/f
printf 'changed\n' > 'src/name*'
printf 'changed\n' > src/nameX

run env GIT_ICASE_PATHSPECS=1 ait status src/F
check_rc "ait accepts a literal path independently of caller case magic" 0
check "ait does not widen a case-sensitive repository path" "! has out 'modified:.*src/f'"
for setting in GIT_GLOB_PATHSPECS=1 GIT_NOGLOB_PATHSPECS=1 GIT_ICASE_PATHSPECS=1 GIT_LITERAL_PATHSPECS=0 GIT_LITERAL_PATHSPECS=1
do
	run env "$setting" ait status 'src/name*'
	check_rc "ait defines its own pathspec mode with $setting" 0
	check "ait preserves the exact filename with $setting" "has out 'modified:.*src/name\*' && ! has out 'modified:.*src/nameX'"
done
run env GIT_GLOB_PATHSPECS=1 GIT_NOGLOB_PATHSPECS=1 GIT_ICASE_PATHSPECS=1 ait status 'src/name*'
check_rc "ait's common boundary also isolates conflicting inherited pathspec modes" 0
check "combined caller modes do not change ait filename selection" "has out 'modified:.*src/name\*' && ! has out 'modified:.*src/nameX'"

run env GIT_GLOB_PATHSPECS=1 ait checkout -- 'src/name*'
check_rc "native Git retains caller glob mode" 0
check "native glob checkout selects both matching paths" "grep -qx original 'src/name*' && grep -qx original src/nameX"
printf 'changed\n' > 'src/name*'
printf 'changed\n' > src/nameX
run env GIT_NOGLOB_PATHSPECS=1 ait checkout -- 'src/name*'
check_rc "native Git retains caller noglob mode" 0
check "native noglob checkout selects the literal star filename" "grep -qx original 'src/name*' && grep -qx changed src/nameX"
run env GIT_ICASE_PATHSPECS=1 ait checkout -- src/F
check_rc "native Git retains caller case-insensitive mode" 0
check "native case magic is interpreted by Git" "grep -qx original src/f"
done_testing
