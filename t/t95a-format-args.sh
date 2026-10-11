#!/bin/sh
# The input formats of the spec (args), each with what it accepts and what it refuses; a refused request changes nothing
. "$(dirname "$0")/lib.sh"
setup
A=$(ait session open --agent a)
prompt_defs; exec_def pass
git add -A && tick && git commit -q --no-verify -m defs
H=$(git rev-parse HEAD)
bad() { kind=$1 d=$2; shift 2; tick; expect_unchanged expect_err "$d" "$kind" "$@"; }

# 3.1 PATH: not empty; no leading / or -; no blank, control character, ':' or ','; no empty, '.' or '..' piece; not .ait
for p in '/src/order.c' 'src//order.c' './src/order.c' 'src/../src/order.c' 'src/order .c' '.ait/x' '.ait' \
	"$(printf 'src/a\001b')" "$(printf 'src/a\177b')" "$(printf 'src/a\tb')"; do
	bad E_SCOPE "a provider '$p'" ait future offer "$p" specs/p.yaml -m m
	bad E_SCOPE "a definition '$p'" ait future offer src/order.c "$p" -m m
done
bad E_USAGE "a scope starting with '-'" ait future offer -x specs/p.yaml -m m
bad E_SCOPE "an empty piece of a scope list" ait future offer src/order.c, specs/p.yaml -m m

# 3.2 scope arguments
H95=$(git rev-parse HEAD); tick; expect_ok "a line range" ait future offer src/order.c:1-2 specs/p.yaml -m m; git reset -q --hard "$H95"
for r in 0-2 3-1 a-b 1-; do bad E_SCOPE "a malformed range ':$r'" ait future offer "src/order.c:$r" specs/p.yaml -m m; done
bad E_SCOPE "new: for a file that exists" ait future offer new:src/order.c specs/p.yaml -m m
bad E_SCOPE "a file that is not there" ait future offer src/none.c specs/p.yaml -m m
bad E_SCOPE "an abstract scope on a current offer" ait offer src/ specs/p.yaml --force -m m
bad E_SCOPE "a directory without '/'" ait future offer src specs/p.yaml -m m
for ok in new:src/new.c src/ / src/order.c,src/stock.c; do
	tick; expect_ok "a future provider '$ok'" ait future offer "$ok" specs/p.yaml -m m
	git reset -q --hard "$H"
done

# 1.9 aliases and IDs
for a in Bad a-b 1a demand "$(printf 'a%.0s' $(seq 1 33))"; do
	bad E_USAGE "an alias '$a'" ait future offer src/order.c specs/p.yaml as "$a" -m m
done
bad E_USAGE "an alias used twice" ait future offer src/order.c specs/p.yaml as x future offer src/stock.c specs/p.yaml as x -m m
bad E_ID "an ID that does not exist" ait future amend O000000000000 -m m
bad E_ID "an alias from another command" ait future amend x -m m
bad E_USAGE "a clause that does not exist" ait future offer src/order.c specs/p.yaml bogus -m m

# 5.3 messages
bad E_USAGE "a blank message" ait future offer src/order.c specs/p.yaml -m '   '
bad E_READ "a message file that is not there" ait future offer src/order.c specs/p.yaml -F "$T/none"
printf 'a\000b\n' > "$T/nul"
bad E_FORMAT "a message file with a NUL" ait future offer src/order.c specs/p.yaml -F "$T/nul"

done_testing
