# ait 1단계 규격 (확정본)

판: v15 · 대상: C 구현 · 기준일: 2026-10-10 (v14에 `contract pull`의 개정을 더했다. 근거는 `drafts/pull-criteria.md` 개정 9와 `drafts/pull-decisions.md`의 결정 1~37)

이 규격은 1단계 구현의 유일한 규범이다. 절 번호는 `ait-phase1-scenarios.md`가 참조한다. 컨셉과 사용 이유는 `ait-concept.md`에 있다.

이 규격은 아래 **전제**와 **규칙**에서 시작한다. 각 절은 규칙을 구현하는 방법이거나, 규칙에서 따라 나오는 결론이다. 결론은 "그래서"로 적는다. 규칙에 없는 예외는 두지 않는다. 어떤 동작이 규칙에서 나오지 않으면 그 동작이 틀린 것이다.

## 전제 (에이전트에 대한 가정)

- **전제 1 — 코드에는 주인이 없다.** 수많은 에이전트가 한 코드베이스를 동시에 고친다. 누구든 어느 계약이든 만들고, 바꾸고, 되돌리고, 떠맡고, 이행할 수 있다. 권한 개념은 없다.
- **전제 2 — 에이전트는 유한한 시간 안에, 자기에게 주어진 한정된 문맥만으로, 명세를 충족하는 구현을 해낸다. 다만 이행하기 전까지는 실수할 수 있다.**

전제의 구절마다 다음이 나온다.

| 구절 | 그래서 |
| --- | --- |
| 권한이 없다(전제 1) | 누가 무엇을 했는지를 남긴다(규칙 1). 막는 대신 드러낸다(규칙 9). 누구도 자기 판을 고집할 수 없으므로, 남이 바꾼 계약은 바뀐 상태를 기준으로 일한다 |
| 여럿이 동시에 고친다(전제 1) | 변경이 한 계약에서 만난다. 그 계약을 받는 쪽에 알린다(규칙 6). 상태는 정해진 전이로만 바뀌므로 합친 결과도 그 전이로 판정하고(규칙 2), 판정할 수 없는 동시 수정은 pull에서는 고르기로, git merge에서는 충돌로 드러난다(7.12, 7.13) |
| 여럿이 동시에 고친다(전제 1) | 요구와 그것을 해낼 약속이 서로 다른 에이전트에게서 따로 생긴다. 둘을 잇는 떠맡기가 필요하고, 떠맡은 약속은 요구를 포함한다(규칙 5) |
| 명세를 충족하는 구현을 해낸다 | 이행을 믿는다. 확인 결과나 `--force`의 판단이 곧 확인이고(규칙 4), ait는 결과를 다시 검사하지 않고 기록한다(0.4a). 그래서 내가 건 요구가 이행되면, 그 이행과 구현을 받아 이어서 개발한다(10.1b) |
| 유한한 시간 안에 | 약속은 이행되거나 거둬진다. 그래서 요구를 건 쪽은 답을 기다리지 않고 약속을 기준으로 개발한다. 하지 않는 일을 잡아 두면 남이 그 약속을 기준으로 기다리므로, 착수할 때 떠맡고 하지 않을 일은 거둔다(10.1a) |
| 한정된 문맥만으로 | 남의 변경을 스스로 알 수 없다. 그래서 내 약속·요구·구독에 닿는 변경을 알리고(규칙 6), hint로 그것을 보는 쪽이 자기 문맥에서 실행할 다음 명령을 준다(규칙 7) |
| 한정된 문맥만으로 | 남이 만든 계약이 왜 있는지, 어떤 코드가 그 계약에 기대는지는 만든 쪽만 안다. 그래서 hint는 남이 만든 계약을 cancel·drop하는 명령도, 남이 만든 offer의 undertake를 release하는 명령도 주지 않는다. 그것은 만든 쪽이 정한다(규칙 7). 되돌리기(`ait future amend K`)는 계약을 없애지 않고 state만 future로 바꾸므로, 남이 만든 계약이어도 누구든 한다(1.5d) |
| 한정된 문맥만으로 | 남의 브랜치에 있는 것을 다 읽을 수 없다. 그래서 받을 계약을 이름으로 지정하고, 그 계약의 판이 처음 들어간 커밋까지만 이력째 받는다. 그 이력이 바꾸는 계약은 모두 지정해야 받는다. 어떤 코드가 필요한지는 가려낼 수 없으므로 코드는 지정 없이 함께 들어온다(규칙 8, 10.1b) |
| 한정된 문맥만으로 | 한정된 문맥에서 한 구현이므로 이행한 뒤에도 코드는 다시 고친다(수정·리팩터링). current 계약을 약속으로 되돌려 다시 이행하는 것은 정상 경로다(1.5d) |
| 이행하기 전까지는 실수할 수 있다 | 중간 커밋은 조건을 어겨도 된다. 커밋 hook은 결과를 요구하지 않는다(7.1.2) |
| 이행하기 전까지는 실수할 수 있다 | 확인된 것을 확인 없이 바꾼 실수는 막지 않고 미확인으로 드러낸다. 그 커밋을 만든 쪽과 받는 쪽에 알린다(규칙 4, 6, 9) |

## 규칙

- **규칙 1 — 사실만 기록하고, 판정은 계산한다.** 저장하는 것은 누가 무엇을 했다는 사실뿐이다: 계약 레코드와 연결(`.ait/`. current 계약의 확인 행이 무엇을 어떻게 확인했는지를 담는다), 명세 파일, 커밋 기록줄(누가 요청했나), session의 이름, 명시한 구독, 전달 기록(4.2·4.3). 미확인, 걸린 계약, 받는 쪽, 다음 명령 같은 판정은 이 사실에서 그때 계산하고 따로 저장하지 않는다. 계약을 바꾸는 일은 모두 커밋 하나이고 이력에 남는다(5.4, 7.9.5).
- **규칙 2 — 상태는 정해진 전이로만 바뀐다.** 계약의 state와 연결은 2.3의 전이로만 바뀐다. 이것은 데이터로 판정한다: 판은 사고 판이 아닌 `prev` 중 하나에서 이 전이로 갈 수 있어야 하고, 끝난 판을 포함하는 판은 그 끝난 판이어야 한다(2.3.3). 어떤 git 조작으로 그 판이 생겼는지는 보지 않는다. 통합(7.1.3)에서도 각 레코드는 출발 판 중 하나(또는 없음)에서 이 전이로 갈 수 있는 판이다.
- **규칙 3 — 조건과 코드는 한 커밋에서 함께 새로 바뀌지 않는다.** 조건을 정하는 일과 그 조건으로 코드를 확인하는 일을 커밋으로 나눠야, 커밋마다 무엇을 정했고 무엇을 확인했는지가 하나로 남는다(규칙 1). 조건은 명세 파일이고, 코드는 계약 경로(`.ait/` 아래 경로와 명세 파일, 6.1) 밖의 경로다. 명세 파일은 계약을 따라서만 바뀌고, 그 내용은 계약에 고정된 것과 같다(2.4). 조건을 정하는 커밋(관문)에는 코드가 없고, 코드를 바꾸는 커밋(이행·commit)에는 명세 파일이 없다. 레코드는 그것이 기록하는 일과 같은 커밋에 든다: 관문의 레코드는 조건과 함께, 이행·확인의 기록(state와 고정)은 확인한 코드와 함께다(6.1, 7.7.3-2). 통합 커밋은 부모들이 따로 남긴 변경을 합치므로 둘을 함께 담는다. 합치며 current 계약의 고정이 확인 없이 바뀌면 그 계약(과 그것을 떠맡은 current O)을 약속으로 되돌린다(규칙 4·5, 7.1.3-3). `contract pull`은 되돌리지 않고 받기 전에 멈춘다(7.12).
- **규칙 4 — current는 확인된 것이다.** ait 명령은 계약을 current로 만들거나 current의 고정을 바꿀 때 확인과 함께 한다. 확인은 확인 결과나 `--force`의 판단(사소하다고 책임진다)이고, 계약 레코드의 확인 행(`check reported|forced PINS`)으로 남는다. current의 코드나 조건을 확인 없이 바꾸려면 먼저 그 계약을 future로 되돌린다(`ait future amend`). 확인 행이 맞는데 provider 코드가 고정과 다른 current는 **미확인**이다. 확인 행이 없거나 맞지 않는 current와 고정과 다른 명세 파일은 **사고**다(1.5a, 2.3.3). ait 명령이 직접 쓰는 커밋은 미확인을 만들지 않으므로, 미확인은 그 밖의 커밋에서만 생긴다(1.5a). 막지 않고 드러낸다(1.5d, 8장).
- **규칙 5 — 떠맡으면 포함한다.** offer O가 demand D를 떠맡으면 O는 D를 포함하는 약속이다. 그래서 O의 이행은 future인 D를 함께 이행하고 그 커밋 뒤에 current인 D의 조건까지 확인하며(1.5b), D는 O 없이 먼저 이행될 수 있다(1.6, 7.7).
- **규칙 6 — 위험에 놓인 쪽에만 알린다.** 계약의 변화에 대한 경고와 알림은 위험만, 그 위험에 놓인 쪽에만 보낸다. 위험은 따로 한 변경들이 한 계약에서 만나는 것(전제 1)과, 이행 전의 실수가 확인된 것을 바꾼 것(전제 2)이다. 위험에 놓인 쪽(**받는 쪽**)은 그 계약이 **내 약속**이나 그 상대(1.3)이거나 **내 요구**(내 current demand)이거나 **내 구독**인 쪽이다. 무엇이 일어났는지는 그 사건의 커밋으로, 그것이 나와 무슨 관계인지는 내 HEAD로 판정한다. 뜻이 바뀌지 않은 변화(다시 고정)는 위험이 아니다. 내 명령이 만나거나 만든 위험(HEAD에 없는 변경, 미확인, 통합과 pull의 되돌리기)은 그 명령의 경고로 나에게 알린다(6.7a, 7.1.2, 7.1.5, 7.12).
- **규칙 7 — 판정을 쓰고, hint가 다음 명령을 준다.** 에이전트는 한정된 문맥에서 일한다(전제 2). 그래서 ait가 쓰는 오류·경고·알림·보고는 git 형식이고, 판정 결과를 그대로 쓴다. 이어서 그것을 **보는 쪽**(알림이면 받는 쪽, 명령이면 실행한 쪽)이 자기 HEAD에서 실행할 다음 명령을 `hint:`로 준다. hint는 쓰는 순간의 상태로 판정한다. hint는 남이 만든 계약을 cancel·drop하는 명령, 남이 만든 offer의 undertake를 release하는 명령, 남이 바꾼 계약을 옛 판으로 복원하는 명령을 주지 않는다. pull의 고르기 hint에서 내 판을 고르는 `K=HEAD`는 복원이 아니다. 결과 판이 남의 판을 `prev`에 담아 그 바뀐 것을 안고 가기 때문이다(7.12). 따로 읽어야 할 해석표가 필요하면 그 오류·경고·알림·보고가 모자란 것이다(5.6, 5.9a).
- **규칙 8 — `contract pull`은 지정한 계약을 이력째 받는다.** `contract pull K…`는 K의 받을 판이 처음 들어간 커밋과 그 조상을 받는다(git merge처럼). 그 이력이 바꾸는 계약은 모두 지정해야 받고, 코드는 지정 없이 함께 들어온다. 판이 갈라졌으면 받는 쪽이 고른다. 계약 파일은 pull이 정하고 git merge에 맡기지 않는다. `contract push`는 내 브랜치를 그대로 올린다(7.10, 7.12).
- **규칙 9 — 막는 것은 아래 목록뿐이다.** 나머지는 모두 되고, 규칙 1에 따라 기록에 남는다.

| 막는 것 | 근거 규칙 | 어디서 |
| --- | --- | --- |
| 계약 레코드의 형식과 구조(2.2, 5.9) | 1·2·5 | 관문, fulfill, 확인할 것이 있는 commit, 통합 hook, push hook |
| 정해지지 않은 상태 전이(2.3). 관문을 거치지 않은 레코드 변경(일반 커밋의 `.ait/`). 통합에서 출발 판에서 갈 수 없는 레코드(끝난 출발 판이 있으면 끝난 판에서만, 7.1.3-4) | 2 | 관문, fulfill, 확인할 것이 있는 commit, 일반 pre-commit, 통합 hook |
| 고정과 다른 명세 파일(2.4). 조건과 코드가 섞인 커밋 | 3 | 관문, fulfill, commit, 일반 pre-commit, push hook |
| 확인 없는 이행·current 등록·고정 변경(`--force`는 판단으로 확인하고 기록한다) | 4 | 관문, fulfill, 확인할 것이 있는 commit |
| 계약 커밋이 없는 push, 내보낼 미확인이 있는 push(`--force`는 그 계약을 사소하다고 확인해 기록하고 올린다) | 8 | contract push |
| 갈라졌는데 고르지 않은 pull, 지정하지 않은 계약을 바꾸는 pull, 받는 결과가 구조 검증(5.9)을 지나지 못하는 pull, 같은 파일이 두 번 충돌하는 pull, 후보가 모두 사고 판이나 형식이 틀린 판인 pull, 새 미확인이나 확인 없는 명세 변경을 만드는 pull(`--force`는 판단으로 확인해 2.2.2의 확인 행으로 기록한다) | 2·4·8 | contract pull |

그래서 다음은 모두 된다.
- 브랜치, merge, rebase, reset, cherry-pick 같은 모든 git 조작. hook을 끄거나 `--no-verify`로 커밋하기. `git push`·`git fetch`·`git pull`로 주고받기(이때 검사는 push hook의 validate와 통합 hook뿐이다).
- 어느 에이전트든 어느 계약이든 고치고, 되돌리고, 떠맡고, 이행하기(전제 1).
- 한 파일을 여러 계약이 명세 파일로 함께 쓰기. 한 demand를 여러 offer가 떠맡거나, 한 계약을 여러 drop이 겨누기.
- current 계약의 코드를 되돌리지 않고 바꾸기. 그 계약은 미확인이 되고 받는 쪽에 알림이 간다(규칙 4, 6).
- 확인 결과의 내용. ait는 과제마다 pass가 근거와 함께 보고됐는지만 보고, 결과를 커밋 메시지에 남긴다.

ait 명령 자신의 입력 검사(문법, 범위, session)와 전제조건(5.2)은 그 명령이 안전하게 돌기 위한 것이다. git 조작에는 영향을 주지 않는다.

## 0. 목적

ait는 여러 에이전트가 코드 범위에 **계약을 요구하고(demand), 떠맡고(undertake), 고치고(amend), 거두고(cancel·drop), 보장하게(offer)** 하는 git 확장이다(전제 1). 요구는 코드에 걸고, 그 일에 착수하는 에이전트가 그때 떠맡는다(10.1a). 에이전트는 아직 이행되지 않은 약속(future)을 기준으로 개발하고, **약속의 이행(fulfill)을 구현·명세 파일과 함께 버전 관리**한다. 요구가 이행되면, 요구를 건 쪽은 그 이행과 구현을 받아 이어서 개발한다(10.1b). 계약이 바뀌거나 확인된 코드가 확인 없이 바뀌면 위험에 놓인 에이전트에게 알리며, 다른 코드베이스에도 알린다.

- 0.1 **계약**은 에이전트가 코드 범위에 대해 거는 요구(demand)나 보장(offer), 그리고 그것을 철회하겠다는 철회 예약(drop)이다. 레코드는 `.ait/<ID>`이고, 요구와 보장은 **명세 파일**을 가리킨다. 명세 파일은 조건과 확인 방법을 적은 **명세 정의**(YAML)와 그 정의가 선언한 검사 재료(`inputs`)·환경 정의 파일이다(1.2, 2.5).
- 0.2 원본은 저장소 tree다. 계약은 `.ait/`에 두고, 명세 파일은 사용자가 정한 경로에 둔다.
- 0.3 계약의 조건과 범위를 정하는 ait 명령은 모두 **하나의 관문(6장)**을 지나 커밋된다. 확인한 코드의 위치(이행·확인에서 옮긴 범위)는 그 코드와 함께 fulfill·commit이 기록한다(규칙 3). 입구는 둘이다.
  - 도우미 명령: `demand`·`offer`·`future`·`amend`·`undertake`·`release`. 실행하면 자동으로 커밋된다.
  - 직접 작성: `contract new` → 파일 작성 → `contract add` → `contract commit`. `ait contract …`는 `ait ct …`로 줄여 쓸 수 있다.
- 0.4 약속의 이행은 `fulfill`로 한다. 이행 커밋에는 stage된 구현, 계약의 state와 확인 행, 확인 결과와 이행 주체(기록줄)가 함께 들어간다. 이행할 약속 없이 코드를 커밋하면서 걸린 계약(1.5b)을 함께 확인하려면 `ait commit`을 쓴다(7.7a).
- 0.4a **확인**: 1단계에서 ait는 명세 정의를 실행하지 않는다. 확인할 과제를 주석으로 주고(8.2), 확인한 에이전트가 낸 결과(`-r FILE`)를 받아 커밋 메시지에 남긴다. 결과가 참인지 보장하지 않는다. 실행은 2단계다(12장).
- 0.5 운반과 코드의 병합·충돌 해결은 git이 맡는다. 계약 파일은 `contract pull`이 정하고, 명세 파일은 pull이 받지 않을 변경만 정해 빼면 git이 코드와 함께 합친다(7.12). ait 명령이 아닌 하위 명령은 git에 그대로 넘긴다.
- 0.6 버전은 git 객체 ID다. 저장과 출력은 행 기반 텍스트다.

## 1. 용어와 판정

- 1.1 **계약**: 코드 범위에 대한 요구(D)나 보장(O), 또는 current 계약 하나의 철회 예약(X, 1.4). 만들고 바꾸는 행위자는 언제나 에이전트다(creator, 커밋 기록줄). 범위는 저장소의 경로(`.ait/` 밖, 3.1)다. 요구는 코드에 걸지 에이전트에게 보내지 않는다(10.1a). 범위와 명세 파일을 가지는 것은 D·O이고, X는 target을 가리킨다.
  - demand D: "범위 B의 코드가 이 명세 파일들을 충족해야 한다"는 요구다. 범위 A는 B에 기대는 코드다. 레코드에서 A는 `consumer`, B는 `provider`다.
  - offer O: "범위 B의 코드가 이 명세 파일들을 충족한다(충족하게 하겠다)"는 보장이다. B는 `provider`다.
- 1.2 **명세 파일**: 계약이 무엇을 충족해야 하는지 적은 파일이다. 레코드의 `file` 행이 고정한다.
  - **명세 정의**(YAML, 2.5): 조건(`body`)과 확인 방법이다. 모든 정의가 pass여야 계약이 통과한다. `engine: exec`이면 `command`를 돌려 확인하고, 생략하면 **prompt**로 코드를 읽고 판단한다.
  - 정의가 `inputs`와 `environment`로 선언한 파일(검사 재료와 환경 정의)도 명세 파일이다. 관문이 정의와 함께 고정한다(6.2). 검사받는 코드와 환경이 읽는 프로젝트 파일(`package.json` 등)은 명세 파일이 아니다.
  - `command`의 인자가 후보 tree나 작업 폴더에 있는 파일인데 명세 파일이 아니면 관문이 `warning: 'PATH' is not pinned`와 `hint: if it is test material, add it to 'inputs' in 'DEF'`를 쓴다. 인자는 환경 정의의 `cwd`(없으면 저장소 뿌리) 기준으로 해석하고 `.`·`./` 조각을 정리한 경로로 비교한다.
- 1.3 **약속(future)**: state가 `future`인 계약. 구현되기 전부터 개발 기준이 된다.
  - 약속 M의 **상대**는 M이 offer면 M이 떠맡은 D, demand면 M을 떠맡은 future offer, drop이면 M이 겨눈 계약이다(1.4, 1.6). 상대가 바뀌면 약속 M을 지키는 일이 바뀐다.
  - **내 약속**은 creator가 나(현재 session의 agent)인 약속과 내가 되돌린 약속(1.5d)이다.
- 1.4 **철회 예약(drop)** X: current 계약 하나를 앞으로 철회하겠다는 약속. 철회할 범위는 target의 범위다. **거두기**는 만든 쪽이 자기 계약을 끝내는 일이다: future는 cancel하고, current는 drop을 걸어 이행해 철회한다.
- 1.5 **이행(fulfill)**: 약속을 확인 결과와 함께 확정하는 일. future D·O는 `current`가 되고, future X는 `fulfilled`, 그 target은 `withdrawn`이 된다. future O를 이행하면 O가 떠맡은 future D도 같은 커밋에서 이행된다(1.6).
- 1.5a **확인과 미확인**(규칙 4):
  - current K의 **고정**은 provider 행과 `file` 행이다. **확인 행**은 current D·O의 레코드에 있는 `check reported|forced PINS` 한 행이고(2.2.2), PINS는 그 판의 고정을 해시한 값이다(5.4). `reported`는 확인 결과(`-r`)로, `forced`는 `--force`의 판단으로 확인한 것이다. K의 고정이 **확인된 고정**이라는 것은 K의 판에 PINS가 맞는 확인 행이 있다는 뜻이다. 확인은 레코드에 사실로 남고(규칙 1), 커밋 메시지나 조상을 보지 않고 그 판만으로 판정한다.
  - current D·O K는 확인 행이 맞고 provider 코드가 고정 이후 바뀌었으면(`changed_since(provider, T)`, 3.11. 바뀌든 옮겨지든 사라지든 같다) **미확인**이다. 조건은 이것 하나다.
  - 확인 행이 없거나 PINS가 맞지 않는 current D·O의 판은 **사고 판**이고(2.3.3), 고정과 다른 명세 파일은 묶음 무결성(2.4)을 어긴 **사고**다. 둘 다 ait의 규칙 밖에서 계약이나 조건이 바뀐 것이고, 미확인(코드가 확인 없이 바뀐 것)과 다르다. HEAD에 있으면 5.10의 오류다.

  그래서:
  - consumer는 보지 않는다. 계약이 provider에게 요구하는 것은 조건뿐이다. consumer만 바꾼 판은 고정이 같으므로 확인 행이 그대로 맞는다.
  - 확인 행은 확인한 고정과 함께 레코드에 있다. 레코드가 복사되어도(amend·cherry-pick) 고정이 같으면 같은 내용을 확인한 것이고, 확인 없이 고정만 바뀌면 PINS가 맞지 않아 사고 판이 된다.
  - 확인한 내용으로 코드를 복원하면 미확인이 풀린다. 그 고정을 확인한 확인 행이 레코드에 있기 때문이다.
  - ait 명령이 직접 쓰는 커밋(관문, fulfill, commit, push의 확인 커밋, pull)은 미확인을 만들지 않는다. 확인 집합을 확인하거나, 확인할 수 없으면 커밋하지 않기 때문이다(1.5b, 7.12). 통합 hook은 current 계약의 고정이 확인 없이 바뀌면 약속으로 되돌린다(7.1.3-3). 그래서 미확인은 ait 밖의 커밋(일반 커밋, hook이 돌지 않는 git 조작, 우회)과, 합친 코드가 확인된 고정과 달라진 통합에서만 생긴다.
  - 다시 확인하면(빈 amend `ait amend K -r`, 그 코드를 바꾸는 `ait commit -r`, 되돌린 뒤의 이행) 풀린다. 다시 확인할 명령은 출력의 hint에 있다(5.9a).
  - 새 미확인(9.1)은 그 계약을 받는 쪽(9.3)에 알림으로 간다.
- 1.5b **걸린 계약**: 변경 A→B가 current D·O K의 provider를 바꾸면 K는 그 변경에 **걸린다**. 곧 `touches(provider, A→B)`(3.10)이거나, A에서는 고정 이후 바뀌지 않았는데(`changed_since(provider, A)`가 거짓) B에서는 바뀌었다(참, 3.11). 두 diff의 정렬이 달라 한쪽이 놓치는 변경도 고정을 기준으로 잡기 위해서다. A에서 이미 provider를 찾을 수 없는 계약은 확인할 코드가 없으므로 걸리지 않는다.
  - ait 명령이 만드는 커밋은 걸린 계약을 그 커밋에서 확인한다. 확인은 이행이 아니므로 규칙 4의 "확인된 것"을 지키는 일이다. A→B는 HEAD→그 커밋의 tree(fulfill·commit은 stage된 상태 T0, 관문은 정규화된 tree T)다. `contract pull`은 확인 결과(`-r`)를 받지 않는다. 받는 결과가 새 미확인이나 확인 없는 명세 변경을 만들면 받기 전에 멈추고 되돌리기 절을 hint로 주며, `--force`면 그 검사에서 막힌 current 계약을 그 판단으로 확인한다(7.12.5).
  - **확인 집합**: ait 명령이 그 커밋에서 확인하는 계약이다. 그 커밋 뒤에 current인 D·O 중, 그 커밋에서 current가 되거나 고정이 바뀌는 것, 빈 amend(6.5)로 다시 확인하는 것, 이행하는 O가 떠맡은 D(규칙 5), 걸린 계약이다. 되돌리거나 철회하는 계약은 그 커밋 뒤에 current가 아니므로 확인하지 않는다. 확인 집합이 비지 않으면 확인 결과나 `--force`가 필요하다(8장). 확인 집합의 계약은 모두 그 커밋의 tree에서 provider를 찾을 수 있어야 한다. 아니면 `E_CODE_REQUIRED`다(1.5c). 관문(6.5), 이행(7.7.3-5), commit(7.7a.1)은 이 정의를 그대로 쓴다.
  - 코드를 옮겼으면 `ID=SCOPES`로 새 위치를 주어 그 위치로 확인한다(7.7.1). 여러 커밋에 걸쳐 바꾸는 중이면 먼저 되돌린다(1.5d).
  - 출력의 `touch ID`는 걸린 계약 중 이 커밋에서 확인하는 것이다.
  - `git commit`은 걸린 계약을 확인하지 않는다. pre-commit이 경고와 hint를 쓴다(7.1.2).
- 1.5c **추상 계약**: provider가 `/`(코드베이스 전체)나 `DIR/`인 future D·O다. 어느 코드에 구현할지 아직 정하지 않은 약속이다. 이행할 때 실제로 구현한 파일·행으로 좁힌다(`ait fulfill ID=범위`, 7.7.1).
  - 그래서 current의 provider 행은 모두 `PATH BLOB`이나 `PATH:S-E BLOB`이고, 그 범위를 current가 되는 커밋의 tree에서 찾을 수 있어야 한다. current는 그 범위의 코드가 바뀌면 미확인이 되므로(1.5a), 확인한 범위가 정해져 있어야 하기 때문이다. `/`·`DIR/`·`new:PATH`와 찾을 수 없는 범위는 current의 provider가 될 수 없다. 확인 방법(prompt·exec)과는 상관없다.
- 1.5d **되돌리기**: current D·O를 future로 되돌리는 것(`ait future amend K`, 7.3.2)이다. 계약을 그대로 둔 채, 확인 없이 그리고 사고 없이 그 코드나 조건을 바꾸는 유일한 길이다(규칙 4).
  - 그래서 여러 커밋에 걸쳐 확정된 코드(current 계약의 provider 코드)를 바꾸는 일(리팩터링)은, 먼저 걸릴 계약을 되돌리고, 중간 커밋을 자유롭게 하고(테스트가 깨져도 된다), 끝에서 다시 이행한다(10.4).
  - 되돌린 계약은 레코드의 state가 future이므로 누구에게나 "지금은 확인되지 않았다"로 보이고(`peek now`에서 빠진다), 받는 쪽에 알림이 간다.
  - current O가 떠맡은 current D는 그 O와 함께 되돌린다(1.6, 5.9 d).
  - 계약은 한 단위다. 그 일부 코드만 바뀌어도 보장 전체가 다시 확인할 대상이 된다. 그래서 계약 전체를 되돌리고, 다시 이행할 때 그대로 둔 조각과 옮긴 조각을 범위 목록으로 준다.
  - 되돌리기는 거두기가 아니다. 계약은 그대로 남고 누구든 다시 이행할 수 있다(전제 1).
  - 되돌린 계약은 되돌린 쪽의 약속이다. 커밋이 다음을 모두 만족하면 K를 **되돌린 커밋**이다. 커밋의 K 판과 부모들의 K 판, 그 `prev`만으로 판정한다(규칙 2).
    1. 그 커밋의 K 판 V가 future이고, 어느 부모의 K 판에도 포함되지 않는(2.2.4) 새 판이다. 옛 판으로 돌아간 커밋(git revert 등)은 사건이 아니기 때문이다.
    2. 부모의 K 판이거나 V의 `prev`에 있는 current 판 C가 있고,
    3. 부모의 K 판 중 C나 C와 본문(2.2.4)이 같은 판을 포함하는 future 판이 없다. 그런 부모는 남의 되돌림을 이미 실어 왔다.

    관문의 되돌리기, 통합 hook의 되돌리기, 다른 가지의 이행을 버린 통합, pull의 고르기로 남의 이행을 버린 커밋이 모두 이것이다. 되돌림을 그대로 실어 나른 병합, 3-way로 본문이 같은 다시 확인을 덮은 커밋은 아니다(7.12). 판정에 필요한 blob(C, 또는 부모의 future 판에서 포함을 따라가는 판)이 없으면 세지 않는다. 판정할 수 없기 때문이다. 그래서 판정이 clone마다 다를 수 있다(11.5).
  - 판정하는 커밋 B(명령은 HEAD, 통합 hook은 만들 커밋(index를 tree로, 7.1.3의 부모와 7.1.5가 붙일 기록줄로), 알림이 일어난 일을 볼 때는 사건 커밋 C, 5.9a)의 이력에서 되돌린 커밋 중, 그 커밋을 조상으로 둔 B 이력의 커밋에서 K가 다시 current가 된 적이 없는 것들의 `Ait-Agent`가 **되돌린 쪽**이다(여럿일 수 있다. 기록줄이 없는 커밋과 `Ait-Agent`가 `unknown`인 커밋은 되돌린 쪽을 남기지 않는다). 커밋의 레코드와 기록줄에서 계산하고 따로 저장하지 않는다(규칙 1, 9.3, 7.8.1).
- 1.5e **다시 고정**: current K의 provider를 같은 범위 그대로 지금 tree에 맞추는 것이다. provider 행마다 PATH가 같고, `PATH:S-E`면 S-E는 옛 고정을 그 tree로 사상한 것(3.7)이며, BLOB은 그 tree의 blob이다. 범위와 조건을 바꾸지 않으므로 뜻이 같다(9.2). 확인과 함께 할 때만 그 고정이 확인된 고정이 된다(확인 행, 1.5a). 이력의 `verified`(7.9.5)와 merge driver의 4단계(7.13.1)가 이 판정을 쓴다.
- 1.6 **undertake** D←O (규칙 5): "offer O가 demand D의 책임을 떠맡는다". O는 D를 포함하는 약속이다(예: D "최저가를 내놓아라", O "최고가와 최저가를 내놓겠다"). 그래서 관계는 비대칭이다.
  - D는 먼저 이행될 수 있다. D는 언제든 D 자체로 이행한다. 떠맡은 O는 future로 남아 나머지 약속을 계속 진다.
  - O는 D 없이 이행될 수 없다. future O를 이행하려면 O가 떠맡은 future·current D의 명세 파일도 통과해야 하고(그 커밋 뒤에 cancelled·withdrawn인 D는 확인하지 않는다. 같은 커밋에서 철회되는 D도 그렇다, 1.5b), 그중 future인 D는 같은 커밋에서 함께 이행된다. 여러 offer가 한 D를 함께 떠맡아도 된다.
  - 연결의 state는 `undertaken`(떠맡음)과 `released`(놓음)다. `release`는 그 offer로 그 요구를 더는 맡지 않을 때 쓴다. cancel·철회는 떠맡기와 상관없이 되고, 되돌리기는 5.9 d만 지킨다. 연결은 남아 다른 끝의 state와 함께 보인다.
  - undertaken 연결에서 O가 current이면 D는 future일 수 없다(5.9 d). 확인된 O가 확인되지 않은 D를 포함할 수는 없기 때문이다(규칙 4·5). 그래서 future D를 current O로 떠맡을 수 없고(future offer로 떠맡는다), current O가 떠맡은 current D를 혼자 되돌릴 수 없다(O도 함께 되돌린다, 1.5d). 둘 다 `E_STATE`다.
  - 떠맡기는 current D를 바꾸는 약속이 아니다. current D의 코드를 바꾸려면 D를 되돌린다(1.5d). 되돌린 D는 future이므로 떠맡은 O(current였으면 함께 되돌린다)와 함께 이행된다.
- 1.7 **요청**: ait 쓰기 명령 하나(`ait future demand …`, `ait fulfill …` 등)로 하는 계약 변경이다. 그 변경을 담은 커밋이 **요청 커밋**이고, 커밋 기록줄 `Ait-Request: RID`의 요청 ID(RID)로 식별한다(5.4). 통합 hook과 `contract pull`도 계약 파일을 새 판으로 적은 커밋에 기록줄을 붙여 요청 커밋으로 만든다(7.1.5, 7.12.6). 알림의 `request RID` 행이 이 ID로 요청을 가리킨다(9.7). 받기(`contract pull`)는 요청이 아니라 계약을 ID로 지정하고, 받았는지는 판의 포함으로 판정한다(7.12).
- 1.8 **session**: 에이전트 하나의 작업 단위. 이름(agent), worktree, 구독을 가진다. 닫기 전까지 남는다.
- 1.9 식별자(hex는 소문자):

| 종류 | 형식 |
| --- | --- |
| demand·offer·drop | `D`·`O`·`X` + 12 hex |
| 요청 | `R` + 12 hex |
| session | `S` + 12 hex |
| alias | `[a-z][a-z0-9_]{0,31}`. 예약어 `demand offer future undertake release as drop amend cancel contract ct fulfill`은 쓸 수 없다. 한 요청 안에서만 쓰이고, 그 안에서 중복될 수 없다 |

- 1.10 ID는 시스템 CSPRNG의 6바이트로 만든다(git의 `csprng_bytes`가 고르는 대로). 같은 요청에서 이미 쓴 값이나 HEAD에 있는 값이면 다시 만든다. 직접 작성할 때는 `ait contract new`가 ID를 발급한다.

## 2. 저장소 파일

### 2.1 배치

- 2.1.1 `.ait/<ID>`: 계약 레코드.
- 2.1.2 `.ait/undertake/<DID>-<OID>`: 연결 레코드.
- 2.1.2a `.ait/init`: 이 저장소가 ait를 쓴다는 init 파일이다(7.1.1). 레코드가 아니므로 레코드 규칙(2.2·2.3, 5.9 a~e, 9.2, 7.9.5)에 들지 않는다. `.ait/` 아래 경로이므로 계약 경로(6.1)다. 내용은 정확히 `version 2` 한 행이고 mode는 `100644`다. `version 2`는 확인 행(2.2.2)이 든 레코드 형식이다. ait-mini의 `version 1` 저장소를 옮기는 규칙은 두지 않는다(ait-mini는 시험용이다). 다르면 `E_FORMAT`이고 hint는 5.10의 것이다. 지워져도 다음 `ait init`이 다시 만든다.
- 2.1.3 `.ait/` 아래에 그 밖의 경로가 있으면 `E_FORMAT`이다. 레코드 파일의 mode는 `100644`다.
- 2.1.4 명세 파일은 `.ait/` 밖의 경로이고, mode는 `100644` 또는 `100755`다.
- 2.1.5 레코드는 삭제하지 않는다. 거두기(cancel, 철회)·놓기·되돌리기는 모두 state 변경으로 표현한다.

### 2.2 레코드 형식

- 2.2.1 레코드는 `key SP value LF` 행의 나열이다. 빈 행, CR, 앞뒤 공백, 마지막 LF 누락, 제어 문자는 형식 오류다.
- 2.2.2 **정규형**(커밋에 들어가는 형식). 키 순서는 고정이다.

```
demand:    role demand / state S / consumer SCOPE (1행 이상) / provider SCOPE (1행 이상) / file PATH MODE BLOB def|input (1행 이상) / check reported|forced PINS (current일 때 1행) / creator AGENT SID / prev P (1행 이상)
offer:     role offer / state S / provider SCOPE (1행 이상) / file PATH MODE BLOB def|input (1행 이상) / check reported|forced PINS (current일 때 1행) / creator AGENT SID / prev P (1행 이상)
drop:      role drop / state S / target ID / creator AGENT SID / prev P (1행 이상)
undertake: state undertaken|released / prev P (1행 이상)
```

  - SCOPE는 `PATH BLOB`, `PATH:S-E BLOB`, `new:PATH`, `DIR/ TREE`, `/ TREE` 중 하나다. 범위 목록(3.2a)이면 범위마다 한 행이고, 행 순서는 3.2a를 따른다.
  - `file` 행은 PATH 바이트 오름차순이다. 끝의 `def`는 이 계약의 명세 정의, `input`은 정의가 선언해서 함께 고정한 파일이다.
  - `check` 행(확인 행, 1.5a)은 current인 D·O에만 정확히 하나 있다. PINS는 그 판의 고정의 해시(5.4)다. future로 되돌리면 지운다. current가 아닌 판의 확인 행, 둘 이상의 확인 행, OID 꼴이 아닌 PINS는 형식 오류다. 확인 행이 없거나 PINS가 맞지 않는 current는 형식 오류가 아니라 사고 판이다(2.3.3).
  - 확인 행을 쓰는 규칙: 이번 확인이 `-r`이거나, 출발 판(관문·fulfill·commit은 HEAD 판, 통합은 출발 판, pull은 최신 판과 내 판)에 같은 PINS의 `reported` 행이 있으면 `reported`, 아니면 `forced`다. 같은 코드·명세가 테스트로 통과했다는 사실은 뒤의 `--force`로 없어지지 않기 때문이다. 그래서 PINS가 바뀔 때만 이번 확인의 종류가 그대로 적힌다.
  - `prev` 행은 OID 오름차순이다. 처음 판은 `-` 한 행이다.
  - `creator`는 만든 session의 `AGENT SID`다. session 없이 통합 hook이 만든 레코드는 `unknown -`다(7.1.5의 기록줄과 같다). 그래서 그 계약은 누구의 "내가 만든 계약"도 아니고, 거두는 hint는 누구에게도 쓰지 않는다.
  - drop에는 범위를 적지 않는다. 철회할 범위는 target의 범위다(1.4). 그래서 target이 바뀌어도 어긋날 사본이 없다(규칙 1).

예:

```
role demand
state future
consumer src/order.c:10-40 3b18e512dba79e4c8300dd08aeb37f8e728b8dad
provider src/stock.c 9a8b7c6d5e4f30211a2b3c4d5e6f708192a3b4c5
file specs/environments/node.yaml 100644 6a7b8c9d0e1f2a3b4c5d6e7f8091a2b3c4d5e6f7 input
file specs/no-oversell.yaml 100644 1f2e3d4c5b6a79881726354a5b6c7d8e9f0a1b2c def
file tests/no_oversell.sh 100755 4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f input
creator agent-a S81c2d3e4f5a6
prev -
```

- 2.2.3 **작성 형식**(직접 작성 입구): 정규형과 같지만 다음을 허용한다. 관문이 정규형으로 채운다(6.2).
  - 키 순서는 자유다.
  - 생략할 수 있는 것: SCOPE의 BLOB, `file`의 MODE·BLOB·`def`·`input` 구분, `check`, `creator`, `prev`. 적힌 BLOB·MODE·`check`·`prev`는 무시하고 다시 계산한다. 확인 행은 관문이 H 판의 것을 두거나 이번 확인으로 새로 쓴다(2.2.2, 6.5).
  - `def`·`input` 구분이 없는 `file` 행과 `def` 행은 명세 정의로 본다. `input` 행은 정의에서 다시 펼쳐 정한다.
  - `creator`를 적었으면 새 레코드에서는 session 값으로 덮고, 기존 레코드에서는 HEAD 판과 같아야 한다(6.2).
- 2.2.4 **`prev`(판 DAG)**: 직전 판들의 blob OID다. 레코드를 바꾸는 모든 경로(관문, fulfill, commit, 통합 hook, `contract pull`, `contract push --force`)가 바꾼 레코드의 prev를 직전 판으로 쓴다. 충돌 해결로 고른 판은 극대 판들을 모두 가진다(7.1.3-5). pull과 merge driver가 이 DAG로 어느 판이 나중인지 가린다.
  - **판**은 레코드 파일이 바뀔 때마다 생기는 그때의 내용 하나이고 blob OID로 가리킨다. 판의 **본문**은 `prev` 행과 확인 행을 뺀 것이다. 같은지는 바이트로 본다.
  - 판 a가 판 b를 **포함**한다는 것은 b가 a이거나, a의 `prev`를 따라가 b에 닿는다는 뜻이다. 따라가다 객체 저장소에 없는 blob을 만나면 그 가지는 거기서 끝난다.
  - 한 레코드의 여러 판 중 **극대 판**은, 같은 판을 하나로 친 뒤 다른 판에 포함되지 않는 판이다. 없음(그 레코드가 아직 없던 쪽)은 극대 판이 아니다. 7.1.3-5가 쓴다.
- 2.2.5 **`creator`**: 처음 만든 주체이며 바뀌지 않는다. 이후의 행위자는 커밋 기록줄에 남는다.

### 2.3 상태 전이(규칙 2)

| 종류 | 전이 | 경로 |
| --- | --- | --- |
| D·O | 없음 → `future` | 관문 |
| D·O | 없음 → `current` | 관문 + 확인 결과 |
| D·O | `future` → `future` (범위·명세 파일 수정) | 관문 |
| D·O | `future` → `cancelled` | 관문 |
| D·O | `future` → `current` | fulfill + 확인 결과 |
| D·O | `current` → `current` (범위·명세 파일 수정, 다시 확인) | 관문·fulfill·commit·`contract push --force`(7.10.1-4)·`contract pull --force`(7.12.5) + 확인 결과나 `--force`(consumer만 바꾸면 결과 없음) |
| D·O | `current` → `future` (되돌리기, 1.5d) | 관문. 통합 hook(7.1.3-3) |
| D·O | `current` → `withdrawn` | target인 X의 fulfill |
| X | 없음 → `future`(target이 current), `future` → `future`(target 변경. 새 target이 current), `future` → `cancelled` | 관문 |
| X | `future` → `fulfilled`(target이 current) | fulfill |
| undertake | 없음·`released` → `undertaken`(두 끝이 future·current인 D·O. 5.9 d), `undertaken` → `released` | 관문 |

- 2.3.1 관문은 이 표의 "관문" 행만 허용한다. `contract pull`의 도우미 절은 관문을 지나므로 "관문"에 든다(7.12). 그 밖의 변경(역할·creator를 바꾸기, 레코드 삭제, 종결 상태 수정)은 `E_STATE`다. 한 요청 안에서 여러 절을 거쳐도 판정은 HEAD 판과 결과 판의 전이로 한다.
- 2.3.2 통합 커밋에서는 "HEAD 판"을 "출발할 수 있는 판(7.1.3) 중 하나"로 읽는다. 없음(새 레코드)도 출발점이다. 출발할 수 있는 판 중 끝난 판(cancelled·withdrawn인 D·O, fulfilled·cancelled인 X)이나 끝난 판을 포함하는 판이 있으면 끝난 판에서만 출발할 수 있다. 끝난 상태에서는 나가는 길이 없기 때문이다(2.3.3). 확인 결과가 필요한 전이는 통합에서 할 수 없다. 그래서 통합에서 확인 없이 바뀐 current의 고정은 7.1.3-3이 다룬다(규칙 3·4).
- 2.3.3 **사고 판과 형식이 틀린 판**(규칙 2). 판마다 그 판과 `prev`의 데이터로만 판정한다. 어떤 git 조작으로 생겼는지는 보지 않는다.
  - **형식이 틀린 판**은 2.2의 정규형에 맞지 않는 판이다.
  - **사고 판**은 다음 중 하나인 판이다.
    - 사고 판도 형식이 틀린 판도 아닌 `prev` 중 어느 것에서도 갈 수 없는 판. 갈 수 있다는 것은 역할과 creator가 같고, 본문이 같거나 2.3 표의 state 짝으로 갈 수 있다는 뜻이다. 처음 판(`prev -`)은 "없음 →" 짝으로 본다. 연결 레코드는 state 짝만 본다.
    - 자신을 제외하고 `prev`를 따라 도달하는 끝난 판이 있는데(사고 판을 거쳐 도달하는 것도 센다), 본문이 그 끝난 판 중 어느 것과도 같지 않은 판. git merge 충돌을 손으로 풀어 cancel을 되살린 판이 그렇다. 검사하는 판 자신도 끝난 판일 수 있으므로, 2.2.4의 포함에서 자신을 빼고 본다.
    - 확인 행이 없거나 PINS가 맞지 않는 current D·O의 판(1.5a).
    - `prev`의 blob이 없어 판정할 수 없으면 사고 판으로 보지 않는다. 다른 레코드와의 제약(5.9)은 여기서 보지 않는다.
  - 그래서 사고 판 위에 쌓은 판도 사고 판이고, 그 브랜치는 고칠 때까지 막힌다(5.10). 고치는 길은 사고 판이 아닌 판을 `prev`로 삼으므로 막힘이 풀린다. 다른 브랜치의 사고 판과 형식이 틀린 판은 pull과 통합 hook이 출발 판에서 빼고 경고한다(7.1.3, 7.12.1).
  - ait 명령이 쓰는 판은 사고 판이 아니다. 관문·fulfill·commit은 HEAD 판에서 2.3의 전이로 쓰고, pull은 남는 판 하나의 본문에 최신 판 모두를 `prev`로 쓴다(7.12).

### 2.4 묶음 무결성(규칙 3)

- 2.4.1 future·current인 D·O의 `file` 행은 그 커밋의 tree에서 경로, mode, blob이 모두 일치해야 한다.
- 2.4.2 future·current인 D·O의 `input` 행의 집합은 그 계약의 `def` 행들이 선언한 파일(`inputs`와 환경 정의 파일. 이미 `def`인 파일은 뺀다)과 같아야 한다.
- 2.4.3 이를 어긴 상태는 관문, 이행, 확인할 것이 있는 commit, 통합 hook, push hook을 통과하지 못한다(`E_INCOMPLETE`).

### 2.5 명세 정의와 환경 정의 (YAML)

- 2.5.1 **YAML 부분집합**: UTF-8, LF. `#` 주석, 블록 map, 블록 list, flow list(`[a, b]`)와 flow map(`{K: v}`)(서로 중첩 가능), scalar(plain·`'…'`·`"…"`. `"…"`의 이스케이프는 YAML과 같다), 10진 정수, `|` 블록 문자열(들여쓰기·끝 행 처리 표시 `|2`·`|-`·`|+`를 붙일 수 있다)만 쓴다. 놓이는 자리는 YAML 1.2와 같다: map의 키도 scalar(plain·`'…'`·`"…"`)이고, 문서 전체가 flow map일 수 있으며, flow collection은 여러 행에 걸치고 그 안에 주석을 둘 수 있다. 문서는 하나이고 `---`·`...` 표시는 써도 된다. anchor·alias·tag·지시(`%YAML` 등)·여러 문서·중복 키·알 수 없는 키는 `E_FORMAT`이다.
- 2.5.2 **명세 정의**(아무 경로). 틀은 `ait contract new spec`이 준다(7.4.1a):

```yaml
body: 재고가 0인 상품의 예약 요청은 거절한다.   # 필수. 조건
engine: exec                                   # 선택. 생략하거나 비우면 prompt
command: [sh, tests/no_oversell.sh]            # exec면 필수. argv이고 셸을 끼우지 않는다
environment: node                              # 선택. specs/environments/node.yaml
success: [0]                                   # 선택. 통과로 보는 exit 코드. 기본 [0]
inputs: [tests/no_oversell.sh]                 # 선택. 계약과 함께 고정할 검사 재료
```

- 2.5.3 **환경 정의**(`specs/environments/<이름>.yaml`):

```yaml
cwd: .                              # 저장소 루트 기준. 기본 .
timeout_ms: 600000                  # 프로세스마다. 기본 600000
env:
  inherit: [PATH, HOME, DATABASE_URL]   # ait의 환경에서 넘길 변수
  set: {NODE_ENV: test}
  required: [DATABASE_URL]          # 없으면 확인할 수 없다(unknown)
prepare:                            # 확인 전에 차례로 실행하는 argv 목록
  - [npm, ci]
cleanup:                            # 확인 뒤 역순으로 실행
  - [rm, -rf, node_modules]
```

  1단계에서 ait는 환경 정의를 실행하지 않고, 형식만 검사해 확인 요청 파일에 실어 준다(8.2).

- 2.5.4 engine은 `exec`와 prompt(생략)뿐이다. 그 밖의 이름은 `E_FORMAT`이다. 다른 에이전트나 LLM에게 확인을 맡기려면 그 프로그램을 exec의 `command`로 적는다.
- 2.5.5 `inputs`의 경로는 3.1을 따르는 저장소 파일이어야 한다. `input` 행은 다시 펼치지 않는다.
- 2.5.6 prompt 정의에 올 수 있는 키는 `body`와 `inputs`뿐이다(값이 빈 `engine`은 없는 것으로 본다).
## 3. 범위(scope)

- 3.1 PATH 규칙: 저장소 루트 기준이며 cwd와 무관하다.
  - 비어 있지 않다.
  - `/`나 `-`로 시작하지 않는다.
  - 공백, 제어 문자, `:`, `,`가 없다.
  - `/`로 나눈 각 조각은 비어 있지 않고 `.`이나 `..`이 아니다.
  - `.ait`이거나 `.ait/`로 시작하지 않는다.
- 3.2 인자 형식:

| 형식 | 뜻 | 사용처 |
| --- | --- | --- |
| `PATH` | 파일 전체 | 전부 |
| `PATH:S-E` | S~E행(1부터 세고 양끝 포함, `[1-9][0-9]*`, S≤E) | 전부 |
| `new:PATH` | 아직 없는 파일 | 계약 범위 |
| `DIR/` | 그 접두어 아래 전부 | peek·구독, future 계약의 provider(추상, 1.5c) |
| `/` | 저장소 전체 | peek·구독, future 계약의 provider(추상, 1.5c) |

- 3.2a **범위 목록**: consumer와 provider는 범위 하나 이상이다. 인자에서는 `,`로 잇는다(예: `src/runner.c,src/cli.c:10-40`. PATH에는 `,`가 없다). 같은 범위를 두 번 주면 `E_SCOPE`다. 명세 파일 목록(FILES, 7.3.1)에 같은 경로를 두 번 주면 하나로 본다(git이 `git add a a`의 경로를 하나로 보는 것과 같다). 범위에 대한 판정(`overlap`, `touches`, `changed_since`, 사상되지 않음)은 목록 중 하나라도 참이면 참이다. 고정과 다시 고정은 범위마다 하고, 다시 고정한 뒤 같아진 행은 하나로 합친다. 표시는 범위마다 한 행이고, 한 행 안에 여럿을 쓸 때(7.1.2, 7.8.2, 8.2, 8.3)는 표시를 `,`로 잇는다.
  - 레코드의 행 순서: PATH(추상이면 `DIR/`·`/` 그대로, `new:PATH`는 PATH) 바이트 순, 같은 PATH면 파일 전체가 먼저이고 행 범위는 S, E 순이다.
  - 직접 작성한 레코드의 범위는 (new 여부, PATH, S, E)가 같은 HEAD 행과 짝짓는다. 짝이 없는 행만 바뀐 범위다(3.3).

- 3.3 **고정**: 새로 주어지거나 바뀐 범위만 HEAD 커밋 H에서 고정한다. 바뀌지 않은 범위는 기존 고정을 유지한다. 도우미가 받은 범위(`-`가 아닌 것)는 언제나 새로 주어진 것으로 보고 HEAD 행 번호로 고정한다. 직접 작성한 레코드에서는 (new 여부, PATH, S, E)가 같은 H 판의 행이 없을 때 바뀐 것으로 본다(3.2a). BLOB은 비교하지 않는다. 작성 형식에 적힌 BLOB은 무시하고 다시 계산한다. 아래 조건을 어기면 `E_SCOPE`다.
  - `PATH`: `H:PATH`가 blob이어야 하고, 그 OID를 붙인다.
  - `PATH:S-E`: `H:PATH`가 NUL 없는 blob이고 E ≤ 행 수여야 한다.
  - `new:PATH`: `H:PATH`가 없어야 한다.
  - `DIR/`: `H:DIR`가 tree여야 하고, 그 OID를 붙인다. `/`는 H의 root tree OID를 붙인다.
  - fulfill·commit의 `=SCOPES`(7.7.1, 7.7a.1)는 H 대신 T0에서 같은 조건으로 고정한다.
- 3.4 범위를 고정할 때 확인하는 입력 규칙이다(`E_SCOPE`).
  - `new:`는 demand의 consumer와 future 계약의 provider에만 쓸 수 있다.
  - 추상 범위(`/`, `DIR/`)는 future D·O의 provider에만 쓸 수 있다. current로 등록하거나 current 계약의 provider를 추상으로 amend하면 `E_SCOPE`다(1.5c). consumer는 요구를 건 쪽 자신의 코드이므로 건 쪽이 위치를 안다(10.1a). 아직 없는 파일이면 `new:PATH`를 쓴다.
- 3.5 행 수 = LF 개수 + (크기 > 0이고 마지막 바이트가 LF가 아니면 1).
- 3.6 **diff0**: 범위 계산에 쓰는 diff는 모두 아래 옵션으로 실행한다.

  `git diff -U0 --no-color --no-ext-diff --no-textconv --text --inter-hunk-context=0 --diff-algorithm=myers --no-indent-heuristic`
- 3.7 **사상** `map(S,E, F→T)`:
  1. F=T이면 그대로다. F가 없으면 **nobase**다.
  2. `diff0 F T`의 hunk `@@ -os[,ol] +ns[,nl] @@`를 순서대로 본다. 생략된 개수는 1이다. off는 앞선 hunk들의 Σ(nl−ol)이다.
     - x가 hunk 앞(ol=0이고 x≤os, 또는 ol>0이고 x<os) → `x+off`.
     - x가 hunk 안(ol>0이고 os≤x≤os+ol−1):
       - 시작점은 nl>0이면 `ns`, 아니면 `ns+1`.
       - 끝점은 nl>0이면 `ns+nl−1`, 아니면 `ns`.
     - 모든 hunk 뒤 → `x+off`(off는 전체 합).
  3. 시작이 끝보다 크면 **deleted**다.
- 3.8 **표시**(출력은 BLOB 없이 이 형태로만):

| 저장 | 대상 tree의 상태 | 표시 |
| --- | --- | --- |
| `PATH BLOB` | 같은 blob / 다른 blob / 없음 | `PATH` / `PATH ?changed` / `PATH ?missing` |
| `new:PATH` | 없음 / 있음 | `new:PATH` / `new:PATH ?exists` |
| `PATH:S-E BLOB` | 사상 성공, 범위에 닿는 변경 없음 / 있음(3.11) | `PATH:s-e` / `PATH:s-e ?changed` |
| `PATH:S-E BLOB` | 없음·deleted·nobase | `PATH:S-E ?missing`·`?deleted`·`?nobase` |

  `DIR/ TREE`·`/ TREE`는 `DIR/`·`/`로 표시한다(추상 계약은 future뿐이다).
  사상 표지(`?missing`·`?deleted`·`?nobase`·`?exists`)는 모든 범위에 붙는다. `?changed`는 current D·O의 provider에만 붙인다. 표시는 범위의 위치와 변화만 보여 준다. 미확인인지는 판정이므로 peek·todo가 `unverified` 행으로 따로 쓴다(1.5a, 규칙 7). consumer나 future 계약의 범위에 붙은 `?`는 코드 위치가 바뀌었다는 표시일 뿐이다.

- 3.9 **겹침** `overlap(q, r, T)`: 아래 순서로 판정한다.
  1. q나 r이 `/`이면 참.
  2. q나 r이 `DIR/`이면 다른 쪽의 PATH나 DIR가 그 접두어로 시작할 때 참(둘 다 DIR이면 어느 한쪽이 다른 쪽의 접두어일 때).
  3. 경로가 다르면 거짓.
  4. 둘 다 range이면 구간이 교차할 때 참. T에서 사상하지 못한 range(`?missing`·`?deleted`·`?nobase`)는 파일 전체로 본다.
  5. 그 밖은 모두 참.
- 3.9a **기대는 겹침**: 알림(9.3)은 "이 범위에 기대는가"를 묻는다. 추상 범위(`/`·`DIR/`)인 provider는 아직 위치가 정해지지 않은 것이라 어느 `PATH`·`PATH:S-E` 범위에도 기대지 않는다. 그래서 알림에서는 레코드의 추상 범위가 추상 구독 범위하고만 겹친다. 보여 주기(peek, elsewhere)는 "여기에 구현될 수 있는가"를 찾으므로 3.9를 그대로 쓴다.
- 3.10 **변경 접촉** `touches(r, A→B)`: r을 A에서 해석한다. 사상하지 못한 range는 파일 전체로 본다. 추상 범위는 future에만 있으므로 걸림(1.5b)에 쓰이지 않는다.
  - 바뀐 경로는 `git diff-tree -r --no-renames -z A B`에서 blob이 다른 경로다. mode만 바뀐 경로는 바뀌지 않은 것으로 본다(고정은 내용만 본다, 11.7).
  - `PATH`이면 PATH가 바뀐 경로에 있으면 참이다(current의 provider에는 `new:`가 없다, 1.5c).
  - range [s,e]이면, `diff0 A:PATH B:PATH`의 어떤 hunk의 접촉 구간과 겹치면 참이다. 접촉 구간은 ol>0이면 [os, os+ol−1], 아니면 [os, os+1]이다.
  - B에서 파일이 사라졌으면 참이다.
- 3.11 **고정 이후 변경** `changed_since(scope, T)`:
  - `PATH BLOB`: `T:PATH`의 blob이 BLOB과 다르면 참.
  - `PATH:S-E BLOB`: `diff0 BLOB T:PATH`에 [S,E]와 접촉하는 hunk가 있거나, 파일이 사라졌으면 참. BLOB이 이 저장소에 없으면(rebase 등으로 옛 판이 전해지지 않았다) 참으로 본다.
  - `new:PATH`: T에 PATH가 있으면 참.
  - `DIR/ TREE`·`/ TREE`: TREE와 T의 그 아래 경로 중 계약 경로(6.1. 판정하는 상태는 T)가 아닌 것의 blob이 하나라도 다르면 참. 더해지거나 지워진 경로도 다른 것이다. mode만 바뀐 것은 다르지 않다.

## 4. 로컬 실행 상태 (`<git-common-dir>/ait/`, 추적 안 함)

- 4.1 모든 worktree가 이 상태를 공유한다. 디렉터리가 없으면 그 안에 쓰는 명령이 만든다. 데이터 파일은 임시 파일에 쓴 뒤 rename해서 갱신한다. `.seen`은 덧붙이기만 한다. 잠금 파일(`*.lock`, `*.listen`)은 rename하지도 지우지도 않는다. 지우면 같은 경로에 새 파일이 생겨, 기다리던 쪽과 새로 온 쪽이 서로 다른 잠금을 잡기 때문이다. 잠금은 `flock(2)`이다.
- 4.2 `sessions/<SID>` 파일의 행:

```
agent NAME
worktree /abs/path
sub id ID
sub scope SCOPE           (range는 `PATH:S-E BLOB`, 그 밖은 `PATH`·`DIR/`·`/`)
tip OID REFNAME           (listener가 마지막으로 다 처리한 ref 스냅샷)
```

- 4.3 `sessions/<SID>.seen`: 내보냈거나 일부러 건너뛴 사건의 키 목록(쓰기에 실패한 사건은 넣지 않는다, 9.5). 행은 사건의 키(`version ID BLOB`, `spec ID PATH MODE BLOB`, `unverified ID BLOB PCONTENT`)다. 무엇을 전달했는지 적은 전달 영수증이다. 덧붙이기만 한다.
- 4.4 `sessions/<SID>.lock`: session 파일을 읽고 고치는 짧은 구간에 `flock`으로 잡는다.
- 4.5 `sessions/<SID>.listen`: listener가 도는 동안 `flock`으로 잡는 파일이다. listener는 `.lock`을 잡은 채 이 파일을 열어(여는 것으로 비우지 않는다) 잡고, 잡은 fd에서 내용을 비우고 자기 PID를 쓴 뒤 `.lock`을 놓는다. `session close`도 `.lock`을 잡은 채 이 파일을 잡아 본다. 잡히지 않으면 PID를 읽어 hint에 쓰고(7.2.3), 잡히면 두 잠금을 쥔 채 session의 데이터 파일(`<SID>`, `<SID>.seen`)을 지운다. 그래서 hint의 PID는 언제나 지금 잡고 있는 listener의 것이다.
- 4.6 `worktrees/<KEY>`: 그 worktree의 기본 session ID 한 행(5.1). KEY는 worktree 경로를 `git hash-object --stdin`에 넣은 값이다. `worktrees/<KEY>.lock`은 그 worktree의 공유 상태를 바꾸는 짧은 구간(session을 만들고 지우기, 기본 session 포인터를 읽고 쓰기, 공개 뒤의 동기화 5.5-4)에 잡는다. 그래서 같은 worktree의 두 명령이 겹쳐도 포인터나 index가 어긋나지 않는다.
- 4.7 확인 요청 파일과 결과 틀은 worktree마다 `<git-dir>/ait/check-request.yaml`·`check-result.yaml`에 둔다(`git rev-parse --git-dir`. linked worktree에서는 `.git`이 파일이다). 언제 새로 쓰는지는 8.4를 따른다. 명령은 그 경로를 출력하므로 에이전트는 출력된 경로를 쓴다. hint는 확인 요청 파일의 경로를 `REQ`, 결과 틀의 경로를 `RES`로 적고 실제 절대경로로 채운다(5.8).
- 4.8 후보 tree를 만드는 임시 index는 `$TMPDIR`(없으면 `/tmp`)에 `mkdtemp`로 만들고, 끝나면 지운다.

## 5. 공통 규칙

- 5.1 **session**: `AIT_SESSION`이 현재 session을 가리킨다.
  - 다음 명령은 유효한 session이 필요하다: 관문을 지나는 명령, `fulfill`, 확인할 것이 있는 `ait commit`(기록줄을 쓰기 때문이다), `contract push --force`, 요청 커밋을 만드는 `contract pull`(7.12.6), `sub`, `unsub`, `listen`, SID를 생략한 `session close`.
  - `AIT_SESSION`이 없으면 이 worktree의 기본 session을 쓴다. `AIT_SESSION=-`이면 session 없이 실행한다. `session open`은 그 worktree에 기본 session이 없을 때만 자기를 기본으로 한다(남의 이름으로 기록되지 않게). `session close`는 그 SID를 가리키는 기본 session 포인터를 어느 worktree에서든 지운다. session을 만들고 지우는 일과 포인터를 읽고 쓰는 일은 `worktrees/<KEY>.lock` 안에서 한다(4.6). 포인터가 없는 session을 가리키면 포인터가 없는 것으로 보고 고친다.
  - 무효이면 `E_SESSION`이다: 값이 없고 이 worktree의 기본 session(4.6)도 없다, 모르는 ID다, 다른 worktree의 session이다.
  - `peek`, `todo`, `contract new`·`add`, `contract push`(`--force` 없이), 요청 커밋을 만들지 않는 `contract pull`, `init`, `session list`는 session이 없어도 된다. 값이 설정됐는데 무효이면 `E_SESSION`이다. `session open`, SID를 준 `session close`, 확인할 것이 없는 `ait commit`(7.7a.1), `merge-driver`, git 통과는 session을 보지 않는다. `hook`은 무효인 session을 없는 것으로 본다.
- 5.2 **쓰기 전제조건**: 쓰기 명령(도우미, `contract add`, `contract commit`, `fulfill`, `commit`, `contract push --force`, `contract pull`, `init`의 init 커밋)은 아래 순서로 확인한다.
  1. HEAD가 브랜치를 가리키고, 그 브랜치에 커밋이 있어야 한다. 아니면 `E_BUSY`. 진행 중인 git 조작(2)이 있으면 그 hint가 앞선다(rebase는 detached HEAD에서 돈다). `init`의 init 커밋만 커밋이 없는 브랜치를 허용한다.
  2. 진행 중인 git 조작(merge·cherry-pick·revert·rebase·`git am`)이 없어야 한다. 아니면 `E_BUSY`. 진행 중 판정: MERGE_HEAD는 `--git-path`로 얻은 파일을 읽어 OID 수를 센다. CHERRY_PICK_HEAD·REVERT_HEAD는 `git rev-parse -q --verify`로 확인하고, 여러 커밋을 고르는 중이면 그것이 없어도 `sequencer/todo`(`--git-path`)가 있으면 진행 중이다. rebase는 `rebase-merge`·`rebase-apply` 디렉터리로 보되, `rebase-apply/applying`이 있으면 `git am`이다. 통합 상태는 7.1.3이 정한다.
  3. 도우미 명령, `fulfill`, `commit`, `contract pull`, `init`은 `git status --porcelain --untracked-files=all -- .ait`가 비어 있어야 한다. 아니면 `E_DIRTY`. `contract pull`은 이에 더해, pull이 쓰는 경로의 index와 작업 폴더를 받는 결과를 정한 뒤 아무것도 바꾸기 전에 본다(7.12.0).
- 5.2a **설치 확인**: `init`이 아닌 모든 ait 명령은, HEAD에 레코드가 있는데 이 clone의 ait hook(7.1.1-1) 중 하나라도 없으면 막지 않고 5.9a의 H12를 쓴다. hook이 없으면 일반 커밋의 미확인과 사고가 경고 없이 생기기 때문이다. 레코드가 없으면 사고가 생길 계약도 없다(규칙 6).
- 5.3 **메시지**: `-m MSG`(여러 번 주면 문단을 나눈다) 또는 `-F FILE`(`-`는 stdin) 중 하나는 반드시 있어야 한다. 둘을 섞거나, `git stripspace`로 정리한 결과가 비었으면 `E_USAGE`다. `contract pull`은 고르기·도우미 절·`--force`가 있을 때만 메시지가 필요하다. pull이 규칙대로 스스로 적는 새 판(3-way, 끝난 쪽, 같은 본문 합치기, 6.2의 다시 고정)은 받는 쪽의 판단이 아니므로 ait가 메시지를 쓴다(7.12.6). 첫 행이 `[CONTRACT] `(이행 커밋은 `[FULFILL] `)로 시작하지 않으면 그 접두어를 붙인다. `ait commit`은 코드 커밋이므로 붙이지 않는다. 기록줄을 붙인 메시지는 5.4의 파서로 다시 읽어, 붙인 행이 그대로 읽혀야 한다(요청 커밋은 기록줄로 가리기 때문이다, 1.7). git의 자르기 행(`# ------------------------ >8 ------------------------`)처럼 그 아래를 메시지에서 빼는 행이 있으면 `E_USAGE`(`error: the message hides the trailers: 'LINE'`)이고 hint는 `remove that line from the message`다.
- 5.4 **커밋 기록줄**(git trailer, 규칙 1): 메시지 끝에 빈 행 하나를 두고 붙인다.

```
Ait-Request: RID
Ait-Agent: NAME
Ait-Session: SID
```

  - 확인은 기록줄에 남기지 않는다. 확인한 사실은 레코드의 확인 행(1.5a, 2.2.2)에, 확인 결과는 메시지 본문(8.4)에 있다.
  - **PINS**는 그 레코드의 `provider ` 행들과 `file ` 행들을 레코드에 적힌 순서대로, 행마다 LF로 끝내 이은 바이트열을 그 저장소의 `git hash-object --stdin`에 넣은 값이다. 확인 행(2.2.2)이 쓴다. `reported`는 낸 결과로 통과했다는 뜻이고 결과는 본문에 있다(8.4). `forced`는 결과 대신 판단으로 넘겼다는 뜻이다.
  - 기록줄은 git이 기록줄로 읽는 블록만 본다. 모든 곳에서 같은 파서(`%(trailers:key=…,valueonly)`)로 읽고 값을 정확히 비교한다. 이력과 되돌린 쪽 판정은 필요한 이력의 기록줄을 이 파서로 한꺼번에 읽는다(13.4). `Ait-Request`·`Ait-Agent`·`Ait-Session` 중 하나라도 없거나 값이 둘 이상이면 그 커밋은 요청 커밋이 아니다.
  - 기록줄은 누가 요청했는지(되돌린 쪽 1.5d, 알림의 발신과 받는 쪽 9.3)를 찾는 데만 쓴다. 계약의 상태와 확인, 받았는지는 레코드의 판으로 판정한다(규칙 1, 2.2.4).
- 5.5 **공개**:
  1. 명령을 시작할 때(후보를 만들기 전) `git symbolic-ref HEAD`로 얻은 ref R(`refs/heads/B`, B는 브랜치 이름)과 HEAD OID H를 끝까지 그대로 쓴다. 후보를 만들 때 읽은 index의 판도 적어 둔다.
  2. `git commit-tree T -p H`로 C를 만든다. H가 없으면(커밋이 없는 브랜치의 `init`) 부모 없이 만든다.
  3. `git update-ref R C H`를 실행한다(H가 없으면 old value에 0 OID를 주어 R이 없을 때만 만든다). 이 CAS가 실패하면 `E_HEAD_MOVED`이고 아무것도 바뀌지 않는다.
  4. `worktrees/<KEY>.lock`을 잡고, `git symbolic-ref HEAD`가 R이고 R이 여전히 C인지 본다. 그러면 H→C나 H→U(후보)에서 바뀐 경로를 `git read-tree -i -m O C`로 index에 맞춘다(`noop`일 때도 같다). O는 C에 그 경로들의 1에서 적어 둔 index 판을 놓은 tree다. intent-to-add 항목은 git처럼 stage되지 않은 것이라, C가 그 경로를 빼면 O도 빼서 git commit처럼 그 항목을 남긴다. 바뀐 경로와 O는 3의 공개 전에 만들어 두므로, 공개 뒤에는 git의 단계만 남고 그 실패는 5.5-5의 경고다. 그래서 git의 two-way merge대로, 그사이 다른 쪽이 stage한 경로는 그대로 두고, 그 경로를 이 요청도 바꾸어 둘이 어긋나면 index를 통째로 두고 경고한다(5.5-5). 맞춘 뒤 git이 쓴 파일의 stat 정보를 index에 새로 적는다(`git update-index --refresh`). 맞춘 경로 중 skip-worktree가 아닌 `.ait/` 경로는 작업 폴더도 맞춘다. 작업 폴더의 다른 파일은 건드리지 않는다. sparse checkout(`core.sparseCheckout`이 true이고 패턴 파일 `info/sparse-checkout`이 있을 때. 패턴 파일이 없으면 git도 sparse로 다루지 않는다)이면 맞춘 뒤 `git update-index --refresh`와 `git sparse-checkout reapply`로 패턴 밖의 경로를 작업 폴더에서 다시 빼고 skip-worktree 표시를 되살린다(git이 패턴 밖 경로를 다루는 대로다). 맞춘 뒤 HEAD가 여전히 R인지 다시 보고 잠금을 놓는다.
  5. 4를 건너뛰거나 다 하지 못해도(조건이 맞지 않음, 갱신 실패, 그사이 바뀐 경로) 요청은 성공으로 처리하고 `warning: committed 'C' to 'R', but the index and work tree were not updated`(일부만 맞췄으면 그 경로)를 쓴다. 지금 HEAD가 R이고 R이 C면 맞추지 못한 경로에 대해 hint `bring 'PATH...' to 'C' in the index and work tree`(`.ait/` 경로)와 `bring 'PATH...' to 'C' in the index`(나머지 경로)를 쓴다(없는 쪽은 쓰지 않는다). 아니면 다른 작업이 그 위에 있으므로 쓰지 않는다.
  6. `contract pull`의 공개도 같다. CAS는 3과 같고, 4에서 맞추는 경로는 `.ait/`만이 아니라 pull이 쓰는 경로(7.12) 모두다. git merge처럼 그 경로는 작업 폴더도 맞추고, 5의 hint에서는 `.ait/` 경로와 같은 꼴이다.
- 5.6 **출력**(규칙 7): git의 선례를 그대로 따른다.
  - 결과와 판정은 stdout에 `key value` 행(porcelain)으로 쓴다. OID는 출력 어디서나 줄이지 않은 전체 OID로 쓴다.
  - 진단은 stderr에 쓰고, 프로그램 이름이나 기호 코드를 앞에 붙이지 않는다. `fatal: MESSAGE`(시작할 수 없거나 입력이 틀렸다, 128), `error: MESSAGE`(규칙에 막혀 거절했다, 1), `error: MESSAGE` 다음 `usage: ait …`(문법 오류, 129), `warning: MESSAGE`(계속 진행하며 알린다: 규칙 6의 위험, 또는 명령이 다 하지 못한 일과 의심스러운 입력), `hint: MESSAGE`(다음 명령, 5.9a).
  - MESSAGE는 영어 한 행이다. 소문자로 시작하고 마침표로 끝내지 않는다. 대상(ID, 경로, 범위, 과제)은 `'…'`로 감싸 그대로 쓴다. 무엇이, 어느 대상에서, 왜 안 되는지를 쓴다.
  - 5.8의 첫 행 문구와 이 규격이 정한 경고 문구는 규범이다. hint는 명령과 ID가 규범이고 나머지 문구는 규범이 아니다.
- 5.7 **종료 코드**(git과 같다): 0 성공(no-op 포함), 1 규칙에 막힌 거절, 128 시작할 수 없거나 입력이 틀림, 129 문법 오류. git 통과와, 검사를 지나 git을 실행한 `contract push`는 git의 종료 코드를 그대로 쓴다. `contract pull`이 코드 충돌로 멈추면 git merge와 같은 1이다(7.12.6). `session open`은 성공하면 SID 한 행을 쓰고 0. 인자 없이 실행하면 git처럼 사용법을 쓰고 1이다(7.14.1). 출력이나 실행 상태 파일을 쓰지 못하면 git처럼 끝난다: EPIPE면 SIGPIPE를 기본 동작으로 되돌려 다시 일으키고(종료 상태 141), 그 밖에는 `E_IO`다.
- 5.8 **오류 종류**(이것이 전부다). 이름은 규격 안에서 부르는 이름이고 출력하지 않는다. porcelain 낱말은 stdout 보고(`invalid WORD …`, 7.7.6)에 쓴다. hint는 5.9a의 원칙을 따르고, 대문자 이름은 실제 값으로 채운다.

| 이름 | 종료 | 첫 행 | porcelain | hint |
| --- | --- | --- | --- | --- |
| `E_USAGE` | 129 | `error: …` 다음 행 `usage: ait …` | `usage` | alias를 그 명령 밖에서 썼으면 `aliases are local to one command; use the ID printed on its 'new' line` |
| `E_READ` | 128 | `fatal: could not read '…'`(인자로 준 파일 `-F`·`-r`을 읽을 수 없다. git과 같다) | `read` | 없다(판정은 읽지 못했다는 것만 안다. 5.9a 판정이 아는 것만) |
| `E_SESSION` | 128 | `fatal: no usable ait session: …` | `session` | `ait session list`(agent가 내 이름인 session을 찾는다), 없으면 `ait session open --agent <name>`. 다른 worktree의 session이면 `cd 'PATH'`(그 session의 worktree)와 그 명령. 그 worktree가 없으면(지워졌거나 `git worktree list`에 없다) `its worktree is gone; close it: 'ait session close S…', then open a session with the same name in the worktree of your branch: 'ait session open --agent NAME'`(NAME은 그 session의 agent. 이름이 같아야 내 약속과 요구가 이어진다, 7.2.1) |
| `E_BUSY` | 128 | `fatal: cannot run now: …`(진행 중인 merge 등, detached·unborn HEAD, 이미 도는 listener: `a listener is already running for session 'S…'`) | `busy` | 진행 중인 git 조작(5.2-2)이면 `a git OP is in progress; finish or abort it as 'git status' says, then rerun this command`(OP는 `git am`이면(5.2-2) am, 그 밖의 rebase 디렉터리가 있으면 rebase, MERGE_HEAD가 있으면 merge, 그 밖에는 CHERRY_PICK_HEAD·REVERT_HEAD나 `sequencer/todo`의 첫 낱말로 cherry-pick·revert), 이어서 5.9a의 조작 안내다. 다시 실행할 때의 상태는 그 조작이 끝난 뒤의 것이므로 그 밖의 로컬 변경 hint는 붙이지 않는다. detached HEAD는 `run this command with HEAD on a branch`(ait는 브랜치에만 기록한다), 커밋이 없는 브랜치는 `run 'ait init' first`(init 커밋이 첫 커밋이 된다, 7.1.1-5), `listen`이면 `read the running listener's output`, `session close`면 `stop the listener first: kill PID`(4.5) |
| `E_DIRTY` | 1 | `error: uncommitted changes in .ait/` 또는 `error: uncommitted changes in the index`. `contract pull`이 쓸 경로면 `error: your local changes to 'PATH' would be overwritten by this pull`, 추적하지 않는 파일이면 `error: untracked working tree files would be overwritten by this pull: 'PATH'`(git과 같은 꼴, 7.12.0) | `dirty` | `.ait/`면 남기려면 `ait contract add PATH...`와 `ait contract commit -m <why>`, 버리려면 `or drop them: restore '.ait/' to HEAD's version and remove the new files there`. index면 `first commit or set aside your changes, then rerun this command`. pull이 쓸 경로면, 계약 경로(6.1)는 `commit your changes with 'ait contract add PATH' and 'ait contract commit -m <why>', or set them aside, before you pull`, 코드는 `commit or set aside your changes to 'PATH' before you pull`, 추적하지 않는 파일은 `move or remove 'PATH' before you pull` |
| `E_ID` | 128 | `fatal: no such contract: …`(없는 ID·alias, 역할 불일치) | `id` | 다른 ref에 있으면 그것을 들이는 명령(5.9a의 들이기)과 `then rerun this command`. alias 모양이면 E_USAGE와 같은 alias hint |
| `E_STATE` | 1 | `error: not allowed: …`(허용되지 않은 전이, 구조 검증 d, 통합에서 출발 판에서 갈 수 없는 레코드(2.3.2), 내보낼 미확인이 있는 push, 이미 있는 경로, ait가 쓰지 않는 hook) | `state` | 통합이면 7.1.3-7의 hint. push의 미확인이면 계약마다 H7·H8, 그리고 그 계약들의 provider를 모두 찾을 수 있을 때만 `or, if it is trivial, take responsibility: 'ait contract push REMOTE --force -m <why>'`(REMOTE는 이 push의 remote). 관문·fulfill의 거절이면 5.9a의 전이 hint. push·pre-push가 우회로 생긴 5.9 d 오류에 막히면 5.10(pre-push는 7.1.6), pull이면 7.12.4. `init`의 hook이면 7.1.1-1 |
| `E_SCOPE` | 128 | `fatal: bad scope: …` | `scope` | current가 되는 추상 계약에 범위가 없으면, 같은 명령에 그 계약을 `ID=<scope>`로 바꾸거나 더한 것. 범위의 PATH가 그 tree(3.3의 HEAD나 T0)에 없으면: future 계약의 provider나 demand의 consumer면 같은 명령에서 그 PATH의 범위들을 `new:PATH` 하나로 바꾼 것(`new:`는 파일 전체다, 3.2. current D를 되돌리는 절이면 5.9 d의 O…도 함께), current가 되는 계약이면 5.8 `E_CODE_REQUIRED`의 "작업 폴더에서 찾을 수 있으면"과 같은 형(이행·commit은 stage, 관문은 코드 먼저나 약속으로 하기) |
| `E_FORMAT` | 128 | `fatal: bad format: …`(파일과 행) | `format` | 통합 hook이면 7.1.3-7의 hint. `.ait/` 아래 파일이면 5.10의 hint. 결과 파일이면 `fix 'RES': every task needs an outcome (pass, fail or unknown), and a pass needs a basis` |
| `E_INCOMPLETE` | 1 | `error: spec files do not match the contract: …` | `incomplete` | 도우미에 준 명세 정의가 없으면 `ait contract new spec PATH exec|prompt`. `unstaged` 행이 있으면 `stage them together: 'ait contract add PATH...'`(PATH…는 이 명령의 PATH에 `unstaged` 행의 경로를 더하고, 그로 닿는 계약의 관련 경로를 7.5.2를 지날 때까지 더한 것). `missing` 행이 있으면 그 경로가 HEAD에 있을 때 `restore 'PATH' from HEAD, or stop declaring it in 'DECL'`(DECL은 이 명령이 닿은 계약 중 그 경로를 선언한 작업 폴더의 정의나 레코드. 여럿이면 모두), 없을 때 `create it or stop declaring it in 'DECL'`. 부분 stage면 stage하려면 `ait contract add PATH...`, 빼려면 `unstage 'PATH...'`. 깨진 묶음이 HEAD에 있으면(fulfill·commit·push·pre-push·pull이 HEAD 상태의 validate e에 막혔다. pre-push는 7.1.6) `ait contract commit -m <why>`(stage 없이 관문에 넣으면 6.2가 다시 고정한다). 관문이 HEAD에서 다시 펼치지 못하면(선언한 명세 파일이 HEAD에 없거나 일반 파일이 아니다, 2.1.4) 다시 고정할 수 없으므로 hint가 없다(5.9a 판정이 아는 것만). 통합 hook이면 7.1.3-7의 hint. pull이면 7.12.4 |
| `E_MIXED` | 1 | `error: contract changes and code changes are mixed: …` | `mixed` | 관문이면 `unstage the code ('PATH...'), then rerun this command; when the code is ready, stage it and run 'ait commit -m <why>'`(PATH는 `impl` 행의 경로). 이행·commit이면 `unstage the spec files ('PATH...'), then rerun this command; when they are ready, run 'ait contract add PATH...' and 'ait contract commit -m <why>'`(PATH는 `mixed` 행의 경로) |
| `E_EMPTY` | 1 | `error: no contract change to push: …` | `empty` | `push it with git`. 내보낼 커밋에 내보낼 미확인(7.10.1-4의 판정)이 있으면 그 앞에 계약마다 H7·H8 |
| `E_HEAD_MOVED` | 1 | `error: HEAD moved while running; nothing changed` | `head-moved` | `rerun this command` |
| `E_CODE_REQUIRED` | 1 | `error: no code under the contract's scope: …` | `code-required` | `contract push --force`(7.10.1-4)면 계약마다 H8. 그 밖에는 코드가 없는 계약마다 맞는 것을 차례로(형은 5.9a의 고치는 명령): 그 범위를 작업 폴더에서 찾을 수 있으면(stage나 커밋을 하지 않았다), 코드면 이행·commit은 `stage 'PATH...', then rerun this command`, 관문은 코드 먼저(`<your code>` 자리에 그 PATH), 명세 파일이면 명세 파일 고치기. 아직 쓰지 않은 `new:PATH`면 `write 'PATH'`, 이어서 같은 stage 명령. 관문이 새 레코드로 만드는 current 계약이면 약속으로 하기. HEAD에 있는 계약이면 `if it was moved` 형의 범위 옮기기. 되돌리기 hint. 이 명령이 이행하는 O가 떠맡은 D면 놓기. 내가 만든 계약이면 이행·commit에서는, current면 철회 hint, 선택한 future면 `if it is gone for good, run 'ait future cancel ID -m <why>'` |
| `E_NEEDS_RESULTS` | 1 | `error: check results required: …`(과제 수) | `needs-results` | `check each task, fill 'RES', and rerun with -r 'RES'`, `if it is trivial, add --force instead (recorded as forced)`. 이어서 되돌리기 hint(5.9a) |
| `E_CHECK_FAILED` | 1 | `error: check failed: …`(fail·unknown인 과제) | `check-failed` | 과제마다(형은 5.9a의 고치는 명령): fail이면 provider의 조각 종류마다, 코드면 이행·commit은 `make the code meet the failed task(s) and stage it, then rerun without -r`, 관문은 코드 먼저, 명세 파일이면 명세 파일 고치기(어느 조각 때문인지 ait는 모르므로 둘 다 쓴다). unknown이면 `check it where its environment is available (see 'REQ') and rerun with -r 'RES'`와 `if it is trivial, add --force (recorded as forced)`. 이어서 그 과제의 계약 K마다: 관문이 새 레코드로 만드는 current K면 약속으로 하기. 되돌리기 hint. 이 명령이 이행하는 O가 떠맡은 D면 놓기. 이행·commit에서 K가 HEAD에서 current면 `to change its condition, reopen it with the new one: 'ait future amend K - - <new defs> -m <why>', then rerun this command`(offer면 `K - <new defs>`, 5.9 d의 O…도 넣는다. 10.5) |
| `E_GUARD` | 1 | `error: contract files cannot be changed by a plain commit: …` | `guard` | 막힌 경로만 stage됐으면 `commit them through ait: 'ait contract add PATH...' and 'ait contract commit -m <why>'`. 다른 경로도 stage됐으면 `unstage 'PATH...' and commit the rest; then commit them through ait: 'ait contract add PATH...' and 'ait contract commit -m <why>'`(PATH는 막힌 경로). 다른 경로도 커밋하려 했는데 hook이 받은 index가 이 worktree의 index가 아니면(`git commit -a`나 경로를 준 commit은 임시 index로 커밋한다. 막힌 경로만이면 위의 첫 형이다, 결정 50) 빼도 듣지 않으므로 대신 `commit without -a or paths: stage the code ('CODE...') and commit it, then commit them through ait: 'ait contract add PATH...' and 'ait contract commit -m <why>'`(CODE는 막히지 않은 경로). |
| `E_DIVERGED` | 1 | `error: 'D' has diverged between 'C1' (REF) and 'HEAD'`(`contract pull`에서 고를 판이 둘 이상이다. 고를 판의 커밋을 모두 쓴다, 7.12.1) | `diverged` | 고르기: `pick one: 'ait contract pull D=C1 -m <why>' or 'ait contract pull D=HEAD -m <why>'`(7.12.1) |
| `E_PICK` | 1 | `error: 'C' has no version of 'D' to choose: a newer one includes it`(`K=C`의 C가 고를 판이 아니다) | `pick` | 갈라져 있으면 고르기 hint, 아니면 `take it as it is: 'ait contract pull D'` |
| `E_ACCIDENT` | 1 | `error: 'D' at HEAD was changed outside ait's rules`(HEAD의 사고 판, 2.3.3), `error: 'D' at 'REF' (C) was changed outside ait's rules`(pull이 받을 판이 사고 판·형식이 틀린 판뿐이다), `error: taking 'C' needs 'D' as 'REF' has it, but 'D' at 'REF' (C2) was changed outside ait's rules`(pull의 받는 결과의 구조 위반이 빠진 사고 판에서 온다) | `accident` | HEAD면 5.10의 복원. 다른 브랜치면 `'D' has to be fixed on branch 'REF' first` |
| `E_UNNAMED` | 1 | `error: taking 'C' also changes contracts you did not name: 'Q'`(7.12.3) | `unnamed` | `name them too: 'ait contract pull D Q'` |
| `E_SPLIT` | 1 | `error: 'C1' and 'C2' both change 'PATH', which you changed too`, 나눠 받을 수 없으면 `error: 'C1' and 'C2' can only be taken together, but both change 'PATH', which you changed too`(한 파일이 두 번 충돌한다, 7.12.4) | `split` | 나눠 받는 명령들, 아니면 받을 수 있게 되는 상태(7.12.4) |
| `E_UNCHECKED` | 1 | `error: taking 'C' changes what contract 'K' checked: 'PATH'`, `error: taking 'C' merges your changes into what 'K' checked: 'PATH'`, `error: 'K' at 'C' checked code your branch no longer has: 'PATH'`, `error: taking 'C' conflicts in what 'K' checked: 'PATH'`, `error: --force cannot take a pull whose code conflicts: 'PATH'`(받는 결과가 새 미확인이나 확인 없는 명세 변경을 만든다, 7.12.5) | `unchecked` | 되돌리기 절을 더한 pull, 이어서 `--force`(7.12.5) |
| `E_SHALLOW` | 128 | `fatal: 'C' is beyond the shallow boundary of this clone`(들일 커밋이나 그 merge base가 이 clone에 없다) | `shallow` | `'C' can be taken once this clone has its history down to the merge base with HEAD` |
| `E_GIT` | 128 | `fatal: git failed: …`(git 명령과 그 stderr 첫 행) | `git` | git 저장소 안이 아니면 `run this command inside a git repository` |
| `E_IO` | 128 | `fatal: write error: …`(쓰지 못한 파일이나 `standard output`과 그 이유. git과 같다) | `io` | `make 'PATH' writable (free space, permissions), then rerun this command`(PATH는 쓰지 못한 파일이고, 그 파일을 만들지 못했으면 그 디렉터리다. listener면 `then start 'ait listen' again`). `standard output`이면 고칠 것이 이 명령 밖에 있으므로 hint를 쓰지 않는다. 공개(5.5) 뒤의 실패는 5.5-5의 경고이므로 여기에 오지 않는다 |

- 5.9 **구조 검증** `validate(state)`(규칙 1·2·3·5). 상태 하나만 보고 판정하는 불변식이다. 부모와 비교하는 판정(전이, creator)은 6.3과 7.1.3이 한다. `.ait/init`은 2.1.2a로만 본다.

| 항목 | 조건 | 코드 |
| --- | --- | --- |
| a | 정규형이고, 파일 이름의 ID와 역할이 일치한다. state는 역할에 맞는 값이다(D·O: future·current·cancelled·withdrawn, X: future·fulfilled·cancelled, 연결: undertaken·released). D·O에는 `def` 행이 하나 이상 있다. consumer는 추상 범위가 아니다. current D·O의 provider 행은 모두 `PATH BLOB`이나 `PATH:S-E BLOB`이다(1.5c) | `E_FORMAT` |
| b | drop의 target이 존재하는 D·O다 | `E_FORMAT` |
| c | 연결의 두 끝이 존재하는 D와 O다 | `E_FORMAT` |
| d | fulfilled X의 target은 withdrawn이고, withdrawn D·O는 그것을 겨눈 fulfilled X가 있다(둘은 한 커밋에서 함께 바뀐다, 2.3). undertaken 연결의 O가 current이면 D는 future가 아니다(1.6) | `E_STATE` |
| e | 묶음 무결성(2.4) | `E_INCOMPLETE` |

  그래서 한 계약을 여러 drop이 겨누거나 여러 offer가 떠맡는 것은 상태로서 틀리지 않는다. 먼저 이행된 drop이 target을 철회하면, 남은 drop은 이행할 수 없게 되고(2.3. target이 current가 아니다) 그 creator에게 H2가 간다.

- 5.9a **다음 명령(hint)**(규칙 7). 모든 hint는 아래 원칙을 따른다.
  - **판정이 아는 것만**: hint는 그것을 쓰는 판정(거절·경고)이 정한 이유로 고르고, 그 판정이 아는 것만 말한다. 주는 명령이 어떻게 판정될지(결과를 요구할지 등)는 그 명령이 스스로 알린다. git도 거절 이유로 advice를 고르고(git v2.53.0 builtin/push.c:393), 권하는 명령의 결과를 말하지 않는다(사용자 결정 52).
  - **판정 시점**: hint는 쓰는 순간의 상태로 판정한다. 알림이면 일어난 일은 사건 커밋 C로, 받는 쪽과의 관계와 할 일은 받는 worktree의 HEAD로 본다. 명령이면 그 명령이 판정한 tree(보고면 T0, 커밋하면 그 커밋, push면 H, pull이면 받는 결과(멈췄으면 아무것도 바꾸기 전의 HEAD), hook이면 index)로 본다. 지금 맞지 않으면 쓰지 않는다.
  - **인자**: 명령은 그것이 실행될 tree 기준의 값을 넣어 그대로 실행할 수 있게 쓴다(amend의 범위는 HEAD, fulfill·commit의 `=SCOPES`는 T0). ait가 아는 값(ID, 범위, 경로, 정의, 결과 틀)은 모두 채운다. 범위는 표시(3.8)의 `?` 표지 없이 쓰고, 지금 tree에서 새 입력으로 유효한 형으로 바꾼다(`new:PATH`인데 파일이 있으면 `PATH`, 찾을 수 없는 범위는 `<scope>`). 옮긴 계약의 새 범위는 `KEPT,<new scope>`로 쓴다. KEPT는 그 명령이 범위를 해석할 tree(amend는 HEAD, fulfill·commit은 T0. 알림은 받는 쪽의 T0를 모르므로 HEAD)에서 찾을 수 있는 그 계약의 provider 조각이고, 없으면 `<new scope>`만 쓴다. 사람이 정할 것과 앞선 명령이 출력할 값만 `<why>`·`<new scope>`·`<your code>`·`<X>`처럼 `<…>`로 남긴다. hint가 명령에 넣는 alias(`o`·`x`)는 그 명령이 이미 쓰는 alias와 겹치지 않는 이름으로 고른다(한 요청 안에서 alias는 겹칠 수 없다, 1.9). 채운 값에 셸이 특별히 읽는 글자가 있으면 POSIX 셸 인용으로 쓴다(작은따옴표로 감싸고 안의 `'`는 `'\''`로). `<…>`는 바꿔 넣을 자리이므로 그대로 쓴다. **hint가 주는 git 명령은 마치는 커밋 하나뿐이다**(7.1.3). 파일을 되돌리거나 지우거나 stage하는 것처럼 git으로 하는 일은 명령 대신 이루어질 상태로 쓴다: `restore 'PATH...' as in 'SRC'`, `remove 'PATH...'`(둘 다 index와 작업 폴더에서), `stage 'PATH...'`, `unstage 'PATH...'`. 그 일을 어떻게 할지는 git의 일이기 때문이다. hint 속의 경로는 최상위 기준이고(3.1), ait 명령은 cwd와 무관하다. 계약 경로(6.1)는 `ait contract add PATH...`로 stage하고 이어서 `ait contract commit -m <why>`로, 코드는 `stage 'PATH...'`로 쓰고 이어서 `ait commit -m <why>`로 커밋한다(**stage 규칙**, 규칙 3). 통합 hook이 그 통합을 마치게 하는 hint에서는 ait의 쓰기 명령이 돌지 않으므로(5.2-2) 계약 경로도 `stage 'PATH...'`로 쓰고 커밋은 마치는 커밋이다. 통합 hook이 그 커밋을 정리한다. 그 밖의 hint는 통합을 마친 뒤의 일이다(아래의 진행 중인 git 조작). 되돌리기(`future amend K`)는 K가 current O가 떠맡은 current D면 그 O…도 같은 요청에서 되돌린다(5.9 d). 현재 session이 이 worktree의 기본 session이 아니면 ait 명령과 마치는 커밋 앞에 `AIT_SESSION=SID `를 붙인다. demand와 offer의 amend 인자는 다르다(demand `ID - SCOPE -`, offer `ID SCOPE -`). 아래 표는 offer 형으로 적는다.
  - **기록**: 기록하는 명령에는 `-m <why>`가 붙는다(pull은 고르거나 절을 더하거나 `--force`일 때, 7.12.1). `-r`는 붙이지 않는다: 결과가 필요하면 그 명령의 출력(`E_NEEDS_RESULTS`)이 과제와 내는 법(`-r`, 사소하면 `--force`)을 준다.
  - **남의 계약**: **내가 만든 계약**은 creator가 현재 session의 agent인 계약이다. 거두는 명령(cancel·drop)은 그 계약이, 놓는 명령(release)은 그 offer가 내가 만든 계약일 때만 쓴다. 내가 되돌려 내 약속이 된 남의 계약도 거두거나 놓게 하지 않는다. 바뀐 계약을 옛 판으로 복원해 우기게 하지 않는다(전제 1).
  - **들이기**: hint 명령이 다룰 것(인자인 계약·범위의 파일·명세 정의, 그 명령이 보여 주거나 바꿀 상태)이 보는 쪽 HEAD에 없으면, 그것을 들이는 명령을 앞에 붙인다. 바뀐 상태를 기준으로 일하려면 그 상태가 HEAD에 있어야 하기 때문이다(전제 1). 들이는 명령은 그 계약 K를 지정한 `ait contract pull K`다. 갈라져 있으면 7.12.1의 고르기 hint다. 그 pull이 이름을 더 요구하거나(7.12.3) 그 밖에 멈추면 그 pull이 hint로 준다(사용자 결정 54). HEAD에 있는지는 판의 포함으로 본다: K의 최신 판(7.12의 낱말)이 모두 HEAD 판에 포함되면 HEAD에 있다.
  - **로컬 변경**: hint는 그 명령이 로컬 변경(stage된 코드나 명세 파일, `.ait/`의 바뀐 것) 때문에 거절될지를 미리 맞히지 않는다. 그 명령이 `E_DIRTY`·`E_MIXED`로 스스로 알린다(판정이 아는 것만, 사용자 결정 52. 반례: 거절된 contract commit이 남긴 stage 위의 todo hint).
  - **진행 중인 git 조작**: hint의 ait 쓰기 명령은 진행 중인 git 조작(5.2-2)이 있으면 `E_BUSY`에 막히고, 들이는 hint는 그 조작에 섞이므로 그 앞에 `first finish or abort the git OP in progress ('git status')`(OP는 `E_BUSY`와 같다)와 조작 안내를 쓴다. **조작 안내**는 통합 상태(7.1.3)면 그 상태의 마치는 hint다(git의 안내만 따르면 hook을 건너뛴다). 그리고 **충돌 경로**(index에 unmerged로 남은 경로)가 아닌 계약 경로에, git이 hook 없이 커밋할 바뀐 것(통합 상태면 stage하지 않은 것, 그 밖에는 index나 작업 폴더의 것)이 있으면 이어서 `; keep your contract changes out of it, since only ait commits contract files`다. 통합 hook이 그 통합을 마치게 하는 hint와 merge driver의 hint에는 붙이지 않는다. hook의 hint는 그 커밋 뒤의 상태로 본다: merge는 그 커밋으로 끝나고, rebase와 남은 고르기는 이어진다.
  - **고치는 명령**: 결과가 필요한 명령이 확인 집합 때문에 거절되면(`E_CODE_REQUIRED`·`E_NEEDS_RESULTS`·`E_CHECK_FAILED`), 거절된 요청은 index와 작업 폴더에 그대로 남는다. 그래서 고치는 명령은 그 상태에서 그대로 되는 것으로 쓴다. 관문의 `E_STATE`도 같은 자리에서 고친다(5.8). provider가 코드 조각과 명세 파일 조각을 함께 가지면 조각의 종류마다 그 형을 쓴다. `then rerun this command`는 다시 실행할 명령이 이 명령과 같을 때의 형이다. 앞선 도우미가 확인 집합에서 뺀 계약(되돌리거나 놓은 계약)에 이 명령이 `ID=SCOPES`를 주었으면 그것을 뺀 명령으로 `then rerun 'RERUN'`을 쓴다(RERUN은 그 명령)(그대로 두면 `E_SCOPE`이거나 그 계약이 선택한 대상이 된다, 7.7.1). 되돌리기와 놓기는 그렇게 한 뒤 다시 실행할 명령의 확인 집합(1.5b, 7.7.2)에서 그 계약이 빠질 때만 쓴다.
    - **계약을 고치는 것**(아래의 되돌리기, 범위 옮기기, 약속으로 하기, 명세 파일 고치기)은 관문이 거절했으면 그 요청 안에서 한다. 관문의 요청은 계약을 바꾸는 요청이고, contract commit의 요청은 `.ait/`를 차지할 수 있어 도우미를 따로 실행할 수 없기 때문이다(5.2-3). 도우미면 hint는 같은 명령에서 그 계약의 절을 바꾸거나 절을 더한 명령이다(`E_SCOPE`와 같다. 옵션은 `-m <why>`만 남긴다). contract commit이면 그 파일을 고치는 일과 `then 'ait contract add PATH...' and 'ait contract commit -m <why>'`다. 이행·commit이 거절했으면 도우미 명령을 먼저 실행하고 `then rerun this command`를 붙인다. 도우미는 stage된 변경을 커밋하지 않기 때문이다(7.3.4). 다시 실행하면 새 과제를 받는다.
      - 되돌리기 K: 도우미는 K의 `amend`·`future cancel` 절을 `future amend`로 바꾸거나 `future amend K` 절을 더한다. contract commit은 `set 'state future' in '.ait/K'`. K가 current O…가 떠맡은 current D면 O…도 같은 방식으로 되돌린다(5.9 d). 따로 실행할 때는 `ait future amend K -m <why>`다.
      - 범위 옮기기 K: 도우미는 K의 절의 provider 인자를 `KEPT,<new scope>`로 바꾸거나 `amend K KEPT,<new scope> -` 절을 더한다. contract commit은 `set the 'provider' lines of '.ait/K' to KEPT and <new scope>`. 이행·commit은 도우미 대신 같은 명령에 `K=KEPT,<new scope>`를 준다(7.7.1): `if it was moved, rerun with 'K=KEPT,<new scope>'`.
      - 약속으로 하기(관문이 새 레코드로 만드는 current K): 도우미는 K를 만드는 `demand`·`offer` 절을 `future demand`·`future offer`로 바꾼다. contract commit은 `set 'state future' in '.ait/K'`. 그 K가 D이고 이 요청에서 current O…에 떠맡기면 O…도 future로 한다(5.9 d): 새 O면 그 `offer` 절을 `future offer`로, HEAD의 O면 `future amend O` 절을 더하고, contract commit은 `.ait/O…`에도 `state future`를 쓴다. 아직 성립하지 않은 조건은 약속으로 걸고 코드와 함께 이행한다(10.1a). hint는 `if it does not hold yet, make it future: …`다.
      - 명세 파일 고치기(provider가 명세 파일 PATH): 고친 PATH를 요청에 싣는다. contract commit은 `ait contract add PATH`다. 도우미는 이 요청이 PATH를 이미 싣고 있으면(FILES나 그 선언) `fix 'PATH', then rerun this command`(고친 뒤 index 판이 따로 남아 부분 stage가 되면 다시 실행한 명령이 알린다, 7.3.3), 아니면 PATH를 선언한 HEAD의 future·current D·O 중 ID가 가장 작은 OWNER의 `amend OWNER - - DEFS` 절(DEFS는 OWNER의 명세 정의, offer면 `amend OWNER - DEFS`)을 더한 `fix 'PATH' and carry it in this request: '…'`이다. 이행·commit은 그 절을 먼저 도우미로 실행한다: `fix 'PATH' and commit it first: 'ait amend OWNER - - DEFS -m <why>', then rerun this command`. 6.2가 PATH를 선언한 나머지 계약도 다시 고정한다.
      - 놓기(이 명령이 이행하는 O가 떠맡은 D가 막고, O가 내가 만든 계약일 때): `to fulfill 'O' without 'D', release it first: 'ait release D O -m <why>', then rerun this command`. 놓으면 그 D는 O의 이행에서 빠진다(1.6). D가 걸린 계약이거나 이 명령이 이행하는 다른 O도 떠맡았으면 빠지지 않으므로 쓰지 않는다.
    - **코드를 고치는 것**: 이행·commit이면 stage된 코드가 확인 대상이므로 고쳐서 stage하고 다시 실행한다. 관문이면 코드는 요청에 넣을 수 없으므로(규칙 3) **코드 먼저**다: `commit code that meets it first: stage <your code> and run 'ait commit -m <why>', then rerun this command`. contract commit에는 이 형을 쓰지 않는다. 그 요청이 stage해 둔 `.ait/`와 명세 파일 때문에 `ait commit`이 받지 않기 때문이다(5.2-3, 7.7.3-2). 코드 먼저인 커밋은 HEAD의 조건으로 확인되므로, 새 조건과 함께 지금 조건도 지키는 코드여야 한다.
  - **되돌리기 hint**: `to change 'K' without a check, reopen it: …`(명령은 위의 되돌리기). 고치는 명령에서는 거절의 대상인 계약(`E_CODE_REQUIRED`는 코드가 없는 계약, `E_NEEDS_RESULTS`는 확인 집합, `E_CHECK_FAILED`는 fail·unknown인 과제의 계약) 중 HEAD에서 current인 것마다 쓴다. 다만 이 명령이 이행하는 O가 떠맡은 D는 되돌려도 그 이행이 다시 확인하므로(규칙 5) 쓰지 않는다(위의 "빠질 때만"). H8과 H13에서는 따로 실행하는 형이다.
  - **철회 hint**(내가 만든 current K): `if it is gone for good, withdraw it: 'ait future drop K as x -m <why>', then, with nothing staged, 'ait fulfill <X> -m <why>'`(`<X>`는 그 drop의 `new` 행이 출력할 ID). 관문이 거절했으면 그 요청 안에서: 도우미는 그 `future cancel K` 절을 `future drop K as x`로 바꾼 명령, contract commit은 `keep 'K' current and add a drop for it in this request: 'ait contract new drop', set 'target K' in it, then 'ait contract add' it`이고, 커밋한 뒤 그 drop을 이행한다. 이행·commit의 `E_CODE_REQUIRED`에서는 이 명령이 커밋하려던 변경과 함께 철회하므로(그 커밋 뒤에 withdrawn인 계약은 확인하지 않는다, 1.5b), 뒤쪽은 `then 'ait fulfill … <X> -m <why>'`(이 명령의 선택한 대상과 `=SCOPES`에 `<X>`를 더한 fulfill)다.
  - **전이 hint**(관문·fulfill의 `E_STATE`, 규칙 2): 거절된 레코드마다, 바라던 결과에 이르는 허용된 길(2.3)을 고치는 명령의 자리(관문이면 그 요청 안, 이행이면 먼저)에 쓴다. 아래에서 처음 맞는 것 하나이고, 거두는 길(cancel·drop)은 그 계약이 내가 만든 계약일 때만 쓴다(남의 계약). `restore it`은 그 레코드를 기준 판(HEAD 판. 5.10의 기준 판이 있으면 그 판)으로 되돌리는 `restore 'PATH' as in 'SRC'`다(SRC는 HEAD 판이면 `HEAD`, 그 밖에는 그 판을 담은 커밋). 기준 판에 없는 레코드면 대신 `remove 'PATH'`다. 명령은 고치는 자리의 형으로 쓴다: 도우미의 거절이면 같은 명령에서 그 절을 바꾸거나 빼거나 더한 명령, contract commit의 거절이면 그 요청 안에서 같은 일을 하는 파일 고치기(state는 `set 'state S' in '.ait/K'`, 새 계약은 `ait contract new ROLE`이 만든 파일에 값을 써서 `ait contract add`), 이행의 거절이면 따로 실행하는 도우미 명령이다(5.9a의 고치는 명령).
    1. HEAD에 이미 있던 구조 오류(이 요청이 만든 것이 아니다): 5.10의 복원, 이어서 `then rerun this command`.
    2. 레코드를 지우거나, role·creator를 바꾸거나, 새 레코드를 future·current가 아닌 state로 썼다: `restore it`. role을 바꾸려던 것이면 이어서 `make a new contract instead`(같은 값의 `ait future demand`·`ait future offer`). creator를 바꾸면서 다른 행도 바꿨으면 그 고친 것을 잃지 않게 `restore it` 대신 `remove the 'creator' line from '.ait/K' (HEAD's value is kept), then 'ait contract add .ait/K' and 'ait contract commit -m <why>'`다(2.2.3).
    3. 끝난(cancelled·withdrawn·fulfilled) K를 바꾸거나 이행하거나, 끝난 D를 떠맡았다: `'K' is closed`. 이어서 D·O면 `to need it again, make a new contract: 'ait future demand <your code> PROVIDER DEFS -m <why>'`(offer면 `ait future offer PROVIDER DEFS`, K의 값. H6과 같다), X면 그 target T가 current이고 내가 만든 계약일 때만 `to withdraw 'T', make a new drop: 'ait future drop T as x -m <why>'`. contract commit이면 그 요청 안의 형이고(D·O는 `to need it again, move your change to a new contract in this request: 'ait contract new ROLE', fill it with LINES, then 'ait contract add' it`(LINES는 요청에 있는 K 판의 consumer·provider 행과 명세 정의(`def`)의 `file` 행을 작성 형식(2.2.3)의 `consumer SCOPE`·`provider SCOPE`·`file PATH`로 쓰고 각각 `'…'`로 감싸 `, `로 이은 것. 범위는 인자 규칙의 형이다. 옛 고정의 행 번호를 그대로 옮기면 다른 행이 고정된다, 3.3), X는 `to withdraw 'T', add a new drop in this request: 'ait contract new drop', set 'target T' in it, then 'ait contract add' it`), 이어서 그 레코드의 `restore it`이다(끝난 판은 바뀌지 않는다. 먼저 복원하면 고친 행을 잃는다).
    4. future D·O를 current로, future X를 fulfilled로 직접 썼다(이행으로만 간다): `fulfill it instead:` 그 레코드에 다른 변경도 있으면 `set 'state future' back in '.ait/K', then 'ait contract add .ait/K' and 'ait contract commit -m <why>'`, 없으면 `restore it`의 명령, 이어서 `then 'ait fulfill K -m <why>'`(기록 규칙을 따른다).
    5. current D·O를 cancelled·withdrawn으로 썼다: 되돌리기 hint, 그리고 내가 만든 계약이면 철회 hint.
    6. future D·O를 withdrawn으로 썼다(철회는 current에만 된다): 내가 만든 계약이면 cancel(도우미는 `future cancel K` 절, contract commit은 `set 'state cancelled' in '.ait/K'`), 아니면 `restore it`.
    7. 이미 current인 K를 이행했다: `'K' is already current; to check it again, run 'ait amend K -m <why>'`(H7과 같은 명령).
    8. current가 아닌 K를 겨누는 drop을 만들거나 target을 그런 K로 바꿨다: `'K' is not current; a drop withdraws a current contract`와 그 drop을 빼는 형(도우미면 그 절을 뺀 명령이고 남는 절이 없으면 쓰지 않는다, contract commit이면 `restore it`), 그리고 K가 내가 만든 future면 `if it is no longer needed, cancel it`과 cancel의 형(6과 같다).
    9. target이 current가 아닌 X를 이행했다: H2의 X 형(X가 내가 만든 계약이 아니면 cancel 명령 없이 앞 문장만).
    10. future D를 current O로, 또는 D를 끝난 O로 떠맡았다: O가 이 요청이 만드는 새 계약이면 약속으로 하기, 아니면 `undertake it with a future offer: 'ait future offer PROVIDER DEFS as o undertake D o -m <why>'`(D의 값. 도우미는 그 `undertake` 절을 이것으로 바꾼 명령). contract commit은 그 요청 안에서: 연결 파일을 `restore it`으로 빼고 `undertake it with a future offer in this request: 'ait contract new offer', fill it with LINES, write 'state undertaken' to '.ait/undertake/D-<O>', then 'ait contract add' them`(`<O>`는 `contract new`가 출력할 ID, LINES는 D의 provider·file 행이고 3과 같은 형).
    11. current O…가 떠맡은 current D를 혼자 되돌렸다: `'D' is undertaken by current 'O…'; reopen them in the same request: …`(O…는 D를 떠맡은 current offer 모두. 명령은 되돌리기).
    12. 그 밖: `restore it`.
  - 할 일이 없으면 쓰지 않는다. 같은 hint는 한 번만 쓴다.

  표의 "보는 쪽"은 규칙 7의 것이다. AGENTS는 되돌린 쪽(1.5d) 모두를 이름순으로, 각각 `'…'`로 감싸 `, `로 이은 것이다.

| # | 보는 쪽 | 일어난 일 | hint |
| --- | --- | --- | --- |
| H1 | D를 떠맡은, 내가 만든 future offer O | D가 current가 되었다(남이 이행) | `if 'O' was only for 'D', run 'ait future cancel O -m <why>'` |
| H2 | D를 떠맡은, 내가 만든 future offer O, K를 겨눈 내가 만든 future drop X | D·K가 cancelled·withdrawn이 되었거나, K가 future로 되돌려졌다 | O(D가 끝났을 때): `if 'O' was only for 'D', run 'ait future cancel O -m <why>'`. X(K가 끝났을 때): `'K' can no longer be withdrawn; run 'ait future cancel X -m <why>'`. X(K가 되돌려졌을 때): `'K' is not current now; 'X' can be fulfilled after 'K' is fulfilled again, or run 'ait future cancel X -m <why>'` |
| H3 | 내 약속 M(1.3)이 K 자신이거나 K를 상대로 둔다, 내 요구(current demand) K, 내 구독인 K | 남이 K의 조건이나 범위를 바꿨다(되돌리며 바꾼 것 포함. 새로 만든 것은 아니다) | M이 K 자신이거나 K를 떠맡은 offer면 M마다 `see the new conditions with 'ait todo M'`(todo가 K의 조건을 보여 준다, 7.8.2), 그 밖에는 `see the new conditions with 'ait peek K'` |
| H4 | D를 떠맡은, 내가 만든 future offer O | 다른 creator의 future offer O2도 D를 떠맡았고(HEAD나 다른 ref), 내 연결이 늦다 | `warning: 'D' is also undertaken by 'O2'`와 `hint: if your work can go on without it, run 'ait future cancel O -m <why>' (or 'ait release D O -m <why>' to keep 'O' for its other conditions)`. 늦은 쪽: 한 연결 커밋이 다른 쪽의 조상이면 자손 쪽, 아니면 author 시각이 늦은 쪽, 같으면 O ID가 큰 쪽. 이른 쪽에는 쓰지 않는다 |
| H5 | 내가 만든 future demand D(되돌린 쪽이 있는 것은 빼고, 1.5d) | D를 떠맡은 future offer가 C에 없고, C에 없는 offer 중 HEAD에서 D를 떠맡은 future offer도 없다(C에 있는 offer의 상태는 C로 본다) | `if you need it now, undertake it yourself: 'ait future offer PROVIDER DEFS as o undertake D o -m <why>'`(D의 provider와 정의) |
| H6 | 내가 만든 demand K(future·current), 내 구독인 남의 계약 K | K가 cancelled·withdrawn이 되었거나, K를 겨눈 drop이 생겼다 | `if your code relies on it, demand it yourself: 'ait future demand <your code> PROVIDER DEFS -m <why>'`(K의 provider와 정의) |
| H7 | 누구든 | K가 HEAD에서 미확인이고 provider를 찾을 수 있다 | `check 'K' again: run 'ait amend K -m <why>'` |
| H8 | 누구든 | K가 HEAD에서 미확인이고 provider를 찾을 수 없다 | `'K' has no code under its scope (unverified since 'OID'); if it was moved, run 'ait amend K KEPT,<new scope> - -m <why>'`, 이어서 되돌리기 hint. K가 내가 만든 계약이면 이어서 철회 hint. OID는 K가 새 미확인이 된 커밋(9.1)이다(알림은 C, 그 밖에는 판정한 tree의 이력에서 가장 최근 것). 그 미확인을 지금 만드는 커밋의 경고(7.1.2, 7.1.5의 `is unverified after this merge`)에서는 OID가 아직 없으므로 `(unverified since 'OID')`를 쓰지 않는다 |
| H9 | 명령을 실행한 쪽 | 명령이 바꾸거나 확인하는 계약에, HEAD 판에 포함되지 않고 뜻(9.2)이 HEAD 판과 다른 최신 판(7.12의 낱말)이 있다 | `warning: 'ID' has changes not in HEAD: 'C' on 'REFNAME'`(C는 그 판의 들이는 커밋(7.12의 낱말), REFNAME은 C를 담은 ref 중 이름순 첫째)와, 그것을 들이는 명령(5.9a의 들이기)에 `to include them`을 붙인 hint |
| H10 | 내가 이행할 약속 P: 내가 되돌린 future D·O, 그리고 되돌린 쪽이 없는, 내가 만든 future offer·drop(1.3, 1.5d) | — | `when ready, stage the code and run 'ait fulfill P -m <why>'`. P가 추상이면 `P=<scope>`, P가 떠맡은 future D 중 추상인 것마다 `D=<scope>`를 붙인다. P의 provider를 판정한 tree(위의 판정 시점)에서 찾을 수 없으면(아직 만들지 않은 `new:PATH`는 빼고) 대신 `'P' has no code under its scope; if it was moved, run 'ait fulfill P=KEPT,<new scope> -m <why>'`이고, P가 내가 만든 계약이면 이어서 `if it is gone for good, run 'ait future cancel P -m <why>'`. P가 drop이면 `when ready, with nothing staged, run 'ait fulfill P -m <why>'`이고 target이 current일 때만 쓴다 |
| H11 | peek이 보여 준 future demand D(provider가 ARG와 겹칠 때. HEAD에 없고 다른 ref에만 있는 D도 elsewhere 블록에서 같다) | — | 아래 중 처음 맞는 것. D를 떠맡은 future offer O가 HEAD에 있고 H10의 대상이면 그런 O마다 H10. 내가 D를 되돌린 쪽에 있으면(1.5d) D에 H10. D가 내가 만든 계약이고 되돌린 쪽이 있으면 H14. D를 떠맡은 future offer O가 HEAD에 있으면, 내가 만들었지만 남이 되돌린 O면 H14, 그 밖의 O면 `'O' undertakes it; develop against it`. 다른 ref에만 있으면 `'O' on 'REFNAME' undertakes it; include it and develop against it`과 그것을 들이는 명령(5.9a의 들이기). D에 되돌린 쪽이 있으면 `'D' was reopened by AGENTS; develop against it`. 어디에도 없으면 `to start it, run 'ait future offer PROVIDER DEFS as o undertake D o -m <why>'`(D가 추상이면 앞에 `if your change implements it,`, D가 HEAD에 없으면 들이기를 앞에 붙인다) |
| H12 | 이 clone | 5.2a의 조건(HEAD에 레코드가 있는데 ait hook이 없는 hook이 있다) | `this clone is not set up for ait; run 'ait init'` |
| H13 | 일반 커밋을 하는 쪽(pre-commit) | stage된 변경으로 미확인이 되는 current K | 7.1.2의 틀대로: 다음부터 `ait commit`을 쓰라는 것, 계약마다 되돌리기 hint, 그리고 이 커밋 뒤에 실행할 H7(찾을 수 없게 되면 H8) |
| H14 | 남이 되돌린, 내가 만든 계약 K(되돌린 쪽이 있고 그 안에 내가 없다, 1.5d) | K가 되돌려졌다 | `'K' was reopened by AGENTS; develop against it, or, if it is no longer needed, run 'ait future cancel K -m <why>'` |
| H15 | 내가 만든 demand D(HEAD에서 future·current) | D가 이행되었다(C에서 future에서 current로, 7.9.5의 `fulfilled`) | `'D' is fulfilled; include it and build on it`과 그것을 들이는 명령(5.9a의 들이기). 들일 것이 HEAD에 있으면(9.4의 `in-head yes`. 판의 포함으로 본다) 쓰지 않는다 |

- 5.10 **틀린 레코드**: 판정하는 상태(관문의 후보 U, fulfill·commit의 T0, 통합의 index, push의 H, pre-push의 OID, pull의 HEAD)에 구조 검증(5.9 a~d)을 어긴 레코드가 있으면 관문, fulfill, 확인할 것이 있는 commit, 통합 hook, push, pre-push, pull은 그 오류로 거절한다(통합 hook은 HEAD에 이미 있던 오류로는 거절하지 않는다, 7.1.3-7). HEAD의 사고 판(2.3.3)과 묶음 무결성(5.9 e, 2.4)의 위반도 HEAD의 오류다. 사고 판이면 `E_ACCIDENT`(`error: 'K' at HEAD was changed outside ait's rules`)이고, hint는 아래의 복원이다. 묶음 무결성 위반은 `E_INCOMPLETE`다. 도우미는 틀린 레코드를 읽을 수 없으므로, 고치는 일은 직접 작성 입구(`contract add`·`contract commit`)로 한다(아래 hint). merge driver는 레코드를 읽어야 하는 단계(7.13.1-2~4)에서 형식이 틀린 레코드를 만나면 5단계(충돌)로 간다. 일반 pre-commit, 확인할 것이 없는 commit, peek, todo, 알림, `contract push`의 커밋 분류는 형식이 틀린 레코드를 건너뛰고 `warning: skipping malformed 'PATH' at 'REV'`를 쓴다(형식이 틀린 레코드는 5.9 a를 어긴 것이다. current가 아닌 판의 확인 행도 그렇다). 레코드가 선언한 정의를 읽지 못하면(없거나 형식이 틀렸다) 그 정의를 읽는 명령은 todo와 hook까지 그 오류(`E_INCOMPLETE`·`E_FORMAT`)로 멈춘다(사용자 결정 53. git이 깨진 설정 파일에서 멈추는 것과 같다). 묶음 무결성(5.9 e)은 고정과 다른 정의를 읽지 않고 그것만으로 어긋난 것으로 보므로, 정의를 고쳐 `ait contract add`·`ait contract commit`하는 길은 막히지 않는다. peek과 todo는 HEAD의 사고 판을 그대로 보여 주고, 첫 행을 `warning:`으로 바꾼 그 오류와 아래의 hint를 쓴다. `contract pull`과 통합 hook은 다른 브랜치의 형식이 틀린 판과 사고 판을 출발 판에서 빼고 같은 꼴로 경고한다(7.1.3, 7.12.1).
  - hint: 경로가 레코드 경로가 아니면(2.1.3, 파일 이름이 ID가 아닌 것 포함) 고칠 수 없으므로 `remove 'PATH'`이고, 그 경로가 HEAD에 있으면 이어서 `then 'ait contract commit -m <why>'`다. `.ait/init`이면 내용이 한 가지뿐이므로 `rewrite '.ait/init' as the single line 'version 2' with mode 100644, then 'ait contract add .ait/init' and 'ait contract commit -m <why>'`다. 레코드의 오류가 이 요청(index나 작업 폴더)에만 있으면 고친 것을 잃지 않게 `fix it in this request (see the error line), then 'ait contract add PATH...' and 'ait contract commit -m <why>'`다(PATH…는 오류가 가리키는 레코드).
  - 오류가 HEAD에 있으면(ait 밖의 커밋이 만든 것이다, 11.1) **얽힌 레코드**를 함께 **GOOD**의 판으로 되돌린다. GOOD은 HEAD에서 첫 부모를 따라 내려가며(`git rev-list --first-parent HEAD`) 처음으로 얽힌 레코드의 판들이 사고 판이 아니고 함께 5.9 a~d를 지나며 그 판들의 묶음 무결성(5.9 e, 2.4)도 그 커밋에서 맞는 커밋이다(없는 레코드는 없음으로 본다. 이 브랜치의 판을 먼저 따르는 것은 7.1.3-7·7.13.1-5와 같다). 얽힌 레코드는 HEAD의 오류들이 가리키는 레코드에서 HEAD 판이나 GOOD 판의 참조(X와 그 target, 연결과 그 D·O)를 양쪽으로 따라 닿는 레코드 전부다. 둘은 서로를 정하므로 묶음이 더 커지지 않을 때까지 GOOD을 다시 구한다. 그래서 GOOD 판으로 둔 묶음은 묶음 밖의 레코드를 참조하지도 참조받지도 않고, 구조 검증은 참조로 이어진 레코드끼리만 보므로 GOOD에서처럼 지난다. GOOD이 있으면 `restore 'PATH...' as in 'GOOD'`(얽힌 레코드 중 HEAD 판이 GOOD 판과 다른 것과, GOOD 판 중 future·current인 D·O가 선언한 명세 파일(2.4.1. GOOD에 있다) 중 index나 작업 폴더에 없는 것)와 `remove 'PATH...'`(GOOD에 없는 얽힌 레코드), 없으면 `remove 'PATH...'`(얽힌 레코드)이고, 이어서 `then 'ait contract commit -m <why>'`다. 이미 없는 경로는 쓰지 않는다.
  - HEAD에 구조 오류가 있으면 관문은 얽힌 레코드에 대해 GOOD의 판(GOOD에 없거나 GOOD이 없으면 없음)을 **기준 판**으로 써서 전이와 creator를 판정하고 범위를 짝짓는다(2.3.1의 HEAD 판 자리와 3.2a의 HEAD 행 자리. 그래서 GOOD 판 그대로인 범위는 다시 고정하지 않는다). 정해진 전이로 생기지 않은 판은 계약의 판이 아니고, 묶음의 다른 레코드도 그 판에 맞춰 바뀌었을 수 있기 때문이다. 얽힌 레코드는 기준 판을 잇는다: 바뀌었는지는 실제 HEAD 판과 prev까지 비교하고(prev만 틀린 판도 고쳐야 한다. 6.2는 prev를 빼고 비교한다), 바뀐 레코드의 prev는 기준 판의 blob이다. prev를 뺀 내용이 기준 판과 같으면 전이는 없다(끝난 계약도 그 판으로 돌아갈 수 있다). 기준 판이 없으면 새 레코드로 본다.
  - pre-push의 hint는 7.1.6이, 통합 hook의 hint는 7.1.3-7이 정한다.
  - `contract pull`은 HEAD의 오류로 0단계에서 멈추고 이 hint를 준다(7.12.0). `contract push`는 자기 validate 단계에서 거절한다. merge driver는 충돌로 처리한다.
- 5.11 **ref 목록**: `refs/heads/*`와 `refs/remotes/*`에서 심볼릭 ref를 뺀 것.
- 5.12 **git 호출 규약**:
  - 시작할 때 git 디렉터리와 toplevel(환경에 `GIT_DIR`·`GIT_WORK_TREE`가 있으면 git이 그것을 따라 찾은 것)과 index 경로를 절대경로로 확정한다. hook이 넘긴 `GIT_INDEX_FILE`이 있으면 절대경로로 바꿔 두고, 후보 index를 읽는 호출에 그 값을 명시해 넘긴다. `-F FILE`(`-`는 제외)과 `-r FILE`의 realpath도 이때 구한다. 없는 `-F`·`-r` 파일은 여기서 `E_READ`(5.13의 1)다. `-r` 파일의 내용은 8에서 읽는다.
  - 그다음 자기 환경에서 `GIT_DIR`, `GIT_WORK_TREE`, `GIT_INDEX_FILE`, `GIT_PREFIX`, `GIT_COMMON_DIR`, `GIT_DIFF_OPTS`를 지운다. 객체 저장소를 가리키는 변수(`GIT_OBJECT_DIRECTORY`, `GIT_ALTERNATE_OBJECT_DIRECTORIES`)는 둔다.
  - 하위 git은 `git --git-dir=<git 디렉터리> --work-tree=<대상 worktree> -C <대상 worktree> -c core.quotePath=false -c trailer.separators=: -c core.commentChar=# -c core.commentString=# …` 형식으로 부른다. git이 hook에 `GIT_DIR`를 넘기듯 시작할 때 확정한 저장소를 그대로 넘기는 것이다(지운 변수로 다시 찾으면 사용자가 가리킨 것과 다른 저장소를 찾을 수 있다). worktree가 없는 bare 저장소(hook만 돈다)는 `--git-dir`만 준다. 환경에 `GIT_LITERAL_PATHSPECS=1`을 둔다. 경로 목록은 `-z`로 받고, 긴 OID 목록은 `--stdin`으로 넘긴다.
  - 해시(확인 입력 8.3, worktree 키 4.6, PINS 5.4, 사건의 키 9.5)는 `git hash-object --stdin`의 출력을 쓴다.
- 5.13 **오류 우선순위**: 아래 순서로 검사하고, 종료 오류는 처음 막힌 것이다. 이행 준비 보고(7.7.6)는 거절되더라도 계산할 수 있는 모든 행을 쓴다.
  1. 문법, 경로 형식(3.1·3.2 위반은 `E_SCOPE`), 인자로 준 파일(`E_READ`)
  2. session(`contract pull`은 7.12.5 다음에 본다. 요청 커밋을 만들 때만 필요하지만 설정된 값의 유효성은 언제나 본다)
  3. 5.2
  4. 절 조건(fulfill은 7.7.3-1·2와 `=SCOPES` 검사 7.7.1, contract commit은 6.1. 결정 48)
  5. 정규화·전이
  6. validate
  7. 확인할 코드: 확인 집합의 provider(7.7.3-4, 관문은 정규화된 T에서, 6.5), 그다음 이행·commit은 이행 후 상태(7.7.4 적용 후)의 validate. 확인할 코드가 없으면 결과를 볼 까닭이 없고, current 후보도 만들 수 없기 때문이다.
  8. 확인 결과(8.4). `ait commit`의 session(5.1)은 이 앞에서 본다.
  9. CAS
- 5.14 **`--force`**: 어디서나 "ait가 막은 것을 내 판단으로 넘긴다"는 뜻이다. 확인을 넘기면 그 판단이 확인 행 `check forced PINS`로 기록된다(2.2.2). 명령마다 넘기는 것은 다음뿐이다.

| 명령 | 넘기는 것 |
| --- | --- |
| 도우미·`contract commit`·`fulfill`·`commit` | 확인 결과. 확인 집합의 계약을 판단으로 확인한다(8.5) |
| `contract push` | 내보낼 미확인(7.10.1-4). 계약 커밋이 있는지(`E_EMPTY`), 상태(validate), 넘길 계약의 provider를 모두 찾을 수 있는지를 먼저 보고, 모두 맞을 때만 그 계약들을 사소하다고 확인하는 요청 커밋 하나(`-m` 필요)를 만들고 올린다. provider를 찾을 수 없는 계약은 사소하다고 판단할 코드가 없으므로 넘기지 않는다 |
| `contract pull` | 확인(`E_UNCHECKED`, 7.12.5). 그 검사에서 막힌 current 계약마다 받는 결과로 다시 고정하고 확인 행을 2.2.2대로 쓴다. 받는 결과에서 코드가 충돌할 pull에는 받지 않는다. 갈라짐, 지정 검사, 구조 검사는 넘기지 않는다 |

## 6. 관문 (조건과 범위를 바꾸는 모든 요청의 단일 검사)

관문은 기준 H(HEAD)와 후보 tree U를 받아 정규화된 tree T를 만든다. 검사에 막히면 거절한다.

- 6.1 **계약 경로만**(규칙 3): H→U에서 바뀐 경로는 모두 **계약 경로**여야 한다. 아니면 `E_MIXED`이고 `impl PATH` 행을 쓴다. 계약 경로는 `.ait/` 아래 경로와, 판정하는 상태들에서 future·current인 D·O가 선언한 명세 파일이다. 관문에서 그 상태는 H와 U다(U의 명세 정의가 새로 선언한 `inputs`·환경 정의 포함).
- 6.2 **정규화**: 정규화한 결과의 본문(2.2.4)이 H 판의 본문과 같으면 그 레코드는 바꾸지 않는다(확인으로 확인 행이 바뀌는 것은 6.5). 다르면 다음과 같이 채운다.

| 필드 | 새 레코드 | 기존 레코드 |
| --- | --- | --- |
| consumer·provider | 3.3으로 고정 | 바뀐 범위만 다시 고정 |
| file | 주어진 명세 정의는 `def`, 그 정의가 선언한 `inputs`와 환경 정의 파일은 `input`으로, 모두 U의 mode·blob으로 고정 | 같은 방식으로 다시 펼쳐, 목록·내용·mode가 바뀐 것만 다시 고정 |
| creator | 현재 session(써 두었어도 덮어쓴다) | H와 같아야 함(생략하면 H 값). 다르면 `E_STATE` |
| check | 없음(current로 만들면 6.5가 쓴다) | H 판의 것(확인하면 6.5가 쓰고, future로 되돌리면 지운다) |
| prev | `-` | H 판 blob |

  - 명세 파일의 mode나 blob이 U에서 고정값과 다른 future·current D·O는 U 값으로 다시 고정한다(원시 편집도 이렇게 복구된다). 끝난 D·O는 파일을 맞추지 않으므로(2.4.1) 다시 펼치지도 고정하지도 않는다.
- 6.3 **전이**: 바뀐 레코드는 2.3의 "관문" 행을 따라야 한다(`E_STATE`).
- 6.4 정규화된 상태에 `validate`를 실행한다.
- 6.5 **확인 결과**(규칙 4): 관문의 확인 집합(1.5b, A→B는 H→T)은 이 요청에서 확인한다. 곧 `-r FILE`로 낸 확인 결과가 필요하다(8장). 과제는 그 계약들의 명세 정의 전부다. 그래서 provider가 이 요청이 바꾼 명세 파일인 current 계약도 걸린 계약으로 확인 집합에 든다(1.5b).
  - **빈 amend**: 바꿀 필드 없이(인자를 생략하거나 모두 `-`로) `amend`한 current D·O는 다시 확인한다. 미확인이던 계약은 다시 고정으로 레코드가 바뀌어 커밋된다(H7). 같은 내용을 같은 방법으로 다시 확인하면 확인 행까지 그대로이므로 레코드가 같다(6.6의 `noop`).

  확인 집합의 provider를 T에서 찾을 수 없는 계약이 있으면 결과를 보기 전에 `E_CODE_REQUIRED`다(1.5b, 5.13-7). 결과가 없거나 확인 입력이 다르면 `E_NEEDS_RESULTS`, fail·unknown이 있으면 `E_CHECK_FAILED`다. 결과 때문에 거절할 때 stdout에 과제마다 `task CONTRACT:PATH exec|prompt` 행과 `check-request PATH`·`check-result PATH` 행을 쓴다(7.7.6과 같다). 확인할 코드가 없어 거절하면 이 행들을 쓰지 않는다(8.4). `--force`면 결과 없이 통과한다(5.14). 통과하면 확인 집합의 provider를 T로 다시 고정하고(7.7.4와 같은 방식), 계약마다 확인 행을 2.2.2의 규칙으로 쓰고, 결과 파일을 메시지 본문에 덧붙인다(8.4).
- 6.6 통과하면 공개(5.5)한다. T가 H tree와 같으면 `noop`을 쓰고 exit 0으로 끝낸다(contract commit은 이어서 7.6.1의 `unstaged` 행을 쓴다)(레코드가 아닌 `.ait/` 경로를 지우는 것도 바뀐 것이다, 5.10).
- 6.7 출력. 같은 종류 안에서는 ID 오름차순이다. 도우미의 `new`만 만든 순서를 따른다.

```
request RID
new ID [ALIAS]
changed ID
undertaken D O
released D O
check ID reported|forced
```

- 6.7a **경고**(stderr): 새로 떠맡은 연결마다 H4. 이 요청이 바꾸거나 확인하는 계약(9.2의 R ∪ 확인 집합)마다 H9. 1.2의 고정되지 않은 파일.
- 6.8 **구독**: 관문은 구독을 바꾸지 않는다. 내 약속과 내 요구는 구독 없이 받는다(9.3). 구독은 에이전트가 `sub`(또는 `listen ARG`)로 더하고 `unsub`으로 지운 것뿐이다(규칙 1).
## 7. 명령

### 7.1 초기화와 git hook

- 7.1.1 `ait init`은 이 clone에서 ait를 쓸 준비를 하고, 저장소에 ait를 쓴다는 init 파일을 남긴다. 다시 실행해도 결과가 같으므로 에이전트는 시작할 때마다 실행한다. 이름은 `git init`과 같지만 git으로 넘기지 않는다(7.14.1). git 저장소 안이 아니면 `E_GIT`이다. 1~4는 HEAD의 상태와 상관없이 먼저 한다.
  1. hooks 디렉터리 DIR(`git rev-parse --git-path hooks`의 절대경로)에 여섯 hook `pre-commit`, `pre-merge-commit`, `commit-msg`, `pre-push`, `post-commit`, `post-merge`를 쓴다(mode 755). 내용은 모두 같은 형태다.

     ```
     #!/bin/sh
     command -v ait >/dev/null 2>&1 || exit 0
     exec ait hook <이름> "$@"
     ```

     **ait hook이 있다**는 것은 DIR의 그 hook 파일이 실행 가능하고(git은 실행 권한이 없는 hook을 부르지 않는다) `ait`·`hook`·`<이름>`이 그 차례로, 사이에 공백·따옴표·쉼표·괄호만 두고 이어진 곳을 가진다는 것이다(`ait`는 낱말 그대로이고 `/path/ait`는 아니다, 결정 49)(셸의 `ait hook pre-commit`과 다른 언어의 `["ait", "hook", "pre-commit"]`이 모두 그렇다. 주석 행(앞 공백 다음이 `#`나 `//`인 행)의 것은 꺼 둔 것이므로 보지 않는다. 5.2a도 이것으로 판정한다). DIR이 이 저장소의 어느 worktree 안이고 git 공통 디렉터리(`git rev-parse --git-common-dir`) 밖이면(`core.hooksPath`가 저장소 안을 가리킨다) **저장소의 hook 디렉터리**다(기본 `.git/hooks`는 git 공통 디렉터리 안이다. linked worktree도 그것을 쓴다). ait는 그 파일이 없고 DIR이 저장소의 hook 디렉터리가 아닐 때만 쓴다. 다른 파일이 있거나 저장소의 hook 디렉터리면 그 파일은 저장소의 사람이나 도구가 관리하므로 쓰지 않는다. 그중 ait hook이 없는 hook마다 `E_STATE`(`error: not allowed: hook 'PATH' is not set up for ait`, PATH는 그 파일의 절대경로)와 hint `make 'PATH' run 'ait hook NAME' with its arguments and its stdin, and stop when that fails; git runs a hook only if it is executable. In a shell hook, these lines after the first line do it ('#!/bin/sh' first if the file is new):`, 이어서 아래의 넣을 행, 끝에 `then rerun this command`를 쓴다. 그 파일을 어떻게 고칠지(셸이 아닌 hook, 꺼 둔 hook, 도구가 만드는 파일)는 그 저장소가 정한다. 그 파일이 이 worktree 안이고 무시되는 경로가 아니면(`git check-ignore`) 저장소에 커밋할 파일이므로 이어서 `then commit it: stage 'PATH' and run 'ait commit -m <why>'`를 쓴다(커밋하지 않은 채 두면 치우거나 되돌릴 때 사라진다. 무시되는 파일은 도구가 만드는 것이라 커밋하지 않는다).

     그런 hook이 있으면 나머지 hook을 쓰고 2~4를 마친 뒤, 5·6을 하지 않고 그 오류로 끝낸다.

     ```
     if command -v ait >/dev/null 2>&1; then ait hook <이름> "$@" || exit $?; fi      (pre-push가 아닌 hook)
     t=$(mktemp) && cat > "$t" || exit 1                                             (pre-push, 세 행. stdin을 두 번 읽도록 받아 둔다)
     if command -v ait >/dev/null 2>&1; then ait hook pre-push "$@" < "$t" || { s=$?; rm -f "$t"; exit $s; }; fi
     exec < "$t"; rm -f "$t"
     ```
  2. `info/attributes`에 `.ait/** merge=ait` 행을 추가한다.
  3. `merge.ait.driver`를 `command -v ait >/dev/null 2>&1 && exec ait merge-driver %O %A %B %P || exec git merge-file %A %O %B`로, `merge.ait.name`을 설정한다. ait가 없으면 git의 기본 텍스트 병합과 같다. `merge.ait.recursive`는 `binary`로 둔다(merge base끼리의 내부 병합에는 driver를 부르지 않는다).
  4. runtime 디렉터리를 만든다.
  5. HEAD에 바른 `.ait/init`(2.1.2a)이 있으면 `init exists`를 쓰고 끝낸다. 있는데 바르지 않으면 2.1.2a의 `E_FORMAT`과 5.10 hint로 끝낸다(결정 46. 규칙이 다른 저장소를 말없이 바꾸지 않는다). 없으면 후보 U = HEAD tree(커밋이 없는 브랜치면 빈 tree) + `.ait/init`(`version 2`)로 커밋을 만든다. 제목은 `[CONTRACT] ait init`이고 기록줄은 없다(session이 필요 없다). 5.2와 5.5를 따르고, 막히면 그 오류로 끝낸다(1~4는 이미 끝났다). CAS가 실패하면 HEAD를 다시 읽어, 그사이 바른 init 파일이 생겼으면 `init exists`로 성공한다.
  6. stdout에 `init created OID` 또는 `init exists`를 쓴다.
- 7.1.2 **일반 `pre-commit`**. 통합 상태(7.1.3)면 7.1.3을 한다. 그 밖에는 다음을 한다.
  - staged 변경이 계약 경로(6.1. 판정하는 상태는 HEAD)에 닿으면 `E_GUARD`(레코드는 정해진 전이로만, 명세 파일은 계약을 따라서만 바뀐다, 규칙 2·3).
  - 커밋 hook은 확인 결과를 요구하지 않는다. 중간 커밋은 조건을 어겨도 막지 않는다(전제 2).
  - 이 커밋 뒤 새 미확인이 되는 계약(index를 C로, HEAD를 부모로 본 9.1)이 있으면 막지 않고 stderr에 다음을 쓴다. `warning:` 행과 계약 행의 문구가 규범이고, hint 행은 5.6을 따른다.
    - 계약은 ID 순이다. PROVIDER는 HEAD에서의 표시(3.8, 범위가 여럿이면 `,`로 잇는다), CREATOR는 creator의 agent다. 정의마다 body 행과, exec면 `$ ARG…` 행을 쓴다.
    - 이 커밋으로 provider를 찾을 수 없게 되는 계약은 PROVIDER 뒤에 ` (no code under its scope)`를 붙인다.
    - hint는 5.9a의 H13이다. 다음부터 `ait commit`을 쓰라는 행 하나, 이어서 계약마다(ID 순) 되돌리기 hint 한 행과 다시 확인 hint다. 다시 확인 hint는 이 커밋 뒤에 실행할 H7 한 행(찾을 수 없게 되면 H8의 행들. 같은 hint는 한 번만 쓰므로 되돌리기 행은 겹치지 않는다)이다. 아래 틀의 명령은 5.9a의 인자 규칙을 따른다(ID가 current O가 떠맡은 current D면 되돌리기 명령은 `ait future amend ID future amend O… -m <why>`다).

    ```
    warning: this commit leaves N current contract(s) unverified
        ID ROLE PROVIDER (CREATOR)
            body 행
            $ ARG…
    warning: their receivers are notified after this commit
    hint: next time, to check and commit in one step, use 'ait commit' instead of 'git commit'
    hint: to change 'ID' without a check, reopen it: 'ait future amend ID -m <why>'
    hint: (H7, 찾을 수 없게 되면 H8)
    ```

- 7.1.3 **통합 상태**: git이 다른 커밋 I의 변경을 HEAD에 들이다 멈췄고, 그 결과를 아직 커밋하지 않은 상태다. 아래에서 처음 맞는 줄이고, **출발 판**(2.3.2)과 **부모**(9.1)는 다음과 같다.

| 상태 | 판정(5.2-2의 방법으로 본다) | 출발 판(레코드마다) | 부모(9.1) |
| --- | --- | --- | --- |
| merge | MERGE_HEAD에 OID 하나 이상(rebase가 다시 만드는 병합도 이것이다) | HEAD 판, MERGE_HEAD의 각 판 | HEAD와 MERGE_HEAD의 OID들 |
| cherry-pick | CHERRY_PICK_HEAD(I) | HEAD 판, I의 판 | HEAD |
| revert | REVERT_HEAD(I) | HEAD 판, I의 첫 부모의 판 | HEAD |
| rebase | rebase가 진행 중이고 REBASE_HEAD(I)가 있으며, `rebase-merge`면 MERGE_MSG가 있고 `rebase-apply`면 HEAD가 `rebase-apply/abort-safety`와 같다(해결을 커밋하면 MERGE_MSG가 지워지고 HEAD가 움직인다) | HEAD 판, I의 판 | HEAD |

  이 상태의 `pre-commit`은 7.1.2 대신 통합 결과를 정리한다(규칙 2·3·4). cherry-pick·rebase·revert에서 들어오는 쪽(I)의 판은 I가 그 레코드를 바꿨을 때만 출발 판이다. 들어오는 쪽(HEAD가 아닌 출발 판)의 사고 판과 형식이 틀린 판(2.3.3)은 출발 판에서 빼고 `warning: skipping 'K' at 'REV': it was changed outside ait's rules`·`warning: skipping malformed '.ait/K' at 'REV'`를 쓴다. pull의 후보와 같은 규칙이다(7.12.1). HEAD 판은 빼지 않는다. HEAD의 오류는 7과 5.10이 다룬다. 그 밖의 진행 중인 git 조작(멈춘 커밋이 없는 rebase, sequencer만 남은 고르기, `git am`)은 통합 상태가 아니다. index 판이 HEAD 판 그대로인, 형식이 틀린 레코드는 읽을 수 없으므로 2~5에서 건너뛴다(HEAD에 이미 있던 오류인지는 7이 가린다).
  **마치는 커밋**은 통합 상태의 해결을 hook이 도는 커밋으로 남기는 명령이다: `git commit --allow-empty --no-edit --cleanup=strip`, rebase면 `git commit --allow-empty --allow-empty-message -C REBASE_HEAD`. git의 `--continue`로 바로 마치면 hook을 건너뛰거나(rebase), 해결이 HEAD와 같을 때 hook이 돌기 전에 멈추므로(cherry-pick·revert) 먼저 이것으로 커밋한다. 그래서 **마치는 hint**는 `commit the resolution first: 'CMD', then continue as git says`다(CMD는 마치는 커밋). 그 밖의 마무리는 git이 안내한다.
  1. **레코드 경로**: 출발 판들과 index에 있는 `.ait/` 레코드 경로의 합집합을 본다. index에서 사라진 레코드는 `E_STATE`이고 hint는 7과 같다(2.1.5).
  2. **정규화**: 관문(6.2)과 같은 방법으로 index의 명세 파일을 다시 펼치고 고정한다. creator는 출발한 판과 같아야 하고, 출발 판이 없는 새 레코드는 6.2의 새 레코드처럼 채운다(creator는 현재 session의 값이고, 없으면 `unknown -`다. hook은 session 때문에 git 조작을 막지 않는다, 5.1·규칙 9). current가 아닌 D·O의 확인 행은 지운다(확인 행은 current에만 있다, 2.2.2. 그래서 state만 future로 바꾼 해결도 되돌리기다). 그래서 명세 파일을 손으로 합쳐도 된다.
  3. **되돌리기**: index에서 current인 D·O 중, current인 출발 판이 있고 고정(provider 행과 `file` 행)이 current인 출발 판 어느 것과도 다른 것은 확인 없이 고정이 바뀐 것이므로 state를 future로 바꾸고 확인 행을 지운다(1.5d, 규칙 4). 그 D를 떠맡은 current O도 future로 바꾼다. O는 D의 조건을 포함하므로(규칙 5) O도 확인되지 않은 것이기 때문이다(5.9 d). 그 계약은 다시 이행해야 current가 된다. current로 남는 계약은 고정이 같은 출발 판의 확인 행을 둔다(2.2.2의 규칙. 같은 PINS의 `reported`가 있으면 `reported`).
  4. **전이**: 레코드마다 본다. index 판이 HEAD 판 그대로면 지난다(HEAD에 이미 있던 오류는 7이 경고한다). 그 밖에는 3까지 적용한 index 판이 출발 판 중 하나(또는 없음)에서 2.3의 관문 전이로 갈 수 있는 판이어야 한다(2.3.2. 본문으로 비교한다. prev는 5가 정한다. 출발 판을 그대로 둔 것도 된다). 출발 판 중 끝난 판이나 끝난 판을 포함하는 판이 있으면 끝난 판에서만 출발할 수 있다(2.3.2). 그래서 충돌을 손으로 풀어 cancel을 되살린 판은 거절된다. 아니면 `E_STATE`이고 hint는 7과 같다.
  5. **prev**: index 판(2의 다시 고정 전)이 사고 판이 아니고 출발 판을 모두 포함하면(2.2.4) 판을 다시 고르지 않는다. 2가 바꾸지 않았으면 그대로 두고, 바꿨으면 prev는 그 판과 그 판의 prev 모두다. 그래야 받지 않은 끝의 최신 판이 빠지지 않고, 그 판의 blob이 없는 clone에서도 나머지 prev로 모인다. pull이 정한 계약 파일이 그렇다(7.12.6). 누가 정했는지는 묻지 않는다. 그 밖에는, 출발 판의 극대 판(2.2.4)이 둘 이상이고 뜻이 서로 다르면(다시 고정만 다른 판은 뜻이 같다, 1.5e) **충돌 해결**이므로 prev는 극대 판들의 blob이다(index가 그중 하나와 같아도 그렇다). 그 밖에는 index 판의 본문이 어느 극대 판과 같으면 그 판의 prev 그대로, 다르면 극대 판들의 blob이다. 극대 판이 없으면(새 레코드) `-`다(2.2.2).
  6. 2~5의 결과를 index에 쓰고, 작업 폴더는 hook이 돌기 전에 index와 같던 경로만 맞춘다(그 밖의 작업 폴더 변경은 에이전트의 것이다).
  7. `validate`를 실행한다. 오류가 가리키는 레코드 중 HEAD에서도 구조 오류(5.9 a~d)가 가리키는 것이 있으면 그 오류는 HEAD에 이미 있던 것이다(5.10. 그 레코드에 얽힌 레코드는 통합 뒤 5.10이 GOOD으로 함께 되돌리므로, 이 통합이 거기에 더한 것도 그때 함께 정리된다). 이 통합이 만든 것이 아니고 통합 안에서는 고칠 수 없으므로(5.10의 기준 판은 출발 판이 아니어서 4가 막는다) 막지 않고, 오류가 가리키는 레코드마다 `warning: 'PATH' keeps an error from HEAD`를 한 번씩 쓰고 hint는 쓰지 않는다(무엇이 고치는지는 그 커밋 뒤의 상태로 정해진다. 5.9a 판정이 아는 것만). 그 밖의 오류가 있으면 그 오류로 끝난다(7.1.7). hint는 5.8의 해당 오류와 복원 명령이다: 오류마다, 그 레코드(5.9의 오류가 가리키는 레코드)를 그 판으로 복원하면 그 오류가 없어지고 새 오류가 생기지 않는 출발 판으로 되돌리는 `restore 'PATH...' as in 'SIDE_OID' and stage the result`(그 판에 없는 경로는 지운다. SIDE_OID는 그 출발 판을 담은 커밋: 7.1.3 표의 HEAD, MERGE_HEAD의 OID, I, I의 첫 부모. PATH…는 그 레코드와, 그 판이 future·current인 D·O면 그것이 선언한 명세 파일 중 index에 없는 것. 2.4.1. index에 있는 명세 파일은 2가 다시 고정하고, 고정이 바뀐 current는 3이 되돌린다. 손으로 합친 명세 파일을 덮지 않는다)(그런 판이 여럿이면 7.13.1-5의 고를 쪽). 그런 판이 없으면 그 오류의 얽힌 레코드(5.10의 정의에서 HEAD 판·GOOD 판 대신 index 판·그 출발 판)를 함께 한 출발 판으로 복원해 그렇게 되게 하는 명령이다(남은 다른 오류는 그 오류의 복원이 맡는다). 이어서 `then run 'CMD'`(CMD는 마치는 커밋)를 쓴다. 어느 오류에 그런 출발 판이 없으면(들어오는 쪽에만 있는 레코드에 우회로 생긴 오류가 있으면 그렇다. 지우는 복원은 1이 막는다) 이 통합 안에서는 고칠 수 없으므로 hint는 `no side passes here; abort the git OP as 'git status' says`다(OP는 `E_BUSY`와 같다).

- 7.1.4 **`pre-merge-commit`**(충돌 없는 자동 병합. `-s ours` 같은 전략도 여기로 온다): 이 hook이 도는 동안에는 MERGE_HEAD가 아직 없어 출발 판을 알 수 없다. 그래서 `validate`만 실행한다. HEAD에 이미 있던 오류는 7.1.3-7처럼 경고만 하고, 그 밖의 오류가 있으면 그 오류로 끝나고(7.1.7), git은 merge를 끝내지 않은 채 남긴다. hint는 `fix it in this merge and run 'CMD'; the records and spec files are pinned and validated again then`이다(CMD는 마치는 커밋. 그때 7.1.3이 돈다). 7.1.3의 정리를 건너뛰지 않았는지는 commit-msg가 본다(7.1.5).
- 7.1.5 **`commit-msg`**: 통합 상태가 아니면 아무것도 하지 않는다. 통합 상태(자동 병합도 이때는 MERGE_HEAD가 있다)에서는 다음을 한다.
  - **정리 확인**: 먼저 7.1.3을 읽기만 하며(쓰지도 경고하지도 않고) 돌린다. 1·4·7에 막히거나 2·3·5가 index를 바꿔야 하면, pre-commit을 거치지 않은 자동 병합이다. 그 오류로 끝나고(7.1.7) git은 merge를 끝내지 않은 채 남긴다. 막힌 오류가 있으면 그 오류(5.8)와 그 hint(7.1.3-7의 복원 명령이면 `then run 'CMD'`로 끝난다)를, index만 바꿔야 하면 `error: not allowed: this merge needs its records pinned and validated again`(`E_STATE`)과 `finish it with 'CMD' (its pre-commit hook must run 'ait hook pre-commit')`(CMD는 마치는 커밋. 탐지(7.1.1-1)가 놓친 hook이어도 무엇을 고칠지 알게 한다)을 쓴다. 이 clone의 pre-commit에 ait hook이 없으면(7.1.1-1) 그 커밋도 같은 까닭으로 막히므로 그 앞에 H12를 쓴다. 그래서 자동 병합으로 레코드를 버리거나 정리를 건너뛰지 않는다. 7.1.3은 같은 index에 다시 돌려도 바꿀 것이 없으므로, pre-commit을 거친 커밋은 여기를 지난다.
  - **기록줄**: index의 레코드 중 어느 출발 판과도 다른 것(prev까지 비교한다)이 있으면, 기록줄 블록의 `Ait-Request`·`Ait-Agent`·`Ait-Session` 행을 지우고 다음 행을 붙인다. 출발 판의 prev를 그대로 물려받은 레코드는 새 사실이 아니다. 기록줄 블록은 git의 기록줄 파서가 읽는 블록이다(5.4). 없으면 빈 행 하나 뒤에 새 문단으로 붙인다.
    - 새 `Ait-Request`
    - `Ait-Agent`·`Ait-Session`: 유효한 session의 값. 없으면 `unknown`과 `-`
  - **경고**(규칙 6·7): 이 커밋에서 새 미확인이 되는 계약(index를 C로, 7.1.3의 부모로 본 9.1)마다 `warning: 'ID' is unverified after this merge`를 쓴다. 이 커밋이 만든 미확인이기 때문이다. index에서 미확인이고 HEAD에서는 아니던 계약 중 현재 session의 약속·요구·구독(9.3)인 것마다 `warning: 'ID' comes in unverified`를 쓴다. 이 커밋이 그 계약의 되돌린 커밋(1.5d)이 되는 D·O마다 `warning: 'ID' is reopened in this merge`를 쓴다. 부모는 7.1.3의 부모이고 이 커밋의 판은 index 판이므로 pre-commit이 이미 되돌렸어도 같다. 이유를 알 수 있으면 그 행 끝에 처음 맞는 것 하나를 붙인다: prev를 뺀 내용이 future인 출발 판과 같으면 `: it keeps the future version over the current one`, 고정이 current인 출발 판 어느 것과도 다르면(7.1.3-3) `: its pins changed without a check`, 떠맡은 D가 이 커밋에서 되돌려진 O면 `: it undertakes reopened 'D'`. 이어서 그 계약이 이 커밋 뒤 H10의 대상이면(되돌린 쪽은 1.5d로 정한다) H10을 쓴다. 미확인 경고에는 이어서 H7·H8을 쓴다.

  그래서 충돌 해결은 누가 무엇을 해결했는지 드러나는 새 사건이 된다(9.3).
- 7.1.6 **`pre-push`**: stdin으로 들어온 각 행 중 삭제가 아닌 것마다, local OID를 커밋으로 벗겨(`OID^{commit}`) 그 상태를 `validate`한다. 커밋으로 벗겨지지 않는 객체(tree·blob을 가리키는 tag 등)는 계약 상태가 없으므로 보지 않는다. 실패하면 그 오류로 끝난다(7.1.7). hint는 그 오류의 것(5.10, `E_INCOMPLETE`)인데, HEAD가 브랜치를 가리키고 그 행의 local 이름(LOCAL. 브랜치·태그·식·OID가 그대로 온다)이 `HEAD`나 그 브랜치일 때만 여기서 고칠 수 있다(ait는 브랜치에만 기록하고, 고친 커밋을 따라 움직이는 이름은 그것뿐이다). 그 밖에는 hint가 없다(여기서 고칠 명령이 없다. 그 커밋이 HEAD인 브랜치에서는 ait 명령이 같은 오류와 그 hint를 준다). 그곳으로 가는 법과 고친 것을 내보내는 법은 git의 일이다.
- 7.1.7 hook이 거절할 때는 stderr에 5.8의 첫 행과 hint를 쓰고, 그 오류의 종료 코드(5.8)로 끝난다. git은 0이 아닌 종료를 거절로 본다.
- 7.1.8 **`post-commit`·`post-merge`**(규칙 6, 9): 커밋이나 merge(fast-forward 포함)가 끝난 뒤 git이 부른다. git은 이 hook의 종료 코드를 보지 않으므로 막지 않고 알린다. `--no-verify`나 충돌 없는 revert처럼 앞의 hook이 돌지 않은 커밋도 여기서 드러난다. 새 HEAD를 전 HEAD(post-commit은 첫 부모, post-merge는 `ORIG_HEAD`)와 견준다.
  - 새 HEAD에 새로 생긴 HEAD의 오류(5.10: 구조 오류, 형식이 틀린 판, 사고 판, 고정과 다른 명세 파일)마다 그 오류의 첫 행(5.8)을 `error:` 대신 `warning:`으로 쓰고 5.10의 hint를 쓴다(예: `warning: 'K' at HEAD was changed outside ait's rules`).
  - 레코드가 옛 판으로 돌아갔으면(새 HEAD의 판이 전 HEAD의 판에 포함되고 그와 다르다) `warning: this commit put 'K' back to an older version; only this branch has it`와 `hint: to take it back for others too, once this commit is reverted, run CMDS`를 쓴다. CMDS는 그 커밋을 되돌려 K가 전 HEAD의 판(전 판)으로 돌아간 상태에서 ait의 규칙으로 옛 판(새 HEAD의 판)에 이르는 명령이다(결정 47): 연결이면 옛 판의 state로 `'ait release D O -m <why>'`·`'ait undertake D O -m <why>'`(release는 O가 내가 만든 계약일 때만(5.9a 남의 계약), undertake는 그 상태에서 두 끝이 살아 있고 current O가 future D를 떠맡게 되지 않을 때만(2.3, 5.9 d)). 옛 판이 future면 `'ait future amend K ARGS -m <why>'`(전 판이 current D면 5.9 d의 O…도 `future amend O`로 함께). 옛 판이 current이고 전 판이 future면 `'ait fulfill K -m <why>'`(앞에 `'ait future amend K ARGS -m <why>', then `). 둘 다 current면 `'ait amend K ARGS -m <why>'`다. ARGS는 옛 판의 consumer(demand)·provider·정의를 5.9a 인자의 형으로 쓴 것이되, 전 판과 같은 자리는 `-`로 두고(7.3.1), 모두 같으면 쓰지 않는다(state도 같으면 이를 판이 없으므로 hint가 없다). 옛 판이 고정한 명세 파일이 되돌린 상태의 것과 다르면(내용, mode, 있음) 도우미는 그때의 파일을 고정하므로, 명령 앞에 `restore 'PATH...' as in 'C', then `(C는 이 커밋)을 쓴다. 전 판이나 옛 판이 끝난 판이면(끝난 계약은 되살릴 수 없다), drop이면(future drop으로만 바뀐다), 옛 판의 범위가 되돌린 상태에 없으면(다시 줄 수 없다), 이 커밋이 옛 판이 고정한 명세 파일을 그대로 갖지 않으면(복원할 곳이 없다) hint가 없다. 옛 판으로 돌아간 것은 사고가 아니다. 그 브랜치가 옛 상태에 있을 뿐이고(규칙 2), 새 판이 없으므로 남에게 사건이 아니다(9.1). 남에게도 거두려면 ait 명령으로 새 판을 남긴다.

### 7.2 session

- 7.2.1 `ait session open --agent NAME`
  - NAME은 `[A-Za-z0-9._-]{1,64}`이고 `unknown`은 쓸 수 없다(session 없는 기록의 자리, 2.2.2·7.1.5). agent 이름은 모든 clone에서 동시에 일하는 에이전트끼리 겹치지 않고, 재시작해도 같아야 한다. 내 약속과 내 요구가 이름으로 판정되기 때문이다(9.3, 7.8.1).
  - 현재 ref 목록을 tip으로 기록하고, SID를 출력한다.
  - 같은 NAME의 다른 session이 있으면 `warning: agent name 'NAME' is also used by session 'S…' (worktree 'PATH')`와 `hint: if 'S…' is yours, close this one: 'ait session close SID', and use 'S…' in 'PATH' with AIT_SESSION=S…; otherwise close this one and open again with another name`. 그 session의 worktree가 없으면 대신 `its worktree is gone; close it: 'ait session close S…', and keep this one if this worktree is on your branch, otherwise close this one too ('ait session close SID') and open it again in the worktree of your branch`다(내 약속의 일은 내 브랜치에 있다).
  - 이 worktree의 기본 session이 다른 agent의 session이면, 이 worktree를 다른 에이전트가 쓰고 있다는 뜻이다(10.1). `warning: this worktree is used by session 'S…' (agent 'X')`와 `hint: close this one: 'ait session close SID', then open a session in a worktree of your own: 'ait session open --agent NAME' there`(NAME은 이 session의 agent 이름). worktree를 만드는 법은 git의 일이다.
- 7.2.2 `ait session list`: SID 순으로 `session`, `agent`, `worktree`, `default yes|no`(그 session이 자기 worktree의 기본 session인지), `subs N`, 그 `sub …` 행들, `listening yes|no` 블록을 쓴다.
- 7.2.3 `ait session close [SID]`: session의 데이터 파일을 지운다(4.5의 잠금 순서, 이어서 `worktrees/<KEY>.lock` 안에서 그 SID를 가리키는 포인터를 지운다. 잠금 파일은 남긴다). SID를 생략하면 현재 session이다. 없는 SID는 `E_SESSION`. listener가 돌고 있으면 `E_BUSY`이고 hint는 `stop the listener first: kill PID`다.
- 7.2.4 **구독**(규칙 1, 6.8):
  - `ait sub ARG...`: ARG를 현재 session의 구독에 더한다. ARG는 범위(`DIR/`나 `/` 포함)이거나 ID다. 범위는 `sub scope`(`PATH:S-E`는 3.3으로 HEAD에서 고정해 그 BLOB을 쓴다), ID는 `sub id`다. HEAD에도 ref 목록에도 없는 ID는 `E_ID`. ARG가 없으면 `E_USAGE`. 새로 더한 구독마다 ARG 순서로 `sub id ID`·`sub scope DISPLAY`를 쓴다. 이미 있는 구독은 다시 더하지 않고 쓰지도 않는다.
  - `ait unsub [ARG...]`: ARG가 없으면 현재 session의 `sub` 행을 모두, 있으면 ARG와 맞는 `sub` 행만 지운다. ID ARG는 같은 `sub id`, 범위 ARG는 HEAD에서의 표시(3.8)가 같은 `sub scope`와 맞는다. 지운 구독마다 session 파일의 순서로 `unsub id ID`·`unsub scope DISPLAY`를 쓴다. 맞는 것이 없으면 아무것도 쓰지 않고 exit 0이다.
  - `ait session list`는 session마다 `sub …` 행을 그대로 보여 준다.
- 7.2.5 `ait listen [ARG...] [--fetch SEC]`: 알림을 받는 유일한 방법이다. 에이전트는 이것을 백그라운드로 띄워 둔다.
  - ARG가 있으면 먼저 `ait sub ARG...`와 같이 구독에 더한다(7.2.4). 그 `sub …` 행은 stderr에 쓴다. stdout에는 사건만 쓴다.
  - 유효한 session이 필요하다. 같은 session에 listener가 이미 돌고 있으면 `E_BUSY`(`fatal: cannot run now: a listener is already running for session 'S…'`).
  - 도는 동안 `<SID>.listen`을 잡고 자기 PID를 쓴다(4.5). 멈출 때까지 1초마다 9.5를 실행한다. 사건마다 stdout에 9.7의 payload와 `ait-event-end` 한 행을 쓴다.
  - `--fetch SEC`이면 SEC초마다 `git fetch --all`도 한다. fetch가 실패하면 `warning: fetch failed: …`와 `hint: check the remotes`를 쓰고 계속한다.
  - SIGINT·SIGTERM에서 exit 0. SIGPIPE는 무시하고 쓰기 실패를 직접 다룬다(9.5-3). listener가 없는 동안 생긴 사건은 tip이 남아 있으므로 다음 listen에서 나온다. 다만 그때 ref 목록에서 아직 도달하는 커밋만이다.

### 7.3 계약 도우미 (입구 1)

- 7.3.1 문법:

```
request := ["contract"|"ct"] clause+ option*          (option은 절 사이 어디에 와도 된다)
option  := "-m" MSG | "-F" FILE | ("-r"|"--result") FILE | "--force"
clause  := "demand" A B FILES ["as" NAME]
         | "offer" B FILES ["as" NAME]
         | "future" "demand" A B FILES ["as" NAME]
         | "future" "offer" B FILES ["as" NAME]
         | "future" "drop" K ["as" NAME]
         | ["future"] "amend" K [ARGS]        (D: A B FILES, O: B FILES, X: target인 K; "-"는 그대로 둠)
         | "future" "cancel" K
         | "undertake" K K                    (D O)
         | "release" K K                      (D O)
K       := 계약을 가리키는 ID나 alias(1.9)
FILES   := PATH ("," PATH)*
A, B    := 범위 목록 (3.2a)
```

  - 위치 인자는 `-`로 시작할 수 없다. amend의 단독 `-`만 예외다.
  - 다음은 `E_USAGE`다: 문법 위반, alias 중복, 옵션 중복(`-m` 반복은 예외), `fulfill`과 섞음, `-r`와 `--force`를 함께 씀.
- 7.3.2 절을 순서대로 HEAD 상태에 적용한다. 각 절의 조건은 그 절을 적용하는 시점에 검사한다. 전이의 판정은 2.3.1을 따른다.

| 절 | 조건 | 효과 |
| --- | --- | --- |
| `demand A B FILES` | — (범위는 3.3·3.4) | 새 D (current, 6.5) |
| `offer B FILES` | — (범위는 3.3·3.4) | 새 O (current, 6.5) |
| `future demand A B FILES` | — | 새 D (future) |
| `future offer B FILES` | — | 새 O (future) |
| `future drop K` | K가 current D·O | 새 X (future) |
| `amend K [ARGS]` | K가 future·current인 D·O, 또는 future X | `-`가 아닌 필드만 바꾼다. 준 범위는 HEAD에서 다시 고정한다(3.3). current는 current로 남고, 확인이 필요한지는 6.5로 정한다. 빈 amend(6.5)면 current는 다시 확인, future는 noop |
| `future amend K [ARGS]` | K가 future·current인 D·O, 또는 future X | K가 current D·O면 future로 되돌린다(1.5d, 5.9 d). 그 밖은 `amend`와 같다 |
| `future cancel K` | K가 future(이미 cancelled면 noop) | cancelled |
| `undertake D O` | D는 demand, O는 offer. 나머지 조건은 2.3 | undertaken(이미 undertaken이면 noop) |
| `release D O` | —(undertaken이 아니면 noop) | released |

  역할이 맞지 않으면 `E_ID`, 그 밖의 조건 위반은 `E_STATE`다.
- 7.3.3 후보 U = H tree + 메모리의 레코드 + FILES 각 경로의 **작업 폴더 내용과 mode**. FILES는 명세 정의(2.5)이고, 작업 폴더에 있는 일반 파일이어야 한다. 정의가 선언한 파일도 작업 폴더 내용으로 U에 넣는다. index 판은 HEAD 판이나 작업 폴더 판과 같아야 한다(부분 stage 금지). 대소문자를 가리지 않는 파일시스템(`core.ignorecase`)에서 HEAD나 index에 대소문자만 다른 경로가 있으면 받지 않는다(`git add`는 그 경로를 stage하지 않는다. 계약 범위 인자면 `E_SCOPE`). 어기면 `E_INCOMPLETE`.
- 7.3.4 U를 관문에 넣는다. 사용자가 따로 stage해 둔 다른 변경은 커밋에 포함하지 않고 그대로 남겨 둔다.
- 7.3.5 HEAD에 없던 연결이 최종 상태에서 `released`이면 그 파일은 만들지 않는다.

### 7.4 contract new · 7.5 contract add · 7.6 contract commit (입구 2)

- 7.4.1 `ait contract new demand|offer|drop`: 새 ID로 작업 폴더에 작성 형식의 틀을 쓴다. 내용은 정확히 다음과 같다. demand: `role demand` / `state future` / `consumer PATH` / `provider PATH` / `file PATH`. offer: `role offer` / `state future` / `provider PATH` / `file PATH`. drop: `role drop` / `state future` / `target ID`. 출력은 `path .ait/<ID>` 한 행이다. stage는 하지 않는다.
- 7.4.1a `ait contract new spec PATH exec|prompt`: 명세 정의의 틀을 PATH에 쓴다. 주석이 쓸 수 있는 키를 알려 준다(규칙 7). PATH는 3.1을 따르고, 없는 상위 디렉터리는 만든다. 이미 있으면 덮어쓰지 않고 `E_STATE`(`error: not allowed: 'PATH' already exists`)이며 hint는 `edit 'PATH' or choose another path`다. 출력은 `path PATH` 한 행이다. stage는 하지 않는다. 틀의 내용은 정확히 다음과 같다(끝에 LF). exec:

```yaml
# ait spec definition (exec): the checker runs 'command'; it passes if the exit code is in 'success'.
# Keys: body, engine, command, success, environment, inputs. Any other key is a format error.
body: |
  <the condition the code must keep>
engine: exec
command: [<program>, <arg>]   # argv, run without a shell
success: [0]                  # exit codes that count as pass
inputs: []                    # files pinned with the contract: scripts in 'command', test data. Not the code under check
# environment: <name>         # optional: specs/environments/<name>.yaml (cwd, timeout_ms, env, prepare, cleanup)
```

  prompt:

```yaml
# ait spec definition (prompt): the checker reads the code and judges whether 'body' holds.
# Keys: body, inputs. Any other key is a format error.
body: |
  <the condition the code must keep>
inputs: []                    # files pinned with the contract, such as test data. Not the code under check
```

- 7.5.1 `ait contract add PATH...`: 각 PATH는 계약 경로(6.1. 판정하는 상태는 HEAD와 작업 폴더)여야 한다. 아니면 `E_MIXED`.
- 7.5.2 **관련 경로**: PATH가 닿는 계약(그 레코드이거나 그 계약이 선언한 명세 파일)마다, 그 레코드와 선언한 명세 파일. 선언은 작업 폴더의 레코드(없으면 HEAD의 것)가 future·current인 D·O일 때 그 `def` 행과, 그 정의들의 작업 폴더 내용이 선언한 `inputs`·환경 정의 파일이다(관문이 다시 펼치는 것과 같다, 6.2). 그래서 정의에서 뺀 파일은 관련 경로가 아니다. 끝난 계약은 파일을 맞추지 않으므로(2.4.1) 선언이 없다. 작업 폴더에 없는 관련 경로(일반 파일이 아니면 명세 파일로서 없는 것이다, 2.1.4)마다 `missing PATH`, 작업 폴더에 있는 관련 경로 중 주어진 PATH만 stage하면 작업 폴더와 index가 어긋날 것마다 `unstaged PATH` 행을 쓰고 `E_INCOMPLETE`이며, 아무것도 stage하지 않는다. 모두 맞으면 주어진 PATH만 `git add`하고 `staged PATH` 행을 쓴다. 작업 폴더에서 지운 레코드는 그 계약의 관련 경로를 보지 않고 그 삭제를 stage한다(지울 수 있는지는 관문이 판정한다, 2.3.1).
- 7.6.1 `ait contract commit [-r|--result FILE | --force] message`: 후보 U는 index tree다. `.ait/` 아래에 추적하지 않는 파일이 있거나, stage된 계약들(index에서 지운 레코드의 계약은 뺀다. 지울 수 있는지는 관문이 판정한다, 2.3.1)의 관련 경로(7.5.2) 중 작업 폴더에 없거나 작업 폴더와 index가 다른 것이 있으면 `E_INCOMPLETE`다(7.5.2와 같은 `missing`·`unstaged` 행). U가 HEAD와 같아도 관문에 넣는다(깨진 묶음은 6.2로 다시 고정되고, 바뀐 것이 없으면 `noop`). `noop`인데 작업 폴더와 index가 다른 계약 경로(7.5.1의 집합)가 있으면, 이어서 그 경로마다 `unstaged PATH` 행을 쓰고 hint `stage it: 'ait contract add PATH...', then rerun this command`를 쓴다. 고친 것이 커밋되지 않았다는 것이 이 명령의 결과이기 때문이다(git도 stage된 것이 없는 commit에서 그렇게 알린다). 그런 경로가 없는 `noop`인데 index가 HEAD와 다른 레코드가 있으면 그것은 HEAD 판과 뜻이 같은 다른 철자이므로 hint `restore 'PATH...' as in 'HEAD': it means what HEAD's version means`를 쓴다(결정 44. 그대로 두면 다음 도우미가 `E_DIRTY`에 막힌다. stage할 것이 있으면 그것이 먼저다. 먼저 복원하면 그것을 버린다).

### 7.7 fulfill (약속 이행)

- 7.7.1 `ait fulfill ID[=SCOPES]... [-r|--result FILE | --force] message`: ID를 하나 이상 받는다. 중복이나 alias, `-r`와 `--force`를 함께 주면 `E_USAGE`.
  - **선택한 대상**: `=` 없는 인자, 그리고 `ID=SCOPES` 중 ID가 HEAD에서 future이면서 함께 이행되는 D(7.7.2)가 아닌 것. 그 밖의 `ID=SCOPES`는 범위만 준다. 선택한 대상이 없으면 `E_USAGE`다.
  - `=SCOPES`는 그 계약의 provider를 바꿀 범위 목록이다(3.2a). 각 범위는 `PATH`나 `PATH:S-E`이고, 행 번호는 T0 기준이다. 확인 집합(7.7.3-5)의 D·O에 줄 수 있다. 추상 계약을 좁히거나, 코드를 옮기는 이행에서 계약을 새 위치로 옮길 때 쓴다.
  - 다음은 `E_SCOPE`다: current가 되는 추상 계약에 `=SCOPES`가 없다, 확인 집합에 없는 계약에 주었다, 추상 계약을 그 범위 밖으로 좁혔다, 3.3의 조건을 T0에서 어긴다.
- 7.7.2 **함께 이행되는 D**: 선택한 future O가 떠맡은 future D 전부(1.6).
- 7.7.3 **이행 조건**(규칙 4·5): 기준은 실제 index의 tree T0다. 하나라도 어기면 거절하고 7.7.6의 보고를 stdout에 쓴다(검사 순서는 5.13). 범위와 고정은 HEAD 레코드의 것으로 계산한다.
  1. 선택한 각 ID가 HEAD에서 future다(`E_STATE`, 없는 ID는 `E_ID`). 선택한 X의 target은 HEAD에서 current다(`E_STATE`).
  2. T0가 명세 파일(6.1. 판정하는 상태는 HEAD)을 바꾸지 않았다(`E_MIXED`, 규칙 3).
  3. 이행 후 상태(7.7.4 적용 후)가 validate를 통과한다.
  4. **확인할 코드**: 확인 집합(5)의 계약은 모두 `=SCOPES`를 적용한 뒤 T0에서 provider를 찾을 수 있다(`new:PATH`는 T0에 있고, 행 범위는 사상된다. 1.5c). 아니면 `E_CODE_REQUIRED`다(1.5b).
  5. **확인 집합**(1.5b, A→B는 H→T0). 다시 고정하기 전에 정해진다.
  6. **확인 결과**: 확인 집합의 모든 명세 정의에 대해 `-r FILE`로 낸 결과가 있고 모두 pass다(8장). 없거나 확인 입력이 바뀌었으면 `E_NEEDS_RESULTS`, fail·unknown이 있으면 `E_CHECK_FAILED`. 과제가 없으면 결과는 필요 없다. `--force`면 결과 대신 판단으로 넘긴다(5.14).
- 7.7.4 통과하면 다음과 같이 공개한다(5.5).
  - 선택한 D·O와 함께 이행되는 D는 current가 된다. X는 fulfilled가 되고, 그 target은 withdrawn이 된다.
  - `=SCOPES`를 준 계약의 provider는 그 범위들을 T0에서 고정한 것으로 바꾼다.
  - 확인 집합의 나머지 D·O는 provider를 T0로 다시 고정한다. `PATH BLOB`은 T0의 blob으로, `PATH:S-E BLOB`은 3.7로 T0에 사상한 행과 T0의 blob으로 바꾼다.
  - current가 되는 계약의 `new:PATH`(consumer 포함)는 T0에 있으면 `PATH BLOB`으로 바꾼다.
  - 바뀐 레코드의 prev는 HEAD 판이다.
  - 확인 집합의 계약마다 확인 행을 2.2.2의 규칙으로 쓴다(PINS는 다시 고정한 뒤의 고정, 5.4). withdrawn이 되는 target은 current가 아니므로 확인 행을 지운다. 받은 결과 파일을 메시지 본문에 덧붙인다(8.4).
- 7.7.5 출력: `request RID`, `fulfilled ID [via O…]`(current·fulfilled가 된 레코드마다 ID 순. D를 떠맡은 offer 중 이 커밋에서 함께 current가 된 것이 있으면 `via`로 붙인다), `withdrawn ID`, `check ID reported|forced`. 경고는 6.7a와 같다.
- 7.7.6 **이행 준비 보고**: `-r`도 `--force`도 없이 실행했는데 과제가 있거나, 조건에 막히면, 커밋하지 않고 무엇이 모자라고 무엇을 확인해야 하는지 쓴다. 다음 순서로 쓴다.

```
id ID                                  (선택한 대상마다 한 행. 선택 순서)
provider ID DISPLAY                    (확인 집합의 계약마다: provider를 T0에 사상한 표시, 3.8. 범위마다 한 행. `=SCOPES`를 줬으면 그 범위. ID 순)
mixed PATH                             (stage된 명세 파일 변경, 경로 순)
invalid WORD MESSAGE                   (이행 후 상태의 validate 실패. WORD는 5.8의 porcelain 낱말)
code-required ID                       (확인 집합의 계약 중 T0에서 provider를 찾을 수 없는 것, 7.7.3-4)
touch ID                               (이 변경에 걸려 함께 확인할 계약, 1.5b. ID 순)

task CONTRACT:PATH exec|prompt         (확인할 과제. 확인 집합의 모든 명세 정의, 이름 순)
check-request PATH                     (확인 요청 파일, 8.2. 이번에 썼거나 이미 있을 때만, 절대경로)
check-result PATH                      (채워서 -r로 낼 결과 틀. 같은 조건, 절대경로)
ready yes|no
```

  - 빈 행 하나가 상태 행(`id`~`touch`)과 과제 행(`task`~`check-result`)·`ready` 행을 나눈다.
  - `ready yes`는 mixed·invalid·code-required가 없다는 뜻이다. 그때 결과 틀을 채워 `-r`로 다시 실행하면 이행된다. `code-required`가 있으면 과제 행(`task`·`check-request`·`check-result`)을 쓰지 않는다(8.4).
  - `provider` 행은 코드를 옮기는 이행에서 `ID=SCOPES`를 만드는 재료다. 그대로 둔 조각은 이 표시에서 `?` 표지를 뗀 범위를 쓰고, `?missing`·`?deleted`·`?nobase`인 조각은 남은 것이 없으므로 쓰지 않으며, 옮긴 조각만 새로 더한다(10.4). hint가 이 범위를 채워 준다.
  - `E_ID`·`E_STATE`·`E_USAGE`·`E_SCOPE`처럼 대상을 정하기 전에 막힌 오류는 보고 없이 stderr만 쓴다.
  - 확인 요청 파일과 결과 틀은 같은 상태(확인 입력이 같으면)에서 다시 실행해도 그대로 쓸 수 있다.
  - stderr에는 6.7a의 H9와, `code-required` 행마다 5.8의 `E_CODE_REQUIRED` hint를 쓴다.

### 7.7a commit (코드 커밋과 걸린 계약의 확인)

- 7.7a.1 `ait commit [ID=SCOPES]... [-r|--result FILE | --force] message`: stage된 변경(T0)을 커밋하면서, 그 변경에 걸린 계약(1.5b)을 같은 커밋에서 확인한다. 선택한 대상이 없는 `fulfill`이다. 다른 점만 적는다.
  - 확인 집합은 걸린 계약뿐이다. `ID=SCOPES`는 걸린 계약에 줄 수 있다(코드를 옮긴 계약을 함께 옮긴다). `=` 없는 인자는 `E_USAGE`. T0가 HEAD tree와 같으면 `noop`.
  - 확인할 계약이 없으면 결과 없이, 기록줄 없이 커밋한다. validate를 하지 않고 session도 보지 않는다. `git commit`과 같은 커밋이고 출력은 없다.
  - 확인할 계약이 있으면 7.7.3-3~6과 7.7.4를 따른다. 출력은 `request RID`, `check ID reported|forced`다.
  - 메시지에는 접두어를 붙이지 않는다(5.3). 5.2와 `E_MIXED`는 fulfill과 같다.
  - 보고(7.7.6)는 `id` 행 없이 쓴다.
  - `commit-tree`로 커밋을 만들므로 사용자의 다른 git hook을 거치지 않는다(11.2).

### 7.8 todo (무엇을 할 차례인가)

- 7.8.1 **대상**: 기준은 실제 index의 tree T0다. index에 unmerged 경로가 남아 있으면 그 경로는 HEAD 판(git이 만든 충돌에서는 stage 2다)으로 보고, 경로마다 `warning: 'P' is unmerged; it is read as HEAD has it`를 쓴다(결정 45. git의 읽는 명령처럼 나머지는 index대로 보인다). 커밋도 알림도 만들지 않는다.
  - ARG 없음: 현재 session의 내 약속(1.3)과, 미확인인 내 요구(내 current demand). session이 없으면 모든 future와 미확인 current.
  - ID: 그 레코드. 범위: consumer·provider·명세 파일(X는 target의 범위)이 겹치는 future와 미확인 current.
- 7.8.2 **`ait todo [ARG...]`**: 대상마다 ID 순서로 블록을 쓰고, 각 블록은 빈 행으로 끝난다. 블록마다 그 계약에 대한 5.9a의 hint 중 지금 상태로 판정할 수 있는 것(H1, H2, H5, H7, H8, H10, H14)을 stderr에 쓴다.

```
id ID
role …
state …
creator AGENT
consumer DISPLAY                       (demand)
provider DISPLAY | target ID
scope DISPLAY                          (drop: target의 범위)
unverified yes                         (미확인인 current일 때)
file PATH exec|prompt|input           (자기 명세 파일)
    정의면 body의 각 행 앞에 공백 4칸 (구현 목표인 조건)
    exec 정의면 이어서 `    $ ARG…` 한 행과, environment가 있으면 `    environment NAME` 한 행
undertakes ID STATE AGENT              (O: 떠맡은 demand, 그 state와 creator)
undertakes-provider ID DISPLAY         (그중 future D: 함께 이행될 때 확인할 범위)
undertakes-file ID PATH exec|prompt|input   (그 demand의 명세 파일; 정의면 같은 방식으로)
undertaken-by ID STATE AGENT           (D: 이 demand를 떠맡은 offer, 그 state와 creator)
```

  대상 블록 뒤에, stage된 변경(H→T0)에 걸린 계약이 있으면 `index` 한 행, 이어서 `code-required ID`·`touch ID` 행(`ait commit`의 7.7.6과 같은 집합과 순서), 빈 행을 쓴다.

### 7.9 peek (계약 읽기·이력)

- 7.9.1 `ait peek [now|after] [ARG...]`. 기본은 after다. ARG는 범위(`DIR/`나 `/` 포함)이거나 ID다. `PATH`는 파일이 없어도 된다. `PATH:S-E`는 3.3으로 HEAD에서 고정한다.
- 7.9.2 HEAD 기준으로 두 view를 정의한다. now = current 계약. after = (current − future drop의 target) ∪ future D·O. 곧 약속이 모두 이행된 뒤의 모습이다(이름이 계약의 state `future`와 겹치지 않게 view는 `after`라 한다).
- 7.9.3 선택: ARG가 없으면 view 전체. 범위 ARG에는 consumer, provider, 명세 파일 경로 중 하나가 `overlap`(3.9)인 계약. ID ARG에는 view와 상관없이 그 레코드(HEAD에 없으면 elsewhere에서 찾는다. HEAD의 이력에서 지워졌으면 `id ID`와 `event RID|- AGENT|? deleted OID` 행. 그래도 없으면 `E_ID`).
- 7.9.4 출력: 머리말(`view …`, `session SID|-`), 빈 행, 레코드 블록(ID 순), elsewhere 블록 순서다. 각 블록은 빈 행으로 끝난다. stderr에는 보여 준 future demand(elsewhere 블록의 것도)마다 H11, 미확인인 current마다 H7·H8을 쓴다.

```
id ID
role …
state …
consumer DISPLAY                      (demand)
provider DISPLAY | target ID
scope DISPLAY                         (drop: target의 범위)
unverified yes                        (미확인인 current일 때)
creator AGENT SID
undertake ID STATE                    (undertaken인 연결의 다른 끝과 그 state)
drop XID                              (이 계약을 겨눈 future drop)
match ARG consumer|provider|file      (범위 ARG가 어디와 겹쳤나)
file PATH exec|prompt|input
    정의면 body의 각 행 앞에 공백 4칸
event RID AGENT ACTION [ARG]          (ID ARG일 때, 7.9.5)
```

- 7.9.5 **이력**(규칙 1): ID로 지정한 레코드는 그 레코드, 관련 연결 파일, 그 계약의 명세 파일(그 레코드의 모든 판에 나온 경로)을 바꾼 커밋들을 오래된 순서로 보여 준다. 확인도 레코드의 확인 행으로 남으므로(1.5a) 그 레코드를 바꾼 커밋에 든다.
  - 대상 커밋: `git rev-list --reverse --topo-order --full-history HEAD -- 경로…`. 경로에는 그 레코드의 모든 판에 나온 provider 경로와, 그 계약을 겨눈 drop·떠맡은 offer의 레코드 경로(모든 판)도 넣는다.
    - 병합 커밋은 관련 경로마다 따로 보고, 그 경로가 모든 부모의 판과 다를 때만 그 변화를 쓴다.
    - 같은 RID에서 레코드 blob도 같은 변화는 한 번만 쓴다(cherry-pick 사본). RID가 `-`인 행은 합치지 않는다.
  - ACTION은 첫 부모 판과 비교해 정한다. 다만 `reopened`는 1.5d의 되돌린 커밋으로 정한다(다른 가지의 이행을 버린 병합도 되돌리기다). 한 레코드에 여러 변화가 겹치면 표의 위쪽 행 하나만 쓴다. 다만 그 커밋에서 9.1이 이 계약을 새 미확인으로 보면, 그 행 바로 뒤에 `unverified` 행을 따로 쓴다.

| 변화 | ACTION | ARG |
| --- | --- | --- |
| 레코드가 새로 생김 | `created-future` / `created-current` | — |
| state가 future에서 current로 | `fulfilled` | D를 떠맡은 offer 중 이 커밋에서 함께 current가 된 것이 있으면 `via O…`(ID 순) |
| X의 state가 fulfilled로 | `fulfilled` | — |
| 이 커밋이 되돌린 커밋이다(1.5d) | `reopened` | — |
| state가 withdrawn·cancelled로 | `withdrawn` / `cancelled` | withdrawn이면 철회한 X |
| state는 그대로, 범위(다시 고정, 1.5e는 뺀다)·target이 바뀜 | `amended` | — |
| state는 그대로, 명세 파일 행(목록·mode·blob)이 바뀜 | `respecified` | 추가·삭제·다시 고정된 PATH들(오름차순) |
| current인 채로, 본문이 그대로이거나 다시 고정만 되었고(1.5e), 확인 행이 바뀌었으며 그 PINS가 이 판의 고정과 맞음(1.5a) | `verified` | — |
| 본문과 확인 행은 그대로, prev만 바뀜(충돌 해결이나 pull이 판을 합침) | `resolved` | — |
| 레코드는 그대로인데, 첫 부모 판에서 이 계약이 future·current로 선언한 명세 파일이 바뀜 | `edited` | 바뀐 PATH |
| 이 커밋에서 이 계약이 새 미확인이 됨(9.1). 위의 행과 따로 쓴다 | `unverified` | 그 커밋 OID |
| 연결이 생김·바뀜 | `undertaken` / `released` | 다른 끝 ID |
| 레코드 내용이 그 밖으로 바뀜 | `changed` | — |

  - 위 어느 행에도 해당하지 않는 커밋은 쓰지 않는다.
  - X의 행은 ARG 뒤에 `target ID`(그 판의 target)를 붙인다.
  - 이 판의 확인 행이 첫 부모 판과 다르면 `check reported`·`check forced`를 행 끝에 덧붙인다. 형식은 `event RID AGENT ACTION [ARG] [target ID] [check reported|forced]`다.
  - 한 커밋 안에서는 레코드 자신의 변화를 먼저 쓰고, 연결 변화를 다른 끝 ID 순서로 그 뒤에 쓴다.
  - 기록줄이 없는 커밋은 RID를 `-`, AGENT를 `?`로 쓴다(우회의 흔적).
- 7.9.6 **elsewhere**(HEAD에 없는 관련 요청): 대상은 ref 목록에서 도달하지만 HEAD에서는 도달하지 않는 요청 커밋 중, 그 요청이 적은 계약 파일의 판이 HEAD 판에 모두 포함되지는 않는 것이다(받았는지는 판의 포함으로 본다, 2.2.4). 보여 주기만 하므로 알림의 걸러 내기(9.2의 뜻 변화, 9.3)를 쓰지 않는다. ID ARG는 그 요청이 바꾼 레코드나 그 연결의 다른 끝에 그 ID가 있으면, 범위 ARG는 그 요청이 바꾼 레코드의 consumer·provider·명세 파일 경로(P판·C판)와 3.9로 겹치면(추상 범위도) 고른다. ARG가 없으면 모두 고른다.

```
elsewhere RID
ref REFNAME
agent NAME
subject 제목
contract ID ACTION [ARG] [target ID]   (이 요청이 바꾼 계약마다 7.9.5의 행. 그 요청 당시의 일이다)
```

  들일 방법은 hint가 준다(H11, 5.9a의 들이기).

- 7.9.7 peek은 읽기만 하고 구독을 바꾸지 않는다. 구독은 명시한 것뿐이기 때문이다(규칙 1). 보이는 것을 지켜보려면 `ait sub ARG`(7.2.4)를 쓴다.

### 7.10 contract push · 7.12 contract pull (규칙 8)

git은 커밋을 보낼 때 조상을 모두 함께 보내므로 보내는 쪽에서는 고를 수 없다. 그래서 고르는 일은 받는 쪽만 한다.

- 7.10.0 **계약 커밋**(push가 쓴다): 계약 경로(6.1)를 판정하는 상태는 H, 그 커밋, 그 부모다. 요청 커밋, 또는 요청 커밋이 아닌 병합 아닌 커밋 중 부모(뿌리면 빈 tree)에 대해 바꾼 경로가 하나 이상이고 모두 계약 경로인 것이다. 병합 커밋은 모든 부모와 다른 경로가 있고 그것이 모두 계약 경로이면 계약 커밋이다. 그 밖은 계약 커밋이 아니다.
- 7.10.1 `ait contract push [REMOTE] [--force -m MSG]`: REMOTE는 설정된 remote 이름이다(생략하면 upstream의 remote, 그것도 없으면 `origin`. URL이나 없는 이름은 `E_USAGE`). detached HEAD는 `E_BUSY`. 시작할 때 R(`refs/heads/B`)과 H를 정한다(5.5-1). 다음 순서로 보고, 막히면 멈춘다.
  1. `git fetch REMOTE '+refs/heads/*:refs/remotes/REMOTE/*'`로 원격의 모든 브랜치를 받는다(실패하면 `E_GIT`). 이어서 `git ls-remote --heads REMOTE`가 알린 OID 중 이 저장소에 있는 것을 **원격 끝**이라 한다. 원격에서 지워진 브랜치의 추적 ref는 원격 끝이 아니다. **내보낼 커밋**은 `git rev-list H --not 원격 끝…`이다. 비었으면 확인할 것이 없다: 원격의 R이 H면 `noop`, 아니면 5로 간다.
  2. 내보낼 커밋 중 계약 커밋이 하나도 없으면 `E_EMPTY`.
  3. H의 상태를 `validate`한다. H의 사고 판과 고정과 다른 명세 파일도 5.10의 오류다.
  4. 내보낼 커밋 중 어느 것에서 새 미확인이 된(9.1) current 계약이 H에서도 미확인이면(**내보낼 미확인**) `E_STATE`(`error: not allowed: unverified current contract(s) 'K'…`)다. `contract push`로는 확인된 계약을 보낸다(규칙 4·8). 그래서 원격에 아직 없는 새 미확인은 내보내는 쪽이 확인하고 보낸다. `--force`면 그 계약들의 provider를 H에서 모두 찾을 수 있을 때만, 그 계약들을 H로 다시 고정하고(1.5e) 확인 행을 2.2.2대로 쓰는(`--force`의 확인이다) 요청 커밋 하나를 만들어 공개하고(5.5. 메시지는 MSG에 5.3의 `[CONTRACT] ` 접두어, 기록줄은 `Ait-Request`·`Ait-Agent`·`Ait-Session`), 그 커밋을 H로 삼는다. 찾을 수 없는 계약이 있으면 `--force`여도 `E_CODE_REQUIRED`이고 hint는 계약마다 H8이다(5.14).
  5. `git push REMOTE H:R`. 계약 커밋이 아닌 커밋이 섞여 있어도 통째로 올린다.
  - 출력: `pushed REMOTE B`, 새로 공개된 요청마다 `request RID`(4에서 만든 커밋도. 위상 순, 오래된 것부터), 그리고 4에서 넘긴 계약마다 `check ID forced`.

- 7.11 받아 오기(fetch)만 하는 ait 명령은 없다. `git fetch`나 `ait listen --fetch SEC`로 한다.

- 7.12 `ait contract pull K[=C]… [도우미 절…] [--force] [-m MSG | -F FILE]`: 지정한 계약을 이력째 받는다(규칙 8).
  - K는 계약 ID다. C는 고를 판을 가진 커밋이거나 `HEAD`다. 연결 레코드는 `<D>-<O>=C`로 고른다.
  - 도우미 절은 7.3.1의 `clause`와 같은 꼴이다(`future amend K` 등). 6.1~6.4의 기준 H는 절을 적용하기 전의 받는 결과이고, U는 그 위에 절을 적용한 상태다. 그래서 관문의 계약 경로와 전이 검사는 절이 바꾼 것을 보며, 함께 받는 코드를 절의 코드 변경으로 보지 않는다. 수신으로 생기는 확인은 7.12.5, 절이 요구하는 확인은 6.5를 따르고, 공개는 7.12.6에서 한 커밋으로 한다. 절의 확인 집합이 있으면 과제를 쓰고 `E_NEEDS_RESULTS`로 멈추며, hint는 같은 pull에 `--force`를 더한 명령이다. pull은 `-r`를 받지 않으므로 `-r`를 요구하는 hint를 주지 않는다. `--force`면 그 절의 확인 집합도 받는 결과로 다시 고정하고 2.2.2대로 확인 행을 쓴다. provider를 찾을 수 없으면 6.5대로 `E_CODE_REQUIRED`이며, 코드가 충돌할 pull에는 `--force`를 받지 않는다(7.12.5, 결정 36).
  - 메시지는 고르기, 도우미 절, `--force`가 있을 때 필요하다(5.3). 없으면 `E_USAGE`다. 그 밖에는 없어도 된다.
  - 판정에 쓰는 것은 저장소의 데이터뿐이다: 계약 파일의 판과 그 `prev`·확인 행, 커밋, ref. 어떤 git 조작으로 커밋이 생겼는지, 기록줄이 있는지는 보지 않는다(규칙 2). 합치기, merge 설정, abort는 git의 일이고, ait는 그에 대한 장치나 보장을 더하지 않는다. ait가 정하는 것은 계약 데이터이고, 어긋난 것은 경고와 고치는 길로 드러난다.
  - 시점은 명령을 시작할 때의 HEAD, ref 목록(5.11), 객체다.
- **7.12의 낱말.** 계약 파일은 `.ait/<ID>`와 `.ait/undertake/<D>-<O>`이고, 판·본문·포함은 2.2.4, 사고 판과 형식이 틀린 판은 2.3.3, 끝난 판은 2.3.2의 것이다.
  - **후보 판**: 계약 파일마다 ref 목록의 각 ref 끝 커밋이 가진 판, HEAD의 판(**내 판**), 이번 pull이 들이는 커밋이 가진 판. 그 밖의 과거 커밋의 판은 후보가 아니다. 브랜치가 옛 판으로 돌아갔다면 그 브랜치는 옛 상태에 있을 뿐이다(규칙 2).
  - **최신 판**: 형식이 틀린 판과 사고 판이 아닌 후보 판 중, 그런 다른 후보 판에 포함되지 않는 판.
  - **앞으로 나감**(3-way): 두 판 v, w의 가장 나중 공통 조상(포함으로 본다)이 모두 같은 본문 B를 가지고, v의 본문은 B, w의 본문은 B가 아니면 w는 v에서 앞으로 나간 판이다. git merge가 공통 조상과 견주어 한쪽만 바꿨으면 고르지 않는 것과 같다. 가장 나중 공통 조상이 없거나, 그중 blob이 없는 것이 있으면 앞으로 나간 것으로 보지 않는다.
  - **남는 판**: 최신 판에서 다음을 차례로 뺀 것.
    1. 3-way: 최신 판 전체를 한 번에 본다. 다른 최신 판이 앞으로 나간 판은 뺀다. 다만 앞으로 나감이 꼬리를 물어 고리를 이루면 그 고리 위의 판은 빼지 않는다. 빼는 것은 동시에 한다. 그래서 남는 판이 하나도 없는 일은 없다.
    2. 끝난 쪽: 1을 거친 판에 끝난 판과 끝나지 않은 판이 함께 있으면 끝나지 않은 판을 뺀다. 끝난 상태에서는 나가는 길이 없기 때문이다(2.3.3). 3-way를 먼저 하므로 바꾼 것이 없는 판은 여기까지 오지 않는다.
  - **결과 판**: pull이 지정한 계약에 두는 판이다. 결과 판은 그 계약의 최신 판을 모두 포함한다.
    - 본문은 남는 판의 본문이다. 본문이 둘 이상이면 고른 판(`K=C`)의 본문이다.
    - 그 본문을 가진 남는 판이나 내 판 중 최신 판을 모두 포함하는 판이 있고, current D·O이면 그 확인 행이 아래 규칙으로 정한 것과 같으면 그 판이 결과다. 내 판이면 들일 것이 없다.
    - 없으면 새 판이다. 새 판은 그 본문에 `prev` = 최신 판 모두다. 내 판은 최신 판이거나 최신 판에 포함되므로 언제나 결과에 포함된다. current D·O면 확인 행을 둔다: 최신 판과 내 판 중 결과와 PINS가 같은 판에 `reported`가 있으면 `reported`, 없으면 `forced`다(2.2.2).
  - **들이는 커밋**(판 v의): ref 목록에서 닿는 커밋 중 그 계약 파일이 v이고 어느 부모에서도 v가 아닌 커밋이다. 여럿이면(사본) HEAD에 없는 커밋이 가장 적은 것, 그 수도 같으면 커밋 OID가 작은 것이다.
    - 결과 판이 들어오는 판 그대로면 그 판의 들이는 커밋을 들인다.
    - 결과가 새 판이면, 결과의 본문을 가진 남는 판들의 들이는 커밋 중 하나를 들인다. 내 판을 포함하는 판의 것을 먼저 고르고, 그다음 위의 순서다. 내 상태 위에 쌓은 쪽은 git에서 fast-forward로 판단 없이 받는 것과 같기 때문이다. 다른 쪽을 원하면 `K=C`로 고른다. `K=C`면 C의 판의 것이다. 내 판이 그 본문을 가지면 아무것도 들이지 않는다.
  - **들어오는 계약**: 들이는 커밋이 가진 판 중 내 판에 포함되지 않고 형식이 틀린 판도 사고 판도 아닌 것이 있는 계약이다. 연결 레코드 `<D>-<O>`가 그렇다면 D와 O다(9.2-2와 같다). 들이는 커밋의 이력이 옛 판으로 돌아갔거나 레코드를 지웠으면 들어오는 계약이 아니다.
  - **합친 결과**: HEAD와 들이는 커밋들을 git(`git merge-tree --write-tree`)으로 차례로 합친 결과다. 어떻게 합치는지(설정, rename, merge base)는 git의 일이다. 계약 파일은 pull이 정하고, 명세 파일은 받지 않을 변경을 뺀 뒤 같은 실행에서 합친다(아래 받는 결과).
    - 충돌은 파일마다 한 번이다. 어떤 차례에서 이미 충돌한 파일을 뒤의 끝이 또 바꾸면(충돌하든 안 하든) 그 차례로는 받을 수 없다. "또 바꾸면"은 지금까지 합친 결과와 그 끝의 merge base에서 그 끝이 바꾸는 것이다.
    - 차례: 끝의 차례 중 파일마다 한 번만 충돌하는 차례가 있으면 그 차례로 받는다. 여럿이면 끝의 커밋 OID를 차례대로 늘어놓아 가장 앞선 차례다. 그런 차례가 없으면 7.12.4에서 멈춘다. 충돌 여부는 지금까지 합친 결과에 대고 본다.
    - 계약 파일의 충돌은 코드 충돌이 아니다. pull이 정하기 때문이다. 명세 파일은 받지 않을 변경을 뺀 뒤 git이 합치므로, 그래도 충돌하면 코드 충돌이다. 제외한 뒤의 내용과 git이 고른 merge base의 차이로 충돌해도 git의 충돌로 처리한다(결정 37).
  - **받는 결과**: 합친 결과 위에 pull이 정한 것을 놓은 상태다.
    - **`.ait/` 아래의 모든 경로**: 지정한 계약은 결과 판을, 지정하지 않은 계약은 내 판을 둔다. 지정하지 않은 계약이 들어오는 계약이면 7.12.3에서 멈추므로, 여기까지 오면 내 판이 맞다. 레코드는 지우지 않는다(2.1.5). 계약 파일이 아닌 경로와, 지정하지 않은 계약의 형식이 틀린 판·사고 판은 받지 않고 경고한다(7.12.6).
    - **명세 파일**: ait는 받지 않을 변경만 정하고, 합치기는 git이 한다.
      - 아래에서 명세 파일의 내용은 mode와 blob을 함께 비교한다(2.4.1). 받지 않는 변경을 빼거나 사고인 쪽의 고정으로 볼 때도 둘을 함께 둔다.
      - **두 쪽**: 내 쪽(HEAD)과 들이는 쪽(들이는 커밋마다)이다. ref 끝이 아니다. 한 쪽의 변경은 그 커밋과 HEAD의 merge base에 견준 경로마다의 변경이다. 내 쪽은 합치는 차례의 첫 끝과의 merge base에 견준다. 쪽마다 한 번만 본다. 앞 끝을 합친 내용을 다시 보지 않는다.
      - **결과의 고정**: 받는 결과(지정한 계약은 결과 판, 지정하지 않은 계약은 내 판)에서 future·current인 계약의 고정이다. 도우미 절과 다시 고정을 하기 전의 것이다.
      - **그 쪽에서 고정한 판**: 그 쪽 커밋에서 future·current인 판이다. 끝난 판은 고정하지 않는다(2.4.1). 형식이 틀린 판은 state를 믿을 수 없으므로 state와 상관없이, `file` 행(낱말을 공백으로 나눠 읽는다)이 그 경로를 적으면 센다.
      - 한 쪽의 변경은 다음 차례로 본다.
        1. 결과의 고정 중 하나가 그 내용이면 받는다. 결과가 새 판이어도 그렇다. 판이 아니라 고정으로 견주기 때문이다.
        2. 아니고, 그 쪽에서 고정한 판이 그 내용으로 고정하면 옛 판이나 버린 판의 명세다. 받지 않는다.
        3. 아니고, 그 쪽에서 고정한 판이 그 경로를 다르게 고정하면 그 쪽의 사고다(2.4). 경고하고, 그 쪽의 내용은 그 판의 고정으로 본다. 그것이 결과의 고정이면 받고, 아니면 받지 않는다.
        4. 아니면 그 쪽의 코드 변경이다. 받는다. 다만 결과가 고정하는 경로를 지우는 변경은 받지 않고 경고한다.
      - **받지 않는 변경은 git에 넘기기 전에 그 쪽에서 뺀다.** 그 경로를 결과의 고정으로 바꾼 tree(결과가 그 경로를 한 내용으로 고정하지 않으면 merge base의 내용)를 그 커밋의 자리에 두고, git이 코드와 함께 같은 차례로 합친다. 결과가 고정하지 않는 경로는 분류에 쓴 merge base의 내용으로 두지만, 뒤의 합치기에서 git이 더 새로운 merge base를 골라 충돌할 수 있다. 그 충돌을 없애려고 merge base의 tree를 바꾸지 않는다. merge base 선택과 그 내용, rename, 충돌은 git의 것이고, 합치는 차례는 위 규칙대로 정한다(결정 37). 대체 tree를 담은 임시 커밋은 원래 커밋을 부모로 두며, 중간 합친 커밋에서도 원래 HEAD와 이미 들이는 끝들에 닿는다. 그러므로 뒤의 merge base가 원래 끝을 볼 수 있다. 대체 tree와 임시 커밋은 객체 저장소에만 쓰고, 공개할 커밋의 부모는 7.12.6의 원래 HEAD와 들인 커밋들이다.
      - 합친 명세 파일이 결과의 고정과 다르면 그 내용을 두고 6.2로 다시 고정한다. 다시 고정할 계약이 current면 7.12.5에서 멈춘다. 충돌하면 코드 충돌이다.
      - 그래서 옛 판, 버린 판, 사고의 명세 변경은 들어오지 않는다. 결과의 다른 계약이 같은 내용을 고정하면 1로 받는다. 어느 쪽의 코드 변경도 알리지 않고 사라지지 않는다. 끝난 결과의 옛 명세 파일처럼 어느 쪽에서도 고정하지 않은 파일은 코드다.
    - **도우미 절, `--force`, 6.2의 다시 고정**: 결과 판 위에 적용한다. 결과 판이 새 판 V1이면, 그 위에 얹는 판의 `prev`는 V1과 V1의 `prev` 모두다. V1과 같은 고르기만 한 다른 에이전트와도 모이고, V1의 blob이 없는 clone에서도 나머지 `prev`로 모인다. 6.2의 다시 고정은 바뀐 명세 파일을 선언한 future·current 계약을 모두 다시 고정한다.
  - **pull이 쓰는 경로**: 받는 결과가 HEAD와 다른 경로 모두다(코드 포함). git이 충돌로 알린 모든 경로도 든다. 결과의 파일 내용이 HEAD와 같거나 충돌 표지가 없어도, index에 충돌 stage를 쓸 경로는 포함한다. pull은 이 경로만 index와 작업 폴더에 쓴다.

  아래 7.12.0~7.12.5에서 멈추면 아무것도 바꾸지 않는다. 바꾸지 않는 것은 ref, HEAD, index, 작업 폴더, ait 상태다. 객체 저장소에 객체가 느는 것과 도우미 절의 확인 거절 때 6.5·8장대로 확인 과제 파일을 쓰는 것은 예외다. 사용자의 merge driver가 하는 일은 git merge와 같이 이 보장 밖이다.

- 7.12.0 **시작 조건**
  - 5.2를 따른다. `.ait/`가 깨끗하고, 진행 중인 git 조작이 없고, 브랜치 위에 있어야 한다.
  - HEAD에 이미 있는 오류(5.10. HEAD의 사고 판, 형식이 틀린 판, 고정과 다른 명세 파일을 포함한다)로 막히면 그 오류와 hint를 쓴다.
  - pull이 쓰는 경로에 index나 작업 폴더의 변경이 있거나 추적하지 않는 파일이 있으면 멈춘다(`E_DIRTY`). 무시되는 파일(`.gitignore`, `info/exclude`, `core.excludesFile`)은 git merge처럼 덮어쓴다. 받는 결과가 merge이고 index가 HEAD와 다르면 `E_DIRTY`(`error: uncommitted changes in the index`)다. pull이 쓰는 경로는 받는 결과를 정한 뒤에 알 수 있으므로, 이 검사는 7.12.5 다음, 아무것도 바꾸기 전에 한다.
    - 그 경로가 계약 경로(6.1. 판정하는 상태는 HEAD)이면 hint는 ait로 커밋하는 길이다: `commit your changes with 'ait contract add PATH' and 'ait contract commit -m <why>', or set them aside, before you pull`. 코드 경로이면 `commit or set aside your changes to 'PATH' before you pull`, 추적하지 않는 파일이면 `move or remove 'PATH' before you pull`이다. 낱말과 문장의 꼴은 git merge의 같은 경우를 따른다.
  - 들이는 커밋이나 그 merge base가 shallow 경계 밖이면 멈춘다(`E_SHALLOW`). hint는 이루어질 상태다. git 명령은 적지 않는다. partial clone에서는 git이 객체를 받아 오는 대로 읽고, 받아 오지 못하면 멈춘다.

- 7.12.1 **판 정하기.** 지정한 계약마다, 그리고 지정한 계약이 끝인 연결 레코드마다 다음을 본다.
  - 형식이 틀린 판과 사고 판은 후보에서 빼고 `warning: skipping malformed '.ait/K' at 'C'`, `warning: skipping 'K' at 'REF' (C): it was changed outside ait's rules`를 쓴다. 그 브랜치를 고칠 때까지 pull마다 나온다.
  - 남는 판을 구한다. 끝난 쪽 규칙으로 판이 빠졌으면 경고한다. 빠진 것이 내 판이면 `warning: your change to 'K' is overridden: 'K' was withdrawn in 'C'`, 다른 브랜치의 판이면 `warning: the change to 'K' in 'C2' is overridden: 'K' was withdrawn in 'C'`다(`cancelled`면 `was cancelled`). 빠진 판을 처음 담은 커밋들의 `Ait-Agent`는 받는 쪽이다(9.3).
  - **남는 판의 본문이 하나면** 그것으로 결과 판을 정한다(7.12의 낱말). 결과 판이 내 판이면 들일 것이 없다.
  - **갈라짐**: 남는 판의 본문이 둘 이상인데 고르지 않았으면 멈춘다(`E_DIVERGED`). 셋 이상이 꼬리를 무는 경우도 같다. 문장은 고를 판의 커밋을 모두 쓴다. 끝난 쪽 규칙으로 빠진 판은 쓰지 않는다.
    ```
    error: 'D' has diverged between 'C1' (REF) and 'HEAD'
    hint: pick one: 'ait contract pull D=C1 -m <why>' or 'ait contract pull D=HEAD -m <why>'
    ```
    - hint는 원래 지정한 이름과 이미 고른 것을 모두 둔 한 명령이다. 갈라진 계약이 여럿이면, 모두 같은 커밋을 고르는 명령들을 먼저 주고 그다음 나머지 선택을 준다.
    - 고르기를 7.12.4로 미리 시험하지 않는다. 고른 pull이 막히면 그 pull이 알린다(사용자 결정 54). 고른 판이 이름을 더 요구하면(7.12.3) 그 pull이 알린다. 다만 7.12.4의 묶음 무결성(한 명세 파일의 고정이 맞지 않는다)으로 거절한 pull의 hint는, 그 판정이 아는 대로 이름을 채우고 그것을 지나는 선택만 준다.
  - **`K=C`**: C(또는 `HEAD`)의 판이 남는 판 중 하나여야 한다. 결과 판은 그 본문으로 정한다. 그래서 고른 판이 내 판을 포함하지 않으면, 남는 판이 하나여도 새 판이다. `K=HEAD`이고 내 판이 최신 판을 모두 포함하면 들일 것이 없다. C의 판이 남는 판이 아니면 멈춘다(`E_PICK`). 그 계약이 갈라져 있으면 지금의 고르기 hint를, 갈라져 있지 않으면 고르기를 뺀 `pull K…`를 준다.
  - 지정한 계약에 후보 판이 하나도 없으면 `E_ID`다. 후보가 있었는데 모두 형식이 틀린 판이나 사고 판이라 빠졌으면 `E_ACCIDENT`다(`error: 'K' at 'REF' (C) was changed outside ait's rules`와 `hint: 'K' has to be fixed on branch 'REF' first`). 같은 명령을 다시 주는 hint는 쓰지 않는다.
  - 도우미 절과 `--force`가 없고, 지정한 계약이 모두 들일 것이 없고 새로 적을 것도 없으면 session의 유효성을 보고(필요하지는 않다, 5.1) `already K…`를 쓰고 exit 0으로 끝난다. 도우미 절이나 `--force`가 있으면 7.12.5까지 적용한 뒤 새로 적을 것이 없는지를 본다. 이미 받은 계약을 지정했다는 이유로 함께 준 절을 버리지 않는다.

- 7.12.2 **들이는 커밋 모으기.** 7.12.1에서 정한 들이는 커밋 중 다른 들이는 커밋의 조상이거나 HEAD의 조상인 것은 뺀다. 남는 판의 들이는 커밋이 HEAD의 조상이면 내 브랜치가 git으로 옛 판에 돌아간 것이다. 들일 커밋은 없고, 그 판과 그 명세 파일을 HEAD 위의 커밋(요청 커밋)으로 적는다. 들이는 끝이 하나도 없으면 결과의 고정(7.12의 낱말)이 한 PATH에 같은 MODE·BLOB을 요구할 때만 그 파일을 복원한다. 서로 다른 고정을 요구하면 7.12.4에서 `E_INCOMPLETE`로 멈추고, 같은 명세 파일을 고정한 판을 고르는 hint를 준다. 조상 커밋을 merge 끝으로 다시 넣거나 충돌 상태를 만들지 않는다(결정 35). 코드는 오지 않으므로 7.12.5가 판정한다.

- 7.12.3 **지정 검사.** 들어오는 계약은 모두 지정되어 있어야 한다. 아니면 멈춘다(`E_UNNAMED`).
  ```
  error: taking 'C' also changes contracts you did not name: 'Q'
  hint: name them too: 'ait contract pull D Q'
  ```
  - 연결 레코드가 들어오면 문장은 `… you did not name: 'O' (undertake 'D-O')`다.
  - 들이는 커밋이 가진 판은 후보 판이 되고, 더해야 할 이름이 끝인 연결 레코드도 새로 보이게 된다. 그래서 이름과 후보가 더 늘지 않을 때까지 7.12.1~7.12.3을 되풀이하고, 그때의 이름 모두를 한 hint에 준다. `K=C`의 검사는 그 뒤에 한다.

- 7.12.4 **구조 검사**
  - 먼저 합친 결과를 구한다. 어느 차례로도 이미 충돌한 파일을 뒤의 끝이 또 바꾸면 멈춘다(`E_SPLIT`).
    ```
    error: 'C1' and 'C2' both change 'src/x.c', which you changed too
    hint: take them one at a time: 'ait contract pull D=C1 -m <why>', then 'ait contract pull Q'
    ```
    - 문장은 그 파일들을 바꾸는 끝과 두 번 걸리는 파일을 모두 쓴다(`'C1', 'C2' and 'C3' all change 'src/x.c'`, `… both change 'src/x.c' and 'src/y.c'`). `which you changed too`는 내가 그 파일을 바꿨을 때만 붙인다.
    - **나눠 받는 hint**는 명령마다 한 묶음의 끝을 받는다. 묶음은 함께만 받을 수 있는 끝들이다. 한 끝을 받으면 지정 검사(7.12.3)가 다른 끝의 판까지 요구하면 그렇다. 명령의 차례는 앞 명령들을 받은 뒤의 상태에서 어느 명령도 7.12.3이나 7.12.4에서 멈추지 않는 차례다. 그런 차례가 여럿이면 끝의 커밋 OID 순으로 가장 앞선 차례다. 둘째 명령부터는 앞 명령을 받은 뒤의 상태로 이름과 고르기를 정한다.
    - 그런 차례가 없으면 까닭을 오류 행에, 받을 수 있게 되는 상태를 hint에 쓴다.
      ```
      error: 'C1' and 'C2' can only be taken together, but both change 'src/x.c', which you changed too
      hint: they can be taken once one of them is rebased onto the other, or once your branch merges cleanly with one of them
      ```
      - 앞의 상태: 한 끝이 다른 끝 위로 rebase되면 들이는 커밋이 하나가 되어 파일마다 한 번만 충돌한다. merge로 합친 커밋은 이 상태가 아니다. 판이 처음 들어간 커밋은 그대로이기 때문이다.
      - 뒤의 상태: 내 브랜치가 한 끝과 깨끗하게 merge되면, 그 끝을 먼저 받아 나머지 끝에서 파일마다 한 번만 충돌한다. 끝이 둘일 때 언제나 맞다. 끝이 셋 이상이면 앞의 상태만 쓴다(`once one of them is rebased onto the others`).
  - 받는 결과는 validate(5.9 a~e)를 지나야 한다. 다만 git이 충돌로 남긴 명세 파일 경로의 묶음 무결성(e)은 해결한 내용을 다시 고정하는 마치는 커밋(7.1.3-2·7)에서 검사한다. 충돌 상태에는 하나의 파일 내용이 없으므로, future 계약의 서로 다른 고정이나 충돌 표지로 이 단계에서 거절하지 않는다(결정 29). current 계약이 확인하거나 고정한 파일의 충돌은 7.12.5에서 멈춘다. 그 밖의 위반은 그 오류(5.8)로 멈춘다. HEAD에 이미 있던 위반은 7.12.0에서 다루므로, 여기서는 이 pull이 바꾼 레코드에 얽힌 위반만 본다.
  - 위반에 얽힌 레코드의 들이는 쪽 판이 사고 판이나 형식이 틀린 판으로 빠졌으면, 그 사실을 오류로 쓴다(`E_ACCIDENT`). 고칠 것은 그 브랜치에 있기 때문이다.
    ```
    error: taking 'C' needs 'D' as 'REF' has it, but 'D' at 'REF' (C2) was changed outside ait's rules
    hint: 'D' has to be fixed on branch 'REF' first
    ```
  - 들이는 끝이 없어서 한 명세 파일의 서로 다른 고정을 함께 복원할 수 없는 경우도 묶음 무결성(e) 위반이다. git이 만든 충돌이 아니므로 검사를 미루거나 6.2로 고른 판의 고정을 바꾸지 않는다.
  - 그 밖에는 hint가 모든 위반을 푸는 한 명령이다. 절은 계약 ID 순으로 한 번씩 담는다.
    - 다른 계약을 지정하면 풀리는 경우: 그 이름을 더한다.
    - 판을 맞춰 고르면 풀리는 경우: 맞는 선택들을 준다(7.12.1의 갈라짐과 같다).
    - 계약을 되돌리면 풀리는 경우: `future amend` 절을 더한다. current offer가 떠맡은 계약을 되돌리면 그 offer도 함께 되돌린다(5.9 d). 되돌리기는 누구의 계약이든 할 수 있다(1.5d).

- 7.12.5 **확인 검사**(규칙 4). 받는 결과가 **새로** 미확인을 만들거나, 확인 없는 명세 변경을 만들면 멈춘다(`E_UNCHECKED`). 그래서 ait 명령인 pull은 미확인도 사고도 새로 만들지 않는다.
  - **새 미확인**: 받는 결과에서 current 계약의 provider 코드가 그 판의 고정과 다른데, 같은 고정으로 이미 미확인이던 부모(HEAD나 들이는 커밋)가 없는 것이다(9.1의 "새 미확인").
  - **확인 없는 명세 변경**: 6.2의 다시 고정으로 current 계약의 고정이 바뀌는 것이다.
  - **이미 미확인이던 것**: 막지 않는다. 새 HEAD에서 미확인이고 옛 HEAD에서는 아니던 계약 중 현재 session의 약속·요구·구독(9.3)인 것이면 `warning: 'K' comes in unverified`와 H7·H8을 쓴다. 합친 코드가 새 내용이면 그 코드로 미확인 사건이 하나 더 생긴다(9.1).
  - hint는 그 계약이 어디서 왔느냐에 따라 다르다.
    - 내 브랜치(HEAD)에 있던 계약 K. 누가 만들었는지는 상관없다.
      ```
      error: taking 'C' changes what contract 'K' checked: 'src/stock.c'
      hint: reopen it in the same request: 'ait contract pull D future amend K -m <why>'
      hint: or, if the change is trivial for 'K': 'ait contract pull D --force -m <why>'
      ```
    - 들어오는 계약 D: 내 변경이 합쳐져 D가 확인한 것과 달라졌다.
      ```
      error: taking 'C' merges your changes into what 'D' checked: 'src/stock.c'
      hint: take it reopened: 'ait contract pull D future amend D -m <why>'
      hint: or, if your changes are trivial for 'D': 'ait contract pull D --force -m <why>'
      ```
    - 되돌리기 절은 5.9a를 따른다. current offer가 그 계약을 떠맡았으면 그 offer도 함께 되돌린다.
    - 들이는 커밋이 없으면(7.12.2의 옛 판) 문장은 `error: 'D' at 'C' checked code your branch no longer has: 'src/stock.c'`이다.
    - 받는 결과에서 provider를 찾을 수 없게 되면 되돌리기 절만 준다. 범위를 옮기는 명령은 받는 결과의 코드가 있어야 해서 지금 따를 수 없기 때문이다.
    - `--force` hint는 받는 결과에 코드 충돌이 없을 때만 준다.
  - 코드 충돌은 아래의 충돌 진단으로 먼저 검사한다. `--force`가 있으면 코드 충돌 자체의 거절이고, 없으면 확인한 파일의 코드 충돌을 본다. 충돌 표지를 일반 코드 변경으로 보아 새 미확인 진단을 먼저 내지 않는다.
  - **확인한 파일의 코드 충돌**: 코드가 충돌하는 파일을 받는 결과의 current 계약이 확인하거나 고정하고 있으면 멈춘다(`error: taking 'C' conflicts in what 'D' checked: 'src/stock.c'`). 충돌을 해결한 내용은 아직 아무도 확인하지 않았기 때문이다. hint는 되돌리기 절을 더한 명령뿐이다.
  - **`--force`**: 이 확인 검사에서 막힌 current 계약마다 받는 결과로 다시 고정하고 확인 행을 2.2.2대로 쓴다(`--force`의 확인). 받는 결과에서 코드가 충돌할 pull에는 `--force`를 받지 않는다(`error: --force cannot take a pull whose code conflicts: 'PATH'`, hint는 되돌리기 절).
  - **명세 파일을 공유한 future 계약**: 멈추지 않는다. 6.2대로 다시 고정된 채 받는다.
  - 이어서 session을 본다(5.1, 5.13). 요청 커밋을 만들 것이면(7.12.6) session이 필요하고, 요청 커밋을 만들지 않으면 session이 없어도 되지만 설정된 값이 무효이면 거절한다. 그다음 7.12.0의 작업 폴더 검사를 한다.

- 7.12.6 **받기**
  - 들일 커밋이 하나이고, HEAD가 그 조상이고, 받는 결과가 그 커밋의 tree와 같으면 fast-forward한다.
  - 그 밖에는 pull이 `commit-tree`로 커밋 하나를 직접 만든다(hook을 거치지 않는다, 11.2). 부모는 HEAD와 들인 커밋들이다. 중간 커밋은 없다. 들인 커밋이 없으면 HEAD 위의 커밋이다.
  - 공개는 5.5를 따른다(5.5-6). pull이 쓰는 경로(코드 포함)를 index와 작업 폴더에 쓴다. git merge와 같다.
  - pull이 만든 커밋에서 어느 계약 파일이 모든 부모의 판과 다르면 그 커밋은 요청 커밋이다(기록줄, 5.4. 7.1.5와 같은 기준). 새 판, 도우미 절, `--force`, 6.2의 다시 고정, 7.12.2의 옛 판이 여기에 든다. 메시지는 요청 커밋이 아니어도 같다: 준 메시지(5.3의 접두어를 붙인다), 없으면 ait가 쓰는 `[CONTRACT] pull K…`(지정한 계약 ID를 공백으로 잇는다)다. 되돌린 커밋(1.5d)이면 그 `Ait-Agent`가 되돌린 쪽이다.
  - **코드가 충돌하면**(7.12.5의 "확인한 파일의 코드 충돌"에 걸리지 않으면) git처럼 멈춘다. 충돌은 파일마다 한 번이다.
    - pull이 merge 상태를 직접 만든다. `git merge --no-ff`가 만드는 것과 같은 꼴이다(MERGE_HEAD에 받을 끝 모두를 합치는 차례대로 적고, MERGE_MODE는 `no-ff`). pull이 쓰는 경로만 index와 작업 폴더에 쓴다. 계약 파일과 명세 파일은 정한 내용으로, 충돌한 코드는 충돌 상태로 둔다. 받는 변경끼리 충돌한 고정 명세 파일도 충돌 상태로 둔다.
    - merge 메시지(MERGE_MSG)는 pull이 쓸 커밋 메시지다(메시지와 기록줄).
    - 출력은 git merge의 충돌 행(`CONFLICT (content): Merge conflict in src/x.c`, `Automatic merge failed; fix conflicts and then commit the result.`. git처럼 stdout)과 마치는 hint(7.1.3)다. 종료 코드는 git merge와 같은 1이다. 현재 session이 이 worktree의 기본 session이 아니면 마치는 커밋 앞에 `AIT_SESSION=SID `를 붙인다(5.9a).
      ```
      hint: commit the resolution first: 'git commit --allow-empty --no-edit --cleanup=strip', then continue as git says
      ```
    - 마치는 커밋의 부모는 HEAD와 받을 끝 모두다. 그 커밋의 hook은 데이터만 본다(7.1.3): 들어오는 쪽의 사고 판과 형식이 틀린 판은 출발 판에서 빠지고, pull이 정한 계약 파일은 출발 판을 모두 포함하므로 다시 고르지 않으며(7.1.3-5), 해결하며 바뀐 명세 파일에는 future 계약의 다시 고정만 한다(7.1.3-2. 고정이 바뀐 current는 7.1.3-3이 되돌린다).
    - abort는 `git merge --abort`가 하는 대로다. ait는 그 밖의 보장을 하지 않는다.
  - **출력**: 들인 계약마다 `included K from 'C'`. 7.12.2에서 들이는 커밋이 HEAD의 조상이어서 빠졌어도 그 판을 복원하면 `included K from 'C'`이며 C는 그 판의 들이는 커밋이다. 내 판의 본문으로 들일 것 없이 새 판만 적었으면 `kept K`. 7.12.1의 warning, 받는 결과의 건너뛰기 warning(`warning: skipping '.ait/PATH' in 'C': not a contract file`, `warning: skipping the removal of 'Q' in 'C': records are never removed`, `warning: skipping 'PATH' at 'C': it was changed outside ait's rules`, `warning: skipping the removal of 'PATH' in 'C': 'K' pins it`), 7.12.5의 `comes in unverified`도 쓴다.
    - 요청 커밋을 만들면 다른 요청 명령과 같은 출력 행(6.7: `request RID` 등, 확인 행을 쓴 계약마다 `check K reported|forced`)을 쓴다.
    - 이 커밋이 되돌린 커밋(1.5d)이면 `warning: 'K' is reopened by this pull`과 H10을 쓴다(규칙 6). 내가 이제 그 계약을 다시 이행할 쪽이기 때문이다. 이유는 7.1.5의 조건이 맞을 때만, 처음 맞는 하나를 붙인다(예: 결과의 본문이 future인 출발 판과 같으면 `: it keeps the future version over the current one`). 맞는 조건이 없으면 붙이지 않는다.

- 7.12.7 **출력 목록.** 커밋은 전체 OID로 쓴다(5.6). `'C' (REF)`는 커밋과 그것을 끝으로 가진 ref이고, `'K' at 'REF' (C)`는 ref와 그 ref에서 그 판을 담은 커밋이다. ref는 짧은 이름이다(`refs/heads/a`는 `a`, `refs/remotes/origin/a`는 `origin/a`). 끝이 셋 이상이면 `'C1', 'C2' and 'C3'`처럼 잇는다. `fatal:`인 것은 shallow뿐이다. 이 clone이 가진 것으로는 판정할 수 없기 때문이다. 그 밖의 멈춤은 받을 수 있는 상태로 가는 길이 있으므로 `error:`다.

| 경우 | 오류 | 첫 행 | hint |
| --- | --- | --- | --- |
| 갈라짐(7.12.1) | `E_DIVERGED` | `error: 'D' has diverged between 'C1' (REF) and 'HEAD'` | `pick one: 'ait contract pull D=C1 -m <why>' or 'ait contract pull D=HEAD -m <why>'` |
| 고른 판이 남는 판이 아님(7.12.1) | `E_PICK` | `error: 'C' has no version of 'D' to choose: a newer one includes it` | 갈라져 있으면 고르기 hint, 아니면 `take it as it is: 'ait contract pull D'` |
| 후보가 사고 판·형식이 틀린 판뿐(7.12.1) | `E_ACCIDENT` | `error: 'W' at 'REF' (C) was changed outside ait's rules` | `'W' has to be fixed on branch 'REF' first` |
| 지정(7.12.3) | `E_UNNAMED` | `error: taking 'C' also changes contracts you did not name: 'Q'` | `name them too: 'ait contract pull D Q'` |
| 구조 위반이 사고 판에서 옴(7.12.4) | `E_ACCIDENT` | `error: taking 'C' needs 'D' as 'REF' has it, but 'D' at 'REF' (C2) was changed outside ait's rules` | `'D' has to be fixed on branch 'REF' first` |
| 구조 위반(7.12.4) | 5.8의 그 오류 | `error: not allowed: …` 등 | 모든 위반을 푸는 한 명령 |
| 같은 파일이 두 번 걸림, 나눠 받을 수 있음(7.12.4) | `E_SPLIT` | `error: 'C1' and 'C2' both change 'src/x.c', which you changed too` | `take them one at a time: '…', then '…'` |
| 같은 파일이 두 번 걸림, 함께만(7.12.4) | `E_SPLIT` | `error: 'C1' and 'C2' can only be taken together, but both change 'src/x.c', which you changed too` | `they can be taken once one of them is rebased onto the other, or once your branch merges cleanly with one of them` |
| 내 계약이 확인한 것이 바뀜(7.12.5) | `E_UNCHECKED` | `error: taking 'C' changes what contract 'K' checked: 'src/stock.c'` | 되돌리기 절, 이어서 `--force` |
| 들어오는 계약이 확인한 것에 내 변경이 합쳐짐(7.12.5) | `E_UNCHECKED` | `error: taking 'C' merges your changes into what 'D' checked: 'src/stock.c'` | 되돌리기 절, 이어서 `--force` |
| 들이는 커밋 없이 확인한 코드가 없음(7.12.5) | `E_UNCHECKED` | `error: 'D' at 'C' checked code your branch no longer has: 'src/stock.c'` | 되돌리기 절, 이어서 `--force` |
| 확인한 파일의 코드 충돌(7.12.5) | `E_UNCHECKED` | `error: taking 'C' conflicts in what 'D' checked: 'src/stock.c'` | 되돌리기 절만 |
| 코드가 충돌할 pull의 `--force`(7.12.5) | `E_UNCHECKED` | `error: --force cannot take a pull whose code conflicts: 'src/other.c'` | 되돌리기 절 |
| 작업 폴더의 변경(7.12.0) | `E_DIRTY` | `error: your local changes to 'specs/d.yaml' would be overwritten by this pull` | 7.12.0의 두 꼴 |
| 추적하지 않는 파일(7.12.0) | `E_DIRTY` | `error: untracked working tree files would be overwritten by this pull: 'specs/q.yaml'` | `move or remove 'specs/q.yaml' before you pull` |
| index의 변경(7.12.0) | `E_DIRTY` | `error: uncommitted changes in the index` | 5.8 그대로 |
| shallow(7.12.0) | `E_SHALLOW` | `fatal: 'C' is beyond the shallow boundary of this clone` | `'C' can be taken once this clone has its history down to the merge base with HEAD` |
| HEAD에 이미 있는 오류(7.12.0) | 5.10 | 5.10의 오류 | 5.10의 hint |
| 코드 충돌(7.12.6) | — (git merge와 같은 1) | git merge의 충돌 행(stdout) | 마치는 hint |

  그래서:
  - 고르기로 멈추는 것은 두 쪽이 공통 조상 뒤에 **둘 다** 바꿨을 때(셋 이상이 꼬리를 무는 경우 포함), 서로 다른 끝난 판이 남았을 때, blob이 없어 공통 조상의 본문을 모를 때뿐이다. 사본, 이미 합쳐진 판, 본문이 같은 판, 한쪽만 바뀐 판, 끝난 판과 끝나지 않은 판으로는 멈추지 않는다.
  - 결과 판은 언제나 최신 판을 모두 포함한다. 그래서 끝난 쪽 경고와 갈라짐은 다음 pull에서 되풀이되지 않는다. 건너뛰기 경고는 그 브랜치를 고칠 때까지 pull마다 나온다.
  - 보지 않은 계약은 들어오지 않는다(7.12.3). 코드는 지정 없이 언제나 함께 들어온다.
  - 계약 파일은 git merge에 맡기지 않는다. 명세 파일은 ait가 받지 않을 변경만 정하고 git이 합친다. 그래서 옛 판, 지운 레코드, 버린 판·사고의 명세 변경이 섞여 들어오지 않고, 어느 쪽의 코드 변경도 알리지 않고 사라지지 않는다.
  - 멈추면 언제나 따를 hint나 받을 수 있게 되는 상태가 있다.
  - pull은 미확인도, 사고 판도 새로 만들지 않는다.

### 7.13 merge driver · 7.14 git 통과

- 7.13.1 `ait merge-driver BASE OURS THEIRS PATH`:
  1. OURS와 THEIRS가 같으면 exit 0.
  2. THEIRS의 prev DAG에 OURS의 blob이 있으면 THEIRS를 쓰고 exit 0.
  3. OURS의 DAG에 THEIRS의 blob이 있으면 OURS를 그대로 두고 exit 0.
  4. BASE·OURS·THEIRS가 모두 current이고, 양쪽 모두 BASE를 다시 고정한 판이면(1.5e. prev와 확인 행은 빼고 본다) OURS를 그대로 두고 exit 0. 확인 행은 통합 hook이 정한다(7.1.3-3). 양쪽이 각자 다시 고정한 것이므로 뜻이 같다. 합쳐진 코드가 OURS의 고정과 다르면 미확인으로 드러난다(9.1).
  5. 그 밖에는 충돌 표지(`<<<<<<< ours` … `>>>>>>> theirs`)를 쓰고 exit 1. stderr에 hint `keep 'SIDE' for 'PATH' and stage it ('ours' is HEAD; 'theirs' is the incoming commit, your stashed change, or in a rebase your own commit)`와 `then commit it through ait: 'ait contract add PATH' and 'ait contract commit -m <why>' (if a git operation stopped, 'ait contract add' says how to finish it first)`를 쓴다(5.10의 틀린 레코드도 여기로 온다). driver는 git이 어느 조작으로 멈출지 모르지만 `E_BUSY`는 멈춘 상태를 보고 마치는 hint와 조작 안내를 준다.
     - 고를 쪽 SIDE: `git am`(`rebase-apply/applying`)이면 ours다(git am은 ait를 거치지 않고 커밋한다). 그 밖에는 한쪽 판만 형식이 틀리면(5.10) 다른 쪽, 한쪽 판에서만 다른 쪽 판으로 2.3의 전이로 갈 수 있으면 그 다른 쪽, 아니면 이 브랜치의 판이다: rebase 디렉터리가 있으면 theirs, 그 밖에는 ours.
     - session을 보지 않으므로(5.1) `AIT_SESSION` 접두를 붙이지 않는다.

  DAG를 따라가다 없는 blob을 만나면 그 가지는 거기서 끝난 것으로 본다. 노드는 최대 10000개까지 본다. 내부 오류는 5단계(충돌)로 처리한다. `.ait/init`은 레코드가 아니므로 1을 거치지 않으면 5단계다.
- 7.14.1 첫 인자가 `init hook merge-driver session sub unsub listen contract ct demand offer future amend undertake release fulfill commit todo peek` 중 하나가 아니면 그대로 `git`을 실행한다. 인자가 없으면 사용법을 쓰고 exit 1.
## 8. 확인 결과 (1단계)

1단계에서 ait는 명세 정의를 실행하지 않는다. 확인할 과제를 주석으로 내놓고, 확인한 에이전트가 결과를 낸다. ait는 결과가 빠짐없이 와서 모두 pass인지, 그리고 그 결과가 지금 상태에 대한 것인지만 본다.

- 8.1 **과제**: 확인할 계약의 명세 정의마다 하나다. 과제 이름은 `<계약>:<정의 PATH>`다. <계약>은 기존 ID이거나, 도우미가 이 요청에서 새로 만드는 레코드의 순번(`+1`, `+2`, …. 6.7의 `new` 행 순서이고 drop도 센다)이다. 한 명령의 과제는 확인 집합 전체를 합쳐 이름으로 겹침을 없앤 것이다.
- 8.2 **확인 요청 파일**(4.7의 `check-request.yaml`): 확인하는 에이전트에게 무엇을 어떻게 확인할지 알려 주는 주석이다. YAML 부분집합(2.5.1)으로 쓴다.
  - `input`: 확인 입력(8.3).
  - `tree`: 확인할 tree의 OID. fulfill·`ait commit`이면 stage된 상태 T0, 관문이면 정규화된 후보 T다. 모든 파일을 꺼내려면 `top=$(git rev-parse --show-toplevel) && d=$(mktemp -d) && GIT_INDEX_FILE="$d/index" git -C "$top" read-tree TREE && GIT_INDEX_FILE="$d/index" git -C "$top" checkout-index -a --prefix="$d/tree/"`를 쓴다(index 파일은 git이 새로 만들게 둔다. 하위 디렉터리에서 실행해도 전부 꺼내도록 `-C`로 뿌리에서 실행한다).
  - `tasks`: 과제마다 `task`, `contract`, `role`, `consumer`(demand), `provider`, `engine`(`exec`·`prompt`), `body`. 범위 목록은 표시를 `,`로 이은 한 문자열이다. `=SCOPES`를 줬으면 그 범위다.
  - exec 과제면 이어서 `command`, `success`와, environment가 있으면 그 환경 정의 파일의 행들을 들여써서 `environment` 아래에 그대로 싣는다.
  - 쓰기 규칙(확인 요청 파일과 결과 틀): 읽은 값이 쓴 값과 같도록 YAML의 규칙대로 쓴다. 문자열 값은 `"…"`이고 YAML의 이스케이프를 쓴다. 여러 행이 될 수 있는 문자열(`body`)은 `|` 블록 문자열이고, 앞의 공백이나 끝의 행바꿈은 표시(`|2`, `|-`, `|+`)로 지킨다. 쓰는 쪽(ait-mini에서는 libyaml)이 블록으로 쓰지 않는 값(줄 끝 공백이 있는 값 등)은 `"…"`다. UTF-8이 아닌 바이트가 든 문자열(git이 바이트로 두는 경로)은 git이 그런 경로를 보여 주는 대로(`core.quotePath`) C식으로 인용한 문자열로 쓰고, 결과 파일의 `task`도 그렇게 쓴 이름으로 맞춘다. 문자열의 목록(`command`)은 flow list이고 원소도 `"…"`다. 정수 목록(`success`)은 flow list다. `tasks`는 블록 list이고 키보다 들여쓰지 않을 수 있다. 빈 값은 값을 쓰지 않는다.

```yaml
input: "5d41402abc4b2a76b9719d911017c592ae2b1c3f"
tree: "7c4a8d09ca3762af61e59520943dc26494f8941b"
tasks:
- task: "D1a2b3c4d5e6f:specs/no-oversell.yaml"
  contract: "D1a2b3c4d5e6f"
  role: "demand"
  consumer: "src/order.c:10-40"
  provider: "src/stock.c"
  engine: "exec"
  body: |-
    재고가 0인 상품의 예약 요청은 거절한다.
  command: ["sh", "tests/no_oversell.sh"]
  success: [0]
```

- 8.3 **확인 입력** IN: 아래 행(각 행은 LF로 끝난다)을 차례로 이은 바이트열을 `git hash-object --stdin`에 넣은 값이다. 아래의 V는 관문이면 후보 U, fulfill·commit이면 T0다.

```
head H
path PATH MODE OID          (H→V에서 `.ait/` 밖의 바뀐 경로마다, PATH 바이트 순. V에 없으면 MODE와 OID는 `-`)
task NAME BLOB DISPLAY      (과제마다 NAME 바이트 순. BLOB은 정의의 blob, DISPLAY는 V에서의 provider 표시. `=SCOPES`면 그 범위)
```

  같은 상태에서 같은 명령을 다시 실행하면 IN이 같다. mode가 들어가므로 실행 권한만 바뀌어도 IN이 달라진다.
- 8.4 **결과 파일**(`-r FILE`): 4.7의 결과 틀을 채운 것이다. YAML 부분집합이다. 파일은 확인 결과 단계(5.13-8)에서 연다.

```yaml
input: 5d41402abc4b2a76b9719d911017c592ae2b1c3f
results:
  - task: D1a2b3c4d5e6f:specs/no-oversell.yaml
    outcome: pass                  # pass | fail | unknown
    basis: |                       # 근거. 돌린 명령과 그 결과, 본 코드 위치
      sh tests/no_oversell.sh → exit 0 (tree 7c4a8d0)
    reason:                        # fail·unknown이면 이유(선택)
```

  - 파일을 열 수 없으면 `E_READ`다.
  - YAML 부분집합이 아니거나 `input`이 없으면 `E_FORMAT`이다. 그다음 `input`을 IN과 비교한다. 다르면 다른 검사 없이 `E_NEEDS_RESULTS`다.
  - `input`이 같으면, `results`가 없거나, 과제가 빠지거나 더해지거나 겹치면, outcome이 셋 중 하나가 아니면, pass에 basis가 비었으면 `E_FORMAT`.
  - 하나라도 fail·unknown이면 `E_CHECK_FAILED`. stderr에 그 과제 이름들을 쓴다.
  - 받아들인 결과 파일은 커밋 메시지의 본문 뒤, 기록줄 앞에 덧붙인다. `task:` 값의 `+n`만 실제 ID로 바꾸고 나머지는 그대로다. `|` 블록은 끝의 빈 행을 뺀다.
  - 확인 요청 파일과 결과 틀은 과제가 있고 `--force`가 아니며 확인할 코드가 있을 때(5.13-7을 지났을 때), `-r`가 없거나 `E_NEEDS_RESULTS`이면 쓴다. 확인할 코드가 없으면 확인할 수 없으므로 쓰지 않고, 과제 행도 쓰지 않는다(6.5, 7.7.6). 다만 결과 틀이 이미 있고 그 `input`이 IN과 같으면 결과 틀은 다시 쓰지 않는다. 그래서 같은 상태에서 다시 실행하거나 `E_FORMAT`·`E_CHECK_FAILED`로 거절되어도 채운 결과가 지워지지 않는다.
- 8.5 **`--force`**: 결과 대신 "사소하다고 판단하고 책임진다"는 확인이다. 다시 고정은 똑같이 하고, 확인 집합의 계약마다 확인 행을 2.2.2의 규칙으로 쓴다: PINS가 바뀌면 `forced`이고, 같은 PINS를 이미 `reported`로 확인했으면 `reported` 그대로다. 그래서 그 고정은 확인된 고정이다(1.5a, 전제 2). 넘기는 것은 결과뿐이고, 그 밖의 조건(형식, 전이, 확인할 코드)은 그대로다(5.14).

## 9. 알림 (규칙 6·7)

- 9.1 **사건**: ref 목록에서 도달 가능한 커밋 C 중 다음 둘이다.
  - **요청**: 요청 커밋(5.4). 사건 ID는 요청 ID다.
  - **변경**: 요청 커밋이 아닌 커밋 중, 새 미확인이 있거나 9.2의 R이 비지 않은 것. 사건 ID는 커밋 OID다. 기록줄이 없거나 일부만 있는 커밋이 여기에 든다.
  - **새 미확인**(상태): C에서 미확인(1.5a)인 current 계약 K 중, 같은 고정(provider 행과 `file` 행)으로 이미 미확인이던 부모가 없는 것이다. 곧 C에서 새 미확인이 되었다. 통합 커밋, cherry-pick, 일반 커밋, pull 모두 이 규칙 하나를 따른다(커밋 전의 hook은 7.1.3의 부모를 쓴다). 구현은 레코드·provider 경로·명세 파일이 부모와 같은 계약의 계산을 생략해도 된다.
  - **미확인 사건**: C에서 미확인인 current 계약 K 중, (K, K의 판, provider 코드 내용)이 같은 채로 미확인이던 부모가 없는 것이다. provider 코드 내용은 C에서 provider 범위마다의 `PATH BLOB`이다. 이미 미확인이던 계약도 코드가 다시 다르게 바뀌면 사건이고, 같은 코드 변경이 사본으로 여러 커밋에 생기면 한 번이다(9.5).
  - **새 판**: C의 계약 파일 판 중 어느 부모의 판에도 포함되지 않는(2.2.4) 판이다. 사건은 새 판에만 생긴다. 옛 판으로 돌아간 커밋(git revert, reset 뒤의 커밋, 파일만 되돌리기)은 그 브랜치가 옛 상태에 있을 뿐이므로 남에게 사건이 아니다(규칙 2, 7.1.8).
- 9.2 **뜻이 바뀐 계약 R**: 계약의 뜻은 state, 범위, 조건(명세 파일 행: PATH, mode, 내용, `def`·`input`), target, 연결이다. 다시 고정(1.5e)과 prev만 바뀐 것은 뜻이 같다. 확인 행만 바뀐 것도 뜻이 같다. 다시 확인할 때마다 레코드가 바뀌지만 알림은 가지 않는다. 커밋 C를 부모와 비교해 다음을 모은다.
  1. C의 새 판인 레코드 중, 뜻이 그 판의 `prev` 중 어느 하나(처음 판이면 없음)와 다른 것의 ID. 그 `prev`의 판을 가진 쪽에게는 이 커밋이 그 뜻을 바꾼 것이기 때문이다. 부모가 하나인 커밋(관문, 이행)과, 판을 합친 커밋(충돌 해결, pull의 새 판)이 모두 이 규칙 하나를 따른다. 고르지 않은 쪽도 여기에 든다.
  2. 1의 방법으로 연결 레코드가 바뀌면 그 D와 O.
  3. C나 비교한 부모 판에서 future·current인 D·O가 선언한 명세 파일의 내용이나 mode가 바뀌면 그 D·O(병합 커밋이면 모든 부모와 다른 경로만 본다).
  4. 1~3에 든 X의 C판·부모 판에서의 target.
  5. C의 미확인 사건(9.1).

  R이 비면 그 요청은 받는 쪽이 없다(사건으로 처리해 seen에 넣는다). 보여 주기(7.9.5, 7.9.6)는 R을 쓰지 않는다.
- 9.3 **받는 쪽**(규칙 6): R의 계약 K마다, 다음 중 하나라도 맞는 session이 받는다. 관계는 받는 worktree의 HEAD로 판정한다. 맞은 이유는 모두 모으고, 같은 행은 한 번만 쓴다.
  - **내 약속**(1.3): HEAD에서 K가 session의 agent의 약속이다 → `reason future ROLE K`(ROLE은 K의 role: `demand`·`offer`·`drop`). 또는 K가 HEAD에서 그 agent의 약속 M의 상대다 → `reason future ROLE(M) M`(`reason future offer M`·`reason future demand M`·`reason future drop M`).
  - **내 요구**: HEAD에서 K가 creator가 session의 agent인 current demand다 → `reason demand K`. future인 요구는 내 약속이다.
  - **내 구독**: `sub id I`이고 I가 K → `reason sub id I`. `sub scope Q`가 K의 consumer, provider, 명세 파일 경로(C판과 9.2에서 비교한 `prev`의 판)와 기대는 겹침(3.9a)이면 → `reason sub scope DISPLAY(Q)`. 구체 범위의 구독은 추상 provider와 겹치지 않으므로(3.9a), 추상 계약은 `ait sub ID`나 추상 범위(`/`·`DIR/`)의 구독으로 받는다.

  - **버려진 내 변경**: C의 새 판 V의 `prev`가 둘 이상이면, 그 `prev`를 최신 판으로 보고 7.12의 3-way와 끝난 쪽으로 남는 판을 구한다(끝난 쪽으로 빠진 판도 센다). 그중 V와 본문이 다른 판이 **버려진 판**이다. 버려진 판을 처음 담은 커밋(7.12의 들이는 커밋 정의에서 사본을 고르기 전의 모두)의 `Ait-Agent`가 session의 agent이면 → `reason overridden K`. 기록줄은 판정에 쓰지 않고 누구에게 알릴지 찾는 데만 쓴다. 기록줄이 없으면 보내지 않는다.

  그래서:
  - current offer를 만든 쪽은 그 offer의 사건을 구독해야 받는다. 약속도 요구도 아니기 때문이다.
  - 내 HEAD에서 이미 놓거나 끝낸 것(release·cancel한 연결, 끝난 계약)은 다른 브랜치에 옛 판이 남아 있어도 받지 않는다.
- 9.4 **in-head 행**: C가 받는 worktree HEAD의 조상이거나, C의 새 판이 모두 HEAD 판에 포함되면(2.2.4) `in-head yes`, 아니면 `in-head no`다. 받았는지는 판의 포함으로 본다. 사본이나 다시 쓴 커밋으로 받았어도 같다.
  - 들이는 명령은 hint가 준다(5.9a의 들이기). 들일 것은 R의 계약이다.
- 9.5 **전달**(`ait listen`이 1초마다 한 session에 대해 한다):
  1. `lock`을 잡고 session 파일을 읽은 뒤 놓는다. 그때의 구독과 받는 worktree의 HEAD OID를 **받는 상태** V라 한다(받는 쪽은 이 둘로 판정하기 때문이다, 9.3). ref 목록 스냅샷 N을 만든다.
  2. 후보를 구한다: `git rev-list --reverse --topo-order --ignore-missing --stdin`. 입력은 N의 OID들과 tip의 OID마다 `^OID`이다. 사건인지는 9.1로 정한다.
  3. 후보를 차례로 처리한다. 사건은 (계약, 판)으로 센다. 사건의 **키**는 R의 1·2·4에 든 레코드마다 `version ID BLOB`(C의 판), R의 3마다 `spec ID PATH MODE BLOB`(C에서 그 계약이 선언한 명세 파일의 실제 값), 미확인 사건(R의 5)마다 `unverified ID BLOB PCONTENT`(PCONTENT는 provider 코드 내용(9.1)을 이은 바이트열의 해시)다. 같은 판은 몇 커밋에 생기든 한 번만 간다.
     - 사건이 아니면 건너뛴다. R과 미확인 사건에서 seen에 키가 있는 것을 빼고, 남는 것이 없으면 사본(rebase·cherry-pick·amend)이거나 이미 보낸 것이므로 건너뛴다. 한 차례의 후보 중 같은 키를 가진 것이 여럿이면, 그 키는 받는 worktree HEAD에 없는 커밋이 가장 적은 후보(같으면 OID가 작은 것)의 것으로 본다(7.12의 들이는 커밋과 같은 순서).
     - 발신 session이 자기 자신이거나 받는 쪽(9.3)이 아니면 건너뛴다. 변경은 발신 session이 없다.
     - 받는 쪽이면, 내보내기 전에 받는 상태가 V 그대로인지 본다. 바뀌었으면 이번 차례를 여기서 멈추고 4를 건너뛴다(다음 차례에 새 상태로 다시 판정한다. 이미 내보낸 사건은 seen이 막는다). 그대로면 stdout에 payload(남은 R과 미확인 사건의 것)와 `ait-event-end`를 쓰고 flush한 뒤 seen에 그 키들을 더한다. 쓰기가 실패하면 seen에도 tip에도 아무것도 쓰지 않고 5.7대로 끝난다(EPIPE면 SIGPIPE로, 그 밖에는 `E_IO`).
  4. 모두 처리했으면 `lock`을 잡고 session 파일과 HEAD를 다시 읽는다. 받는 상태가 V와 같으면 건너뛴 사건들의 키를 seen에 더하고 `tip` 행만 N으로 바꿔 쓴다. 바뀌었으면 건너뛴 사건은 쓰지 않고 tip도 그대로 둔다. 잠금을 놓는다.
- 9.6 같은 사건(같은 키)은 한 session에 한 번만 나온다(seen). 쓰다가 끊긴 사건은 다음 listen에서 다시 나올 수 있다(at-least-once). listener가 없는 session은 다음 listen에서 그동안의 사건 중 아직 도달하는 것을 받는다.
- 9.7 **payload**:

```
ait-event 1
request RID | change OID     (요청 | 변경)
recipient SID
ref REFNAME                   (C를 포함하는 ref, 이름순)
reason future demand|future offer|future drop ID | reason demand ID | reason sub id ID | reason sub scope DISPLAY | reason overridden ID   (맞은 이유마다 한 행. future demand, future offer, future drop, demand, sub id, sub scope, overridden 순이고, 같은 종류 안에서는 ID·DISPLAY 순)
in-head yes|no
unverified ID                 (C의 미확인 사건, ID 순)
contract ID ACTION [ARG] [target ID]   (R의 1~4에 든 계약마다, 7.9.5가 그 커밋에 쓰는 행. `check` 꼬리는 붙이지 않는다. ID 순, 한 계약 안에서는 7.9.5의 순서. `unverified`는 위 행이 있으므로 뺀다)
hint: …                       (5.9a. 일어난 일은 C로, 관계와 할 일은 받는 쪽의 지금 HEAD로 판정한 다음 명령. 할 일이 없으면 쓰지 않는다)

<git show --no-color --no-ext-diff --no-textconv --no-show-signature --diff-merges=first-parent --src-prefix=a/ --dst-prefix=b/ --format=fuller --stat --patch C>
```

  - payload에는 **누가**(Author, Ait-Agent·Ait-Session), **무엇을 왜**(메시지와 diff), **어떻게**(diff의 확인 행과 메시지의 확인 결과)가 모두 들어 있다.
  - hint는 5.9a의 H1~H8, H14, H15 중 받는 쪽에 맞는 것이다. 명령이 다룰 것이 받는 쪽 HEAD에 없으면 들이는 명령을 앞에 붙인다(5.9a의 들이기). 들인 뒤의 hint는 `contract pull`이 쓴다(7.12.5·7.12.6).

## 10. 협업 흐름 (전제와 규칙에서 나오는 일하는 법)

- 10.1 에이전트 하나는 session 하나, worktree 하나, 브랜치 하나를 쓴다. 시작할 때마다 `ait init`을 한다. agent 이름은 7.2.1대로 정한다(예: 맡은 작업의 ID). 한 worktree에서는 맡은 일 하나를 한다. 한 일에 계약이 여럿일 수 있다. 예를 들어 떠맡은 요구들, 그 요구들을 떠맡은 내 offer, 그 구현에 필요해 내가 남에게 건 요구다. 남이 내 계약을 받으면 그 판이 처음 들어간 커밋과 그 조상, 곧 그보다 앞선 내 브랜치의 커밋이 모두 함께 간다(규칙 8, 7.12). 그래서 그 일과 관계없는 다른 일은 다른 worktree에서 한다. 같은 저장소 안에서는 커밋하면 관련 에이전트의 listener가 다음 차례(1초 간격)에 받는다. 다른 clone에는 `contract push` 뒤 그쪽의 fetch가 일어날 때 간다.
- 10.1a **착수할 때 떠맡는다**(전제 1·2).
  - 요구는 코드에 건다. 어디에 구현될지 모르면 provider를 `/`·`DIR/`로 둔다. 건 쪽은 답을 기다리지 않고 그 약속을 기준으로 개발한다.
  - 그 일에 착수하는 에이전트가 그때 최신 판으로 떠맡는다(H9, H11). 착수하지 않을 것은 잡지 않는다. 잡고만 있으면 병목이다.
  - 이미 누가 떠맡았으면 그 약속을 기준으로 다른 일을 한다(H11). 그것 없이 내 일이 나아갈 수 없으면 함께 떠맡는다. 여럿이 떠맡을 수 있다(1.6). 함께 떠맡은 것을 알게 되면 늦은 쪽이 H4를 받는다. 먼저 이행된 구현을 `contract pull`로 받을 때 내 구현이 합쳐져 그 계약이 확인한 코드와 달라지면, pull은 받기 전에 멈추고 받으며 되돌리거나 사소하면 `--force`로 확인하라는 hint를 준다(7.12.5).
  - 그만두면 자기 offer를 cancel한다. cancel은 만든 쪽이 자기 약속을 거두는 것이다. 남의 계약을 거두지 않는다.
  - 계약이 바뀌면(남이 amend·cancel·되돌리기를 해도) 바뀐 상태를 기준으로 일한다.
- 10.1b **남의 브랜치에서 받는 것**(전제 2): 남의 브랜치에서는 계약을 지정해 받는다(`ait contract pull K…`, 규칙 8). 받는 명령은 알림과 명령 출력의 hint가 준다(5.9a의 들이기).
  - 계약 변경: 내 명령이 다루는 계약이나, 내가 만들었거나 떠맡았거나 구독한 계약에 내 HEAD에 없는 변경이 있으면 그 변경을 받는다(H3, H9, H11, H14).
  - 이행: 내가 건 요구가 이행되었으면 그 이행을 받고 이어서 개발한다(H15). 이행한 판이 처음 들어간 커밋과 그 조상이 함께 오므로, 그 이행이 확인한 구현도 온다.
  - 받는 계약의 이력에 있는 코드는 지정 없이 함께 온다. 어떤 코드가 필요한지는 가려낼 수 없기 때문이다. 그래서 계약 커밋은 그 계약의 코드 커밋보다 먼저 한다. 그러면 남이 내 약속을 받을 때 아직 이행되지 않은 내 코드가 함께 가지 않는다.
  - 남의 코드에 기대면 그 조건을 요구로 건다. 내가 건 요구의 알림은 구독하지 않아도 온다(규칙 6).
- 10.2 기본 흐름:

```sh
# A: 요구를 건다. 답을 기다리지 않고 이 약속을 기준으로 자기 코드를 계속 쓴다
ait future demand src/order.c:10-40 src/stock.c specs/no-oversell.yaml as d -m "재고 초과 판매 금지"; ait contract push
# B(다른 clone): 착수할 때 받아 와서 보고(H11), 최신 판을 들여 떠맡는다
git fetch; ait peek src/stock.c; ait contract pull D…; ait future offer src/stock.c specs/no-oversell.yaml as o undertake D… o -m "착수"; ait contract push
# B: 구현 → 확인 → 이행
ait fulfill O… -m "재고 가드 구현"          # -r 없이: 과제와 결과 틀을 받는다
ait fulfill O… -r "$(git rev-parse --git-dir)/ait/check-result.yaml" -m "재고 가드 구현"; ait contract push
# A: 이행 알림(fulfilled D via O)이 오면 hint(H15)대로 그 이행과 구현을 받아 이어서 개발한다(O도 그 커밋에서 바뀌었으므로 함께 지정한다, 7.12.3)
git fetch; ait contract pull D… O…
```

- 10.3 **검토 고리**: 저자가 설계를 future demand로 올린다. 검토자들은 `ait sub /`로 구독해 알림을 받고, 들여 고쳐서(`contract commit`) push한다. 동시 수정은 pull의 고르기로 드러난다(7.12.1). 고른 판은 고르지 않은 판을 `prev`에 담으므로 결과 판이 둘을 모두 포함하고, 고르지 않은 쪽에 알림이 간다(9.3의 버려진 내 변경). 검토자는 그 결과 판을 `contract pull`로 받는다.
- 10.4 **확정된 코드를 여러 커밋에 걸쳐 바꾸기**(리팩터링, 1.5d)의 예다.

```sh
# 1) 바꿀 코드에 걸린 current 계약을 본다
ait peek now src/stock.c
# 2) 그 계약들을 future로 되돌린다(한 요청). 받는 쪽에 알림이 간다. 이미 future인 계약은 할 일이 없다
ait future amend O… future amend D… -m "stock.c 10-20을 inventory.c로 옮김"
# 3) 리팩터링 커밋들
# 4) 다시 이행. -r 없이 먼저 실행하면 보고의 provider 행이 지금 위치를 준다
ait fulfill O… D… -m "옮김"
#    provider O… src/stock.c:1-40   → 그대로 둔 조각
#    provider D… src/stock.c:10-10  → 호출 한 줄만 남았다
ait fulfill O…=src/stock.c:1-40,src/inventory.c:1-11 D…=src/inventory.c:1-11 -m "옮김"   # 과제를 받는다
ait fulfill O…=src/stock.c:1-40,src/inventory.c:1-11 D…=src/inventory.c:1-11 -r <check-result> -m "옮김"
```

- 10.5 **조건 자체를 바꿔야 할 때**: 내 변경이 남의 current 계약의 조건을 일부러 바꾸면, 확인은 실패한다(fail). 조건을 바꾸는 것도 계약 변경이므로, 그 계약을 되돌리며 조건을 바꾼다(`ait future amend K - - NEW_DEFS -m <why>`, offer면 `K - NEW_DEFS`. 새 명세 정의는 내가 정한다). 받는 쪽은 새 조건을 받고(H3), 바뀐 상태를 기준으로 일한다(전제 1). 확인된 뒤에 다시 이행한다.

## 11. 1단계의 범위와 한계

- 11.1 규칙 9의 목록 밖은 막지 않는다. 그래서 다음도 된다. 다만 이력에 남는다.
  - ait 없이 하는 fast-forward나 reset 같은 ref 이동, 충돌 없이 끝난 cherry-pick·revert·rebase, 해결을 커밋하지 않고 잇는 `rebase --continue`, `git am`(git이 커밋 전의 hook들을 부르지 않으므로 7.1.3이 돌지 않는다. git이 post-commit·post-merge를 부르는 조작은 그 결과를 7.1.8이 알린다), `--no-verify`, hook 끄기, 기록줄 위조, PINS를 계산해 손으로 쓴 확인 행, `ait init` 전의 clone.
  - 충돌 없이 끝난 revert 커밋과, 레코드가 출발 판과 같은 revert 해결 커밋은 기록줄이 없으므로(7.1.5) 이력에 `- ?`로 남는다. rebase된 커밋은 원래 기록줄을 그대로 가진다(사본, 9.5).
  - squash merge와 `cherry-pick -n`: 통합 상태가 남지 않으므로 그 뒤의 commit은 일반 커밋이다(`E_GUARD`, 우회하면 `?`).
  - rebase의 fixup·squash는 커밋 메시지를 합치므로, 합친 메시지의 기록줄 블록(5.4)에 남지 않은 기록줄은 읽히지 않는다. 확인은 레코드의 확인 행에 있으므로 사라지지 않고, 누가 요청했는지만 남지 않을 수 있다. 충돌로 멈춘 rebase 명령(fixup·squash·reword·edit)을 마치는 커밋으로 마치면 그 명령의 뒷일(합치기, 메시지 고치기, 멈춤)은 일어나지 않는다.
  - 멈춘 rebase 위에서 다른 합치기를 하면 7.1.3은 그것을 rebase의 고르기로 볼 수 있다. git 조작은 하나씩 마친다.
  - 다른 도구가 hook 파일을 다시 쓰면 ait를 부르던 행이 사라질 수 있다. 다음 ait 명령의 H12가 알린다(5.2a).
  - 공개 뒤의 동기화(5.5-4) 중에 다른 쪽이 브랜치를 바꾸면 index와 작업 폴더를 맞추지 못할 수 있다. 그때는 경고만 쓴다(5.5-5).
- 11.2 ait의 쓰기 명령은 `commit-tree`로 커밋을 만들므로 커밋 hook(`pre-commit`, `commit-msg` 등)을 거치지 않는다. ait가 부르는 git 명령이 실행하는 hook은 git대로 돈다(`update-ref`의 `reference-transaction`, `read-tree`·`update-index`의 `post-index-change` 등). 그 hook이 `update-ref`를 거부하면 커밋은 공개되지 않고 `E_GIT`이다. `contract pull`도 `commit-tree`로 커밋을 만든다(7.12.6). 코드 충돌로 멈춘 pull은 마치는 커밋(`git commit`)이 hook을 거친다.
- 11.3 rename은 추적하지 않는다. 범위는 경로로 고정하고 diff로 사상할 뿐이다(3.3, 3.7).
- 11.4 알림은 `ait listen`으로만 받고 at-least-once다. listener가 없는 동안 생겼다가 ref 목록에서 사라진 사건은 받지 못한다.
- 11.5 prev DAG의 blob은 git이 운반하지 않는다. 이력의 커밋에 있는 판만 온다. 그래서 없는 blob에 기대는 판정(포함, 3-way의 공통 조상, 되돌린 커밋, 사고 판)은 clone마다 다를 수 있다. 판정할 수 없는 쪽으로 정한다: 포함은 그 가지에서 끝나고(2.2.4), 공통 조상의 본문을 모르면 pull은 고르기로 멈추며(7.12), 되돌린 커밋과 사고 판으로 세지 않는다(1.5d, 2.3.3).
- 11.6 여러 offer의 명세 파일을 합치면 요구가 충족되는지는 따지지 않는다. 요구 자신의 명세 파일을 확인할 뿐이다.
- 11.7 범위의 고정은 내용(blob)만 본다. provider 파일의 mode만 바뀐 것은 미확인이 되지 않는다.

## 12. 다음 단계로 넘긴 것

2단계: offer 명세 파일의 합이 요구를 함의하는지 검사, resolver(함수 단위 범위), ait가 명세 정의를 직접 실행하는 판정(exec 환경 실행·prepare·cleanup·변조 검사, `ait test`), 판정 결과 캐시와 컨테이너·원격 실행기, LSP 범위, rename 추적, 서버 측 gate, rebase 정책, prev 객체의 운반, daemon·서버 push 알림, JSON 출력, doctor.

3단계: 수신 hook으로 에이전트의 입력 큐에 바로 넣는 전달(`session open --hook`, `reference-transaction` hook의 즉시 알림, 전달 잠금·재처리 표시·hook 시간 제한).

## 13. 구현 지침

- 13.1 git 2.40 이상(pull이 `git merge-tree --write-tree`를 쓴다), libc(POSIX)와 `flock(2)`만 쓴다(Linux, macOS, BSD).
- 13.2 git은 fork·execvp로 argv 배열을 넘겨 부른다(5.12).
- 13.3 행수는 C(주석 포함)로 센다. 상한은 저장소의 CLAUDE.md가 정한다: ait-mini의 기능과 pull을 합쳐 6,500행이다(2026-10-10의 ait-mini 5,030행에 pull 추정 1,500행을 더했다). 아래 표는 1단계 전체의 모듈별 추정이다. pull은 판 정하기와 들이는 커밋을 C로 쓴 뒤 다시 잰다.

| 모듈 | 예산 |
| --- | --- |
| util·run·env(+메시지·hint 표·셸 인용) | 420 |
| rec(읽기·정규화·쓰기·DAG·포함, 범위 목록, 확인 행·PINS, 사고 판) | 400 |
| scope(+추상 범위·범위 목록) | 285 |
| gate(계약 경로·전이·validate·무결성) | 320 |
| helper(+되돌리기·경고) | 270 |
| contract new·add·commit(+명세 정의 틀) | 170 |
| fulfill·commit·todo(대상·걸린 계약·보고·hint 공유) | 330 |
| 확인 요청·결과(과제·확인 입력·확인 요청 파일 쓰기·결과 파싱) | 200 |
| yaml(부분집합 파서, 명세 정의·환경 정의) | 250 |
| peek(+이력·elsewhere, 판정 행·hint) | 360 |
| contract push(+책임 확인 커밋) | 100 |
| contract pull: 판 정하기(후보, 최신 판, 3-way·고리, 끝난 쪽, 결과 판·확인 행) 330, 들이는 커밋·지정 되풀이 170, 합치기(차례)·명세 판정과 빼기 280, 확인 검사(새 미확인, 다시 고정, 도우미 절, `--force`) 200, 받기(fast-forward, commit-tree, index·작업 폴더, merge 상태) 200, 0단계·출력·hint 170, 되돌린 커밋(1.5d, todo와 함께 씀) 50 | 1,400 |
| session(+기본 session·worktree 잠금·sub·unsub·listen) | 190 |
| notify(전달·미확인 사건·사건의 키·받는 쪽·contract 행·hint) | 280 |
| hook 6종(+통합 정리·전이 판정, 들어오는 쪽 출발 판 빼기, 위에 얹는 다시 고정, 통합 경고, post-commit·post-merge)·init·merge-driver·main | 460 |

- 13.4 비용이 큰 계산은 한 명령 안에서 한 번만 한다. 커밋마다 레코드 집합, 9.1의 새 미확인과 미확인 사건, 9.2의 R, 사건의 키는 한 번만 계산한다. 판의 포함과 사고 판은 blob마다 한 번만 계산한다(2.2.4, 2.3.3). 필요한 이력의 기록줄(누가 요청했나)은 `git log --format='%H%x00%P%x00%(trailers:key=Ait-Request,valueonly)%x00%(trailers:key=Ait-Agent,valueonly)…'` 한 번으로 읽는다(5.4). 확인된 고정은 그 판의 확인 행만으로 정하므로(1.5a) 조상을 따라가지 않는다. 레코드·provider 경로·명세 파일이 모두 부모와 같은 계약은 9.1을 다시 계산하지 않는다(`diff-tree`로 본다).
