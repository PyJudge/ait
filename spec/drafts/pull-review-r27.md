# `pull-criteria.md` 개정 3: 4중 검수 결과 (r27)

검수자는 넷이다. 보고서는 scratchpad의 `r27-*/review.md`에 있다.

| 검수자 | 보는 쪽 |
| --- | --- |
| Claude A | 결정과 v14에 맞는지 (17건) |
| Claude B | 빠진 경우와 hint의 막다른 길 (14건, 더할 테스트 행 26개) |
| Codex A | git 사실 재현과 구현 가능성 (8건) |
| Codex B | 두 구현이 갈리는 곳 (5건) |

모두 git 2.53.0의 실제 저장소로 반례를 만들었다.

## 0. 한 줄 판정

- **맞은 것:** 표의 "git에서 확인한 것" 42칸은 넷 모두에서 실제와 맞았다(Codex A가 칸마다 대조). 결정 1·3·5·11과 고르기 뒤 다시 pull하면 멈추지 않는다는 것도 확인됐다.
- **깨진 곳:** 대부분 한 가지 원인에서 나왔다. 계약 파일과 그 고정 명세 파일을 **git merge의 결과에 맡긴 것**이다.
- **재현 스크립트 결함:** 해결 커밋이 명세 파일 충돌 표지를 커밋하는 결함이 있었다(Codex A M1). 이미 고쳐서 다시 돌렸다.

## 1. 내가 직접 다시 확인한 것

| 주장 | 결과 |
| --- | --- |
| `merge --no-commit` 뒤 `git commit`이면 pre-commit과 commit-msg가 돈다(v14 정리 hook이 pull의 결정을 다시 바꾼다) | 맞다. 일반 merge는 pre-merge-commit, commit-msg, post-merge가 돈다. fast-forward는 post-merge만 돈다. `commit-tree`는 hook이 없다 |
| octopus merge는 계약 파일의 merge driver를 부르지 않는다 | 맞다. 두 커밋 merge에서는 driver를 1회 부르고, octopus에서는 0회 부른다 |
| U2·G1·G2·G3 재현에서 명세 파일에 충돌 표지가 커밋됐다 | 맞다. `resolve()`를 고쳐 고정대로 되돌리고 다시 돌렸다. 묶음 무결성이 모두 맞다 |

## 2. 공통 원인과 고칠 것 (Claude가 정함, 사용자 확인 전)

이 절의 고칠 것은 사용자의 결정이 아니라 Claude가 정한 것이다. 모두 [pull-decisions.md](pull-decisions.md)의 "Claude가 허락 없이 정한 것"에 옮겨 적었다.

### 2.1 계약 파일과 고정 명세 파일은 pull이 판으로 정한다

받는 결과에서 `.ait/` 아래 계약 파일과, future·current 계약이 고정한 명세 파일은 모두 pull이 판의 데이터로 정한다. git merge의 결과는 그 밖의 파일(코드)에만 쓴다. 이 하나로 아래가 풀린다.

- 지정했지만 들일 것이 없는 계약에 상대가 git으로 되돌린 옛 판이나 지운 레코드가 들어오던 것(Claude B 1: RV2, DEL1). 옛 판은 내 판에 포함되므로 내 판을 둔다. 레코드 삭제는 받지 않는다(2.1.5).
- 옛 판이 된 계약을 쓸데없이 지정하라던 것(Claude A 11). 들어오는 계약은 "들이는 커밋이 가진 판 중 내 판에 포함되지 않는 판이 있는 계약"이다.
- 버린 쪽의 명세 파일 변경이 다른 pull에서 다시 섞여 들어와, 지정하지 않은 계약을 6.2로 다시 고정하던 것(Claude B 3: KS1).
- 끝난 판(cancelled·withdrawn)의 옛 명세 파일을 놓던 것(Claude B 11). 끝난 계약은 명세 파일을 맞추지 않는다(2.4.1).
- octopus가 driver를 부르지 않아 아무도 만들지 않은 레코드가 생기던 것(Claude B 5).
- 사용자의 merge 설정이 계약 판정을 바꾸던 것(Codex A H4). 코드의 merge는 git 설정을 따른다. git merge와 같다.
- 내 브랜치가 git으로 옛 판에 돌아가 최신 판의 들이는 커밋이 HEAD의 조상인 경우(Claude B 6: RV3). 최신 판을 HEAD 위의 커밋으로 적는다.
- 사고 검사(4단계)에 닿지 않던 것(Claude A 14). 사고 판은 후보에서 빼므로 4단계를 지운다.

### 2.2 pull의 커밋은 hook을 거치지 않게 만든다

- 깨끗한 경우 pull은 병합 커밋을 `commit-tree`로 직접 만든다. 다른 ait 쓰기 명령과 같다. 규격 11.2를 고친다.
- 코드 충돌로 멈출 때는 pull이 정한 계약 파일을 ait 로컬 상태에 적어 둔다. 마치는 커밋의 hook은 그 계약 파일을 다시 정규화하지 않고 그대로 둔다. 마치거나 abort하면 그 상태를 지운다(Claude A 2, Claude B 4, Codex A H6).
- 코드 충돌로 멈출 pull에는 `--force`를 받지 않는다(C3과 같은 까닭).

### 2.3 확인 검사는 "새 미확인"(9.1)으로 본다

- pull이 막는 것은 pull이 **새로 만드는** 미확인뿐이다. 9.1의 정의대로, 같은 고정으로 이미 미확인이던 부모가 있으면 새 미확인이 아니다.
- HEAD에 이미 있던 미확인과 보낸 쪽에서 이미 미확인이던 것은 막지 않는다. v14처럼 `warning: 'D' comes in unverified`와 H7을 쓴다(Claude A 1·5·6, Codex A H1).
  - 확인 기록이 아예 없는 current도 보낸 쪽에서 이미 미확인이다.
- 6.2가 current의 명세 고정을 바꾸는 것은 지금처럼 막는다(결정 6).
- provider를 찾을 수 없게 되면 `--force` 대신 H8의 꼴로 쓴다(Claude A 7).
- 되돌리기 hint는 들어오는 계약이든 내 계약이든 5.9a의 규칙(떠맡은 current offer까지)을 따른다(Claude B 12).
- 위반이 여럿이면 한 hint에 모든 절을 계약 ID 순으로 한 번씩 담는다(Codex B 3).

### 2.4 정의의 빈틈

- **본문이 같은 서로 다른 판 사이의 동률:** (HEAD에 없는 커밋 수, 커밋 OID) 순으로 정한다(Codex A H2, Codex B 2).
- **`K=C`가 유일한 최신 판을 가리킬 때:** 새 판을 만들지 않는다. `K=HEAD`인데 내 판이 유일한 최신 판이면 `already`다. 갈라지지 않은 계약에 고르기를 주면, 고르기를 뺀 `pull K…`를 hint로 준다(Codex B 4, Claude B 13).
- **3단계의 다시 보기:** 들이는 커밋이 가진 판을 후보에 더하면서, 더 늘지 않을 때까지 1~3을 되풀이한다. `K=C`의 검사는 그 뒤에 한다(Claude B 7, Codex B 1·5).
- **사고 판:** 2.3의 상태 짝만 본다. target이 current여야 한다거나 withdrawn에는 fulfilled X가 짝이어야 한다는 다른 레코드와의 제약은 받는 결과의 validate(5단계)가 본다. 떠맡기 기록은 role과 creator가 없는 꼴이다(Codex B 1).
- **떠맡기 기록:** 지정한 계약이 끝인 떠맡기 기록을 모두 본다. 내 판에 없는 것이 있으면 다른 끝도 지정해야 한다(결정 7. Claude A 10). K7의 기대가 `already D`에서 "멈추고 `pull D O`"로 바뀐다.
- **도우미 절:** pull이 정한 판 위에 적용하고, 새 판의 `prev`는 그 판이다. 2.2.4에 pull을 더한다(Claude A 8).
- **shallow clone:** 들이는 커밋이나 merge base가 shallow 경계 밖이면 멈춘다(Codex A H5). partial clone에서는 git이 객체를 받아 오는 대로 읽고, 받지 못하면 멈춘다(Codex A M2).
- **"아무것도 바꾸지 않는다"의 범위:** ref, HEAD, index, 작업 폴더, ait 상태다. 객체 저장소에 객체가 느는 것은 예외다(Codex A M2).
- **작업 폴더 검사:** pull이 쓸 경로(git이 쓰는 경로와 pull이 정하는 파일)에 index나 작업 폴더의 변경이 있으면 아무것도 바꾸기 전에 멈춘다. 추적하지 않는 파일도 같다(Claude B 10).
- **post-merge hook:** merge와 fast-forward에서는 post-commit이 돌지 않으므로 post-merge도 더한다(Claude A 9).
- **테스트 데이터:** K1의 a 끝이 5.9 d를 어긴다. 이를 고치고, Claude B의 행 26개 중 맞는 것과 NT2·NT3을 더한다.
- **5절에 빠진 조항을 채운다**(Claude A 12, 13):
  - 규칙 9 표, 5.14, 5.8(새 오류의 종류와 첫 행), 5.1(session), 5.10, 2.2.4, 10.1~10.3
  - `Ait-Resolves`를 쓰는 곳 전부
  - `ait-mini.md`: git merge를 가르친다. 결정 9와 어긋난다
  - `ait-concept.md`, SKILL, 시나리오에서 미확인과 사고가 섞인 곳
- **결정 없이 정한 것을 알린다**(Claude A 16): C3에서 `--force`를 주지 않는다, 본문이 같은 판의 동률 규칙, `prev`가 없으면 사고로 보지 않는다, 본문을 바이트로 비교해 고정만 다른 판도 갈라짐으로 본다.

## 3. 사용자가 정할 것

1. **끝난 판과 갈라졌을 때** (Claude A 3, Claude B 2). U7에서 철회된 D보다 내 future D를 고르면, 양쪽이 서로 받지 못하고 hint도 없는 막다른 길이 된다.
2. **들이는 커밋이 여럿일 때** (Codex A H3, Claude B 5·8). octopus는 driver를 부르지 않고, 순서를 정해 하나씩 합쳐도 코드 결과가 순서에 따라 달라질 수 있다.
3. **고르지 않은 쪽에게 알리기** (Claude A 15, Claude B 9). 결정 4가 약속한 알림을 9.3의 받는 쪽 규칙이 보장하지 않는다.
4. **명세 고정이 확인 없이 바뀐 current를 무엇으로 부를지** (Claude A 5·13). 사용자 용어로는 사고다. 그런데 그것을 알려면 확인 기록(기록줄)을 봐야 한다.
5. **결정 12의 범위** (Claude A 4, Claude B 9, Codex B 6). (계약, 판)으로 세면 미확인 알림이 사라진다. 계약 파일의 사건에만 그렇게 세고 미확인은 지금처럼 셀지 확인이 필요하다.
