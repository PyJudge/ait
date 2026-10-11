#!/bin/sh
# T12 current 계약의 명세 파일 수정과 mode (2.4, 6.2, 6.5)
# Not yet supported: peek ('ait peek now M' in 7 is replaced by 'ait todo M').
. "$(dirname "$0")/lib.sh"

id_of() { sed -n "s/^new \([^ ]*\) $1\$/\1/p" "$T/out"; }
tasks_of() { grep '^task ' "$T/out"; }
rec_line() { git show "HEAD:.ait/$1" | grep -qx -- "$2"; }
# the hints hold these texts in this order (5.9a)
err_in_order() {
	rest=$(cat "$T/err")
	for s; do
		case $rest in *"$s"*) rest=${rest#*"$s"} ;; *) return 1 ;; esac
	done
}
prep() {
	setup; A=$(ait session open --agent agent-a); prompt_defs
	mkdir -p tests; printf '#!/bin/sh\nexit 0\n' > tests/c.sh; chmod 755 tests/c.sh
	printf 'body: c\nengine: exec\ncommand: [./tests/c.sh]\ninputs: [tests/c.sh]\n' > specs/c.yaml
	echo 'body: n' > specs/n.yaml
	tick; with_r pass ait offer src/stock.c specs/c.yaml as o -m m; O=$(id_of o)
	tick; with_r pass ait offer src/order.c specs/n.yaml as n -m m; NN=$(id_of n)
	[ -n "$O" -a -n "$NN" ] || say_fail "prep"
}

prep
# T12-1 (7.5, 7.6.1, 5.8, 5.9a): the request carries only a spec file, so code first, then the reopen
echo '# changed' >> tests/c.sh
expect_ok "1: contract add tests/c.sh" ait contract add tests/c.sh
check "1: staged tests/c.sh" out_has "staged tests/c.sh"
desc="1: contract commit"
tick; expect_unchanged expect_err "$desc" E_NEEDS_RESULTS ait contract commit -m m
check "1: the task is O:specs/c.yaml" [ "$(tasks_of)" = "task $O:specs/c.yaml exec" ]
tick; with_r fail ait contract commit -m m
check "1: -r fail is E_CHECK_FAILED" sh -c "[ $rc = 1 ] && head -1 '$T/err' | grep -q '^error: check failed:'"
check "1: code-first hint in order" err_in_order "unstage 'tests/c.sh' first" "stage <your code> and run 'ait commit -m <why>'" "ait contract add tests/c.sh" "ait contract commit -m <why>"
check "1: reopen hint in the request" err_has "to change '$O' without a check, reopen it: set 'state future' in '.ait/$O', then 'ait contract add .ait/$O' and 'ait contract commit -m <why>'"
check "1: no separate future amend O" sh -c "! grep -qF 'ait future amend $O' '$T/err'"

# T12-2 (6.2, 6.5)
tick; with_r pass ait contract commit -m m
check "2: -r pass ok" [ "$rc" = 0 ]
check "2: changed O" out_has "changed $O"
check "2: check O reported" out_has "check $O reported"
check "2: tests/c.sh input line re-pinned" rec_line "$O" "file tests/c.sh 100755 $(blob HEAD:tests/c.sh) input"
check "2: N is not checked" sh -c "! grep -q '^check $NN' '$T/out'"

# T12-3
echo 'body: n2' > specs/n.yaml
expect_ok "3: contract add specs/n.yaml" ait contract add specs/n.yaml
tick; with_r pass ait contract commit -m m
check "3: check N reported" out_has "check $NN reported"

# T12-4: specs/c.yaml becomes a prompt definition; its inputs stay
printf 'body: c\ninputs: [tests/c.sh]\n' > specs/c.yaml
expect_ok "4: contract add specs/c.yaml" ait contract add specs/c.yaml
desc="4: contract commit"
tick; expect_unchanged expect_err "$desc" E_NEEDS_RESULTS ait contract commit -m m
check "4: the task is O:specs/c.yaml prompt" [ "$(tasks_of)" = "task $O:specs/c.yaml prompt" ]
tick; with_r pass ait contract commit -m m
check "4: check O reported" out_has "check $O reported"
check "4: specs/c.yaml def line re-pinned" rec_line "$O" "file specs/c.yaml 100644 $(blob HEAD:specs/c.yaml) def"

# T12-5: a future D on the same files is re-pinned without a result
prep
tick; expect_ok "5: future demand D on the same file" ait future demand src/order.c src/stock.c specs/c.yaml as d -m m
D=$(id_of d)
echo '# changed' >> tests/c.sh
expect_ok "5: contract add tests/c.sh" ait contract add tests/c.sh
desc="5: contract commit"
tick; expect_unchanged expect_err "$desc" E_NEEDS_RESULTS ait contract commit -m m
check "5: one task, O:specs/c.yaml" [ "$(tasks_of)" = "task $O:specs/c.yaml exec" ]
tick; with_r pass ait contract commit -m m
check "5: ok" [ "$rc" = 0 ]
check "5: changed D" out_has "changed $D"
check "5: changed O" out_has "changed $O"
check "5: D re-pinned" rec_line "$D" "file tests/c.sh 100755 $(blob HEAD:tests/c.sh) input"

# T12-6: a current contract's spec file list can change too, with results
prep
printf 'file specs/n.yaml\n' >> ".ait/$O"
expect_ok "6: contract add .ait/O" ait contract add ".ait/$O"
desc="6: contract commit"
tick; expect_unchanged expect_err "$desc" E_NEEDS_RESULTS ait contract commit -m m
check "6: tasks O:specs/c.yaml and O:specs/n.yaml" [ "$(tasks_of)" = "task $O:specs/c.yaml exec
task $O:specs/n.yaml prompt" ]
tick; with_r pass ait contract commit -m m
check "6: changed O" out_has "changed $O"
check "6: check O reported" out_has "check $O reported"

# T12-7 (1.5b, 6.5): a contract the gate's change touches is checked too
prep
tick; with_r pass ait offer specs/n.yaml specs/p.yaml as m -m m; M=$(id_of m)
check "7: current offer M on specs/n.yaml" [ -n "$M" ]
echo 'body: n2' > specs/n.yaml
expect_ok "7: contract add specs/n.yaml" ait contract add specs/n.yaml
desc="7: contract commit"
tick; expect_unchanged expect_err "$desc" E_NEEDS_RESULTS ait contract commit -m m
check "7: tasks M:specs/p.yaml and N:specs/n.yaml" sh -c "grep -qx 'task $M:specs/p.yaml prompt' '$T/out' && grep -qx 'task $NN:specs/n.yaml prompt' '$T/out' && [ \$(grep -c '^task ' '$T/out') = 2 ]"
check "7: reopen hint for M in the request" err_has "set 'state future' in '.ait/$M'"
check "7: reopen hint for N in the request" err_has "set 'state future' in '.ait/$NN'"
W=$(out_value check-result)
fill "$W" pass "$M:specs/p.yaml=fail"
desc="7: M fails"
tick; expect_unchanged expect_err "$desc" E_CHECK_FAILED ait contract commit -m m -r "$W"
check "7: fix the spec file, then reopen M" err_in_order "fix 'specs/n.yaml', then 'ait contract add specs/n.yaml' and 'ait contract commit -m <why>'" "set 'state future' in '.ait/$M'"
check "7: no 'future amend M' form" sh -c "! grep -qF 'ait future amend $M' '$T/err'"
tick; with_r pass ait contract commit -m m
check "7: ok" [ "$rc" = 0 ]
set -- $(printf '%s\n' "$M" "$NN" | LC_ALL=C sort)
check "7: request, changed M and N, check M and N reported, in ID order" [ "$(sed 's/^request R[0-9a-f]\{12\}$/request R/' "$T/out")" = "request R
changed $1
changed $2
check $1 reported
check $2 reported" ]
check "7: M's provider re-pinned" rec_line "$M" "provider specs/n.yaml $(blob HEAD:specs/n.yaml)"
# not yet: 'ait peek now M' is not supported yet; M has no 'unverified' line in todo
expect_ok "7: todo M" ait todo "$M"
check "7: M is not unverified" sh -c "! grep -q '^unverified' '$T/out'"

# T12-7, --force
prep
tick; with_r pass ait offer specs/n.yaml specs/p.yaml as m -m m; M=$(id_of m)
echo 'body: n2' > specs/n.yaml
expect_ok "7: contract add specs/n.yaml (--force)" ait contract add specs/n.yaml
tick; expect_ok "7: contract commit --force" ait contract commit --force -m m
check "7: check M forced" out_has "check $M forced"
check "7: check N forced" out_has "check $NN forced"

# 7, fulfill M2 whose provider is a spec file
prep
tick; with_r pass ait offer specs/n.yaml specs/p.yaml as m -m m; M=$(id_of m)
tick; expect_ok "7: future offer M2 on specs/n.yaml" ait future offer specs/n.yaml specs/p.yaml as m2 -m m
M2=$(id_of m2)
tick; with_r fail ait fulfill "$M2" -m m
check "7: fulfill M2 -r fail is E_CHECK_FAILED" sh -c "[ $rc = 1 ] && head -1 '$T/err' | grep -q '^error: check failed:'"
check "7: hint amends N first" err_has "ait amend $NN - specs/n.yaml -m <why>"
check "7: no reopen hint (M2 is future)" sh -c "! grep -q 'reopen' '$T/err'"

# 7, empty amend of M with a fail result
prep
tick; with_r pass ait offer specs/n.yaml specs/p.yaml as m -m m; M=$(id_of m)
before=$(snapshot)
tick; with_r fail ait amend "$M" -m m
check "7: amend M -r fail is E_CHECK_FAILED" sh -c "[ $rc = 1 ] && head -1 '$T/err' | grep -q '^error: check failed:'"
check "7: unchanged" [ "$(snapshot)" = "$before" ]
check "7: hint adds N's clause" err_has "ait amend $M amend $NN - specs/n.yaml -m <why>"
check "7: reopen hint" err_has "ait future amend $M -m <why>"
check "7: no 'ait commit' hint" sh -c "! grep -q 'ait commit' '$T/err'"

# T12-8 (1.5b, 10.5): reopening while changing the condition needs no results
prep
echo '# changed' >> tests/c.sh
expect_ok "8: contract add tests/c.sh" ait contract add tests/c.sh
sed 's/^state current$/state future/' ".ait/$O" > "$T/rec" && cp "$T/rec" ".ait/$O"
expect_ok "8: contract add .ait/O" ait contract add ".ait/$O"
tick; expect_ok "8: contract commit" ait contract commit -m 조건
check "8: stdout is request and changed O" [ "$(sed 's/^request R[0-9a-f]\{12\}$/request R/' "$T/out")" = "request R
changed $O" ]
check "8: O is future" rec_line "$O" "state future"
check "8: tests/c.sh re-pinned" rec_line "$O" "file tests/c.sh 100755 $(blob HEAD:tests/c.sh) input"

# T12-9 (5.9a): a direct writing request that changed a record is fixed in the request
prep
sed 's/^provider src\/stock.c .*/provider src\/stock.c:1-5/' ".ait/$O" > "$T/rec" && cp "$T/rec" ".ait/$O"
expect_ok "9: contract add .ait/O" ait contract add ".ait/$O"
desc="9: contract commit"
tick; expect_unchanged expect_err "$desc" E_NEEDS_RESULTS ait contract commit -m m
W=$(out_value check-result)
fill "$W" fail
desc="9: contract commit -r fail"
tick; expect_unchanged expect_err "$desc" E_CHECK_FAILED ait contract commit -m m -r "$W"
check "9: no code-first hint" sh -c "! grep -qF \"unstage\" '$T/err' && ! grep -qF 'ait commit' '$T/err'"
check "9: no separate future amend O" sh -c "! grep -qF 'ait future amend $O' '$T/err'"
check "9: reopen hint in the request" err_has "to change '$O' without a check, reopen it: set 'state future' in '.ait/$O', then 'ait contract add .ait/$O' and 'ait contract commit -m <why>'"
sed 's/^state current$/state future/' ".ait/$O" > "$T/rec" && cp "$T/rec" ".ait/$O"
expect_ok "9: contract add .ait/O after the reopen" ait contract add ".ait/$O"
tick; expect_ok "9: contract commit" ait contract commit -m x
check "9: changed O" out_has "changed $O"
check "9: no check line" sh -c "! grep -q '^check ' '$T/out'"
check "9: O is future" rec_line "$O" "state future"
check "9: O's provider is src/stock.c:1-5 <blob>" rec_line "$O" "provider src/stock.c:1-5 $(blob HEAD:src/stock.c)"

done_testing
