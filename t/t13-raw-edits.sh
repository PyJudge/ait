#!/bin/sh
# T13 원시 편집은 막지 않되 드러나고, 공유에서 막힌다
# (규칙 1·9, 1.5a, 2.4, 5.10, 7.1.2, 7.1.6, 7.6, 7.9.5)
# Covers the pre-commit guard, accidents, the unverified display, the contract commit repairs
# and the pre-push hook of 9. Not yet supported: peek, 'contract push' and 'future drop'.
. "$(dirname "$0")/lib.sh"

id_of() { sed -n "s/^new \([^ ]*\) $1\$/\1/p" "$T/out"; }
# err E_ACCIDENT (5.8, 5.10): exit 1 and the first line "error: 'ID' at HEAD was changed outside ait's rules"
expect_accident() {
	desc=$1 id=$2; shift 2; run "$@"
	if [ "$rc" = 1 ] && head -1 "$T/err" | grep -qxF -- "error: '$id' at HEAD was changed outside ait's rules"; then
		say_ok "$desc"
	else
		say_fail "$desc (want E_ACCIDENT, exit $rc)"
	fi
}
# HEAD's accident version of ID as a warning (5.10, 7.1.8), and the restore hint from GOOD
accident_warning() { err_has "warning: '$1' at HEAD was changed outside ait's rules"; }
restore_hint() { err_has "hint: restore '.ait/$1' as in '$2', then 'ait contract commit -m <why>'"; }
# the hints hold these texts in this order (5.9a)
err_in_order() {
	rest=$(cat "$T/err")
	for s; do
		case $rest in *"$s"*) rest=${rest#*"$s"} ;; *) return 1 ;; esac
	done
}
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
# T13-2 (7.1.8, 5.8): post-commit warns, with the HEAD form of E_INCOMPLETE's hint that step 5 follows
check "2: post-commit warns of the spec file" err_has "warning: spec files do not match the contract:"
check "2: post-commit hints the contract commit" err_has "ait contract commit -m <why>"
expect_err "2: peek O" UNSUPPORTED ait peek "$O"
# not yet: instead of peek's 'event - ? edited tests/pass.sh', the record still pins the old blob
check "2: rec(O) still pins the old tests/pass.sh" sh -c "git show HEAD:.ait/$O | grep -qx 'file tests/pass.sh 100755 $(blob $H:tests/pass.sh) input'"
check "2: and the commit has no trailers" [ -z "$(git log -1 --format='%(trailers:key=Ait-Request,valueonly)')" ]
# 3
expect_err "3: contract push origin" UNSUPPORTED ait contract push origin
# not yet: 'git push' and the pre-push hook are not tested here
# 4
echo s51 >> src/stock.c; git add src/stock.c
desc="4: fulfill O with the raw-edited spec file"
expect_unchanged expect_err "$desc" E_INCOMPLETE ait fulfill "$O" -m m
check "4: report has invalid incomplete" out_matches '^invalid incomplete '
check "4: report has ready no" out_has "ready no"
git reset -q --hard
# T13-5 (6.2, 7.6.1): the contract commit with nothing staged re-pins the raw-edited spec file
tick; expect_ok "5: contract commit" ait contract commit -m "판 다시 고정"
check "5: changed O" out_has "changed $O"
# not yet: instead of peek's 'event <R> agent-a respecified tests/pass.sh', the record pins the new blob
check "5: tests/pass.sh re-pinned" sh -c "git show HEAD:.ait/$O | grep -qx 'file tests/pass.sh 100755 $(blob HEAD:tests/pass.sh) input'"
# not yet: the 'ait contract push origin' that follows is not supported yet

# 6
prep
sed 's/^state future$/state current/' ".ait/$O" > "$T/rec" && cp "$T/rec" ".ait/$O"
tick; run git commit -q --no-verify -am forge
check "6: the forged commit is made" [ "$rc" = 0 ]
CF1=$(git rev-parse HEAD^)
# T13-6: a current version without a check line is an accident version (1.5a, 2.3.3, 7.1.8)
check "6: post-commit warns of the accident" accident_warning "$O"
check "6: post-commit gives the restore hint" restore_hint "$O" "$CF1"
# not yet: peek's history and 'peek now' are not supported yet; todo shows HEAD's accident version as peek does (5.10)
expect_err "6: peek O" UNSUPPORTED ait peek "$O"
expect_ok "6: todo O" ait todo "$O"
check "6: todo O shows state current" out_has "state current"
check "6: todo O has no unverified line" sh -c "! grep -q '^unverified' '$T/out'"
check "6: todo O warns of the accident" accident_warning "$O"
check "6: todo O gives the restore hint" restore_hint "$O" "$CF1"
desc="6: future offer src/order.c p"
expect_unchanged expect_accident "$desc" "$O" ait future offer src/order.c specs/p.yaml -m m
check "6: future offer gives the restore hint" restore_hint "$O" "$CF1"
# T13-6 (5.10): the restore the hint names, then the contract commit
git --literal-pathspecs restore --source="$CF1" --staged --worktree -- ".ait/$O"
tick; expect_ok "6: contract commit after the restore" ait contract commit -m m
check "6: changed O" out_has "changed $O"
check "6: O is future" sh -c "git show HEAD:.ait/$O | grep -qx 'state future'"
check "6: O's prev is blob(Cf^:.ait/O)" sh -c "git show HEAD:.ait/$O | grep -qx 'prev $(blob "$CF1:.ait/$O")'"

# 7
prep
C=$(git rev-parse HEAD)
tick; run git revert --no-edit "$C"
check "7: revert deletes .ait/O" sh -c "[ $rc = 0 ] && ! git cat-file -e HEAD:.ait/$O 2>/dev/null"
expect_err "7: peek O" UNSUPPORTED ait peek "$O"

# 8
prep
# T13-8: K's provider is the line range src/order.c:1-5
tick; with_r pass ait offer src/order.c:1-5 specs/p.yaml as k -m m; K=$(id_of k); RK=$(git rev-parse HEAD)
check "8: K" [ -n "$K" ]
sed '3s/.*/o3x/' src/order.c > "$T/o" && cat "$T/o" > src/order.c
sed "s/^provider src\/order.c:1-5 .*/provider src\/order.c:1-5 $(git hash-object src/order.c)/" ".ait/$K" > "$T/rec" && cp "$T/rec" ".ait/$K"
tick; run git commit -q --no-verify -am repin; CR=$(git rev-parse HEAD)
check "8: the re-pinned commit is made" [ "$rc" = 0 ]
# T13-8: K's check line keeps Rk's pins, which are not pins(Cr, K): an accident version, not unverified (1.5a, 2.3.3)
check "8: K's check line is the one of Rk" [ "$(check_line "$CR" "$K")" = "reported $(pins "$RK" "$K")" ]
check "8: it does not match pins(Cr, K)" [ "$(check_line "$CR" "$K")" != "reported $(pins "$CR" "$K")" ]
check "8: post-commit warns of the accident" accident_warning "$K"
check "8: post-commit gives the restore hint" restore_hint "$K" "$RK"
# not yet: 'peek now' is not supported yet; todo shows HEAD's accident version as peek does (5.10)
expect_ok "8: todo K" ait todo "$K"
check "8: provider src/order.c:1-5 without ?" out_has "provider src/order.c:1-5"
check "8: no unverified line" sh -c "! grep -q '^unverified' '$T/out'"
check "8: todo K warns of the accident" accident_warning "$K"
check "8: todo K gives the restore hint" restore_hint "$K" "$RK"
expect_err "8: peek K" UNSUPPORTED ait peek "$K"
# T13-8: 'ait amend K -r pass' stops at its first run, as validate comes before the results (5.13)
desc="8: amend K"
tick; expect_unchanged expect_accident "$desc" "$K" ait amend "$K" -m m
check "8: amend K gives the restore hint" restore_hint "$K" "$RK"
# T13-8 (5.10, 1.5a): the restore the hint names, then the contract commit
git --literal-pathspecs restore --source="$RK" --staged --worktree -- ".ait/$K"
tick; expect_ok "8: contract commit after the restore" ait contract commit -m 복구
check "8: changed K" out_has "changed $K"
check "8: K's check line now matches" [ "$(check_line HEAD "$K")" = "reported $(pins HEAD "$K")" ]
# not yet: 'peek now' is not supported yet; todo shows the unverified mark as peek does
expect_ok "8: todo K after the restore" ait todo "$K"
check "8: unverified yes after the restore" out_has "unverified yes"
tick; with_r pass ait amend "$K" -m m
check "8: amend K -r pass ok" [ "$rc" = 0 ]
check "8: check K reported" out_has "check $K reported"
check "8: K's check line is reported pins(HEAD, K)" [ "$(check_line HEAD "$K")" = "reported $(pins HEAD "$K")" ]
expect_ok "8: todo K after the check" ait todo "$K"
check "8: no unverified line after the check" sh -c "! grep -q '^unverified' '$T/out'"
expect_err "8: peek K after the check" UNSUPPORTED ait peek "$K"

# 9
prep
printf '%s' "$(cat ".ait/$O")" > "$T/rec" && cp "$T/rec" ".ait/$O"
tick; git commit -q --no-verify -am bad; CB=$(git rev-parse HEAD); G=$(git rev-parse HEAD^)
expect_err "9: peek" UNSUPPORTED ait peek
# T13-9 (5.10): todo, like peek, skips the malformed record with a warning and the restore hint from G
expect_ok "9: todo" ait todo
check "9: todo warns about the malformed record" err_has "warning: skipping malformed '.ait/$O' at '"
check "9: todo gives the restore hint" restore_hint "$O" "$G"
check "9: no O block" sh -c "! grep -q '^id $O' '$T/out'"
desc="9: future offer src/order.c p"
expect_unchanged expect_err "$desc" E_FORMAT ait future offer src/order.c specs/p.yaml -m m
check "9: future offer gives the restore hint" restore_hint "$O" "$G"
echo s51 >> src/stock.c; git add src/stock.c
desc="9: fulfill O"
expect_unchanged expect_err "$desc" E_FORMAT ait fulfill "$O" -m m
# T13-9 (5.9a): the repair, then the fulfill again; staged code that stops contract commit is its E_MIXED's to say
check "9: fulfill O hints restore, contract commit, rerun" err_in_order "restore '.ait/$O' as in '$G'" "ait contract commit -m <why>" "then rerun this command"
git reset -q --hard
check "9: HEAD is still Cb" [ "$(git rev-parse HEAD)" = "$CB" ]
# the last run gave no hint
not_hint() { ! grep -q '^hint:' "$T/err"; }
# T13-9 (7.1.6): pre-push checks what is pushed; only HEAD's branch is fixed here
git switch -q -c ok "$G"
run git push origin master:refs/heads/x
check "9: push of master from branch ok is refused" [ "$rc" != 0 ]
check "9: master is not fixed here: no hint" not_hint
check "9: no restore hint for master" sh -c "! grep -qF \"restore '.ait/$O'\" '$T/err'"
git switch -q master
tick; expect_err "9: contract commit on master" E_FORMAT ait contract commit -m x
check "9: contract commit gives the restore hint" restore_hint "$O" "$G"
git switch -q master; git branch -qD ok
git tag bad
run git push origin bad
check "9: push of tag bad is refused" [ "$rc" != 0 ]
check "9: nor the tag" not_hint
git tag -d bad >/dev/null
git switch -q --detach master
run git push origin HEAD:refs/heads/y
check "9: push of a detached HEAD is refused" [ "$rc" != 0 ]
check "9: nor a detached HEAD" not_hint
git switch -q master
check "9: origin has neither x nor y" sh -c "! git ls-remote --exit-code origin refs/heads/x refs/heads/y >/dev/null"
# T13-9 (5.10): the restore the hint names; prev is the last good version
git --literal-pathspecs restore --source="$G" --staged --worktree -- ".ait/$O"
tick; expect_ok "9: contract commit after the restore" ait contract commit -m 복구
check "9: changed O" out_has "changed $O"
check "9: O's prev is blob(G:.ait/O)" sh -c "git show HEAD:.ait/$O | grep -qx 'prev $(blob "$G:.ait/$O")'"

# 10: not yet: it needs 'future drop'

done_testing
