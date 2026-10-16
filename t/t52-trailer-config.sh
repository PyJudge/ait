#!/bin/sh
# spec 4.5: every command that appends trailers refuses a config that hides them, after 4.3 and the session.
. "$(dirname "$0")/lib.sh"
setup
run ait future offer src/stock.c cond.yaml -m seed
ait contract push >/dev/null 2>&1
git branch -q --set-upstream-to=origin/ait/master ait/master
run ait contract pull
check_rc 'without such a config, contract pull goes on' 0
check 'and finds nothing to merge' "has out '^Already up to date\$'"
git config trailer.separators =
tip=$(git rev-parse ait/master)
run ait contract pull
check_rc 'contract pull refuses hidden trailers' 128
check 'the refusal names the hidden keys' "has err \"^fatal: git config hides the trailers: 'Ait-Request', 'Ait-Agent', 'Ait-Session' ('trailer.separators')\$\""
printf 'body: |\n  more\n' >"$T/draft.yaml"
run ait contract new offer
path=$(sed -n 's/^path //p' "$T/out")
run ait contract add "$path"
run ait contract commit -m staged
check_rc 'contract commit refuses hidden trailers' 128
check 'no checkin was made' "test \"\$(git rev-parse ait/master)\" = $tip"
git config --unset-all trailer.separators
done_testing
