#!/bin/bash
# ckin.sh BRANCH OLD PATH CONTENT  (OLD may be "" for create)
set -e
I=$(mktemp -u "${TMPDIR:-/tmp}/ckidx.XXXX")
if [ -n "$2" ]; then GIT_INDEX_FILE=$I git read-tree "$2"; P=(-p "$2"); else P=(); fi
b=$(printf "$4" | git hash-object -w --stdin)
GIT_INDEX_FILE=$I git update-index --add --cacheinfo "100644,$b,$3"
t=$(GIT_INDEX_FILE=$I git write-tree); rm -f "$I"
n=$(git commit-tree "$t" "${P[@]}" -m ck)
git update-ref "refs/heads/$1" "$n" "$2"
echo "$n"
