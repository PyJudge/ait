# ait v1.1 규격

판: v1.1 초안 3 · 대상: C 구현 · 기준일: 2026-10-14

이 규격은 v1.1 구현의 규범이다. 결정과 그 근거(사용자의 말, 재현)는 `drafts/v1.1.md`에 있고, 이 규격은 그 결정을 규칙과 명령으로 적는다. 괄호 속 `Uxx`·`Cxx`는 그 결정의 번호다. v14 규격(`ait-phase1-spec.md`)의 절을 쓰는 곳은 "v14 N.N"으로 가리키고, 그 절에서 쓰는 부분과 바뀌는 점을 함께 적는다. "v14 N.N"이 가리키는 절의 다른 내용(hook, 코드 tree의 `.ait/`, `prev`, 사고 판, PINS, pull의 이력 받기, 관문, 연결 파일)은 v1.1에 없다.

이 규격은 아래 **전제**와 **규칙**에서 시작한다. 각 절은 규칙을 구현하는 방법이거나 규칙에서 따라 나오는 결론이다. 규칙에 없는 예외는 두지 않는다. 정하지 않은 동작이 나오면 스스로 정하지 않고 사용자에게 묻는다.

## 전제

- **전제 1 — 코드에는 주인이 없다.** 수많은 에이전트가 한 코드베이스를 동시에 고친다. 누구든 어느 계약이든 만들고, 바꾸고, 떠맡고, 이행할 수 있다.
- **전제 2 — 에이전트는 유한한 시간 안에, 한정된 문맥만으로, 명세를 충족하는 구현을 해낸다. 다만 이행하기 전까지는 실수할 수 있다.**

v14 전제 표의 결론은 그대로다. v1.1에서 동시 수정은 같은 ait 브랜치에서는 체크인의 CAS(4.4)로, ait 브랜치끼리는 git merge의 충돌이나 구조 불변식(4.10)으로 드러난다.

## 규칙

- **규칙 1 — 사실만 기록하고, 판정은 계산한다.** 저장하는 것: ait 브랜치의 계약 YAML과 이행 파일, 체크인 커밋의 기록줄, session의 이름·구독·전달 기록, worktree의 체크아웃. current인지, 이행 뒤 바뀌었는지, 받는 쪽, 다음 명령은 그때 계산한다.
- **규칙 2 — 상태는 정해진 전이로만 바뀐다.** 계약의 state와 떠맡기는 2.4의 전이로만 바뀐다. 데이터로 판정한다.
- **규칙 3 — 계약은 코드와 다른 브랜치에 있다.** 계약 YAML과 이행 파일은 코드와 이력을 공유하지 않는 `ait/` 브랜치에만 있다. 코드 브랜치의 어떤 git 조작도 계약을 건드리지 않는다. ait 브랜치에는 코드(검사 재료 포함)를 두지 않는다(U1, U4).
- **규칙 4 — current는 확인된 것이다.** 계약은 지금 `condition-id`로 한 이행 기록이 있고 그 기록의 코드가 지금 코드와 같을 때만 current다(5장). 이행 기록은 확인 결과나 `--force`의 판단(사소하다고 책임진다)과 함께만 생긴다. 이행 뒤에 코드가 바뀌면 막지 않고 `current ?unverified`로 드러낸다.
- **규칙 5 — 떠맡기는 통째이거나 부분이고, 기대는 짝은 조용히 연쇄하지 않는다.** 한쪽을 고쳐 다른 쪽의 이행이 기댈 곳을 잃게 되는 요청은 같은 요청에서 다른 쪽도 고쳐야 한다(1.8, U40, U41).
- **규칙 6 — 위험에 놓인 쪽에만 알린다.** 계약이 바뀐 사건은 그 계약이 내 약속이나 그 상대이거나, 내 요구이거나, 내 구독인 session에만 알린다(9장).
- **규칙 7 — 판정을 쓰고, hint가 다음 명령을 준다.** 오류·경고·알림·보고는 git 형식이고 판정을 그대로 쓴다. hint에는 ait의 명령(4.1)과 마치는 git 커밋, 그리고 git으로 이루어질 상태만 쓴다. 틀릴 수 있는 hint는 주지 않는다. hint는 남이 만든 계약을 cancel·drop하는 명령, 남이 만든 offer의 떠맡기를 release하는 명령을 주지 않는다.
- **규칙 8 — ait는 git을 다시 만들지 않는다.** 판정은 git이 할 수 있으면 git에게서 받는다. ait가 모르는 명령은 git에 넘기지 않는다. git과 같은 이름의 ait 명령은 git의 그 명령과 같은 쓰임에 계약의 확인을 더한 것이다(U38, C15).
- **규칙 9 — 막는 것은 아래 목록뿐이다.**

| 막는 것 | 근거 | 어디서 |
| --- | --- | --- |
| 상대할 ait 브랜치를 정할 수 없음(4.3의 0–2) | 3 | 계약 명령의 처음 |
| 계약 YAML·이행 파일의 형식, 같은 ID 두 곳(2.3, 2.5) | 1·2 | 계약 명령의 처음(4.3의 3), 체크인의 새 tree(4.10) |
| 구조 불변식을 늘려 어기는 체크인·합치기(떠맡기 섞기, 기대는 짝, 4.10) | 2·5 | 체크인(4.4), `contract pull`(6.5.6) |
| 정해지지 않은 전이(2.4), 이행 기록을 `fulfill` 밖에서 쓰기, 낡은 초안(6.5.4) | 1·2 | 요청, `contract commit` |
| 확인 없는 이행 | 4 | 이행하는 명령(6.7) |
| 이행할 수 없는 코드 상태: HEAD가 계약 브랜치, unmerged index, 범위를 찾을 수 없음, 커밋 실패(4.3의 5, 6.7) | 3·4 | 이행하는 명령 |
| 다른 worktree가 쓰고 있는 ait 브랜치로의 체크인, 체크인 중 바뀐 파일(4.4) | 3 | 체크인 |
| 작업 폴더에 커밋하지 않은 변경이 있는 채로 ait 브랜치에 들어가기(6.3) | 3 | `ait switch` |
| `--force`로 provider를 바꾸기(6.4.1) | 4 | amend |

그래서 다음은 모두 된다: 코드 브랜치의 모든 git 조작, ait 브랜치를 git으로 주고받고 합치기(충돌은 사람이 푼다, U17), 어느 계약이든 고치고 떠맡고 이행하기, current 계약의 코드를 바꾸기(`current ?unverified`가 된다), 확인 결과의 내용(ait는 pass가 근거와 함께 보고됐는지만 본다. 결과가 참인지, 어느 작업 폴더에서 돌았는지 보지 않는다, U27).

## 0. 목적

ait는 여러 에이전트가 코드 범위에 계약을 요구하고(demand), 보장하고(offer), 떠맡고(undertake), 고치고(amend), 거두고(cancel·drop), 이행하게(fulfill) 하는 git 확장이다.

- 0.1 계약은 YAML 파일 하나이고 조건(spec)도 그 안에 있다(U45). 계약과 이행 기록은 `ait/` 브랜치에 있다.
- 0.2 계약을 만들고 바꾸는 입구는 둘이고 검사는 같다: 도우미 명령(6.4)과 직접 작성(`contract new` → `.ait/`에서 작성 → `contract add` → `contract commit`, 6.5).
- 0.3 이행은 관련 파일을 코드 브랜치에 커밋하고 이행 기록을 ait 브랜치에 체크인한다(6.7).
- 0.4 ait는 spec을 실행하지 않는다. 확인할 과제를 주고, 확인한 에이전트가 낸 결과를 이행 기록에 남긴다(7장).
- 0.5 운반과 병합은 git이 한다. `contract push`·`pull`은 그것을 ait 브랜치에 맞춰 부르는 편의다(6.5).

## 1. 용어

- 1.1 **계약**: 코드 범위에 대한 요구(D, demand)나 보장(O, offer), 또는 계약 하나의 철회 예약(X, drop).
  - demand D: "범위 B(`provider`)의 코드가 이 spec들을 충족해야 한다". 범위 A(`consumer`)는 B에 기대는 코드다.
  - offer O: "범위 B(`provider`)의 코드가 이 spec들을 충족한다(충족하게 하겠다)".
  - drop X: target 계약 하나를 철회하겠다는 약속.
- 1.2 **spec**: 계약이 충족할 조건 하나. `specs:` 목록의 원소이고 모두 pass여야 한다(U46). `engine: exec`면 `command`를 돌려 확인하고, 없으면 prompt로 읽고 판단한다. spec이 부르는 테스트·데이터·환경 정의는 코드 브랜치에 있다(U4).
- 1.3 **state**(저장): D·O는 `future`·`cancelled`·`withdrawn`, X는 `future`·`cancelled`. 바꾸는 것은 요청뿐이다.
- 1.4 **판정**(계산, 정의는 5.4 하나): 저장된 state가 `future`인 D·O는 **current**, **current ?unverified**(이행 뒤 코드가 바뀜), **future**(유효한 이행 없음) 중 하나다. 저장된 state가 `future`인 X는 그 첨부 목록에 이행 파일이 있으면 **fulfilled**(끝남), 없으면 **future**다. 판정은 상대하는 ait 브랜치와 tree마다 다르다.
- 1.5 **약속**: 판정 future나 current ?unverified인 D·O, 판정 future인 X. 판정 tree는 명령이면 5.1의 X, 알림이면 받는 session의 worktree의 HEAD다. 약속 M의 **상대**: M이 offer면 M이 떠맡은 D(통째·부분), demand면 M을 떠맡은 O, drop이면 target. **내 약속**은 creator가 현재 session의 agent인 약속이다. **내 요구**는 그 agent가 만든 demand 중 판정 current인 것이다.
- 1.6 **condition-id와 change-id**(2.3.3, U50): `condition-id`는 조건을 마지막으로 정한 요청의 ID이고 이행이 유효한지를 가린다. `change-id`는 계약을 마지막으로 고친 요청의 ID이고 두 ait 브랜치에서 같은 계약을 고치면 merge가 충돌하게 한다.
- 1.7 **떠맡기**(U40):
  - 통째(`undertakes`): O가 D를 포함한다. O를 이행하면 D도 같은 이행에서 함께 이행되고, O가 current이려면 D도 current여야 한다. 한 D를 여러 O가 통째로 떠맡을 수 있다. 통째 떠맡기는 O의 확인 조건이다(O의 이행이 D를 확인한다).
  - 부분(`undertakes-partially`): O가 D의 일부를 맡는다. D가 O에 포함되지 않는다. O를 이행해도 D는 이행되지 않고, O의 판정은 D와 상관없다. 그래서 부분 떠맡기는 O의 확인 조건이 아니다(O의 `condition-id`를 바꾸지 않는다). D는 D를 이행할 때 닫히고, D가 current이려면 D를 부분으로 떠맡은 살아 있는 O가 모두 current여야 한다.
  - **살아 있는 떠맡기**: 목록의 D와 그 D를 떠맡은 O가 둘 다 저장된 state `future`인 것. 아닌 항목은 없는 것으로 본다.
- 1.8 **기대는 짝**(U41): (통째로 떠맡은 O → D)와 (부분으로 떠맡긴 D → O). Y → X는 Y의 판정이 X에 기댄다는 뜻이다. **기대는 짝 불변식**: 살아 있는 짝 Y → X마다, Y에 유효한 이행(5.2)이 있으면 X에도 유효한 이행이 있다. 이 불변식은 데이터만으로 판정한다(ait 브랜치마다 같은 답). 요청이 이것을 새로 어기게 되면(X의 `condition-id`를 바꾸거나, 이행된 D를 아직 이행되지 않은 O가 부분으로 떠맡게 하거나) 거절하고, 같은 요청에서 Y의 `condition-id`도 바꾸거나(빈 `future amend Y`로 다시 연다) X를 이행하면 허용한다(4.10). cancel·withdraw는 걸리지 않는다: 끝난 계약은 짝에서 빠진다.
- 1.9 **요청**: ait 쓰기 명령 하나로 하는 계약 변경이다. 체크인 커밋 하나이고 기록줄 `Ait-Request: RID`로 식별한다(4.5).
- 1.10 **session**: 에이전트 하나의 작업 단위(이름, worktree, 구독). 한 worktree를 여러 session이 쓸 수 있고(U20), 그 session들은 worktree의 체크아웃 하나를 같이 쓴다(U25).
- 1.11 **식별자**(hex는 소문자): demand·offer·drop은 `D`·`O`·`X` + 12 hex, 요청은 `R` + 12 hex, session은 `S` + 12 hex. agent 이름은 v14 7.2.1의 규칙(`[A-Za-z0-9._-]{1,64}`). alias는 `[a-z][a-z0-9_]{0,31}`이고 예약어 `demand offer future undertake release as drop amend cancel contract fulfill`은 쓸 수 없다. alias는 한 요청 안에서만 쓰이고 겹칠 수 없다. ID는 시스템 CSPRNG의 6바이트(git의 `csprng_bytes`가 고르는 대로)로 만들고, 같은 요청에서 쓴 값이나 상대하는 ait 브랜치에 있는 값이면 다시 만든다.

## 2. 저장

### 2.1 ait 브랜치 (U1, U9, U11, U44)

- 2.1.1 `refs/heads/ait/<name>`. 코드 브랜치와 공통 조상이 없는 orphan 이력이다. 여럿 둘 수 있다.
- 2.1.2 **이름의 집합**: 로컬 `refs/heads/ait/<name>`의 이름과, 원격 추적 ref 중 `refs/remotes/<remote>/ait/<name>`(`<remote>`는 `git remote`가 주는 원격 이름. `/`가 들어갈 수 있으므로 원격 이름을 접두어로 맞춘다)의 이름을 합친 것이다. 세는 함수는 하나다.
- 2.1.3 **배치**: 계약은 `<group>/<ID>.yaml`, 그 계약의 이행 파일은 같은 그룹의 `<group>/fulfill/<ID>-<RID>.md`다. 그룹이 없으면 뿌리의 `<ID>.yaml`과 `fulfill/`. 그룹은 3.1의 PATH 조각 규칙을 따르는 조각 하나 이상이고 어느 조각도 `fulfill`일 수 없다. 그 밖의 경로는 형식 오류다. ID를 찾는 일은 `git ls-tree -r --name-only T`의 출력을 거르는 것이다(ls-tree는 glob pathspec을 받지 않는다). 같은 ID가 두 곳에 있으면 형식 오류다. 계약은 그룹을 옮기지 않는다.

### 2.2 작업 폴더의 `.ait/` (U47)

작업 폴더의 `.ait/`는 초안 자리다. `.ait/<group>/<ID>.yaml`이 ait 브랜치의 `<group>/<ID>.yaml`과 짝이다. 그 밖의 이름의 파일(조건 파일 등)은 초안일 뿐이고 ait 브랜치와 짝이 없다. `ait init`이 `.git/info/exclude`에 `/.ait/`를 더하므로 코드 커밋에 딸려 들어가지 않는다(`.gitignore`는 커밋되는 코드 파일이라 ait가 고치지 않는다).

### 2.3 계약 YAML

- 2.3.1 **읽기**: v14 2.5.1의 YAML 부분집합. 알 수 없는 키, 빠진 키, kind에 맞지 않는 키, 값의 형식(ID·RID 형식, kind별 state, 범위 3장, agent 이름)이 틀리면 형식 오류다.
- 2.3.2 **필드**(이 순서, 이 모양으로 쓴다):

```yaml
condition-id: R7f3a00000000
change-id: R7f3a00000000
kind: demand
creator: agent-a
consumer: "src/ui/"
provider: "src/api/page.c:10-40 9a1c0000000000000000000000000000000000ab"
state: future
specs:
- body: |
    GET /users는 페이지당 50건 이하를 돌려준다
  engine: exec
  command: ["make", "test-api"]
  success: [0]
fulfilled:
- "user/fulfill/D1a2b3c4d5e6f-R9b2100000000.md"
```

  - kind별 키: demand는 `condition-id change-id kind creator consumer provider state specs fulfilled`, offer는 `condition-id change-id kind creator provider state specs undertakes undertakes-partially fulfilled`, drop은 `condition-id change-id kind creator target state fulfilled`. 모든 키는 늘 있다. 빈 `undertakes`·`undertakes-partially`는 `[]`(flow)이고, 빈 `fulfilled`는 키 행 `fulfilled:` 하나다(값 없음). 그래서 첫 이행은 키 행을 두고 그 아래에 행을 더하기만 하므로 바로 위 행(`specs`)의 변경과 merge에서 충돌하지 않는다(키를 생략하거나 `fulfilled: []`로 쓰면 그 행이 바뀌어 충돌한다, drafts 9.31).
  - **쓰는 모양**: 한 키가 한 행이다(`specs`와 `fulfilled`의 원소만 여러 행). `condition-id`·`change-id`·`kind`·`creator`·`state`·`target`은 plain scalar. 범위 목록(`consumer`·`provider`)은 `"…"` 문자열 하나이고 범위는 v14 3.2a의 순서로 `,`로 잇는다. `undertakes`·`undertakes-partially`는 ID 순의 flow list. `fulfilled`는 블록 list(원소는 `"…"`, 들여쓰지 않은 `- `)이고 끝에 더한다. `specs`도 들여쓰지 않은 블록 list다. `specs` 원소의 키 순서는 `body engine command success environment`이고, `body`는 v14 8.2의 쓰기 규칙(`|` 블록, 필요하면 `|2`·`|-`·`|+`, 쓸 수 없으면 `"…"`), `command`는 원소가 `"…"`인 flow list, `success`는 정수 flow list다. ait는 읽은 계약을 쓸 때 이 모양으로 다시 쓴다(주석은 남지 않는다).
  - **범위**: 3장. 행 범위만 기준 blob을 가진다(`PATH:S-E BLOB`, U22, C21).
  - **spec 원소**: `body` 필수, `engine`(없거나 빈 값이면 prompt, 아니면 `exec`), exec면 `command` 필수, `success`(기본 `[0]`), `environment`(선택. 코드 브랜치의 `specs/environments/<이름>.yaml`, v14 2.5.3의 형식). prompt면 `body`만(v14 2.5.6에서 `inputs`를 뺀 것).
- 2.3.3 **condition-id와 change-id**(U21, U23, U50):
  - **확인 조건**: `provider`·`specs`·`undertakes`. **계약 행**: 확인 조건에 `consumer`·`undertakes-partially`·`target`을 더한 것. `state`와 `fulfilled`는 둘 다 아니다.
  - `condition-id`는 계약을 만들거나 확인 조건을 바꾸는 요청이 그 요청 ID로 바꾼다. 사소한 변경(`future amend … --force`, `contract commit --force`)은 `condition-id`를 두고 `change-id`만 바꾼다.
  - `change-id`는 계약 행을 고치는 모든 요청이 그 요청 ID로 바꾼다.
  - 이행(첨부 한 줄)과 state 변경은 둘 다 바꾸지 않는다.
  - 그래서(drafts 9.3, 9.6, 9.18, 9.31에서 재현): 같은 계약의 계약 행을 두 ait 브랜치에서 고치면 `change-id`에서 반드시 충돌한다. 이행은 `change-id`를 건드리지 않으므로 계약 행 변경과 깨끗이 합쳐지고, 옛 이행은 `condition-id`로 걸러진다. 같은 계약을 두 곳에서 각자 이행하면 첨부 목록 끝에서 충돌한다(U17). state 변경과 계약 행 변경은 행이 붙어 있으면 충돌하고 아니면 합쳐진다(끝난 계약은 판정하지 않으므로 어느 쪽도 해가 없다). 서로 다른 계약을 고친 변경은 깨끗이 합쳐지고, 그 결과가 구조 불변식을 어길 수 있다(4.10).

### 2.4 전이 (규칙 2)

| 종류 | 전이 | 요청 |
| --- | --- | --- |
| D·O | 없음 → `future` | `future demand`·`future offer`, `contract commit` |
| D·O | 없음 → `future`, 같은 요청에서 이행 | `demand`·`offer`(6.4.1) |
| D·O | `future` → `future`(계약 행 변경) | `amend`·`future amend`, undertake·release, `contract commit` |
| D·O | `future` → `cancelled` | `future cancel`, `contract commit` |
| D·O | `future` → `withdrawn` | target인 X의 이행 |
| X | 없음 → `future`(target이 판정 current인 D·O) | `future drop`, `contract commit` |
| X | `future` → `future`(target 변경, 새 target이 판정 current) | `future amend X K`, `contract commit` |
| X | `future` → `cancelled` | `future cancel`, `contract commit` |

  - 그 밖의 변경(kind·creator·그룹 바꾸기, 계약 파일 지우기, 끝난 계약(cancelled·withdrawn, fulfilled인 X)의 변경, 이행 파일·첨부 목록을 이행 밖에서 바꾸기)은 `E_STATE`다.
  - X의 이행은 target이 판정 current일 때 하고, X에 이행 파일을 더하며 target의 state를 `withdrawn`으로 바꾼다.

### 2.5 이행 파일 (U5, U6)

```markdown
---
contract: D1a2b3c4d5e6f
condition-id: R7f3a00000000
by: agent-b
session: S0123456789ab
commit: 5d0e000000000000000000000000000000000000
outcome: pass
code:
  "src/api/page.c:10-40 9a1c0000000000000000000000000000000000ab": "100644 blob 77b2000000000000000000000000000000000000 12-42"
---
<결과 파일에서 이 계약의 과제(+n은 ID로 바꾼 것)를 v14 8.4의 꼴로. forced면 -m의 메시지>
```

  - 앞머리는 YAML 부분집합(2.3.1)이고 키는 위의 것뿐이며 모두 필수다. `commit`은 이행한 코드 커밋(6.7의 1 뒤 HEAD)이고, 행 범위가 그 뒤 바뀌었을 때 어느 커밋에서 확인했는지 알려 주는 사실이다(C51). `outcome`은 `pass`·`forced`이고, drop의 이행은 `withdrawn`이며 `code`는 `{}`다.
  - `code`의 키는 계약의 provider 범위 문자열 그대로, 값은 `commit`의 tree 항목 `mode 타입 OID`(행 범위면 그 blob에서 확인한 행 `s-e`를 덧붙인다. 기준 blob은 gc로 지워질 수 있어 사실로 저장한다, U26). 범위는 이행할 때 구체 범위다(3.3). gitlink는 `160000 commit OID`, symlink는 링크 문자열의 blob이다.
  - 이행 파일은 이행하는 명령(6.7)만 쓴다. 한 번 쓰면 고치지 않는다. ait는 고쳤는지 검사하지 않는다(C9).
  - 이행으로 세는 것은 그 계약의 `fulfilled:`에 있고, 앞머리의 `contract`가 그 계약이며, `condition-id`가 그 계약의 지금 `condition-id`인 것뿐이다. 목록에 없이 놓인 이행 파일은 세지 않는다. 목록에 있는데 파일이 없거나 `contract`가 다르거나 그룹이 다르면 형식 오류다.

### 2.6 로컬 실행 상태

- 2.6.1 **체크아웃**: `$(git rev-parse --path-format=absolute --git-path ait-branch)` 파일 한 행(`ait/<name>`, U30).
- 2.6.2 **session과 구독**: v14 4.1–4.6의 파일(`<git-common-dir>/ait/sessions/<SID>`, `.seen`, `.lock`, `.listen`, `worktrees/<KEY>`)과 잠금 순서를 쓴다. 바뀌는 점: session 파일의 `tip` 행은 9.1의 ait ref 스냅샷이고, `.seen`의 키는 9.4의 것이다.
- 2.6.3 **확인 요청 파일과 결과 틀**: worktree마다 `<git-dir>/ait/check-request.yaml`·`check-result.yaml`.
- 2.6.4 **계약 stage**: worktree마다 `<git-dir>/ait/index`. git index 파일이고, `contract add`가 add할 때의 내용을 담는다(git index처럼, C42).
- 2.6.5 작업 폴더가 없는 bare 저장소에서는 계약 명령을 쓰지 않는다(`fatal: this operation must be run in a work tree`, git과 같은 문구, 128).

## 3. 범위

v14 3.1·3.2·3.2a·3.5–3.10의 범위 문법, 행 수, diff0, 사상, 표시, 겹침, 접촉을 쓴다. 바뀌는 점:
- 3.1 PATH 규칙(v14 3.1)의 `.ait` 금지는 그대로다. 조건 파일(FILES)만 `.ait/` 아래에 있어도 된다.
- 3.2 **고정**: 계약 YAML에는 행 범위의 기준 blob만 저장한다. 범위를 받는 요청은 행 범위를 HEAD(이행하는 명령과 `K=SCOPES`는 index)에서 고정한다(v14 3.3의 조건과 `E_SCOPE`). 파일·`new:PATH`·`DIR/`·`/`는 blob을 저장하지 않는다.
- 3.3 **추상 범위**(v14 1.5c, 7.7.1): `DIR/`·`/`·`new:PATH`는 판정 future인 계약의 provider로만 쓴다. 이행할 때 provider는 모두 그 tree에서 찾을 수 있는 `PATH`나 `PATH:S-E`여야 하고, 아니면 `fatal: bad scope: 'K' needs concrete scopes to be fulfilled`와 `hint: ait fulfill K=<scope> …`(같은 명령에 `=SCOPES`를 더한 것). `=SCOPES`로 좁히는 것은 provider를 바꾸는 것이므로 같은 요청에서 `condition-id`를 바꾸고 새 `condition-id`로 이행한다.
- 3.4 **표시**(v14 3.8): 파일 범위는 이행 파일의 항목과 비교한다. 유효한 이행이 없으면 `?changed`를 쓰지 않는다.
- 3.5 diff는 blob 둘로 한다(`git diff -U0 --no-color --no-ext-diff --no-textconv --text --inter-hunk-context=0 --diff-algorithm=myers --no-indent-heuristic <blob> <blob>`, 커밋 없이 된다).

## 4. 공통 규칙

### 4.1 명령의 집합 (U38, U39, U43)

ait가 받는 하위 명령은 `init`, `checkout`, `switch`, `status`, `demand`, `offer`, `future`, `amend`, `undertake`, `release`, `contract`, `fulfill`, `sub`, `unsub`, `listen`, `session`뿐이다(`demand`·`offer`·`future`·`amend`·`undertake`·`release`는 요청의 첫 절, 6.4). 약어는 없다. 그 밖의 하위 명령은 `error: '<cmd>' is not an ait command`와 사용법(exit 129). 인자가 없으면 사용법(exit 129).
- git과 같은 이름(`init`, `checkout`, `switch`, `status`)은 git의 그 명령과 같은 쓰임이고, 어떤 꼴을 ait가 맡고 어떤 꼴을 그 git 명령에 넘기는지는 그 명령의 절이 정한다. 일반 통과는 없다.

### 4.2 상대하는 ait 브랜치

체크아웃(2.6.1)이 있으면 그것이고, 없으면 이름의 집합이 하나뿐이고 로컬일 때 그것이다(`(the only one)`). 체크아웃이 있으면 그것이 먼저다.

### 4.3 먼저 거절할 상태 (CLAUDE.md 절차 3)

계약을 읽거나 쓰는 명령(요청, `contract add`·`commit`·`push`·`pull`, `fulfill`, `status`, `sub`, `listen`)은 처음에 이 목록을 위에서부터 보고 처음 걸린 것으로 멈춘다. 명령별 판정은 이것을 지난 뒤에만 한다. 그 상태를 고치는 명령(`init`, `checkout`, `switch`)과 `session`, `unsub`, `contract new`는 이 목록을 지나지 않는다. 사용법(E_USAGE: 문법, 옵션, 메시지)은 이 목록보다 먼저, session(4.6)은 이 목록 뒤에 본다.

0. ait 브랜치가 하나도 없음: `fatal: no ait branch`, `hint: ait init`(E_BRANCH).
1. 로컬 ait 브랜치가 없고 원격 추적 것만 있음: `fatal: no local ait branch`. `ait init`이 만들 수 있는 이름(원격 하나에만 있거나 `checkout.defaultRemote`가 고르는 이름)이 있으면 `hint: ait init`(E_BRANCH).
2. 상대할 ait 브랜치를 정할 수 없음: 여럿인데 체크아웃이 없으면 `fatal: more than one ait branch; none is checked out`, 체크아웃이 로컬 ait 브랜치가 아니면 `fatal: contract branch 'ait/<name>' does not exist`. hint는 로컬 이름마다 `ait checkout ait/<name>`, 1의 조건으로 만들 수 있는 원격 이름이 있으면 `ait init`(E_BRANCH).
3. 상대하는 ait 브랜치의 tip에 형식 오류(2.3, 2.5, 같은 ID 두 곳, merge 충돌 표시가 커밋된 것): `fatal: bad format: '<path>' at 'ait/<name>': <이유>`, `hint: fix '<path>' in 'ait/<name>' (enter it with 'ait switch ait/<name>')`(E_FORMAT). merge 충돌을 푸는 것과 같은 일이다(U17). `status`는 멈추지 않고 같은 줄을 `warning:`으로 쓰고 그 파일을 건너뛴다.
4. 쓰기: 상대하는 ait 브랜치를 어느 worktree가 쓰고 있음. git이 판정하고 문구도 git의 것이다(4.4의 3).
5. 이행하는 명령(6.7): 이 worktree의 HEAD가 `ait/*`이면 `fatal: HEAD is on contract branch 'ait/<name>'`(E_BRANCH). index에 unmerged 경로가 있으면 `error: fulfilling is not possible because you have unmerged files`(git의 `error_resolve_conflict` 판정, E_STATE).

### 4.4 체크인 (U3, C2, C3)

ait 쓰기 명령 하나가 체크인 하나다. 작업 폴더와 코드 브랜치를 건드리지 않는다.
1. 상대하는 ait 브랜치의 tip T를 읽고, 명령의 판정(4.3, 절의 조건, 4.10)을 T에서 한다.
2. 임시 index(`GIT_INDEX_FILE`, `$TMPDIR`의 `mkdtemp`)에 T를 올리고 이 요청이 바꾸는 파일만 바꾼다. `write-tree`로 새 tree를 만들고 4.10을 대어 본다(늘린 위반이 있으면 멈춘다). `commit-tree -p T`(T가 없으면 부모 없이)로 NEW를 만든다.
3. 두 판정을 git에게서 받는다. (a) 쓰는 worktree: `git fetch -q --dry-run --no-write-fetch-head --no-recurse-submodules . NEW:refs/heads/ait/<name>`. exit 128이면 git의 오류를 그대로 전하고 멈춘다(그 브랜치를 HEAD·rebase·bisect로 쓰는 worktree가 있다, `builtin/fetch.c:1382`). exit 1(그새 tip이 움직여 fast-forward가 아니다)이면 (b)로 간다. (b) `git update-ref -m <요약> refs/heads/ait/<name> NEW T`(CAS. T가 없으면 0 OID).
4. CAS가 거절되면 새 tip T'를 읽는다(git의 메시지는 지역화되므로 tip으로 판정한다). T'가 T와 같으면 git의 오류를 그대로 전하고 멈춘다(E_GIT). 이 요청이 바꾸는 파일 중 하나라도 T와 T' 사이에 바뀌었으면(`git diff-tree --quiet T T' -- <paths>`가 1) `error: 'ait/<name>' moved while running; nothing changed`(E_MOVED)로 멈춘다. 아니면 T := T'로 놓고 1부터 다시 한다(판정을 모두 다시 한다).
5. 성공하면 `[ait/<name> <짧은 OID>] <요약>`을 쓴다(git commit의 요약 행 모양, `sequencer.c:1403`. 짧은 OID는 `git rev-parse --short`. 첫 커밋은 ` (root-commit)`).

### 4.5 메시지와 기록줄

- 쓰기 명령은 `-m MSG`(여러 번이면 문단) 또는 `-F FILE` 중 하나가 필요하다(v14 5.3. 없거나 정리한 결과가 비면 E_USAGE).
- 체크인 커밋의 메시지는 MSG이고 첫 행에 `[CONTRACT] `(이행이면 `[FULFILL] `)를 붙인다. 4.4의 `<요약>`은 그 첫 행이다. 끝에 기록줄 `Ait-Request: RID`, `Ait-Agent: NAME`, `Ait-Session: SID`(v14 5.4의 형식과 파서).
- 이행이 만드는 코드 커밋의 메시지는 MSG 그대로이고 기록줄이 없다(코드 커밋은 git의 것이다).
- 기록줄은 사용자의 git 설정으로 읽는다. ait는 설정을 고치거나 내부에서 덮어쓰지 않는다. 기록줄을 붙이는 명령은 4.3의 목록과 session(4.6)을 지난 뒤, 명령별 판정·코드 커밋·체크인보다 먼저 MSG 없이 기록줄만 둔 메시지를 v14 5.4의 파서로 다시 읽는다. 세 키 중 하나라도 그대로 읽히지 않으면 `fatal: git config hides the trailers: '<key>'…`(읽히지 않은 키를 `, `로 잇는다)와, 읽기를 바꾸는 설정(`core.commentChar`·`core.commentString`·`trailer.separators`·`trailer.<token>.key`) 중 설정된 것이 있으면 ` ('<name>'…)`를 쓰고 멈춘다(E_CONFIG). hint는 없다(설정을 어떻게 고칠지는 그 저장소가 정한다). MSG 때문에 읽히지 않는 것(v14 5.3의 자르기 행)은 E_USAGE이고, git 명령 자체가 실패하면 E_GIT다.

### 4.6 session

v14 5.1의 session 규칙(AIT_SESSION, worktree의 기본 session, `E_SESSION`)을 쓴다. 유효한 session이 필요한 명령: 요청, `contract commit`·`pull`, `fulfill`, `sub`, `unsub`, `listen`, SID를 생략한 `session close`. session 없이 되는 명령: `init`, `checkout`, `switch`, `status`, `contract new`·`add`·`push`, `session open`·`list`. 값이 설정됐는데 무효이면 어느 명령에서든 `E_SESSION`이다.

### 4.7 출력과 종료 코드

v14 5.6·5.7을 쓴다. 바뀌는 점: 요약 행(4.4의 5, 6.7)의 OID는 짧은 OID다. git 통과가 없고, 인자 없이 실행하면 exit 129다. git을 부르고 그 출력을 전하는 명령(`checkout`·`switch`·`status`의 git 부분, `contract push`, 4.4의 3 (a))은 git의 출력과 종료 코드를 그대로 쓴다.

### 4.8 오류 종류

| 이름 | 종료 | 첫 행 |
| --- | --- | --- |
| `E_USAGE` | 129 | `error: …` 다음 `usage: ait …` |
| `E_READ` | 128 | `fatal: could not read '…'` |
| `E_SESSION` | 128 | `fatal: no usable ait session: …`(hint는 v14 5.8) |
| `E_BUSY` | 128 | `fatal: cannot run now: …`(listener가 이미 돈다) |
| `E_ID` | 128 | `fatal: no such contract: '…'`(없는 ID·alias, kind가 맞지 않음) |
| `E_BRANCH` | 128 | 4.3의 0–2·5, `fatal: no local branch 'ait/<name>'`, `fatal: no contract branch 'ait/<name>'`, `fatal: 'ait/<name>' has no upstream` |
| `E_CONFIG` | 128 | `fatal: git config hides the trailers: …`(4.5) |
| `E_FORMAT` | 128 | `fatal: bad format: …`(파일과 행) |
| `E_SCOPE` | 128 | `fatal: bad scope: …`, `fatal: repository lacks the base blob of '…' ('…')` |
| `E_STATE` | 1 | `error: not allowed: …`, 아래의 거절 문구들 |
| `E_MOVED` | 1 | `error: 'ait/<name>' moved while running; nothing changed` |
| `E_CONFLICT` | 1 | `error: contract branches conflict in '…'`, `error: merging '…' breaks contract rules: …`(6.5.6) |
| `E_NEEDS_RESULTS` | 1 | `error: check results required: …`(hint는 v14 5.8) |
| `E_CHECK_FAILED` | 1 | `error: check failed: …` |
| `E_COMMIT` | 1 | `error: commit failed; '…' not fulfilled` |
| `E_NOT_RECORDED` | 1 | `error: committed '…' but '…' not fulfilled: …`, `error: '…' not fulfilled: …` |
| `E_GIT` | 128 | `fatal: git failed: …`, 저장소 밖이면 `fatal: not a git repository` |
| `E_IO` | 128 | `fatal: write error: …` |

  `E_STATE`의 문구: 전이(`error: not allowed: …`), 섞기(`error: 'D' is already undertaken partially by 'O'`·`wholly`), 기대는 짝(`error: changing 'X' would leave 'Y' fulfilled on top of an unfulfilled 'X'`), `--force`의 provider(`error: cannot change the provider of 'K' with --force`), 이행 기록(`error: fulfilments are recorded only by 'ait fulfill'`), 낡은 초안(`error: 'K' changed at 'ait/<name>' since '<path>' was written`), unmerged(4.3의 5), drop(`error: not allowed: 'X' targets 'K', which is not current`), 이미 있는 경로(`error: not allowed: '<path>' already exists`), 끝난 계약(`error: not allowed: 'K' has ended`), 정체 변경(`error: not allowed: 'K' changes its group, kind or creator`), 정해지지 않은 state 변경(`error: not allowed: 'K' goes from '<old>' to '<new>'`). 섞기의 "already"는 T에서 그 D를 떠맡고 있던 쪽이다. 이 판정들은 이유를 돌려주고 문구는 한 표에서 고른다. `--force`의 provider 거절의 hint는 `--force`를 뺀 같은 명령이다.

### 4.9 hint

v14 5.9a의 원칙(쓰는 순간의 상태로 판정, ait가 아는 값은 모두 채움, 사람이 정할 자리만 `<…>`, POSIX 셸 인용, 틀릴 수 있는 hint는 주지 않음, 현재 session이 기본 session이 아니면 `AIT_SESSION=SID ` 접두어)만 쓴다. v14 5.9a의 H 표는 쓰지 않는다. 각 hint는 이 규격의 그 절이 정한다. hint의 명령은 4.1의 집합과 마치는 git 커밋이고, git으로 하는 일은 이루어질 상태로 쓴다. 쓰기 명령의 hint에는 `-m <why>`를 붙인다.

### 4.10 구조 불변식 (한 함수)

ait 브랜치의 tree 하나에 대해 판정한다: (a) 형식(2.3, 2.5, 배치 2.1.3), (b) 떠맡기를 섞지 않음(한 D의 살아 있는 떠맡기는 모두 통째이거나 모두 부분, 1.7), (c) 기대는 짝 불변식(1.8). (a)를 어긴 tip은 4.3의 3이 거절한다. 체크인(4.4의 2)과 `contract pull`의 merge 결과(6.5.6)는 새 tree가 T보다 (b)·(c)의 위반을 늘리면(위반을 계약 ID와 종류로 세어 T에 없던 것이 있으면) 거절한다. 그래서 git merge로 이미 생긴 위반은 그 계약을 고치는 요청을 막지 않고, `status`가 `warning: '<ID>' breaks <rule>`로 보여 준다(10.4).
- 거절 hint: 섞기면 같은 요청에 상대 쪽을 모두 release하는 절을 더한 명령. 기대는 짝이면 같은 요청에 `future amend Y`(Y마다)를 더한 명령. 둘 다 받은 요청의 절과 옵션을 그대로 두고 덧붙인다.

## 5. 판정 (규칙 4)

- 5.1 **tree X**: 읽는 명령(`status`)과 이행하는 명령의 0단계에서는 index의 tree다(v14 7.8.1). unmerged 경로는 HEAD 판으로 읽고 경로마다 `warning: 'P' is unmerged; it is read as HEAD has it`(이 경고는 git status 출력 앞에 쓴다). X를 만드는 함수는 하나다(그 경로를 HEAD 판으로 바꾼 임시 index에서 `write-tree`). 이행이 기록하는 코드는 커밋한 뒤의 HEAD다(6.7).
- 5.2 **유효한 이행**: 계약 K의 첨부 목록의 이행 파일 중 `condition-id`가 K의 `condition-id`와 같은 것(U21).
- 5.3 **범위가 같은가**(범위 하나, 이행 파일 하나): `code`에 그 범위 키가 없으면 다르다. 파일 범위는 X의 항목(mode 타입 OID)이 `code`의 항목과 같으면 같다. 행 범위는 `code`의 blob이 이 저장소에 없으면 다르다. 있으면 3.5의 diff에서 어떤 hunk의 접촉 구간(v14 3.10. 순수 삽입은 `[os, os+1]`)이 `code`의 s-e와 겹치면 다르다. mode가 다르거나 파일이 없으면 다르다.
- 5.4 **결과**(정의는 여기 하나): 저장된 state `future`인 D·O K는, 유효한 이행 중 K의 provider 범위가 모두 같은 이행이 하나라도 있고, K가 offer면 K가 통째로 떠맡은 살아 있는 D가 모두, K가 demand면 K를 부분으로 떠맡은 살아 있는 O가 모두 current이면 **current**다. 유효한 이행은 있는데 그렇지 않으면 **current ?unverified**, 유효한 이행이 없으면 **future**다. 범위마다 다른 이행을 섞지 않는다. 떠맡기의 조건이 고리를 이루지 않는다(통째와 부분은 한 D에서 섞이지 않고, 통째는 O → D, 부분은 D → O 한 방향이다). git merge로 섞기가 생겨(10.4) 고리가 되면, 판정하는 중에 다시 만난 계약은 current가 아니다(C69).
- 5.5 **걸린 계약**: HEAD에서 판정 current인 K의 provider 범위 중, HEAD와 X에서 5.3의 비교로 다른 것이 있으면 K는 지금 변경에 걸린다. `status`가 `touch`로 보여 준다(U2).

## 6. 명령

### 6.1 `ait init`

1. git 작업 폴더 안이 아니면 E_GIT(2.6.5). ait는 지원·검증 기준인 git 2.53 이상을 요구한다. 낮으면 `fatal: git X.Y is too old for ait`와 `hint: install git 2.53 or later`.
1a. `git rev-parse --git-path hooks/reference-transaction`이 없으면 알림 hook(9.8)을 쓴다. 이미 다른 내용이 있으면 고치지 않고 `warning: '<path>' exists; 'ait listen' wakes by polling only`.
2. `git rev-parse --git-path info/exclude` 파일에 `/.ait/` 행이 없으면 더한다.
3. 원격 추적 `refs/remotes/<remote>/ait/<name>`(2.1.2) 중 로컬에 없는 이름마다, 원격 하나에만 있거나 `checkout.defaultRemote`가 그 원격이면 `git branch -q ait/<name> <remote>/ait/<name>`(upstream이 설정된다). 여러 원격에 있고 고를 수 없으면 `warning: 'ait/<name>' matched multiple (N) remote tracking branches`(`builtin/checkout.c:1321`의 문구)를 쓰고 그 이름은 만들지 않는다.
4. 그러고도 이름의 집합이 비어 있으면 설정된 원격마다 `git ls-remote <remote> 'refs/heads/ait/*'`. 닿지 못하면 `warning: cannot reach '<remote>'; skipped`(U36). 닿은 원격에 ait 브랜치가 있으면 `fatal: '<remote>' has ait branches that are not fetched`(E_BRANCH)와 `hint: fetch 'refs/heads/ait/*' from '<remote>', then 'ait init'`(U29). 없으면 `ait/<D>`(`git var GIT_DEFAULT_BRANCH`)를 빈 tree의 커밋으로 만든다. 메시지 `[CONTRACT] init`, 기록줄 없음, `update-ref`의 old 값은 0 OID.
5. 다시 해도 결과가 같다. 출력: 만든 브랜치마다 `branch ait/<name>`.

### 6.2 `ait checkout`

- ait가 맡는 꼴: 인자가 정확히 `ait/<name>` 하나이거나 `ait/<name> --`이고 `ait/<name>`이 이름의 집합에 있을 때(git이 브랜치를 바꾸는 꼴, `builtin/checkout.c` `parse_branchname_arg`). 그 밖은 `git checkout`에 그대로 넘긴다(C30).
- 맡으면: 로컬 ait 브랜치면 체크아웃 파일에 쓰고 stdout에 `On contract branch ait/<name>`. 원격에만 있으면 git에 넘기지 않는다(넘기면 git이 브랜치를 만들어 작업 폴더를 바꾼다): `fatal: no local branch 'ait/<name>'`, 4.3의 1의 조건이면 `hint: ait init`.
- 성공 hint: `the work tree stays on '<branch>'; to enter the contract branch: 'ait switch ait/<name>'`(detached면 `'<branch>'` 자리에 `a detached HEAD`). `git diff --quiet HEAD`가 아니면 앞 절만 쓴다(그때 `ait switch`가 거절하므로, U35).

### 6.3 `ait switch`, `ait status`

- **`ait switch ait/<name>`**(U37): 인자가 정확히 `ait/<name>` 하나이고 이름의 집합에 있을 때만 ait가 맡고, 그 밖은 `git switch`에 넘긴다. 맡으면: 로컬이 아니면 `fatal: no local branch 'ait/<name>'`(4.3의 1의 조건이면 `hint: ait init`). `git diff --quiet HEAD`가 아니면 `fatal: uncommitted changes in the work tree; commit them before entering 'ait/<name>'`(stage한 새 파일이 ait 브랜치로 따라 들어가지 않게. 추적하지 않는 파일은 git처럼 따라 들어간다). 지나면 `git switch ait/<name>`의 출력과 종료 코드를 전하고, 성공하면 `hint: contracts change only through ait commands; to go back: 'ait switch -'`.
- **`ait status [PATH...]`**(U15, C34): PATH는 범위(v14 3.2의 형식)만 받는다.
  1. stdout에 `On contract branch ait/<name>`(4.2에서 자동이면 ` (the only one)`)을 쓰고 내보낸다.
  2. 5.1의 unmerged 경고(stderr).
  3. `git status -- <pathspec>...`을 stdout을 물려준 채 돌린다(출력을 손대지 않는다). PATH마다 pathspec은 `/`는 `:/`, `DIR/`는 `DIR/`, `PATH`·`PATH:S-E`·`new:PATH`는 `PATH`다(git이 행 범위를 모르고 `/`를 저장소 밖으로 읽기 때문이다, drafts 9.32). 같은 pathspec은 한 번만 준다. git이 0이 아닌 코드로 끝나면 그 코드로 끝낸다.
  4. 빈 행과 계약 블록. **고르기**: PATH가 없으면 session이 있을 때 내 약속, 내가 만든 demand, 그리고 누구에게나 `current ?unverified`인 계약, 지금 변경에 걸린 계약(5.5), 구조 불변식 위반(4.10). PATH가 있으면 consumer·provider가 PATH와 겹치는(v14 3.9, 사상 tree는 X) 계약. 행 범위 PATH는 X에서 고정한다.
  5. **블록**(ID 순, 빈 행으로 끝남):

```
id ID
kind demand|offer|drop
group GROUP|-
state current|current ?unverified|future|fulfilled|cancelled|withdrawn   (판정, 1.4)
creator AGENT
consumer DISPLAY                          (demand)
provider DISPLAY                          (demand·offer)
target ID                                 (drop)
spec exec|prompt BODY첫행                  (spec마다)
undertakes ID STATE AGENT whole|partial   (offer. STATE는 그 D의 판정)
undertaken-by ID STATE AGENT whole|partial (demand. STATE는 그 O의 판정)
fulfilled RID AGENT pass|forced|withdrawn valid|old COMMIT   (첨부마다)
touch                                     (지금 변경에 걸렸으면)
```

  DISPLAY는 v14 3.8의 표시를 `,`로 이은 것이다(X에서 사상한 지금 위치, `?changed`·`?missing`·`?deleted`·`?nobase`). 블록 뒤의 hint: 내 약속이고 판정 future나 current ?unverified인 D·O면 `ait fulfill ID -m <why>`, 아무도 떠맡지 않은 demand면 `ait future offer <provider> <files> as o undertake ID o -m <why>`(provider는 그 D의 provider).

### 6.4 요청 (도우미 명령)

문법은 v14 7.3.1의 규칙(위치 인자는 `-`로 시작할 수 없고 amend의 단독 `-`만 예외, 문법 위반·alias 중복·옵션 중복·`-r`와 `--force`를 함께 쓰면 E_USAGE)을 쓰고 아래와 같다.

```
request := clause+ option*
option  := "-m" MSG | "-F" FILE | ("-r"|"--result") FILE | "--force" | "-g" GROUP
clause  := "demand" A B FILES ["as" NAME]
         | "offer" B FILES ["as" NAME]
         | "future" "demand" A B FILES ["as" NAME]
         | "future" "offer" B FILES ["as" NAME]
         | "future" "drop" K ["as" NAME]
         | ["future"] "amend" K [ARGS]        (D: A B FILES, O: B FILES, X: K; "-"는 그대로 둠)
         | "future" "cancel" K
         | "undertake" ["-p"|"--partial"] K K  (D O)
         | "release" K K                       (D O)
K       := ID | alias
FILES   := PATH ("," PATH)*                   (조건 파일, 6.4.2. 저장소 뿌리 기준, 3.1. `.ait/` 아래 허용)
A, B    := 범위 목록 (v14 3.2a)
```

- 6.4.1 **절의 효과**: 절을 순서대로 T에 적용한다. 각 절의 조건은 적용하는 시점에, 구조 불변식(4.10)은 모든 절을 적용한 뒤의 tree에 대어 본다. `future`가 붙은 절은 이행하지 않고, 붙지 않은 `demand`·`offer`·`amend`는 그 계약을 같은 요청에서 이행한다(6.7의 0–3. 결과 `-r`나 `--force`가 필요하다. 둘 다 없으면 결과 틀을 쓰고 E_NEEDS_RESULTS).
  - `future demand A B FILES`, `future offer B FILES`, `future drop K`: 새 계약(state `future`, `condition-id`=`change-id`=RID, creator=session의 agent, 그룹은 `-g`, 없으면 뿌리). drop은 K가 판정 current인 D·O여야 한다.
  - `future amend K ARGS`: `-`가 아닌 필드를 바꾼다. 확인 조건이 바뀌면 `condition-id`를, 계약 행이 바뀌면 `change-id`를 RID로. 인자가 없으면(빈 amend) `condition-id`와 `change-id`를 RID로 바꿔 K를 다시 연다(옛 이행이 세지 않게 된다). `--force`면 사소한 변경이다: `condition-id`를 두고 `change-id`만 바꾼다(U7). `--force`로 provider를 바꾸면 `error: cannot change the provider of 'K' with --force`(E_STATE, 그 이행은 옛 provider를 확인한 것이다, C23).
  - `amend K ARGS`, `demand …`, `offer …`: 위와 같이 바꾸거나 만들고, 그 계약을 이행한다. 여기서 `--force`는 결과 대신 판단으로 이행한다는 뜻이다(v14의 `--force`).
  - `future cancel K`: state `cancelled`(이미 cancelled면 noop). D·O·X 모두.
  - `undertake [-p] D O`: D는 저장된 state `future`인 demand, O는 저장된 state `future`인 offer(아니면 kind가 틀리면 E_ID, state가 틀리면 E_STATE). O의 `undertakes`(통째) 또는 `undertakes-partially`(부분)에 D를 더한다. 이미 그 목록에 있으면 noop, 다른 목록에 있으면 옮긴다. 통째는 O의 `condition-id`와 `change-id`를, 부분은 `change-id`만 바꾼다(1.7, 2.3.3).
  - `release D O`: O의 두 목록에서 D를 뺀다(통째에서 빼면 `condition-id`도). 없으면 noop.
- 6.4.2 **조건 파일**(FILES): 각 PATH는 작업 폴더의 YAML이고 spec 원소 하나의 형식(2.3.2)이다. 형식을 검사하고(E_FORMAT, 파일과 행) 그 내용을 `specs`에 FILES 순서대로 담는다. 파일은 작업 폴더에 남는다. 없거나 읽을 수 없으면 E_READ와 `hint: ait contract new spec '<path>' exec|prompt`.
- 6.4.3 `-g GROUP`은 2.1.3의 그룹 규칙(어기면 E_USAGE)이고 새 계약에만 쓴다.
- 6.4.4 체크인 하나다(4.4). 이 체크인이 읽은 파일은 절이 값을 쓴 계약 YAML(4.10이 읽은 것 포함)이고, 바꾸는 파일은 그 요청이 쓰는 계약 YAML과 이행 파일이다. 출력: 새 계약마다 `new ID [NAME]`, 이행한 계약마다 `fulfilled ID`(6.7.8), 이어서 4.4의 요약 행.

### 6.5 `ait contract` (U43)

- 6.5.1 `ait contract new spec PATH exec|prompt`: 조건 파일의 틀을 PATH에 쓴다(PATH는 필수, 3.1, `.ait/` 아래 허용. 상위 디렉터리를 만든다). 틀은 v14 7.4.1a의 내용에서 `inputs` 행과 주석의 `inputs`를 뺀 것이다. 이미 있으면 E_STATE. 출력 `path PATH`.
- 6.5.2 `ait contract new demand|offer|drop [GROUP]`: 새 ID로 `.ait/<GROUP>/<ID>.yaml`에 틀을 쓴다. 틀은 2.3.2의 그 kind의 키를 모두 두고, `condition-id`·`change-id`·`creator`는 빈 값(`""`), `state: future`, 범위·target·specs의 자리는 `"<scope>"`·`"<ID>"`·`- body: |\n    <condition>`, 목록은 2.3.2의 빈 꼴이다. ID 유일성은 4.2로 정해지는 ait 브랜치가 있으면 그것, 없으면 로컬 ait 브랜치 모두에 대해 본다. 출력 `path PATH`.
- 6.5.2a `ait contract checkout K...`(U49): 상대하는 ait 브랜치의 `<group>/K.yaml`을 작업 폴더의 `.ait/<group>/K.yaml`로 꺼낸다(git의 `git checkout <branch> -- <path>`와 같은 쓰임). 4.3의 0–3을 지난다. 이미 있으면 덮어쓰지 않고 E_STATE(`error: not allowed: '<path>' already exists`). 꺼낸 초안의 `change-id`가 그때의 값이므로, 그사이 남이 고치면 `contract commit`이 낡은 초안으로 막는다(6.5.4). 출력 `path PATH`.
- 6.5.3 `ait contract add PATH...`: PATH는 `.ait/` 아래의 `<group>/<ID>.yaml` 꼴이어야 한다(아니면 E_USAGE). 초안 형식(2.3.1에서 `condition-id`·`change-id`·`creator`의 빈 값을 허용)을 검사하고, 그 내용을 계약 stage(2.6.4)에 `<group>/<ID>.yaml` 경로로 담는다(add할 때의 내용, git index처럼). 출력: PATH마다 `add <group>/<ID>.yaml`.
- 6.5.4 `ait contract commit -m MSG [--force]`: stage한 계약들을 체크인 하나로 올린다. 이행하지 않는다. 계약마다:
  - 새 계약(T에 그 ID가 없음): 2.4의 "없음 →" 전이를 따른다. `condition-id`·`change-id`는 RID, `creator`는 session의 agent.
  - 있는 계약: 초안의 `change-id`가 T의 `change-id`와 다르면 낡은 초안이다: `error: 'K' changed at 'ait/<name>' since '<path>' was written`(E_STATE). 그룹이 다르면 E_STATE(계약은 그룹을 옮기지 않는다). 초안의 `fulfilled`가 T의 목록의 앞부분(prefix)이 아니면 `error: fulfilments are recorded only by 'ait fulfill'`(E_STATE). 아니면 T의 `fulfilled`를 그대로 두고, 확인 조건이 바뀌었으면 `condition-id`를, 계약 행이 바뀌었으면 `change-id`를 RID로(`--force`면 `condition-id`를 두고, provider가 바뀌었으면 6.4.1의 `--force` 거절).
  - 끝나면 stage를 비운다. 작업 폴더의 파일은 남는다. 출력: 계약마다 `new ID` 또는 `change ID`, 이어서 요약 행.
- 6.5.5 `ait contract push [<remote>]`: 4.3의 0–3을 지난 뒤 `git push <remote> refs/heads/ait/<name>:refs/heads/ait/<name>`. remote는 인자, 없으면 `git for-each-ref --format='%(push:remotename)' refs/heads/ait/<name>`이 주는 것(git의 `branch.<b>.pushRemote` → `remote.pushDefault` → `branch.<b>.remote` 순, 비면 `origin`). git의 출력과 종료 코드를 전한다.
- 6.5.6 `ait contract pull [<remote>]`: 4.3을 지난 뒤
  1. remote는 인자, 없으면 `%(upstream:remotename)`, 비면 `origin`. `git fetch <remote>`(원격의 설정된 refspec을 쓴다).
  2. upstream U = fetch 뒤 이름의 집합(2.1.2)을 읽은 `git for-each-ref`의 `%(upstream)`이 그 집합에 있는 원격 추적 ait ref일 때(`git rev-parse ait/<name>@{upstream}`과 같은 답이다. listen의 9.2도 이 판정을 쓴다, U56). 없으면 `fatal: 'ait/<name>' has no upstream`(E_BRANCH).
  3. 출력과 메시지의 `<U>`는 git이 merge 메시지에 쓰는 대로 줄인 이름이다(`git rev-parse --abbrev-ref`, 예: `origin/ait/master`).
  3a. `git merge-base --is-ancestor U ait/<name>`이면 `Already up to date`. `git merge-base --is-ancestor ait/<name> U`이면 U로 4.4의 3을 한다(fast-forward. 출력 `Fast-forward <old>..<new>`).
  4. 갈라졌으면 `git merge-tree --write-tree --name-only ait/<name> U`. 충돌이 있으면 아무것도 바꾸지 않고 `error: contract branches conflict in '<paths>'`(E_CONFLICT)와 `hint: merge '<U>' into 'ait/<name>' and resolve the conflicts there (enter it with 'ait switch ait/<name>')`. 충돌이 없으면 결과 tree에 4.10을 대어 본다. (a)를 어기거나 (b)·(c)를 늘리면 `error: merging '<U>' breaks contract rules: '<ID>' <rule>`(E_CONFLICT, 같은 hint). 지나면 두 부모(`-p ait/<name> -p U`)의 커밋을 만들어 4.4의 3–5로 체크인한다(메시지 `[CONTRACT] merge '<U>'`, 기록줄. CAS에 지면 새 tip에서 merge부터 다시 한다. merge는 모든 파일을 읽으므로 4.4의 4에서 멈추지 않는다).

### 6.6 (비움)

### 6.7 `ait fulfill` (U8, U27, U31–U33)

- 6.7.1 **문법**: `ait fulfill K[=SCOPES]... [-r FILE | --force] -m MSG`. K는 여럿일 수 있고 덮는 계약 집합은 각 K의 집합의 합이다. `K=SCOPES`는 그 K의 provider를 SCOPES로 바꾼다(같은 요청의 amend: `condition-id`·`change-id`가 RID가 되고 새 `condition-id`로 이행한다, 3.3). 덮이기만 하는 계약에는 `=SCOPES`를 줄 수 없다(E_USAGE). `-r`·`--force`가 없으면 결과 틀을 쓰고 E_NEEDS_RESULTS(v14 7.7.6처럼 stdout에 `task NAME` 행들, `request PATH`, `result PATH`).
- 6.7.2 **덮는 계약 집합**(정의는 여기 하나): K가 offer면 K와 K가 통째로 떠맡은 살아 있는 D들. K가 demand면 K와 K를 부분으로 떠맡은 살아 있는 O 중 X에서 판정 current가 아닌 것들. K가 drop이면 K(target의 state 변경이 따른다. drop은 spec이 없어 과제가 없고 결과 없이 이행한다). 0단계에서 정한 집합을 끝까지 쓴다: 체크인이 다시 판정할 때 집합이나 그 계약들의 `condition-id`가 0단계와 다르면 E_MOVED이고 hint는 없다(결과가 확인한 것과 다르다, C67). 같은 요청이 만드는 새 계약의 ID는 다시 판정해도 처음의 것이다.
- 6.7.3 **결과 틀**: 덮는 계약마다의 `condition-id`(`condition-ids`)와 과제(7장). 과제에는 행 범위가 index에서 몇 행인지(기준 blob에서 사상한 s-e)를 보여 준다.
- 6.7.4 **위치 판정**(한 함수): provider 범위마다 찾는다. 3.3의 구체 범위가 아니면 E_SCOPE. 경로가 없거나 행 범위의 사상이 `?deleted`면 `fatal: bad scope: '<scope>' of 'K' is not in the code`. 계약의 기준 blob이 이 저장소에 없으면 `fatal: repository lacks the base blob of 'K' ('<path>')`(E_SCOPE, git `apply --3way`와 같은 모양)와 `hint: ait future amend K - '<provider>' - -m <why>`(offer면 `K '<provider>' -`. provider를 지금 판에서 다시 지정하면 새 기준 blob이 생긴다). 결과 틀, 0단계(index), 2단계(HEAD), `--force`가 이 함수를 부른다.
- 6.7.5 **순서**:
  0. 먼저 판정한다(아무것도 바꾸지 않는다): 4.3(쓰는 worktree는 지금 tip T로 묻는다: `git fetch --dry-run . T:refs/heads/ait/<name>`), `=SCOPES`의 amend와 4.10(기대는 짝: `K=SCOPES`가 K의 `condition-id`를 바꿔 K에 기대는 Y를 남기면 거절하고 `hint: ait future amend K - '<scopes>' - future amend Y -m <why>` 뒤 다시 이행), 결과가 덮는 계약 집합과 같고 그 계약마다 T에서 저장된 state `future`이며 `condition-id`가 결과의 `condition-ids`와 같은가(7.3), 위치 판정(index), 결과 형식. 하나라도 틀리면 멈춘다.
  1. 관련 파일을 커밋한다: `git commit -m MSG`(hook·서명은 git의 것). stage한 것이 없으면(`git diff --cached --quiet`. intent-to-add는 git commit처럼 없는 것으로 본다) 커밋하지 않고 `warning: nothing to commit; recording the code at HEAD ('<short OID>') for <'ID'들>`을 쓰고 계속한다(U33). 커밋이 실패하면 git의 오류를 그대로 보인 뒤 `error: commit failed; <'ID'들> not fulfilled`(E_COMMIT). 아무것도 기록되지 않았다.
  2. 체크인한다(4.4): 덮는 계약마다 HEAD에서 위치 판정을 다시 하고(hook이 범위를 지웠을 수 있다) `code`를 계산해 이행 파일을 쓰고 각자의 첨부 목록에 더한다. drop이면 target의 state를 `withdrawn`으로. 이 체크인이 읽은 파일은 덮는 계약과 그 판정에 쓴 계약의 YAML, 바꾸는 파일은 덮는 계약의 YAML(drop이면 target도)과 새 이행 파일이다.
  3. 2가 실패하면(위치, 4.4의 3 (a), E_MOVED, T'=T) 기록은 없다. 1에서 커밋했으면 `error: committed '<short OID>' but <'ID'들> not fulfilled: <이유>`, 아니면 `error: <'ID'들> not fulfilled: <이유>`(E_NOT_RECORDED). hint는 E_MOVED로 멈췄고 덮는 계약이 T'에서 모두 그대로일 때만 `ait fulfill <받은 K 인자> -r '<result>' -m '<msg>'`(채운 값. 다시 하면 그때 stage한 것을 커밋하고 그때의 HEAD를 기록한다).
- 6.7.6 기록하는 코드는 1의 뒤 HEAD다. hook이 고친 것이든, hook이 stage를 비워 빈 커밋이 된 것이든 HEAD의 사실이다(U27).
- 6.7.7 `--force`: 결과 없이 0–3을 지난다. `condition-id`는 T의 것, `outcome: forced`, 본문은 MSG.
- 6.7.8 출력: 덮는 계약마다 `fulfilled ID`(drop이면 이어서 `withdrawn TARGET`), 1의 커밋이 있으면 git commit이 쓰는 요약 행, 이어서 4.4의 요약 행.

### 6.8 `ait sub`·`unsub`·`listen`, `ait session`

v14 7.2(session open·list·close, sub·unsub, listen의 잠금·신호·`--fetch`)를 쓴다. 바뀌는 점:
- `sub scope PATH:S-E`의 기준 blob은 그 session의 worktree의 index에서 고정한다. 기준 blob이 없으면 그 구독은 파일 전체로 본다.
- `sub id K`는 상대하는 ait 브랜치에 K가 있어야 한다(E_ID).
- `listen --fetch SEC`은 SEC초마다 원격마다 `git fetch <remote>`.
- listen 도중 상대할 ait 브랜치를 정할 수 없게 되면(4.3의 2) 그 차례를 건너뛰고 `warning:`으로 그 문구를 한 번 쓴다. 정할 수 있게 되면 이어서 돈다.
- agent 이름 `unknown`은 v14 7.2.1대로 예약어라 쓸 수 없다(`session open`은 `error: bad agent name 'unknown'`과 사용법, 129). v1.1은 기록줄 없는 커밋에 보낸 session을 두지 않으므로 기록줄에 `unknown`을 쓰지 않지만, 예약은 그대로 둔다(U59).

## 7. 확인 결과

v14 8장의 확인 요청 파일·결과 파일 형식과 쓰기 규칙(v14 8.2의 YAML 쓰기, 8.4의 검사 순서)을 쓴다. 바뀌는 점:
- 7.1 **과제**: 덮는 계약의 spec마다 하나. 이름은 `<계약>:<n>`(n은 `specs`의 순번, 1부터). 같은 요청에서 새로 만드는 계약은 `+1`, `+2`, …(new 행 순서).
- 7.2 **확인 요청 파일**: `input` 대신 `condition-ids`(덮는 기존 계약마다 `ID: "RID"`, ID 순)를 쓰고, `tree`는 index의 tree다. 과제의 키는 v14 8.2에서 `role`을 `kind`로 바꾼 것이다. exec면 `environment`에 코드 브랜치의 환경 정의를 HEAD에서 읽어 그 행들을 들여써 싣는다. HEAD에 그 파일이 없으면 `fatal: could not read 'specs/environments/<이름>.yaml' at HEAD`(E_READ, C61).
- 7.3 **결과 파일**: `input` 대신 `condition-ids`. 0단계가 덮는 기존 계약마다 T의 `condition-id`와 비교하고, 다르거나 빠지거나 남으면 다른 검사 없이 E_NEEDS_RESULTS(그사이 계약이 바뀌었다). 새 계약(`+n`)은 `condition-ids`에 없다. ait는 결과를 코드와 묶지 않는다(U27). 나머지(과제가 빠짐·더함·겹침, outcome, pass의 basis, fail·unknown이면 E_CHECK_FAILED)는 v14 8.4다.
- 7.4 결과 틀은 이미 있고 그 `condition-ids`와 과제 목록이 지금과 같으면 다시 쓰지 않는다.

## 8. (비움)

## 9. 알림 (규칙 6, U28)

- 9.1 **tip**: session 파일의 `tip OID REFNAME` 행들은 `refs/heads/ait/*`와 원격 추적 ait ref(2.1.2) 전체의 스냅샷이다. `session open`과 차례마다 갱신한다.
- 9.2 **후보**: `git rev-list --reverse --topo-order --ignore-missing --stdin`에 감시하는 ref의 OID들과 tip의 OID마다 `^OID`. 감시하는 ref는 session의 worktree가 상대하는 로컬 ait 브랜치와, upstream이 원격 추적 ait ref면 그 upstream이다. 되감기나 force push로 사라진 커밋은 알리지 않는다.
- 9.3 **받는 상태** V: 구독, 받는 worktree의 HEAD, 체크아웃 파일의 내용. 내보내기 전에 V가 바뀌었으면 그 차례를 멈춘다(v14 9.5-3).
- 9.4 **사건과 키**: 후보 커밋 C가 첫 부모와 비교해 바꾼 계약 파일(`git diff-tree -r --root --name-only --diff-merges=first-parent C`). 키는 바뀐 계약마다 `version ID BLOB`(C의 `<ID>.yaml` blob), 이행 파일마다 `fulfil ID BLOB`(ID는 그 이행 파일의 `contract`). 같은 키는 한 번만 나간다(사본 포함). C의 기록줄의 session이 받는 session이면 건너뛴다.
- 9.5 **받는 쪽**(관계는 받는 worktree의 HEAD와 상대하는 로컬 ait 브랜치의 tip에서, 사건은 C와 첫 부모 양쪽의 판에서): 내 약속이면 `reason future ROLE K`, K가 내 약속 M의 상대면 `reason future ROLE(M) M`, 내 요구면 `reason demand K`, `sub id K`면 `reason sub id K`, `sub scope Q`가 C나 첫 부모의 판에서 K의 consumer·provider와 기대는 겹침(v14 3.9a)이면 `reason sub scope DISPLAY(Q)`. drop X가 바뀌면 target도 바뀐 계약으로 센다. 버려진 변경은 알리지 않는다(U34).
- 9.6 **전달**: v14 9.5의 순서와 9.6(at-least-once). 차례의 처음에 잠금 안에서 V와 tip 스냅샷 N을 읽고, 후보는 N에서 구한다. 키는 한 차례 안에서도 한 번만 나간다. 내보낸 사건의 키는 내보낼 때, 건너뛴 사건(받는 쪽이 아님, 자기 session의 요청)의 키와 N은 차례 끝에 V가 그대로일 때만 쓴다. stdout 쓰기가 실패하면 listen이 끝난다(EPIPE면 SIGPIPE, 그 밖에는 `fatal: write error`).
- 9.7 **payload**: `ait-event 1`, `commit OID`, `recipient SID`, `ref REFNAME`(C를 담은 감시 ref, 이름순), `reason …` 행들(v14 9.7의 순서), `in-head yes|no`(키가 모두 받는 session이 상대하는 로컬 ait 브랜치의 tip에 있으면 yes), `contract ID ACTION` 행들(계약마다 해당하는 것을 `create`·`amend`(`condition-id`가 바뀜)·`edit`(`change-id`만)·state 이름·`fulfil` 순으로 한 행씩), 빈 행, `git show --no-color --no-ext-diff --no-textconv --no-show-signature --diff-merges=first-parent --format=fuller --stat --patch C`, `ait-event-end`. 코드 쪽의 `current ?unverified`는 알리지 않는다(C37).

- 9.8 **깨우기**(U51): `ait init`이 둔 `reference-transaction` hook은 `committed` 단계에서 `refs/heads/ait/`나 원격 추적 ait ref가 바뀌면 `<git-common-dir>/ait/wake`에 한 행을 더한다(4096바이트를 넘으면 먼저 비운다). `ait listen`은 0.1초마다 그 파일의 크기를 보고 바뀌면 한 차례를 돈다(mtime은 초 단위라 같은 초의 두 번째 변경을 놓친다). hook이 없거나 다른 hook이면 1초마다, 있으면 30초마다도 돈다(hook이 모르는 변경을 채운다).

## 10. 한계

- 10.1 이행 뒤 코드가 바뀐 것은 막지 않고 보여 준다.
- 10.2 행 범위의 판정은 기록한 blob이 그 저장소에 있어야 한다. 올리지 않은 코드 커밋의 blob이면 clone마다 판정이 다를 수 있다.
- 10.3 ait 밖에서 ait 브랜치의 계약 행을 고치고 `change-id`·`condition-id`를 두면 판단 없는 `--force`와 같아진다. 흐름 밖이다(U16).
- 10.4 git으로 직접 ait 브랜치끼리 merge하면 구조 불변식(4.10)을 지나지 않는다. 생긴 위반은 `status`가 보여 주고, 위반을 늘리는 요청만 막힌다.
- 10.5 체크인의 3 (a)와 (b) 사이에 누가 그 ait 브랜치를 체크아웃하는 경합은 남는다.
- 10.6 결과가 참인지, 확인이 어느 작업 폴더에서 돌았는지 ait는 보장하지 않는다(U27).
