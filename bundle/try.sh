#!/bin/sh
# Make a playground: a small repository with a spec and no implementation, ait set up,
# the ait skill in .claude/skills, and a worktree for each of two agents, a and b.
#   sh try.sh [DIR]      (default ./ait-playground)
set -e
here=$(cd "$(dirname "$0")" && pwd)
dir=${1:-ait-playground}
command -v ait >/dev/null 2>&1 || { echo "fatal: ait is not on PATH; run 'sh $here/install.sh' first" >&2; exit 128; }
[ ! -e "$dir" ] || { echo "fatal: '$dir' already exists" >&2; exit 128; }

mkdir -p "$dir/main" && cd "$dir/main"
git init -q -b master
mkdir -p doc src tests .claude/skills
cp -R "$here/skills/ait" .claude/skills/
cat > doc/spec.md <<'SPEC'
# 재고와 주문

셸 스크립트 두 개로 된 작은 프로그램이다. `src/stock.sh`는 재고를, `src/order.sh`는 주문을 맡는다.
재고는 `stock.txt`(행마다 `상품 수량`)에 둔다.

1. `sh src/stock.sh reserve 상품 수량`은 재고가 충분하면 그만큼 줄이고 0으로 끝난다. 모자라면 재고를 바꾸지 않고 1로 끝난다.
2. `sh src/stock.sh count 상품`은 그 상품의 재고 수량을 한 행으로 쓴다. 없는 상품은 0이다.
3. `sh src/order.sh 상품 수량`은 재고를 예약하고, 성공하면 `ordered 상품 수량`, 실패하면 `out of stock 상품`을 쓴다. 재고를 직접 고치지 않고 stock.sh를 거친다.
SPEC
cat > CLAUDE.md <<'MD'
# ait playground

여러 에이전트가 `doc/spec.md`를 함께 구현하는 실험용 저장소다. 이 저장소는 ait를 쓴다.
코드를 고치거나 커밋하기 전에 ait 스킬(`.claude/skills/ait`)을 읽고 그 절차를 따른다.
구현은 아직 없다. 스킬의 references/spec-only.md가 이 경우의 절차다.
MD
printf 'stock.txt\n' > .gitignore
git add -A
git -c user.name=setup -c user.email=setup@localhost commit -qm "spec, no implementation"
ait init >/dev/null
git worktree add -q ../a -b a
git worktree add -q ../b -b b
cd ..
top=$(pwd)
cat <<MSG
playground ready: $top
  main/  master (the spec, ait init)
  a/     worktree of agent a, branch a
  b/     worktree of agent b, branch b

Open one Claude Code session in each of a/ and b/. For example:
  a: "너는 에이전트 a다. doc/spec.md를 ait로 b와 함께 구현한다. 먼저 명세의 조항을 계약으로 걸고, 주문(3조)을 맡아라."
  b: "너는 에이전트 b다. doc/spec.md를 ait로 a와 함께 구현한다. a가 건 조항 계약을 contract pull로 받은 뒤 재고(1, 2조)를 맡아라."
Watch: cd $top/main && sh $here/ait-graph.sh
MSG
