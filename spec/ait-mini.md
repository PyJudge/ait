# ait-mini 안내

ait-mini는 `ait-phase1-spec.md` v14의 부분집합이다. 쓰는 레코드, 기록줄(`Ait-Check` 등), 출력은 v14 그대로이므로, 완성판 ait가 mini의 이력을 그대로 이어받는다. 없는 명령이나 형식을 쓰면 `fatal: not supported in ait-mini: '…'`로 멈춘다(128).

## 있는 명령

| 명령 | 규격 |
| --- | --- |
| `ait init` | 7.1.1 (hook 다섯 개와 `.ait/init`. merge driver는 설치하지 않는다) |
| `ait session open --agent NAME` · `list` · `close [SID]` | 7.2.1–7.2.3 |
| `ait [future] demand A B DEFS [as N]` · `ait [future] offer B DEFS [as N]` | 7.3 |
| `ait [future] amend K [ARGS]` · `ait future cancel K` · `ait undertake D O` · `ait release D O` | 7.3 |
| `ait fulfill ID[=SCOPES]… -r FILE / --force -m MSG` | 7.7 |
| `ait commit [ID=SCOPES]… -r FILE / --force -m MSG` | 7.7a |
| `ait todo [ID…]` | 7.8 |
| 그 밖의 `ait <git 명령>` | git으로 넘긴다(7.14) |

커밋은 ait가 직접 만들지 않고 `git commit`을 불러서 만든다. ait는 판정한 tree를 임시 index에 담아 커밋하므로, 커밋이 막혀도 index와 작업 폴더는 그대로다. 커밋된 뒤에는 바뀐 경로 중 index가 아직 HEAD 판을 가진 것만 새 판으로 맞추고, `.ait/`는 작업 폴더도 맞춘다(5.5-4). 그 사이에 stage된 다른 변경은 건드리지 않고 경고한다. 규격 5.5의 CAS(판정한 HEAD 위에만 커밋한다)는 git의 `reference-transaction` hook으로 지킨다. ait는 판정한 HEAD와 tree와 브랜치를 `AIT_EXPECT`로 넘기고, hook은 git의 ref 잠금 안에서 들어오는 커밋이 그 브랜치 위의, 그 tree를 가진, 부모가 그 HEAD 하나뿐인 커밋인지 본다. 아니면 transaction을 취소하고 요청은 `E_HEAD_MOVED`로 끝난다. git 자신의 ref 잠금이 먼저 막은 경우(브랜치가 이미 움직였다)도 같다. 그래서 `ait init`은 hook을 규격의 네 개에 더해 `reference-transaction`까지 다섯 개 설치한다.

git은 2.48 이상이어야 한다. `reference-transaction` hook이 reflog만 바뀌는 transaction에도 불리던 문제가 2.48에서 고쳐졌기 때문이다(2.48 RelNotes). 그보다 낮으면 ait는 `fatal:`로 멈춘다.

## 없는 명령과 대신 할 것

| 없는 것 | 대신 |
| --- | --- |
| `ait contract pull RID` (받기) | `git fetch` 다음 `git merge <브랜치>`로 받는다. merge 커밋은 pre-merge-commit hook이 레코드를 검증하고, 틀리면 막는다. 충돌을 풀어 마치는 커밋(merge·cherry-pick·revert)은 pre-commit hook이 같은 검증을 한다 |
| `ait contract push` | `git push` |
| `ait peek` | `ait todo ID` (계약 하나를 볼 때), 또는 `.ait/ID`를 직접 읽는다 |
| `ait listen` · `sub` · `unsub` (알림) | 없다. `git fetch`로 받은 뒤 `ait todo`로 내 약속과 미확인인 내 요구를 본다 |
| `ait contract new` · `add` · `commit` (직접 작성) | 도우미 명령(`future amend` 등)으로 고친다 |
| `ait future drop` (current 계약 철회) | 없다. 되돌린 뒤 거둔다: `ait future amend K -m …`를 실행하고, 다음 요청으로 `ait future cancel K -m …`를 실행한다 |
| 행 범위 `PATH:S-E` | 파일 범위 `PATH`를 쓴다 |
| `.ait/` 병합 충돌 해결(merge driver) | git의 기본 병합을 쓴다. 충돌이 나면 한쪽 판을 골라 stage하고 merge를 마친다. hook이 결과를 검증한다 |
| 명세 정의의 `environment` | 쓰지 않는다 |

## 한계

- 통합 커밋(merge)에서는 레코드 형식만 검증한다. 출발 판에서 갈 수 있는 전이인지는 보지 않고, 확인 없이 바뀐 current를 되돌리지도 않는다(7.1.3).
- "내 약속"은 내가 만든 약속만 센다. 내가 되돌린 남의 계약은 넣지 않는다(1.3).
- hint는 5.8의 기본 형태와 5.9a의 고치는 명령(코드 먼저, 명세 파일 고치기, 약속으로 하기, 되돌리기)까지 준다. 전이 hint(5.9a의 1–12)는 일부만 준다.
- 시험은 `make test`(`t/`)다. `spec/ait-phase1-scenarios.md`에서 mini 범위에 드는 시나리오를 옮겼고, mini 때문에 바꾼 곳은 `# mini:` 주석으로 적었다.
