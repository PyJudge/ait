#!/bin/sh
# T04 도우미 등록과 커밋 구조 (2.2, 5.3-5.5, 6, 7.3)
. "$(dirname "$0")/lib.sh"
setup

A=$(ait session open --agent agent-a)
mkdir -p specs
echo 'body: 재고가 0이면 예약 요청을 거절한다.' > specs/p.yaml
exec_def guard
echo o51 >> src/order.c; git add src/order.c; echo s51 >> src/stock.c; H0=$(git rev-parse HEAD)

# FILES tests/guard.sh means the exec definition specs/guard.yaml (0.2).
# T04-1: the consumer is the line range src/order.c:10-40
tick
expect_ok "future demand registers" \
	ait future demand src/order.c:10-40 new:src/stock_v2.c specs/p.yaml,specs/guard.yaml as d -m "요구 등록"
D=$(new_id)
RID=$(out_value request)
check "stdout is exactly request + new D d" \
	sh -c '[ $(wc -l < "$T/out") = 2 ] && grep -qE "^request R[0-9a-f]{12}$" "$T/out" && grep -qE "^new D[0-9a-f]{12} d$" "$T/out"'
check "HEAD^ is H0" [ "$(git rev-parse HEAD^)" = "$H0" ]
ns=$(git diff-tree --no-commit-id -r --name-status HEAD)
check "name-status is the four added paths" [ "$ns" = "$(printf 'A\t.ait/%s\nA\tspecs/guard.yaml\nA\tspecs/p.yaml\nA\ttests/guard.sh' "$D")" ]

want=$(cat <<EOF
role demand
state future
consumer src/order.c:10-40 $(blob "$H0:src/order.c")
provider new:src/stock_v2.c
file specs/guard.yaml 100644 $(blob HEAD:specs/guard.yaml) def
file specs/p.yaml 100644 $(blob HEAD:specs/p.yaml) def
file tests/guard.sh 100755 $(blob HEAD:tests/guard.sh) input
creator agent-a $A
prev -
EOF
)
check "rec(D) is the canonical record" [ "$(rec "$D")" = "$want" ]

msg=$(git log -1 --format=%B)
wantmsg=$(printf '[CONTRACT] 요구 등록\n\nAit-Request: %s\nAit-Agent: agent-a\nAit-Session: %s' "$RID" "$A")
check "message is subject, blank, three trailers in order" [ "$msg" = "$wantmsg" ]

check "staged is only src/order.c" [ "$(git diff --cached --name-only)" = "src/order.c" ]
check "unstaged is only src/stock.c" [ "$(git diff --name-only)" = "src/stock.c" ]
check "status of .ait specs tests is empty" [ -z "$(git status --porcelain -- .ait specs tests)" ]

ses=$(git rev-parse --git-common-dir)/ait/sessions/$A
check "session file exists" [ -f "$ses" ]
check "session has no sub line" sh -c "! grep -q '^sub ' '$ses'"

# 7: failures are unchanged
tick
expect_unchanged expect_err "missing spec file" E_INCOMPLETE \
	ait future demand src/order.c new:src/x.c specs/none.yaml -m m
echo 'body: 바뀐 조건.' > specs/p.yaml; git add specs/p.yaml; echo 'body: 또 바뀐 조건.' > specs/p.yaml
expect_unchanged expect_err "partially staged spec file" E_INCOMPLETE \
	ait future demand src/order.c new:src/x.c specs/p.yaml -m m
git checkout -q HEAD -- specs/p.yaml
ln -s p.yaml specs/link.yaml
expect_unchanged expect_err "symlink spec file" E_INCOMPLETE \
	ait future demand src/order.c new:src/x.c specs/link.yaml -m m
rm specs/link.yaml
expect_unchanged expect_err ".ait/x as spec file" E_SCOPE \
	ait future demand src/order.c new:src/x.c .ait/x -m m

done_testing
