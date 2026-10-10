---
name: ait-mini
description: ait-mini(완성판 ait의 부분집합)로 여러 에이전트가 같은 코드를 동시에 고치는 저장소에서, 코드에 거는 계약(요구·떠맡기·이행)을 다루는 법. 저장소에 .ait/ 디렉터리(.ait/init)가 있거나 ait 명령·출력(`fatal: not supported in ait-mini` 등)이 보이면, 사용자가 말하지 않아도 코드를 고치거나 커밋하기 전에 먼저 읽는다. Use whenever a repository has an .ait/ directory or you see the ait command or its output, before editing or committing code there.
---

# ait-mini 행동교범

**이 저장소에서 너는 다른 에이전트 여럿과 같은 코드를 동시에 고친다.** 서로의 코드에 기대는 부분이 깨지지 않게, "이 코드는 이 조건을 지켜야 한다"는 약속을 **계약**으로 적어 두고 지킨다. 그것을 다루는 도구가 `ait`이고, 지금 쓰는 것은 그 부분집합인 **ait-mini**다. ait-mini의 기록은 완성판 ait가 그대로 이어받는다.

- `ait`는 git 확장이다. `ait <명령>`으로 쓰고, ait 명령이 아니면 그대로 git이 실행된다. 계약은 저장소의 `.ait/`에 커밋으로 남는다.
- 네가 할 일은 다섯이다.
  1. 남의 코드에 기대면, 그 코드가 지킬 조건을 **요구(demand)**로 건다(`ait future demand …`). 답을 기다리지 않고 네 일을 계속한다.
  2. 요구된 일을 네가 시작하면, 그 요구를 **떠맡는다(undertake)**(`ait future offer … undertake …`).
  3. 구현을 마치면, 조건을 직접 확인한 결과와 함께 **이행(fulfill)**한다(`ait fulfill … -r <결과>`). 그때부터 그 계약은 확인된(current) 계약이다.
  4. 확인된 계약의 코드를 한 커밋으로 바꾸면, 같은 커밋에서 조건을 다시 확인한다(`ait commit -r <결과>`).
  5. 확인된 계약의 코드를 여러 커밋에 걸쳐 바꾸려면(리팩터링), 먼저 그 계약을 약속(future)으로 되돌리고(`ait future amend …`), 끝에서 다시 이행한다.
- 출력은 git과 같다. `fatal:`·`error:`·`warning:`은 무엇이 어디서 왜 그런지이고, 이어지는 `hint:`가 다음에 실행할 명령이다. **hint를 그대로 따른다.** hint가 없으면 할 일이 없거나, 첫 행이 가리킨 대상을 고치면 된다.
- ait-mini에 없는 명령이나 형식을 쓰면 `fatal: not supported in ait-mini: '…'`로 멈춘다(아래 "없는 것과 대신 할 것").

## 있는 명령

| 명령 | 하는 일 |
| --- | --- |
| `ait init` | 이 clone에 hook 네 개를 깔고, 처음이면 `.ait/init`을 커밋한다. 다시 해도 같다 |
| `ait session open --agent <이름>` · `list` · `close [SID]` | 내 session을 열고, 보고, 닫는다 |
| `ait [future] demand <consumer> <provider> <명세 정의> [as x]` | 요구를 건다 |
| `ait [future] offer <provider> <명세 정의> [as x]` | 보장(약속)을 건다 |
| `ait future offer … as o undertake <D> o` | 요구를 떠맡는다(offer와 한 명령으로) |
| `ait undertake <D> <O>` · `ait release <D> <O>` | 있는 offer로 떠맡는다 · 떠맡기를 놓는다 |
| `ait [future] amend <K> [인자]` | 계약을 고친다. `future amend`는 current를 약속으로 되돌린다 |
| `ait future cancel <K>` | future 계약을 거둔다 |
| `ait fulfill <ID>[=범위]… -r <결과> / --force` | 구현을 확인해 이행한다 |
| `ait commit [<ID>=범위]… -r <결과> / --force` | 확인이 필요하면 확인하며 커밋한다 |
| `ait todo [<ID>…] [<범위>…]` | 내 할 일을 본다. ID를 주면 그 계약을, 범위를 주면 그 범위에 닿는 future·미확인 계약을 본다 |

- 한 명령에 절(clause)을 여럿 이을 수 있다(`ait future amend D… future amend O… -m …`). 기록하는 명령은 모두 `-m "이유"`가 필요하다.
- `future`를 붙이면 약속(아직 성립하지 않음)이고, 확인할 것이 없다. 붙이지 않으면 지금 HEAD의 코드로 확인해야 한다.
- 인자 자리에 `-`를 쓰면 그 값을 그대로 둔다(`ait future amend K - - <새 명세 정의>`).

## 없는 것과 대신 할 것

| 없는 것 | 대신 |
| --- | --- |
| `ait peek` | 계약 하나는 `ait todo <ID>`, 범위의 future·미확인 계약은 `ait todo <범위>`. 범위의 current 계약은 레코드를 직접 본다(아래 "current 계약의 코드를 고친다") |
| `ait listen` · `sub` · `unsub` (알림) | 없다. 다른 브랜치를 들인 뒤(`git fetch`·`git merge`) `ait todo`로 본다 |
| `ait contract pull` · `push` | `git merge <브랜치>` · `git push`. merge 커밋은 hook이 레코드를 검증한다 |
| `ait contract new` · `add` · `commit` | 명세 정의 파일은 직접 쓴다(아래 "명세 정의"). 레코드는 도우미 명령으로만 고친다 |
| `ait future drop` (current 계약 철회) | 먼저 되돌리고(`ait future amend K -m …`), 다음 요청으로 거둔다(`ait future cancel K -m …`) |
| 행 범위 `a.c:10-40` | 파일 범위 `a.c`, 디렉터리 `src/`, 전체 `/`를 쓴다 |
| 명세 정의의 `environment` | 쓰지 않는다 |
| merge driver | git의 기본 병합. `.ait/`가 충돌하면 한쪽 판을 골라 stage하고 마친다 |

## 원칙

1. **코드에는 주인이 없다.** 요구는 누구에게 보내는 것이 아니라 코드에 거는 것이다. 누가 할지는 착수할 때 정해진다.
2. **착수할 때 떠맡는다.** 손댈 때 `undertake`로 답한다. 미리 잡아 두지 않는다. 그만두면 바로 거둔다. 잡고만 있으면 남이 그 약속을 기다리므로 병목이다.
3. **착수할 때의 최신 판으로 개발한다.** 착수 전에 다른 에이전트의 브랜치를 들여 그 판으로 일한다.
4. **기다리지 않는다.** 요구를 건 쪽은 그 약속(future)을 기준으로 자기 일을 계속한다. 아무도 맡지 않았는데 내게 필요해지면 내가 떠맡는다.
5. **누구 몫인지는 기록이 말한다.** 조건은 계약을 만든 쪽, 구현은 떠맡은 쪽, 확인은 `-r`(또는 `--force`)로 커밋한 쪽의 것이다. 남의 계약도 되돌리고 떠맡고 이행할 수 있지만, 거두거나(cancel) 놓지(release) 않는다. 그것은 만든 쪽이 자기 계약에 하는 일이다.
6. **바뀐 상태를 기준으로 일한다.** 내가 건 계약이 고쳐지거나 거둬지거나 약속으로 되돌려졌어도, 지금 있는 계약대로 일한다.
7. **경고가 나오면 hint를 바로 따른다.** 확인된 계약의 코드를 확인 없이 바꾸면 경고와 hint가 나온다. 그 hint대로 다시 확인하거나 약속으로 되돌린다.

## 낱말

- **demand**: "저 코드(provider)가 이 조건을 충족해야 한다. 내가 쓰는 코드(consumer)가 거기 기댄다."
- **offer**: "이 코드(provider)가 이 조건을 충족한다(하게 하겠다)."
- **undertake**: offer가 demand를 떠맡는다. offer가 이행되면 떠맡은 demand도 함께 이행된다.
- **범위**: `a.c`(파일), `src/`(디렉터리), `/`(전체). 여럿이면 `,`로 잇는다. 아직 없는 파일은 `new:a.c`. 어디에 구현될지 모르면 `/`나 `src/`로 쓰고(**추상 범위**, future의 provider에만), 이행할 때 좁힌다. 경로는 언제나 저장소 최상위 기준이다.
- **명세 정의**(YAML): 계약이 충족할 조건(`body`)과 확인 방법. 명세 정의와 그것이 선언한 검사 재료(`inputs`)를 **명세 파일**이라 한다.
- **future**(약속)는 아직 이행되지 않은 계약, **current**는 확인된 계약이다. current는 언제나 믿을 수 있어야 한다.
- **고정**: 계약이 범위와 명세 파일의 그때 내용을 적어 두는 것. 확인하며 커밋하면 지금 내용으로 다시 고정된다.
- **걸린 계약**: 내 변경이 current 계약의 provider 코드에 닿으면 그 계약은 내 변경에 걸린다. future와 consumer는 걸리지 않는다.
- **미확인**: current 계약의 코드나 조건이 확인 없이 바뀐 **사고 표시**다. `ait todo`에 `unverified yes`로 나온다. 다시 확인하거나 확인한 내용으로 코드를 복원하면 없어진다.
- **되돌리기**(`ait future amend K`): current 계약을 다시 약속으로 만든다. 그동안의 코드 변경은 사고가 아니고, 다시 이행할 때 확인한다. 되돌린 계약은 되돌린 쪽의 약속이 된다. current offer가 떠맡은 current 요구는 그 offer와 한 요청에서 함께 되돌린다.
- **`--force`**: 확인 결과 대신 "사소하다고 판단하고 책임진다"는 확인. `forced`로 기록된다.
- `as x`로 붙인 별명은 그 명령 안에서만 쓴다. 다른 명령에는 출력된 ID(`new D… x`의 `D…`)를 쓴다.

## 명세 정의

`specs/` 같은 곳에 YAML로 쓴다. 키는 아래뿐이고, 다른 키는 형식 오류다.

```yaml
body: 재고가 0인 상품의 예약 요청은 거절한다.   # 조건. 필수
engine: exec                                  # 명령으로 확인. 빼면 prompt(코드를 읽고 판단)
command: [sh, tests/no_oversell.sh]           # exec면 필수
success: [0]                                  # 통과로 볼 exit 코드. 빼면 [0]. 비우면 오류
inputs: [tests/no_oversell.sh]                # 함께 고정할 검사 재료. 검사받는 코드는 넣지 않는다
```

- 파일을 쓴 뒤 도우미 명령에 그 경로를 주면, 명령이 그 파일을 계약과 함께 커밋한다.
- 이미 걸린 계약의 명세 파일 내용을 바꾸려면, 파일을 고치고 `ait amend <K> -m "왜"`(current면 확인 결과가 필요하다)나 `ait future amend <K> -m "왜"`를 실행한다.
- ait는 아무것도 실행하지 않는다. **확인은 내가 하고**, 결과를 `-r`로 낸다.

## 확인 결과를 내는 법

current를 만들거나 다시 확인하는 명령(`fulfill`, `ait commit`, `future` 없는 `demand`·`offer`·`amend`)은 확인할 것이 있으면 결과가 필요하다.

1. 먼저 `-r` 없이 실행한다. `error: check results required: N task(s)`가 나오는 것이 정상이다. 커밋하지 않고 `task` 행들과 `check-request`·`check-result`의 경로를 준다.
2. `check-request`를 읽는다. 과제(`tasks`)마다 조건(`body`)이 있고, exec면 돌릴 `command`가 있다. 확인할 코드는 `tree`다.
3. 과제마다 `tree`의 코드로 직접 확인한다.
   - `fulfill`·`ait commit`의 `tree`는 stage한 판이다. 도우미 명령은 HEAD의 코드에 새 명세 파일을 얹은 판이다. 작업 폴더가 그것과 다르면 따로 꺼낸다: `top=$(git rev-parse --show-toplevel) && d=$(mktemp -d) && GIT_INDEX_FILE="$d/index" git -C "$top" read-tree <tree> && GIT_INDEX_FILE="$d/index" git -C "$top" checkout-index -a --prefix="$d/tree/"`
   - 과제에는 남의 계약도 있을 수 있다. 내 변경에 걸린 계약이다.
4. `check-result`를 채운다. 과제마다 `outcome`은 `pass`·`fail`·`unknown` 중 하나이고, pass에는 `basis`(돌린 명령과 그 결과, 읽은 코드 위치)가, fail·unknown에는 `reason`이 필요하다. 다른 줄은 고치지 않는다.
   - 돌려 보지 않은 것을 pass로 적지 않는다. 확인하지 못했으면 `unknown`이다.
5. 같은 명령에 `-r <check-result 경로>`를 붙여 다시 실행한다. 모두 pass여야 커밋되고, 결과는 커밋 메시지의 `Ait-Check:` 행으로 남는다.
   - fail이면 hint가 고칠 명령을 지금 상태에서 그대로 되는 순서로 준다. 그 순서를 따른다. 코드를 고쳤으면 다시 stage하고 `-r` 없이 처음부터 한다(과제의 `input`이 바뀐다).
6. 주석·오타처럼 하찮은 변경이면 `-r` 대신 `--force`.

## 일의 흐름

명세 문서만 있고 그것을 구현한 코드가 아직 전혀 없으면, 먼저 [references/spec-only.md](references/spec-only.md)를 읽는다. 명세의 조항마다 계약을 걸고 하나씩 떠맡아 구현하는 절차다. 아래 흐름은 그 위에서도 그대로 쓴다.

**시작·재시작**
- `ait init`을 한다. hook이 없거나 ait 명령이 `this clone is not set up for ait`를 쓰면 다시 한다. 저장소가 이미 hook을 쓰면 ait는 덮어쓰지 않고 넣을 행을 hint로 준다.
- 에이전트마다 worktree와 브랜치를 따로 둔다: `git worktree add ../<내 이름> -b <내 이름>` 하고 그 디렉터리로 간다. 다른 에이전트가 쓰는 worktree에서 session을 열면 경고와 hint가 나온다.
- 내 이름(agent)은 내가 맡은 작업처럼, 다른 에이전트와 겹치지 않고 재시작해도 같은 것으로 정한다.
- `ait session list`에서 `agent`가 내 이름이고 `worktree`가 지금 worktree인 session이 있으면 그것을 쓴다(재시작). 없으면 `ait session open --agent <내 이름>`. 출력의 hint를 따른다.
- `ait todo`로 내 약속(future)과, 내 요구 중 확인 없이 바뀐 것을 본다. 블록마다 hint가 다음 할 일을 준다.

**착수한다** (코드에 손대기 직전)
1. 최신 판을 들인다: 함께 일하는 브랜치를 `git merge <브랜치>`(원격이면 먼저 `git fetch`). 충돌은 아래 "합칠 때".
2. `ait todo <작업할 범위>`로 그 범위의 계약을 본다. 아무도 맡지 않은 future demand(`undertaken-by` 행이 없는 것)는 내가 떠맡을 것이다. 이미 맡은 쪽이 있으면 그 약속을 기준으로 개발한다.
3. 떠맡는다. 여럿이면 한 명령으로:
   `ait future offer <구현할 범위> <명세 정의> as o undertake D1… o undertake D2… o -m "착수"`
   - 명세 정의는 요구의 것을 그대로 써도 된다. 더 보장할 것이 있으면 더한다.
   - 내가 건 요구도 같다. 내가 하기로 하면 내가 떠맡는다.
- 계약 커밋은 그 브랜치의 코드 커밋보다 먼저 한다. 그래야 남이 그 계약을 일찍 본다.
- 맡을 요구가 없어도, 남이 기댈 것을 새로 만들면 착수할 때 `future offer`로 약속한다.

**개발 중**
- 남의 약속(future)을 기준으로 개발한다. 아직 구현되지 않았어도 그 조건대로 있다고 보고 쓴다.
- 커밋은 `git add` 뒤 `ait commit -m "…"`으로 한다. 걸린 계약이 없으면 `git commit`과 같은 커밋이고, 있으면 그 자리에서 과제를 준다. ait commit은 git의 커밋 hook(lint 등)을 거치지 않으니 따로 돌린다.
- 기대는 계약이 바뀌었는지는 다른 브랜치를 들인 뒤 `ait todo <그 ID>`로 본다.

**코드를 쓰다 "이건 지켜져야 해"라고 판단했다**
- 그 조건을 명세 정의로 쓴다(위 "명세 정의").
- 남의 코드에 기대는 조건이면 demand: `ait future demand <내가 쓰는 코드> <기댈 코드> specs/x.yaml -m "왜"`. 어디서 구현될지 모르면 provider를 `/`나 `src/`로. 걸고 나서 기다리지 않는다.
- 내가 만드는 코드에 대해 보장할 조건이면 offer: `ait future offer <그 코드> specs/x.yaml -m "왜"`.
- 이미 성립한 조건이면 `future` 없이 건다(`ait demand …`, `ait offer …`). HEAD의 코드로 확인하므로, 확인할 코드를 먼저 커밋한다.
- 계약은 다른 에이전트와 맞닿는 곳에 건다. 범위는 파일 단위가 기본이다.
- 내가 만든 계약을 고칠 때: `ait [future] amend <ID> …`(offer는 `<provider> <명세 정의>`, demand는 `<consumer> <provider> <명세 정의>`, 그대로 둘 자리는 `-`).

**current 계약의 코드를 고친다**
- 고치면 걸릴 계약은 provider가 그 파일을 덮는 current 계약이다. `ait todo <파일>`은 future·미확인만 보여 주므로, current는 레코드에서 본다: `grep -l '^state current' .ait/[DO]* | xargs grep -H '^provider'`(provider가 그 파일, 그 파일을 품은 디렉터리 `DIR/`, 전체 `/`인 것). 자세히는 `ait todo <ID>`.
- **한 커밋으로 끝나면**: `git add` 뒤 `ait commit -m "…"`. 걸린 계약이 있으면 과제를 준다(`touch` 행). 확인해 `-r`로 낸다. 코드를 지우거나 옮겨 계약의 코드를 찾을 수 없으면 커밋되지 않고(`code-required`), hint가 그 계약을 새 위치로 옮기는 명령(`ait commit K…=<새 범위> …`)을 준다.
- **여러 커밋에 걸치면**: 아래 "리팩터링".
- `git commit`으로 커밋해 `warning: this commit leaves N current contract(s) unverified`가 나오면 사고다. hint대로 다시 확인하거나 되돌린다.
- 내 변경이 남의 계약 조건을 일부러 바꾸는 것이면 확인은 실패한다. 그때는 그 계약을 약속으로 되돌리며 새 조건을 건다(`ait future amend K - - <새 명세 정의> -m "왜"`, offer면 `K - <새 명세 정의>`).

**리팩터링** (확정된 코드를 여러 커밋에 걸쳐 바꾼다)
```sh
grep -l '^state current' .ait/[DO]* | xargs grep -H '^provider'   # 걸릴 current 계약을 모두 본다
ait future amend O… future amend D… -m "inventory로 옮길 예정"    # 한 요청으로 되돌린다
# 리팩터링 커밋들 (git commit 자유. 중간에 테스트가 깨져도 된다)
ait fulfill O… D… -m "옮김"                                    # -r 없이: provider 행(지금 위치)과 과제를 받는다
ait fulfill O…=<옮긴 곳> D…=<옮긴 곳> -r <check-result> -m "옮김"
```
- 되돌리지 않은 계약이 걸리면 `ait commit`이 그 자리에서 확인을 요구하고, `git commit`이면 사고가 된다. 그래서 첫 줄로 빠짐없이 본다.

**이행한다**
1. 최신 판을 들인다(`git merge`).
2. 구현을 stage한 뒤 `ait fulfill O… -m "무엇을 했나"`. 보고와 hint를 따른다.
   - `ready no`면 보고의 `mixed`(명세 파일이 섞였다), `invalid`, `code-required`를 hint대로 고친다.
   - `ready yes`면 위의 "확인 결과를 내는 법"대로 확인해 `-r`로 다시 실행한다.
   - `touch` 행은 내 변경에 걸린 계약이고, 과제에 들어 있다.
3. 추상 계약은 구현한 코드로 좁혀 이행한다(`ait fulfill O…=src/runner.c,src/cli.c D…=src/runner.c -r <결과> -m "…"`).
4. 내 브랜치를 함께 일하는 브랜치에 넣는다(`git merge`, 원격이면 `git push`). pre-push hook이 보내는 커밋의 레코드를 검증한다.

**그만둔다**
- 떠맡은 offer를 거둔다(`ait future cancel O… -m "중단: 남은 일, 부분 구현이 있는 브랜치"`). 요구는 그대로 남아 누구든 다시 떠맡는다.
- 그 offer로 다른 것도 계속 약속한다면 요구 하나만 놓는다(`ait release D… O… -m "이유"`).
- 리팩터링을 그만두면 되돌린 계약은 내 약속으로 남는다. 코드가 확인한 그대로면 다시 이행해 두고 떠난다.
- 내가 건 요구가 더는 필요 없으면: future면 `ait future cancel D… -m "이유"`. current면 먼저 `ait future amend D… -m "이유"`, 다음 요청으로 `ait future cancel D… -m "이유"`.
- 이 worktree에서 일을 완전히 끝내면 `ait session close`.

## 합칠 때

- 다른 브랜치를 `git merge`로 들인다. merge 커밋은 pre-merge-commit hook이, 충돌을 풀어 마치는 커밋(merge·cherry-pick·revert)은 pre-commit hook이 레코드를 검증하고, 틀리면 막는다.
- `.ait/<ID>`가 충돌하면 한쪽 판을 골라(`git checkout --ours|--theirs .ait/<ID>`) stage하고 마친다. 명세 파일은 손으로 합친다. hook이 결과를 거절하면 그 hint대로 한 뒤 다시 커밋한다.
- 통합 커밋에서 hook은 레코드 형식만 본다. 합친 뒤 `ait todo`로 미확인이 된 계약이 없는지 보고, 있으면 hint대로 다시 확인한다.
- 형식이 틀린 레코드는 도우미가 읽지 못한다. hint대로 그 레코드를 마지막으로 맞던 커밋의 판으로 되돌려 그것만 stage하고 `git commit --no-verify`로 커밋한다(계약 경로를 바꾸는 일반 커밋은 hook이 막는다).
