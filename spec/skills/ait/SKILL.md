---
name: ait
description: 여러 에이전트가 같은 코드를 동시에 고치는 저장소에서, 코드에 거는 계약(요구·떠맡기·이행)을 ait 명령으로 다루는 법. 저장소에 .ait/ 디렉터리(.ait/init)가 있거나, ait 명령·ait-event 알림·`fatal: no such contract` 같은 ait 출력이 보이면, 사용자가 ait를 말하지 않아도 코드를 고치거나 커밋하기 전에 먼저 읽는다. Use whenever a repository has an .ait/ directory or you see the ait command or its output, before editing or committing code there.
---

# ait 행동교범

**이 저장소에서 너는 다른 에이전트 여럿과 같은 코드를 동시에 고친다.** 서로의 코드에 기대는 부분이 깨지지 않게, 여기서는 "이 코드는 이 조건을 지켜야 한다"는 약속을 **계약**으로 적어 두고 지킨다. 그것을 다루는 도구가 `ait`다.

- `ait`는 git 확장이다. `ait <명령>`으로 쓰고, ait 명령이 아니면 그대로 git이 실행된다. 계약은 저장소의 `.ait/`에 커밋으로 남는다.
- 네가 할 일은 다섯이다.
  1. 남의 코드에 기대면, 그 코드가 지킬 조건을 **요구(demand)**로 건다(`ait future demand …`). 그리고 답을 기다리지 않고 네 일을 계속한다.
  2. 요구된 일을 네가 시작하면, 그 요구를 **떠맡는다(undertake)**(`ait future offer … undertake …`).
  3. 구현을 마치면, 조건을 직접 확인한 결과와 함께 **이행(fulfill)**한다(`ait fulfill … -r <결과>`). 그때부터 그 계약은 확인된(current) 계약이다.
  4. 확인된 계약의 코드를 한 커밋으로 바꾸면, 같은 커밋에서 그 조건을 다시 확인한다(`ait commit -r <결과>`).
  5. 확인된 계약의 코드를 여러 커밋에 걸쳐 바꾸려면(리팩터링), 먼저 그 계약을 약속(future)으로 되돌리고(`ait future amend …`), 끝에서 다시 이행한다(`ait fulfill … -r <결과>`).
- 계약이 바뀌면 관련 에이전트에게 알림이 온다(`ait listen`이 알림을 파일에 쌓아 둔다).
- 출력은 git과 같다. `fatal:`·`error:`·`warning:`은 무엇이 어디서 왜 그런지이고, 이어지는 `hint:`가 다음에 실행할 명령이다. 알림과 보고에도 `hint:`가 붙는다. **hint를 그대로 따른다.** hint가 없으면 할 일이 없거나, 첫 행이 가리킨 대상(파일과 행, 범위, 과제)을 고치면 된다.

예: 너는 주문 코드(`src/order.c`)를 쓰다가, 재고 코드(`src/stock.c`)가 "재고가 0이면 예약을 거절"해야 네 코드가 맞다는 것을 안다.
1. 그 조건을 `specs/no-oversell.yaml`에 적고 요구를 건다(`ait future demand src/order.c src/stock.c specs/no-oversell.yaml -m "재고 초과 판매 금지"`). 출력의 `new D…` 행이 그 요구의 ID다. 그리고 그 조건이 있다고 보고 주문 코드를 계속 쓴다.
2. 재고 코드에 착수하는 에이전트가 그 요구를 떠맡고(`ait future offer src/stock.c specs/no-oversell.yaml as o undertake D… o -m "착수"`. 출력의 `new O…`가 그 offer의 ID다), 재고 코드를 구현하고, 조건을 확인해 이행한다(`ait fulfill O… -r <결과> -m "재고 가드 구현"`. 결과를 내는 법은 아래 "확인 결과를 내는 법").
   - 다른 에이전트가 하면: 너에게 "이행됐다"는 알림이 온다(`contract D… fulfilled via O…`).
   - 네가 직접 하기로 하면: 네가 같은 명령으로 떠맡고, 재고 코드를 구현하고, 확인해 이행한다. offer가 이행되면 떠맡은 요구도 함께 이행된다(`fulfilled D… via O…`).

## 원칙

1. **코드에는 주인이 없다.** 요구는 누구에게 보내는 것이 아니라 코드에 거는 것이다. 누가 할지는 착수할 때 정해진다.
2. **착수할 때 떠맡는다.** 손댈 때 `undertake`로 답한다. 미리 잡아 두지 않는다. 그만두면 바로 거둔다. 잡고만 있으면 남이 그 약속을 기다리므로 병목이다.
3. **착수할 때의 최신 판으로 개발한다.** 떠맡을 때 HEAD에 없는 변경이 있으면 ait가 알려 준다. 들여서 그 판으로 일한다.
4. **기다리지 않는다.** 요구를 건 쪽은 그 약속(future)을 기준으로 자기 일을 계속한다. 아무도 맡지 않았는데 내게 필요해지면 내가 떠맡는다. 이미 누가 맡았어도 그것 없이 내 일이 나아갈 수 없으면 함께 떠맡는다.
5. **누구 몫인지는 기록이 말한다.** 조건은 계약을 만든 쪽, 구현은 떠맡은 쪽, 확인은 `-r`(또는 `--force`)로 커밋한 쪽의 것이다. 남의 계약도 되돌리고 떠맡고 이행할 수 있지만, 거두거나(cancel·drop) 놓지(release) 않는다. 거두고 놓는 것은 만든 쪽이 자기 계약에 하는 일이다. 내가 되돌려 내 약속이 된 남의 계약도 거두거나 놓지 않는다.
6. **바뀐 상태를 기준으로 일한다.** 내가 건 계약이 amend되거나, cancel되거나, 약속으로 되돌려졌어도, 지금 있는 계약대로 일한다. 옛 판으로 복원해 우기지 않는다.
7. **경고가 나오면 hint를 바로 따른다.** 확인된 계약의 코드를 확인 없이 바꾸면 경고와 hint가 나온다. 그 hint대로 다시 확인하거나 약속으로 되돌린다.
8. **확정된 코드를 고쳐야 할 때는 약속 안에서 고친다.** 한 커밋으로 끝나면 같은 커밋에서 다시 확인한다(`ait commit -r …`). 여러 커밋에 걸치면 그 계약을 먼저 약속으로 되돌리고(`ait future amend …`), 끝에서 다시 이행한다.

## 낱말

- **계약**: 에이전트가 코드 범위에 대해 거는 요구나 보장, 그리고 그것의 철회 예약(drop)이다.
  - **demand**: "저 코드(provider)가 이 조건을 충족해야 한다. 내가 쓰는 코드(consumer)가 거기 기댄다"
  - **offer**: "이 코드(provider)가 이 조건을 충족한다(하게 하겠다)"
  - **undertake**: offer가 demand를 떠맡는다. "그 요구는 이 약속으로 해낸다". 끊는 것은 `release`다.
  - 범위는 `a.c`(파일 전체), `a.c:10-40`(행)처럼 쓰고, 여럿이면 `,`로 잇는다. 아직 없는 파일은 `new:a.c`다. 어디에 구현될지 모르면 `/`(전체)나 `src/`(디렉터리)로 쓴다(**추상 범위**, future의 provider에만).
- **명세 정의**(YAML): 계약이 충족할 조건(`body`)과 확인 방법이다. 여럿이면 역시 `,`로 잇는다. 명세 정의와 그것이 선언한 검사 재료(`inputs`)·환경 정의 파일을 **명세 파일**이라 한다.
  - `engine: exec`: `command`를 돌려 확인한다.
  - `engine` 생략: **prompt**다. 코드를 읽고 판단한다.
  - ait는 아무것도 실행하지 않는다. **확인은 내가 하고**, 결과를 `-r`로 낸다.
- **future**(약속)는 아직 이행되지 않은 계약, **current**는 확인된 계약이다. current는 언제나 믿을 수 있어야 한다. current 계약의 provider 코드를 **확정된 코드**라 한다.
- **고정**: 계약이 범위와 명세 파일의 그때 내용을 적어 두는 것이다. 확인하며 커밋하면 지금 내용으로 다시 고정된다.
- **걸린 계약**: 내 변경이 current 계약의 provider 코드에 닿으면 그 계약은 내 변경에 걸린다. future와 consumer는 걸리지 않는다.
- **미확인**: current 계약의 코드나 조건이 확인 없이 바뀐 **사고 표시**다. peek·todo에 `unverified yes` 행으로 나온다. 다시 확인하거나(hint의 명령), 확인한 내용으로 코드를 복원하면 없어진다. `?changed`·`?deleted` 같은 표지는 코드 위치와 변화만 보여 준다.
- **되돌리기**(`ait future amend K`): current 계약을 다시 약속(future)으로 만든다. 그동안의 코드 변경은 사고가 아니고, 다시 이행할 때 확인한다. 남의 계약이어도 된다. 거두는 것이 아니라 "지금은 확인되지 않았다"고 적는 것이다. 되돌린 계약은 되돌린 쪽의 약속이 된다. current offer가 떠맡은 current 요구는 그 offer와 한 요청에서 함께 되돌린다(혼자 되돌리면 오류와 hint가 나온다).
- **`--force`**: 확인 결과 대신 "사소하다고 판단하고 책임진다"는 확인이다. 그 판단이 `forced`로 기록되고, 그 계약은 확인된 것으로 친다.
- 기록하는 명령은 모두 `-m "이유"`가 필요하다. `ait contract …`는 `ait ct …`로 줄여 쓴다.
- 명령의 `as x`처럼 붙인 별명은 그 명령 안에서만 쓴다. 다른 명령에는 출력된 ID(`new D… x`의 `D…`)를 쓴다.
- 경로와 범위는 언제나 저장소 최상위 기준이다. 하위 디렉터리에서 실행해도, hint 문장 속 경로도 같다.
- 행 번호는 HEAD(커밋된 판) 기준이다. `fulfill`·`ait commit`에서 `ID=범위`로 옮기거나 좁힐 때만 stage된 판 기준이다.
- `peek`은 보기만 한다. 바뀌면 알림을 받고 싶은 범위나 ID는 `ait sub <범위나 ID>`로 구독한다. 구독은 `ait unsub <범위나 ID>`로 지울 때까지 남는다.

## 확인 결과를 내는 법

current를 만들거나 확인하는 명령(`fulfill`, `ait commit`, current `demand`·`offer`, current `amend`, current 계약의 명세 파일을 고친 `contract commit`)은 확인할 것이 있으면 확인 결과가 필요하다.

1. 먼저 `-r` 없이 실행한다. `error: check results required`가 나오는 것이 정상이다. 커밋하지 않고 `task` 목록과 `check-request`·`check-result`의 경로를 준다.
2. `check-request`를 읽는다. 과제마다 조건(`body`)이 있고, exec면 돌릴 `command`와 환경이 있다. 확인할 코드는 `tree`다.
3. 과제마다 `tree`의 코드로 직접 확인한다.
   - `fulfill`·`ait commit`의 `tree`는 stage한 판이다. 관문을 지나는 명령(도우미, `contract commit`)은 HEAD의 코드에 새 명세 파일을 얹은 판이다. 작업 폴더가 그것과 다르면 따로 꺼낸다(어느 디렉터리에서 실행해도 전부 꺼낸다): `top=$(git rev-parse --show-toplevel) && d=$(mktemp -d) && GIT_INDEX_FILE="$d/index" git -C "$top" read-tree <tree> && GIT_INDEX_FILE="$d/index" git -C "$top" checkout-index -a --prefix="$d/tree/"`
   - exec는 그 명령을 돌린다. prompt는 코드를 읽는다.
   - 과제에는 남의 계약도 있을 수 있다. 내 변경에 걸린 계약이다.
4. `check-result`를 채운다. `outcome`은 `pass`·`fail`·`unknown` 중 하나이고, `basis`에는 돌린 명령과 그 결과, 본 코드 위치를 적는다.
   - 돌려 보지 않은 것을 pass로 적지 않는다. 확인하지 못했으면 `unknown`이다. unknown이면 hint대로 그 환경(check-request에 있다)을 갖춘 곳에서 확인하거나, 사소하면 `--force`, 확인된 계약을 확인 없이 바꾸는 중이면 hint의 되돌리기를 고른다.
5. 같은 명령에 `-r <check-result 경로>`를 붙여 다시 실행한다. 모두 pass여야 커밋되고, 결과는 커밋 메시지에 남는다.
   - fail이면 그 과제의 provider가 조건을 어긴 것이다. 고칠 명령은 hint가 지금 상태에서 그대로 되는 순서로 준다. 그 순서를 그대로 따른다.
     - `fulfill`·`ait commit`이면 고친 코드를 stage하고 `-r` 없이 다시 실행한다. 계약 쪽을 고치는 것(되돌리기, 명세 파일 고치기, 떠맡은 요구 놓기)은 hint의 도우미 명령을 먼저 실행하고 다시 실행한다.
     - 관문을 지나는 명령(도우미, `contract commit`)은 HEAD의 코드를 확인한다. 코드를 고치려면 hint대로 코드를 먼저 커밋하고 다시 실행한다(`contract commit`은 요청이 `.ait/`를 바꾸지 않았을 때만 이 길이 있다). 계약 쪽을 고치는 것(되돌리기, 범위 옮기기, 약속으로 하기, 명세 파일 고치기)은 그 요청 안에서 한다: 도우미면 hint가 고친 명령을, `contract commit`이면 고칠 파일과 고칠 곳(`state future` 등)을 준다.
     - 확인된 계약을 확인 없이 바꾸는 중이면 hint의 되돌리기를 고른다.
6. 주석·오타처럼 하찮은 변경이면 `-r` 대신 `--force`. 결과 없이 넘기고 `forced`로 남는다.

## 일의 흐름

**시작·재시작**
- 시작할 때마다 `ait init`. 이 clone에 ait가 쓰는 hook과 설정을 깐다. 다시 해도 같다. 저장소가 이미 hook을 쓰면 ait는 덮어쓰지 않고, 그 hook이 `ait hook <이름>`을 부르게 하라는 hint(셸 hook이면 넣을 행)를 준다. 그렇게 고친 뒤 `ait init`을 다시 한다. 저장소 안의 hook 파일(`core.hooksPath`)이면 hint대로 그 변경을 커밋한다(커밋하지 않으면 치울 때 사라진다). hook 도구가 hook 파일을 다시 쓰면 그 행이 사라질 수 있다. ait 명령이 `this clone is not set up for ait`를 쓰면 다시 한다.
- 에이전트마다 worktree와 브랜치를 따로 둔다. 다른 에이전트가 쓰는 worktree면(hint가 알린다) 내 worktree를 새로 만들어(기준은 다른 에이전트가 작업 중인 브랜치가 아닌 것) 거기서 session을 연다.
- 내 이름(agent)은 내가 맡은 작업의 ID처럼, 모든 clone의 다른 에이전트와 겹치지 않고 재시작해도 같은 것으로 정한다.
- `ait session list`에서 `agent`가 내 이름인 session이 있으면 그것을 쓴다(재시작). 그 session의 `worktree`로 가서(`cd`) 일한다. 다른 worktree의 session은 쓸 수 없다. 그 session이 자기 worktree의 기본(`default yes`)이 아니면 명령마다 `AIT_SESSION=<그 SID>`를 붙인다. 그 worktree가 없어졌으면(그 session으로 명령을 실행하면 hint가 그렇게 알린다, 예: `AIT_SESSION=<그 SID> ait todo`) 그 session의 inbox를 다 읽은 뒤 그 session을 닫고, 내 브랜치의 worktree에서 같은 이름으로 연다(내 약속의 구현은 내 브랜치에 있다). 내 이름의 session이 없으면 `ait session open --agent <내 이름>`. 이름이 겹치거나 다른 에이전트의 worktree면 출력의 hint를 따른다.
- `git remote`가 비어 있지 않으면 다른 clone과 일하는 것이다. 그때는 착수·이행 전에 `git fetch`하고, 계약을 바꾼 뒤 `ait contract push`한다(아래 흐름의 "원격이 있으면" 행들).
- 내 session이 `listening no`이면 알림 받기를 띄운다(`ait listen >> "$(git rev-parse --git-common-dir)/ait-inbox.<내 SID>" 2>&1 &`). 다른 clone의 소식도 일하는 중에 받으려면 `--fetch 60`을 붙인다. 띄운 뒤 `ait session list`에서 `listening yes`인지 본다(띄우기에 실패하면 그 이유는 inbox 파일에만 남는다).
- `ait todo`로 내 약속과, 내 요구 중 확인 없이 바뀐 것을 본다. 블록마다 hint가 다음 할 일을 준다. 떠맡아 둔 것 중 이어서 하지 않을 것은 지금 거둔다(아래 "그만둔다").

**착수한다** (코드에 손대기 직전)
1. 원격이 있으면 `git fetch`. inbox를 읽는다(아래 "알림을 읽는다").
2. `ait peek <작업할 범위>`로 그 범위의 계약을 본다. 보이는 future demand마다 hint가 다음 명령을 준다: 아무도 맡지 않았으면 떠맡는 명령, 이미 맡았거나 되돌려 고치는 쪽이 있으면 그 약속을 기준으로 개발하라는 것(원칙 4), 다른 브랜치에서 맡았으면 들이는 명령이다. `unverified yes`인 current가 있으면 다시 확인하는 hint도 나온다.
   - `elsewhere` 블록은 다른 브랜치의 요청이다. 들일 일이면 hint가 들이는 명령을 준다.
3. 떠맡는다. 여럿이면 한 명령으로 떠맡는다. 떠맡은 요구의 변경은 따로 구독하지 않아도 온다.
   `ait future offer <구현할 범위> <명세 정의> as o undertake D1… o undertake D2… o -m "착수"`
   - 명세 정의는 요구의 것을 그대로 써도 된다. 더 보장할 것이 있으면 더한다.
   - 내가 건 요구도 같다. 내가 하기로 하면 내가 떠맡는다.
   - 출력의 경고와 hint를 따른다. HEAD에 없는 변경이 있으면 들이는 명령을, 다른 offer도 떠맡았으면 거두거나 놓는 명령을 준다.
4. 원격이 있으면 바로 `ait contract push`.
- 계약 커밋은 그 브랜치의 코드 커밋보다 먼저 한다(공유된 판 바로 위에). 그래야 남이 `contract pull`로 코드 없이 받는다.
- 맡을 요구가 없어도, 남이 기댈 것을 새로 만들면 착수할 때 `future offer`로 약속한다.

**개발 중**
- 남의 약속(future)을 기준으로 개발한다. 아직 구현되지 않았어도 그 조건대로 있다고 보고 쓴다.
- 남의 약속이나 보장에 기대어 개발하면 `ait sub <그 ID>`로 구독해 둔다. 그것이 바뀌거나 없어지면 알림과 hint가 온다.
- 커밋은 `git add` 뒤 `ait commit -m "…"`으로 한다. 걸린 계약이 없으면 `git commit`과 같은 커밋이고, 있으면 그 자리에서 확인할 과제를 준다. lint 같은 git hook은 거치지 않으니 따로 돌린다.
- 커밋 전에 inbox를 읽는다.

**코드를 쓰다 "이건 지켜져야 해"라고 판단했다**
- 먼저 그 조건을 명세 정의로 쓴다. 틀을 받아 채운다(`ait contract new spec specs/x.yaml exec`, 명령으로 확인할 수 없으면 `prompt`). 틀의 주석이 쓸 수 있는 키를 알려 준다. `inputs`에는 함께 고정할 검사 재료를 적는다(`command`의 스크립트 포함). 검사받는 코드는 넣지 않는다.
  ```yaml
  body: 재고가 0인 상품의 예약 요청은 거절한다.
  engine: exec
  command: [sh, tests/no_oversell.sh]
  inputs: [tests/no_oversell.sh]
  ```
- 남의 코드에 기대는 조건이면 demand를 건다(`ait future demand <내가 쓰는 코드> <기댈 코드> specs/x.yaml -m "왜"`). 어디서 구현될지 모르면 provider를 `/`나 `src/`로 둔다. 걸고 나서 답을 기다리지 않는다.
- 내가 만드는 코드에 대해 보장할 조건이면 offer를 건다(`ait future offer <그 코드> specs/x.yaml -m "왜"`). 어느 코드에 구현할지 모르면 provider를 `/`나 `src/`로 둔다(이행할 때 좁힌다).
- 이미 성립한 조건이면 `future` 없이 건다(`ait demand …`, `ait offer …`). HEAD의 코드로 확인하므로, 확인할 코드를 먼저 커밋한다. 아직 성립하지 않았으면 `future`로 걸고 코드와 함께 이행한다.
- 계약은 다른 에이전트와 맞닿는 곳에 건다. 범위는 파일 단위가 기본이다.
- 내가 만든 계약을 고칠 때: 범위·명세 정의는 `ait amend <ID> …`(offer는 `<provider> <명세 정의>`, demand는 `<consumer> <provider> <명세 정의>`, 그대로 둘 자리는 `-`), 명세 파일 내용은 `ait contract add <파일>` 후 `ait contract commit -m "왜"`.
- 원격이 있으면 바로 `ait contract push`.

**current 계약의 코드를 고친다**
- `ait peek now <파일>`로 그 파일의 current 계약을 본다. `match … provider`인 것이 고치면 걸릴 계약이다. 행 범위로 보면 경계에 삽입할 때 이웃 범위의 계약을 놓칠 수 있으므로 파일 단위로 본다.
- **한 커밋으로 끝나면**: 고친 것을 `git add`한 뒤 `ait commit -m "…"`. 걸린 계약이 있으면 과제를 주므로 확인해 `-r`로 낸다(위 "확인 결과를 내는 법"). 코드를 지우거나 옮겨 계약의 코드를 찾을 수 없으면 커밋되지 않고(`code-required`), hint가 그 계약을 새 위치로 옮기는 명령(`ait commit K…=<남은 조각>,<새 범위> …`)을 준다. 그 명령도 먼저 `-r` 없이 실행해 과제를 받는다.
- **여러 커밋에 걸치면**(리팩터링): 아래 "확정된 코드를 여러 커밋에 걸쳐 바꾼다".
- `git commit`에서 미확인 경고가 나오면 hint대로 한다(원칙 7).
- 내 변경이 남의 계약 조건을 일부러 바꾸는 것이면 확인은 실패한다. 그때는 hint대로 그 계약을 약속으로 되돌리며 새 조건을 건다(`ait future amend K - - <새 명세 정의> -m "왜"`, offer면 `K - <새 명세 정의>`). 받는 쪽은 새 조건을 받는다.

**확정된 코드를 여러 커밋에 걸쳐 바꾼다** (리팩터링)
- 먼저 걸릴 current 계약을 약속으로 되돌린다. 이것이 착수다. 되돌린 동안의 변경은 사고가 아니다. 그래서 중간 커밋에서 테스트가 깨져도 된다.
  ```sh
  ait peek now <바꿀 파일>                                     # 걸릴 current 계약을 모두 본다(파일 단위로)
  ait future amend O… future amend D… -m "inventory로 옮길 예정"   # 한 요청으로 되돌린다. 이미 future인 계약은 할 일이 없다
  # 리팩터링 커밋들 (git commit 자유)
  ait fulfill O… D… -m "옮김"                                   # -r 없이: provider 행(지금 위치)을 받는다
  ait fulfill O…=<그대로 둔 조각>,<옮긴 곳> D…=<옮긴 곳> -m "옮김"   # -r 없이: 그 범위의 과제를 받는다
  ait fulfill O…=<그대로 둔 조각>,<옮긴 곳> D…=<옮긴 곳> -r <check-result> -m "옮김"
  ```
  - 보고의 `provider ID 범위` 행이 지금 코드에서의 위치다. 그대로 둔 조각은 그 값을 쓰고, 옮긴 조각만 새로 더한다.
  - 되돌리지 않은 계약이 걸리면 `ait commit`이 그 자리에서 확인을 요구하고, `git commit`이면 사고가 된다. 그래서 첫 줄의 peek으로 빠짐없이 본다.

**이행한다**
- 원격이 있으면 먼저 `git fetch`. inbox를 읽는다.
- 구현을 stage한 뒤 `ait fulfill O… -m "무엇을 했나"`. 보고와 hint를 따른다.
  - `ready no`면 보고의 `mixed`(명세 파일이 섞였다), `invalid`, `code-required`(확인할 코드를 찾을 수 없다. hint가 새 위치를 주는 명령을 준다)를 고친다.
  - `ready yes`면 위의 "확인 결과를 내는 법"대로 확인해 `-r`로 다시 실행한다.
  - `touch` 행은 내 변경에 걸린 계약이고, 과제에 들어 있다.
  - HEAD에 없는 변경이 있다는 경고가 나오면 hint대로 들인 뒤 이행한다. hint가 먼저 커밋하거나 치우라고 하면 그렇게 한다.
- 추상 계약은 구현한 코드로 좁혀서 이행한다(`ait fulfill O…=src/runner.c,src/cli.c:10-40 D…=src/runner.c -r <check-result> -m "…"`).
- 원격이 있으면 `ait contract push`. 내 브랜치를 통째로 올리고, 막히면 그 hint를 따른다. 내보낼 커밋의 미확인 때문에 막혔는데 정말 사소하면 `ait contract push --force -m "왜"`로 책임지고 올린다(기록된다).

**그만둔다** (다른 일로 옮기거나, 끝내지 못하고 떠날 때)
- 떠맡은 offer를 거둔다(`ait future cancel O… -m "중단: 남은 일, 부분 구현이 있는 브랜치·커밋"`). 요구는 그대로 남아 누구든 다시 떠맡는다.
- 그 offer로 다른 것도 계속 약속한다면 요구 하나만 놓는다(`ait release D… O… -m "이유"`).
- 리팩터링을 그만두면 되돌린 계약은 내 약속으로 남는다. 코드가 확인한 그대로이거나 그렇게 복원했으면 다시 이행해 두고 떠난다(`ait fulfill K… -m "이유"`). 그럴 수 없으면 약속으로 남긴다. 누구든 다시 이행할 수 있고, 만든 쪽은 hint로 그것을 기준으로 개발하거나 거둔다.
- 내가 건 요구가 더는 필요 없으면: future면 cancel한다(`ait future cancel D… -m "이유"`). current면 철회를 걸고 이행한다(`ait future drop D… as x -m "이유"`, 이어서 아무것도 stage하지 않은 채 출력된 X로 `ait fulfill X… -m "이유"`).
- 원격이 있으면 `ait contract push`.
- 이 worktree에서 일을 완전히 끝내면 listener를 멈추고 `ait session close`(listener가 돌면 멈추는 명령을 hint로 준다).

## 알림을 읽는다

- 읽는 때: 착수 전, 커밋 전, 이행 전, 그리고 손이 빌 때. listener는 파일에 쌓기만 하고 나를 깨우지 않는다. 다 읽은 `ait-event-end` 수를 `"$(git rev-parse --git-common-dir)/ait-inbox.<내 SID>.read"`에 적어 두고, 다음에는 그 뒤부터 읽는다. 재시작해도 이어진다.
- 사건 하나는 `ait-event 1`로 시작해 `ait-event-end`로 끝난다. 첫 빈 행까지가 머리말이고, 그 뒤는 커밋 전문이다(필요할 때만 읽는다). 머리말의 `hint:`가 내가 할 일이다. **따른다.** hint가 없으면 알기만 하면 된다. 알림의 명령은 그 알림이 쓰인 때의 상태로 만든 것이므로, git 조작(merge·rebase·cherry-pick 등)이 진행 중이면(`git status`) 그 조작을 마친 뒤 실행한다. 그 변경을 들여야 하면 hint 앞에 들이는 hint(`ait contract pull …`이나 들일 계약 커밋들)가 붙어 있다. 들이기 전에 로컬 변경은 커밋하거나 치운다(git이 거절할 수 있다). stash 목록은 이 저장소의 모든 worktree가 함께 쓰므로 치운 것이 남의 것과 섞이지 않게 한다. 충돌로 멈추면 아래 "합칠 때 계약이 충돌했다"대로 마친다.
- 오는 것: 내 약속(내가 만들었거나 되돌린 약속. 내 offer가 떠맡은 요구, 내 약속인 요구를 떠맡은 offer, 내 drop이 겨눈 계약 포함)과 내 요구의 사건, `ait sub`(또는 `ait listen <범위나 ID>`)로 구독한 것의 사건이다. 다시 확인만 한 것은 오지 않는다. 평소에는 `/`를 구독하지 않는다.
- `contract pull`은 남의 작업 중 코드가 섞이면 멈추고 hint를 준다. 들일 계약 커밋을 주는 hint는 계약만 들이고, `--force`는 코드까지 받는다. 코드까지 받을 일이 아니면 받지 않고 내 일을 계속한다. 들인 뒤에는 원래 hint의 나머지 명령을 다시 실행한다.

## 합칠 때 계약이 충돌했다

- 같은 계약을 두 브랜치가 따로 바꾸면 merge에서 `.ait/<ID>`가 충돌한다. merge가 고를 쪽을 hint로 준다. 그대로 고르고, 명세 파일은 손으로 합친다. 이어서 hint대로 `ait contract add`를 하면, git 조작이 멈춰 있을 때는 그것이 마치는 커밋을 알려 준다. 그 커밋으로 먼저 커밋하고 git의 안내대로 잇는다(git의 `--continue`만 하면 ait의 hook이 돌지 않는다). 레코드가 아닌 코드만 충돌해도 같다. git 조작이 멈춘 동안에는 다른 merge·cherry-pick을 시작하지 않는다. 합치며 고정이 바뀐 current 계약은 hook이 약속으로 되돌리고, 합친 코드가 확인된 것과 다른 계약은 미확인이 된다. 둘 다 경고와 hint가 나온다.
- 치워 둔 변경을 되살리다(`git stash apply`, rebase의 autostash) 계약 파일이 충돌하면 'theirs'가 치워 둔 내 변경이다. 고른 판 위에 남길 내 변경을 다시 해서 ait로 커밋한다. autostash의 충돌에는 git이 ait의 hint를 보여 주지 않는다.
- `git am`은 ait를 거치지 않고 커밋한다. 패치의 계약 변경은 git am에 싣지 말고(HEAD 판으로 둔다), 마친 뒤 패치에서 다시 해서 ait로 커밋한다.
- hook이 레코드를 거절하면 그 hint대로 한쪽 판으로 되돌린 뒤 커밋을 다시 한다.
- git 조작(rebase·cherry-pick 등)이 멈춘 동안 바꾼 계약 파일은 ait만 커밋한다. 그대로 이으면 git이 그것을 hook 없이 커밋하므로, 조작이 끝날 때까지 조작 밖에 두었다가 끝난 뒤 `ait contract add`·`ait contract commit`한다.
- 내 session이 이 worktree의 기본이 아니면 마치는 커밋에도 `AIT_SESSION=<내 SID>`를 붙인다.
