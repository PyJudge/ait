#!/bin/sh
# Sequences tools/explore.py found and shrank, kept as regressions; each failed before its fix
. "$(dirname "$0")/lib.sh"
# the explorer's repository (tools/explore.py SETUP); a step's output is in out and err
explorer_repo() {
	unset $(set | sed -n 's/^\([DOX][0-9]*\)=.*/\1/p')
	T=$(mktemp -d); export T HOME=$T GIT_CONFIG_NOSYSTEM=1 GIT_EDITOR=: LC_ALL=C
	unset AIT_SESSION
	git config --global user.name t; git config --global user.email t@t; git config --global init.defaultBranch master
	N=0
	cd "$T" && git init -q r && cd r
	mkdir src lib specs tests
	for f in src/a.c src/b.c lib/c.c; do seq 1 20 | sed "s|^|$f |" > $f; done
	echo 'body: p holds' > specs/p.yaml; echo 'body: q holds' > specs/q.yaml
	printf '#!/bin/sh\nexit 0\n' > tests/x.sh; chmod +x tests/x.sh
	printf 'body: x passes\nengine: exec\ncommand: [./tests/x.sh]\ninputs: [tests/x.sh]\n' > specs/x.yaml
	tick; git add -A && git commit -qm base
	ait init >/dev/null 2>&1
	SA=$(ait session open --agent a)
	SB=$(ait session open --agent b 2>/dev/null)
}
bind() { for k in D O X; do i=$(set | grep -c "^$k[0-9]*="); for id in $(sed -n "s/^new \($k[0-9a-f]*\).*/\1/p" out); do eval "$k$i=$id"; i=$((i+1)); done; done; }

# 5.10 (choice 246): a broken bundle on a record HEAD's accident entangles is mended by the same restore, not by a contract commit alone
explorer_repo
tick; ait future offer src/a.c specs/x.yaml -m o > out 2> err; bind
tick; sed -i.bak 's/^state .*/state cancelled/' .ait/$O0 && rm -f .ait/$O0.bak && ait contract add .ait/$O0 >/dev/null 2>&1
tick; ait contract commit -m w > out 2> err; bind
tick; sed -i.bak '7s/$/ x/' src/a.c && rm -f src/a.c.bak && git add -A
tick; ait commit --force -m c > out 2> err; bind
tick; rm -f tests/x.sh && ln -s ../src/a.c tests/x.sh && git add -A tests
tick; git commit -q --no-verify -m nv > out 2> err; bind
tick; sed -i.bak 's|^creator .*|creator z S000000000000|' .ait/$O0 && rm -f .ait/$O0.bak && ait contract add .ait/$O0 >/dev/null 2>&1
tick; git commit -qm w --no-verify > out 2> err; bind
tick; sed -i.bak '6s/$/ x/' lib/c.c && rm -f src/a.c.bak && git add -A
tick; ait commit -m c > out 2> err; bind
tick; echo 'body: changed 12' > specs/x.yaml && ait contract add specs/x.yaml >/dev/null 2>&1
tick; ait contract commit -m s > out 2> err; bind
tick; sed -i.bak '3s/$/ x/' lib/c.c && rm -f src/a.c.bak && git add -A
tick; git commit -qm c > out 2> err; bind
tick; sed -i.bak 's/^state .*/state current/' .ait/$O0 && rm -f .ait/$O0.bak && git add .ait/$O0
tick; git commit -q --no-verify -m nv > out 2> err; bind
check "seed 544: the broken bundle's hint is the shared restore" sh -c "grep -q \"^warning: spec files do not match the contract: '$O0'\" err && ! grep -qx \"hint: run 'ait contract commit -m <why>'\" err"
# 5.9a hint 1, 7.1.3-7: a broken bundle of the request on a record HEAD's structure error points to is HEAD's error
explorer_repo
tick; ait future offer src/a.c specs/p.yaml -m o > out 2> err; bind
awk '/^creator /{print "check forced 0000000000000000000000000000000000000000"} {print}' .ait/$O0 > "$T/o" && cat "$T/o" > .ait/$O0
echo 'body: changed' > specs/p.yaml; git add -A; tick; git commit -q --no-verify -m hand 2>/dev/null
echo x >> src/a.c; git add src/a.c
tick; ait fulfill $O0 --force -m f > out 2> err
check "HEAD's check on a promise, the request's broken bundle: the hint is 5.10's repair" sh -c "grep -q \"^hint: restore '.ait/$O0' as in '[0-9a-f]*', then 'ait contract commit -m <why>', then rerun this command\" err && ! grep -q 'ait future amend' err"

# 5.10 (seed 892): no repair ends in a contract commit when the gate cannot pin a bundle again, entangled or not
explorer_repo
tick; ait demand src/a.c src/a.c specs/x.yaml --force -m d > out 2> err; bind
tick; printf 'body: b\ninputs: [tests/none.sh]\n' > specs/x.yaml && git add specs/x.yaml
tick; git commit -q --no-verify -m nv > out 2> err
tick; sed -i.bak 's/^state .*/state future/' .ait/$D0 && rm -f .ait/$D0.bak && ait contract add .ait/$D0 >/dev/null 2>&1
tick; ait contract commit -m w > out 2> err
tick; git add -A
tick; AIT_SESSION=$SB ait fulfill $D0 --force -m f > out 2> err
tick; printf 'body: b\nfoo: 1\n' > specs/q.yaml && git add specs/q.yaml
tick; git commit -q --no-verify -m nv > out 2> err
tick; git add -A
tick; ait fulfill $D0 -m f > out 2> err
check "seed 892: fulfill stops on HEAD's first error, with no hint, as no command mends it (user decision 55)" sh -c "grep -q \"^fatal: bad format: '.ait/$D0'\" err && ! grep -q '^hint:' err"

# 5.10 (seed 818): the same for a broken bundle entangled with another error of HEAD
explorer_repo
tick; ait demand lib/c.c src/b.c specs/q.yaml --force -m d > out 2> err; bind
tick; printf 'body: b\nfoo: 1\n' > specs/x.yaml && git add specs/x.yaml
tick; git commit -q --no-verify -m nv > out 2> err
tick; ait offer lib/c.c specs/p.yaml --force -m o > out 2> err; bind
tick; AIT_SESSION=$SB ait future amend $O0 -m a > out 2> err
tick; ait demand lib/c.c src/a.c specs/p.yaml --force -m d > out 2> err; bind
tick; echo 'body: changed 6' > specs/q.yaml && ait contract add specs/q.yaml >/dev/null 2>&1
tick; AIT_SESSION=$SB ait contract commit -m s > out 2> err
tick; ait undertake $D1 $O0 -m u > out 2> err
tick; ait future cancel $O0 -m c > out 2> err
tick; rm -f tests/x.sh && ln -s ../src/a.c tests/x.sh && git add -A tests
tick; git commit -q --no-verify -m nv > out 2> err
tick; echo 'body: changed 12' > specs/x.yaml && ait contract add specs/x.yaml >/dev/null 2>&1
tick; git commit -qm s > out 2> err
tick; git rm -q specs/x.yaml
tick; git commit -qm gone > out 2> err
tick; git revert --no-edit HEAD > out 2> err
tick; sed -i.bak 's/^state .*/state withdrawn/' .ait/$O0 && rm -f .ait/$O0.bak && git add .ait/$O0
tick; git commit -q --no-verify -m nv > out 2> err
tick; printf 'body: b\ninputs: [tests/none.sh]\n' > specs/p.yaml && git add specs/p.yaml
tick; git commit -q --no-verify -m nv > out 2> err
check "seed 818: the broken bundle no command pins again gets no contract commit to run" sh -c "grep -q \"^warning: spec files do not match the contract: '$D1'\" err && ! grep -q 'ait contract commit' err"
done_testing
