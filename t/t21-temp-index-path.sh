#!/bin/sh
# spec 4.4: a temporary index has the same path for ait and Git's -C child.
. "$(dirname "$0")/lib.sh"
setup

mkdir -p 'src/local temp 한'
cd src
run env TMPDIR='local temp 한' ait future offer src/stock.c cond.yaml -m temporary
check_rc "checkin accepts a relative TMPDIR from a subdirectory" 0
check "the successful checkin removes its temporary directory" "test -z \"\$(ls -A 'local temp 한')\""
cd ..
check "the contract branch contains the checkin" "git log -1 --format=%s ait/master | grep -qx '\[CONTRACT\] temporary'"

# An absolute spelling continues to work, including a symlinked directory.
mkdir -p "$T/temp backing"
ln -s "$T/temp backing" "$T/temp alias"
run env TMPDIR="$T/temp alias" ait future offer src/order.c cond.yaml -m absolute
check_rc "checkin accepts an absolute symlinked TMPDIR with spaces" 0
check "the absolute temporary directory is removed" "test -z \"\$(ls -A '$T/temp backing')\""

done_testing
