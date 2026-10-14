# ait v1.1 규격

판: v1.1 초안 1 · 대상: C 구현 · 기준일: 2026-10-14

이 규격은 v1.1 구현의 규범이다. 결정과 그 근거(사용자의 말, 재현)는 `drafts/v1.1.md`에 있고, 이 규격은 그 결정을 규칙과 명령으로 적는다. 절 끝의 `(Uxx)`·`(Cxx)`는 그 결정의 번호다. v14 규격(`ait-phase1-spec.md`)의 절을 그대로 쓰는 곳은 "v14 N.N"으로 가리키고 바뀌는 점만 적는다.

이 규격은 아래 **전제**와 **규칙**에서 시작한다. 각 절은 규칙을 구현하는 방법이거나 규칙에서 따라 나오는 결론이다. 규칙에 없는 예외는 두지 않는다. 어떤 동작이 규칙에서 나오지 않으면 그 동작이 틀린 것이다. 정하지 않은 동작이 나오면 스스로 정하지 않고 사용자에게 묻는다.

## 전제

- **전제 1 — 코드에는 주인이 없다.** 수많은 에이전트가 한 코드베이스를 동시에 고친다. 누구든 어느 계약이든 만들고, 바꾸고, 떠맡고, 이행할 수 있다. 권한 개념은 없다.
- **전제 2 — 에이전트는 유한한 시간 안에, 한정된 문맥만으로, 명세를 충족하는 구현을 해낸다. 다만 이행하기 전까지는 실수할 수 있다.**

v14 전제 표의 결론은 그대로다. 다만 v1.1에서는 계약이 코드와 다른 브랜치에 있으므로(규칙 3), "판정할 수 없는 동시 수정"은 ait 브랜치끼리의 git merge 충돌로 드러나고 사람이 푼다.

## 규칙

- **규칙 1 — 사실만 기록하고, 판정은 계산한다.** 저장하는 것은 누가 무엇을 했다는 사실뿐이다: ait 브랜치의 계약 YAML과 이행 파일, 체크인 커밋의 기록줄, session의 이름과 구독과 전달 기록. current인지, 이행 뒤 바뀌었는지, 받는 쪽, 다음 명령 같은 판정은 이 사실에서 그때 계산하고 저장하지 않는다.
- **규칙 2 — 상태는 정해진 전이로만 바뀐다.** 계약의 state와 떠맡기는 2.4의 전이로만 바뀐다. 이것은 데이터로 판정한다. ait 브랜치끼리의 merge는 git이 하고, 같은 계약을 양쪽에서 고쳤으면 `edited` 행에서 충돌한다(2.3.3).
- **규칙 3 — 계약은 코드와 다른 브랜치에 있다.** 계약 YAML과 이행 파일은 코드와 이력을 공유하지 않는 `ait/` 브랜치에만 있고, 코드 브랜치에는 들어가지 않는다. 그래서 코드 브랜치의 어떤 git 조작도 계약을 건드리지 않는다. ait 브랜치에는 코드(검사 재료 포함)를 두지 않는다(U1, U4).
- **규칙 4 — current는 확인된 것이다.** 계약은 그 계약의 지금 `rev`로 한 이행 기록이 있고, 그 기록의 코드가 지금 코드와 같을 때만 current다(5장). 이행 기록은 확인 결과나 `--force`의 판단(사소하다고 책임진다)과 함께만 생긴다. 이행 뒤에 코드가 바뀐 계약은 막지 않고 `current ?unverified`로 드러낸다.
- **규칙 5 — 떠맡기는 통째이거나 부분이다.** 통째 떠맡기는 offer가 demand를 포함한다는 뜻이고, 부분 떠맡기는 offer가 demand의 일부를 맡는다는 뜻이다. 판정이 서로 기대는 짝은 조용히 연쇄하지 않는다: 한쪽을 고쳐 다른 쪽이 current에서 빠지게 되는 요청은 다른 쪽도 같은 요청에서 고쳐야 한다(1.8, U40, U41).
- **규칙 6 — 위험에 놓인 쪽에만 알린다.** 계약이 바뀐 사건은 그 계약이 **내 약속**이나 그 상대이거나, **내 요구**이거나, **내 구독**인 session에만 알린다(9장).
- **규칙 7 — 판정을 쓰고, hint가 다음 명령을 준다.** 오류·경고·알림·보고는 git 형식이고 판정을 그대로 쓴다. 이어서 보는 쪽이 실행할 다음 명령을 `hint:`로 준다. hint에는 ait의 명령(4.1)과 마치는 git 커밋만 쓴다. 틀릴 수 있는 hint는 주지 않는다. hint는 남이 만든 계약을 cancel·drop하는 명령, 남이 만든 offer의 떠맡기를 release하는 명령을 주지 않는다.
- **규칙 8 — ait는 git을 다시 만들지 않는다.** 판정은 git이 할 수 있으면 git에게서 받는다(브랜치를 쓰는 worktree, fast-forward, merge, 기본 브랜치 이름, diff). ait가 모르는 명령은 git에 넘기지 않는다. git과 같은 이름의 ait 명령은 git의 그 명령과 같은 쓰임에 계약의 확인을 더한 것이다(U38, C15).
- **규칙 9 — 막는 것은 아래 목록뿐이다.** 나머지는 모두 되고 기록에 남는다.

| 막는 것 | 근거 | 어디서 |
| --- | --- | --- |
| 계약 YAML·이행 파일의 형식, 같은 ID가 두 곳에 있는 것(2.3) | 1·2 | 모든 계약 명령의 처음(4.3) |
| 정해지지 않은 전이(2.4), 통째·부분 떠맡기를 섞는 것, 기대는 짝을 혼자 고치는 것(1.8) | 2·5 | 요청(6.4), `contract commit` |
| 확인 없는 이행(`--force`는 판단으로 확인하고 기록한다) | 4 | fulfill, 이행하는 요청 |
| 코드를 커밋할 수 없는 이행(커밋이 실패하면 이행할 수 없다) | 4 | fulfill(6.7) |
| 다른 worktree가 쓰고 있는 ait 브랜치로의 체크인 | 3 | 체크인(4.4) |
| 이행 파일을 `ait fulfill` 밖에서 쓰는 것 | 1·4 | `contract add`·`commit` |

그래서 다음은 모두 된다.
- 코드 브랜치의 모든 git 조작(branch, merge, rebase, reset, cherry-pick, `--no-verify`). 계약은 그 브랜치에 없다.
- ait 브랜치를 git으로 주고받고 합치기(`git fetch`, `git push`, `git merge`). 충돌은 사람이 푼다.
- 어느 에이전트든 어느 계약이든 고치고, 떠맡고, 이행하기.
- current 계약의 코드를 바꾸기. 그 계약은 `current ?unverified`가 된다.
- 확인 결과의 내용. ait는 과제마다 pass가 근거와 함께 보고됐는지만 보고 결과를 이행 파일에 남긴다. 결과가 참인지, 어느 작업 폴더에서 돌았는지 보지 않는다(U27).

## 0. 목적

ait는 여러 에이전트가 코드 범위에 계약을 **요구하고(demand), 보장하고(offer), 떠맡고(undertake), 고치고(amend), 거두고(cancel·drop), 이행하게(fulfill)** 하는 git 확장이다. 요구는 코드에 걸고, 그 일에 착수하는 에이전트가 그때 떠맡는다. 에이전트는 아직 이행되지 않은 약속을 기준으로 개발한다.

- 0.1 **계약**은 YAML 파일 하나다. 조건(spec)도 그 파일 안에 있다(U45). 계약과 이행 기록은 `ait/` 브랜치에 있다.
- 0.2 계약을 만들고 바꾸는 입구는 둘이고 관문(같은 검사)은 하나다. 도우미 명령(`demand`·`offer`·`future`·`amend`·`undertake`·`release`, 6.4)은 실행하면 체크인된다. 직접 작성은 `contract new` → 작업 폴더의 `.ait/`에서 YAML 작성 → `contract add` → `contract commit`이다(6.5).
- 0.3 이행은 `fulfill`로 한다. 관련 파일(stage한 코드)을 코드 브랜치에 커밋하고, 이행 기록을 ait 브랜치에 체크인한다(6.7).
- 0.4 **확인**: ait는 spec을 실행하지 않는다. 확인할 과제를 주고, 확인한 에이전트가 낸 결과(`-r FILE`)를 이행 기록에 남긴다(7장).
- 0.5 운반과 병합은 git이 한다. `contract push`·`pull`은 그것을 ait 브랜치에 맞춰 부르는 편의다(6.6).
- 0.6 버전은 git 객체 ID다. 출력은 행 기반 텍스트다.

## 1. 용어

- 1.1 **계약**: 코드 범위에 대한 요구(D, demand)나 보장(O, offer), 또는 계약 하나의 철회 예약(X, drop). 행위자는 언제나 에이전트다.
  - demand D: "범위 B의 코드가 이 spec들을 충족해야 한다". 범위 A는 B에 기대는 코드다. YAML에서 A는 `consumer`, B는 `provider`다.
  - offer O: "범위 B의 코드가 이 spec들을 충족한다(충족하게 하겠다)". B는 `provider`다.
  - drop X: target 계약 하나를 철회하겠다는 약속. X를 이행하면 target은 `withdrawn`이 된다.
- 1.2 **spec**: 계약이 충족할 조건 하나. 계약 YAML의 `specs:` 목록의 원소이고, 모든 spec이 pass여야 계약이 통과한다(U46). `engine: exec`이면 `command`를 돌려 확인하고, 생략하면 prompt로 코드를 읽고 판단한다. spec이 부르는 검사 재료(테스트, 데이터, 환경 정의)는 코드이고 코드 브랜치에 있다(U4).
- 1.3 **state**(저장): D·O는 `future`·`cancelled`·`withdrawn`, X는 `future`·`cancelled`. 바꾸는 것은 요청뿐이다(cancel, drop의 이행).
- 1.4 **판정**(계산, 5장): state가 `future`인 D·O는 ait 브랜치와 코드 tree마다 **current**, **current ?unverified**(이행 뒤 코드가 바뀜), **future**(이행 안 됨) 중 하나다. 저장된 state `future`와 판정 future는 다르다: 판정 current인 계약도 저장된 state는 `future`다.
- 1.5 **약속**: 저장된 state가 `future`이고 판정 current가 아닌(future나 current ?unverified) 계약. 판정의 tree는 명령이면 5.1의 X, 알림이면 받는 session의 worktree의 HEAD다(v14 9.3처럼). 약속 M의 **상대**는 M이 offer면 M이 떠맡은 D, demand면 M을 떠맡은 offer, drop이면 target이다. **내 약속**은 creator가 현재 session의 agent인 약속이다.
- 1.6 **rev와 edited**(2.3.3): `rev`는 확인 조건을 마지막으로 바꾼 요청의 ID이고 이행의 유효성을 맡는다. `edited`는 계약 행을 마지막으로 고친 요청의 ID이고 merge 충돌을 맡는다.
- 1.7 **떠맡기**(규칙 5, U40):
  - 통째: offer O가 demand D를 포함한다. O를 이행하면 D도 같은 이행에서 함께 이행되고, O가 current이려면 D도 current여야 한다. 한 D를 여러 O가 통째로 떠맡을 수 있다.
  - 부분: O가 D의 일부를 맡는다. D가 O에 포함된다는 전제가 없다. O를 이행해도 D는 이행되지 않는다. D는 D를 이행할 때 닫히고, D가 current이려면 D를 부분으로 떠맡은 살아 있는 O가 모두 current여야 한다.
  - 한 D에 살아 있는 떠맡기는 모두 통째이거나 모두 부분이다.
  - **살아 있는 떠맡기**: 떠맡기 목록의 D와 그 D를 떠맡은 O가 둘 다 저장된 state `future`인 것. 아닌 항목은 없는 것으로 본다(merge로 끝난 계약을 가리키게 됐을 수 있다).
- 1.8 **기대는 짝**(U41): (통째로 떠맡은 O → D)와 (부분으로 떠맡긴 D → O). 화살표 쪽에 기댄다. 요청이 X의 `rev`를 바꾸거나 X에 새 기대를 만들 때, X에 기대는 살아 있는 계약 Y에 유효한 이행(5.2)이 있으면, 같은 요청에서 Y의 `rev`도 바꿔야 한다(Y를 amend하거나, 바꿀 것이 없으면 빈 amend). 판정은 상대하는 ait 브랜치의 데이터로 한다. cancel·withdraw는 걸리지 않는다: 끝난 계약은 기대는 대상에서 빠질 뿐 다른 계약을 current에서 빼지 않는다.
- 1.9 **요청**: ait 쓰기 명령 하나로 하는 계약 변경이다. 체크인 커밋 하나이고, 기록줄 `Ait-Request: RID`로 식별한다(4.5).
- 1.10 **session**: 에이전트 하나의 작업 단위. 이름(agent), worktree, 구독을 가진다. 한 worktree를 여러 session이 쓸 수 있다(U20). 그 session들은 worktree의 체크아웃 하나를 같이 쓴다(U25).
- 1.11 식별자(hex는 소문자): demand·offer·drop은 `D`·`O`·`X` + 12 hex, 요청은 `R` + 12 hex, session은 `S` + 12 hex. alias는 `[a-z][a-z0-9_]{0,31}`이고 예약어 `demand offer future undertake release as drop amend cancel contract fulfill`은 쓸 수 없다. alias는 한 요청 안에서만 쓰이고 그 안에서 겹칠 수 없다. ID는 시스템 CSPRNG의 6바이트로 만들고(git의 `csprng_bytes`가 고르는 대로), 같은 요청에서 쓴 값이나 상대하는 ait 브랜치에 있는 값이면 다시 만든다.

## 2. 저장

### 2.1 ait 브랜치 (U1, U9, U11, U44)

- 2.1.1 `refs/heads/ait/<name>`. 코드 브랜치와 공통 조상이 없는 orphan 이력이다. 여럿 둘 수 있다(요구사항을 시험해 볼 수 있다).
- 2.1.2 **이름의 집합**: 로컬 `refs/heads/ait/*`와 원격 추적 `refs/remotes/<remote>/ait/*`의 이름(`ait/<name>`)을 합친 것이다. 세는 함수는 하나다.
- 2.1.3 **배치**: 계약은 `<group>/<ID>.yaml`, 그 계약의 이행 파일은 같은 그룹의 `<group>/fulfill/<ID>-<RID>.md`다. 그룹이 없으면 뿌리의 `<ID>.yaml`과 `fulfill/`. 그룹은 경로 조각 하나 이상이고 각 조각은 3.1의 PATH 조각 규칙을 따르며 `fulfill`일 수 없다. 그 밖의 경로가 있으면 형식 오류다. 계약 ID는 폴더를 넘어 유일하고, 명령은 ID만 받는다. 같은 ID가 두 곳에 있으면 형식 오류다.

### 2.2 작업 폴더의 `.ait/` (U47)

작업 폴더의 `.ait/`는 ait 브랜치와 같은 모양의 초안 자리다. 커밋되지 않는다: `ait init`이 `.git/info/exclude`에 `/.ait/`를 더한다(`.gitignore`는 커밋되는 코드 파일이라 ait가 고치지 않는다). `contract new`와 `contract new spec`의 기본 경로가 여기다. 다른 경로의 파일도 인자로 받는다.

### 2.3 계약 YAML

- 2.3.1 **YAML 부분집합**: v14 2.5.1 그대로다.
- 2.3.2 **필드**:

```yaml
rev: R7f3a00000000               # 확인 조건을 마지막으로 바꾼 요청의 ID
edited: R7f3a00000000            # 계약 행을 마지막으로 고친 요청의 ID
kind: demand                     # demand | offer | drop
creator: agent-a
consumer: src/ui/                # demand만. 범위 목록
provider: src/api/page.c:10-40 9a1c0000000000000000000000000000000000ab   # D·O. 범위 목록
state: future                    # D·O: future | cancelled | withdrawn. X: future | cancelled
specs:                           # D·O. 1개 이상
  - body: |
      GET /users는 페이지당 50건 이하를 돌려준다
    engine: exec
    command: [make, test-api]
    success: [0]
    environment: node            # 선택. 코드 브랜치의 specs/environments/node.yaml
undertakes: [D1a2b3c4d5e6f]      # offer만. 통째로 떠맡은 demand
undertakes-partially: []         # offer만. 부분으로 떠맡은 demand
target: O9f8e7d6c5b4a            # drop만
fulfilled:                       # 첨부 목록. 더하기만 한다
  - user/fulfill/D1a2b3c4d5e6f-R9b2100000000.md
```

  - 키의 순서는 위와 같고, 그 kind에 없는 키는 쓰지 않는다. `undertakes`·`undertakes-partially`·`fulfilled`가 비면 그 키를 생략할 수 있다. 알 수 없는 키, 빠진 필수 키는 형식 오류다.
  - **범위**는 3장이다. 범위 목록은 `,`로 이은 한 문자열이다. 행 범위만 기준 blob을 가진다: `PATH:S-E BLOB`(행 번호는 그 blob의 것, U22, C21).
  - **spec 원소**의 키는 v14 2.5.2에서 `inputs`를 뺀 것이다(`body` 필수, `engine`, exec면 `command` 필수, `success`, `environment`). prompt면 `body`만이다. 환경 정의는 코드 브랜치의 `specs/environments/<이름>.yaml`(v14 2.5.3의 형식)이고, 결과 틀을 낼 때 HEAD에서 읽어 싣는다(C39).
- 2.3.3 **rev와 edited**(U21, U23):
  - **확인 조건**은 `provider`·`specs`·`undertakes`·`undertakes-partially`다. **계약 행**은 확인 조건에 `consumer`와 `target`을 더한 것이다. `state`와 `fulfilled`는 둘 다 아니다.
  - `rev`는 계약을 만들거나 확인 조건을 바꾸는 요청(`--force` amend는 빼고)이 그 요청 ID로 바꾼다.
  - `edited`는 계약 행을 고치는 모든 요청(`--force` amend 포함)이 그 요청 ID로 바꾼다.
  - 이행(첨부 한 줄), state 변경은 둘 다 바꾸지 않는다.
  - 그래서: 같은 계약의 계약 행을 두 ait 브랜치에서 고치면 `edited`에서 반드시 충돌한다. 숫자를 올리면 양쪽이 같은 값이 되어 충돌이 숨으므로 요청 ID다(drafts 9.3, 9.18). 이행은 `edited`를 건드리지 않으므로 계약 행 변경과 깨끗이 합쳐지고, 옛 이행은 `rev`로 걸러진다. 같은 계약을 두 곳에서 각자 이행하면 첨부 목록 끝에서 충돌한다(U17). state 변경과 계약 행 변경은 합쳐질 수 있다(끝난 계약은 판정하지 않으므로 해가 없다).

### 2.4 전이 (규칙 2)

| 종류 | 전이 | 요청 |
| --- | --- | --- |
| D·O | 없음 → `future` | `future demand`·`future offer`, `contract commit`의 새 파일 |
| D·O | 없음 → `future` + 이행 | `demand`·`offer`(결과와 함께, 6.4) |
| D·O | `future` → `future`(계약 행 변경) | amend, `contract commit` |
| D·O | `future` → `cancelled` | cancel, `contract commit` |
| D·O | `future` → `withdrawn` | target인 X의 이행 |
| X | 없음 → `future`(target이 D·O이고 state `future`) | `future drop`, `contract commit` |
| X | `future` → `cancelled` | cancel, `contract commit` |
| 떠맡기 | 목록에 더함·뺌 | undertake·release, `contract commit` |

  - 그 밖의 변경(kind·creator 바꾸기, 계약 파일 지우기, 끝난 계약의 변경, 이행 파일·첨부 목록을 `fulfill` 밖에서 바꾸기)은 거절한다(`E_STATE`).
  - X를 이행하는 것은 target이 판정 current일 때다(v14 1.4: current 계약의 철회). X의 이행은 X에 이행 파일을 더하고 target의 state를 `withdrawn`으로 바꾼다. X는 이행 파일이 있으면 끝난 것이다.

### 2.5 이행 파일 (U5, U6)

```markdown
---
contract: D1a2b3c4d5e6f
rev: R7f3a00000000               # 이행한 계약의 rev
by: agent-b                      # session의 agent
session: S0123456789ab
command: make test-api           # spec마다의 command를 ; 로 이은 것(prompt면 -)
outcome: pass                    # pass | forced
code:                            # 계약의 provider 범위마다, 이행한 커밋(HEAD)의 코드
  src/api/page.c:10-40 9a1c0000000000000000000000000000000000ab: 100644 blob 77b2000000000000000000000000000000000000 12-42
---
(결과 파일의 results를 그대로. forced면 -m의 이유)
```

  - 이행 파일은 `ait fulfill`(과 이행하는 요청)만 쓴다. 한 번 쓰면 고치지 않는다. ait는 고쳤는지 검사하지 않는다(이력을 걸어야 해서, C9).
  - `code`의 키는 계약의 provider 범위 문자열 그대로이고, 값은 이행한 커밋의 tree 항목 `mode 타입 OID`다. 행 범위면 그 blob에서 확인한 행 `s-e`를 덧붙인다(사실로 저장한다: 계산에 쓰는 기준 blob은 gc로 지워질 수 있다, U26, C21). 범위 `/`는 root tree, `DIR/`은 그 tree, 파일은 그 blob, gitlink는 `160000 commit OID`, symlink는 링크 문자열의 blob이다.
  - 이행으로 세는 것은 그 계약의 `fulfilled:`에 있고, 앞머리의 `contract`가 그 계약이며, `rev`가 그 계약의 지금 `rev`인 것뿐이다(5.2). 목록에 없이 놓인 이행 파일은 세지 않는다. 목록에 있는데 파일이 없거나 `contract`가 다르면 형식 오류다.

### 2.6 로컬 실행 상태

- 2.6.1 **체크아웃**: `$(git rev-parse --path-format=absolute --git-path ait-branch)` 파일 한 행(`ait/<name>`). git이 worktree마다 HEAD를 두는 자리와 같은 규칙이다(main은 `.git/ait-branch`, linked는 `.git/worktrees/<id>/ait-branch`). `git worktree remove`가 함께 지운다(U30).
- 2.6.2 **session과 구독**: v14 4.1–4.6 그대로(`<git-common-dir>/ait/sessions/<SID>`, `.seen`, `.lock`, `.listen`, `worktrees/<KEY>`). `tip` 행은 9장의 ait ref 스냅샷이다.
- 2.6.3 **확인 요청 파일과 결과 틀**: worktree마다 `<git-dir>/ait/check-request.yaml`·`check-result.yaml`(v14 4.7).
- 2.6.4 작업 폴더가 없는 bare 저장소에서는 ait를 쓰지 않는다(`fatal: this operation must be run in a work tree`, git과 같은 문구).

## 3. 범위

v14 3.1–3.11 그대로다. 바뀌는 점:
- 3.1 PATH 규칙의 `.ait`는 작업 폴더의 초안 자리다(2.2). 범위로 쓸 수 없는 것은 같다.
- 3.2 **고정**(v14 3.3): 계약 YAML에는 행 범위의 기준 blob만 저장한다(`PATH:S-E BLOB`). 파일·`new:PATH`·`DIR/`·`/` 범위는 blob을 저장하지 않는다(판정은 이행 파일의 항목으로 한다, 5장). 범위를 받는 요청은 행 범위를 HEAD(이행하는 요청과 `K=SCOPES`는 index)에서 고정한다. 조건(v14 3.3의 `E_SCOPE` 조건)은 같다.
- 3.3 **추상 범위**(v14 1.5c): `DIR/`·`/`·`new:PATH`는 판정 future인 계약의 provider로 쓸 수 있다. 이행할 때는 HEAD에서 찾을 수 있는 범위여야 한다. 추상 범위를 구체 범위로 좁히는 것은 `ait fulfill K=SCOPES`(6.7.1)이고, 그것은 provider를 바꾸는 것이므로 같은 요청에서 `rev`를 바꾸고 새 `rev`로 이행한다. `DIR/`·`/`를 그대로 이행하면 그 tree 항목이 기록된다.
- 3.4 **사상과 접촉**(v14 3.6·3.7·3.10): 그대로다. diff는 blob 둘로 한다(`git diff -U0 … <blob> <blob>`, 커밋 없이 된다).

## 4. 공통 규칙

### 4.1 명령의 집합 (U38, U39, U43)

ait가 받는 하위 명령은 `init`, `checkout`, `switch`, `status`, `demand`, `offer`, `future`, `amend`, `undertake`, `release`, `contract`, `fulfill`, `sub`, `unsub`, `listen`, `session`뿐이다(`future`·`amend`·`undertake`·`release`·`demand`·`offer`는 요청의 첫 절, 6.4). 약어는 없다. 그 밖의 하위 명령은 `error: '<cmd>' is not an ait command`와 사용법을 쓰고 exit 129다. 인자가 없으면 사용법을 쓰고 exit 129.
- git과 같은 이름(`init`, `checkout`, `switch`, `status`)은 git의 그 명령과 같은 쓰임이다(규칙 8). 어떤 꼴을 ait가 맡고 어떤 꼴을 그 git 명령에 넘기는지가 명령마다 정해져 있다(6장). 일반 통과는 없다.

### 4.2 상대하는 ait 브랜치

- 이름의 집합(2.1.2)이 하나뿐이고 로컬이면 체크아웃 없이 그것이다(`(the only one)`).
- 체크아웃(2.6.1)이 있으면 그것이다.
- 여럿인데 체크아웃이 없으면 정할 수 없다(4.3의 2).

### 4.3 먼저 거절할 상태 (CLAUDE.md 절차 3)

계약을 읽거나 쓰는 명령(요청, `contract add`·`commit`·`push`·`pull`, `fulfill`, `status`, `sub`, `listen`)은 처음에 이 목록을 위에서부터 보고 처음 걸린 것으로 멈춘다. 명령별 판정은 이것을 지난 뒤에만 한다. 그 상태를 고치는 명령(`init`, `checkout`, `switch`)과 `session`, `unsub`, `contract new`는 이 목록을 지나지 않는다.

0. ait 브랜치가 하나도 없음(로컬도 원격 추적도): `fatal: no ait branch`, `hint: ait init`.
1. 로컬 ait 브랜치가 없고 원격 추적 것만 있음: `fatal: no local ait branch`. `ait init`이 만들 수 있는 이름이 있으면 `hint: ait init`(2.1.2, 6.1).
2. 상대할 ait 브랜치를 정할 수 없음: 여럿인데 체크아웃이 없으면 `fatal: more than one ait branch; none is checked out`, 체크아웃이 로컬 ait 브랜치가 아닌 이름을 가리키면 `fatal: contract branch 'ait/<name>' does not exist`. hint는 로컬 이름마다 `ait checkout ait/<name>`, 원격에만 있는 이름이 있으면 `ait init`.
3. 상대하는 ait 브랜치의 계약 YAML·이행 파일이 형식에 맞지 않음(merge 충돌 표시가 커밋된 것, 같은 ID 두 곳 포함): `fatal: bad format: '<path>' at 'ait/<name>': <이유>`. hint: `enter it with 'ait switch ait/<name>', fix '<path>', and commit the fix there`(ait 브랜치를 고치는 길은 git이다. 계약을 바꾸는 요청이 아니라 형식을 바로잡는 것이다). `status`는 멈추지 않고 같은 줄을 `warning:`으로 쓰고 그 파일을 건너뛴다.
4. 쓰기: 상대하는 ait 브랜치를 어느 worktree가 쓰고 있음. git이 판정하고 문구도 git의 것이다(4.4의 3).
5. fulfill과 이행하는 요청: 이 worktree의 HEAD가 `ait/*`이면 `fatal: HEAD is on contract branch 'ait/<name>'`. index에 unmerged 경로가 있으면 `error: fulfilling is not possible because you have unmerged files`(git의 `error_resolve_conflict` 판정, `advice.c:201`. 문구는 CLAUDE.md 출력 규칙대로).

### 4.4 체크인 (U3, C2, C3)

ait 쓰기 명령 하나가 체크인 하나다. 작업 폴더와 코드 브랜치를 건드리지 않는다.
1. 상대하는 ait 브랜치의 tip T를 읽는다.
2. 임시 index(`GIT_INDEX_FILE`, `$TMPDIR`의 `mkdtemp`)에 T를 올리고 이 요청이 바꾸는 파일만 바꾼다. `write-tree`, `commit-tree -p T`(T가 없으면 부모 없이)로 NEW를 만든다.
3. 두 판정을 git에게서 받는다. (a) 그 브랜치를 어느 worktree가 쓰는가: `git fetch -q --dry-run --no-write-fetch-head --no-recurse-submodules . NEW:refs/heads/ait/<name>`. 쓰고 있으면 git이 `fatal: refusing to fetch into branch … checked out at …`(exit 128)로 거절하고 ait는 그것을 그대로 전하고 멈춘다(HEAD뿐 아니라 rebase·bisect 중인 worktree도, `builtin/fetch.c:1382`). (b) 갱신은 `git update-ref -m <요약> refs/heads/ait/<name> NEW T`(CAS. T가 없으면 0 OID).
4. CAS가 거절되면 새 tip T'를 읽는다(git의 메시지는 지역화되므로 tip으로 판정한다). T'가 T와 같으면 git의 오류를 그대로 전하고 멈춘다. T'가 다르고, 이 요청이 읽고 판정에 쓴 파일과 바꾸는 파일이 모두 T와 T' 사이에 바뀌지 않았으면 T'에서 2부터 다시 한다. 하나라도 바뀌었으면 `error: 'ait/<name>' moved while running; nothing changed`로 멈춘다. 예외는 없다.
5. 성공하면 `[ait/<name> <짧은 OID>] <요약>` 한 행을 쓴다(git commit의 요약 행 모양, `sequencer.c:1403`. 첫 커밋은 ` (root-commit)`). 짧은 OID는 `git rev-parse --short`가 준다.

읽은 파일은 YAML을 읽는 함수 하나가 모은다(명령마다 목록을 따로 두지 않는다).

### 4.5 메시지와 기록줄

- `-m MSG`(여러 번이면 문단) 또는 `-F FILE` 중 하나가 쓰기 명령에 필요하다(v14 5.3). 체크인 커밋의 첫 행에 `[CONTRACT] `(이행이면 `[FULFILL] `)를 붙인다. 요약 행의 `<요약>`은 이 첫 행이다.
- 기록줄(v14 5.4): `Ait-Request: RID`, `Ait-Agent: NAME`, `Ait-Session: SID`. 체크인 커밋에만 붙인다. 코드 브랜치의 커밋(fulfill의 1단계)은 `-m`의 메시지 그대로이고 기록줄을 붙이지 않는다(코드 커밋은 git의 것이다).
- 결과 파일은 이행 파일의 본문에 남는다(2.5). 체크인 커밋 메시지에는 넣지 않는다.

### 4.6 session

v14 5.1 그대로다. 유효한 session이 필요한 명령: 요청, `contract commit`·`push`·`pull`, `fulfill`, `sub`, `unsub`, `listen`, SID를 생략한 `session close`. session 없이 되는 명령: `init`, `checkout`, `switch`, `status`, `contract new`·`add`, `session open`·`list`.

### 4.7 출력과 종료 코드

v14 5.6·5.7 그대로다(stdout은 porcelain, stderr는 `fatal:`·`error:`·`warning:`·`hint:`, 영어 한 행, 소문자로 시작, 마침표 없음, 대상은 `'…'`). git을 부르고 그 출력을 전하는 명령(`checkout`·`switch`·`status`의 git 부분, `contract push`)은 git의 종료 코드를 그대로 쓴다.

### 4.8 오류 종류

| 이름 | 종료 | 첫 행 |
| --- | --- | --- |
| `E_USAGE` | 129 | `error: …` 다음 `usage: ait …` |
| `E_READ` | 128 | `fatal: could not read '…'` |
| `E_SESSION` | 128 | `fatal: no usable ait session: …`(hint는 v14 5.8) |
| `E_BUSY` | 128 | `fatal: cannot run now: …`(listener가 이미 돈다 등) |
| `E_ID` | 128 | `fatal: no such contract: '…'` |
| `E_STATE` | 1 | `error: not allowed: …`(전이, 섞기, 기대는 짝은 아래 문구) |
| `E_SCOPE` | 128 | `fatal: bad scope: …` |
| `E_FORMAT` | 128 | `fatal: bad format: …`(파일과 행) |
| `E_BRANCH` | 128 | 4.3의 0–2 |
| `E_MOVED` | 1 | `error: 'ait/<name>' moved while running; nothing changed` |
| `E_NEEDS_RESULTS` | 1 | `error: check results required: …`(hint는 v14 5.8) |
| `E_CHECK_FAILED` | 1 | `error: check failed: …` |
| `E_COMMIT` | 1 | `error: commit failed; '…' not fulfilled`(6.7) |
| `E_NOT_RECORDED` | 1 | `error: committed '…' but '…' not fulfilled: …`, `error: '…' not fulfilled: …`(6.7) |
| `E_GIT` | 128 | `fatal: git failed: …` |
| `E_IO` | 128 | `fatal: write error: …` |

  기대는 짝: `error: changing 'X' would reopen 'Y', which depends on it`, `hint: ait amend X <…> amend Y`. 섞기: `error: 'D' is already undertaken partially by 'O'`(또는 `wholly`). 둘 다 `E_STATE`다.

### 4.9 hint

v14 5.9a의 원칙(쓰는 순간의 상태로 판정, 아는 값은 모두 채움, 사람이 정할 자리만 `<…>`, POSIX 셸 인용, 틀릴 수 있는 hint는 주지 않음)을 따른다. 바뀌는 점: hint에 쓰는 명령은 4.1의 집합과 마치는 git 커밋뿐이다. 들이기 hint(v14의 다른 ref에서 가져오기)는 `ait contract pull`이다.

## 5. 판정 (규칙 4)

- 5.1 **tree X**: 읽는 명령(`status`)에서는 index의 tree다(커밋하기 전의 변경이 이행된 계약을 건드리면 미리 보인다, v14 7.8.1). unmerged 경로는 HEAD 판으로 읽고 경로마다 `warning: 'P' is unmerged; it is read as HEAD has it`. X를 만드는 함수는 하나다(unmerged 경로를 HEAD 판으로 바꾼 임시 index에서 `write-tree`). fulfill은 X가 아니라 커밋한 뒤의 HEAD를 기록한다(6.7).
- 5.2 **유효한 이행**: 계약 K(저장된 state `future`인 D·O)의 첨부 목록의 이행 파일 중 `rev`가 K의 `rev`와 같은 것. 다른 `rev`의 이행은 옛 조건의 것이라 세지 않는다. 이 비교 하나가 조건 변경도 ait 브랜치끼리의 merge도 거른다(U21).
- 5.3 **범위가 같은가**(범위 하나, 이행 파일 하나): `code`에 그 범위 키가 없으면 다르다. 파일·디렉터리·`/`는 X의 항목이 `code`의 항목과 같으면 같다. 행 범위는 `code`의 blob이 이 저장소에 없으면 다르다(clone마다 다를 수 있다). 있으면 `diff0 <code의 blob> <X의 blob>`(v14 3.6의 옵션)의 어떤 hunk의 접촉 구간(v14 3.10: 순수 삽입은 `[os, os+1]`)이 `code`의 s-e와 겹치면 다르다. mode가 다르거나 파일이 없으면 다르다. git의 `git log -L`보다 넓게 알린다(맞닿은 삽입도 바뀐 것).
- 5.4 **결과**(정의는 여기 하나): 유효한 이행 중 K의 provider 범위가 **모두** 같은 이행이 하나라도 있고, K가 offer면 K가 통째로 떠맡은 살아 있는 D가 모두, K가 demand면 K를 부분으로 떠맡은 살아 있는 O가 모두 current이면 **current**다. 유효한 이행은 있는데 그렇지 않으면 **current ?unverified**다. 유효한 이행이 없으면 **future**다. 범위마다 다른 이행을 섞어 current를 만들지 않는다.
- 5.5 이 판정은 ait 브랜치와 tree마다 다르다. 같은 코드가 체크아웃한 ait 브랜치에 따라 current이기도 future이기도 하다(U9).
- 5.6 **걸린 계약**: X가 HEAD와 다르게 바꾼 경로가 판정 current인 K의 provider에 닿으면(v14 3.10의 `touches(provider, HEAD→X)`) K는 지금 변경에 걸린다. `status`가 보여 준다(U2의 "해당 코드 todo").

## 6. 명령

### 6.1 `ait init`

1. git 저장소의 작업 폴더 안이 아니면 `E_GIT`(`run this command inside a git repository`).
2. `.git/info/exclude`(`git rev-parse --git-path info/exclude`)에 `/.ait/` 행이 없으면 더한다.
3. 원격 추적 `refs/remotes/<remote>/ait/<name>` 중 로컬에 없는 이름마다, 그 이름이 원격 하나에만 있거나 `checkout.defaultRemote`가 그 원격을 가리키면 `git branch ait/<name> <remote>/ait/<name>`(upstream이 설정된다). 여러 원격에 있고 고를 수 없으면 `warning: 'ait/<name>' matched multiple (N) remote tracking branches`를 쓰고 그 이름은 만들지 않는다.
4. 이름의 집합이 비어 있으면, 설정된 원격마다 `git ls-remote <remote> 'refs/heads/ait/*'`. 닿지 못한 원격은 `warning: cannot reach '<remote>'; skipped`(U36). 닿은 원격에 ait 브랜치가 있으면 `fatal: '<remote>' has ait branches that are not fetched`와 `hint: fetch 'refs/heads/ait/*' from '<remote>', then 'ait init'`(U29). 없으면 `ait/<D>`를 빈 tree의 orphan 커밋으로 만든다. D는 `git var GIT_DEFAULT_BRANCH`(U11). 메시지는 `[CONTRACT] init`, 기록줄 없음.
5. 다시 해도 결과가 같다. 출력: 만든 브랜치마다 `branch ait/<name>` 한 행.

### 6.2 `ait checkout`

- ait가 맡는 꼴: 인자가 정확히 `ait/<name>` 하나이거나 `ait/<name> --`(git이 브랜치를 바꾸는 꼴, `builtin/checkout.c` `parse_branchname_arg`). 그 밖은 `git checkout`에 그대로 넘긴다(C30).
- 맡는 꼴이면: 로컬 ait 브랜치면 체크아웃 파일(2.6.1)에 쓴다. 원격에만 있으면 git에 넘기지 않는다(넘기면 git이 브랜치를 만들어 작업 폴더를 바꾼다): `fatal: no local branch 'ait/<name>'`, `hint: ait init`. 이름의 집합에 없으면 `git checkout`에 넘긴다(코드 브랜치일 수 있다).
- 성공: `On contract branch ait/<name>`, 이어서 `hint: the work tree stays on '<branch>'; to enter the contract branch: 'ait switch ait/<name>'`(detached면 `'<branch>'` 자리에 `a detached HEAD`. 추적 파일에 커밋하지 않은 변경이 있으면 앞 절만, U35).

### 6.3 `ait switch`, `ait status`

- `ait switch ait/<name>`(U37): 인자가 정확히 `ait/<name>` 하나일 때만 ait가 맡고, 그 밖은 `git switch`에 넘긴다. 맡으면: 로컬 ait 브랜치가 아니면 넘기지 않는다(원격에만 있으면 `fatal: no local branch 'ait/<name>'`, `hint: ait init`. 집합에 없으면 `fatal: no contract branch 'ait/<name>'`). `git diff --quiet HEAD`가 아니면 `fatal: uncommitted changes in the work tree; commit them before entering 'ait/<name>'`. 지나면 `git switch ait/<name>`을 부르고 출력과 종료 코드를 전한다. 성공하면 `hint: contracts change only through ait commands; to go back: 'ait switch -'`.
- `ait status [PATH...]`(U15, C34): 먼저 `On contract branch ait/<name>`(자동이면 ` (the only one)`)을 쓰고 내보낸다. 이어서 `git status -- PATH...`를 stdout을 물려준 채 돌린다(출력을 손대지 않는다). git이 0이 아닌 코드로 끝나면 그 코드로 끝낸다. 이어서 빈 행과 계약 부분을 쓴다. 인자는 PATH(범위, 3.2의 형식)만 받는다(옵션을 받으면 porcelain에 ait 행이 섞인다).
  - **고르기**: PATH가 없으면 내 약속(1.5), 내가 만든 demand, `current ?unverified`인 계약, 지금 변경에 걸린 계약(5.6). PATH가 있으면 consumer·provider가 PATH와 겹치는(v14 3.9) 계약.
  - **블록**(ID 순, 빈 행으로 끝남):

```
id ID
kind demand|offer|drop
group GROUP|-
state current|current ?unverified|future|cancelled|withdrawn
creator AGENT
consumer DISPLAY                       (demand)
provider DISPLAY | target ID
spec ENGINE BODY첫행                    (spec마다)
undertakes ID STATE AGENT [partial]    (offer)
undertaken-by ID STATE AGENT [partial] (demand)
fulfilled RID AGENT pass|forced valid|old   (첨부마다. valid는 rev가 맞는 것)
touch                                  (지금 변경에 걸렸으면)
```

  DISPLAY는 v14 3.8의 표시(X에서 사상한 지금 위치, `?changed`·`?missing`·`?deleted`·`?nobase`)다. 블록마다 그 계약에 지금 할 일이 정해지면 hint를 쓴다(이행이면 `ait fulfill ID`, 떠맡을 수 있으면 `ait future offer … as o undertake ID o`).

### 6.4 요청 (도우미 명령)

v14 7.3을 그대로 쓴다. 바뀌는 점만 적는다.

```
request := clause+ option*
option  := "-m" MSG | "-F" FILE | ("-r"|"--result") FILE | "--force" | "-g" GROUP
clause  := "demand" A B FILES ["as" NAME]
         | "offer" B FILES ["as" NAME]
         | "future" "demand" A B FILES ["as" NAME]
         | "future" "offer" B FILES ["as" NAME]
         | "future" "drop" K ["as" NAME]
         | ["future"] "amend" K [ARGS]        (D: A B FILES, O: B FILES, X: target인 K; "-"는 그대로 둠)
         | "future" "cancel" K
         | "undertake" ["-p"|"--partial"] K K  (D O)
         | "release" K K                       (D O)
FILES   := PATH ("," PATH)*                   (조건 파일, 6.4.2)
```

- 6.4.1 **절의 효과**: 절을 순서대로 상대하는 ait 브랜치의 tip에 적용한다. 각 절의 조건은 적용하는 시점에, 섞기와 기대는 짝(1.7, 1.8)은 모든 절을 적용한 뒤의 상태로 판정한다(같은 요청에서 상대 쪽을 놓거나 함께 고치면 허용).
  - `future demand`·`future offer`·`future drop`: 새 계약(state `future`). `rev`=`edited`=이 요청 ID. 그룹은 `-g`(없으면 뿌리).
  - `demand`·`offer`(future 없이): 새 계약을 만들고 같은 요청에서 이행한다(6.7의 0–3, 결과 `-r`나 `--force`가 필요하다).
  - `future amend K ARGS`: `-`가 아닌 필드를 바꾼다. 확인 조건이 바뀌면 `rev`를, 계약 행이 바뀌면 `edited`를 이 요청 ID로. 빈 `future amend K`는 `rev`와 `edited`를 바꿔 K를 다시 연다(옛 이행이 세지 않게 된다).
  - `amend K ARGS`(future 없이): `future amend`와 같고 같은 요청에서 K를 이행한다(결과나 `--force`).
  - `amend … --force`(결과 없이, U7): 사소한 변경이라 옛 이행을 그대로 센다. `rev`를 두고 `edited`만 바꾼다. provider를 바꿀 수 없다: `error: cannot change the provider of 'K' with --force`(exit 1), `hint: --force keeps the old fulfilments, which checked the old provider`(C23).
  - `future cancel K`: state `cancelled`(이미 cancelled면 noop).
  - `undertake [-p] D O`: O의 `undertakes`(통째) 또는 `undertakes-partially`(부분)에 D를 더한다. O의 확인 조건 변경이다. 이미 같은 목록에 있으면 noop.
  - `release D O`: O의 두 목록에서 D를 뺀다. 없으면 noop.
- 6.4.2 **조건 파일**(FILES): 각 PATH는 작업 폴더의 YAML 파일이고 v14 2.5.2의 명세 정의에서 `inputs`를 뺀 형식이다(spec 원소 하나, 2.3.2). ait는 형식을 검사하고(틀리면 `E_FORMAT`, 파일과 행) 그 내용을 계약 YAML의 `specs`에 순서대로 담는다. 파일은 작업 폴더에 남는다. 틀은 `ait contract new spec`(6.5.1).
- 6.4.3 체크인 하나다(4.4). 출력: 새 계약마다 `new ID [NAME]` 행(v14 6.7), 이어서 4.4의 요약 행.

### 6.5 `ait contract` (U43)

- 6.5.1 `ait contract new spec [PATH] exec|prompt`: 조건 파일의 틀을 PATH(기본 `.ait/specs/<새 이름>.yaml`)에 쓴다. 틀의 내용은 v14 7.4.1a에서 `inputs` 행을 뺀 것이다. 이미 있으면 `E_STATE`(`error: not allowed: 'PATH' already exists`). 출력 `path PATH`.
- 6.5.2 `ait contract new demand|offer|drop [GROUP]`: 새 ID로 `.ait/<GROUP>/<ID>.yaml`에 계약 YAML의 틀을 쓴다(`rev`·`edited`는 비워 두고 체크인할 때 채운다). 출력 `path PATH`.
- 6.5.3 `ait contract add PATH...`: `.ait/` 아래의 계약 YAML을 이 worktree의 계약 stage(`<git-dir>/ait/staged` 목록)에 더한다. 형식을 검사한다. stage는 commit할 때 읽는다.
- 6.5.4 `ait contract commit -m MSG [--force]`: stage한 계약들을 체크인 하나로 올린다. 이행하지 않는다(이행은 `fulfill`과 이행하는 요청뿐). `--force`는 확인 조건을 바꾼 계약을 사소한 변경으로 본다: `rev`를 두고 `edited`만 바꾼다(provider를 바꾼 계약이 있으면 6.4.1의 `--force` 거절). 위치는 `.ait/` 아래 경로 그대로다. 관문은 요청과 같은 검사다(전이 2.4, 섞기, 기대는 짝, `rev`·`edited`는 바뀐 행으로 ait가 정한다). `fulfilled`나 `fulfill/` 아래 파일을 바꾸면 `error: fulfilments are recorded only by 'ait fulfill'`. 끝나면 stage를 비운다. 작업 폴더의 파일은 남는다.
- 6.5.5 `ait contract push [<remote>]`: 4.3을 지난 뒤 `git push <remote> refs/heads/ait/<name>:refs/heads/ait/<name>`. remote는 인자, 없으면 `branch.ait/<name>.remote`, 없으면 `origin`(git의 기본 규칙). git의 출력과 종료 코드를 전한다.
- 6.5.6 `ait contract pull [<remote>]`: 4.3을 지난 뒤
  1. `git fetch <remote> 'refs/heads/ait/*:refs/remotes/<remote>/ait/*'`(remote는 push와 같은 규칙).
  2. 상대하는 ait 브랜치의 upstream U(`git rev-parse -q --verify --symbolic-full-name ait/<name>@{upstream}`가 `refs/remotes/*/ait/*`를 줄 때)가 없으면 `fatal: 'ait/<name>' has no upstream`.
  3. U가 로컬의 조상이면 `Already up to date.`. 로컬이 U의 조상이면 U로 체크인 3의 (a)·(b)를 한다(fast-forward).
  4. 갈라졌으면 `git merge-tree --write-tree ait/<name> U`. 충돌이 있으면 아무것도 바꾸지 않고 `error: contract branches conflict in '<paths>'`와 `hint: enter it with 'ait switch ait/<name>', merge '<U>' there and resolve the conflicts, then 'ait switch -'`(ait는 merge를 다시 만들지 않는다). 충돌이 없으면 결과 tree가 4.3의 3을 지나는지 보고(지나지 않으면 멈춤), 두 부모의 merge 커밋을 만들어 체크인한다(메시지 `[CONTRACT] merge '<U>'`, 기록줄).

### 6.6 (비움)

### 6.7 `ait fulfill` (U8, U27, U31–U33)

- 6.7.1 문법: `ait fulfill K[=SCOPES]... [-r FILE | --force] -m MSG`. `K=SCOPES`는 K의 provider를 그 범위로 바꾸고(같은 요청의 amend, `rev`가 바뀐다) 그 범위로 이행한다(추상 범위 좁히기, v14 7.7.1). 결과 없이 `ait fulfill K…`면 결과 틀을 낸다.
- 6.7.2 **덮는 계약 집합**(정의는 여기 하나): K가 offer면 K와 K가 통째로 떠맡은 살아 있는 D들. K가 demand면 K와 K를 부분으로 떠맡은 살아 있는 O 중 아직 current가 아닌 것들. K가 drop이면 K(그리고 target의 state 변경). drop은 spec이 없으므로 과제가 없고 결과 없이 이행한다(v14와 같다). 그 target이 판정 current가 아니면 `E_STATE`(`error: not allowed: 'X' targets 'K', which is not current`).
- 6.7.3 **결과 틀**: 덮는 계약마다 `rev`와 과제(7장)를 담는다. 과제에는 행 범위가 지금 몇 행인지(기준 blob에서 index로 사상한 s-e)를 보여 준다.
- 6.7.4 **위치 판정**(한 함수): provider 범위마다 찾는다. 경로가 없으면 `?missing`, 행 범위의 사상이 `?deleted`면 이행할 수 없다(`E_SCOPE`). 계약의 기준 blob이 이 저장소에 없으면 `fatal: repository lacks the base blob of 'K' ('<path>')`, `hint: ait amend K <provider>`(git `apply --3way`와 같은 모양). 결과 틀, 0단계(index), 2단계(HEAD), `--force`가 이 함수를 부른다.
- 6.7.5 **순서**:
  0. 먼저 판정한다(아무것도 바꾸지 않는다): 4.3(쓰고 있는 브랜치는 지금 tip T로 묻는다: `git fetch --dry-run . T:refs/heads/ait/<name>`), 결과가 덮는 계약 집합이 지금 계산한 집합과 같고 그 계약마다 T에서 state `future`이며 `rev`가 결과와 같은가, 위치 판정(index), 결과 형식(7장). 하나라도 틀리면 멈춘다.
  1. 관련 파일을 커밋한다: `git commit -m MSG`(hook·서명은 git의 것). stage한 것이 없으면(`git diff --cached --quiet`. intent-to-add는 git commit처럼 없는 것으로 본다) 커밋하지 않고 `warning: nothing to commit; recording the code at HEAD ('<short OID>') for <IDs>`를 쓰고 계속한다(U33). 커밋이 실패하면 git의 오류를 그대로 보인 뒤 `error: commit failed; <IDs> not fulfilled`(E_COMMIT). 아무것도 기록되지 않았다.
  2. 체크인한다: 덮는 계약마다 HEAD에서 위치 판정을 다시 하고(hook이 범위를 지웠을 수 있다) `code`를 계산해 이행 파일을 쓰고 각자의 첨부 목록에 더한다. drop이면 target의 state를 `withdrawn`으로. 체크인 커밋의 메시지는 `[FULFILL] ` + MSG의 첫 행.
  3. 2가 실패하면(위치, 4.4의 (a), CAS에서 멈춤, T'=T) 기록은 없다. 1에서 커밋했으면 `error: committed '<short OID>' but <IDs> not fulfilled: <이유>`, 아니면 `error: <IDs> not fulfilled: <이유>`(E_NOT_RECORDED). hint는 CAS에서 졌고 덮는 계약이 모두 그대로일 때만 `ait fulfill K -r <result> -m <msg>`(다시 하면 그때 stage한 것을 커밋하고 그때의 HEAD를 기록한다).
- 6.7.6 기록하는 코드는 1의 뒤 HEAD다. hook이 고친 것이든, hook이 stage를 비워 빈 커밋이 된 것이든 HEAD의 사실이다(U27).
- 6.7.7 `--force`: 결과 없이 0–3을 지난다. 덮는 계약 집합은 6.7.2, `rev`는 T의 것, 이행 파일의 `outcome: forced`, 본문은 MSG.
- 6.7.8 출력: 이행한 계약마다 `fulfilled ID`, 1의 커밋이 있으면 git commit의 요약 행(git이 쓴다), 이어서 4.4의 요약 행.

### 6.8 `ait sub`·`unsub`·`listen`, `ait session`

v14 7.2 그대로다. 바뀌는 점:
- `sub scope PATH:S-E`의 기준 blob은 그 session의 worktree의 index에서 고정한다. 기준 blob이 없으면 그 구독은 파일 전체로 본다.
- `sub id K`는 상대하는 ait 브랜치에 K가 있어야 한다(`E_ID`).
- `listen --fetch SEC`은 원격마다 `git fetch <remote> 'refs/heads/ait/*:refs/remotes/<remote>/ait/*'`.
- 알림은 9장이다.

## 7. 확인 결과

v14 8장을 쓴다. 바뀌는 점:
- 7.1 **과제**: 덮는 계약(6.7.2)의 spec마다 하나다. 이름은 `<계약>:<n>`(n은 `specs`의 순번, 1부터). 새로 만드는 계약은 `+1`, `+2`, …(v14 8.1).
- 7.2 **확인 요청 파일**(v14 8.2): `input` 대신 `revs`(덮는 계약마다 `ID: rev`)를 쓰고, `tree`는 index의 tree다. 과제의 키는 v14와 같고, exec면 `environment`에 코드 브랜치의 환경 정의를 HEAD에서 읽어 싣는다.
- 7.3 **결과 파일**(v14 8.4): `input` 대신 `revs`를 둔다. 0단계가 덮는 계약마다 T의 `rev`와 비교하고, 다르면 다른 검사 없이 `E_NEEDS_RESULTS`(계약이 그사이 바뀌었다). ait는 결과를 코드와 묶지 않는다(U27). 나머지(과제가 빠짐·더함·겹침, outcome, pass의 basis, fail·unknown이면 `E_CHECK_FAILED`)는 v14 8.4 그대로다. 받아들인 결과는 이행 파일의 본문이 된다.
- 7.4 결과 틀은 이미 있고 그 `revs`가 지금과 같으면 다시 쓰지 않는다(채운 결과가 지워지지 않게).

## 8. (비움)

## 9. 알림 (규칙 6, U28)

v14 9.5의 전달 절차를 ait 브랜치의 ref로 좁힌다.
- 9.1 **tip**: session 파일의 `tip OID REFNAME` 행들은 `refs/heads/ait/*`와 `refs/remotes/*/ait/*` 전체의 스냅샷이다. `session open`과 차례마다 갱신한다.
- 9.2 **후보**: `git rev-list --reverse --topo-order --ignore-missing --stdin`에 감시하는 ref의 OID들과 tip의 OID마다 `^OID`. 감시하는 ref는 session의 worktree가 상대하는 로컬 ait 브랜치와, upstream이 `refs/remotes/*/ait/*`이면 그 upstream이다. 되감기나 force push로 사라진 커밋은 알리지 않는다.
- 9.3 **받는 상태** V: v14 9.5의 V(구독과 HEAD)에 체크아웃 파일의 내용을 더한다. 내보내기 전에 V가 바뀌었으면 그 차례를 멈춘다.
- 9.4 **사건과 키**: 후보 커밋 C가 첫 부모와 비교해 바꾼 계약 파일이다(`git diff-tree -r --root --name-only --diff-merges=first-parent C`). 키는 바뀐 계약마다 `version ID BLOB`(C의 `<ID>.yaml` blob), 이행 파일마다 `fulfil ID BLOB`. 사본(merge·rebase)은 같은 키라 한 번만 나간다. 기록줄의 session이 받는 session 자신이면 건너뛴다.
- 9.5 **받는 쪽**: 관계는 받는 session이 상대하는 로컬 ait 브랜치의 tip에서, 사건은 C와 그 첫 부모 양쪽의 판에서 본다.
  - 내 약속: K가 그 agent가 만든 약속(1.5, 판정은 받는 worktree의 HEAD)이면 → `reason future ROLE K`. K가 그 agent의 future offer가 (통째·부분으로) 떠맡은 D이거나, 그 agent의 future D를 떠맡은 O면 → `reason future ROLE(M) M`.
  - 내 요구: K가 그 agent가 만든 demand이고 받는 worktree의 HEAD에서 판정 current → `reason demand K`.
  - 내 구독: `sub id K` → `reason sub id K`. `sub scope Q`가 C나 첫 부모의 판에서 K의 consumer·provider와 기대는 겹침(v14 3.9a)이면 → `reason sub scope DISPLAY(Q)`.
  - drop X가 바뀌면 그 target도 바뀐 계약으로 센다.
  - 버려진 변경은 알리지 않는다(U34).
- 9.6 **seen과 전달**: v14 9.5·9.6 그대로(at-least-once).
- 9.7 **payload**: `ait-event 1`, `commit OID`, `recipient SID`, `ref REFNAME`, `reason …` 행들, `in-head yes|no`(키가 모두 받는 session이 상대하는 로컬 ait 브랜치의 tip에 있으면 yes), `contract ID ACTION` 행(`rev`가 바뀌면 `amend`, `edited`만이면 `edit`, state가 바뀌면 그 state, 새 파일이면 `create`, 첨부가 늘면 `fulfil`), 빈 행, `git show --no-color --no-ext-diff --no-textconv --no-show-signature --diff-merges=first-parent --format=fuller --stat --patch C`, `ait-event-end`. 코드 쪽의 `current ?unverified`는 알리지 않는다(`status`가 보여 준다, C37).

## 10. 한계

- 10.1 이행 뒤 코드가 바뀐 것은 막지 않고 보여 준다(규칙 4, 9).
- 10.2 행 범위의 판정은 기록한 blob이 그 저장소에 있어야 한다. 올리지 않은 코드 커밋의 blob이면 clone마다 판정이 다를 수 있다.
- 10.3 ait 밖에서 ait 브랜치의 계약 행을 고치고 `edited`·`rev`를 두면 판단 없는 `--force`와 같아진다. 그것은 흐름 밖이다(U16).
- 10.4 체크인의 (a)와 (b) 사이에 누가 그 ait 브랜치를 체크아웃하는 경합은 남는다(4.4).
- 10.5 결과가 참인지, 확인이 어느 작업 폴더에서 돌았는지 ait는 보장하지 않는다(U27).
