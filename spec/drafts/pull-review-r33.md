# `pull-criteria.md` 개정 5: 4중 검수 결과 (r33)

| 검수자 | 보는 쪽 | 건수 |
| --- | --- | --- |
| Claude A | 결정 1~25와 v14에 맞는지, 기록 1~66이 정말 끌어낸 것인지 | 높음 2, 중간 3, 낮음 9 |
| Claude B | 빠진 경우, 막다른 길, 알리지 않고 버려지는 것 | 높음 3, 중간 8, 낮음 5, 테스트 행 18 |
| Codex A | 표 93행의 git 사실, plumbing으로 되는지 | 높음 2, 중간 3, 낮음 1 |
| Codex B | 두 구현이 갈리는 곳 | 높음 2, 중간 3 |

보고서는 scratchpad의 `r33-*/review.md`에 있다. 모두 git 2.53.0의 실제 저장소로 만들었다.

## 0. 한 줄 판정

- **맞은 것**
  - 표 93행 중 92행의 git 사실이 실제와 맞았다(Codex A). 틀린 한 행은 MH3이다.
  - 결정 20의 merge 상태(중간 커밋 없음, 받을 끝 여럿, 마지막 끝만 충돌)는 plumbing으로 만들 수 있다. 커밋하지 않은 다른 파일도 남는다(Codex A, Claude B).
  - 차례로 서로 받으면 조건, 끝난 판, 확인 행의 경우가 모두 한 바퀴에 모인다(Claude B `sim.log`).
- **사용자 원칙 (2026-10-10):** "git과 싸우지 않는다. 고치는 길이 있고 경고로 드러나고 원칙이 분명하면 된다." 그래서 git이 하는 일(abort, merge 설정, abort 뒤 남은 상태 알아보기)에 대한 지적은 장치를 더하지 않고, 약속을 빼는 쪽으로 정리했다.

## 1. 내가 직접 다시 확인한 것

스크립트와 출력은 scratchpad의 `r33-verify/`에 있다.

| 주장 | 결과 |
| --- | --- |
| 사고 판 위에 판을 하나 더 쌓으면 그 판은 사고가 아니다(Codex B F5, Claude A F1, Claude B B3) | 맞다. 3-way가 없는 저장소에서도(main이 cancel 판만 가짐) 그 판을 그대로 받아 cancel이 되살아난다(`f5.py`, `more.py` F5c) |
| MH3의 마치는 커밋에서 시작 HEAD가 빠진다(Codex A H1, Claude B B5) | 맞다. MERGE_MODE에 `no-ff`를 쓰면 부모가 셋이다 |
| 기존 판을 그대로 두면 같은 PINS의 `reported`가 `forced`에 덮인다(Codex B F1) | 맞다(`more.py` F1) |
| `branch.main.mergeOptions '-s ours'`를 git merge는 따르고 merge-tree는 따르지 않는다(Codex A M2) | 맞다 |
| 깨끗이 합쳐진 파일을 손댄 뒤 `git merge --abort`는 실패한다(Codex A M3) | 맞다(git의 동작) |
| MERGE_MSG의 주석 행으로 pull 상태를 알아보는 방법 | `git commit --no-edit`에서는 주석 행이 메시지에 남는다. 그래서 쓰지 않는다. 이 장치 자체를 뺀다(2.1) |
| 함께만 받을 수 있는 두 끝이 둘 다 내 코드와 충돌하면 막다른 길이다(Claude B B1) | 맞다. 한쪽이 다른 쪽을 받아도(aM) 들이는 커밋은 그대로 a1, b1이다. 두 끝의 충돌이 다른 파일이면 한 merge 상태에 함께 둘 수 있다(`b1state.sh`: UU 둘, 마치면 부모 셋) |
| PV1에서 cancelled 결과가 놓지 않는 `specs/d.yaml`이 git에서 충돌한다(Claude B B4) | 맞다 |

## 2. 고칠 것 (Claude가 결정과 원칙에서 끌어냄)

pull-decisions.md의 기록 67~85에 옮겨 적었다.

### 2.1 git과 싸우는 장치를 뺀다 (사용자 원칙)
- 코드 충돌로 멈춘 pull의 로컬 상태를 뺀다(기록 29의 일부, 60). 마치는 커밋의 hook은 데이터만 본다. 계약 파일이 사고 판이 아니고 출발 판을 모두 포함하면 그대로 둔다. pull이 정했는지 사람이 정했는지는 묻지 않는다. Codex A H2, Claude B B6이 이것으로 없어진다.
- "abort하면 pull 전 그대로"라는 약속을 뺀다. abort는 `git merge --abort`가 하는 대로다(Codex A M3).
- "git merge와 같이 설정을 따른다"는 약속을 뺀다. 코드는 git(merge-tree)이 합친다(Codex A M2).
- merge 상태는 `git merge --no-ff`가 만드는 꼴로 쓴다(MERGE_MODE `no-ff`). 그러면 마치는 커밋의 부모가 HEAD와 받은 끝 모두다(Codex A H1, Claude B B5).

### 2.2 사고
- **사고 판 위에 쌓은 판도 사고다.** "갈 수 있다"는 사고 판이 아닌 `prev`에서 갈 수 있다는 뜻이다. 결정 8의 "그 브랜치는 고칠 때까지 막는다" 그대로다. 고치는 길(5.10)은 사고 판이 아닌 판을 `prev`로 삼으므로 풀린다(Codex B F5, Claude A F1, Claude B B3).
  - 다른 제안 둘은 쓰지 않았다. "3-way에서 끝난 판을 빼지 않는다"(Codex B)는 3-way가 없는 경우를 막지 못한다. "포함은 사고 판에서 끝난다"(Claude B)는 사고 판 위에 쌓은 그 브랜치를 막지 못한다.
- **들이는 쪽에서 계약이 고정한 명세 파일을 그 고정과 다르게 바꿨으면 사고다.** 받지 않고 경고한다. 사고 판을 건너뛰는 것(기록 4)과 같다. 상대의 코드 변경으로 받는 것(CP2)은 들이는 쪽에서 그 경로를 선언한 계약이 없을 때뿐이다(Claude A F2, Codex B F2).
- 확인 행이 없는 current는 형식 오류가 아니라 사고다. 형식은 "current D·O에 하나까지"다(Claude A F8).

### 2.3 파일을 누가 정하나
- 받는 결과에서 어느 계약도 고정하지 않는 파일은 코드다. 끝난 결과의 옛 명세 파일이 그렇다. git의 결과를 쓰고, 충돌하면 코드 충돌이다. 그래서 PV1은 `specs/d.yaml`의 코드 충돌로 git처럼 멈춘다(Claude B B4. TF2와 결정 3에서 나온다).
- 고정한 명세 파일의 git 병합이 충돌하고 상대 쪽에서는 그 파일이 코드였으면, 코드 충돌이다. 그 파일을 고정한 계약이 current면 5에서 멈춘다(C3과 같다)(Claude B B2).
- `.ait/` 아래의 모든 경로는 pull이 정한다. 계약 파일이 아닌 경로와, 지정하지 않은 계약의 형식이 틀린 판·사고 판은 받지 않고 경고한다(Claude B B11. DEL1과 같다).
- 받은 뒤에는 pull이 쓰는 경로(코드 포함)를 index와 작업 폴더에 쓴다. git merge와 같다(Claude A F4).

### 2.4 새 판과 기록
- 새 판 위에 얹는 판(도우미 절, `--force`, 6.2 다시 고정)의 `prev`는 그 새 판과 그 새 판의 `prev` 모두다. 같은 고르기만 한 다른 에이전트와도 모이고, 새 판의 blob이 없는 clone에서도 모인다(Claude B B7. 기록 51을 고친다).
- 기존 판을 그대로 두는 것은 current D·O이면 확인 행도 결정 22로 정한 것과 같을 때뿐이다(Codex B F1).
- pull이 만드는 커밋에서 어느 계약 파일이 모든 부모의 판과 다르면 요청 커밋이다. RV3도 여기에 든다(Claude A F3. v14 7.1.5와 같다). RV3에서 D가 current면 5에서 멈추고 문구를 정한다(Claude B B15).
- `comes in unverified`는 v14 7.12.1-7의 조건일 때만 쓴다(Claude A F6).
- 문장 맞추기: 기록 47·50의 글을 기준에 맞춘다(Claude A F9, Codex B F3). 4절과 2절 "그래서"의 문장을 좁힌다(Claude A F10, Claude B B14·B16).

### 2.5 hint는 따를 수 있어야 한다
- 3단계는 이름도 더 늘지 않을 때까지 되풀이하고, hint는 한 번에 준다(Claude B B8).
- 따를 수 없는 범위 옮기기 hint(H8 꼴)는 주지 않는다(Claude B B9).
- 후보가 사고 판이나 형식이 틀린 판뿐이면 `E_ID`가 아니라 ACC1 꼴의 오류다(Claude B B10).
- 계약 경로의 작업 폴더 변경은 `ait contract add`와 `ait contract commit`으로 커밋하라고 쓴다(Claude B B13).
- 나눠 받는 hint는 "원래 이름을 모두 담는다"의 예외다(Codex B F4).
- 코드 충돌로 멈출 때 마치는 커밋 hint를 쓴다(Claude A F12).

### 2.6 그 밖
- 5절에 v14의 빠진 조항 14곳을 더한다(Claude A 4절).
- 기록의 꼬리표(10, 17, 65)와 용어("되돌리다"를 reopen이 아닌 뜻으로 쓴 행, "약속")를 고친다(Claude A F13·F14).
- 재현 스크립트: 형식 검사를 바이트로 정규형까지 본다(Codex A M1). PINS는 저장소의 `git hash-object`이고, 스크립트는 SHA-1 저장소만 다룬다고 적는다(Codex A L1).

## 3. 사용자가 정할 것

1. **함께만 받을 수 있는 두 끝이 둘 다 내 코드와 충돌할 때**(Claude B B1). 지금은 막다른 길이다. → **결정 26**(가): 코드 충돌은 파일마다 한 번까지 한 merge 상태에 함께 둔다.
2. **pull로 `D=HEAD`를 골라 남의 이행(current)을 버렸을 때, 고른 쪽이 "되돌린 쪽"이 되는가**(Claude A F5). → **결정 27**(가): 된다. git merge와 같다.
