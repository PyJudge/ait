#!/bin/sh
# spec 2.1.3, 4.4, 6.7.2: a fixed new ID is rechecked after a lost CAS.
. "$(dirname "$0")/lib.sh"
setup
git worktree add -qb other-code "$T/other" master
(cd "$T/other" && ait session open --agent other >/dev/null)
mkdir -p "$T/bin"
cat > "$T/bin/git" <<'GIT'
#!/bin/sh
real=$(PATH=$(echo "$PATH" | sed "s|$T/bin:||") command -v git)
case " $* " in
*' update-ref '*' --stdin '*)
	if [ ! -e "$T/raced" ]; then
		: > "$T/raced"
		cat > "$T/cas-input"
		read action ref new old < "$T/cas-input"
		path=$("$real" diff-tree --no-commit-id --name-only -r "$old" "$new")
		id=$(basename "$path" .yaml)
		if [ "$collision" = same ]; then rival=$path; else rival=rival/$id.yaml; fi
		mkdir -p "$T/other/.ait/$(dirname "$rival")"
		"$real" show "$new:$path" > "$T/other/.ait/$rival"
		(cd "$T/other" && PATH=$(echo "$PATH" | sed "s|$T/bin:||") && export PATH &&
		 ait contract add ".ait/$rival" >/dev/null &&
		 ait contract commit -m 'concurrent same identity' >/dev/null) || exit 1
		"$real" rev-parse ait/master > "$T/rival-tip"
		printf '%s\n' "$id" > "$T/rival-id"
		exec "$real" "$@" < "$T/cas-input"
	fi
	;;
esac
exec "$real" "$@"
GIT
chmod +x "$T/bin/git"

for entry in request draft; do
	for collision in same other; do
		export collision
		rm -f "$T/raced"
		if [ "$entry" = draft ]; then
			run ait contract new offer ours
			draft=$(sed -n 's/^path //p' "$T/out")
			sed -i.bak 's#provider: "<scope>"#provider: "src/stock.c"#' "$draft"
			ait contract add "$draft" >/dev/null
			stage=$(git rev-parse --git-dir)/ait/index
			cp "$stage" "$T/saved-stage"
			cp "$draft" "$T/saved-draft"
			run env PATH="$T/bin:$PATH" ait contract commit -m 'new staged identity'
		else
			run env PATH="$T/bin:$PATH" ait future offer src/stock.c cond.yaml -g ours -m 'new generated identity'
		fi
		check_rc "$entry safely refuses a concurrent ID in the $collision group" 1
		check "$entry leaves only the concurrent $collision-group checkin" \
			"test \"\$(git rev-parse ait/master)\" = \"\$(cat $T/rival-tip)\""
		id=$(cat "$T/rival-id")
		check "$entry leaves exactly one file for the colliding ID ($collision)" \
			"test \"\$(git ls-tree -r --name-only ait/master | grep -c /$id.yaml\$)\" = 1"
		if [ "$entry" = request ]; then
			check "the generated ID collision is reported as a moved branch ($collision)" "has err 'moved while running; nothing changed'"
		else
			check "the refused draft and its stage remain intact ($collision)" \
				"cmp -s $stage $T/saved-stage && cmp -s $draft $T/saved-draft"
			rm -f "$stage" "$draft" "$draft.bak"
		fi
		run ait status
		check_rc "the branch remains readable after $entry/$collision collision" 0
	done
done

done_testing
