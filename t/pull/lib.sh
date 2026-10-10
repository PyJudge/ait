# Helpers for the pull criteria (spec/drafts/pull-criteria.md, revision 9). A pull test sources this.
# Each row has two kinds of checks:
#   fact  - the history the row describes, as git and the record data show it ("git에서 확인한 것").
#           These pass now; a failing fact means the fixture is wrong.
#   want  - what `ait contract pull` must do ("기대"). These fail until pull is implemented.
# Notifications (decision 16, 18) are left out for now.
. "$(dirname "$0")/../lib.sh"
AIT_DIR=$(cd "$(dirname "$0")/../.." && pwd)
PATH="$AIT_DIR:$PATH"
export PATH LC_ALL=C

fact() { desc=$1; shift; if "$@"; then say_ok "fact: $desc"; else say_fail "fact: $desc"; fi; }
want() { desc=$1; shift; if "$@"; then say_ok "want: $desc"; else say_fail "want: $desc"; fi; }
not() { ! "$@"; }

# contract IDs (1.10: role letter and 12 hex digits) and the agents' sessions
D=D000000000001 Q=D000000000002 K=D000000000003 K5=D000000000005 K6=D000000000006
W=D000000000007 Y=D000000000008 Z=D000000000009 R=D00000000000a Q2=D00000000000b
A=D00000000000c B=D00000000000d C=D00000000000e
O=O000000000001 O1=O000000000002 O2=O000000000003 OE=O000000000004 O6=O000000000006
X=X000000000001
SA="agent-a S00000000000a" SQ="agent-q S00000000000f"
# A blob variable must not silently change the creator of every later fixture.
readonly SA SQ
id_of() { eval "echo \$$1"; }
spec_of() { echo "specs/$(echo "$1" | tr 'A-Z' 'a-z').yaml"; }

# the fixture of the Python scripts: src/stock.c of nine lines, src/order.c, specs/d.yaml, D future, commit init
# pbase [FUNCTION...]: a fresh fixture; each FUNCTION adds to it before the commit init.
# DSTATE=current (and DCHECK=forced) make init's D current. They are read once and unset here: a shell in
# POSIX mode keeps an assignment made before a function call, so it would leak into the next row.
pbase() {
	ds=${DSTATE:-future} dck=${DCHECK:-reported}; unset DSTATE DCHECK
	[ -n "$T" ] && cd / && rm -rf "$T"
	setup
	MSID=$(ait session open --agent agent-m)
	printf 'line%s v=0\n' 1 2 3 4 5 6 7 8 9 > src/stock.c
	echo order > src/order.c
	mkdir -p specs; echo 'body: c0' > specs/d.yaml
	for f; do "$f"; done
	rec D "$ds" check="$dck" >/dev/null
	tick; c init >/dev/null; git tag init
}

# c MSG: commit everything as an agent would (no ait hook). Prints the commit OID.
c() { git add -A && tick && git commit -q --no-verify --allow-empty -m "$1" && git rev-parse HEAD; }
cond() { printf 'body: %s\n' "$2" > "${3:-$(spec_of "$1")}"; }
line() { awk -v n="$1" -v v="$2" 'NR==n{print "line" n " v=" v; next}1' "${3:-src/stock.c}" > "$T/l" && cat "$T/l" > "${3:-src/stock.c}"; }
edit() { awk -v a="$2" -v b="$3" '$0==a{print b; next}1' "$1" > "$T/l" && cat "$T/l" > "$1"; }
lines10() { for i in 1 2 3 4 5 6 7 8 9 10; do echo "$1$i"; done; }
wblob() { [ -e "$1" ] && git hash-object -w "$1"; }

# rec NAME [STATE] [key=value...]: write .ait/<ID> in the record format (2.2.2) with the check line of
# decision 17, and print its blob. Keys: role spec provider consumer inputs creator check prev target.
# prev defaults to the record now in the work tree (or '-'); check defaults to reported for a current D/O.
rec() {
	name=$1 state=${2:-future}; shift; [ $# -gt 0 ] && shift
	role=demand spec=$(spec_of "$name") provider=src/stock.c consumer=src/order.c inputs= creator=$SA check=reported prev= target=
	case $name in O*) role=offer ;; X*) role=drop ;; esac
	for kv; do eval "${kv%%=*}=\"\${kv#*=}\""; done
	id=$(id_of "$name"); f=.ait/$id
	[ -n "$prev" ] || prev=$(wblob "$f" || echo -)
	{
		echo "role $role"; echo "state $state"
		if [ "$role" = drop ]; then echo "target $(id_of "$target")"; else
			[ "$role" = demand ] && echo "consumer $consumer $(wblob "$consumer")"
			echo "provider $provider $(wblob "$provider")"
			{ echo "$spec def"; for i in $inputs; do echo "$i input"; done; } | sort | while read -r p kind; do
				echo "file $p 100644 $(wblob "$p") $kind"; done > "$T/rows"
			cat "$T/rows"
		fi
	} > "$T/rec"
	if [ "$role" != drop ] && [ "$state" = current ] && [ "$check" != none ]; then
		echo "check $check $(grep -E '^(provider|file) ' "$T/rec" | git hash-object --stdin)" >> "$T/rec"
	fi
	{ cat "$T/rec"; echo "creator $creator"; for p in $(echo $prev | tr ' ' '\n' | sort); do echo "prev $p"; done; } > "$f"
	git hash-object -w "$f"
}
# link D O [STATE]: the undertake record .ait/undertake/<D>-<O>
link() { mkdir -p .ait/undertake; f=.ait/undertake/$(id_of "$1")-$(id_of "$2"); p=$(wblob "$f" || echo -)
	printf 'state %s\nprev %s\n' "${3:-undertaken}" "$p" > "$f"; git hash-object -w "$f"; }

# reading records and commits
at() { git rev-parse -q --verify "$1:$2" 2>/dev/null; }
recat() { at "$1" ".ait/$(id_of "$2")"; }
body() { git cat-file -p "$1" | grep -v -e '^prev ' -e '^check '; }
prevs() { git cat-file -p "$1" | sed -n 's/^prev //p' | grep -v '^-$' | sort; }
state_of() { git cat-file -p "$1" | sed -n 's/^state //p'; }
same_body() { [ "$(body "$1")" = "$(body "$2")" ]; }
# contains A B: B is A or reached from A through prev (a missing blob ends that branch)
contains() {
	[ "$1" = "$2" ] && return 0
	echo "$1" > "$T/todo"; : > "$T/seen"
	while [ -s "$T/todo" ]; do
		o=$(head -1 "$T/todo"); sed -i.bak 1d "$T/todo"
		grep -qx "$o" "$T/seen" && continue; echo "$o" >> "$T/seen"
		for p in $(git cat-file -p "$o" 2>/dev/null | sed -n 's/^prev //p' | grep -v '^-$'); do
			[ "$p" = "$2" ] && return 0; echo "$p" >> "$T/todo"; done
	done
	return 1
}
parents() { git rev-list --parents -n1 "${1:-HEAD}" | wc -w | awk '{print $1-1}'; }
merging() { git rev-parse -q --verify MERGE_HEAD >/dev/null; }
unmerged() { git diff --name-only --diff-filter=U | tr '\n' ' ' | sed 's/ $//'; }
# mt A B: the paths git's merge-tree leaves in conflict
mt() { git merge-tree --write-tree --name-only "$1" "$2" 2>/dev/null | awk 'NR==1{next} /^$/{exit} {print}' | tr '\n' ' ' | sed 's/ $//'; }
is_ancestor() { git merge-base --is-ancestor "$1" "$2"; }

# running pull
pull() { run ait contract pull "$@"; }
first_err() { head -1 "$T/err"; }
err_line() { grep -qxF -- "$1" "$T/err"; }
out_line() { grep -qxF -- "$1" "$T/out"; }
stops_with() { [ "$rc" = "$1" ] && head -1 "$T/err" | grep -qF -- "$2"; }
# a pull that stops changes nothing (2절: ref, HEAD, index, work tree)
pull_unchanged() { before=$(snapshot); pull "$@"; [ "$(snapshot)" = "$before" ]; }
