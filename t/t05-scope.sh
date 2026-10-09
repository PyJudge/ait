#!/bin/sh
# T05 범위 검증 (3.1-3.4)
. "$(dirname "$0")/lib.sh"
setup

A=$(ait session open --agent a)
prompt_defs

# A (the consumer) is each of these; FILES is p. All are E_SCOPE and unchanged.
# The ":0-3" style entries are read as src/order.c:0-3 etc. They are malformed PATH:S-E
# arguments (3.2: S, E are [1-9][0-9]*, S<=E) and paths with ':' (3.1), so E_SCOPE even in mini.
for a in src/order.c:0-3 src/order.c:5-3 src/order.c:05-6 src/order.c:3 src/order.c:a-b \
	new:src/x.c:1-2 new:src/order.c \
	src/missing.c src \
	../x src/../order.c ./src/order.c .ait/x .ait src//order.c src/order.c/ src/a,b.c
do
	tick
	expect_unchanged expect_err "consumer '$a'" E_SCOPE ait future demand "$a" src/stock.c specs/p.yaml -m m
done

# mini: src/order.c:10-51 is a well-formed line range (E beyond the 50 lines); mini has no line
# ranges, so it says so instead of E_SCOPE
tick
expect_unchanged expect_err "consumer 'src/order.c:10-51'" MINI ait future demand src/order.c:10-51 src/stock.c specs/p.yaml -m m

tick
expect_unchanged expect_err "new: as provider of a current demand" E_SCOPE ait demand src/order.c new:src/x.c specs/p.yaml -m m
tick
expect_unchanged expect_err "offer new: is a scope error before results (5.13)" E_SCOPE ait offer new:src/x.c specs/p.yaml -m m

tick
with_r pass ait demand new:src/c.c src/stock.c specs/p.yaml -m m
check "demand new:src/c.c src/stock.c p -r pass is ok" [ "$rc" = 0 ]
tick
expect_ok "future offer new:src/x.c p" ait future offer new:src/x.c specs/p.yaml -m m

# mini: peek is not supported
expect_unchanged expect_err "peek src/order.c:10-51" MINI ait peek src/order.c:10-51
expect_unchanged expect_err "peek src/nothing.c" MINI ait peek src/nothing.c

done_testing
