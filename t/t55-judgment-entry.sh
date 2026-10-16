#!/bin/sh
# spec 5: one judgment per tree; a verdict needs no position, a position no verdict.
. "$(dirname "$0")/lib.sh"
setup
real_git=$(command -v git)
mkdir "$T/trace-bin"
cat >"$T/trace-bin/git" <<GIT
#!/bin/sh
case " \$* " in *' -U0 '*) printf '%s\n' "\$*" >>"$T/diffs" ;; esac
exec "$real_git" "\$@"
GIT
chmod +x "$T/trace-bin/git"
traced() { : >"$T/diffs"; run env PATH="$T/trace-bin:$PATH" "$@"; }
diffs() { wc -l <"$T/diffs" | tr -d ' '; }

printf 'x\n' >src/other.c
git add src/other.c && git commit -qm other
run ait future demand src/order.c:1-2,src/order.c:10-12 src/stock.c cond.yaml -m d
D=$(newest D)
run ait fulfill "$D" --force -m verified
check_rc 'prepare a current demand with consumer ranges' 0
sed -i.bak '30s/.*/head/' src/order.c && git commit -qam head
sed -i.bak '40s/.*/index/' src/order.c && git add src/order.c

traced ait future drop "$D" -m withdraw
check_rc 'a drop judges its target' 0
check 'judging a target maps no consumer range' "[ \$(diffs) = 0 ]"

traced ait status src/other.c
check_rc 'a filtered status runs' 0
check 'a filtered status judges only what it shows' "[ \$(diffs) = 0 ] && ! has out \"^id $D\$\""

traced ait status
check_rc 'a full status runs' 0
cp "$T/diffs" "$T/full-diffs"
check 'one consumer pair is diffed once for display' "[ \$(diffs) = 1 ] && has out '^consumer src/order.c:1-2,src/order.c:10-12\$'"

printf 'a\n' >'q?x'
printf 'b\n' >plain
git add -A && git commit -qm q
run ait future offer 'q?x' cond.yaml -m q
Q=$(newest O)
run ait fulfill "$Q" --force -m q
check_rc 'fulfil a path that holds a question mark' 0
printf 'a2\n' >'q?x'
git add -A
run ait status
check 'a changed file whose path holds ? is marked ?changed' "has out '^provider q?x ?changed\$'"
done_testing
