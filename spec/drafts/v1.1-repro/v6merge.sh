#!/bin/bash
# v6merge.sh DIR : merge behaviour of contract YAML laid out as spec 2.3.2
cd "$1" || exit 1
new() { git init -q "$1"; cd "$1"; git config user.name t; git config user.email t@t; git checkout -q --orphan ait/master; }
c() { git add -A; git commit -qm "$1"; }
K() { # rev body fulfilled-lines-or-NONE
	printf 'rev: %s\nedited: %s\nkind: demand\ncreator: a\nconsumer: src/ui/\nprovider: a.c\nstate: future\nspecs:\n  - body: |\n      %s\n' "$1" "$1" "$2" > K.yaml
	case "$3" in NONE) ;; *) printf 'fulfilled:\n%b' "$3" >> K.yaml ;; esac
}

# A) base omits the empty fulfilled: key
new a; K R0 old NONE; c base; git branch try
K R1 new NONE; c amend
git checkout -q try; K R0 old '  - fulfill/K-R9.md\n'; c fulfil
git merge-tree --write-tree --name-only ait/master try >/dev/null; echo "A) fulfilled omitted: merge exit=$?"
cd ..

# B) base keeps an empty fulfilled: key
new b; K R0 old ''; c base; git branch try
K R1 new ''; c amend
git checkout -q try; K R0 old '  - fulfill/K-R9.md\n'; c fulfil
git merge-tree --write-tree --name-only ait/master try >/dev/null; echo "B) fulfilled kept:    merge exit=$?"
cd ..

# C) whole undertake on one branch, partial on the other
new m; O() { printf 'kind: offer\nundertakes: %s\nundertakes-partially: %s\n' "$2" "$3" > "$1.yaml"; }
O O1 '[]' '[]'; O O2 '[]' '[]'; c base; git branch try
O O1 '[D]' '[]'; c whole
git checkout -q try; O O2 '[]' '[D]'; c partial
T=$(git merge-tree --write-tree ait/master try); echo "C) whole vs partial:  merge exit=$?"
for f in O1.yaml O2.yaml; do echo "   $f: $(git cat-file -p "$T:$f" | tr '\n' ' ')"; done
