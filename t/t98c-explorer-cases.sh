#!/bin/sh
# Sequences tools/explore.py found and shrank, kept as regressions; each failed before its fix
. "$(dirname "$0")/lib.sh"
# the explorer's repository (tools/explore.py SETUP); a step's output is in out and err
explorer_repo() {
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
done_testing
