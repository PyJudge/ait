#!/bin/sh
# T13 원시 편집은 막지 않되 드러나고, 공유에서 막힌다
# (규칙 1·9, 1.5a, 2.4, 5.10, 7.1.2, 7.1.6, 7.6, 7.9.5)
# ait-mini: covers the pre-commit guard and the unverified display. There is no peek,
# 'contract push', 'contract add'/'commit', or drop; the push hook is not tested here.
. "$(dirname "$0")/lib.sh"

id_of() { sed -n "s/^new \([^ ]*\) $1\$/\1/p" "$T/out"; }
prep() {
	setup; A=$(ait session open --agent agent-a); prompt_defs; exec_def pass
	tick; ait future offer src/stock.c specs/pass.yaml as o -m m >"$T/out" 2>"$T/err"; O=$(id_of o)
	git push -q origin master
	[ -n "$O" ] || say_fail "prep"
}

prep
# 1
echo '# x' >> tests/pass.sh
H=$(git rev-parse HEAD)
tick; run git commit -qam raw
check "1: plain commit of a spec file fails" [ "$rc" != 0 ]
check "1: E_GUARD line" err_has "error: contract files cannot be changed by a plain commit:"
check "1: a hint line" err_has "hint: "
check "1: HEAD unchanged" [ "$(git rev-parse HEAD)" = "$H" ]
# 2
tick; run git commit -q --no-verify -am raw
check "2: --no-verify commits" [ "$rc" = 0 -a "$(git rev-parse HEAD^)" = "$H" ]
expect_err "2: peek O" MINI ait peek "$O"
# mini: instead of peek's 'event - ? edited tests/pass.sh', the record still pins the old blob
check "2: rec(O) still pins the old tests/pass.sh" sh -c "git show HEAD:.ait/$O | grep -qx 'file tests/pass.sh 100755 $(blob $H:tests/pass.sh) input'"
check "2: and the commit has no trailers" [ -z "$(git log -1 --format='%(trailers:key=Ait-Request,valueonly)')" ]
# 3
expect_err "3: contract push origin" MINI ait contract push origin
# mini: 'git push' and the pre-push hook are not tested here
# 4
echo s51 >> src/stock.c; git add src/stock.c
desc="4: fulfill O with the raw-edited spec file"
expect_unchanged expect_err "$desc" E_INCOMPLETE ait fulfill "$O" -m m
check "4: report has invalid incomplete" out_matches '^invalid incomplete '
check "4: report has ready no" out_has "ready no"
git reset -q --hard
# 5
expect_err "5: contract commit" MINI ait contract commit -m "판 다시 고정"
# mini: the helper re-pins the raw-edited spec file instead (6.2)
tick; expect_ok "5 (helper): future amend O - specs/pass.yaml" ait future amend "$O" - specs/pass.yaml -m m
check "5 (helper): changed O" out_has "changed $O"
check "5 (helper): tests/pass.sh re-pinned" sh -c "git show HEAD:.ait/$O | grep -qx 'file tests/pass.sh 100755 $(blob HEAD:tests/pass.sh) input'"

# 6
prep
sed 's/^state future$/state current/' ".ait/$O" > "$T/rec" && cp "$T/rec" ".ait/$O"
tick; run git commit -q --no-verify -am forge
check "6: the forged commit is made" [ "$rc" = 0 ]
FORGE=$(git rev-parse HEAD)
# mini: peek's history and 'peek now' are not in ait-mini; todo shows the unverified mark (7.8.2)
expect_err "6: peek O" MINI ait peek "$O"
expect_ok "6: todo O" ait todo "$O"
check "6: todo O shows state current" out_has "state current"
check "6: todo O shows unverified yes" out_has "unverified yes"

# 7
prep
C=$(git rev-parse HEAD)
tick; run git revert --no-edit "$C"
check "7: revert deletes .ait/O" sh -c "[ $rc = 0 ] && ! git cat-file -e HEAD:.ait/$O 2>/dev/null"
expect_err "7: peek O" MINI ait peek "$O"

# 8
prep
desc="8: offer with a line range"
expect_unchanged expect_err "$desc" MINI ait offer src/order.c:1-5 specs/p.yaml as k -m m
# mini: the file scope src/order.c instead of src/order.c:1-5
tick; with_r pass ait offer src/order.c specs/p.yaml as k -m m; K=$(id_of k)
check "8: K" [ -n "$K" ]
sed '3s/.*/o3x/' src/order.c > "$T/o" && cat "$T/o" > src/order.c
sed "s/^provider src\/order.c .*/provider src\/order.c $(git hash-object src/order.c)/" ".ait/$K" > "$T/rec" && cp "$T/rec" ".ait/$K"
tick; git commit -q --no-verify -am repin; CR=$(git rev-parse HEAD)
expect_ok "8: todo K" ait todo "$K"
check "8: provider src/order.c without ?" out_has "provider src/order.c"
check "8: unverified yes" out_has "unverified yes"
tick; with_r pass ait amend "$K" -m m
check "8: amend K -r pass ok" [ "$rc" = 0 ]
check "8: check K reported" out_has "check $K reported"
check "8: Ait-Check pins are pins(Cr, K)" [ "$(git log -1 --format='%(trailers:key=Ait-Check,valueonly)')" = "$K reported $(pins "$CR" "$K")" ]
expect_ok "8: todo K after the check" ait todo "$K"
check "8: no unverified line" sh -c "! grep -q '^unverified' '$T/out'"

# 9
prep
printf '%s' "$(cat ".ait/$O")" > "$T/rec" && cp "$T/rec" ".ait/$O"
tick; git commit -q --no-verify -am bad; CB=$(git rev-parse HEAD)
expect_err "9: peek" MINI ait peek
# mini: todo also skips a malformed record with a warning (5.10)
expect_ok "9: todo" ait todo
check "9: todo warns about the malformed record" err_has "warning: skipping malformed '.ait/$O' at '"
check "9: no O block" sh -c "! grep -q '^id $O' '$T/out'"
desc="9: future offer src/order.c p"
expect_unchanged expect_err "$desc" E_FORMAT ait future offer src/order.c specs/p.yaml -m m
echo s51 >> src/stock.c; git add src/stock.c
desc="9: fulfill O"
expect_unchanged expect_err "$desc" E_FORMAT ait fulfill "$O" -m m
git reset -q --hard
# mini: the pre-push checks and the 'contract commit' repair are not in ait-mini

# 10: mini: it needs 'future drop'

done_testing
