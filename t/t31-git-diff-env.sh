#!/bin/sh
# v14 5.12: parsed Git diffs ignore inherited context; native Git keeps it.
. "$(dirname "$0")/lib.sh"
setup

run ait offer src/stock.c:1-5 cond.yaml --force -m range
check_rc "a concrete row-scoped offer can be fulfilled" 0
O=$(newest O)
sed -i.bak 's/^s40$/outside-range/' src/stock.c
rm -f src/stock.c.bak
git add src/stock.c
run ait status src/stock.c
check "the control leaves a range current when only line forty changes" \
	"grep -A3 '^id $O$' $T/out | grep -qx 'state current' && has out '^provider src/stock.c:1-5$'"
cp "$T/out" "$T/normal-status"
run env GIT_DIFF_OPTS=--unified=50 ait status src/stock.c
check_rc "inherited diff context does not prevent status" 0
check "inherited diff context cannot widen or invalidate an unchanged provider range" \
	"grep -A3 '^id $O$' $T/out | grep -qx 'state current' && has out '^provider src/stock.c:1-5$'"
check "internal scope judgments are identical with inherited diff context" "cmp -s $T/normal-status $T/out"

# This preview is delegated to native Git; q accepts no hunk and changes no file.
native_preview() { printf 'q\n' | env GIT_DIFF_OPTS=--unified=50 ait checkout -p HEAD -- src/stock.c; }
git_preview() { printf 'q\n' | env GIT_DIFF_OPTS=--unified=50 git checkout -p HEAD -- src/stock.c; }
run git_preview
check_rc "Git's native patch preview accepts the caller's diff environment" 0
cp "$T/out" "$T/native-control"
check "the native control really uses the inherited fifty-line diff context" \
	"grep -q '^@@ -1,50 +1,50 @@' $T/native-control"
run native_preview
check_rc "ait's native patch preview succeeds with the same environment" 0
check "native Git delegation preserves its complete preview output" "cmp -s $T/native-control $T/out"
check "previewing leaves the staged out-of-range edit unchanged" \
	"git diff --cached --numstat | grep -qx '1[[:space:]]1[[:space:]]src/stock.c' && grep -qx outside-range src/stock.c"

sed -i.bak 's/^s2$/inside-range/' src/stock.c
rm -f src/stock.c.bak
git add src/stock.c
run ait status src/stock.c
check "a real edit in the provider range remains unverified" \
	"grep -A3 '^id $O$' $T/out | grep -qx 'state current ?unverified' && has out '^provider src/stock.c:1-5 ?changed$'"
cp "$T/out" "$T/changed-control"
run env GIT_DIFF_OPTS=--unified=50 ait status src/stock.c
check "clearing diff context preserves detection of edits inside the provider" "cmp -s $T/changed-control $T/out"

# The code commit is ait-generated Git and uses its normalized context (v14 5.12).
mkdir -p .git/hooks
cat > .git/hooks/pre-commit <<'HOOK'
#!/bin/sh
printf '%s\n' "${GIT_DIFF_OPTS-unset}" > "$T/hook-diff-opts"
HOOK
chmod +x .git/hooks/pre-commit
run env GIT_DIFF_OPTS=--unified=50 ait fulfill "$O" --force -m native-code-commit
check_rc "fulfill still records the staged code under an inherited diff environment" 0
check "ait-generated code-commit hooks receive the normalized diff environment" \
	"grep -qx unset $T/hook-diff-opts"

done_testing
