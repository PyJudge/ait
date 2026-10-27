#!/bin/sh
# v14 3.2a; spec 6.4.2, 6.7.5: FILES is ordered and each path is read once per request.
. "$(dirname "$0")/lib.sh"
setup
printf 'body: original A\n' > a.yaml
printf 'body: original B\n' > b.yaml

run ait future offer src/stock.c b.yaml,a.yaml,b.yaml,a.yaml -m 'duplicate inputs'
O=$(newest O)
check_rc 'repeated condition paths are accepted as one occurrence each' 0
yaml "$O" > "$T/offer"
check 'duplicate FILES create exactly two specs' "test \"\$(grep -c '^- body:' $T/offer)\" = 2"
printf 'original B\noriginal A\n' > "$T/order"
sed -n 's/^    \(original [AB]\)$/\1/p' "$T/offer" > "$T/actual-order"
check 'deduplication keeps first occurrence order, rather than sorting paths' "cmp -s $T/order $T/actual-order"

run ait future amend "$O" - a.yaml,a.yaml -m 'duplicate amendment input'
check_rc 'an amendment accepts repeated condition paths' 0
check 'amendment replaces specs with the single distinct input' "test \"\$(yaml $O | grep -c '^- body:')\" = 1"
run ait offer src/stock.c a.yaml,a.yaml -m 'duplicate checking input'
check_rc 'a fulfilling helper still requests check results' 1
check 'a repeated condition path produces one check task' "test \"\$(grep -c '^task ' $T/out)\" = 1 && has out '^task +1:1$'"

# This range stays current after an unrelated line changes, but judging it runs git diff.
run ait future demand src/order.c src/stock.c:1-2 cond.yaml -m 'range target'
D=$(newest D)
ait fulfill "$D" --force -m current >/dev/null 2>&1
sed -i.bak '40s/.*/changed outside provider/' src/stock.c
git add src/stock.c && git commit -qm 'change outside provider'
racing_git
cat > "$T/before-diff" <<'H'
printf 'body: changed A\n' > a.yaml
H
# The first offer reads A. Drop's target judgment changes A before the second list is read.
run env PATH="$TP/bin:$PATH" ait future offer src/order.c a.yaml as first \
	future drop "$D" future offer src/order.c a.yaml,b.yaml as second -m 'overlapping input lists'
check_rc 'overlapping FILES lists can share one condition snapshot' 0
FIRST=$(sed -n 's/^new \(O[0-9a-f]*\) first$/\1/p' "$T/out")
SECOND=$(sed -n 's/^new \(O[0-9a-f]*\) second$/\1/p' "$T/out")
check 'the controlled file change occurred between clauses' "test -f $T/before-diff.done && grep -qx 'body: changed A' a.yaml"
check 'the first contract retains the original condition' "yaml $FIRST | grep -q 'original A'"
check 'the overlapping list reuses the same original condition' \
	"yaml $SECOND > $T/second && grep -q 'original A' $T/second && ! grep -q 'changed A' $T/second && grep -q 'original B' $T/second"
check 'overlapping-list reuse neither loses nor duplicates the additional condition' "test \"\$(grep -c '^- body:' $T/second)\" = 2"
done_testing
