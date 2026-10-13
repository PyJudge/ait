---
name: ait
description: 여러 에이전트가 같은 코드를 동시에 고치는 저장소에서, 코드에 거는 계약(요구·떠맡기·이행)을 ait 명령으로 다루는 법. 저장소에 .ait/ 디렉터리(.ait/init)가 있거나, ait 명령이나 `fatal: no such contract`·`hint: … ait …` 같은 ait 출력이 보이면, 사용자가 ait를 말하지 않아도 코드를 고치거나 커밋하기 전에 먼저 읽는다. Use whenever a repository has an .ait/ directory or you see the ait command or its output, before editing or committing code there.
---

# ait 행동교범

**이 저장소에서 너는 다른 에이전트 여럿과 같은 코드를 동시에 고친다.** 서로의 코드에 기대는 부분이 깨지지 않게, "이 코드는 이 조건을 지켜야 한다"는 약속을 **계약**으로 적어 두고 지킨다. 그것을 다루는 도구가 `ait`다. 1단계 명세의 알림(`listen`)·`peek`·철회(`future drop`)·`contract push`는 아직 없다(아래 "아직 없는 것").

- `ait`는 git 확장이다. `ait <명령>`으로 쓰고, ait 명령이 아니면 그대로 git이 실행된다. 계약은 저장소의 `.ait/`에 커밋으로 남는다.
- 네가 할 일은 다섯이다.
  1. 남의 코드에 기대면, 그 코드가 지킬 조건을 **요구(demand)**로 건다(`ait future demand …`). 답을 기다리지 않고 네 일을 계속한다.
  2. 요구된 일을 네가 시작하면, 그 요구를 **떠맡는다(undertake)**(`ait future offer … undertake …`).
  3. 구현을 마치면, 조건을 직접 확인한 결과와 함께 **이행(fulfill)**한다(`ait fulfill … -r <결과>`). 그때부터 그 계약은 확인된(current) 계약이다.
  4. 확인된 계약의 코드를 한 커밋으로 바꾸면, 같은 커밋에서 조건을 다시 확인한다(`ait commit -r <결과>`).
  5. 확인된 계약의 코드를 여러 커밋에 걸쳐 바꾸려면(리팩터링), 먼저 그 계약을 약속(future)으로 되돌리고(`ait future amend …`), 끝에서 다시 이행한다.
- 출력은 git과 같다. `fatal:`·`error:`·`warning:`은 무엇이 어디서 왜 그런지이고, 이어지는 `hint:`가 다음에 할 일이다. **hint를 그대로 따른다.** hint가 없으면 할 일이 없거나, ait 명령으로는 고칠 수 없으니 첫 행이 가리킨 대상(파일, 범위, 과제)을 직접 고친다.

## hint 읽는 법

hint는 실행할 ait 명령이나, 만들어 둘 **상태**를 준다. 상태는 git으로 만든다.

| hint의 말 | 하는 일 |
| --- | --- |
| `'ait …'` | 그대로 실행한다. `<why>`처럼 `<…>`로 남은 자리만 채운다 |
| `restore 'P…' as in 'C'` | index와 작업 폴더의 P…를 커밋 C의 판으로: `git checkout C -- P…`. C에 없는 경로는 지운다(`git rm -q -- P`) |
| `restore 'P' from HEAD` | `git checkout HEAD -- P` |
| `remove 'P…'` | index와 작업 폴더에서 지운다: `git rm -q -- P…` |
| `stage 'P…'` · `unstage 'P…'` | `git add -- P…` · `git restore --staged -- P…` |
| `…, then rerun this command` | 앞의 일을 마친 뒤, 거절된 그 명령을 그대로 다시 실행한다 |
| `git commit --allow-empty --no-edit --cleanup=strip` | merge 등 git 조작을 hook이 도는 커밋으로 마친다(아래 "합칠 때") |

## 있는 명령

| 명령 | 하는 일 |
| --- | --- |
| `ait init` | 이 clone에 hook 넷과 설정을 깔고, 처음이면 `.ait/init`을 커밋한다. 다시 해도 같다 |
| `ait session open --agent <이름>` · `list` · `close [SID]` | 내 session을 열고, 보고, 닫는다 |
| `ait [future] demand <consumer> <provider> <명세 정의> [as x]` | 요구를 건다 |
| `ait [future] offer <provider> <명세 정의> [as x]` | 보장(약속)을 건다 |
| `ait future offer … as o undertake <D> o` | 요구를 떠맡는다(offer와 한 명령으로) |
| `ait undertake <D> <O>` · `ait release <D> <O>` | 있는 offer로 떠맡는다 · 떠맡기를 놓는다 |
| `ait [future] amend <K> [인자]` | 계약을 고친다. `future amend`는 current를 약속으로 되돌린다 |
| `ait future cancel <K>` | future 계약을 거둔다 |
| `ait fulfill <ID>[=범위]… -r <결과> / --force` | 구현을 확인해 이행한다 |
| `ait commit [<ID>=범위]… -r <결과> / --force` | 확인이 필요하면 확인하며 커밋한다 |
| `ait todo [<ID>…] [<범위>…]` | 내 할 일과 HEAD의 오류를 본다. ID를 주면 그 계약을, 범위를 주면 그 범위에 닿는 future·미확인 계약을 본다 |
| `ait contract pull <ID>…` (`ait ct pull`) | 남의 계약을 그 판이 처음 들어간 커밋과 조상째로 받는다(아래 "남의 계약을 받는다") |
| `ait contract new spec <경로> exec\|prompt` | 명세 정의의 틀을 만든다 |
| `ait contract new demand\|offer` | 새 레코드의 틀(`.ait/<ID>`)을 만든다. 직접 작성하는 길이고, 보통은 도우미 명령을 쓴다. hint가 이것을 가리킬 때 쓴다 |
| `ait contract add <경로>…` · `ait contract commit -m <why>` | 계약 파일(`.ait/`의 레코드와 명세 파일)을 직접 고쳐 커밋한다. 주로 hint의 복원을 마칠 때 쓴다 |

- 한 명령에 절(clause)을 여럿 이을 수 있다(`ait future amend D… future amend O… -m …`). 기록하는 명령은 모두 `-m "이유"`가 필요하다.
- `future`를 붙이면 약속(아직 성립하지 않음)이고, 확인할 것이 없다. 붙이지 않으면 지금 HEAD의 코드로 확인해야 한다.
- 인자 자리에 `-`를 쓰면 그 값을 그대로 둔다(`ait future amend K - - <새 명세 정의>`).

## 아직 없는 것

없는 명령은 `fatal: not supported: '…'`로 멈춘다.

| 없는 것 | 대신 |
| --- | --- |
| `ait peek` | 계약 하나는 `ait todo <ID>`, 범위의 future·미확인 계약은 `ait todo <범위>`. 범위의 current 계약은 레코드를 직접 본다(아래 "current 계약의 코드를 고친다") |
| `ait listen` · `sub` · `unsub` (알림) | 없다. 남의 계약 커밋을 찾아 보고 받는다(아래 "남의 계약을 받는다") |
| `ait contract push` | 한 clone의 worktree들은 브랜치를 함께 보므로 필요 없다. 다른 clone과는 `git fetch`·`git push`. pre-push hook이 보내는 커밋의 레코드를 검증한다 |
| `ait future drop` (current 계약 철회) | 먼저 되돌리고(`ait future amend K -m …`), 다음 요청으로 거둔다(`ait future cancel K -m …`) |
| 명세 정의의 `environment` | 쓰지 않는다 |
| merge driver | git의 기본 병합 뒤 hook이 계약 파일을 정리한다(아래 "합칠 때") |

## 원칙

1. **코드에는 주인이 없다.** 요구는 누구에게 보내는 것이 아니라 코드에 거는 것이다. 누가 할지는 착수할 때 정해진다.
2. **착수할 때 떠맡는다.** 손댈 때 `undertake`로 답한다. 미리 잡아 두지 않는다. 그만두면 바로 거둔다. 잡고만 있으면 남이 그 약속을 기다리므로 병목이다.
3. **착수할 때의 최신 판으로 개발한다.** 착수 전에 남의 계약을 받아 그 판으로 일한다.
4. **기다리지 않는다.** 요구를 건 쪽은 그 약속(future)을 기준으로 자기 일을 계속한다. 아무도 맡지 않았는데 내게 필요해지면 내가 떠맡는다.
5. **누구 몫인지는 기록이 말한다.** 조건은 계약을 만든 쪽, 구현은 떠맡은 쪽, 확인은 `-r`(또는 `--force`)로 커밋한 쪽의 것이다. 남의 계약도 되돌리고 떠맡고 이행할 수 있지만, 거두거나(cancel) 놓지(release) 않는다. 그것은 만든 쪽이 자기 계약에 하는 일이다.
6. **바뀐 상태를 기준으로 일한다.** 내가 건 계약이 고쳐지거나 거둬지거나 약속으로 되돌려졌어도, 지금 있는 계약대로 일한다.
7. **경고가 나오면 hint를 바로 따른다.** 확인된 계약의 코드를 확인 없이 바꾸면 경고와 hint가 나온다. 그 hint대로 다시 확인하거나 약속으로 되돌린다.
8. **HEAD의 오류가 먼저다.** HEAD에 오류(아래 "HEAD의 오류")가 있으면 도우미·fulfill·확인할 것이 있는 commit·pull은 다른 무엇보다 먼저 그 오류로 멈춘다. git이 unmerged 파일이 있으면 commit·merge·pull을 막는 것과 같다. hint의 복원을 마치고 다시 실행한다.

## 낱말

- **demand**: "저 코드(provider)가 이 조건을 충족해야 한다. 내가 쓰는 코드(consumer)가 거기 기댄다."
- **offer**: "이 코드(provider)가 이 조건을 충족한다(하게 하겠다)."
- **undertake**: offer가 demand를 떠맡는다. offer가 이행되면 떠맡은 demand도 함께 이행된다.
- **범위**: `a.c`(파일), `a.c:10-40`(행), `src/`(디렉터리), `/`(전체). 여럿이면 `,`로 잇는다. 아직 없는 파일은 `new:a.c`. 어디에 구현될지 모르면 `/`나 `src/`로 쓰고(**추상 범위**, future의 provider에만), 이행할 때 좁힌다. 디렉터리 범위는 HEAD에 그 디렉터리가 있을 때만 쓴다. 경로는 언제나 저장소 최상위 기준이다. 행 번호는 HEAD 기준이고, `fulfill`·`ait commit`의 `ID=범위`만 stage한 판 기준이다. 행 범위는 경계에 삽입하면 이웃 범위의 계약을 놓칠 수 있으므로 보통은 파일 단위로 건다.
- **명세 정의**(YAML): 계약이 충족할 조건(`body`)과 확인 방법. 명세 정의와 그것이 선언한 검사 재료(`inputs`)를 **명세 파일**이라 한다. 명세 파일은 작업 폴더의 일반 파일이어야 한다(심볼릭 링크나 링크 너머의 파일은 없는 것으로 본다).
- **future**(약속)는 아직 이행되지 않은 계약, **current**는 확인된 계약이다. current는 언제나 믿을 수 있어야 한다.
- **고정**: 계약이 범위와 명세 파일의 그때 내용을 적어 두는 것. 확인하며 커밋하면 지금 내용으로 다시 고정된다.
- **걸린 계약**: 내 변경이 current 계약의 provider 코드에 닿으면 그 계약은 내 변경에 걸린다. future와 consumer는 걸리지 않는다.
- **미확인**: current 계약의 코드가 확인 없이 바뀐 것이다. `ait todo`에 `unverified yes`로 나온다. 다시 확인하거나 확인한 내용으로 코드를 복원하면 없어진다.
- **사고**: 계약 파일이 ait의 규칙 밖에서 바뀐 것이다. 확인 행이 고정과 맞지 않는 current, 끝난 계약을 손으로 되살린 판 같은 것이다. 미확인과 다르다.
- **HEAD의 오류**: HEAD의 계약 파일에 있는 잘못. 셋이다. 구조 오류(형식이 틀린 레코드, 허용되지 않는 상태의 조합), 사고, 깨진 묶음(명세 파일이 고정과 다르다)이다. ait 밖의 커밋(`git commit --no-verify` 등)만 이것을 만든다. 아래 "HEAD의 오류"를 따른다.
- **되돌리기**(`ait future amend K`): current 계약을 다시 약속으로 만든다. 그동안의 코드 변경은 사고가 아니고, 다시 이행할 때 확인한다. 되돌린 계약은 되돌린 쪽의 약속이 된다. current offer가 떠맡은 current 요구는 그 offer와 한 요청에서 함께 되돌린다(hint가 함께 되돌리는 명령을 준다).
- **`--force`**: 확인 결과 대신 "사소하다고 판단하고 책임진다"는 확인. `forced`로 기록된다.
- `as x`로 붙인 별명은 그 명령 안에서만 쓴다. 다른 명령에는 출력된 ID(`new D… x`의 `D…`)를 쓴다.

## 명세 정의

`ait contract new spec specs/x.yaml exec`(명령으로 확인할 수 없으면 `prompt`)로 틀을 받아 채운다. 키는 아래뿐이고, 다른 키는 형식 오류다.

```yaml
body: 재고가 0인 상품의 예약 요청은 거절한다.   # 조건. 필수
engine: exec                                  # 명령으로 확인. 빼면 prompt(코드를 읽고 판단)
command: [sh, tests/no_oversell.sh]           # exec면 필수. 셸 없이 실행하는 argv
success: [0]                                  # 통과로 볼 exit 코드. 빼면 [0]
inputs: [tests/no_oversell.sh]                # 함께 고정할 검사 재료. 검사받는 코드는 넣지 않는다
```

- 파일을 쓴 뒤 도우미 명령에 그 경로를 주면, 명령이 그 파일을 계약과 함께 커밋한다.
- 이미 건 계약의 명세 파일 내용을 바꾸려면, 파일을 고치고 `ait amend <K> -m "왜"`(current면 확인 결과가 필요하다)나 `ait future amend <K> -m "왜"`를 실행한다.
- 레코드가 선언한 정의 파일이 없거나 형식이 틀리면(읽을 수 없으면) 그 정의를 읽는 명령은 todo와 hook까지 그 오류로 멈춘다. 정의를 고쳐 `ait contract add <정의>`·`ait contract commit -m "왜"`로 다시 고정한다.
- 정의가 `inputs`에 선언한 파일이 없으면 그 계약은 고정할 수 없다(`missing <경로>`). 그 파일을 만들거나 정의에서 뺀다.
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
5. 같은 명령에 `-r <check-result 경로>`를 붙여 다시 실행한다. 모두 pass여야 커밋되고, 확인은 레코드의 확인 행으로, 결과는 커밋 메시지의 `Ait-Check:` 행으로 남는다.
   - fail이면 hint가 고칠 명령을 지금 상태에서 그대로 되는 순서로 준다. 그 순서를 따른다. 코드를 고쳤으면 다시 stage하고 `-r` 없이 처음부터 한다(과제의 `input`이 바뀐다).
6. 주석·오타처럼 하찮은 변경이면 `-r` 대신 `--force`.

## HEAD의 오류

누가 `git commit --no-verify`로 계약 파일이나 명세 파일을 ait 밖에서 바꾸면, 그 커밋 뒤 post-commit hook이 오류마다 `warning:`과 고치는 hint를 쓴다. 그 뒤로는 고칠 때까지:

- 도우미·`fulfill`·확인할 것이 있는 `ait commit`·`contract pull`은 무엇을 하려 했든 먼저 HEAD의 첫 오류로 멈춘다. hint는 그 오류의 복원과 `then 'ait contract commit -m <why>'`, `then rerun this command`다.
  ```
  error: 'D…' at HEAD was changed outside ait's rules
  hint: restore '.ait/D…' as in '<커밋>', then 'ait contract commit -m <why>', then rerun this command
  ```
  그대로 한다: `git checkout <커밋> -- .ait/D…`, `ait contract commit -m "사고 판 복원"`, 그리고 원래 명령을 다시 실행한다. 오류가 여럿이면 다시 실행할 때 다음 오류의 hint가 나온다.
- 확인할 것이 없는 `ait commit`(걸린 계약이 없는 코드 커밋)과 `git commit`은 막히지 않는다.
- `ait todo`는 HEAD의 오류를 모두 경고와 hint로 보여 주고, 고칠 때까지 블록의 hint는 쓰지 않는다(그 명령들이 먼저 멈추기 때문이다).
- hint가 없으면 ait 명령으로는 고칠 수 없는 것이다. 보통 명세 정의가 없는 파일을 `inputs`에 선언한 경우다(관문이 그 계약을 다시 고정할 수 없다). 그 파일을 만들거나 정의에서 빼고(위 "명세 정의"), `ait contract add <정의>`·`ait contract commit -m "왜"`.
- 남의 브랜치의 사고 판은 `contract pull`이 건너뛰고 경고한다. 그 브랜치의 주인이 고친다.

## 일의 흐름

**시작·재시작**
- `ait init`을 한다. hook이 없거나 ait 명령이 `this clone is not set up for ait`를 쓰면 다시 한다. 저장소가 이미 hook을 쓰면 ait는 덮어쓰지 않고 넣을 행을 hint로 준다.
- 에이전트마다 worktree와 브랜치를 따로 둔다: `git worktree add ../<내 이름> -b <내 이름>` 하고 그 디렉터리로 간다. 다른 에이전트가 쓰는 worktree에서 session을 열면 경고와 hint가 나온다.
- 내 이름(agent)은 내가 맡은 작업처럼, 다른 에이전트와 겹치지 않고 재시작해도 같은 것으로 정한다.
- `ait session list`에서 `agent`가 내 이름이고 `worktree`가 지금 worktree인 session이 있으면 그것을 쓴다(재시작). 없으면 `ait session open --agent <내 이름>`. 출력의 hint를 따른다.
- `ait todo`로 HEAD의 오류, 내 약속(future), 내 요구 중 확인 없이 바뀐 것을 본다. HEAD의 오류가 있으면 그것부터 고친다. 블록마다 hint가 다음 할 일을 준다.

**착수한다** (코드에 손대기 직전)
1. 남의 계약 커밋을 보고, 받을 것을 받는다(아래 "남의 계약을 받는다").
2. `ait todo <작업할 범위>`로 그 범위의 계약을 본다. 아무도 맡지 않은 future demand(`undertaken-by` 행이 없는 것)는 내가 떠맡을 것이다. 이미 맡은 쪽이 있으면 그 약속을 기준으로 개발한다.
3. 떠맡는다. 여럿이면 한 명령으로:
   `ait future offer <구현할 범위> <명세 정의> as o undertake D1… o undertake D2… o -m "착수"`
   - 명세 정의는 요구의 것을 그대로 써도 된다. 더 보장할 것이 있으면 더한다.
   - 내가 건 요구도 같다. 내가 하기로 하면 내가 떠맡는다.
   - 떠맡을 요구에 HEAD에 없는 변경이 있으면 `has changes not in HEAD` 경고와 받는 명령(hint)이 나온다. 받은 뒤 다시 한다.
- 계약 커밋은 그 브랜치의 코드 커밋보다 먼저 한다. `contract pull`은 계약이 처음 들어간 커밋과 그 조상을 받으므로, 그래야 남이 내 계약을 받을 때 아직 이행하지 않은 내 코드가 함께 가지 않는다.
- 맡을 요구가 없어도, 남이 기댈 것을 새로 만들면 착수할 때 `future offer`로 약속한다.

**개발 중**
- 남의 약속(future)을 기준으로 개발한다. 아직 구현되지 않았어도 그 조건대로 있다고 보고 쓴다.
- 커밋은 `git add` 뒤 `ait commit -m "…"`으로 한다. 걸린 계약이 없으면 `git commit`과 같은 커밋이고, 있으면 그 자리에서 과제를 준다. ait commit은 git의 커밋 hook(lint 등)을 거치지 않으니 따로 돌린다.
- 기대는 계약이 바뀌었는지는 계약 커밋을 보고 받은 뒤 `ait todo <그 ID>`로 본다.

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
- `git commit`으로 커밋해 `warning: this commit leaves N current contract(s) unverified`가 나오면 사고다. 경고에 나온 계약 **모두**를 hint대로 다시 확인하거나 되돌린다. 여럿은 한 요청에 절로 이어도 된다(`ait amend D… amend O… -m …`). 다시 확인하는 명령이 결과를 요구하면, 정말 사소한 변경일 때 그 hint대로 `--force`를 붙인다.
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
1. 남의 계약 커밋을 보고, 받을 것을 받는다.
2. 구현을 stage한 뒤 `ait fulfill O… -m "무엇을 했나"`. 보고와 hint를 따른다.
   - `ready no`면 보고의 `mixed`(명세 파일이 섞였다), `invalid`, `code-required`를 hint대로 고친다.
   - `ready yes`면 위의 "확인 결과를 내는 법"대로 확인해 `-r`로 다시 실행한다.
   - `touch` 행은 내 변경에 걸린 계약이고, 과제에 들어 있다.
3. 추상 계약은 구현한 코드로 좁혀 이행한다(`ait fulfill O…=src/runner.c,src/cli.c D…=src/runner.c -r <결과> -m "…"`).
4. 다 된 일은 저장소의 통합 브랜치(master 등)에 합친다(아래 "합칠 때"). 다른 clone으로 보낼 때는 `git push`이고, pre-push hook이 보내는 커밋의 레코드를 검증한다.

**그만둔다**
- 떠맡은 offer를 거둔다(`ait future cancel O… -m "중단: 남은 일, 부분 구현이 있는 브랜치"`). 요구는 그대로 남아 누구든 다시 떠맡는다.
- 그 offer로 다른 것도 계속 약속한다면 요구 하나만 놓는다(`ait release D… O… -m "이유"`).
- 리팩터링을 그만두면 되돌린 계약은 내 약속으로 남는다. 코드가 확인한 그대로면 다시 이행해 두고 떠난다.
- 내가 건 요구가 더는 필요 없으면: future면 `ait future cancel D… -m "이유"`. current면 먼저 `ait future amend D… -m "이유"`, 다음 요청으로 `ait future cancel D… -m "이유"`.
- 이 worktree에서 일을 완전히 끝내면 `ait session close`.

## 남의 계약을 받는다

한 clone의 worktree들은 브랜치를 함께 본다. 남이 자기 브랜치에 계약을 커밋하면 push 없이 내 worktree에서도 보인다. 착수 전, 이행 전, 손이 빌 때 본다.

```sh
git log --branches --format='%h %D %s' -- .ait/    # 모든 브랜치의 계약 커밋. 다른 clone이면 git fetch 뒤 --branches --remotes
git show --stat --format=%s <계약 커밋> -- .ait/    # 그 커밋이 바꾼 계약의 ID(.ait/<ID>)
ait contract pull <ID>…                           # 그 계약의 받을 판이 처음 들어간 커밋과 그 조상을 받는다
ait todo <ID>
```

- 출력: 받은 계약마다 `included <ID> from '<커밋>'`, 이미 있으면 `already <ID>`.
- 받을 이력에 그 계약 말고 남의 코드 커밋이 있으면 그것도 함께 온다. 그래서 내 계약 커밋은 코드 커밋보다 먼저 한다.
- 멈추면 `error:` 행이 무엇이 왜 안 되는지, `hint:`가 따를 명령을 준다. 그대로 따른다.
  - 지정(`also changes contracts you did not name`): 받을 이력이 다른 계약도 바꾼다. hint대로 그 ID를 더한다.
  - 갈라짐(`has diverged between …`): 같은 계약을 두 쪽이 따로 바꿨다. hint의 선택 중 하나를 고른다(`<ID>=<커밋>`이나 내 판 `<ID>=HEAD`, `-m "왜"`). 한쪽이 거둔(cancel) 계약이면 고르지 않고 끝난 쪽을 받는다(경고가 나온다).
  - 확인(`changes what contract … checked` 등): 받으면 current 계약이 확인한 코드가 바뀐다. hint대로 같은 요청에서 되돌리거나(`… future amend K -m "왜"`), 정말 사소하면 `--force -m "왜"`로 책임지고 받는다.
  - 같은 파일 충돌(`both change …`): hint의 차례대로 나눠 받는다.
  - 사고(`was changed outside ait's rules`): 그 브랜치를 고칠 때까지 그 판은 받을 수 없다. 남의 브랜치면 그쪽이 고친다.
  - 로컬 변경(`would be overwritten by this pull`): hint대로 커밋하거나 치운 뒤 다시 실행한다.
- 코드가 충돌하면 git처럼 멈춘다. 충돌을 풀고 stage한 뒤, hint의 마치는 커밋(`git commit --allow-empty --no-edit --cleanup=strip`)을 한다.
- 받은 계약이 미확인으로 들어오면, 그것이 내 약속이나 내 요구일 때만 `warning: '<ID>' comes in unverified`와 다시 확인하는 hint가 나온다. hint대로 한다.

## 합칠 때 (git merge·rebase·cherry-pick·revert)

- 다른 브랜치를 `git merge`로 합치면, hook이 합친 계약 파일을 정리한다. 명세 파일을 다시 고정하고, 확인 없이 고정이 바뀐 current 계약은 약속으로 되돌린다(`warning: '<ID>' is reopened in this merge`). 합친 코드가 확인된 것과 다르면 미확인이 된다(`is unverified after this merge`, `comes in unverified`). 모두 hint가 따른다.
- `.ait/<ID>`나 명세 파일이 충돌하면 한쪽 판을 골라(`git checkout --ours|--theirs -- .ait/<ID>`, 명세 파일은 손으로 합친다) stage하고, 마치는 커밋 `git commit --allow-empty --no-edit --cleanup=strip`으로 마친다(rebase면 `git commit --allow-empty --allow-empty-message -C REBASE_HEAD`). git의 `--continue`만 하면 hook이 돌지 않을 수 있으니 먼저 마치는 커밋을 하고 git의 안내대로 잇는다.
- hook이 결과를 거절하면(`not allowed: …`, `bad format: …`, `spec files do not match …`) hint가 `restore '…' as in '<쪽>' and stage the result, then run '<마치는 커밋>'`를 준다. 그대로 한 뒤 마치는 커밋을 다시 한다. 레코드는 지울 수 없다(`records are never removed`).
- `warning: '.ait/<ID>' keeps an error from HEAD`는 합치기 전부터 HEAD에 있던 오류다. 합치기는 막지 않는다. 합친 뒤 위 "HEAD의 오류"대로 고친다.
- 다른 브랜치의 사고 판·형식이 틀린 판은 출발 판에서 빠지고 `warning: skipping …`이 나온다.
