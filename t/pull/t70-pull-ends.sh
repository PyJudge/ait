#!/bin/sh
# pull criteria 3.11 들일 끝이 여럿 (MH1-MH13, ORD1, HK1-HK4, HD2): 1절 "합친 결과" (decision 26 order),
# 2절 4 (the stop sentence, split hints, the decision-20 state hint), 2절 6 (merge state like `git merge --no-ff`,
# the finishing hint, the hook rules). Histories ported from scen.py, scen5.py, scen6.py, scen7.py, scen8.py, scen9.py.
. "$(dirname "$0")/lib.sh"

# ---- helpers of this file
# code_mt A B: the code paths git's merge-tree leaves in conflict (contract files are pull's, not code)
code_mt() { mt "$1" "$2" | tr ' ' '\n' | grep -v '^\.ait/' | tr '\n' ' ' | sed 's/ $//'; }
lt() { [ "$1" != "$2" ] && [ "$(printf '%s\n' "$1" "$2" | sort | head -1)" = "$1" ]; }
refs_list() { git for-each-ref --format='%(refname) %(symref)' refs/heads refs/remotes | awk 'NF==1{print $1}'; }
idx() { git rev-parse -q --verify ":$1"; }
in_merge() { merging && "$@"; }
first_err_re() { head -1 "$T/err" | grep -qE -- "$1"; }
err_re() { grep -qE -- "$1" "$T/err"; }
out_re() { grep -qE -- "$1" "$T/out"; }
setlist() { printf '%s\n' "$@" | sort | tr '\n' ' '; }
prevs_are() { [ "$(prevs "$1" | tr '\n' ' ')" = "$(shift; setlist "$@")" ]; }
# parents_are REV FIRST OTHER...: REV's first parent is FIRST, its other parents are OTHER in any order
parents_are() {
	pa=$(git rev-list --parents -n1 "$1" | cut -s -d' ' -f2-); pf=$2; shift 2
	[ "${pa%% *}" = "$pf" ] && [ "$(echo "${pa#* }" | tr ' ' '\n' | sort | tr '\n' ' ')" = "$(setlist "$@")" ]
}
merge_heads_are() { mh=$(git rev-parse --git-path MERGE_HEAD); [ -f "$mh" ] && [ "$(sort "$mh" | tr '\n' ' ')" = "$(setlist "$@")" ]; }
mhead() { cat "$(git rev-parse --git-path MERGE_HEAD)" 2>/dev/null; }
merge_mode_noff() { [ "$(cat "$(git rev-parse --git-path MERGE_MODE)" 2>/dev/null)" = no-ff ]; }
# compose V CHECK PREV...: a version with V's body, the check line CHECK (- for none) and prev PREV...; prints its blob
compose() {
	cv=$1 ck=$2; shift 2
	{ body "$cv" | awk -v ck="$ck" '/^creator /&&ck!="-"{print ck}1'; for cp in $(printf '%s\n' "$@" | sort -u); do echo "prev $cp"; done; } |
		git hash-object -w --stdin
}
# restore_pinned V: put every spec file V pins back to its pinned blob (scen.restore_pinned)
restore_pinned() { git cat-file -p "$1" | sed -n 's/^file //p' | while read -r fp fm fb fk; do git cat-file -p "$fb" > "$fp"; done; }
# has_all REV PATH: REV's version of PATH contains the version at every ref (5.11): nothing to take
has_all() { hv=$(at "$1" "$2"); for hr in $(refs_list); do hb=$(at "$hr" "$2") || continue; contains "$hv" "$hb" || return 1; done; }
# has_starts V PATH REV...: V contains the version of PATH at each REV that has one
has_starts() { sv=$1 sp=$2; shift 2; for sr; do sb=$(at "$sr" "$sp") || continue; contains "$sv" "$sb" || return 1; done; }
# introducing PATH BLOB: commits reached from the ref list whose PATH is BLOB and none of whose parents has it (1절)
introducing() {
	for ic in $(git rev-list $(refs_list)); do
		[ "$(at "$ic" "$1")" = "$2" ] || continue
		io=1; for ip in $(git rev-list --parents -n1 "$ic" | cut -s -d' ' -f2-); do [ "$(at "$ip" "$1")" = "$2" ] && io=0; done
		[ $io = 1 ] && echo "$ic"
	done
}
# steps26 [-d PATH]... NAME=OID...: merge the ends onto HEAD in this order as scen6.steps26 does. STEPS gets
# 'NAME:FILES' per end (the code files that conflict there, -d PATH: a spec file pull decides), CONFL the files in
# conflict, CUR the merged commit. Fails ('NAME:again:FILE') when an end changes, from its merge base with what is
# merged so far, a file that already conflicts (decision 26, MH10).
steps26() {
	sd=; while [ "$1" = -d ]; do sd="$sd $2"; shift 2; done
	CUR=$(git rev-parse HEAD) CONFL= STEPS=
	for se; do
		sn=${se%%=*} sx=${se#*=}
		for sp in $(git diff --name-only "$(git merge-base "$CUR" "$sx")" "$sx"); do
			case " $CONFL " in *" $sp "*) STEPS="$STEPS $sn:again:$sp"; STEPS=${STEPS# }; return 1 ;; esac
		done
		git merge-tree --write-tree --name-only "$CUR" "$sx" > "$T/mt26"
		sc=$(awk -v d="$sd " 'NR==1{next} /^$/{exit} !/^\.ait\// && index(d, " " $0 " ")==0' "$T/mt26" | tr '\n' ' ' | sed 's/ $//')
		STEPS="$STEPS $sn:$(echo "$sc" | tr ' ' ',')"; CONFL="$CONFL $sc"
		CUR=$(git commit-tree "$(head -1 "$T/mt26")" -p "$CUR" -p "$sx" -m tmp)
	done
	STEPS=${STEPS# }; CONFL=$(echo $CONFL)
}
perms() ( [ $# -le 1 ] && { echo "$1"; exit 0; }
	for x; do r=; for y; do [ "$y" = "$x" ] || r="$r $y"; done; perms $r | sed "s/^/$x,/"; done )
# order26 [-d PATH]... NAME=OID...: the first order, by commit ID, in which each file conflicts at most once (1절
# 차례, record 92). ORDER gets its names (or STOP), ORDER_OIDS its commits, STEPS and CONFL as steps26.
order26() {
	od=; while [ "$1" = -d ]; do od="$od -d $2"; shift 2; done
	for op in $(perms $(printf '%s\n' "$@" | sort -t= -k2)); do
		if steps26 $od $(echo "$op" | tr , ' '); then
			ORDER=$(echo "$op" | tr , '\n' | sed 's/=.*//' | tr '\n' ' ' | sed 's/ $//')
			ORDER_OIDS=$(echo "$op" | tr , '\n' | sed 's/.*=//'); return 0
		fi
	done
	ORDER=STOP ORDER_OIDS= CONFL=; return 1
}
steps_are() { echo "$STEPS" | grep -Eqx -- "$1"; }
# wt_at: a linked worktree $T/wt detached at HEAD (no ref moves) to build what the Python scripts built; wt_done leaves it
wt_at() { git worktree add -q --detach "$T/wt" HEAD && cd "$T/wt"; }
wt_done() { cd "$T/r" && git worktree remove --force "$T/wt"; }
# mstate NAME=OID...: (scen5 MH2', scen6 MH7) the one merge state of the ends in the order of order26, with no
# intermediate commit: the merged tree, the code that conflicts at a step as git's stages, MERGE_HEAD, MERGE_MODE
mstate() {
	order26 "$@" || return 1
	mc=$(git rev-parse HEAD); : > "$T/stages"
	for mo in $ORDER_OIDS; do
		git merge-tree --write-tree "$mc" "$mo" > "$T/mt26"
		awk -F'\t' 'NR==1{next} /^$/{exit} $2 !~ /^\.ait\//' "$T/mt26" >> "$T/stages"
		mc=$(git commit-tree "$(head -1 "$T/mt26")" -p "$mc" -p "$mo" -m tmp)
	done
	git read-tree -m -u HEAD "$mc^{tree}" || return 1
	{ awk -F'\t' '{print "0 0000000000000000000000000000000000000000\t" $2}' "$T/stages" | sort -u; cat "$T/stages"; } |
		git update-index --index-info
	echo "$ORDER_OIDS" > "$(git rev-parse --git-path MERGE_HEAD)"
	echo pull > "$(git rev-parse --git-path MERGE_MSG)"; printf no-ff > "$(git rev-parse --git-path MERGE_MODE)"
}
# finish PATH...: when a merge is in progress, resolve each PATH as ours and run the finishing commit (2절 6);
# FIN gets its exit code (none: there was no merge state to finish)
finish() {
	FIN=none; merging || return 1
	for fp; do git checkout -q --ours -- "$fp" && git add "$fp"; done
	run git commit --allow-empty --no-edit --cleanup=strip; FIN=$rc
}
fin_and() { [ "$FIN" = 0 ] && "$@"; }
FINISH_HINT="hint: commit the resolution first: 'git commit --allow-empty --no-edit --cleanup=strip', then continue as git says"
AUTO_FAILED="Automatic merge failed; fix conflicts and then commit the result."
# valid REV: flows.validate (5.9 b-e as the scripts check them) on REV's tree
valid() {
	vf=$(for vp in $(git ls-tree -r --name-only "$1" .ait); do
		git cat-file -p "$1:$vp" | grep -qx 'role drop' && git cat-file -p "$1:$vp" | grep -qx 'state fulfilled' &&
			git cat-file -p "$1:$vp" | sed -n 's/^target //p'; done)
	for vp in $(git ls-tree -r --name-only "$1" .ait); do
		vt=$(git cat-file -p "$1:$vp"); vs=$(echo "$vt" | sed -n 's/^state //p')
		case $vp in .ait/undertake/*)
			vl=${vp#.ait/undertake/}; vd=$(at "$1" ".ait/${vl%-*}"); vo=$(at "$1" ".ait/${vl#*-}")
			[ -n "$vd" ] && [ -n "$vo" ] || return 1
			[ "$vs" = undertaken ] && [ "$(state_of "$vo")" = current ] && [ "$(state_of "$vd")" = future ] && return 1
			continue ;;
		esac
		case $(echo "$vt" | sed -n 's/^role //p') in
		drop) vg=$(at "$1" ".ait/$(echo "$vt" | sed -n 's/^target //p')") || return 1
			git cat-file -p "$vg" | grep -qx 'role drop' && return 1
			[ "$vs" = fulfilled ] && [ "$(state_of "$vg")" != withdrawn ] && return 1 ;;
		demand|offer)
			[ "$vs" = withdrawn ] && ! echo "$vf" | grep -qx "${vp#.ait/}" && return 1
			case $vs in future|current)
				[ -z "$(echo "$vt" | sed -n 's/^file //p' | while read -r fp fm fb fk; do
					[ "$(at "$1" "$fp")" = "$fb" ] || echo "$fp"; done)" ] || return 1 ;;
			esac ;;
		esac
	done
}

# ---- histories shared by several rows
LK=.ait/undertake/$D-$O
qx() { cond Q q0; rec Q >/dev/null; echo 'x=0' > src/x.c; }
qxy() { qx; echo 'y=0' > src/y.c; }
oxy() { echo o > src/o.c; cond O o0; rec O future provider=src/o.c >/dev/null; echo 'x=0' > src/x.c; echo 'y=0' > src/y.c; }
o10() { echo o > src/o.c; cond O o0; rec O future provider=src/o.c >/dev/null; lines10 x > src/x.c; lines10 y > src/y.c; }
# MH1-MH4 (scen.py MH1/MH2, scen5 MH2'): a1: D c-A ; b1: Q q-B ; with 'clash' a1 x=A, b1 x=B ; main does not touch x.c
mh12() {
	pbase qx
	git checkout -q -b a; cond D c-A; DA=$(rec D); [ -n "$1" ] && echo 'x=A' > src/x.c; A1=$(c a)
	git checkout -q master; git checkout -q -b b; cond Q q-B; QB=$(rec Q); [ -n "$1" ] && echo 'x=B' > src/x.c; B1=$(c b)
	git checkout -q master; H0=$(git rev-parse HEAD)
}
# MH7, MH8, HK1 (scen6 MH7, MH8): c1: D-O ; a1 (on c): D c-A, x=A ; b1 (on c): O o-B and $1.c=B ; main: x=E (and y=E for MH7)
mh7() {
	pbase oxy
	git checkout -q -b c; L1=$(link D O); C1=$(c c1)
	git checkout -q -b a; cond D c-A; DA=$(rec D); echo 'x=A' > src/x.c; A1=$(c a1)
	git checkout -q c; git checkout -q -b b; cond O o-B; OB=$(rec O future provider=src/o.c); echo "$1=B" > "src/$1.c"; B1=$(c b1)
	git checkout -q master; echo 'x=E' > src/x.c; [ "$1" = y ] && echo 'y=E' > src/y.c; H0=$(c e1)
}

# MH1  'pull D Q': D from a1, Q from b1 ; no code conflict
mh12
fact "MH1: a1's D contains mine, b1's Q contains mine" test -n "$(contains "$DA" "$(recat HEAD D)" && contains "$QB" "$(recat HEAD Q)" && echo y)"
order26 a1="$A1" b1="$B1"
fact "MH1: merged in turn, nothing conflicts" test "$ORDER" != STOP -a -z "$CONFL"
pull "$D" "$Q"
want "MH1: pull D Q succeeds" test "$rc" = 0
want "MH1: one commit with three parents: HEAD, a1, b1" parents_are HEAD "$H0" "$A1" "$B1"
want "MH1: D is a1's and Q is b1's" test "$(recat HEAD D)" = "$DA" -a "$(recat HEAD Q)" = "$QB"
want "MH1: specs/d.yaml as a1, specs/q.yaml as b1" test "$(at HEAD specs/d.yaml)" = "$(at "$A1" specs/d.yaml)" -a "$(at HEAD specs/q.yaml)" = "$(at "$B1" specs/q.yaml)"
want "MH1: 'included D from ...'" out_re "^included $D from '[^']+'$"
want "MH1: 'included Q from ...'" out_re "^included $Q from '[^']+'$"
want "MH1: index and work tree are the result" test "$rc" = 0 -a -z "$(git status --porcelain)"

# MH2  as MH1, but a1 and b1 change src/x.c differently ; main did not touch x.c
mh12 clash
order26 a1="$A1" b1="$B1"
fact "MH2: the first end merges clean, the last conflicts on src/x.c" steps_are '[ab]1: [ab]1:src/x\.c'
wt_at; mstate a1="$A1" b1="$B1"
fact "MH2: that state needs no intermediate commit: HEAD as is, 'UU src/x.c'" test "$(git rev-parse HEAD)" = "$H0" -a "$(git status --porcelain | grep -v '^M  ')" = "UU src/x.c"
wt_done
pull "$D" "$Q"
want "MH2: stops like git merge (exit 1)" test "$rc" = 1
want "MH2: CONFLICT line for src/x.c" out_line "CONFLICT (content): Merge conflict in src/x.c"
want "MH2: 'Automatic merge failed; ...'" out_line "$AUTO_FAILED"
want "MH2: the finishing hint" err_line "$FINISH_HINT"
want "MH2: no intermediate commit: HEAD as before" in_merge test "$(git rev-parse HEAD)" = "$H0"
want "MH2: MERGE_HEAD lists both ends a1 and b1" merge_heads_are "$A1" "$B1"
want "MH2: MERGE_MODE is no-ff" merge_mode_noff
want "MH2: MERGE_MSG is written" in_merge test -s "$(git rev-parse --git-path MERGE_MSG)"
want "MH2: only src/x.c is unmerged" in_merge test "$(unmerged)" = src/x.c
want "MH2: the work tree's src/x.c holds the conflict" in_merge grep -q '^<<<<<<<' src/x.c
want "MH2: index .ait/D is a1's, .ait/Q is b1's" in_merge test "$(idx ".ait/$D")" = "$DA" -a "$(idx ".ait/$Q")" = "$QB"
want "MH2: index specs/d.yaml as a1, specs/q.yaml as b1" in_merge test "$(idx specs/d.yaml)" = "$(at "$A1" specs/d.yaml)" -a "$(idx specs/q.yaml)" = "$(at "$B1" specs/q.yaml)"

# MH3  MH2 resolved and finished ; then the same command again
mh12 clash
wt_at; mstate a1="$A1" b1="$B1"; echo 'x=resolved' > src/x.c; git add src/x.c; git commit -q --no-edit --no-verify
fact "MH3: the finishing commit's parents are HEAD, a1, b1" parents_are HEAD "$H0" "$A1" "$B1"
fact "MH3: after it D has nothing to take" has_all HEAD ".ait/$D"
fact "MH3: after it Q has nothing to take" has_all HEAD ".ait/$Q"
wt_done
pull "$D" "$Q"; finish src/x.c
want "MH3: the finishing commit succeeds" test "$FIN" = 0
want "MH3: its parents are HEAD, a1, b1" fin_and parents_are HEAD "$H0" "$A1" "$B1"
pull "$D" "$Q"
want "MH3: the same command again: 'already D Q'" fin_and out_line "already $D $Q"

# MH4  MH2, then 'git merge --abort' right away
mh12 clash
wt_at; S=$(snapshot); mstate a1="$A1" b1="$B1"; git merge --abort
fact "MH4: git merge --abort on that state leaves HEAD, index and work tree as before" test "$(snapshot)" = "$S"
wt_done
S0=$(snapshot); pull "$D" "$Q"
want "MH4: pull left the merge state" merging
run git merge --abort
want "MH4: git merge --abort succeeds" test "$rc" = 0
want "MH4: nothing is left: HEAD, index and work tree as before the pull" test "$rc" = 0 -a "$(snapshot)" = "$S0"

# MH5  D, O future, undertaken ; a1 fulfils O (and D), x=A ; b1 fulfils D only (other consumer), x=B ; 'pull D=b1 O -m <why>'
mh5x() { echo o > src/o.c; cond O o0; rec O future provider=src/o.c >/dev/null; link D O >/dev/null; echo 'x=0' > src/x.c; }
pbase mh5x
git checkout -q -b a; line 1 A; DA=$(rec D current); OA=$(rec O current provider=src/o.c); echo 'x=A' > src/x.c; A1=$(c "a: fulfil O (and D)")
git checkout -q master; git checkout -q -b b; echo 'order B' > src/order.c; line 1 A; DB=$(rec D current); echo 'x=B' > src/x.c; B1=$(c "b: fulfil D")
git checkout -q master; H0=$(git rev-parse HEAD)
fact "MH5: a1's and b1's D both contain D0, neither contains the other" test -n "$(contains "$DA" "$(recat HEAD D)" && contains "$DB" "$(recat HEAD D)" && ! contains "$DA" "$DB" && ! contains "$DB" "$DA" && echo y)"
fact "MH5: their bodies differ (D diverged)" not same_body "$DA" "$DB"
fact "MH5: a1's O contains mine (taken as is)" contains "$OA" "$(recat HEAD O)"
order26 a1="$A1" b1="$B1"
fact "MH5: one end merges without a code conflict, the other conflicts once on src/x.c" steps_are '[ab]1: [ab]1:src/x\.c'
wt_at
for h in $ORDER_OIDS; do
	git merge -q --no-commit --no-ff "$h" >/dev/null 2>&1; git checkout -q --theirs -- . 2>/dev/null
	git cat-file -p "$DB" > ".ait/$D"; git cat-file -p "$OA" > ".ait/$O"; echo 'x=resolved' > src/x.c; c step >/dev/null
done
fact "MH5: the one result (D from b1, O from a1) passes validate" valid HEAD
wt_done
pull "$D=$B1" "$O" -m why
want "MH5: stops like git (exit 1)" test "$rc" = 1
want "MH5: no intermediate commit: HEAD as before" in_merge test "$(git rev-parse HEAD)" = "$H0"
want "MH5: MERGE_HEAD lists a1 and b1" merge_heads_are "$A1" "$B1"
want "MH5: MERGE_MODE is no-ff" merge_mode_noff
want "MH5: only src/x.c is unmerged" in_merge test "$(unmerged)" = src/x.c
want "MH5: index D has b1's body" in_merge same_body "$(idx ".ait/$D")" "$DB"
want "MH5: index D's prev is {a1's D, b1's D}" in_merge prevs_are "$(idx ".ait/$D")" "$DA" "$DB"
want "MH5: index D's check line is reported (b1's)" in_merge test "$(git cat-file -p "$(idx ".ait/$D")" | grep '^check ')" = "$(git cat-file -p "$DB" | grep '^check ')"
want "MH5: index O is a1's" in_merge test "$(idx ".ait/$O")" = "$OA"
finish src/x.c
want "MH5: the finishing commit succeeds" test "$FIN" = 0
want "MH5: its parents are HEAD, a1, b1" fin_and parents_are HEAD "$H0" "$A1" "$B1"
want "MH5: the finished result passes validate" fin_and valid HEAD

# MH6  'pull D Q' : a1 (D) changes x.c, b1 (Q) changes y.c ; main changed both
pbase qxy
git checkout -q -b a; cond D c-A; DA=$(rec D); echo 'x=A' > src/x.c; A1=$(c a)
git checkout -q master; git checkout -q -b b; cond Q q-B; QB=$(rec Q); echo 'y=B' > src/y.c; B1=$(c b)
git checkout -q master; echo 'x=E' > src/x.c; echo 'y=E' > src/y.c; H0=$(c e)
fact "MH6: a1 alone conflicts with HEAD on src/x.c" test "$(code_mt HEAD "$A1")" = src/x.c
fact "MH6: b1 alone conflicts with HEAD on src/y.c" test "$(code_mt HEAD "$B1")" = src/y.c
order26 a1="$A1" b1="$B1"
fact "MH6: in commit-ID order each file conflicts once" steps_are 'a1:src/x\.c b1:src/y\.c|b1:src/y\.c a1:src/x\.c'
pull "$D" "$Q"
want "MH6: stops like git (exit 1)" test "$rc" = 1
want "MH6: CONFLICT line for src/x.c" out_line "CONFLICT (content): Merge conflict in src/x.c"
want "MH6: CONFLICT line for src/y.c" out_line "CONFLICT (content): Merge conflict in src/y.c"
want "MH6: one merge state, HEAD as before" in_merge test "$(git rev-parse HEAD)" = "$H0"
want "MH6: MERGE_HEAD lists a1 and b1" merge_heads_are "$A1" "$B1"
want "MH6: MERGE_MODE is no-ff" merge_mode_noff
want "MH6: src/x.c and src/y.c are unmerged" in_merge test "$(unmerged)" = "src/x.c src/y.c"
finish src/x.c src/y.c
want "MH6: the finishing commit's parents are HEAD, a1, b1" fin_and parents_are HEAD "$H0" "$A1" "$B1"

# MH7  c1: D-O ; a1 (on c): D, x.c ; b1 (on c): O, y.c ; main: x.c and y.c ; 'pull D O'
mh7 y
order26 a1="$A1" b1="$B1"
fact "MH7: each end conflicts with my code once, on different files" steps_are 'a1:src/x\.c b1:src/y\.c|b1:src/y\.c a1:src/x\.c'
wt_at; mstate a1="$A1" b1="$B1"
fact "MH7: one merge state holds 'UU src/x.c' and 'UU src/y.c'" test "$(git status --porcelain | grep '^UU ' | tr '\n' ' ')" = "UU src/x.c UU src/y.c "
echo 'x=R' > src/x.c; echo 'y=R' > src/y.c; git add src/x.c src/y.c; git commit -q --no-edit --no-verify
fact "MH7: finished, its parents are HEAD, a1, b1" parents_are HEAD "$H0" "$A1" "$B1"
fact "MH7: then D has nothing to take" has_all HEAD ".ait/$D"
fact "MH7: then O has nothing to take" has_all HEAD ".ait/$O"
fact "MH7: then the link D-O has nothing to take" has_all HEAD "$LK"
wt_done
pull "$D" "$O"
want "MH7: stops like git (exit 1)" test "$rc" = 1
want "MH7: CONFLICT lines for src/x.c and src/y.c" test "$(grep -c '^CONFLICT (content): Merge conflict in src/[xy]\.c$' "$T/out")" = 2
want "MH7: one merge state, HEAD as before" in_merge test "$(git rev-parse HEAD)" = "$H0"
want "MH7: MERGE_HEAD lists a1 and b1" merge_heads_are "$A1" "$B1"
want "MH7: MERGE_MODE is no-ff" merge_mode_noff
want "MH7: src/x.c and src/y.c are unmerged" in_merge test "$(unmerged)" = "src/x.c src/y.c"
want "MH7: index D is a1's, O is b1's, the link is c1's" in_merge test "$(idx ".ait/$D")" = "$DA" -a "$(idx ".ait/$O")" = "$OB" -a "$(idx "$LK")" = "$L1"
finish src/x.c src/y.c
want "MH7: the finishing commit's parents are HEAD, a1, b1" fin_and parents_are HEAD "$H0" "$A1" "$B1"
pull "$D" "$O"
want "MH7: then 'already D O'" fin_and out_line "already $D $O"

# MH8  as MH7, but b1 changes x.c (O and x.c) ; main: x.c
mh7 x
order26 a1="$A1" b1="$B1"
fact "MH8: in every order src/x.c would conflict twice" test "$ORDER" = STOP
fact "MH8: a1 and b1 both carry c1's link D-O, which HEAD lacks" test "$(at "$A1" "$LK")" = "$L1" -a "$(at "$B1" "$LK")" = "$L1" -a -z "$(at HEAD "$LK")"
want "MH8: stops, nothing changed" pull_unchanged "$D" "$O"
want "MH8: exit 1" test "$rc" = 1
want "MH8: error names both ends, that they come only together, and src/x.c" first_err_re "^error: '[^']+' and '[^']+' can only be taken together, but both change 'src/x\.c', which you changed too$"
want "MH8: hint gives the two states (decision 31)" err_line "hint: they can be taken once one of them is rebased onto the other, or once your branch merges cleanly with one of them"
want "MH8: no split hint (D and O come only together)" test "$rc" = 1 -a -z "$(grep 'take them one at a time' "$T/err")"

# ORD1  'pull D Q' : a1 (D) x.c line 1 ; b1 (Q) x.c line 8 and y.c line 1 ; main x.c line 1 and y.c line 1 ; a1 < b1
ord1x() { cond Q q0; rec Q >/dev/null; lines10 x > src/x.c; lines10 y > src/y.c; }
pbase ord1x
git checkout -q -b b; cond Q q-B; QB=$(rec Q); edit src/x.c x8 'x8 B'; edit src/y.c y1 'y1 B'; B1=$(c b1)
git checkout -q master; git checkout -q -b a; k=
while :; do cond D c-A; DA=$(rec D); edit src/x.c x1 'x1 A'; A1=$(c "a1$k"); lt "$A1" "$B1" && break; git reset -q --hard init; k="$k."; done
git checkout -q master; edit src/x.c x1 'x1 E'; edit src/y.c y1 'y1 E'; H0=$(c e1)
fact "ORD1: a1's commit ID is smaller" lt "$A1" "$B1"
fact "ORD1: order a1, b1: a1 conflicts on x.c and b1 changes x.c again" not steps26 a1="$A1" b1="$B1"
fact "ORD1: (that order stops at b1 on src/x.c)" test "$STEPS" = "a1:src/x.c b1:again:src/x.c"
order26 a1="$A1" b1="$B1"
fact "ORD1: order b1, a1: y.c and x.c conflict once each" test "$ORDER" = "b1 a1" -a "$CONFL" = "src/y.c src/x.c"
wt_at; mstate a1="$A1" b1="$B1"
fact "ORD1: in that merge state x.c's ours side already has b1's line 8" sh -c "git show :2:src/x.c | grep -qx 'x8 B'"
wt_done
pull "$D" "$Q"
want "ORD1: stops like git (exit 1), not on the commit-ID order" test "$rc" = 1
want "ORD1: one merge state, HEAD as before" in_merge test "$(git rev-parse HEAD)" = "$H0"
want "ORD1: MERGE_HEAD lists a1 and b1" merge_heads_are "$A1" "$B1"
want "ORD1: src/x.c and src/y.c are unmerged" in_merge test "$(unmerged)" = "src/x.c src/y.c"
want "ORD1: b1 came first: x.c's ours side has b1's line 8" in_merge sh -c "git show :2:src/x.c | grep -qx 'x8 B'"
want "ORD1: the work tree's x.c keeps b1's line 8" in_merge grep -qx 'x8 B' src/x.c

# HK1  MH7's merge state, then the finishing commit
mh7 y
wt_at; mstate a1="$A1" b1="$B1"
fact "HK1: index D is a1's version (an ait step from D0) and contains every start version" test -n "$(test "$(idx ".ait/$D")" = "$DA" && has_starts "$(idx ".ait/$D")" ".ait/$D" "$H0" "$A1" "$B1" && echo y)"
fact "HK1: index O is b1's version (an ait step from O0) and contains every start version" test -n "$(test "$(idx ".ait/$O")" = "$OB" && has_starts "$(idx ".ait/$O")" ".ait/$O" "$H0" "$A1" "$B1" && echo y)"
fact "HK1: index link is c1's and contains every start version" test -n "$(test "$(idx "$LK")" = "$L1" && has_starts "$(idx "$LK")" "$LK" "$H0" "$A1" "$B1" && echo y)"
wt_done
pull "$D" "$O"
ID=$(idx ".ait/$D") IO=$(idx ".ait/$O") IL=$(idx "$LK")
finish src/x.c src/y.c
want "HK1: the finishing commit succeeds" test "$FIN" = 0
want "HK1: the hook leaves .ait/D as the index had it" fin_and test "$(recat HEAD D)" = "$ID" -a "$ID" = "$DA"
want "HK1: the hook leaves .ait/O as the index had it" fin_and test "$(recat HEAD O)" = "$IO" -a "$IO" = "$OB"
want "HK1: the hook leaves the link as the index had it" fin_and test "$(at HEAD "$LK")" = "$IL" -a "$IL" = "$L1"

# HK2  'pull D Q' stops on a code conflict ; b1 (Q's end) also holds Y made current by hand (an accident)
hk2x() { cond Q q0; rec Q >/dev/null; cond Y y0; rec Y >/dev/null; echo 'x=0' > src/x.c; }
pbase hk2x
git checkout -q -b a; cond D c-A; DA=$(rec D); echo 'x=A' > src/x.c; A1=$(c a1)
git checkout -q master; git checkout -q -b b; line 1 Y; YB=$(rec Y current check=none); cond Q q-B; QB=$(rec Q); B1=$(c "b1: Q, and Y current by hand")
git checkout -q master; echo 'x=E' > src/x.c; H0=$(c e1); Y0=$(recat HEAD Y)
fact "HK2: b1's Y is current without a check line (an accident)" test "$(state_of "$YB")" = current -a -z "$(git cat-file -p "$YB" | grep '^check ')"
fact "HK2: my Y does not contain b1's Y ; it contains every start version that is not an accident" test -n "$(! contains "$Y0" "$YB" && contains "$YB" "$Y0" && echo y)"
order26 a1="$A1" b1="$B1"
fact "HK2: the pull stops on one code conflict, src/x.c" test "$CONFL" = src/x.c
pull "$D" "$Q"
want "HK2: pull stops in a merge state on src/x.c" in_merge test "$(unmerged)" = src/x.c
want "HK2: pull keeps Y as mine" in_merge test "$(idx ".ait/$Y")" = "$Y0"
finish src/x.c
want "HK2: the finishing commit succeeds" test "$FIN" = 0
want "HK2: its hook skips b1's Y with the skip warning" fin_and err_re "^warning: skipping '$Y' at '.*: it was changed outside ait's rules$"
want "HK2: Y is mine as is (no accident made)" fin_and test "$(recat HEAD Y)" = "$Y0"
want "HK2: the finishing commit's parents are HEAD, a1, b1" fin_and parents_are HEAD "$H0" "$A1" "$B1"

# HK3  a1: D cancel, x.c ; main: D c-E, x.c ; 'pull D' stops on x.c ; the resolution writes .ait/D as c-E (future), prev {C, E}
x0() { echo 'x=0' > src/x.c; }
pbase x0
git checkout -q -b a; C=$(rec D cancelled); echo 'x=A' > src/x.c; A1=$(c "a1: cancel D, x.c")
git checkout -q master; cond D c-E; E=$(rec D); echo 'x=E' > src/x.c; H0=$(c "e1: c-E, x.c")
N=$(compose "$E" - "$C" "$E")
fact "HK3: start versions: HEAD's E contains no ended version, a1's C is cancelled" test -n "$(! contains "$E" "$C" && test "$(state_of "$(recat init D)")" = future -a "$(state_of "$C")" = cancelled && echo y)"
fact "HK3: the ended C wins: neither of C and E contains the other" test -n "$(! contains "$C" "$E" && echo y)"
fact "HK3: the resolution N (future, prev {C, E}) contains C with another body (decision 28's accident)" test -n "$(contains "$N" "$C" && ! same_body "$N" "$C" && test "$(state_of "$N")" = future && echo y)"
order26 a1="$A1"
fact "HK3: a1 conflicts with my code on src/x.c" test "$CONFL" = src/x.c
pull "$D"
want "HK3: pull D stops in a merge state on src/x.c" in_merge test "$(unmerged)" = src/x.c
want "HK3: index D is cancelled C's body with prev {C, E}" in_merge test -n "$(same_body "$(idx ".ait/$D")" "$C" && prevs_are "$(idx ".ait/$D")" "$C" "$E" && echo y)"
want "HK3: 'your change to D is overridden' warning" err_re "^warning: your change to '$D' is overridden: '$D' was cancelled in '[^']+'$"
FIN=none
if merging; then
	git cat-file -p "$N" > ".ait/$D"; restore_pinned "$N"; echo 'x=R' > src/x.c; git add -A
	run git commit --allow-empty --no-edit --cleanup=strip; FIN=$rc
fi
want "HK3: the finishing commit's hook refuses it (E_STATE)" test "$FIN" = 1
want "HK3: first line 'error: not allowed:'" test "$FIN" = 1 -a -n "$(first_err | grep '^error: not allowed:')"
A1P=${A1%${A1#???????}}
want "HK3: hint restores .ait/D as in a1 (v14 7.1.3-7)" test "$FIN" = 1 -a -n "$(grep -E "restore '\.ait/$D' as in '$A1P[0-9a-f]*' and stage the result" "$T/err")"
want "HK3: nothing committed, still merging" in_merge test "$FIN" = 1 -a "$(git rev-parse HEAD)" = "$H0"

# MH9  a1: D c-A, x.c ; b0: D c-B ; b1 (on b0): Q, x.c ; main: x.c ; 'pull D=a1 Q -m <why>'
# follow_split ROW: follow the split hint: 'pull D=a1 -m <why>' (end a1), finish keeping main's x.c, then 'pull Q' (end b1)
follow_split() {
	pull "$D=$A1" -m why
	want "$1: following it, 'pull D=a1 -m <why>' stops on src/x.c with a1 the only end" in_merge test "$(unmerged)" = src/x.c -a "$(mhead)" = "$A1"
	finish src/x.c; P1=$(git rev-parse HEAD)
	want "$1: that finishing commit succeeds" test "$FIN" = 0
	pull "$Q"
	want "$1: then 'pull Q' stops on src/x.c once with b1 the only end" fin_and in_merge test "$(unmerged)" = src/x.c -a "$(mhead)" = "$B1" -a "$(git rev-parse HEAD)" = "$P1"
}
# sim_pick_a1 ROW: (scen8 MH9, scen9 MH13) in a worktree, 'pull D=a1' as a merge of a whose x.c keeps main's
sim_pick_a1() {
	wt_at
	git merge -q --no-commit --no-ff "$A1" >/dev/null 2>&1
	V=$(compose "$DA" - "$DA" "$DB"); git cat-file -p "$V" > ".ait/$D"; restore_pinned "$V"; echo 'x=E' > src/x.c; c "pull D=a1" >/dev/null
	fact "$1: after 'pull D=a1' D has nothing to take (b0's D@B is in the result)" has_all HEAD ".ait/$D"
	fact "$1: after it, b1's Q is to take as is" test -n "$(contains "$QB" "$(recat HEAD Q)" && ! contains "$(recat HEAD Q)" "$QB" && echo y)"
	order26 -d specs/d.yaml b1="$B1"
	fact "$1: then 'pull Q' takes b1 alone: src/x.c conflicts once" test "$CONFL" = src/x.c
	wt_done
}
pbase qx
git checkout -q -b a; cond D c-A; DA=$(rec D); echo 'x=A' > src/x.c; A1=$(c a1)
git checkout -q master; git checkout -q -b b; cond D c-B; DB=$(rec D); B0=$(c b0); cond Q q-B; QB=$(rec Q); echo 'x=B' > src/x.c; B1=$(c b1)
git checkout -q master; echo 'x=E' > src/x.c; H0=$(c e1)
fact "MH9: D diverged: a1's c-A and b0's c-B both contain D0, neither the other" test -n "$(contains "$DA" "$(recat HEAD D)" && contains "$DB" "$(recat HEAD D)" && ! contains "$DA" "$DB" && ! contains "$DB" "$DA" && echo y)"
order26 -d specs/d.yaml a1="$A1" b1="$B1"
fact "MH9: a1 and b1 together: in every order src/x.c conflicts twice" test "$ORDER" = STOP
sim_pick_a1 MH9
want "MH9: stops at 4, nothing changed" pull_unchanged "$D=$A1" "$Q" -m why
want "MH9: exit 1" test "$rc" = 1
want "MH9: error names both ends and src/x.c" first_err_re "^error: '[^']+' and '[^']+' both change 'src/x\.c', which you changed too$"
want "MH9: hint: take them one at a time: 'pull D=a1 -m <why>', then 'pull Q'" err_re "^hint: take them one at a time: 'ait contract pull $D=[^ ']+ -m <why>', then 'ait contract pull $Q'$"
want "MH9: not the decision-20 state" test "$rc" = 1 -a -z "$(grep 'they can be taken together' "$T/err")"
follow_split MH9

# MH10  CY1, plus c1 edits x.c line 5 and main edits x.c line 5 ; 'pull D O'
mh10x() { echo o > src/o.c; cond O o0; rec O future provider=src/o.c >/dev/null; lines10 x > src/x.c; }
pbase mh10x
git checkout -q -b c; L1=$(link D O); edit src/x.c x5 'x5 C'; C1=$(c c1)
git checkout -q -b a; cond D c-A; DA=$(rec D); A1=$(c a1)
git checkout -q c; git checkout -q -b b; cond O o-B; OB=$(rec O future provider=src/o.c); B1=$(c b1)
git checkout -q master; edit src/x.c x5 'x5 E'; H0=$(c e1)
fact "MH10: a1 and b1 both carry c1's x.c" test "$(at "$A1" src/x.c)" = "$(at "$C1" src/x.c)" -a "$(at "$B1" src/x.c)" = "$(at "$C1" src/x.c)"
ACC=$(git commit-tree "$(git merge-tree --write-tree HEAD "$A1" | head -1)" -p HEAD -p "$A1" -m tmp)
fact "MH10: merge base of (HEAD + a1) and b1 is c1" test "$(git merge-base "$ACC" "$B1")" = "$C1"
fact "MH10: from there b1 does not change x.c" test -z "$(git diff --name-only "$C1" "$B1" -- src/x.c)"
fact "MH10: from merge-base(HEAD, b1) = init it does" test "$(git merge-base HEAD "$B1")" = "$(git rev-parse init)" -a "$(git diff --name-only init "$B1" -- src/x.c)" = src/x.c
order26 a1="$A1" b1="$B1"
fact "MH10: so x.c conflicts once in commit-ID order" test "$ORDER" != STOP -a "$CONFL" = src/x.c
pull "$D" "$O"
want "MH10: stops like git (exit 1)" test "$rc" = 1
want "MH10: one CONFLICT line, for src/x.c" test "$(grep -c '^CONFLICT' "$T/out")" = 1 -a -n "$(grep -x 'CONFLICT (content): Merge conflict in src/x.c' "$T/out")"
want "MH10: one merge state, HEAD as before" in_merge test "$(git rev-parse HEAD)" = "$H0"
want "MH10: MERGE_HEAD lists a1 and b1" merge_heads_are "$A1" "$B1"
want "MH10: only src/x.c is unmerged" in_merge test "$(unmerged)" = src/x.c
finish src/x.c
want "MH10: the finishing commit's parents are HEAD, a1, b1" fin_and parents_are HEAD "$H0" "$A1" "$B1"

# MH11  D-O taken together ; a1: D, x.c 1, y.c 5 ; b1: O, x.c 8, y.c 1 ; main: x.c 1, y.c 1 ; 'pull D O'
mh11() {
	pbase o10
	git checkout -q -b c; L1=$(link D O); C1=$(c c1)
	git checkout -q -b a; cond D c-A; DA=$(rec D); edit src/x.c x1 'x1 A'; edit src/y.c y5 'y5 A'; A1=$(c a1)
	git checkout -q c; git checkout -q -b b; cond O o-B; OB=$(rec O future provider=src/o.c); edit src/x.c x8 'x8 B'; edit src/y.c y1 'y1 B'; B1=$(c b1)
	git checkout -q master; edit src/x.c x1 'x1 E'; edit src/y.c y1 'y1 E'; H0=$(c e1)
}
mh11
fact "MH11: my x.c conflicts with a1 only, my y.c with b1 only" test "$(code_mt HEAD "$A1")" = src/x.c -a "$(code_mt HEAD "$B1")" = src/y.c
order26 a1="$A1" b1="$B1"
fact "MH11: in both orders a file that already conflicts is changed again" test "$ORDER" = STOP
want "MH11: stops, nothing changed" pull_unchanged "$D" "$O"
want "MH11: exit 1" test "$rc" = 1
want "MH11: error names both files" first_err_re "^error: '[^']+' and '[^']+' can only be taken together, but both change 'src/x\.c' and 'src/y\.c', which you changed too$"
want "MH11: hint gives the two states (decision 31)" err_line "hint: they can be taken once one of them is rebased onto the other, or once your branch merges cleanly with one of them"
want "MH11: no split hint" test "$rc" = 1 -a -z "$(grep 'take them one at a time' "$T/err")"
# a merge is not the decision-20 state: the versions' first commits stay a1 and b1 (decision 1)
git checkout -q b; git merge -q --no-edit --no-verify "$A1" >/dev/null 2>&1; B2=$(c "b2: b merges a1"); git checkout -q master
fact "MH11: after b merges a1, a1 is an ancestor of b2 but not of b1" test -n "$(is_ancestor "$A1" "$B2" && echo y)" -a -z "$(is_ancestor "$A1" "$B1" && echo y)"
want "MH11: after b merges a1, the same stop" pull_unchanged "$D" "$O"
want "MH11: ... the same error" first_err_re "^error: '[^']+' and '[^']+' can only be taken together, but both change"
# state 1 of the hint: b rebases its change onto a1 (b1'); the ends become one
mh11
git checkout -q b; git rebase -q --no-verify "$A1" >/dev/null 2>&1; B1R=$(git rev-parse HEAD); git checkout -q master
fact "MH11/rebase: a1 is an ancestor of b1' and b1' holds O@B" test -n "$(is_ancestor "$A1" "$B1R" && echo y)" -a "$(recat "$B1R" O)" = "$OB"
order26 b1r="$B1R"
fact "MH11/rebase: b1' alone: src/x.c and src/y.c conflict once each" test "$ORDER" != STOP -a "$CONFL" = "src/x.c src/y.c"
pull "$D" "$O"
want "MH11/rebase: stops like git (exit 1)" test "$rc" = 1
want "MH11/rebase: one merge state, MERGE_HEAD is b1' alone" merge_heads_are "$B1R"
want "MH11/rebase: src/x.c and src/y.c are unmerged" in_merge test "$(unmerged)" = "src/x.c src/y.c"
# state 2 of the hint: my branch merges cleanly with b1 (main's y.c line 1 agrees with b1)
mh11
edit src/y.c 'y1 E' 'y1 B'; H1=$(c "e2: y.c agrees with b1")
fact "MH11/mine: my branch merges cleanly with b1" test -z "$(code_mt HEAD "$B1")"
order26 a1="$A1" b1="$B1"
fact "MH11/mine: b1 then a1: src/x.c conflicts once" test "$ORDER" != STOP -a "$CONFL" = src/x.c
pull "$D" "$O"
want "MH11/mine: stops like git (exit 1)" test "$rc" = 1
want "MH11/mine: MERGE_HEAD lists a1 and b1" merge_heads_are "$A1" "$B1"
want "MH11/mine: only src/x.c is unmerged" in_merge test "$(unmerged)" = src/x.c

# MH12  c1: D-O ; a1: D, x.c 1 ; b1: O ; q1 (from init): Q, x.c 1 ; main: x.c 1 ; 'pull D O Q'
mh12x() { o10; cond Q q0; rec Q >/dev/null; }
pbase mh12x
git checkout -q -b c; L1=$(link D O); C1=$(c c1)
git checkout -q -b a; cond D c-A; DA=$(rec D); edit src/x.c x1 'x1 A'; A1=$(c a1)
git checkout -q c; git checkout -q -b b; cond O o-B; OB=$(rec O future provider=src/o.c); B1=$(c b1)
git checkout -q -b q init; cond Q q-Q; QQ=$(rec Q); edit src/x.c x1 'x1 Q'; Q1=$(c q1)
git checkout -q master; edit src/x.c x1 'x1 E'; H0=$(c e1)
order26 a1="$A1" b1="$B1" q1="$Q1"
fact "MH12: all three ends: in every order src/x.c conflicts twice" test "$ORDER" = STOP
order26 a1="$A1" b1="$B1"
fact "MH12: 'pull D O' (a1, b1): src/x.c conflicts once" test "$ORDER" != STOP -a "$CONFL" = src/x.c
fact "MH12: then q1 on top of it conflicts on src/x.c once" test "$(code_mt "$CUR" "$Q1")" = src/x.c
want "MH12: stops at 4, nothing changed" pull_unchanged "$D" "$O" "$Q"
want "MH12: exit 1" test "$rc" = 1
want "MH12: error: two ends both change src/x.c" first_err_re "^error: '[^']+' and '[^']+' both change 'src/x\.c', which you changed too$"
want "MH12: hint: 'pull D O', then 'pull Q'" err_line "hint: take them one at a time: 'ait contract pull $D $O', then 'ait contract pull $Q'"
pull "$D" "$O"
want "MH12: following it, 'pull D O' stops on src/x.c with a1 and b1" in_merge test "$(unmerged)" = src/x.c -a -n "$(merge_heads_are "$A1" "$B1" && echo y)"
finish src/x.c; P1=$(git rev-parse HEAD)
want "MH12: that finishing commit succeeds" test "$FIN" = 0
pull "$Q"
want "MH12: then 'pull Q' stops on src/x.c once with q1 the only end" fin_and in_merge test "$(unmerged)" = src/x.c -a "$(mhead)" = "$Q1" -a "$(git rev-parse HEAD)" = "$P1"

# MH13  as MH9, but b1's commit ID is smaller than a1's
pbase qx
git checkout -q -b b init; cond D c-B; DB=$(rec D); B0=$(c b0); cond Q q-B; QB=$(rec Q); echo 'x=B' > src/x.c; B1=$(c b1)
k=
while :; do git checkout -q -B a init; cond D c-A; DA=$(rec D); echo 'x=A' > src/x.c; A1=$(c "a1$k"); lt "$B1" "$A1" && break; k="$k."; done
git checkout -q master; echo 'x=E' > src/x.c; H0=$(c e1)
fact "MH13: b1's commit ID is smaller" lt "$B1" "$A1"
fact "MH13: 'pull Q' first would take b1" test "$(introducing ".ait/$Q" "$QB")" = "$B1"
fact "MH13: b1 carries D@B, which my D does not contain (it needs D named)" test -n "$(test "$(recat "$B1" D)" = "$DB" && ! contains "$(recat HEAD D)" "$DB" && echo y)"
order26 -d specs/d.yaml a1="$A1" b1="$B1"
fact "MH13: a1 and b1 together: in every order src/x.c conflicts twice" test "$ORDER" = STOP
sim_pick_a1 MH13
want "MH13: stops at 4, nothing changed" pull_unchanged "$D=$A1" "$Q" -m why
want "MH13: exit 1" test "$rc" = 1
want "MH13: error names both ends and src/x.c" first_err_re "^error: '[^']+' and '[^']+' both change 'src/x\.c', which you changed too$"
want "MH13: hint as MH9: 'pull D=a1 -m <why>', then 'pull Q'" err_re "^hint: take them one at a time: 'ait contract pull $D=[^ ']+ -m <why>', then 'ait contract pull $Q'$"
want "MH13: not the decision-20 state" test "$rc" = 1 -a -z "$(grep 'they can be taken together' "$T/err")"
follow_split MH13

# HK4  a1: D c-A newly pins tests/t.sh (line 1 A) ; b1: D c-B ; main: D c-E, tests/t.sh line 1 E as code ; 'pull D=a1'
hk4x() { mkdir -p tests; lines10 t > tests/t.sh; }
pbase hk4x
git checkout -q -b a; edit tests/t.sh t1 't1 A'; printf 'body: c-A\ninputs: [tests/t.sh]\n' > specs/d.yaml; DA=$(rec D future inputs=tests/t.sh); A1=$(c a1)
git checkout -q -b b init; cond D c-B; DB=$(rec D); B1=$(c b1)
git checkout -q master; edit tests/t.sh t1 't1 E'; cond D c-E; DE=$(rec D); H0=$(c e1)
D0=$(recat init D)
V=$(compose "$DA" - "$DA" "$DB" "$DE")
fact "HK4: D@A, D@B, D@E all contain D0 and none contains another (three latest)" test -n "$(for v in "$DA" "$DB" "$DE"; do contains "$v" "$D0" || exit 1; for w in "$DA" "$DB" "$DE"; do [ "$v" = "$w" ] || ! contains "$v" "$w" || exit 1; done; done && echo y)"
fact "HK4: pull's V (body of D@A, prev = the latest) pins tests/t.sh at a1's content" test -n "$(git cat-file -p "$V" | grep -x "file tests/t.sh 100644 $(at "$A1" tests/t.sh) input")"
fact "HK4: my D does not pin tests/t.sh (my change to it is code)" test -z "$(git cat-file -p "$DE" | grep 'tests/t.sh')"
fact "HK4: my side's specs/d.yaml is D@E's pin, not V's" test -n "$(git cat-file -p "$DE" | grep -x "file specs/d.yaml 100644 $(at HEAD specs/d.yaml) def")" -a "$(at HEAD specs/d.yaml)" != "$(at "$A1" specs/d.yaml)"
fact "HK4: git conflicts on tests/t.sh" sh -c "echo ' $(code_mt HEAD "$A1") ' | grep -q ' tests/t.sh '"
TB=$(git show HEAD:tests/t.sh | sed 's/^t1 E$/t1 M/' | git hash-object -w --stdin)
repin() { rv=$1; shift; { body "$rv" | sed "s|^file tests/t.sh 100644 [0-9a-f]* input$|file tests/t.sh 100644 $TB input|"; for p in $(printf '%s\n' "$@" | sort -u); do echo "prev $p"; done; } | git hash-object -w --stdin; }
NV1=$(repin "$V" "$DE" "$DA")
NV2=$(repin "$V" "$V" "$DA" "$DB" "$DE")
fact "HK4: re-pinned with prev = the maximal start versions {D@E, D@A}, D@B is missing" test -n "$(contains "$NV1" "$DA" && contains "$NV1" "$DE" && ! contains "$NV1" "$DB" && echo y)"
fact "HK4: re-pinned on V (prev = V and V's prev), it contains every latest version" test -n "$(contains "$NV2" "$DA" && contains "$NV2" "$DB" && contains "$NV2" "$DE" && echo y)"
pull "$D=$A1" -m why
want "HK4: stops in a merge state on tests/t.sh" in_merge test "$(unmerged)" = tests/t.sh
want "HK4: MERGE_HEAD is a1" merge_heads_are "$A1"
want "HK4: index .ait/D is V (body of D@A, prev {D@A, D@B, D@E})" in_merge test "$(idx ".ait/$D")" = "$V"
want "HK4: index specs/d.yaml is c-A (my c-E is the discarded D@E's spec)" in_merge test "$(idx specs/d.yaml)" = "$(at "$A1" specs/d.yaml)"
FIN=none
if merging; then
	git show HEAD:tests/t.sh | sed 's/^t1 E$/t1 M/' > tests/t.sh; git add tests/t.sh
	run git commit --allow-empty --no-edit --cleanup=strip; FIN=$rc
fi
want "HK4: the finishing commit succeeds" test "$FIN" = 0
want "HK4: it re-pins D to the resolution on top of V" fin_and same_body "$(recat HEAD D)" "$NV2"
want "HK4: the re-pinned D's prev is V and V's prev" fin_and prevs_are "$(recat HEAD D)" "$V" "$DA" "$DB" "$DE"
pull "$D"
want "HK4: then 'already D'" fin_and out_line "already $D"

# HD2  b's HEAD holds REV1's accident V (future, prev {cancelled C, E}) ; b runs a plain git merge of a code-only branch f
hd2x() { echo f0 > src/f.c; }
pbase hd2x
git checkout -q -b a; C=$(rec D cancelled); A1=$(c "a1: cancel D")
git checkout -q master; git merge -q --ff-only a
git checkout -q -b b init; cond D c-E; E=$(rec D); B1=$(c "b1: c-E")
git merge -q --no-commit --no-ff master >/dev/null 2>&1
V=$(compose "$E" - "$E" "$C"); git cat-file -p "$V" > ".ait/$D"; restore_pinned "$V"; BM=$(c "bM: .ait/D resolved by hand as c-E, prev {C, E}")
git checkout -q -b f init; echo f-f > src/f.c; F1=$(c "f1: code only")
git checkout -q b
fact "HD2: b's HEAD holds V" test "$(recat HEAD D)" = "$V"
fact "HD2: V contains the cancelled C with another body (an accident)" test -n "$(contains "$V" "$C" && test "$(state_of "$C")" = cancelled && ! same_body "$V" "$C" && echo y)"
fact "HD2: f's D is D0" test "$(recat "$F1" D)" = "$(recat init D)"
run git merge --no-commit --no-ff --no-verify f
fact "HD2: git merges f without a conflict" test "$rc" = 0
fact "HD2: the index D is HEAD's V as is" test "$(idx ".ait/$D")" = "$V"
git merge --abort
run git merge --no-edit f
want "HD2: the plain git merge passes" test "$rc" = 0
want "HD2: 'warning: '.ait/D' keeps an error from HEAD'" err_line "warning: '.ait/$D' keeps an error from HEAD"
want "HD2: the merge commit's parents are bM and f1" parents_are HEAD "$BM" "$F1"
done_testing
