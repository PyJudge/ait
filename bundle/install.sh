#!/bin/sh
# Install this release's executable and its skill/setup resources.
set -e
here=$(cd "$(dirname "$0")" && pwd)
# Windows (Git Bash) names its build number in uname and its programs with .exe
platform=$(uname -sm | sed 's/^[A-Z0-9]*_NT-[^ ]*/Windows/')
case $platform in Windows*) x=.exe ;; *) x= ;; esac
if [ "$(cat "$here/PLATFORM")" != "$platform" ]; then
	echo "fatal: this release requires $(cat "$here/PLATFORM")" >&2
	exit 128
fi
if ! git_version=$(git version) ||
	! printf '%s\n' "$git_version" | awk '
		$1 == "git" && $2 == "version" {
			split($3, v, ".")
			supported = v[1] > 2 || (v[1] == 2 && v[2] >= 38)
		}
		END { exit !supported }'; then
	echo "fatal: git 2.38 or later is required" >&2
	exit 128
fi
prefix=${PREFIX:-$HOME/.local}
mkdir -p "$prefix"
prefix=$(cd "$prefix" && pwd)
dest="$prefix/bin/ait$x"
[ ! -d "$dest" ] || { echo "fatal: '$dest' is a directory" >&2; exit 128; }
mkdir -p "$prefix/bin"
if [ -L "$dest" ] || [ ! -x "$dest" ] || ! cmp -s "$here/bin/ait$x" "$dest"; then
	# repositories' hooks run the ait found on PATH, which may be another ait than this one
	if [ -e "$dest" ] && ! cmp -s "$here/bin/ait$x" "$dest"; then
		echo "warning: replacing the ait at '$dest'; the hooks that run it now run this one" >&2
		echo "hint: to keep it, install this one elsewhere: 'PREFIX=<dir> sh $here/install.sh'" >&2
	fi
	tmp=$(mktemp "$prefix/bin/.ait.XXXXXX")
	trap 'rm -f "$tmp"' 0
	trap 'exit 1' 1 2 3 15
	cp "$here/bin/ait$x" "$tmp"
	chmod 755 "$tmp"
	mv -f "$tmp" "$dest"
fi
resources="$prefix/share/ait"
mkdir -p "$resources/skills"
cp -R "$here/skills/." "$resources/skills/"
for name in setup.sh try.sh ait-graph.sh README.md VERSION PLATFORM; do
	cp "$here/$name" "$resources/$name"
done
printf 'installed %s\nresources %s\n' "$dest" "$resources"
case ":$PATH:" in
*":$prefix/bin:"*) ;;
*) printf "hint: put '%s/bin' first on PATH before using ait\n" "$prefix" ;;
esac
