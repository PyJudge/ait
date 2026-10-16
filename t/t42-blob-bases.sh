#!/bin/sh
# spec 3.5, 5.3 and 6.7.4: range bases must be actual blobs, not merely existing objects.
. "$(dirname "$0")/lib.sh"
setup
run ait future offer src/stock.c:5-9 cond.yaml -m range
O=$(newest O)
future=$(git rev-parse ait/master)
yaml "$O" >"$T/contract"
stock=$(git rev-parse HEAD:src/stock.c)
run ait fulfill "$O" --force -m record
check_rc 'prepare a recorded line range' 0
recorded=$(git rev-parse ait/master)
file=$(git ls-tree -r --name-only "$recorded" fulfill/ | head -1)
git show "$recorded:$file" >"$T/fulfilment"
git tag -a object-type -m tag HEAD
code_head=$(git rev-parse HEAD)

# Change stored facts with Git, starting each case from the same valid snapshot.
publish() {
	rm -f "$T/index"
	GIT_INDEX_FILE="$T/index" git read-tree "$1"
	blob=$(git hash-object -w "$3")
	GIT_INDEX_FILE="$T/index" git update-index --add --cacheinfo "100644,$blob,$2"
	tree=$(GIT_INDEX_FILE="$T/index" git write-tree)
	tip=$(printf 'stored base type\n' | git commit-tree -p "$1" "$tree")
	git update-ref refs/heads/ait/master "$tip"
}
for source in provider recorded; do
	for type in missing commit tree blob tag; do
		case $type in
		missing) base=1111111111111111111111111111111111111111 ;;
		commit) base=$(git rev-parse HEAD) ;;
		tree) base=$(git rev-parse 'HEAD^{tree}') ;;
		blob) base=$stock ;;
		tag) base=$(git rev-parse refs/tags/object-type) ;;
		esac
		if test "$source" = provider; then
			sed "s/ $stock\"/ $base\"/" "$T/contract" >"$T/changed"
			publish "$future" "$O.yaml" "$T/changed"
		else
			sed "s/blob $stock /blob $base /" "$T/fulfilment" >"$T/changed"
			publish "$recorded" "$file" "$T/changed"
		fi
		tip=$(git rev-parse ait/master)
		run ait status src/stock.c
		check_rc "$source $type: status accepts stored syntax" 0
		if test "$source" = provider; then
			if test "$type" = blob; then
				check "$source $type: display maps the actual blob" "has out '^provider src/stock.c:5-9$'"
			else
				check "$source $type: display reports the unusable base" "has out '^provider src/stock.c:5-9 ?nobase$' && ! has err 'bad format'"
			fi
		else
			if test "$type" = blob; then want=current; else want='current ?unverified'; fi
			check "$source $type: judgment uses only actual blob code bases" "grep -A3 '^id $O$' '$T/out' | grep -qx 'state $want'"
		fi
		rm -f .git/ait/check-result.yaml
		run ait fulfill "$O" -m inspect
		if test "$source" = provider && test "$type" != blob; then
			check_rc "$source $type: template refuses the unusable base" 128
			check "$source $type: NOBASE has its specified reason and repair hint" \
				"has err \"repository lacks the base blob of '$O' ('src/stock.c')\" && has err \"hint: ait future amend $O src/stock.c:5-9 - -m <why>\" && ! test -e .git/ait/check-result.yaml"
		else
			check_rc "$source $type: a valid provider can request fresh results" 1
			check "$source $type: the template has the actual task" "has out 'task $O:1' && test -e .git/ait/check-result.yaml"
		fi
		check "$source $type: template does not publish code or contract changes" \
			"test \"\$(git rev-parse HEAD)\" = $code_head && test \"\$(git rev-parse ait/master)\" = $tip"
		run ait fulfill "$O" --force -m retry
		if test "$source" = provider && test "$type" != blob; then
			check_rc "$source $type: force also uses the shared NOBASE check" 128
			check "$source $type: force preserves both refs" \
				"test \"\$(git rev-parse HEAD)\" = $code_head && test \"\$(git rev-parse ait/master)\" = $tip"
		else
			check_rc "$source $type: the usable provider can be fulfilled again" 0
		fi
	done
done
done_testing
