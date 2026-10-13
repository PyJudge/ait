#!/bin/sh
# pull criteria 3.9 구조 검사 (N5, XT1, XT2, XT3)
. "$(dirname "$0")/lib.sh"

# re TEMPLATE: an ERE for the text; <c> stands for a commit name ('a1' in the criteria), <p> for one in parentheses
re() { printf '%s' "$1" | sed -e 's/[][\.*^$(){}?+|]/\\&/g' -e "s/<c>/[^']+/g" -e 's/<p>/[^)]+/g'; }
err_re() { grep -qE -- "^$(re "$1")\$" "$T/err"; }
out_re() { grep -qE -- "^$(re "$1")\$" "$T/out"; }
# stops RC TEMPLATE: exit code RC, and the first stderr line that is not a warning starts with TEMPLATE
stops() { [ "$rc" = "$1" ] && grep -v '^warning: ' "$T/err" | head -1 | grep -qE -- "^$(re "$2")"; }
# the commands the hints give, and the commits they offer for K (K=C), as sorted OIDs
hint_cmds() { grep '^hint: ' "$T/err" | grep -oE "'ait contract pull [^']*'"; }
picks() { hint_cmds | grep -oE " $1=[^ ']+" | sed "s/^ $1=//" | sort -u |
	while read -r c; do git rev-parse -q --verify "$c^{commit}" || echo "?$c"; done | sort -u | tr '\n' ' '; }
# par N: the N-th parent of HEAD
par() { git rev-parse -q --verify "HEAD^$1"; }
oids() { printf '%s\n' "$@" | sort -u | tr '\n' ' '; }
link_state() { git cat-file -p "$(at "$1" ".ait/undertake/$(id_of "$2")-$(id_of "$3")")" | sed -n 's/^state //p'; }
# once END...: taking the ends in this order, no file conflicts twice (decision 26, as scen6.py steps26): an end
# that changes a file already in conflict (from its merge base with what is merged so far) fails the order
once() {
	cur=$(git rev-parse HEAD); : > "$T/conf"
	for e; do
		for p in $(git diff --name-only "$(git merge-base "$cur" "$e")" "$e"); do grep -qxF "$p" "$T/conf" && return 1; done
		git merge-tree --write-tree --name-only "$cur" "$e" > "$T/mt"
		awk 'NR == 1 { next } /^$/ { exit } { print }' "$T/mt" | grep -v '^\.ait/' >> "$T/conf"
		cur=$(git commit-tree "$(head -1 "$T/mt")" -p "$cur" -p "$e" -m tmp)
	done
}
SE="agent-e S00000000000e"

# N5  D current, main's current offer OE undertakes D ; a1 reopens D ; 'pull D'
oe() { echo oe > src/oe.c; cond OE oe; rec OE current provider=src/oe.c "creator=$SE" >/dev/null; link D OE >/dev/null; }
DSTATE=current pbase oe
git checkout -q -b a; DF=$(rec D future); A1=$(c "a: reopen D"); git checkout -q master
DC=$(recat HEAD D) OE0=$(recat HEAD OE)
fact "N5: a1's D is future on top of my current D" test "$(state_of "$DF")" = future -a "$(prevs "$DF")" = "$DC" -a "$(state_of "$DC")" = current
fact "N5: merging a1 conflicts nowhere and changes only .ait/D" test -z "$(mt HEAD "$A1")" -a "$(git diff --name-only HEAD "$A1")" = ".ait/$D"
fact "N5: the merged state: OE current undertakes D, D future (5.9 d)" test "$(state_of "$OE0")" = current -a "$(link_state "$A1" D OE)" = undertaken
want "N5: pull D stops at 4, nothing changed" pull_unchanged "$D"
want "N5: an error" stops 1 "error: "
want "N5: hint 'pull D future amend OE -m <why>'" sh -c "grep '^hint: ' '$T/err' | grep -qF \"'ait contract pull $D future amend $OE -m <why>'\""
pull "$D" future amend "$OE" -m why
want "N5: following the hint passes" test "$rc" = 0
want "N5: D is a1's version" test "$(recat HEAD D)" = "$DF"
want "N5: OE is reopened on top of mine" test "$(state_of "$(recat HEAD OE)")" = future -a -n "$(contains "$(recat HEAD OE)" "$OE0" && echo y)"
want "N5: the undertaking stays" test "$(link_state HEAD D OE)" = undertaken
want "N5: 'included D from ...'" out_re "included $D from '<c>'"
want "N5: a commit of HEAD and a1" test "$(parents)" = 2 -a "$(par 2)" = "$A1"

# XT1  D current, drop X ; a1 fulfils X (D withdrawn) ; main cancels X ; 'pull D X'
x0() { rec X future target=D >/dev/null; }
DSTATE=current pbase x0
git checkout -q -b a; XF=$(rec X fulfilled target=D); DW=$(rec D withdrawn); A1=$(c "a: fulfil X"); git checkout -q master
XC=$(rec X cancelled target=D); E1=$(c "e: cancel X")
D0=$(recat init D)
fact "XT1: D's latest version is a1's withdrawn, which contains mine" test "$(recat HEAD D)" = "$D0" -a "$(state_of "$DW")" = withdrawn -a -n "$(contains "$DW" "$D0" && echo y)"
fact "XT1: X has two ended versions, fulfilled and cancelled" test "$(state_of "$XF")" = fulfilled -a "$(state_of "$XC")" = cancelled
fact "XT1: ... with different bodies" not same_body "$XF" "$XC"
fact "XT1: ... neither contains the other" test -z "$(contains "$XF" "$XC" && echo y)$(contains "$XC" "$XF" && echo y)"
want "XT1: pull D X stops, nothing changed" pull_unchanged "$D" "$X"
want "XT1: X diverged" stops 1 "error: '$X' has diverged between "
want "XT1: both versions are choices, untried (user decision 54)" test "$(hint_cmds | wc -l | tr -d ' ')" = 2
want "XT1: the hint is 'pull D X=a1 -m <why>'" sh -c "grep '^hint: ' '$T/err' | grep -qE \"'ait contract pull $D $X=[^ ']+ -m <why>'\""
want "XT1: it offers a1 and HEAD" test "$(picks "$X")" = "$(oids "$A1" "$(git rev-parse HEAD)")"
want "XT1: taking HEAD's is refused by that pull itself" pull_unchanged "$D" "$X=HEAD" -m why
pull "$D" "$X=$A1" -m why
want "XT1: following it passes (4절)" test "$rc" = 0
want "XT1: D is a1's withdrawn version" test "$(recat HEAD D)" = "$DW"
want "XT1: X has a1's fulfilled body and keeps both versions" test -n "$(same_body "$(recat HEAD X)" "$XF" && echo y)" -a "$(prevs "$(recat HEAD X)" | tr '\n' ' ')" = "$(oids "$XF" "$XC")"
want "XT1: a commit of HEAD and a1" test "$(parents)" = 2 -a "$(par 2)" = "$A1"
want "XT1: 'included D from ...' and 'included X from ...'" test -n "$(out_re "included $D from '<c>'" && out_re "included $X from '<c>'" && echo y)"

# XT2  c1: D-O ; a1: D c-A, x.c ; d1: D c-D ; b1: O, x.c ; main: x.c ; 'pull D O'
# XT3  the same, and d1 also changes x.c
xt() { echo o > src/o.c; cond O o0; rec O future provider=src/o.c >/dev/null; lines10 x > src/x.c; lines10 y > src/y.c; }
for row in XT2 XT3; do
	pbase xt
	git checkout -q -b c; link D O >/dev/null; C1=$(c c1)
	git checkout -q -b a; cond D c-A; DA=$(rec D); edit src/x.c x1 "x1 A"; A1=$(c a1)
	git checkout -q c; git checkout -q -b d; cond D c-D; DD=$(rec D); [ $row = XT3 ] && edit src/x.c x1 "x1 D"; D1=$(c d1)
	git checkout -q c; git checkout -q -b b; cond O o-B; OB=$(rec O future provider=src/o.c); edit src/x.c x1 "x1 B"; B1=$(c b1)
	git checkout -q master; edit src/x.c x1 "x1 E"; E1=$(c e1)
	D0=$(recat init D)
	fact "$row: a1's and d1's D both contain mine" test -n "$(contains "$DA" "$D0" && contains "$DD" "$D0" && echo y)"
	fact "$row: ... neither contains the other, and their bodies differ (D diverged)" test -z "$(contains "$DA" "$DD" && echo y)$(contains "$DD" "$DA" && echo y)$(same_body "$DA" "$DD" && echo y)"
	fact "$row: b1's O contains mine" contains "$OB" "$(recat HEAD O)"
	fact "$row: D=a1: in either order of a1 and b1, src/x.c conflicts twice" test -z "$(once "$A1" "$B1" && echo y)$(once "$B1" "$A1" && echo y)"
	if [ $row = XT2 ]; then
		fact "XT2: D=d1: d1 then b1 conflicts in src/x.c, once" test -n "$(once "$D1" "$B1" && echo y)" -a "$(cat "$T/conf")" = src/x.c
	else
		fact "XT3: D=d1: in either order of d1 and b1, src/x.c conflicts twice" test -z "$(once "$D1" "$B1" && echo y)$(once "$B1" "$D1" && echo y)"
	fi
	want "$row: pull D O stops, nothing changed" pull_unchanged "$D" "$O"
	want "$row: D diverged" stops 1 "error: '$D' has diverged between "
	want "$row: the hint offers both D=a1 and D=d1" test "$(picks "$D")" = "$(oids "$A1" "$D1")"
	want "$row: each choice keeps O" test "$(hint_cmds | grep -c " $O ")" = 2
	if [ $row = XT2 ]; then pick=$A1 pn=a1; else pick=$D1 pn=d1; fi
	want "$row: following D=$pn stops at 4, nothing changed" pull_unchanged "$D=$pick" "$O" -m why
	want "$row: ... both ends change src/x.c, which I changed too" stops 1 "error: '<c>' and '<c>' can only be taken together, but both change 'src/x.c', which you changed too"
	want "$row: ... hint: the two states (decision 31)" err_line "hint: they can be taken once one of them is rebased onto the other, or once your branch merges cleanly with one of them"
done
done_testing
