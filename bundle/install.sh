#!/bin/sh
# Install the ait binary of this bundle (macOS arm64) into $PREFIX/bin, default ~/.local/bin.
# The hooks ait installs run "ait" from PATH, so the ait found first on PATH serves every repository's hooks.
set -e
here=$(cd "$(dirname "$0")" && pwd)
prefix=${PREFIX:-$HOME/.local}
dest="$prefix/bin/ait"
if [ -e "$dest" ] && ! cmp -s "$here/bin/ait" "$dest"; then
	echo "warning: replacing the ait at '$dest'; repositories set up by another ait (.ait/init 'version 1') stop until they move to 'version 2'"
	echo "hint: to keep it, install elsewhere: 'PREFIX=\$HOME/.local/ait sh $here/install.sh', and put that bin first on PATH where you use this ait"
fi
mkdir -p "$prefix/bin"
cp "$here/bin/ait" "$dest"
chmod 755 "$dest"
echo "installed $dest"
case ":$PATH:" in
*":$prefix/bin:"*) ;;
*) echo "warning: '$prefix/bin' is not on PATH; add it, or the hooks will skip ait" ;;
esac
