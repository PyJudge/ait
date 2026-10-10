#!/bin/sh
# B1: ait-mini develops ait itself — a demand on the ait sources, an offer that undertakes it, a fulfill
. "$(dirname "$0")/lib.sh"
T=$(mktemp -d); export T HOME=$T/home GIT_CONFIG_NOSYSTEM=1 GIT_EDITOR=:; mkdir -p "$HOME"
git config --global user.name t; git config --global user.email t@t; git config --global init.defaultBranch master
N=0
mkdir -p "$T/r/libyaml" && cp "$AIT_DIR"/*.c "$AIT_DIR"/*.h "$AIT_DIR"/Makefile "$T/r/" && cd "$T/r"
cp "$AIT_DIR"/libyaml/*.[ch] "$AIT_DIR"/libyaml/License libyaml/
git init -q && tick && git add -A && git commit -qm "ait sources"
tick; expect_ok "init" ait init
A=$(ait session open --agent maintainer)
mkdir -p specs
printf 'body: |\n  scopes take line ranges PATH:S-E (spec 3.2)\ninputs: []\n' > specs/ranges.yaml
tick; expect_ok "demand: line ranges in pin()" ait future demand request.c record.c specs/ranges.yaml as d -m "line ranges"
D=$(new_id)
git worktree add -q -b builder "$T/b"
cd "$T/b"
B=$(ait session open --agent builder)
tick; expect_ok "the builder takes it on in its own worktree" ait future offer record.c specs/ranges.yaml as o undertake "$D" o -m "I build it"
O=$(new_id)
run ait todo
check "the builder's todo shows the offer" out_has "id $O"
echo '/* line ranges: see spec 3.2 */' >> record.c && git add record.c
tick; with_r pass ait fulfill "$O" -m "line ranges"
check "fulfilled with the demand" out_has "fulfilled $D via $O"
check "the fulfill commit carries both checks" sh -c "git log -1 --format=%B | grep -q 'Ait-Check: $D reported' && git log -1 --format=%B | grep -q 'Ait-Check: $O reported'"
check "the sources still build" make -s ait
# the maintainer takes the fulfilment and builds on it (10.1b): mini receives with git merge
cd "$T/r"
tick; run git merge -q --no-edit builder
check "the merge passes the hook" [ "$rc" = 0 ]
run ait todo
check "the maintainer has nothing left" [ ! -s "$T/out" ]
check "the demand is current in the maintainer's HEAD" sh -c "git show HEAD:.ait/$D | grep -qx 'state current'"
done_testing
