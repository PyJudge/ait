#!/bin/sh
# T12 current 계약의 명세 파일 수정과 mode (2.4, 6.2, 6.5)
# ait-mini has no 'contract add'/'contract commit'; those steps assert MINI.
# mini: the same changes are also made through the helper ('amend', 'future amend'),
# the entrance spec/ait-mini.md names for them, and checked against 6.2 and 6.5.
. "$(dirname "$0")/lib.sh"

id_of() { sed -n "s/^new \([^ ]*\) $1\$/\1/p" "$T/out"; }
tasks_of() { grep '^task ' "$T/out"; }
rec_line() { git show "HEAD:.ait/$1" | grep -qx -- "$2"; }
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
# 1, 2: mini: 'contract add' and 'contract commit' are not in ait-mini
echo '# changed' >> tests/c.sh
desc="1: contract add tests/c.sh"
expect_unchanged expect_err "$desc" MINI ait contract add tests/c.sh
desc="1: contract commit"
expect_unchanged expect_err "$desc" MINI ait contract commit -m m
# the same change through the helper
desc="1 (helper): amend O after changing tests/c.sh"
expect_unchanged expect_err "$desc" E_NEEDS_RESULTS ait amend "$O" - specs/c.yaml -m m
check "1 (helper): the task is O:specs/c.yaml" [ "$(tasks_of)" = "task $O:specs/c.yaml exec" ]
tick; with_r fail ait amend "$O" - specs/c.yaml -m m
check "1 (helper): -r fail is E_CHECK_FAILED" sh -c "[ $rc = 1 ] && head -1 '$T/err' | grep -q '^error: check failed:'"
check "1 (helper): reopen hint" err_has "future amend $O"
tick; with_r pass ait amend "$O" - specs/c.yaml -m m
check "2 (helper): -r pass ok" [ "$rc" = 0 ]
check "2 (helper): changed O" out_has "changed $O"
check "2 (helper): check O reported" out_has "check $O reported"
check "2 (helper): tests/c.sh input line re-pinned" rec_line "$O" "file tests/c.sh 100755 $(blob HEAD:tests/c.sh) input"
check "2 (helper): N is not checked" sh -c "! grep -q '^check $NN' '$T/out'"

# 3
echo 'body: n2' > specs/n.yaml
desc="3: contract add specs/n.yaml"
expect_unchanged expect_err "$desc" MINI ait contract add specs/n.yaml
tick; with_r pass ait amend "$NN" - specs/n.yaml -m m
check "3 (helper): check N reported" out_has "check $NN reported"

# 4
printf 'body: c\ninputs: [tests/c.sh]\n' > specs/c.yaml
desc="4: contract add specs/c.yaml"
expect_unchanged expect_err "$desc" MINI ait contract add specs/c.yaml
desc="4 (helper): c.yaml becomes a prompt definition"
expect_unchanged expect_err "$desc" E_NEEDS_RESULTS ait amend "$O" - specs/c.yaml -m m
check "4 (helper): the task is O:specs/c.yaml prompt" [ "$(tasks_of)" = "task $O:specs/c.yaml prompt" ]
tick; with_r pass ait amend "$O" - specs/c.yaml -m m
check "4 (helper): check O reported" out_has "check $O reported"
check "4 (helper): specs/c.yaml def line re-pinned" rec_line "$O" "file specs/c.yaml 100644 $(blob HEAD:specs/c.yaml) def"

# 5
prep
tick; expect_ok "5: future demand D on the same file" ait future demand src/order.c src/stock.c specs/c.yaml as d -m m
D=$(id_of d)
echo '# changed' >> tests/c.sh
desc="5: contract add tests/c.sh"
expect_unchanged expect_err "$desc" MINI ait contract add tests/c.sh
desc="5 (helper): amend O"
expect_unchanged expect_err "$desc" E_NEEDS_RESULTS ait amend "$O" - specs/c.yaml -m m
check "5 (helper): one task, O:specs/c.yaml" [ "$(tasks_of)" = "task $O:specs/c.yaml exec" ]
tick; with_r pass ait amend "$O" - specs/c.yaml -m m
check "5 (helper): ok" [ "$rc" = 0 ]
check "5 (helper): changed D" out_has "changed $D"
check "5 (helper): changed O" out_has "changed $O"
check "5 (helper): D re-pinned" rec_line "$D" "file tests/c.sh 100755 $(blob HEAD:tests/c.sh) input"

# 6
prep
desc="6: contract add .ait/O"
printf 'file specs/n.yaml\n' >> ".ait/$O"
expect_err "6: contract add .ait/O" MINI ait contract add ".ait/$O"
git checkout -q -- .ait
# helper: amend O's definitions to c.yaml and n.yaml
desc="6 (helper): amend O - specs/c.yaml,specs/n.yaml"
expect_unchanged expect_err "$desc" E_NEEDS_RESULTS ait amend "$O" - specs/c.yaml,specs/n.yaml -m m
check "6 (helper): tasks O:specs/c.yaml and O:specs/n.yaml" [ "$(tasks_of)" = "task $O:specs/c.yaml exec
task $O:specs/n.yaml prompt" ]
tick; with_r pass ait amend "$O" - specs/c.yaml,specs/n.yaml -m m
check "6 (helper): changed O" out_has "changed $O"
check "6 (helper): check O reported" out_has "check $O reported"

# 7
prep
tick; with_r pass ait offer specs/n.yaml specs/p.yaml as m -m m; M=$(id_of m)
check "7: current offer M on specs/n.yaml" [ -n "$M" ]
echo 'body: n2' > specs/n.yaml
desc="7: contract add specs/n.yaml"
expect_unchanged expect_err "$desc" MINI ait contract add specs/n.yaml
# helper: changing specs/n.yaml for N also touches M
desc="7 (helper): amend N - specs/n.yaml"
expect_unchanged expect_err "$desc" E_NEEDS_RESULTS ait amend "$NN" - specs/n.yaml -m m
check "7 (helper): tasks M:specs/p.yaml and N:specs/n.yaml" sh -c "grep -qx 'task $M:specs/p.yaml prompt' '$T/out' && grep -qx 'task $NN:specs/n.yaml prompt' '$T/out' && [ \$(grep -c '^task ' '$T/out') = 2 ]"
check "7 (helper): reopen hint for M" err_has "future amend $M"
W=$(out_value check-result)
fill "$W" pass "$M:specs/p.yaml=fail"
desc="7 (helper): M fails"
tick; expect_unchanged expect_err "$desc" E_CHECK_FAILED ait amend "$NN" - specs/n.yaml -m m -r "$W"
tick; with_r pass ait amend "$NN" - specs/n.yaml -m m
check "7 (helper): ok" [ "$rc" = 0 ]
check "7 (helper): changed M and N" sh -c "grep -qx 'changed $M' '$T/out' && grep -qx 'changed $NN' '$T/out'"
check "7 (helper): check M and N reported" sh -c "grep -qx 'check $M reported' '$T/out' && grep -qx 'check $NN reported' '$T/out'"
check "7 (helper): M's provider re-pinned" rec_line "$M" "provider specs/n.yaml $(blob HEAD:specs/n.yaml)"
check "7 (helper): ID order of changed lines" [ "$(grep '^changed ' "$T/out")" = "$(grep '^changed ' "$T/out" | LC_ALL=C sort)" ]
# mini: 'ait peek now M' is not in ait-mini; M has no 'unverified' line in todo
expect_ok "7: todo M" ait todo "$M"
check "7: M is not unverified" sh -c "! grep -q '^unverified' '$T/out'"

# 7, --force
prep
tick; with_r pass ait offer specs/n.yaml specs/p.yaml as m -m m; M=$(id_of m)
echo 'body: n2' > specs/n.yaml
tick; expect_ok "7 (helper): --force" ait amend "$NN" - specs/n.yaml --force -m m
check "7 (helper): check M forced" out_has "check $M forced"
check "7 (helper): check N forced" out_has "check $NN forced"

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

# 8
prep
echo '# changed' >> tests/c.sh
sed 's/^state current$/state future/' ".ait/$O" > "$T/rec" && cp "$T/rec" ".ait/$O"
expect_err "8: contract add .ait/O" MINI ait contract add ".ait/$O"
git checkout -q -- .ait
# helper: reopen while changing the condition needs no results
tick; expect_ok "8 (helper): future amend O - specs/c.yaml" ait future amend "$O" - specs/c.yaml -m m
check "8 (helper): stdout is request and changed O" [ "$(sed 's/^request R[0-9a-f]\{12\}$/request R/' "$T/out")" = "request R
changed $O" ]
check "8 (helper): O is future" rec_line "$O" "state future"
check "8 (helper): tests/c.sh re-pinned" rec_line "$O" "file tests/c.sh 100755 $(blob HEAD:tests/c.sh) input"

# 9: mini: it edits '.ait/O' and commits it through 'contract add'/'contract commit'
prep
sed 's/^provider src\/stock.c .*/provider src\/stock.c/' ".ait/$O" > "$T/rec" && cp "$T/rec" ".ait/$O"
expect_err "9: contract add .ait/O" MINI ait contract add ".ait/$O"
git checkout -q -- .ait

done_testing
