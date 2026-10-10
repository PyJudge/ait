#!/bin/sh
# git log --graph of the contract commits, each with the records it changed:
# kind (demand, offer, undertake), and state before -> after (a merge: what it brought in).
# Reads git only and changes nothing; ait-mini has no peek or listen to show this.
#   sh ait-graph.sh [git log options...]     (default: --branches)
[ $# -gt 0 ] || set -- --branches
state() { git show "$1:.ait/$2" 2>/dev/null | sed -n 's/^state //p'; }
git log --graph --format='%h %s' "$@" -- .ait/ | while IFS= read -r line; do
	printf '%s\n' "$line"
	c=$(printf '%s\n' "$line" | sed -n 's/^[^0-9a-f]*\([0-9a-f]\{7,\}\) .*/\1/p')
	[ -n "$c" ] || continue
	pad=$(printf '%s\n' "$line" | sed 's/\*.*//; s/[\\/]/|/g')
	git diff-tree -r --no-commit-id --name-only "$c^" "$c" -- .ait/ 2>/dev/null | while read -r p; do
		id=${p#.ait/}
		case $id in
		D*) kind=demand ;; O*) kind=offer ;; undertake/*) kind=undertake ;; *) continue ;;
		esac
		was=$(state "$c^" "$id"); now=$(state "$c" "$id")
		printf '%s|   %s %s %s\n' "$pad" "$kind" "${id#undertake/}" "${was:-new} -> ${now:-gone}"
	done
done
