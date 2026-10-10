#!/bin/sh
# 5.5-4, 7.3.3, 5.4 and 7.1.6: what git does is what ait takes; each case reproduced against git's own behaviour
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent a)
prompt_defs; exec_def pass
git add -A && tick && git commit -q --no-verify -m defs
H=$(git rev-parse HEAD)

# 5.5-4: a path the commit took, staged again meanwhile: git's two-way merge keeps the index where it was not judged
racing_git
echo 'echo raced > "$T/r/src/order.c" && git -C "$T/r" add src/order.c' > "$T/before-update-ref"
echo y >> src/order.c && git add src/order.c
tick; run env PATH="$T/bin:$PATH" ait commit -m judged
check "the judged commit lands" sh -c "[ $rc = 0 ] && ! git show HEAD:src/order.c | grep -q raced"
check "with no warning" [ ! -s "$T/err" ]
check "and the staging made meanwhile stays" sh -c "git show :src/order.c | grep -qx raced"
git reset -q --hard "$H"

# 5.5-4: a record the request changes, staged otherwise meanwhile: read-tree refuses whole, the staging stays
tick; ait future offer src/stock.c specs/p.yaml -m o >"$T/out"; O=$(new_id); H2=$(git rev-parse HEAD)
echo 'printf "raced\n" >> "$T/r/.ait/'$O'" && git -C "$T/r" add .ait/'$O > "$T/before-update-ref"
tick; run env PATH="$T/bin:$PATH" ait future cancel $O -m c
check "the request lands" sh -c "[ $rc = 0 ] && [ \$(git rev-parse HEAD^) = $H2 ]"
check "with the warning of 5.5-5" err_has "but the index and work tree were not updated"
check "and the staging made meanwhile stays" sh -c "git show :.ait/$O | grep -qx raced"
git reset -q --hard "$H"

# 7.3.3: a spec file is staged as git stages it; a symlink is not a regular file
ln -s p.yaml specs/l.yaml
tick; expect_err "a symlinked spec file" E_INCOMPLETE ait offer src/stock.c specs/l.yaml --force -m m
check "says so" err_has "is not a regular file"
rm specs/l.yaml

# 7.3.3: with core.fileMode false the mode git has is kept
git update-index --chmod=+x specs/p.yaml && tick && git commit -q --no-verify -m x
git config core.fileMode false && chmod -x specs/p.yaml
tick; expect_ok "a request with core.fileMode false" ait offer src/stock.c specs/p.yaml --force -m m
check "keeps the executable mode" sh -c "git ls-tree HEAD specs/p.yaml | grep -q '^100755'"
git config --unset core.fileMode; git reset -q --hard "$H"

# 5.5-4: core.sparseCheckout without a patterns file is no sparse checkout; nothing is emptied
git config core.sparseCheckout true
tick; expect_ok "a request" ait future offer src/stock.c specs/p.yaml -m o
check "the work tree is whole" sh -c "[ -f src/order.c ] && [ -f specs/p.yaml ]"
git config --unset core.sparseCheckout; git reset -q --hard "$H"

# 5.4: trailers as git reads them; a commit without trailers and a folded value in one log
tick; ait offer src/stock.c specs/pass.yaml --force -m o >"$T/out"; O=$(new_id)
tick; git commit -q --allow-empty -m plain
tick; git commit -q --allow-empty -m "$(printf 'note\n\nAit-Check: %s\n  forced\nSigned-off-by: x' "$O")"
expect_ok "todo reads the log" ait todo $O
git reset -q --hard "$H"

# 7.1.6: a local name holding a line feed is read from the right, as git writes it
mkdir -p .ait && echo garbage > .ait/Daaaaaaaaaaaa && git add .ait && tick && git commit -q --no-verify -m "$(printf 'bad\nline')"
B=$(git rev-parse HEAD); Z=0000000000000000000000000000000000000000
run sh -c "printf 'HEAD^{/bad\nline} %s refs/heads/nl %s\n' $B $Z | ait hook pre-push"
check "the commit is validated" sh -c "[ $rc != 0 ]"
git reset -q --hard "$H"
# 8.2: the check request is written by libyaml; values read back as they were
printf 'body: "  lead\\n"\nengine: exec\ncommand:\n  - sh\n  - -c\n  - |\n    true\n    true\n' > specs/nl.yaml
git add specs/nl.yaml && tick && git commit -q --no-verify -m nl
tick; run ait offer src/stock.c specs/nl.yaml -m o
Q=$(git rev-parse --git-path ait/check-request.yaml)
check "a line break in an argument is kept" grep -qF 'command: ["sh", "-c", "true\ntrue\n"]' "$Q"
check "a body starting with blanks keeps them" sh -c "sed -n '/body:/,/command:/p' '$Q' | grep -qx '      lead'"
git reset -q --hard "$H"

# 8.4: a new contract's +SEQ in the results is spliced at its place, counted in bytes past UTF-8 text
tick; run ait offer src/stock.c specs/pass.yaml -m o
W=$(out_value check-result); fill "$W" pass
{ echo '# 한글 주석'; cat "$W"; } > "$W.k" && mv "$W.k" "$W"
tick; run ait offer src/stock.c specs/pass.yaml -m o -r "$W"; O=$(new_id)
check "the message names the new ID" sh -c "git log -1 --format=%B | grep -q 'task: $O:specs/pass.yaml'"
git reset -q --hard "$H"

# 4: the state files hold a path a line; a worktree path with a line feed is not supported
git worktree add -q "$T/a
b" -b nl
(cd "$T/a
b" && ait todo >/dev/null 2>"$T/err"; echo $? > "$T/rc")
check "a path with a line feed is refused plainly" sh -c "[ \$(cat '$T/rc') = 128 ]"
check "as not supported" err_has "not supported in ait-mini: 'a repository path with a line feed'"
done_testing
