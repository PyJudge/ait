#!/bin/sh
# Install the ait binary of this bundle (macOS arm64) into $PREFIX/bin, default ~/.local/bin.
# The hooks ait installs run "ait" from PATH, so that directory must be on PATH.
set -e
here=$(cd "$(dirname "$0")" && pwd)
prefix=${PREFIX:-$HOME/.local}
mkdir -p "$prefix/bin"
cp "$here/bin/ait" "$prefix/bin/ait"
chmod 755 "$prefix/bin/ait"
echo "installed $prefix/bin/ait"
case ":$PATH:" in
*":$prefix/bin:"*) ;;
*) echo "warning: '$prefix/bin' is not on PATH; add it, or the hooks will skip ait" ;;
esac
