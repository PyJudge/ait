#!/bin/sh
# SessionStart hook: in a Claude cloud session only, build the ait this repository
# carries with the VM's compiler and install it; a resumed session already has it.
[ "$CLAUDE_CODE_REMOTE" = true ] || exit 0
set -e
kit=$(cd "$(dirname "$0")" && pwd)
installed=/usr/local/share/ait/COMMIT
cmp -s "$kit/COMMIT" "$installed" 2>/dev/null && exit 0
build=$(mktemp -d)
trap 'rm -rf "$build"' 0
cp -R "$kit/src/." "$build"
# stdout of a SessionStart hook goes to Claude; the build log does not belong there
make -C "$build" install prefix=/usr/local >&2
cp "$kit/COMMIT" "$installed"
