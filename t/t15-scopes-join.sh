#!/bin/sh
# spec 2.3.2, v14 3.2a: a stored scope list is in PATH byte order, the whole file first, then lines
. "$(dirname "$0")/lib.sh"
setup
printf 'z\n' >z.c
mkdir -p dir && printf 'd\n' >dir/d.c
git add z.c dir/d.c && git commit -qm more
run ait future offer z.c,src/order.c:10-12,src/order.c,src/order.c:2-4,src/order.c:2-3,new:b.c,dir/,/ cond.yaml -m order
check_rc 'an offer takes scopes in any order' 0
O=$(newest O)
base=$(git rev-parse HEAD:src/order.c)
check 'the provider is stored in canonical order with its bases' \
	"yaml $O | grep -qx 'provider: \"/,new:b.c,dir/,src/order.c,src/order.c:2-3 $base,src/order.c:2-4 $base,src/order.c:10-12 $base,z.c\"'"
run ait status /
check 'status shows the same order without bases' "has out '^provider /,new:b.c,dir/,src/order.c,src/order.c:2-3,src/order.c:2-4,src/order.c:10-12,z.c\$'"
done_testing
