---
name: ait
description: ait v1.1의 별도 계약 브랜치를 쓰는 저장소에서 요구·약속·떠맡기·이행·알림을 다룬다. 사용자가 ait를 요청하거나 저장소가 ait 계약을 사용하면, 코드를 수정하거나 커밋하기 전에 읽는다.
---

# ait v1.1

다른 코드에 기대는 조건을 demand로, 내가 제공할 조건을 offer로 적는다. 착수할 때 offer가 demand를 떠맡고, 구현과 검사를 마치면 이행한다. 구현이 아직 없으면 [명세만 있는 저장소](references/spec-only.md)를 읽는다.

## 시작

1. 저장소에서 `ait init`을 실행한다. 계약은 코드와 독립된 `ait/<name>` 브랜치에 생긴다. 코드 브랜치의 `.ait/`는 초안 자리이며 `.ait/init`을 쓰지 않는다.
2. `ait session list`에서 내 agent 이름과 현재 worktree가 맞는 session을 재사용한다. 없으면 `ait session open --agent <name>`. 다른 session을 명시하려면 `AIT_SESSION=<SID>`를 쓴다. 에이전트마다 코드 worktree와 session을 따로 둔다.
3. `ait status /`로 전체 계약을 보고, 작업할 범위는 `ait status src/stock.c`처럼 좁힌다. PATH 인자는 범위이며 계약 ID 필터가 아니다.
4. ait 브랜치가 여럿이면 `ait checkout ait/<name>`으로 상대할 브랜치를 고른다. 코드 작업 폴더는 그대로다. `ait switch ait/<name>`은 실제로 계약 브랜치에 들어가므로 코드 개발에 쓰지 않는다.

Git 2.53 이상이 필요하다. ait는 아래의 정해진 명령만 받는다. 일반 Git 작업은 `git`으로 한다.

## 계약과 명세 정의

범위는 저장소 뿌리 기준이다: `src/stock.c`, `src/stock.c:10-40`, `src/`, `/`, 아직 없는 파일은 `new:src/stock.c`. 여럿이면 `,`로 잇는다. 행 범위는 보통 HEAD에서, 이행하는 요청과 `K=SCOPES`는 index에서 고정한다. 파일 단위 범위를 우선한다.

정의 파일은 `ait contract new spec specs/stock.yaml exec` 또는 `prompt`로 틀을 받아 쓴다.

```yaml
body: 재고가 모자라면 예약을 거절하고 재고를 바꾸지 않는다
engine: exec
command: [sh, tests/stock.sh]
success: [0]
fixtures: [tests/stock.sh, tests/stock-cases.csv]
```

`body`는 필수다. exec는 `command`가 필요하고, `success` 생략은 `[0]`이다. prompt는 `body`와 `fixtures`만 쓴다. exec는 선택적으로 `environment: NAME`을 쓰며 이행할 index의 `specs/environments/NAME.yaml`을 참조한다. ait가 검사를 실행하지 않으므로 에이전트가 직접 실행·확인한다.

`fixtures`에는 검사가 읽는 코드 쪽 파일(테스트, 데이터, 근거 자료)을 provider처럼 파일이나 행 범위(`tests/stock.sh:10-40`)로 적는다. 계약을 만들 때 그 파일이 있어야 하고, 디렉터리는 쓸 수 없다. 이행하면 provider와 함께 그 파일들의 내용 지문이 기록되고, 환경 정의 파일도 저절로 들어간다. 이행 뒤 fixture나 환경 정의를 고치면 계약은 `current ?unverified`가 되고 status의 `fixtures` 행에 `?changed`가 붙는다. 바꾼 것이 맞으면 다시 확인해 이행하고, 아니면 이행 파일의 `commit`의 판으로 되돌린다. 검사를 통과시키려고 fixture를 약하게 고치지 않는다.

조건 파일은 계약의 `specs`로 복사된다. 원래 파일을 고친 것만으로 계약이 바뀌지 않는다. 고친 파일을 amend에 다시 주어야 한다. 다른 worktree가 그 원본 파일을 가지고 있다고 가정하지 않는다.

```sh
ait future demand src/order.c src/stock.c specs/stock.yaml -m "주문이 기대는 재고 조건"
ait future offer src/stock.c specs/stock.yaml as o undertake D0123456789ab o -m "구현 착수"
```

예시 ID는 실제 출력의 ID로 바꾼다. `as o`는 같은 요청 안에서만 쓸 수 있다. `undertake D O`는 통째 떠맡기, `undertake -p D O`는 부분 떠맡기다. 같은 살아 있는 demand에 통째·부분을 섞을 수 없다.

`/`, `DIR/`, `new:PATH` 같은 추상 provider는 future로 약속할 때 쓴다. 이행할 때는 존재하는 파일이나 행 범위로 좁힌다.

## 코드 수정과 이행

1. 수정 전 `ait status <범위>`에서 계약과 조건, `undertakes`·`undertaken-by` 관계를 확인한다. current 판정은 코드와 이행 기록에서 계산된다. 계약 YAML의 저장된 `state`만 보고 current를 판단하지 않는다.
2. 코드를 구현하고 필요한 파일을 `git add`한다. **fulfill은 현재 Git stage 전체를 코드 커밋으로 만든다.** `git diff --cached`로 이번에 커밋할 내용을 확인한다.
3. `ait fulfill <K>... -m "변경 이유"`를 실행한다. 같은 변경에 걸린 다른 current 계약도 확인하여 필요한 ID를 함께 준다.
4. `check results required`이면 stdout의 `request PATH`, `result PATH`를 읽는다. 요청의 `tree`는 검사할 index tree다. 작업 폴더의 다른 내용으로 대신 검사하지 않는다. 필요하면 임시 폴더에 그 tree를 꺼낸다.

   ```sh
   top=$(git rev-parse --show-toplevel)
   d=$(mktemp -d)
   GIT_INDEX_FILE="$d/index" git -C "$top" read-tree <tree>
   GIT_INDEX_FILE="$d/index" git -C "$top" checkout-index -a --prefix="$d/tree/"
   ```

5. 각 과제를 검사하고 결과 파일에 `outcome: pass|fail|unknown`과 `basis`·`reason`을 채운다. pass에는 실제 검사 근거가 필요하다. 실행하지 않은 검사를 pass로 쓰지 않는다. `condition-ids`와 과제 이름을 고치지 않는다.
6. 같은 명령에 `-r <결과 경로>`를 붙여 다시 실행한다. 코드가 바뀌었으면 다시 검사한다. `condition-ids`는 계약 조건의 일치만 확인하며 검사 결과를 코드와 묶어 보증하지 않는다.

```sh
ait fulfill O0123456789ab -r /path/to/check-result.yaml -m "재고 예약 구현"
ait fulfill O0123456789ab=src/stock.c -r /path/to/check-result.yaml -m "구현 위치 확정"
```

좁힐 범위는 첫 실행과 `-r` 재실행에 똑같이 준다.

offer를 이행하면 그 offer가 통째로 떠맡은 살아 있는 demand도 함께 이행한다. demand를 이행하면 그것을 부분으로 떠맡은 current가 아닌 offer들이 포함될 수 있다. 요청 파일의 과제 전체를 검사한다.

일반 코드 커밋은 `git commit`이다. 이행은 Git의 hook·서명을 거쳐 코드 커밋을 만든 뒤 계약 브랜치에 이행을 기록한다. stage가 비었으면 현재 HEAD를 기록한다. 코드 커밋 뒤 계약 기록이 실패하면 `committed ... but ... not fulfilled`를 따로 알리므로 성공으로 간주하지 않는다.

실제로 사소하다고 판단한 변경만 `-r` 대신 `--force`로 이행한다. `forced`로 기록된다. 명세나 구현을 확인하지 못한 일을 넘기는 수단으로 쓰지 않는다.

## 조건 변경·재개·철회

```sh
ait future amend O0123456789ab - specs/new-stock.yaml -m "새 조건"
ait future amend O0123456789ab -m "여러 커밋에 걸친 재작업"
ait future cancel O0123456789ab -m "약속 중단"
ait release D0123456789ab O0123456789ab -m "이 요구의 떠맡기를 놓음"
ait future drop O0123456789ab -m "현재 보장 철회 제안"
```

D의 amend 인자는 `consumer provider FILES`, O는 `provider FILES`, drop은 `target`이다. 인자의 단독 `-`는 그대로 둔다는 뜻이다. 인자 없는 future amend는 새 조건 ID로 재개하므로 이전 이행이 current의 근거가 되지 않는다. 재작업 후 다시 fulfill한다.

한 요청에 절을 이어 쓸 수 있다. 통째 offer가 기대는 demand의 조건을 다시 열면 그 offer도 같은 요청에서 다시 열어야 할 수 있다. 각 절의 전이는 그 시점에, 떠맡기 섞기·기대는 짝은 요청 전체에 대해 검사하므로 오류의 hint를 따른다.

current 보장을 철회하려면 `future drop K`가 만든 X를 확인하고 `ait fulfill X -m "철회"`한다. target은 withdrawn이 된다. 끝난 계약을 직접 되살리거나 이행 파일·첨부 목록을 손으로 고치지 않는다.

## 직접 작성하는 초안

`ait contract new demand|offer|drop [GROUP]` 또는 `ait contract checkout K...`가 `.ait/`에 초안을 만든다. 필요한 필드를 고친 뒤 별도 계약 stage에 넣는다. 아래는 그룹이 없는 계약의 예다. 그룹이 있으면 checkout이 출력한 `path PATH`의 PATH를 쓴다.

```sh
ait contract checkout O0123456789ab
ait contract add .ait/O0123456789ab.yaml
ait contract commit -m "계약 초안 변경"
```

계약 add·commit은 사용자의 코드 Git stage를 포함하지 않는다. 직접 쓴 신규 계약도 future에서 시작하며 이행은 fulfill만 기록한다. 초안이 낡았다는 오류가 나면 기존 초안을 별도 위치로 보관·이동한 뒤 checkout으로 현재 판을 받는다. 새 초안에 의도한 변경만 다시 적용하여 add·commit한다. checkout은 기존 초안을 덮어쓰지 않는다.

## 같은 clone·원격·알림

같은 clone의 worktree들은 `ait/*` ref를 공유한다. 상대하는 ait 브랜치가 같으면 다른 에이전트의 계약은 즉시 보이며 ID를 넣어 pull하지 않는다. 원격과 주고받을 때는 계약 브랜치의 Git upstream을 준비한다. 아래 `ait/master`는 status 첫 행에 나오는 실제 이름으로 바꾼다.

```sh
git push -u origin ait/master   # 처음 upstream을 정할 때
ait contract push
ait contract pull              # 원격 이름만 선택적으로 받음
ait sub src/stock.c
ait sub O0123456789ab
ait listen --fetch 30
```

`ait sub`는 ID나 범위를 받는다(`sub id K`를 직접 입력하는 문법이 아니다). `ait unsub <ID 또는 표시 범위>`로 구독을 제거한다. listen은 현재 session의 새 사건을 stdout으로 내보내며 session마다 하나만 실행한다. 종료할 때 SIGINT·SIGTERM을 사용한다. 내 약속과 내 current demand에 대한 알림도 받는다. 자신의 session이 만든 요청은 알리지 않는다.

계약 브랜치끼리 충돌하면(`contract branches conflict`) `contract pull`의 hint에 따라 계약 브랜치에서 Git으로 풀고, 코드 브랜치로 돌아온다. 일반 코드 merge와 계약 브랜치 merge는 별개다. ait가 코드 커밋을 자동으로 원격에 보내지 않는다.

`status`의 `current ?unverified`·`?changed`·`?missing`·`?deleted`·`?nobase`(provider와 `fixtures` 행)와 `touch`를 확인한다. 형식 오류는 status가 경고하고 쓰기는 거절한다. `git config hides the trailers`는 사용자 Git 설정과 기록줄이 충돌한 것이므로 설정 원인을 확인하며, 스킬이나 설치기가 설정을 덮어쓰지 않는다.
