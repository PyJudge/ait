# `ait contract pull` 기준 (기대 테스트, 개정 3)

로직보다 먼저 **무엇이 맞는 결과인지**를 정한다. 근거는 사용자와 정한 [pull-decisions.md](pull-decisions.md)의 결정 1~12다.

판정에 쓰는 것은 저장소의 데이터뿐이다. 계약 파일의 판과 그 `prev`, 커밋, ref가 그것이다. 어떤 git 조작으로 커밋이 생겼는지, 기록줄이 있는지는 판정에 쓰지 않는다(결정 8).

3절의 "git에서 확인한 것"은 모두 실제 저장소(git 2.53.0)에서 확인했다.
- 재현 스크립트: [pull-criteria-runs/scen.py](pull-criteria-runs/scen.py)
- 출력: [all.log](pull-criteria-runs/all.log)
- 계약 파일의 merge는 v14 merge driver(7.13.1)의 1·2·3·5단계를 그대로 옮긴 [driver.py](pull-criteria-runs/driver.py)가 한다.
- 스크립트는 git의 사실과 아래 낱말의 데이터(포함, 최신 판, 들이는 커밋, 사고 판)만 출력한다. ait의 판정은 이 문서의 규칙으로 손으로 정했다.
- hint를 그대로 따른 결과가 v14의 구조 검사(5.9 b~e)를 지나고 새 미확인이 없는지는 [flows.py](pull-criteria-runs/flows.py)로 확인했다. 결과는 [flows.log](pull-criteria-runs/flows.log)에 있다(K6, N5, U7, SF1, S1, C2, S3).

## 1. 낱말

- **계약 파일**: `.ait/<ID>`(계약)와 `.ait/undertake/<D>-<O>`(떠맡기 기록).
- **판**: 계약 파일의 내용 하나. blob OID로 가리킨다.
- **본문**: 판에서 `prev` 행을 뺀 것. 같은지는 바이트로 본다. 고정(provider와 `file` 행의 BLOB)도 본문이다.
- **포함**: 판 a가 판 b를 포함한다는 것은, b가 a이거나 a의 `prev`를 따라가 b에 닿는다는 뜻이다. 따라가다 객체 저장소에 없는 blob을 만나면 그 가지는 거기서 끝난다.
- **후보 판**: 다음 판들이다.
  - ref 목록(5.11)의 각 ref 끝 커밋이 그 계약 파일로 가진 판
  - HEAD의 판(**내 판**)
  - 이번 pull이 들이는 커밋이 그 계약 파일로 가진 판(E1)

  그 밖의 과거 커밋의 판은 후보가 아니다. 브랜치가 옛 판으로 돌아갔다면 그 브랜치는 옛 상태에 있을 뿐이기 때문이다(결정 8 규칙 2).
- **사고 판**: `prev`에서 ait 규칙으로 갈 수 없는 판이다(결정 8 규칙 1). 판정은 다음과 같다.
  - `prev` 중 하나라도 다음을 모두 만족하면 사고 판이 아니다.
    - 역할과 creator가 같다.
    - 본문이 같거나, 2.3의 상태 전이로 갈 수 있다.
  - 처음 판(`prev -`)은 2.3의 "없음 →" 전이로 판정한다.
  - `prev`의 blob이 없어 판정할 수 없으면 사고 판으로 보지 않는다.
  - 확인 기록이 있는지는 여기서 보지 않는다. 그것은 미확인(1.5a)이 다룬다.
- **최신 판**: 사고 판이 아닌 후보 판 중, 다른 후보 판에 포함되지 않는 판.
- **들이는 커밋**(판 v의): ref 목록에서 닿는 커밋 중, 그 계약 파일이 v이고 어느 부모에서도 v가 아닌 커밋이다.
  - 여럿이면(사본) HEAD에 없는 커밋이 가장 적은 것을 고른다. 그 수도 같으면 OID가 작은 것이다(결정 11).
  - v가 내 판이면 들이는 커밋은 없다.
- **합친 결과**: HEAD와 들이는 커밋들을 git 방식으로 합친 결과다. 계약 파일은 merge driver(7.13.1)가 합친다.
- **들어오는 계약**: 합친 결과에서 계약 파일이 HEAD와 다른 계약. 떠맡기 기록 `<D>-<O>`가 다르면 D와 O다(결정 7).
- **받는 결과**: 합친 결과 위에 pull이 정한 것을 놓은 상태다. pull이 정하는 것은 셋이다.
  - 지정한 계약의 판과, 그 판이 고정한 명세 파일
  - 도우미 절
  - 6.2의 다시 고정: 명세 파일이 바뀌었으면, 그 파일을 선언한 계약을 모두 다시 고정한다. 관문이 언제나 하는 일이다.

## 2. 판정

```
ait contract pull K[=C]… [도우미 절…] [--force] [-m MSG]
```

- K는 계약 ID다. C는 고를 판을 가진 커밋이거나 `HEAD`다.
- 떠맡기 기록은 `<D>-<O>=C`로 고른다.
- 도우미 절은 `future amend K`처럼 다른 도우미 명령과 같은 꼴이다(결정 10).
- `-m`은 도우미 절이나 `--force`가 있을 때 필요하다.

**시작 조건**은 v14 5.2와 같다. `.ait/`가 깨끗하고, 진행 중인 git 조작이 없고, 브랜치 위에 있어야 한다. HEAD에 이미 있는 오류(5.10)로 막히면 그 hint를 준다.

아래 1~6에서 멈추면 아무것도 바꾸지 않는다.

**1. 판 정하기.** 지정한 계약마다 최신 판을 구한다. 지정한 계약에 D와 O가 함께 있으면 떠맡기 기록 `<D>-<O>`도 같은 방법으로 구한다(K7).
   - 최신 판이 모두 내 판에 포함되면, 그 계약에서는 들일 것이 없다.
   - 최신 판이 하나면 그 판이 결과다. 새 판을 만들지 않고, 그 판의 들이는 커밋을 들인다.
   - 최신 판이 둘 이상인데 본문이 하나면, 결과는 그 본문에 `prev` = 그 판들 모두인 새 판이다.
     - 들이는 커밋은 그 판들 중 들어올 커밋이 가장 적은 것 하나다.
     - 내 판이 그중에 있으면 아무것도 들이지 않는다(G2, G4b).
   - **갈라짐:** 최신 판의 본문이 둘 이상인데 고르지 않았으면 멈춘다. hint는 원래 지정한 이름과 이미 고른 것을 모두 둔 한 명령이다. 갈라진 계약이 여럿이면, 모두 같은 커밋을 고르는 명령들을 먼저 준다(SF1).
     ```
     error: 'D' has 2 latest versions: 'a1' (a), 'HEAD'
     hint: pick one: 'ait contract pull D=a1' or 'ait contract pull D=HEAD'
     ```
   - `K=C`의 C는 그 계약 파일이 최신 판 중 하나인 커밋이어야 한다. 아니면 멈추고 지금의 고르기 hint를 다시 준다(KB1).
     - 결과는 고른 판의 본문이고, `prev`는 최신 판 모두다.
     - C가 HEAD가 아니면 고른 판의 들이는 커밋을 들인다.
   - 사고 판은 후보에서 빼고, 이렇게 쓴다: `warning: skipping 'D' at 'a' (a2): it cannot come from its prev by ait's rules`.
   - 지정한 계약에서 모두 들일 것이 없고 새로 적을 것도 없으면 `already K…`를 쓰고 끝난다.
   - 지정한 계약의 후보 판이 하나도 없으면(사고 판을 빼고) `E_ID`다.

**2. 들이는 커밋 모으기.** 다른 들이는 커밋의 조상이거나 HEAD의 조상인 것은 뺀다.
   - 둘 이상이 남았는데, pull이 정하는 파일 밖에서 git 방식으로 합치다 충돌하면 멈춘다(MH2).
   - pull이 정하는 파일은 지정한 계약의 계약 파일과, 그 판이 고정한 명세 파일이다.
   - hint는 갈라진 계약 모두가 같은 커밋을 고르는 명령(그런 커밋이 있으면)과, 하나씩 받는 명령이다.

**3. 지정 검사.** 들어오는 계약은 모두 지정되어 있어야 한다. 아니면 멈춘다.
   ```
   error: taking 'a2' also changes contracts you did not name: 'Q'
   hint: name them too: 'ait contract pull D Q'
   ```
   - 들이는 커밋이 지정한 계약의 판을 가져오면, 그 판도 후보 판이 되므로 1을 다시 본다.
     - 그 판이 내 판을 포함하면 그 판이 결과다(E1).
     - 갈라졌으면 1의 고르기로 멈추고, 그 들이는 커밋을 선택지로 준다.

**4. 사고 검사.** 받는 결과에서 HEAD와 다른 판이 사고 판이면 멈춘다.
   ```
   error: 'a2' brings 'Q' changed outside ait's rules
   hint: it has to be fixed on that branch first
   ```

**5. 구조 검사.** 받는 결과는 validate(5.9 a~e)를 지나야 한다. 아니면 멈추고, hint는 그 위반을 푸는 한 명령이다.
   - 다른 계약을 지정하면 풀리는 경우: 그 계약을 지정한 명령(U7: `pull D=a2 X`).
   - 판을 맞춰 고르면 풀리는 경우: 맞는 선택들(SF1: `pull D=a1 Q=a1` 또는 `pull D=b1 Q=b1`).
   - 계약을 되돌리면 풀리는 경우: 그 절을 더한 명령(N5: `pull D future amend OE -m <why>`). 되돌리기는 누구의 계약이든 할 수 있다(1.5d).

**6. 확인 검사.** 받는 결과의 current 계약은 모두 확인된 그대로여야 한다(규칙 4). 다음 둘 중 하나에 걸리면 멈춘다. 그래서 ait 명령인 pull은 미확인을 만들지 않는다.
   - 그 계약의 provider 코드가 받는 결과에서 그 판의 고정과 다르다.
   - 6.2의 다시 고정으로 그 계약의 고정이 바뀌었다. 명세 파일이 확인 없이 바뀐 것이다.

   멈출 때의 hint는 그 계약이 어디서 왔느냐에 따라 다르다.
   - **내 브랜치(HEAD)에 있던 계약 K(결정 6).** 들어오는 계약이 아니다(S2, S3). K를 누가 만들었는지는 상관없다.
     ```
     error: taking 'a1' changes what contract 'K6' checked: 'src/stock.c'
     hint: reopen it in the same request: 'ait contract pull D future amend K6 -m <why>'
     hint: or, if the change is trivial for 'K6': 'ait contract pull D --force -m <why>'
     ```
     K를 내 current offer O가 떠맡았으면 첫 hint의 절은 `future amend O future amend K6`이다(S4, 5.9 d).
   - **들어오는 계약 D(결정 10).** 내 변경이 합쳐져 D가 확인한 것과 달라졌다(C2).
     ```
     error: taking 'a1' merges your changes into what 'D' checked: 'src/stock.c'
     hint: take it reopened: 'ait contract pull D future amend D -m <why>'
     hint: or, if your changes are trivial for 'D': 'ait contract pull D --force -m <why>'
     ```
   - **코드 충돌이 확인한 파일에 날 때.** git 방식으로 합칠 때 코드가 충돌하는 파일을 받는 결과의 current 계약이 확인하고 있으면 멈춘다(C3).
     - 충돌을 해결한 내용은 아직 아무도 확인하지 않았기 때문이다.
     - hint는 그 계약을 되돌리는 절을 더한 명령이다.
     - `--force`는 주지 않는다. 사소하다고 판단할 내용이 아직 없기 때문이다.
   - **`--force`일 때.** 위 두 경우에 걸린 계약마다 받는 결과로 다시 고정하고 `Ait-Check: K forced PINS`를 남긴다(7.10.1-4와 같다).
   - **명세 파일을 공유한 future 계약.** future는 확인한 것이 없으므로 멈추지 않는다. 6.2대로 다시 고정된 채 받는다(S1).

**7. 받기.**
   - 들일 커밋이 하나이고, HEAD가 그 조상이고, 새로 적을 판이 없으면 fast-forward한다.
   - 그 밖에는 커밋 하나를 만든다.
     - 들일 커밋이 있으면, 그것들을 부모로 둔 병합 커밋이다.
     - 들일 커밋이 없으면 HEAD 위의 커밋이다. `K=HEAD`이거나, 본문이 같은 내 판을 고른 경우다.
     - 새로 적을 판이 있으면 요청 커밋(기록줄)이다. 새로 적을 판은 고른 결과의 `prev`, 도우미 절, `--force`의 다시 고정, 6.2의 다시 고정에서 생긴다.
   - **코드만 충돌하면**(6의 "코드 충돌이 확인한 파일에 날 때"에 걸리지 않으면) git처럼 그 자리에서 멈춘다(C4).
     - 계약 파일과 고정한 명세 파일은 pull이 정한 대로 index와 작업 폴더에 둔다.
     - 받는 쪽이 코드를 해결하고 마치는 커밋으로 끝낸다.
   - **출력:** 들인 계약마다 `included K from 'C'`. 들인 것 없이 내 판으로 새 판만 적었으면(`K=HEAD`, G4b) `kept K`.

그래서:
- 고르기로 멈추는 것은 계약이 실제로 갈라진 때(1의 갈라짐)뿐이다. 사본, 이미 합쳐진 판, 본문이 같은 판으로는 멈추지 않는다.
- 보지 않은 계약은 들어오지 않는다(3). 코드는 지정 없이 언제나 함께 들어온다(결정 3).
- pull은 미확인도, 사고 판도 만들지 않는다(4, 6).

## 3. 기대 테스트

**표기**
- 계약 이름은 줄였다(D, Q, O, X, K5 등).
- `D@x`는 x가 만든 D의 판이다. `init`의 D는 future이고 조건 c0이다.
- 받는 쪽(나)은 main이다. a, b, f는 다른 에이전트의 브랜치다.
- "git에서 확인한 것"은 [all.log](pull-criteria-runs/all.log)의 같은 이름 항목이다.

### 3.1 기본

| # | 이력 | git에서 확인한 것 | 기대 |
| --- | --- | --- | --- |
| A1 | a가 새 계약 Q를 만들었다(a1). main에는 Q가 없다 | Q의 후보는 a의 판 하나다. 들이는 커밋은 a1(새 커밋 1개). 합치면 `.ait/Q`와 `specs/q.yaml`만 바뀐다 | `pull Q`는 fast-forward. `included Q from 'a1'` |
| A2 | A1 뒤에 다시 `pull Q` | 최신 판이 내 판에 포함된다 | `already Q` |
| A3 | a1이 D와 Q를 바꿨고, b1이 Q를 다르게 바꿨다. `pull D Q` | D의 최신 판은 a1의 것 하나, Q의 최신 판은 a1과 b1의 것 둘이다 | 멈춘다. 지정한 계약은 함께 받거나 하나도 받지 않는다. hint는 `pull D Q=a1` 또는 `pull D Q=b1` |

### 3.2 피할 수 없는 결정 (2절 1의 갈라짐)

| # | 이력 | git에서 확인한 것 | 기대 |
| --- | --- | --- | --- |
| U1 | a1: D의 조건을 c-A로. main: D의 조건을 c-E로 | 최신 판 2개, 본문 2개. a1과 합치면 `.ait/D`와 `specs/d.yaml`이 충돌한다 | 멈춘다. hint는 `D=a1` 또는 `D=HEAD`. `D=a1`을 고르면 a1을 들이고, `.ait/D`는 D@A의 본문에 `prev` {D@A, D@E}다. 명세 파일도 D@A가 고정한 것이다. 그 뒤 다시 pull하면 최신 판이 내 판 하나다 |
| U1b | U1에서 `D=HEAD` | a의 커밋은 main에 들어오지 않는다. 다시 pull하면 최신 판이 내 판 하나다 | `kept D`. HEAD 위에 커밋 하나가 생긴다(D@E의 본문, `prev` {D@E, D@A}). D@A 쪽에 알림이 간다(5절 9.2) |
| U2 | a: D를 c-A로(D@A). b: D를 c-B로 고친 뒤 a를 merge했고, 충돌한 D를 c-B로 정했다(D@B', `prev` {D@A, D@B}). f: 같은 방법으로 c-F로 정했다(D@F'). main: D0 | 최신 판은 D@B'와 D@F'. 본문 2개 | 멈춘다. `D=bM` 또는 `D=fM` |
| U3 | a: D를 이행했다(current, 고정 바뀜). main: D의 조건을 c-E로 | 최신 판 2개 | 멈춘다 |
| U4 | a: D를 cancel했다. main: D의 조건을 c-E로 | 최신 판 2개 | 멈춘다 |
| U6 | D는 current다. a와 b가 각자 코드를 바꾸고 다시 확인했다(조건은 같음) | 최신 판 2개. 고정만 다르다 | 멈춘다. 어느 구현을 받을지 고른다 |
| U7 | D는 current다. a1: drop X를 만들었다. a2: X를 이행했다(D withdrawn). main: D를 되돌렸다(future) | 최신 판 2개. a2와 합치면 `.ait/X`도 바뀐다 | `pull D`는 멈추고 고르게 한다. `D=a2`를 고르면 3에서 멈추고 hint는 `pull D=a2 X`. 그대로 하면 통과하고, D withdrawn과 X fulfilled는 validate를 지난다 |
| U8 | a1: D@A. main이 a1을 받았다. a가 a1을 amend했다(D@A2, `prev` D0) | 최신 판은 내 D@A와 D@A2다. 본문 2개 | 멈춘다. 같은 요청에서 생긴 판이라도 갈라졌다 |

### 3.3 어느 커밋을 들이나

| # | 이력 | git에서 확인한 것 | 기대 |
| --- | --- | --- | --- |
| G1 | main은 b의 D@B(c-B)를 받아 두었다. a: D를 c-A로(D@A). b가 a를 merge했고, 충돌한 D를 c-B로 정했다(D@B', `prev` {D@A, D@B}, 병합 커밋 bM) | a를 `git merge`하면 `.ait/D`와 `specs/d.yaml`이 충돌한다. 최신 판은 D@B' 하나이고, 들이는 커밋은 bM(새 커밋 2개). bM과는 충돌 없이 합쳐진다 | `pull D`는 bM을 들인다. 멈추지 않는다 |
| G2 | a: D를 c-A로(D@A). b: D를 c-B로 고친 뒤 a를 merge했고, 충돌한 D를 c-A로 정했다(D@B'). f: D를 c-F로 고친 뒤 같은 방법으로 c-A로 정했다(D@F'). main: D0 | b를 merge한 뒤 f를 merge하면 충돌한다. 최신 판은 D@B'와 D@F'로 본문이 같다. 들이는 커밋은 bM(새 커밋 4개)과 fM(3개) | 멈추지 않는다. fM을 들이고 `.ait/D`는 그 본문에 `prev` {D@B', D@F'}다. 다시 pull하면 최신 판이 내 판 하나다 |
| G3 | main은 a의 D@A를 받아 두었다. b: D를 c-B로 고친 뒤(b1) a를 merge했고, 충돌한 D를 c-B로 정했다(D@B', 병합 커밋 bM) | b1(D@B)과 merge하면 충돌하고, bM과는 충돌 없이 합쳐진다. 최신 판은 D@B' 하나다 | bM을 들인다 |
| G4 | a와 main이 각자 같은 조건 c-A로 D를 바꿨다 | 같은 blob이다(본문과 `prev`가 같다) | `already D` |
| G4b | a: c-x를 거쳐 c-A로 바꿨다. main: D0에서 바로 c-A로 바꿨다 | 최신 판 2개, 본문 1개 | 멈추지 않는다. 내 판이 최신 판 중에 있으므로 들이지 않는다. HEAD 위에 커밋 하나를 만든다: 같은 본문에 `prev` {두 판} |
| CP1 | a1: D@A. b: 코드 커밋 b0 위에 a1을 cherry-pick했다(b1, 같은 blob) | 들이는 커밋은 b1(새 커밋 2개)과 a1(1개) | a1을 들인다(결정 11) |
| M1 | a: D@B, 그 위에 D@B2(`prev` D@B). 그 뒤 a의 이력을 다시 써서 D@B를 가진 커밋이 없다. main: D0 | 같은 저장소에는 D@B의 blob이 남아 있어서, D@B2가 D0을 포함한다. 새 clone에는 그 blob이 없어서 사슬이 끊긴다 | 같은 저장소에서는 D@B2를 들인다. 새 clone에서는 D0과 D@B2가 갈라진 것으로 보고 멈춘다. 틀려도 멈추는 쪽으로만 틀린다 |
| KB1 | G1에서 `pull D=a1` | a1의 판은 D@B'에 포함된다(최신 판이 아니다) | 멈추고 지금의 고르기 hint를 다시 준다 |

### 3.4 들어오는 계약은 모두 지정한다 (2절 3)

| # | 이력 | git에서 확인한 것 | 기대 |
| --- | --- | --- | --- |
| N1 | a1: Q를 바꿨다. a2: D를 바꿨다. `pull D` | 들이는 커밋 a2와 합치면 `.ait/Q`도 바뀐다 | 멈춘다. hint는 `pull D Q` |
| W1 | a0: 코드만 바꿨다(`src/util.c`). a1: D를 바꿨다 | 합치면 `src/util.c`도 들어온다. 바뀌는 계약은 D뿐이다 | 통과한다. 코드는 지정 없이 들어온다(결정 3) |
| W2 | b: offer O2를 만들고 이행했다. 그 뒤 D@B | 합치면 `.ait/O2`도 바뀐다 | 멈춘다. hint는 `pull D O2` |
| K1 | a1: offer O를 만들고 D를 떠맡았다. a2: O를 이행했다. `pull O` | 합치면 `.ait/undertake/D-O`도 바뀐다 | 멈춘다. hint는 `pull D O`(결정 7) |
| K6 | O는 main의 offer다(init에서 future). a1: D를 O로 떠맡았다. a2: D를 고쳤다. main: O를 이행했다(current). `pull D` | 합치면 `.ait/D`와 `.ait/undertake/D-O`가 바뀐다. 합친 상태는 O current, D future, 떠맡기 undertaken이다 | 멈춘다. hint는 `pull D O`. 그대로 하면 5에서 멈춘다(5.9 d). hint는 `pull D O future amend O -m <why>`. 그대로 하면 통과한다 |
| K7 | main은 D와 O를 가졌다. a1: 떠맡기만 했다 | D와 O의 최신 판은 내 판이다. 떠맡기 기록의 최신 판은 a1의 것 하나다 | `pull D O`는 a1을 들인다. `pull D`만 하면 떠맡기 기록을 보지 않으므로 `already D`다 |
| E1 | b1: D@B. b2: Q를 바꿨다. b3: b1을 `git revert`했다(b의 끝은 D0). `pull Q` | D의 최신 판은 내 D0이다. 그런데 Q의 들이는 커밋 b2와 합치면 D가 D@B가 된다 | 멈춘다. hint는 `pull D Q`. 그대로 하면 b2가 가진 D@B가 후보 판이 되고, D@B가 내 D0을 포함하므로 D는 D@B가 된다. b2 시점의 이력이 그렇기 때문이다 |

### 3.5 구조 검사 (2절 5)

| # | 이력 | git에서 확인한 것 | 기대 |
| --- | --- | --- | --- |
| N5 | D는 current다. main의 current offer OE가 D를 떠맡았다. a1: D를 되돌렸다. `pull D` | 합치면 `.ait/D`만 바뀐다(D future) | 멈춘다(5.9 d). hint는 `pull D future amend OE -m <why>`. 그대로 하면 통과한다 |
| SF1 | D와 Q가 `specs/shared.yaml`을 함께 쓴다. a1은 그 파일을 고치고 둘을 다시 고정했다. b1은 다르게 고치고 둘을 다시 고정했다. `pull D Q` | 두 계약 모두 최신 판이 2개다. a1과 b1을 함께 merge하면 `.ait/D`, `.ait/Q`, `specs/shared.yaml`이 충돌한다. a1만 합치면 충돌이 없다 | 멈춘다. hint는 `pull D=a1 Q=a1` 또는 `pull D=b1 Q=b1`(2절 1). 손으로 `D=a1 Q=b1`을 고르면 한 파일을 두 내용으로 고정하게 되어 5에서 멈추고, 같은 hint를 준다 |

### 3.6 확인 검사 (2절 6)

| # | 이력 | git에서 확인한 것 | 기대 |
| --- | --- | --- | --- |
| C1 | a1: D를 이행했다(stock.c의 1행 → A, 고정 gA, 확인 기록). main은 stock.c를 건드리지 않았다 | 합친 stock.c가 gA다. 확인 기록이 a1에 있다 | 통과한다. D는 current이고, 확인 기록이 이력째 들어오므로 미확인이 아니다 |
| C2 | C1과 같은데, main이 stock.c의 9행을 바꿨다 | 충돌 없이 합쳐지지만, 합친 stock.c가 gA가 아니다 | 멈춘다(결정 10). hint는 `pull D future amend D -m <why>` 또는 `pull D --force -m <why>` |
| C3 | C1과 같은데, main이 1행을 다르게 바꿨다 | `src/stock.c`가 충돌한다 | 멈춘다. hint는 `pull D future amend D -m <why>`. 그대로 하면 D는 future로 들어오고, 코드 충돌에서 git처럼 멈춘다 |
| C4 | a1: D(future)와 `src/other.c`를 바꿨다. main: `src/other.c`를 다르게 바꿨다. other.c를 확인하는 계약은 없다 | `src/other.c`만 충돌한다 | git처럼 멈춘다. `.ait/D`와 `specs/d.yaml`은 pull이 정한 대로 둔다. 받는 쪽이 해결해 마친다 |
| S1 | D와 main의 future K5가 `specs/shared.yaml`을 함께 쓴다. K5는 a가 갈라진 뒤에 main이 만들었다. a1이 그 파일을 고쳤다 | 합친 shared.yaml의 blob이 K5의 고정과 다르다 | 통과한다. K5는 같은 커밋에서 다시 고정된다(6.2) |
| S2 | S1과 같은데 K5가 current다 | S1과 같다 | 6.2의 다시 고정으로 K5의 고정이 바뀌므로 멈춘다(결정 6). hint는 `pull D future amend K5 -m <why>` 또는 `pull D --force -m <why>` |
| S3 | main의 current K6이 `src/stock.c`를 고정한다(g0). a1이 D를 이행하며 stock.c의 1행을 바꿨다 | 합친 stock.c가 K6의 고정 g0과 다르다 | 멈춘다. hint는 `pull D future amend K6 -m <why>` 또는 `pull D --force -m <why>` |
| S4 | S3과 같은데, K6을 main의 current offer O6이 떠맡았다 | S3과 같다 | 멈춘다. 첫 hint의 절은 `future amend O6 future amend K6`이다 |

### 3.7 사고 판과 옛 판 (결정 8)

| # | 이력 | git에서 확인한 것 | 기대 |
| --- | --- | --- | --- |
| R1 | a1: D를 cancel했다. a2: 손으로 future로 되돌리고 `prev`는 a1의 판으로 적었다 | a의 판은 사고 판이다(cancelled에서 future로 가는 전이는 없다) | `warning: skipping 'D' at 'a' (a2): …`. 남은 후보로는 `already D`다 |
| R2 | a1: D@A. main이 a1을 받았다. a가 `git revert a1`을 했다(a의 끝은 D0의 blob) | 내 D@A가 D0을 포함한다 | `already D`. revert는 a 브랜치만 옛 상태에 둔 것이고, 전해지지 않는다(규칙 2) |
| R2b | R2와 같은데, main이 a1을 받지 않았다 | a와 main이 같은 blob이다 | `already D` |
| R3 | R2 뒤에 a가 revert를 무르고(a3), ait로 조건을 c0으로 되돌렸다(a4: 본문 c0, `prev` D@A) | a4의 판이 내 D@A를 포함한다. 들이는 커밋은 a4(새 커밋 3개) | a4를 들인다. A가 거둔 것이 이렇게 전해진다 |

### 3.8 들이는 커밋이 여럿일 때 (2절 2)

| # | 이력 | git에서 확인한 것 | 기대 |
| --- | --- | --- | --- |
| MH1 | `pull D Q`. D는 a1에서, Q는 b1에서 들인다 | `git merge a b`(octopus)가 충돌 없이 끝난다 | 통과한다. 부모가 셋인 병합 커밋 하나다 |
| MH2 | MH1과 같은데, a1과 b1이 `src/x.c`를 다르게 바꿨다 | octopus가 실패한다 | 멈춘다. hint는 하나씩 받는 명령이다: `pull D`, 이어서 `pull Q` |

## 4. 모든 테스트에서 확인하는 것

- **판 누락 없음.** 통과한 뒤 내 판은, 받기 전의 내 판과 그때의 최신 판(사고 판 제외)을 모두 포함한다. 고른 경우도 같다. U1, U1b, G2, G4b에서 다시 pull하면 최신 판이 내 판 하나임을 확인했다.
- **멈추면 아무것도 바뀌지 않는다.** 2절 7의 코드 충돌만 예외다.
- **들어온 계약은 모두 지정한 것이다.**
- **결과의 새 판은 사고 판이 아니다.** 결과는 validate를 지나고, 새 미확인이 없다. `--force`라면 forced 확인 기록이 있다.
- **hint를 따르면 다음 단계로 간다.**
  - 같은 거절을 되풀이하지 않는다.
  - 고르기 hint를 따르면 같은 갈라짐으로 다시 멈추지 않는다.
  - hint의 명령은 원래 이름과 이미 고른 것을 모두 담는다.
- **멈춤의 횟수.** 각 pull에서 고르기로 멈추는 것은 그 시점 데이터의 갈라짐(2절 1)과 정확히 일치해야 한다.
  - 무엇을 고르느냐에 따라 뒤의 갈라짐이 달라질 수 있다(r24 검수 R07: 같은 미래에서 A를 고르면 1회, B를 고르면 2회).
  - 그래서 전체 횟수가 하나의 최소값과 같기를 요구하지 않는다.

## 5. 규격에서 바뀌는 것

- **전제 표, 규칙 8, 10.1b**
  - pull은 이력째 받는다.
  - 들어오는 계약은 모두 지정한다.
  - 코드는 언제나 함께 들어온다.
  - "아직 이행되지 않은 남의 코드는 받지 않는다"를 지운다.
- **7.12 전면 개정** (위 2절)
  - `E_MIXED`, 옛 뜻의 `--force`, `Ait-Resolves` 따라가기를 지운다.
  - `Ait-Resolves` 기록줄(7.1.5)이 아직 쓰이는 곳이 있는지 함께 본다.
  - 7.10.0의 분류 중 pull만 쓰던 것(이행 커밋 등)을 정리한다.
- **pull 문법:** `K=C`, `<D>-<O>=C`, 도우미 절, `--force -m`. 5.9a의 고치는 명령 꼴과 맞춘다.
- **요청 ID 대신 계약 ID:** 1.7, 5.4, 9.4, 7.9.6, H9·H11·H15, 5.9a의 들이기 hint는 `pull RID` 대신 `pull D…`를 쓰고, 받았는지를 판의 포함으로 판정한다. 떠맡기 사건의 hint는 `pull D O`다.
- **1.5a와 규칙 4**
  - 미확인 조건에서 "명세 파일이 고정과 다르다"를 지운다. 그것은 사고이고, 이미 2.4.1로 막힌다.
  - "미확인(사고 표시)" 같은 표현을 고친다. 1.5d 등 두 말이 섞인 곳도 모두 고친다.
- **5.10:** 사고 판(규칙 1)을 HEAD의 오류에 더한다. 고칠 때까지 막고, 고칠 때는 GOOD의 판에서 잇는다.
- **7.1.1:** hook에 `post-commit`을 더한다. 옛 판으로 돌아간 것과 사고 판을 그 자리에서 알린다(결정 8의 출력).
- **7.1.3-5:** 코드 충돌로 멈춘 pull을 마칠 때, pull이 index에 둔 계약 파일의 `prev`(고르지 않은 판 포함)를 지킨다.
- **9.1, 9.2, 9.5**
  - 사건은 새 판에만 생긴다. 옛 판으로 돌아간 커밋은 사건이 아니다(규칙 2).
  - 고른 결과 판은, `prev` 중 결과와 뜻이 다른 판이 있으면 R에 든다. 9.2-2(병합 커밋)를 부모가 하나인 커밋에도 적용하는 것이다.
  - 사건 ID를 (계약, 판)으로 바꾼다(결정 12). 사본을 요청 ID로 가리던 9.5를 지운다.
- **11.1, 11.2:** revert 등에 대한 설명을 결정 8로 바꾼다. pull이 커밋을 어떻게 만드는지도 적는다.
- **13.1:** git 최소 버전을 `merge-tree --write-tree`가 있는 판으로 올린다(사용자가 2.40까지 허용).
- **5.9a:** `make it a promise`를 고친다.
- **시나리오와 SKILL을 함께 맞춘다.** SKILL에는 git merge를 직접 쓰는 길을 적지 않는다(결정 9).

## 6. 정한 것 (이 개정에서)

- **알림은 (계약, 판) 단위로 센다**(결정 12). 같은 판이 사본이나 rebase로 여러 커밋에 생겨도 받는 쪽에는 한 번 간다. 옛 판으로 돌아간 것은 사건이 아니다.
