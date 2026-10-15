#!/bin/sh
# Native checkout operands retain the caller's directory, unlike ait scopes.
. "$(dirname "$0")/lib.sh"
setup
mkdir -p sub
printf 'root original\n' > file
printf 'sub original\n' > sub/file
git add file sub/file
git commit -qm paths
printf 'root changed\n' > file
printf 'sub changed\n' > sub/file
cd sub
run ait checkout -- file
check_rc "a native checkout from a subdirectory succeeds" 0
check "native checkout restores the caller-relative file" "grep -qx 'sub original' file"
check "native checkout preserves the different root file" "grep -qx 'root changed' ../file"
printf 'sub changed again\n' > file
run ait checkout master -- file
check_rc "native checkout with a tree also retains the caller directory" 0
check "the requested tree supplies the caller-relative path" "grep -qx 'sub original' file && grep -qx 'root changed' ../file"
printf 'sub wildcard change\n' > file
run ait checkout -- 'f*'
check_rc "native checkout preserves Git wildcard pathspecs" 0
check "the wildcard is interpreted by Git at the caller directory" "grep -qx 'sub original' file && grep -qx 'root changed' ../file"
printf 'sub literal change\n' > file
run env GIT_LITERAL_PATHSPECS=1 ait checkout -- 'f*'
check_rc "native checkout also preserves an explicitly literal Git environment" 1
check "an unmatched literal operand changes no file" "grep -qx 'sub literal change' file && grep -qx 'root changed' ../file"
run ait checkout -- ':(top,literal)file'
check_rc "native checkout preserves Git top-level pathspec magic" 0
check "Git's explicit top path selects only the root file" "grep -qx 'root original' ../file && grep -qx 'sub literal change' file"
done_testing
