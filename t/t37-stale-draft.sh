#!/bin/sh
# spec 6.5.4 and 4.9: stale drafts are preserved and do not suggest an impossible checkout.
. "$(dirname "$0")/lib.sh"
setup
ait future offer src/stock.c cond.yaml -g drafts -m original >/dev/null
O=$(git ls-tree -r --name-only ait/master drafts/ | sed -n 's|.*/\(O[0-9a-f]*\)\.yaml$|\1|p')
ait contract checkout "$O" >/dev/null
P=.ait/drafts/$O.yaml
cp "$P" "$T/draft-before"
printf 'body: revised condition\n' > newer.yaml
ait future amend "$O" - newer.yaml -m newer >/dev/null
ait contract add "$P" >/dev/null
stage=$(git rev-parse --path-format=absolute --git-path ait/index)
cp "$stage" "$T/stage-before"
tip=$(git rev-parse ait/master)
run ait contract commit -m stale
check_rc 'a stale draft is refused' 1
check 'the refusal names the stale draft and contract' "has err \"'$O' changed at 'ait/master' since '$P' was written\""
check 'a stale draft does not suggest checkout over the existing draft' "! has err 'hint:.*ait contract checkout'"
check 'the refusal preserves the draft, stage, and branch' \
	"cmp -s $P $T/draft-before && cmp -s $stage $T/stage-before && test \"\$(git rev-parse ait/master)\" = $tip"
check 'the refused commit prints no success output' "! test -s $T/out"
done_testing
