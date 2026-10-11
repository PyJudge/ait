# `ait contract pull` 기준 (기대 테스트, 개정 9)

로직보다 먼저 **무엇이 맞는 결과인지**를 정한다. 근거는 사용자와 정한 [pull-decisions.md](pull-decisions.md)의 결정 1~30이다. git이 하는 일(합치기, abort, merge 설정)은 git에 맡기고, 그에 대한 장치를 더하거나 보장을 하지 않는다(사용자 원칙, 2026-10-10). ait가 정하는 것은 계약 데이터의 규칙이고, 어긋난 것은 경고와 고치는 길로 드러난다. 명세 파일도 같다. ait는 어느 변경을 받지 않을지만 정하고, 합치기는 git이 한다(기록 107).

판정에 쓰는 것은 저장소의 데이터뿐이다. 계약 파일의 내용과 그 `prev`·확인 행, 커밋, ref가 그것이다. 어떤 git 조작으로 커밋이 생겼는지, 커밋 메시지의 기록줄이 있는지는 판정에 쓰지 않는다(결정 8, 17). 기록줄은 알림을 누구에게 보낼지 찾는 데만 쓴다(결정 16).

3절의 "git에서 확인한 것"은 모두 실제 저장소(git 2.53.0)에서 확인했다.
- 재현 스크립트와 출력은 [pull-criteria-runs/](pull-criteria-runs/)에 있다.
  - 개정 9에서 더한 경우: `scen9.py` → `rev9.log`
  - 개정 8에서 더한 경우: `scen8.py` → `rev8.log`
  - 개정 7에서 더한 경우: `scen7.py` → `rev7.log`
  - 개정 6에서 더한 경우: `scen6.py` → `rev6.log`
  - 개정 5에서 더한 경우: `scen5.py` → `rev5.log`
  - 개정 4의 경우: `scen4.py U G N S K M Z` → `rev4.log`
  - 개정 3부터 이어 온 경우: `scen.py` → `all.log`
  - hint를 따른 결과: `flows.py` → `flows.log`
  - D-O 떠맡기: `cycle3.py` → `cycle3.log`
  - r30 검수의 주장을 지금 규칙으로 다시 돌린 것: `verify-r30.py` → `verify-r30.log`, `verify-r30-gitcycle.sh` → `verify-r30-gitcycle.log`
  - 로그는 이 차례로 만든다: `scen.py`, `scen4.py`, `scen5.py`, `scen6.py`, `scen7.py`, `scen8.py`, `scen9.py`, `flows.py`, `cycle3.py`, `verify-r30.py`. flows는 앞 스크립트가 만든 저장소를 쓰고, 커밋 시각은 git 호출 수로 정해진다. 그래서 차례가 바뀌면 커밋 OID가 바뀐다. 판정 데이터는 같다.
- 계약 파일의 git merge는 v14 merge driver(7.13.1)의 1·2·3·5단계를 옮긴 `driver.py`로 한다. 비교용이다. pull 자신은 계약 파일을 git merge에 맡기지 않는다(1절의 "받는 결과").
- 스크립트는 git의 사실과 1절 낱말의 데이터만 출력한다(포함, 최신 판, 3-way, 끝난 쪽, 남는 판, 들이는 커밋, 사고 판, 형식이 틀린 판, 확인 행, 되돌린 커밋). 스크립트는 SHA-1 저장소만 만든다. ait의 판정은 이 문서의 규칙으로 손으로 정했다.

## 1. 낱말

- **계약 파일**: `.ait/<ID>`(계약)와 `.ait/undertake/<D>-<O>`(떠맡기 기록).
- **판**: 계약 파일 `.ait/D`가 바뀔 때마다 생기는 그때의 내용 하나다. blob OID로 가리킨다.
- **확인 행**: current인 D·O의 계약 파일에 하나까지 있다(결정 17, 기록 79). current인데 없거나 PINS가 맞지 않으면 사고 판이다.
  - `check reported <PINS>`는 `-r`로 확인한 것(테스트 결과로 통과), `check forced <PINS>`는 `--force`로 판단해 넘긴 것이다.
  - PINS는 v14 5.4와 같다. 그 판의 `provider` 행들과 `file` 행들을 적힌 순서대로, 행마다 LF로 끝내 이은 바이트열을 그 저장소의 `git hash-object --stdin`에 넣은 값이다.
  - future로 되돌리면 확인 행을 지운다.
- **본문**: 판에서 `prev` 행과 확인 행을 뺀 것. 같은지는 바이트로 본다. 고정(BLOB)은 본문에 든다.
- **포함**: 판 a가 판 b를 포함한다는 것은, b가 a이거나 a의 `prev`를 따라가 b에 닿는다는 뜻이다. 따라가다 객체 저장소에 없는 blob을 만나면 그 가지는 거기서 끝난다.
- **형식이 틀린 판**: 2.2의 레코드 형식에 맞지 않는 판이다. CR, 빈 행, 제어 문자, 알 수 없는 키, 정규형이 아닌 키나 범위의 순서(3.2a), 틀린 값(명세 파일 mode, OID, OID 꼴이 아닌 확인 행의 PINS, 3.1을 어긴 경로, 3.2의 범위 문법, OID가 아닌 `prev`), 둘 이상의 확인 행, current D·O가 아닌 판의 확인 행 등이 그렇다.
- **사고 판**: 다음 중 하나다(결정 8 규칙 1, 결정 17, 21, 28).
  - 사고 판도 형식이 틀린 판도 아닌 `prev` 중 어느 것에서도 갈 수 없는 판. 갈 수 있다는 것은 역할과 creator가 같고, 본문이 같거나 2.3의 상태 짝으로 갈 수 있다는 뜻이다. 처음 판(`prev -`)은 2.3의 "없음 →" 짝으로 본다.
    - 그래서 사고 판 위에 쌓은 판도 사고 판이다. 그 브랜치는 고칠 때까지 막힌다(결정 8, REV2, REV3). 고치는 길(5.10)은 사고 판이 아닌 판을 `prev`로 삼으므로 막힘이 풀린다.
  - 자신을 제외하고 `prev`를 따라 도달하는 끝난 판이 있는데, 본문이 그 끝난 판 중 어느 것과도 같지 않은 판. 사고 판을 거쳐 도달하는 것도 센다. 끝난 상태에서는 나가는 길이 없기 때문이다(2.3, 결정 21, 28). 검사하는 판 자신도 끝난 판일 수 있으므로 포함에서 자신을 뺀다. git merge 충돌을 손으로 풀어 cancel을 되살린 판(REV1)과, 사고 판을 거쳐 cancel 판을 포함한 판(REV4)이 여기에 든다.
  - current인 D·O인데 PINS가 맞는 확인 행이 없는 판. 확인 없이 current가 됐거나, 고정이 바뀌었는데 옛 확인 행이 남은 경우다(CK1, CK3, ID1).
  - 다른 레코드와의 제약(target이 current여야 한다, withdrawn에는 fulfilled X가 있어야 한다 등)은 여기서 보지 않는다. 받는 결과의 구조 검사(2절 4)가 본다.
  - `prev`의 blob이 없어 판정할 수 없으면 사고 판으로 보지 않는다.
  - 떠맡기 기록에는 역할과 creator가 없다. state 짝만 본다.
- **후보 판**: 다음 판들이다.
  - ref 목록(5.11)의 각 ref 끝 커밋이 그 계약 파일로 가진 판
  - HEAD의 판(**내 판**)
  - 이번 pull이 들이는 커밋이 그 계약 파일로 가진 판(E1)

  그 밖의 과거 커밋의 판은 후보가 아니다. 브랜치가 옛 판으로 돌아갔다면 그 브랜치는 옛 상태에 있을 뿐이기 때문이다(결정 8 규칙 2).
- **최신 판**: 형식이 틀린 판과 사고 판이 아닌 후보 판 중, 그런 다른 후보 판에 포함되지 않는 판.
- **끝난 판**: state가 cancelled·withdrawn(D·O)이거나 fulfilled·cancelled(X)인 판.
- **앞으로 나감 (3-way)**: 두 판 v, w의 공통 조상 중 가장 나중 것들이 모두 같은 본문 B를 가지고, v의 본문은 B이고 w의 본문은 B가 아니면, w는 v에서 **앞으로 나간** 판이다(결정 15). git merge가 공통 조상과 비교해 한쪽만 바꿨으면 고르지 않는 것과 같다.
  - 가장 나중 공통 조상이 없거나 그중 blob이 없는 것이 있으면(다른 공통 조상에 포함되지 않는 공통 조상의 blob이 없으면) 앞으로 나간 것으로 보지 않는다. 본문을 모르기 때문이다(MS2).
- **남는 판**: 최신 판에서 다음을 차례로 뺀 것.
  1. **3-way.** 최신 판 전체를 한 번에 본다. 다른 최신 판이 그 판에서 앞으로 나갔으면 그 판을 뺀다(결정 15). 다만 앞으로 나감이 꼬리를 물어 고리를 이루면, 그 고리 위의 판은 빼지 않는다(결정 24, CYC1). 빼는 것은 동시에 한다. 그래서 남는 판이 하나도 없는 일은 없다.
  2. **끝난 쪽.** 1을 거친 판에 끝난 판과 끝나지 않은 판이 함께 있으면, 끝나지 않은 판을 뺀다(결정 13). 3-way를 먼저 하므로, 바꾼 것이 없는 판은 여기까지 오지 않는다(U4b).
- **결과 판**: pull이 지정한 계약에 두는 판이다. 결과 판은 그 계약의 **최신 판을 모두 포함한다**(결정 4, 13, 15).
  - 결과의 본문은 남는 판의 본문이다. 본문이 둘 이상이면 고른 판(`K=C`)의 본문이다.
  - 그 본문을 가진 남는 판이나 내 판 중 최신 판을 모두 포함하는 판이 있고, current D·O이면 그 확인 행이 아래 규칙으로 정한 것과 같으면, 그 판이 결과다(CK7). 내 판이면 들일 것이 없다.
  - 없으면 새 판이다. 새 판은 그 본문에, `prev` = 최신 판 모두다. 내 판은 최신 판이거나 최신 판에 포함되므로 언제나 결과에 포함된다.
  - 새 판이 current D·O이면 확인 행을 둔다(결정 22). 최신 판과 내 판 중 결과와 PINS가 같은 판에 `reported`가 있으면 `reported`, 없으면 `forced`다.
- **되돌린 커밋** (결정 27, 기록 99. v14 1.5d의 식을 이것으로 바꾼다): 다음을 모두 만족하는 커밋은 C를 되돌린 커밋이다. 그 커밋의 `Ait-Agent`가 되돌린 쪽이다.
  1. 커밋의 K 판 V가 future이고, 어느 부모의 K 판에도 포함되지 않는 새 판이다. 옛 판으로 돌아간 커밋(git revert 등)은 사건이 아니기 때문이다(결정 8 규칙 2, RV3b).
  2. current 판 C가 부모의 K 판이거나 V의 `prev`에 있다.
  3. 부모의 K 판 중 C나 C와 본문이 같은 판을 포함하는 future 판이 없다. 그런 부모는 남의 되돌림을 이미 실어 왔다.

  - pull의 고르기로 남의 이행을 버린 커밋이 그렇다(U1c). 그 위에 도우미 절을 얹어도 같다(U1d, IND4). 커밋에 들어가지 않는 중간 판은 보지 않는다.
  - 남이 이미 되돌린 판을 실어 나른 커밋은 아니다(T1r, RP3). 3-way로 본문이 같은 다시 확인을 덮은 커밋도 아니다(RP4). TIE는 내 판을 포함하는 판을 먼저 들이므로, 남의 되돌림을 실어 오는 판이 있으면 그것을 들인다(결정 30, RP6, RP7).
  - 판정에 필요한 blob(C, 또는 부모의 future 판에서 포함을 따라가는 판)이 없으면 세지 않는다. 판정할 수 없기 때문이다(U1z, U1m).
- **들이는 커밋**(판 v의): ref 목록에서 닿는 커밋 중, 그 계약 파일이 v이고 어느 부모에서도 v가 아닌 커밋이다.
  - 여럿이면(사본) HEAD에 없는 커밋이 가장 적은 것을 고른다. 그 수도 같으면 커밋 OID가 작은 것이다(결정 11).
  - 결과 판이 들어오는 판 그대로면 그 판의 들이는 커밋을 들인다.
  - 결과가 새 판이면, 결과의 본문을 가진 남는 판들의 들이는 커밋 중 하나를 들인다(TIE). 내 판을 포함하는 판의 것을 먼저 고르고(결정 30), 그다음 위의 순서다. `K=C`면 C의 판의 것이다. 내 판이 그 본문을 가지면 아무것도 들이지 않는다(G4b, `K=HEAD`).
    - 결정 30은 git과 같다. 내 상태 위에 쌓은 쪽은 git에서 fast-forward로, 판단 없이 받는다. 다른 쪽을 원하면 `K=C -m <why>`로 고른다(결정 27).
- **합친 결과**: HEAD와 들이는 커밋들을 git(merge-tree)으로 차례로 합친 결과다. 명세 파일은 받지 않을 변경을 뺀 뒤 같은 실행에서 합친다(아래 받는 결과). 계약 파일은 pull이 정한다. 어떻게 합치는지(설정, driver, rename, merge base)는 git의 일이다(RN1).
  - 충돌은 파일마다 한 번이다(결정 26). 어떤 차례에서 이미 충돌한 파일을 뒤의 끝이 또 바꾸면(충돌하든 안 하든) 그 차례로는 받을 수 없다. "또 바꾸면"은 지금까지 합친 결과와 그 끝의 merge base에서 그 끝이 바꾸는 것이다(MH10).
  - 차례(기록 92): 끝의 차례 중 파일마다 한 번만 충돌하는 차례가 있으면 그 차례로 받는다. 여럿이면 끝의 커밋 OID를 차례대로 늘어놓아 가장 앞선 차례다(ORD1). 그런 차례가 없으면 2절 4에서 멈춘다(MH8). 충돌 여부는 지금까지 합친 결과에 대고 본다.
  - 계약 파일의 충돌은 코드 충돌이 아니다. pull이 정하기 때문이다. 명세 파일은 받지 않을 변경을 뺀 뒤 git이 합치므로, 그래도 충돌하면 코드 충돌이다(결정 29, CP4). 제외한 뒤의 내용과 git이 고른 merge base의 차이로 충돌해도 git의 충돌로 처리한다(결정 37, SF3). KS1·DL2처럼 제외한 뒤 깨끗하게 합쳐지는 경우도 그대로다.
- **받는 결과**: 합친 결과 위에 pull이 정한 것을 놓은 상태다. pull이 정하는 것은 다음과 같다.
  - **`.ait/` 아래의 모든 경로**(기록 71). 지정한 계약은 결과 판을, 지정하지 않은 계약은 내 판을 둔다. 지정하지 않은 계약이 들어오는 계약이면 2절 3에서 멈추므로, 여기까지 오면 내 판이 맞다.
    - 레코드는 지우지 않는다(2.1.5, DEL1).
    - 계약 파일이 아닌 경로와, 지정하지 않은 계약의 형식이 틀린 판·사고 판은 받지 않고 경고한다(MF2).
  - **명세 파일: ait는 받지 않을 변경만 정하고, 합치기는 git이 한다**(기록 107).
    - 아래에서 명세 파일의 내용은 mode와 blob을 함께 비교한다(2.4.1). 받지 않는 변경을 빼거나 사고인 쪽의 고정으로 볼 때도 둘을 함께 둔다.
    - **두 쪽.** 내 쪽(HEAD)과 들이는 쪽(들이는 커밋마다)이다. ref 끝이 아니다(SP7). 한 쪽의 변경은 그 커밋과 HEAD의 merge base에 견준 경로마다의 변경이다. 내 쪽은 합치는 차례의 첫 끝과의 merge base에 견준다. 쪽마다 한 번만 본다. 앞 끝을 합친 내용을 다시 보지 않는다(SP9).
    - **결과의 고정.** 받는 결과(지정한 계약은 결과 판, 지정하지 않은 계약은 내 판)에서 future·current인 계약의 고정이다. 도우미 절과 6.2 다시 고정을 하기 전의 것이다.
    - **그 쪽에서 고정한 판.** 그 쪽 커밋에서 future·current인 판이다. 끝난 판은 고정하지 않는다(2.4.1, SP8). 형식이 틀린 판은 state를 믿을 수 없으므로 state와 상관없이, `file` 행(낱말을 공백으로 나눠 읽는다)이 그 경로를 적으면 센다(MF2, MF3).
    - 한 쪽의 변경은 다음 차례로 본다.
      1. 결과의 고정 중 하나가 그 내용이면 받는다(SP5, SP6, SP13). 결과가 새 판이어도 그렇다. 판이 아니라 고정으로 견주기 때문이다.
      2. 아니고, 그 쪽에서 고정한 판이 그 내용으로 고정하면 옛 판이나 버린 판의 명세다. 받지 않는다(KS1, RV2, PV1, MF2).
      3. 아니고, 그 쪽에서 고정한 판이 그 경로를 다르게 고정하면 그 쪽의 사고다. 경고한다(결정 17). 그 쪽의 내용은 그 판의 고정으로 본다. 그것이 결과의 고정이면 받고(SP10, SP11), 아니면 받지 않는다(SP1).
      4. 아니면 그 쪽의 코드 변경이다. 받는다(CP2, SP2, SP3, SP8). 다만 결과가 고정하는 경로를 지우는 변경은 받지 않고 경고한다(DL1, DL2, DEL1과 같다): `warning: skipping the removal of 'tests/d.sh' in 'a1': 'D' pins it`.
    - **받지 않는 변경은 git에 넘기기 전에 그 쪽에서 뺀다.** 그 경로를 결과의 고정으로 바꾼 tree(결과가 그 경로를 한 내용으로 고정하지 않으면 merge base의 내용)를 그 커밋의 자리에 두고, git이 코드와 함께 같은 차례로 합친다. 결과가 고정하지 않는 경로는 분류에 쓴 merge base의 내용으로 두지만, 뒤에서 git이 더 새로운 merge base를 골라 충돌할 수 있다(SF3). 그 충돌을 없애려고 base의 tree를 바꾸지 않는다(결정 37). 결과의 고정이 있는 X1은 그 고정으로 바꾸므로 기존 기대 그대로다. 대체 tree는 원래 커밋을 부모로 둔 임시 커밋에 담고, 중간 합친 커밋에서도 원래 HEAD와 들이는 끝들에 닿게 한다. 공개할 커밋의 부모는 원래 HEAD와 들인 커밋들이다. 임시 객체는 객체 저장소에만 쓴다. merge base 선택과 그 내용, rename, 충돌은 git의 것이다(RN2, SP12).
    - 합친 명세 파일이 결과의 고정과 다르면 그 내용을 두고 6.2로 다시 고정한다. 다시 고정할 계약이 current면 5에서 멈춘다(결정 3, SP4). 충돌하면 코드 충돌이다(결정 29, CP4, SF2, SP4c, SP13).
    - 그래서 옛 판, 버린 판, 사고의 명세 변경은 들어오지 않는다. 결과의 다른 계약이 같은 내용을 고정하면 1로 받는다(SP13). 어느 쪽의 코드 변경도 알리지 않고 사라지지 않는다. 끝난 결과의 옛 명세 파일처럼 어느 쪽에서도 고정하지 않은 파일은 코드다(TF2, RN1).
  - **도우미 절(결정 10), `--force`, 6.2의 다시 고정.** 결과 판 위에 적용한다. 결과 판이 새 판 V1이면, 그 위에 얹는 판의 `prev`는 V1과 V1의 `prev` 모두다(기록 72, IND2, IND3). V1과 같은 고르기만 한 다른 에이전트와도 모이고, V1의 blob이 없는 clone에서도 나머지 `prev`로 모인다.
  - **6.2의 다시 고정.** 명세 파일이 바뀌었으면, 그 파일을 선언한 future·current 계약을 모두 다시 고정한다.
- **들어오는 계약**: 들이는 커밋이 가진 판 중 내 판에 포함되지 않고, 형식이 틀린 판도 사고 판도 아닌 것이 있는 계약이다. 떠맡기 기록 `<D>-<O>`가 그렇다면 D와 O다(결정 7). 들이는 커밋의 이력이 옛 판으로 돌아갔거나 레코드를 지웠으면 들어오는 계약이 아니다(RV2, DEL1).
- **pull이 쓰는 경로**: 받는 결과가 HEAD와 다른 경로 모두다(코드 포함). git이 충돌로 알린 모든 경로도 든다. 결과의 파일 내용이 HEAD와 같거나 충돌 표지가 없어도, index에 충돌 stage를 쓸 경로는 포함한다(CD1). pull은 이 경로만, index와 작업 폴더에 쓴다(기록 75, WT2).

## 2. 판정

```
ait contract pull K[=C]… [도우미 절…] [--force] [-m MSG]
```

- K는 계약 ID다. C는 고를 판을 가진 커밋이거나 `HEAD`다.
- 떠맡기 기록은 `<D>-<O>=C`로 고른다.
- 도우미 절은 `future amend K`처럼 다른 도우미 명령과 같은 꼴이다. 관문의 기준 H는 절을 적용하기 전의 받는 결과이고 U는 그 위에 절을 적용한 상태다. 계약 경로와 전이는 절이 바꾼 것에 검사하며, 함께 받는 코드를 절이 바꾼 코드로 보지 않는다. 절이 요구하는 확인은 6.5다. 그 확인 집합이 있으면 과제를 쓰고 `E_NEEDS_RESULTS`로 멈추며, hint는 같은 pull에 `--force`를 더한 명령이다. pull이 받지 않는 `-r`를 요구하는 hint는 주지 않는다. `--force`면 그 확인 집합도 받는 결과로 다시 고정하고 2.2.2대로 확인 행을 쓴다. provider가 없으면 `E_CODE_REQUIRED`이며 코드 충돌에는 `--force`를 받지 않는다(결정 36, HC1). 공개는 6에서 한 커밋으로 한다.
- `-m`은 고르기(결정 25), 도우미 절, `--force`가 있을 때 필요하다. 5.3과 같이 `-F FILE`도 되고, 없으면 `E_USAGE`다. pull이 규칙대로 스스로 적는 새 판(3-way, 끝난 쪽, 같은 본문 합치기, 6.2 다시 고정)은 ait가 메시지를 쓴다.

아래 0~5에서 멈추면 아무것도 바꾸지 않는다. 바꾸지 않는 것은 ref, HEAD, index, 작업 폴더, ait 상태다. 객체 저장소에 객체가 느는 것과 도우미 절의 확인 거절 때 6.5·8장대로 확인 과제 파일을 쓰는 것은 예외다. 사용자의 merge driver가 하는 일은 git merge와 같이 이 보장 밖이다.

**0. 시작 조건**
- v14 5.2와 같다. `.ait/`가 깨끗하고, 진행 중인 git 조작이 없고, 브랜치 위에 있어야 한다.
- HEAD에 이미 있는 오류(5.10)로 막히면 그 hint를 준다. HEAD의 사고 판, 형식이 틀린 판, 묶음 무결성(5.9 e, 2.4)의 위반도 5.10의 오류다(결정 8, 17, HD1, HD3).
- pull이 쓰는 경로에 index나 작업 폴더의 변경이 있으면 멈춘다(WT1). 추적하지 않는 파일도 같다(NEW2). 무시되는 파일은 git merge처럼 덮어쓴다(결정 38). 받는 결과가 merge이고 index가 HEAD와 다르면 `E_DIRTY`다(IDX1). pull이 쓰는 경로는 1~5로 받는 결과를 정한 뒤에 알 수 있으므로, 이 검사는 5 다음, 아무것도 바꾸기 전에 한다.
  ```
  error: your local changes to 'specs/d.yaml' would be overwritten by this pull
  hint: commit your changes with 'ait contract add specs/d.yaml' and 'ait contract commit -m <why>', or set them aside, before you pull
  ```
  - 출력은 CLAUDE.md의 git 스타일을 따른다(결정 31). 메시지는 영어 한 행이고 마침표를 찍지 않는다. 까닭은 `error:` 행에, 할 일은 `hint:` 행에 쓴다. hint는 ait 명령이나 이루어질 상태이고, git 명령은 마치는 커밋 하나뿐이다. 낱말과 문장의 꼴은 git의 같은 경우를 따른다(git: `Please commit your changes or stash them before you merge.`).
  - 그 경로가 계약 경로(계약 파일이나 계약이 고정한 파일, 6.1)이면 위처럼 ait로 커밋하라고 쓴다(WT1). 코드 경로이면 `hint: commit or set aside your changes to 'src/x.c' before you pull`이다.
  - 추적하지 않는 파일이면 `hint: move or remove 'specs/q.yaml' before you pull`이다(NEW2. git: `Please move or remove them before you merge.`).
- 들이는 커밋이나 그 merge base가 shallow 경계 밖이면 멈춘다(SH1). hint는 이루어질 상태다: `hint: 'a1' can be taken once this clone has its history down to the merge base with HEAD`. git 명령은 적지 않는다. partial clone에서는 git이 객체를 받아 오는 대로 읽고, 받아 오지 못하면 멈춘다.

**1. 판 정하기.** 지정한 계약마다, 그리고 지정한 계약이 끝인 떠맡기 기록마다 다음을 본다(결정 23).
   - 형식이 틀린 판과 사고 판은 후보에서 빼고 이렇게 쓴다.
     ```
     warning: skipping malformed '.ait/D' at 'a1'
     warning: skipping 'D' at 'a' (a2): it was changed outside ait's rules
     ```
   - 남는 판을 구한다. 끝난 쪽 규칙으로 판이 빠졌으면 경고한다. 빠진 것이 내 판이면 `warning: your change to 'D' is overridden: 'D' was withdrawn in 'a2'`, 다른 브랜치의 판이면 `warning: the change to 'D' in 'b1' is overridden: 'D' was withdrawn in 'a2'`다. 빠진 판을 처음 담은 커밋들의 `Ait-Agent`에게 알림이 간다(결정 16).
   - **남는 판의 본문이 하나면** 그것으로 결과 판을 정한다(1절).
     - 결과 판이 내 판이면 들일 것이 없다.
     - 결과 판이 들어오는 판이거나 새 판이면, 들이는 커밋은 1절의 규칙으로 정한다.
   - **갈라짐:** 남는 판의 본문이 둘 이상인데 고르지 않았으면 멈춘다. 셋 이상이 꼬리를 무는 경우도 같다(CYC1).
     ```
     error: 'D' has diverged between 'a1' (a) and 'HEAD'
     hint: pick one: 'ait contract pull D=a1 -m <why>' or 'ait contract pull D=HEAD -m <why>'
     ```
     - hint는 원래 지정한 이름과 이미 고른 것을 모두 둔 한 명령이다.
     - 갈라진 계약이 여럿이면, 모두 같은 커밋을 고르는 명령들을 먼저 주고 그다음 나머지 선택을 준다(SF1). 결정 29로 섞은 고르기도 4를 지난다.
     - 4를 지나지 못할 선택은 주지 않는다(XT1). 결정 26의 멈춤(나눠 받기든, 결정 20의 상태든)은 여기에 들지 않는다. 그 hint를 따르면 받을 수 있기 때문이다(MH9, XT2, XT3).
   - **`K=C`:**
     - C(또는 `HEAD`)의 판이 남는 판 중 하나여야 한다. 결과 판은 그 본문으로 정한다(1절). 그래서 고른 판이 내 판을 포함하지 않으면, 남는 판이 하나여도 새 판이다(KC2).
     - `K=HEAD`이고 내 판이 최신 판을 모두 포함하면 `already`다(KB3).
     - C의 판이 남는 판이 아니면 멈춘다(KB1, KB2). 그 계약이 갈라져 있으면 지금의 고르기 hint를, 갈라져 있지 않으면 고르기를 뺀 `pull K…`를 준다.
   - 도우미 절과 `--force`가 없고, 지정한 계약이 모두 들일 것이 없고 새로 적을 것도 없으면 session의 유효성을 보고(필요하지는 않다, 5.1) `already K…`를 쓰고 끝난다. 도우미 절이나 `--force`가 있으면 5까지 적용한 뒤 새로 적을 것이 없는지를 본다. 이미 받은 계약을 지정했다는 이유로 함께 준 절을 버리지 않는다.
   - 지정한 계약에 후보 판이 하나도 없으면 `E_ID`다. 후보가 있었는데 모두 형식이 틀린 판이나 사고 판이라 빠졌으면 `E_ID`가 아니라 4의 사고 판 오류와 같은 꼴이다(ID1). 같은 명령을 다시 주는 hint는 쓰지 않는다.
     ```
     error: 'W' at 'a' (a2) was changed outside ait's rules
     hint: 'W' has to be fixed on branch 'a' first
     ```

**2. 들이는 커밋 모으기.** 1에서 정한 들이는 커밋 중 다른 들이는 커밋의 조상이거나 HEAD의 조상인 것은 뺀다.
   - 남는 판의 들이는 커밋이 HEAD의 조상이면, 내 브랜치가 git으로 옛 판에 돌아간 것이다(RV3). 들일 커밋은 없고, 그 판과 그 명세 파일을 HEAD 위의 커밋(요청 커밋)으로 적는다. 들이는 끝이 하나도 없으면 결과의 고정이 한 PATH에 같은 MODE·BLOB을 요구할 때만 그 파일을 복원한다. 서로 다른 고정을 요구하면 4에서 `E_INCOMPLETE`로 멈추고, 같은 명세 파일을 고정한 판을 고르는 hint를 준다(RV4, 결정 35). 조상 커밋을 merge 끝으로 다시 넣거나 충돌 상태를 만들지 않는다. 코드는 오지 않으므로 5가 판정한다(RV3b).

**3. 지정 검사.** 들어오는 계약은 모두 지정되어 있어야 한다. 아니면 멈춘다.
   ```
   error: taking 'a2' also changes contracts you did not name: 'Q'
   hint: name them too: 'ait contract pull D Q'
   ```
   - 떠맡기 기록이 들어오면 문장은 `… you did not name: 'O' (undertake 'D-O')`다(K1, K6, K7).
   - 들이는 커밋이 가진 판은 후보 판이 되고, 더해야 할 이름이 끝인 떠맡기 기록도 새로 보이게 된다. 그래서 이름과 후보가 더 늘지 않을 때까지 1~3을 되풀이하고, 그때의 이름 모두를 한 hint에 준다(K8). `K=C`의 검사는 그 뒤에 한다(E1, E1b).

**4. 구조 검사**
   - 먼저 합친 결과를 구한다(1절). 어느 차례로도 이미 충돌한 파일을 뒤의 끝이 또 바꾸면 멈춘다(결정 26, MH8).
     ```
     error: 'a1' and 'b1' both change 'src/x.c', which you changed too
     hint: take them one at a time: 'ait contract pull D=a1 -m <why>', then 'ait contract pull Q'
     ```
     - 문장은 그 파일들을 바꾸는 끝과 두 번 걸리는 파일을 모두 쓴다(`'a1', 'b1' and 'c1' all change 'src/x.c'`, `… both change 'src/x.c' and 'src/y.c'`). `which you changed too`는 내가 그 파일을 바꿨을 때만 붙인다.
     - **나눠 받는 hint**(결정 20, 기록 109)는 명령마다 한 묶음의 끝을 받는다. 묶음은 함께만 받을 수 있는 끝들이다(D-O, MH12). 명령의 차례는 앞 명령들을 받은 뒤의 상태에서 어느 명령도 3이나 4에서 멈추지 않는 차례다. 그런 차례가 여럿이면 끝의 커밋 OID 순으로 가장 앞선 차례다(MH9, MH13). 둘째 명령부터는 앞 명령을 받은 뒤의 상태로 이름과 고르기를 정한다.
     - 그런 차례가 없으면 까닭을 오류 행에, 받을 수 있게 되는 상태를 hint에 쓴다(결정 20, 31, MH8, MH11).
       ```
       error: 'a1' and 'b1' can only be taken together, but both change 'src/x.c', which you changed too
       hint: they can be taken once one of them is rebased onto the other, or once your branch merges cleanly with one of them
       ```
       - 앞의 상태: 한 끝이 다른 끝 위로 rebase되면 들이는 커밋이 하나가 되어 파일마다 한 번만 충돌한다. merge로 합친 커밋은 이 상태가 아니다. 판이 처음 들어간 커밋은 그대로이기 때문이다(결정 1).
       - 뒤의 상태: 내 브랜치가 한 끝과 깨끗하게 merge되면, 그 끝을 먼저 받아 나머지 끝에서 파일마다 한 번만 충돌한다. 끝이 둘일 때 언제나 맞다. 끝이 셋 이상이면 앞의 상태만 쓴다(`once one of them is rebased onto the others`).
   - 받는 결과는 validate(5.9 a~e)를 지나야 한다. 다만 git이 충돌로 남긴 명세 파일 경로의 묶음 무결성(e)은 해결한 내용을 다시 고정하는 마치는 커밋에서 검사한다. 충돌 상태에는 하나의 파일 내용이 없으므로, future 계약의 서로 다른 고정이나 충돌 표지로 이 단계에서 거절하지 않는다(결정 29, SF2). current 계약이 확인하거나 고정한 파일의 충돌은 5에서 멈춘다. 그 밖의 위반이면 멈춘다.
   - HEAD에 이미 있던 위반은 0에서 다룬다. 여기서는 이 pull이 바꾼 레코드에 얽힌 위반만 본다.
   - 위반에 얽힌 레코드의 들이는 쪽 판이 사고 판이나 형식이 틀린 판으로 빠졌으면, 그 사실을 오류로 쓴다(ACC1). 여기서 할 수 있는 일이 없기 때문이다.
     ```
     error: taking 'a3' needs 'D' as 'a' has it, but 'D' at 'a' (a2) was changed outside ait's rules
     hint: 'D' has to be fixed on branch 'a' first
     ```
   - 들이는 끝이 없어서 한 명세 파일의 서로 다른 고정을 함께 복원할 수 없는 경우도 묶음 무결성(e) 위반이다. git이 만든 충돌이 아니므로 검사를 미루거나 6.2로 고른 판의 고정을 바꾸지 않는다(RV4).
   - 그 밖에는 hint가 모든 위반을 푸는 한 명령이다. 절은 계약 ID 순으로 한 번씩 담는다.
     - 다른 계약을 지정하면 풀리는 경우: 그 이름을 더한다.
     - 판을 맞춰 고르면 풀리는 경우: 맞는 선택들을 준다(SF1).
     - 계약을 되돌리면 풀리는 경우: `future amend` 절을 더한다(N5, K6). current offer가 떠맡은 계약을 되돌리면 그 offer도 함께 되돌린다(5.9a). 되돌리기는 누구의 계약이든 할 수 있다(1.5d).

**5. 확인 검사.** 받는 결과가 **새로** 미확인을 만들거나, 확인 없는 명세 변경을 만들면 멈춘다(규칙 4). 그래서 ait 명령인 pull은 미확인도 사고도 새로 만들지 않는다.
   - **새 미확인:** 받는 결과에서 current 계약의 provider 코드가 그 판의 고정과 다른데, 같은 고정으로 이미 미확인이던 부모(HEAD나 들이는 커밋)가 없는 것이다. 9.1의 "새 미확인"(상태)이다. 알림을 세는 "미확인 사건"(결정 18)과는 다르다.
   - **확인 없는 명세 변경:** 6.2의 다시 고정으로 current 계약의 고정이 바뀌는 것이다(S2, CP2의 current).
   - **이미 미확인이던 것:** 막지 않는다. 새 HEAD에서 미확인이고 옛 HEAD에서는 아니던 계약 중 현재 session의 약속·요구·구독(9.3)인 것이면 `warning: 'D' comes in unverified`와 H7·H8을 쓴다(v14 7.12.1-7과 같은 조건, 기록 76). 합친 코드가 새 내용이면 그 코드로 미확인 사건이 하나 더 생긴다(NU2).
   - **hint는 그 계약이 어디서 왔느냐에 따라 다르다.**
     - 내 브랜치(HEAD)에 있던 계약 K(결정 6, S2·S3). 누가 만들었는지는 상관없다.
       ```
       error: taking 'a1' changes what contract 'K6' checked: 'src/stock.c'
       hint: reopen it in the same request: 'ait contract pull D future amend K6 -m <why>'
       hint: or, if the change is trivial for 'K6': 'ait contract pull D --force -m <why>'
       ```
     - 들어오는 계약 D: 내 변경이 합쳐져 D가 확인한 것과 달라졌다(결정 10, C2).
       ```
       error: taking 'a1' merges your changes into what 'D' checked: 'src/stock.c'
       hint: take it reopened: 'ait contract pull D future amend D -m <why>'
       hint: or, if your changes are trivial for 'D': 'ait contract pull D --force -m <why>'
       ```
     - 되돌리기 절은 5.9a를 따른다. current offer가 그 계약을 떠맡았으면 그 offer도 함께 되돌린다(S4, IND1).
     - 들이는 커밋이 없으면(RV3b) 문장은 `error: 'D' at 'a1' checked code your branch no longer has: 'src/stock.c'`이다.
     - 받는 결과에서 provider를 찾을 수 없게 되면 되돌리기 절만 준다(S5). 범위를 옮기는 명령은 받는 결과의 코드가 있어야 해서 지금 따를 수 없기 때문이다.
     - `--force` hint는 받는 결과에 코드 충돌이 없을 때만 준다(F2).
   - 코드 충돌은 전용 진단을 먼저 쓴다. `--force`면 코드 충돌 자체로 거절하고, 아니면 확인한 파일의 코드 충돌을 본다. 충돌 표지를 새 미확인의 일반 코드 변경으로 먼저 판정하지 않는다(C3).
   - **확인한 파일의 코드 충돌:** 코드가 충돌하는 파일을 받는 결과의 current 계약이 확인하거나 고정하고 있으면 멈춘다(C3, CP4). 충돌을 해결한 내용은 아직 아무도 확인하지 않았기 때문이다. hint는 되돌리기 절을 더한 명령뿐이다.
   - **`--force`일 때:** 이 확인 검사에서 막힌 current 계약마다 받는 결과로 다시 고정하고 확인 행을 2.2.2대로 쓴다. provider 변경뿐 아니라 들어오는 계약의 새 미확인과 명세 파일만 바뀐 계약도 이 검사에서 막히면 확인한다(C2, S2). 받는 결과에서 코드가 충돌할 pull에는 `--force`를 받지 않는다.
   - **명세 파일을 공유한 future 계약:** 멈추지 않는다. 6.2대로 다시 고정된 채 받는다(S1, CP2).

   - 이어서 session을 검사한다. 요청 커밋을 만들 때만 session이 필요하지만, 설정된 값이 무효이면 요청 커밋이 없는 pull도 거절한다(5.1, SS1). 그다음 작업 폴더 검사를 한다.

**6. 받기**
   - 들일 커밋이 하나이고, HEAD가 그 조상이고, 받는 결과가 그 커밋의 tree와 같으면 fast-forward한다.
   - 그 밖에는 pull이 `commit-tree`로 커밋 하나를 직접 만든다(hook을 거치지 않는다). 부모는 HEAD와 들인 커밋들이다. 중간 커밋은 없다(결정 20). 들인 커밋이 없으면 HEAD 위의 커밋이다(`K=HEAD`, G4b, RV3, PV3).
   - 받은 뒤 pull이 쓰는 경로(코드 포함)를 index와 작업 폴더에 쓴다(기록 75). git merge와 같다.
   - pull이 만든 커밋에서 어느 계약 파일이 모든 부모의 판과 다르면 요청 커밋(기록줄)이다(기록 74, v14 7.1.5와 같은 기준). 새 판, 도우미 절, `--force`, 6.2의 다시 고정, RV3이 여기에 든다. 되돌린 커밋(1절, 결정 27)이면 그 `Ait-Agent`가 되돌린 쪽이다.
   - **코드가 충돌하면**(5의 "확인한 파일의 코드 충돌"에 걸리지 않으면) git처럼 멈춘다(C4, MH2, MH7). 충돌은 파일마다 한 번이다(결정 26).
     - pull이 merge 상태를 직접 만든다. `git merge --no-ff`가 만드는 것과 같은 꼴이다(받을 끝 모두를 적고, MERGE_MODE는 `no-ff`). pull이 쓰는 경로만 index와 작업 폴더에 쓴다. 계약 파일과 명세 파일은 정한 내용으로, 충돌한 코드는 충돌 상태로 둔다. 받는 변경끼리 충돌한 고정 명세 파일도 충돌 상태로 둔다(결정 29).
     - merge 메시지(MERGE_MSG)는 pull이 쓸 커밋 메시지다(`-m`과 기록줄).
     - 출력은 git merge의 충돌 행(`CONFLICT (content): Merge conflict in src/x.c`, `Automatic merge failed; fix conflicts and then commit the result.`)과 v14의 마치는 hint다(기록 104). hint에 쓰는 git 명령은 마치는 커밋뿐이다. 현재 session이 이 worktree의 기본 session이 아니면 앞에 `AIT_SESSION=SID `를 붙인다(5.9a).
       ```
       hint: commit the resolution first: 'git commit --allow-empty --no-edit --cleanup=strip', then continue as git says
       ```
     - 마치는 커밋의 부모는 HEAD와 받을 끝 모두다(MH3, MH7). 그 커밋의 hook은 데이터만 본다(기록 81, 102).
       - 들어오는 쪽(받을 끝)의 출발 판에서 사고 판과 형식이 틀린 판은 뺀다. pull의 후보와 같은 규칙이고, 같은 건너뛰기 경고를 쓴다(HK2). HEAD의 판은 v14 5.10·7.1.3-7대로 다룬다. HEAD에 있던 오류로는 거절하지 않는다.
       - 7.1.3-4는 레코드마다 본다. index의 판이 HEAD의 판 그대로면 v14대로 지나고, 그 오류는 7이 HEAD의 오류로 경고한다(HD2). 그 밖에는 출발 판 중 끝난 판이나 끝난 판을 포함하는 판이 있으면 끝난 판에서만 출발할 수 있다(결정 21·28). 그래서 해결하며 cancel을 되살린 판은 거절된다(HK3).
       - 계약 파일이 사고 판이 아니고 남은 출발 판을 모두 포함하면 판을 다시 고르지 않고 그대로 둔다(HK1). pull이 정한 계약 파일은 그렇다. 누가 정했는지는 묻지 않는다.
       - 해결하며 바뀐 명세 파일에는 v14 7.1.3의 다시 고정(future 계약)만 한다(CP4, 결정 29). 그대로 둔 판을 다시 고정하면 그 판 위에 얹는다. `prev`는 그 판과 그 판의 `prev` 모두다(기록 72, 108). 극대 출발 판(7.1.3-5)으로 다시 정하지 않는다. 그래야 받지 않은 끝의 최신 판이 빠지지 않는다(HK4).
     - abort는 `git merge --abort`가 하는 대로다(MH4). ait는 그 밖의 보장을 하지 않는다.
   - **출력:** 들인 계약마다 `included K from 'C'`. 들인 것 없이 새 판만 적었으면 `kept K`. 1의 warning과 5의 `comes in unverified`도 쓴다.
     - 요청 커밋을 만들면 다른 요청 명령과 같은 출력 행(v14 6.7: `request RID` 등, `--force`면 `check K forced`)을 쓴다.
     - 이 커밋이 되돌린 커밋이면 `warning: 'D' is reopened by this pull`과 H10을 쓴다(v14 규칙 6, 7.1.5와 같다). 내가 이제 그 계약을 다시 이행할 쪽이기 때문이다. 이유는 v14 7.1.5의 조건이 맞을 때만, 처음 맞는 하나를 붙인다(예: 결과의 `prev`를 뺀 내용이 future인 출발 판과 같으면 `: it keeps the future version over the current one`, U1c). 맞는 조건이 없으면 붙이지 않는다(IND4).

**출력 목록** (기록 117. v14 5.8로 옮긴다)

출력은 CLAUDE.md의 git 스타일을 따른다(결정 31). 커밋은 전체 OID로 쓰고(v14 5.7 "OID는 출력 어디서나 줄이지 않은 전체 OID"), 아래의 `'a1'`은 그 자리다. `'a1' (a)`는 커밋과 그것을 끝으로 가진 ref이고, `'D' at 'a' (a2)`는 ref와 그 ref에서 그 판을 담은 커밋이다. ref는 짧은 이름이다(`refs/heads/a`는 `a`, `refs/remotes/origin/a`는 `origin/a`). 끝이 셋 이상이면 `'a1', 'b1' and 'c1'`처럼 잇는다.

| 경우 | 종료 | 첫 행 | hint |
| --- | --- | --- | --- |
| 갈라짐(1) | 1 | `error: 'D' has diverged between 'a1' (a) and 'HEAD'` | `pick one: 'ait contract pull D=a1 -m <why>' or 'ait contract pull D=HEAD -m <why>'` |
| 고른 판이 남는 판이 아님(1, KB1, KB2) | 1 | `error: 'a1' has no version of 'D' to choose: a newer one includes it` | 갈라져 있으면 고르기 hint, 아니면 `take it as it is: 'ait contract pull D'` |
| 후보가 사고 판·형식이 틀린 판뿐(1, ID1) | 1 | `error: 'W' at 'a' (a2) was changed outside ait's rules` | `'W' has to be fixed on branch 'a' first` |
| 지정(3) | 1 | `error: taking 'a2' also changes contracts you did not name: 'Q'` | `name them too: 'ait contract pull D Q'` |
| 구조 위반이 사고 판에서 옴(4, ACC1) | 1 | `error: taking 'a3' needs 'D' as 'a' has it, but 'D' at 'a' (a2) was changed outside ait's rules` | `'D' has to be fixed on branch 'a' first` |
| 구조 위반(4) | 1 | v14의 `error: not allowed: …` | 모든 위반을 푸는 한 명령 |
| 같은 파일이 두 번 걸림, 나눠 받을 수 있음(4) | 1 | `error: 'a1' and 'b1' both change 'src/x.c', which you changed too` | `take them one at a time: '…', then '…'` |
| 같은 파일이 두 번 걸림, 함께만(4) | 1 | `error: 'a1' and 'b1' can only be taken together, but both change 'src/x.c', which you changed too` | `they can be taken once one of them is rebased onto the other, or once your branch merges cleanly with one of them` |
| 내 계약이 확인한 것이 바뀜(5) | 1 | `error: taking 'a1' changes what contract 'K6' checked: 'src/stock.c'` | 되돌리기 절, 이어서 `--force` |
| 들어오는 계약이 확인한 것에 내 변경이 합쳐짐(5) | 1 | `error: taking 'a1' merges your changes into what 'D' checked: 'src/stock.c'` | 되돌리기 절, 이어서 `--force` |
| 들이는 커밋 없이 확인한 코드가 없음(5, RV3b) | 1 | `error: 'D' at 'a1' checked code your branch no longer has: 'src/stock.c'` | 되돌리기 절, 이어서 `--force` |
| 확인한 파일의 코드 충돌(5, C3) | 1 | `error: taking 'a1' conflicts in what 'D' checked: 'src/stock.c'` | 되돌리기 절만 |
| 코드가 충돌할 pull의 `--force`(5, F2) | 1 | `error: --force cannot take a pull whose code conflicts: 'src/other.c'` | 되돌리기 절 |
| 작업 폴더의 변경(0, WT1) | 1 | `error: your local changes to 'specs/d.yaml' would be overwritten by this pull` | 2절 0의 두 꼴 |
| 추적하지 않는 파일(0, NEW2) | 1 | `error: untracked working tree files would be overwritten by this pull: 'specs/q.yaml'` | `move or remove 'specs/q.yaml' before you pull` |
| index의 변경(0, IDX1) | 1 | v14의 `E_DIRTY` | v14 그대로 |
| shallow(0, SH1) | 128 | `fatal: 'a1' is beyond the shallow boundary of this clone` | `'a1' can be taken once this clone has its history down to the merge base with HEAD` |
| HEAD에 이미 있는 오류(0) | v14 5.10 | v14 5.10의 오류 | v14 5.10의 hint |
| 코드 충돌(6) | 1 | git merge의 충돌 행(stdout) | v14의 마치는 hint |

- 갈라짐 문장은 고를 판을 모두 쓴다. 끝난 쪽 규칙으로 빠진 판은 쓰지 않는다(TT1은 둘이다).
- shallow는 git과 같이 `fatal:`이다. 이 clone이 가진 것으로는 판정할 수 없기 때문이다(git: `fatal: refusing to merge unrelated histories`). 그 밖의 멈춤은 받을 수 있는 상태로 가는 길이 있으므로 `error:`다.

그래서:
- 고르기로 멈추는 것은 두 쪽이 공통 조상 뒤에 **둘 다** 바꿨을 때(셋 이상이 꼬리를 무는 경우 포함), 서로 다른 끝난 판이 남았을 때, blob이 없어 공통 조상의 본문을 모를 때뿐이다. 사본, 이미 합쳐진 판, 본문이 같은 판, 한쪽만 바뀐 판, 끝난 판과 끝나지 않은 판으로는 멈추지 않는다.
- 결과 판은 언제나 최신 판을 모두 포함한다. 그래서 끝난 쪽 경고와 갈라짐은 다음 pull에서 되풀이되지 않는다. 건너뛰기 경고(사고 판, 형식이 틀린 판)는 그 브랜치를 고칠 때까지 pull마다 나온다(5.10).
- 보지 않은 계약은 들어오지 않는다(3). 코드는 지정 없이 언제나 함께 들어온다(결정 3).
- 계약 파일은 git merge에 맡기지 않는다. 명세 파일은 ait가 받지 않을 변경만 정하고 git이 합친다. 그래서 옛 판, 지운 레코드, 버린 판·사고의 명세 변경이 섞여 들어오지 않고, 어느 쪽의 코드 변경도 알리지 않고 사라지지 않는다.
- pull은 미확인도, 사고 판도 새로 만들지 않는다(1, 5).

## 3. 기대 테스트

**표기**
- 계약 이름은 줄였다(D, Q, O, X, K5 등).
- `D@x`는 x가 만든 D의 판이다. `init`의 D는 future이고 조건 c0이다.
- 받는 쪽(나)은 main이다. a, b, c, f, p, q, y는 다른 에이전트의 브랜치다.
- "git에서 확인한 것"은 `rev9.log`(개정 9에서 더한 경우), `rev8.log`(개정 8), `rev7.log`(개정 7), `rev6.log`(개정 6), `rev5.log`(개정 5), `rev4.log`(개정 4), `all.log`(개정 3부터 이어 온 경우)의 같은 이름 항목이다.
- hint의 `-m <why>`는 결정 25에 따라 고르기, 도우미 절, `--force`에 붙는다.

### 3.1 기본

| # | 이력 | git에서 확인한 것 | 기대 |
| --- | --- | --- | --- |
| A1 | a가 새 계약 Q를 만들었다(a1). main에는 Q가 없다 | Q의 후보는 a의 판 하나다. 들이는 커밋은 a1(새 커밋 1개). 합치면 `.ait/Q`와 `specs/q.yaml`만 바뀐다 | fast-forward. `included Q from 'a1'` |
| A2 | A1 뒤에 다시 `pull Q` | 최신 판이 내 판에 포함된다 | `already Q` |
| A3 | a1이 D와 Q를 바꿨고, b1이 Q를 다르게 바꿨다. `pull D Q` | D의 남는 판은 하나, Q는 둘이다(둘 다 바꿈) | 멈춘다. 지정한 계약은 함께 받거나 하나도 받지 않는다. hint는 `pull D Q=a1 -m <why>` 또는 `pull D Q=b1 -m <why>` |
| A4 | D는 모든 ref에서 같은 current 판이다. `pull D future amend D -m <why>` | 받을 판은 내 판이지만 도우미 절은 D를 future로 바꾼다 | `already`로 먼저 끝내지 않는다. 도우미 절을 적용한 요청 커밋 하나를 만들고 D는 future다 |
| HC1 | D는 모든 ref에서 같은 current 판이다. `pull D amend D -m <why>` | 내 판을 받는 것이지만 빈 current amend는 6.5의 재확인을 요구한다 | 과제를 쓰고 `E_NEEDS_RESULTS`로 멈춘다. hint는 `pull D amend D --force -m <why>`이며 `-r` hint는 없다. 그 명령은 같은 PINS의 reported를 보존하고, 확인 행까지 같으면 `already D`로 끝나 새 커밋을 만들지 않는다(결정 36, 결정 22) |

### 3.2 고르기: 둘 다 바꿨을 때

| # | 이력 | git에서 확인한 것 | 기대 |
| --- | --- | --- | --- |
| U1 | a1: D를 c-A로. main: D를 c-E로 | 남는 판 2개, 본문 2개. a1과 git으로 합치면 `.ait/D`와 `specs/d.yaml`이 충돌한다 | 멈춘다. `D=a1 -m <why>`이면 a1을 들이고 `.ait/D`는 D@A의 본문에 `prev` {D@A, D@E}, 명세 파일은 D@A의 고정대로다. 그 뒤 다시 pull하면 들일 것이 없다 |
| U1b | U1에서 `D=HEAD -m <why>` | a의 커밋은 main에 들어오지 않는다 | `kept D`. HEAD 위의 커밋 하나. 버려진 c-A를 만든 커밋의 `Ait-Agent`에게 알림이 간다(결정 16) |
| U2 | b와 f가 각자 a를 merge하고 충돌한 D를 c-B, c-F로 정했다(D@B', D@F'). main: D0 | 남는 판 2개, 본문 2개 | 멈춘다. `D=bM` 또는 `D=fM` |
| U3 | a: D를 이행했다(current). main: D를 c-E로 | 남는 판 2개(둘 다 바꿈) | 멈춘다 |
| U6 | D는 current다. a와 b가 각자 코드를 바꾸고 다시 확인했다(조건은 같음) | 남는 판 2개. 고정만 다르다 | 멈춘다. 어느 구현을 받을지 고른다 |
| U8 | a1: D@A. main이 a1을 받았다. a가 a1을 amend했다(D@A2, `prev` D0) | 남는 판은 내 D@A와 D@A2다. 공통 조상 D0에서 둘 다 바뀌었다 | 멈춘다 |
| NT2 | a1: D를 c-A, b1: D를 c-B로. A와 B가 동시에 각자 `D=HEAD`를 골랐다 | main에서 보면 남는 판 2개. 가장 나중 공통 조상이 D@A, D@B 둘이고 본문이 다르다 | 멈춘다. 두 에이전트의 판단이 다르기 때문이다 |
| NT3 | NT2 뒤에 A가 다시 `D=HEAD`(a3), 그다음 B가 `pull D` | B에게 남는 판은 a3의 것 하나다(B의 판을 포함한다) | B는 a3을 들인다. 둘이 같은 판으로 모인다 |
| CYC1 | a1 c-P, b1 c-Q, c1 c-R(각자 init에서). a는 c1만 보고 `D=HEAD`(a2), b는 a1만 보고(b2), c는 b1만 보고(c2) 각자 자기 것을 골랐다 | 최신 판 셋. b2는 a2에서, c2는 b2에서, a2는 c2에서 앞으로 나갔다. 셋 다 고리 위에 있다 | 멈춘다(결정 24). hint는 `pull D=a2 -m <why>`, `D=b2`, `D=c2` 중 하나 |
| CYC2 | CYC1 뒤 main이 `D=b2`를 골랐다. 그다음 main, a, b, c가 각자 `pull D` | 결과 판의 `prev`가 세 판을 모두 담는다. main은 들일 것이 없고, a, b, c에게는 결과 판 하나만 남는다 | 누구도 다시 멈추지 않는다. a, b, c는 결과 판을 그대로 받는다 |
| KC2 | T1의 이력에서 `pull D=a2 -m <why>` | 남는 판은 a2의 것 하나이고, 내 판을 포함하지 않는다 | `pull D`와 같다. 새 판은 c-A2 본문에 `prev` {a2의 D, 내 D} |
| U1c | a1: D를 이행했다(current). main: D를 c-E로. main이 `pull D=HEAD -m <why>` | 새 판은 future이고 `prev`에 a의 current 판이 있다. 그 판을 포함하는 future 판은 `prev`에 없다 | `kept D`. 이 커밋은 a의 이행을 되돌린 커밋이고, main의 `Ait-Agent`가 되돌린 쪽이다(결정 27). `warning: 'D' is reopened by this pull: …`과 H10을 쓴다. a에게 알림이 간다 |
| T1r | D는 current다. a1: 다시 확인. a2: `future amend D`(되돌림). main은 D를 바꾸지 않았다. `pull D` | 남는 판은 a2의 것 하나이고 내 판을 포함한다. a2의 판 자신은 a1의 current를 되돌린 판이다 | a2를 그대로 받는다. main은 되돌린 쪽이 아니다. 되돌린 쪽은 a다 |
| U1d | a1: D를 이행했다(C). main: D의 조건을 고쳤다(H). main이 `pull D=HEAD future amend D … -m <why>`(consumer를 바꿈) | 고르기의 중간 판 V1(future, C를 포함)은 어느 커밋에도 없다. pull 커밋의 부모는 HEAD 하나이고, 그 판 H는 C를 포함하지 않는다 | 되돌린 커밋이다. main이 되돌린 쪽이다. 중간 판은 판정에 쓰지 않는다 |
| RP3 | main이 `pull D=a1`로 a의 current C를 받았다(V1). 그 뒤 a가 C를 되돌렸다(R). main이 `pull D` | R이 V1에서 앞으로 나갔다. main의 새 판은 R의 본문에 `prev` {R, V1}이다. 부모 a2의 판 R은 future이고 C(V1과 본문이 같음)를 포함한다 | 되돌린 커밋이 아니다. a의 되돌림을 실어 나른 것이다. 되돌린 쪽은 a다 |
| RP4 | D는 current(D0, `check forced`)다. main이 D를 되돌렸다(R). a가 같은 코드를 `-r`로 다시 확인했다(C', `check reported`) | C'은 D0과 확인 행만 다르므로 본문이 같다. R이 C'에서 앞으로 나갔다. 결과의 본문은 내 판(R)의 것이므로 들이는 커밋이 없다. pull 커밋의 부모는 HEAD 하나다(`rev8.log` RP4) | `kept D`. 새 판은 R의 본문에 `prev` {C', R}이다. 되돌린 커밋이 아니다(부모의 R이 D0을 포함하고, D0의 본문은 C'과 같다). 되돌린 쪽은 이미 R을 적은 main이다 |
| RP5 | RP3의 pull 커밋 | 부모 HEAD의 판 V1은 current, 부모 a2의 판 R은 future이고 C를 포함한다. v14 1.5d의 옛 식으로는 되돌린 커밋이고, 1절의 식으로는 아니다 | 되돌린 커밋이 아니다. pull의 경고도, 그 뒤의 todo·H10·알림도 main을 되돌린 쪽으로 세지 않는다(1.5d를 1절의 식으로 바꾸므로 같은 답이다) |
| RP6 | a1: D를 이행했다(C, 코드 그대로). main이 a1을 받았다. x1: D를 되돌렸다. x2: c-B. y1(init에서): c-B. `pull D` | 최신 판은 x2와 y1의 판이고 본문이 같다. x2의 판은 C를 포함하고(x1을 거쳐) y1의 판은 포함하지 않는다. 새 커밋은 x2 2개, y1 1개다. y1을 들이면 pull 커밋이 C를 버리고, x2를 들이면 x1의 되돌림을 실어 온다 | TIE는 내 판을 포함하는 x2를 들인다(결정 30. 새 커밋이 더 많아도 그렇다). pull 커밋은 되돌린 커밋이 아니다. 되돌린 쪽은 x다. git에서 x를 fast-forward로 받을 때와 같다 |
| U1z | U1c 뒤, a의 브랜치를 받지 않은 clone z가 main을 받았다 | z에는 C의 blob이 없고 pull 커밋은 있다 | z에서는 그 커밋을 되돌린 커밋으로 세지 않는다. C를 읽을 수 없기 때문이다. a의 브랜치를 받으면 센다 |
| RP7 | RP6과 같은데 x는 되돌림과 c-B를 한 커밋(x1)에 했다. x1과 y1은 새 커밋 1개씩이다. 커밋 ID 순서를 바꿔 두 번 | x1의 판은 C를 포함하고 y1의 판은 포함하지 않는다. x1을 들이면 pull 커밋은 되돌린 커밋이 아니고, y1을 들이면 되돌린 커밋이다(`rev9.log` RP7) | 커밋 ID와 상관없이 x1을 들인다(결정 30). 되돌린 쪽은 x다 |
| U1m | a1: D를 이행했다(C). main이 받았다. a: 되돌림(R1) 뒤 c-R(R2). 그 둘을 squash했다. clone z에는 R1이 없다. z가 `pull D=a2s -m <why>` | z에서는 R2에서 C까지 포함을 따라갈 수 없다(R1이 없다). R1이 있는 저장소에서는 부모 a2s의 R2가 C를 실어 온다 | 두 곳 모두 되돌린 커밋이 아니다. z에서는 판정에 필요한 blob이 없어 세지 않는다 |

### 3.3 끝난 쪽이 이긴다 (결정 13, 21)

| # | 이력 | git에서 확인한 것 | 기대 |
| --- | --- | --- | --- |
| U4 | a: D를 cancel했다. main: D를 c-E로 | 3-way로 빠지는 판은 없다. 끝난 판은 a의 것뿐이다 | 멈추지 않는다. 결과는 cancelled 본문에 `prev` {a의 D, 내 D}인 새 판이다. `warning: your change to 'D' is overridden: 'D' was cancelled in 'a1'` |
| U4b | main은 D를 c-tmp로 고쳤다가 다시 c0으로 바꿨다. a: D를 cancel했다 | 내 본문이 D0과 같다. a의 판이 내 판에서 앞으로 나갔다(3-way). 끝난 쪽 규칙에 오기 전에 내 판이 빠진다 | 경고 없이 a1을 들인다. 결과는 cancelled 본문에 `prev` {a의 D, 내 D}다 |
| U7 | D는 current다. a1: drop X. a2: X 이행(D withdrawn). main: D를 되돌렸다 | 끝난 판은 a2의 것이다. a2와 합치면 `.ait/X`도 바뀐다 | `pull D`는 3에서 멈추고 hint는 `pull D X`. 그대로 하면 D withdrawn과 X fulfilled를 받고 경고한다. validate를 지난다 |
| TF2 | D와 main의 future K5가 `specs/shared.yaml`을 함께 쓴다. a1: D를 cancel. main: D를 s-E로 바꿨다(K5도 다시 고정) | 끝난 판은 a1의 것이다. a1과 합치면 `.ait/D`만 충돌하고 shared.yaml은 s-E 그대로다 | 통과한다. D는 cancelled다. `warning: your change to 'D' is overridden: 'D' was cancelled in 'a1'`. 끝난 D는 명세 파일을 놓지 않는다. shared.yaml은 K5가 고정하므로 K5의 고정대로 그대로다 |
| PV1 | a: c-A 뒤 cancel. b: c-x, c-A 뒤 cancel. main: c-E(`om`에도 있다. origin/main 자리) | 끝난 판 둘의 본문이 같다. 결과 새 판의 `prev`는 최신 판 셋이다. 그 뒤 다시 보면 최신 판은 결과 판 하나다. 두 끝난 판 모두 내 판을 포함하지 않는다. git만으로 합치면 specs/d.yaml이 충돌한다(`rev9.log` PV1-9) | 고르기로는 멈추지 않는다. a2를 들인다(새 커밋 2개 < 3개). 경고 한 번. 내 c-E는 버린 판(끝난 쪽에 덮인 내 future)의 명세이므로 받지 않는다. a 쪽의 c-A는 그 쪽에서 끝난 판만 고정하므로 코드다. 그래서 specs/d.yaml은 c-A이고 충돌하지 않는다. 다음 pull은 경고 없이 `already D` |
| PV2 | a: cancel. b: c-B(`Ait-Agent: agent-b`). main: c-E | 끝난 쪽이 b와 내 판을 뺀다. 결과 새 판의 `prev`에 b의 판이 든다 | 내 판과 b의 판에 대한 경고 둘. agent-b에게 알림이 간다. 다음 pull은 경고 없이 `already D` |
| PV3 | D current, drop X. main: X 이행(D withdrawn). a: D의 consumer만 고쳤다 | 남는 판은 내 판(withdrawn)이고, 내 판은 a의 판을 포함하지 않는다 | `kept D`. HEAD 위의 새 판: withdrawn 본문에 `prev` {내 D, a의 D}. 다음 pull은 경고 없이 `already D` |
| TT1 | a: D cancel. b: D 이행, drop X, X 이행(D withdrawn). main: c-E | 끝난 쪽이 내 판을 뺀다. 남는 판은 서로 다른 끝난 판 둘이다 | 멈추고 고른다(결정 13 끝). `D=a1 -m <why>`이면 결과는 cancelled 본문에 `prev` {a, b, 내 판}이다. 사고 판이 아니다 |
| REV1 | main이 a1(D cancel)을 받았다. b(c-E)가 git merge로 main을 합치다 `.ait/D` 충돌을 손으로 풀어, c-E(future)에 `prev` {cancel 판, 자기 판}을 적었다(bM) | 그 판은 `prev`에 cancelled 판이 있는데 본문이 그와 다르다 | 사고 판이다(결정 21). `warning: skipping 'D' at 'b' (bM): …`. `already D`이고 D는 cancelled 그대로다 |
| REV2 | REV1 뒤에 b2가 그 판 위에 c-F를 썼다(`prev`는 REV1의 판) | REV1의 판은 사고 판이고, b2의 판은 그 판에서만 갈 수 있다. b2의 판은 cancel 판을 포함한다 | b2의 판도 사고 판이다(사고 판 위에 쌓은 판). 건너뛰고 경고한다. `already D`이고 D는 cancelled 그대로다 |
| REV3 | main은 cancel된 D만 가졌다. b1: 손으로 future로 바꿨다(사고). b2: 그 위에 조건을 고쳤다 | b2의 판은 cancel 판을 포함하지만, 사고 판 위에만 쌓여 사고 판이다. 그래서 최신 판은 main의 cancel 판 하나다 | b2의 판을 건너뛰고 경고한다. `already D` |
| REV4 | main은 cancel된 D(C)만 가졌다. b1: 손으로 future로 바꿨다(A, 사고). b2: init에서 고친 정상 판 M을 합치며 `prev` {A, M}인 판 V를 손으로 적었다 | V는 정상 판 M에서 갈 수 있지만, A를 거쳐 cancel 판 C를 포함하고 본문은 future다 | V는 사고 판이다(끝난 판을 포함하는데 끝난 판이 아니다). 건너뛰고 경고한다. `already D`이고 D는 cancelled 그대로다 |
| REV5 | main은 cancel된 D(C)다. m1은 init에서 조건을 고친 정상 future 판 M이다. b1은 `prev` {C, M}에 C와 본문이 다른 cancelled 판 V를 손으로 적었다. m ref는 지웠다 | V는 M에서 future → cancelled로 갈 수 있고 자신도 포함하지만, 자신을 뺀 선행 끝난 판 C와 본문이 다르다 | V는 사고 판이다. 자신과 본문이 같다는 것으로 종결 검사를 통과하지 않는다. 건너뛰고 경고하며 `already D`이고 D는 C 그대로다 |

### 3.4 한쪽만 바뀌었으면 고르지 않는다 (결정 15)

| # | 이력 | git에서 확인한 것 | 기대 |
| --- | --- | --- | --- |
| T1 | U1에서 `D=a1`을 고른 뒤 a가 D를 c-A2로 더 고쳤다(a2) | 최신 판 2개. 공통 조상 D@A에서 내 판은 그대로(c-A)이고 a2만 바뀌었다. 남는 판은 a2의 것 하나다 | 묻지 않고 a2를 들인다. 결과는 c-A2 본문에 `prev` {a2의 D, 내 D} |
| T1b | U1b(`D=HEAD`) 뒤 a가 c-A2로 더 고쳤다 | 공통 조상 D@A에서 둘 다 바뀌었다(c-E, c-A2) | 멈추고 고른다 |
| X1 | c가 Y와 Z를 고쳤다. a1(c 위)이 Y와 D를, b1(c 위)이 Z와 O를 더 고쳤다. main도 Y와 Z를 각자 고쳤다. `pull D O` | D와 O는 남는 판이 하나씩이다. Y는 a1의 것과 내 것, Z는 b1의 것과 내 것으로 둘 다 바꿨다. a1과 b1을 차례로 합치면 계약 파일과 명세 파일만 충돌하고 코드는 충돌하지 않는다 | 3에서 Y, Z 지정을 요구한다. 이어 Y와 Z를 한 번씩 고른다(예: `pull D O Y=a1 Z=b1 -m <why>`). a1과 b1을 함께 받는 커밋 하나다. c가 고친 중간의 Y와 Z는 따로 묻지 않는다. specs/y.yaml과 specs/z.yaml은 고른 판의 고정대로다. 받지 않는 변경을 결과의 고정으로 바꿔 넘기므로, b1의 merge base(c1)에서도 충돌하지 않는다 |
| MS2 | y1: c-K. q1: c-M, q2: y1을 보고 `D=HEAD`(c-M, `prev` {M, K}). p2: q1과 y1을 보고 `D=y1`(c-K, `prev` {M, K}). 그 뒤 q를 squash했다 | 같은 저장소에서는 남는 판 2개(가장 나중 공통 조상 M, K의 본문이 다르다). 새 clone에는 M의 blob이 없고, 그래도 남는 판은 2개다 | 같은 저장소와 새 clone 모두 멈추고 고른다. blob이 없다고 한쪽을 버리지 않는다 |
| MS3 | C의 `prev`는 저장소에 없는 blob이다. main: C와 본문이 같은 V(`prev` C). a1: 조건을 고친 W(`prev` C) | 가장 나중 공통 조상은 C 하나이고 읽을 수 있다. 없는 blob은 그보다 옛 조상이다. W가 V에서 앞으로 나갔다 | 멈추지 않는다. a1을 들이고 결과는 W 본문에 `prev` {V, W}다 |

### 3.5 어느 커밋을 들이나

| # | 이력 | git에서 확인한 것 | 기대 |
| --- | --- | --- | --- |
| G1 | main은 b의 D@B를 받아 두었다. a: D를 c-A로. b가 a를 merge하고 충돌한 D를 c-B로 정했다(bM) | a1을 git merge하면 `.ait/D`와 `specs/d.yaml`이 충돌한다. 남는 판은 bM의 것 하나(새 커밋 2개)이고, bM과는 충돌 없이 합쳐진다 | bM을 들인다 |
| G2 | b와 f가 각자 a를 merge하고 충돌한 D를 c-A로 정했다(D@B', D@F'). main: D0 | 두 판은 본문이 같고 공통 조상 D@A에서 둘 다 그대로다. 남는 판 2개, 본문 1개. 들이는 커밋은 bM(4개), fM(3개) | 멈추지 않는다. fM을 들이고, `.ait/D`는 그 본문에 `prev` {D@B', D@F'} |
| G3 | main은 a의 D@A를 받아 두었다. b가 D를 c-B로 고친 뒤(b1) a를 merge하고 c-B로 정했다(bM) | b1과 merge하면 충돌하고 bM과는 합쳐진다. 남는 판은 bM의 것 하나다 | bM을 들인다 |
| G4 | a와 main이 각자 같은 조건 c-A로 D를 바꿨다 | 같은 blob이다 | `already D` |
| G4b | a는 c-x를 거쳐 c-A로, main은 D0에서 바로 c-A로 바꿨다 | 남는 판 2개, 본문 1개 | 들이지 않는다. HEAD 위의 커밋: 같은 본문에 `prev` {두 판}. `kept D` |
| TIE | a는 x를 거쳐 same으로, b는 y를 거쳐 same으로 바꿨다. 각자 다른 코드 파일을 더했다 | 남는 판 2개, 본문 1개. 두 판 모두 내 판을 포함한다. 두 들이는 커밋의 새 커밋 수가 2개로 같다 | 결정 30으로 가려지지 않으므로 커밋 OID가 작은 쪽 하나를 들인다. 다른 쪽의 코드는 들어오지 않는다 |
| CP1 | a1: D@A. b: 코드 커밋 b0 위에 a1을 cherry-pick했다(b1, 같은 blob) | 들이는 커밋은 b1(2개)과 a1(1개) | a1을 들인다 |
| M1 | a: D@B, 그 위에 D@B2. 그 뒤 a의 이력을 다시 써서 D@B를 가진 커밋이 없다. main: D0 | 같은 저장소에는 D@B의 blob이 남아 D@B2가 D0을 포함한다. 새 clone에서는 사슬이 끊긴다 | 같은 저장소에서는 D@B2를 들인다. 새 clone에서는 멈춘다(멈추는 쪽으로만 틀린다) |
| KB1 | G1에서 `pull D=a1 -m <why>` | a1의 판은 남는 판이 아니다 | 멈춘다. 갈라져 있지 않으므로 hint는 `pull D` |
| KB2 | 고르기 hint(`D=a1` 또는 `D=b1`)를 받은 뒤, b가 a를 merge해 c-B로 정했다(bM). main이 `pull D=a1 -m <why>` | 남는 판은 bM의 것 하나다 | 멈춘다. hint는 `pull D` |
| KB3 | KB2 뒤에 main이 bM을 받았다. `pull D=HEAD -m <why>` | 남는 판이 내 판 하나다 | `already D`. 새 판을 만들지 않는다 |
| IND2 | a: D를 이행했다(current). main: D를 c-E로. main이 `pull D=a1 future amend D -m <why>`. 그 뒤 init에 있는 다른 clone이 `pull D` | pull이 정한 중간 판의 blob은 다른 clone에 없다. 결과의 `prev`가 중간 판뿐이면 clone은 남는 판 셋(a, main의 옛 판, 결과)으로 갈라진다. 중간 판의 `prev`({a의 D, main의 D})를 함께 담으면 남는 판은 결과 하나다 | 결과 판의 `prev`는 중간 판과 그 `prev`({a의 D, main의 D}) 모두다. 다른 clone은 멈추지 않고 결과를 받는다 |
| IND3 | a1이 D를 이행, main은 c-E. A: `pull D=a1 future amend D -m <why>`. B: `pull D=a1 -m <why>`. 둘이 서로 받는다. 또 B가 없는 clone이 받는다 | B의 결과는 A의 중간 판과 같은 blob이다. A의 판은 그 blob과 그 `prev`를 모두 담는다. B가 없는 clone에는 그 blob이 없다 | A는 `already D`, B는 A의 판을 그대로 받는다. B가 없는 clone도 A의 판을 받는다 |
| IND4 | a1: D를 이행했다(C). main: c-E(E). `pull D=a1 future amend D -m <why>` | 결과(future)는 부모 a1의 C를 되돌린다. 그 본문은 future 출발 판 E와 다르다 | 되돌린 커밋이다. `warning: 'D' is reopened by this pull`(이유를 붙이지 않는다)과 H10 |
| CP3 | a1과 b1이 각자 손으로 같은 D 판을 적었다(`Ait-Agent` agent-a, agent-b). main: c-E | 같은 blob이다. 들이는 커밋 후보는 a1과 b1(새 커밋 1개씩) | 고르기로 멈춘다. `D=HEAD -m <why>`를 고르면 agent-a와 agent-b 둘 다에게 알림이 간다. 알림에 보이는 커밋은 OID가 작은 쪽이다 |

### 3.6 들어오는 계약은 모두 지정한다 (결정 2, 7, 23)

| # | 이력 | git에서 확인한 것 | 기대 |
| --- | --- | --- | --- |
| N1 | a1: Q를 바꿨다. a2: D를 바꿨다. `pull D` | a2와 합치면 `.ait/Q`도 바뀐다 | 멈춘다. hint는 `pull D Q` |
| W1 | a0: 코드만 바꿨다(`src/util.c`). a1: D를 바꿨다 | 합치면 `src/util.c`도 들어오고, 바뀌는 계약은 D뿐이다 | 통과한다. 코드는 지정 없이 들어온다 |
| W2 | b: offer O2를 만들고 이행했다. 그 뒤 D@B | 합치면 `.ait/O2`도 바뀐다 | 멈춘다. hint는 `pull D O2` |
| K1 | a1: offer O를 만들고 D를 떠맡았다. a2: O와, O가 떠맡은 D를 함께 이행했다(1.6). `pull O` | 합치면 `.ait/D`와 `.ait/undertake/D-O`도 바뀐다. a2의 상태는 validate를 지난다 | 멈춘다. hint는 `pull D O`. 그대로 하면 통과한다 |
| K6 | O는 main의 offer다(init에서 future). a1: D를 O로 떠맡았다. a2: D를 고쳤다. main: O를 이행했다. `pull D` | 합치면 `.ait/D`와 떠맡기 기록이 바뀐다. 합친 상태는 O current, D future, 떠맡기 undertaken | 멈춘다. hint는 `pull D O`. 그대로 하면 4(5.9 d)에서 멈추고, hint는 `pull D O future amend O -m <why>`. 그대로 하면 통과한다 |
| K7 | main은 D와 O를 가졌다. a1: 떠맡기만 했다 | D와 O의 남는 판은 내 판이다. 떠맡기 기록 `D-O`의 남는 판은 a1의 것이다 | `pull D`는 D가 끝인 떠맡기 기록도 본다(결정 23). 3에서 멈추고 hint는 `pull D O`. 그대로 하면 a1을 들인다 |
| E1 | b1: D@B. b2: Q를 바꿨다. b3: b1을 git revert했다(b의 끝은 D0). `pull Q` | ref 끝만 보면 D는 내 판 하나다. b2가 가진 D@B가 후보가 되면 D@B가 내 D0을 포함한다 | 멈춘다. hint는 `pull D Q`. 그대로 하면 b2를 들이고 D는 D@B다 |
| E1b | E1에 더해 main이 D를 c-E로 바꿨다 | b2의 D@B를 후보로 더하면 D@B와 내 c-E가 갈라진다 | `pull D Q`는 고르기로 멈춘다. hint는 `pull D=b1 Q -m <why>` 또는 `pull D=HEAD Q -m <why>`(커밋 이름은 그 판의 들이는 커밋). 사용자가 `D=b2`를 주어도 b2의 판이 남는 판이므로 KB1에 걸리지 않는다 |
| K8 | c1: D를 O1로 떠맡았다. d1: D2를 O1로 떠맡았다(다른 브랜치). `pull D` | 떠맡기 기록 둘이 HEAD에 없다. D-O1의 끝 O1이 D2-O1의 끝이기도 하다 | 3에서 멈추고 hint는 `pull D D2 O1` 하나다(이름은 계약 ID 순)(이름이 더 늘지 않을 때까지 되풀이). 그대로 하면 통과한다 |
| CY1 | c: D를 O로 떠맡았다. a1(c 위): D를 고쳤다. b1(c 위): O를 고쳤다 | `pull D`만 하면 a1과 합쳐 떠맡기 기록이 들어온다. a1과 b1은 서로 조상이 아니다. 차례로 합치면 충돌이 없다 | `pull D`는 멈추고 hint는 `pull D O`. 그대로 하면 a1과 b1을 함께 들여 부모 셋인 커밋 하나다(`cycle3.log`: 5.9 통과, 셋 다 받음) |

### 3.7 옛 판, 지운 레코드, 사고 판, 형식이 틀린 판

| # | 이력 | git에서 확인한 것 | 기대 |
| --- | --- | --- | --- |
| RV2 | main이 b1(D@B)을 받았다. b가 b1을 git revert(b2)하고 Q를 바꿨다(b3). `pull Q` | HEAD는 b3의 조상이다(git이라면 fast-forward). b3의 D는 내 D@B에 포함된다(옛 판). 합치면 `.ait/D`와 `specs/d.yaml`이 옛것으로 돌아간다 | D는 들어오는 계약이 아니다. 통과하고 D와 그 명세 파일은 D@B 그대로다. 받는 결과가 b3의 tree와 다르므로 fast-forward가 아니라 병합 커밋이다 |
| DEL1 | a1: `.ait/Q`를 git으로 지웠다. a2: D를 바꿨다. `pull D` | a2에는 `.ait/Q`가 없다. 합치면 `.ait/Q`가 사라진다 | 통과한다. Q는 내 판 그대로 남는다. `warning: skipping the removal of 'Q' in 'a1': records are never removed` |
| RV3 | main이 a1(D@A)을 받은 뒤 git revert로 D0에 돌아갔다. `pull D` | 남는 판은 D@A이고, 그 들이는 커밋 a1은 HEAD의 조상이다 | 들일 커밋 없이 HEAD 위의 커밋으로 D@A와 그 명세 파일을 적는다. `included D from 'a1'` |
| RV4 | D·Q가 shared.yaml을 함께 고정한다. a1은 s-A, b1은 s-B로 둘을 고쳤다. main이 둘을 합쳐 s-M으로 해결한 뒤 그 merge를 git revert했다. `pull D=a1 Q=b1 -m <why>` | a1·b1 모두 HEAD의 조상이므로 들이는 끝은 없다. D의 고정 s-A와 Q의 고정 s-B는 한 파일에서 함께 성립할 수 없다. 같은 blob의 서로 다른 mode도 같다 | 4에서 `E_INCOMPLETE`로 멈추고 아무것도 바꾸지 않는다. 고르기 hint는 `pull D=a1 Q=a1 -m <why>`와 `pull D=b1 Q=b1 -m <why>`뿐이다. `--force`로 넘기지 못한다. 같은 쪽을 고르면 HEAD 위의 요청 커밋으로 그 파일을 복원하며 merge 상태는 만들지 않는다(결정 35) |
| R1 | a1: D를 cancel했다. a2: 손으로 future로 바꿨다(`prev`는 a1의 판) | a의 판은 사고 판이다(cancelled에서 future로 가는 짝이 없다) | `warning: skipping 'D' at 'a' (a2): …`. 남은 후보로는 `already D` |
| R2 | a1: D@A. main이 a1을 받았다. a가 git revert a1을 했다 | 내 D@A가 a의 D0을 포함한다 | `already D`. revert는 a 브랜치만 옛 상태에 둔 것이다 |
| R3 | R2 뒤에 a가 revert를 무르고(a3), ait로 조건을 c0으로 다시 바꿨다(a4) | a4의 판이 내 D@A를 포함한다 | a4를 들인다. a가 거둔 것이 이렇게 전해진다 |
| CK1 | a1: 손으로 D를 current로 바꾸고 확인 행을 적지 않았다 | a의 판은 사고 판이다(확인 행이 없다) | 건너뛰고 경고한다. `already D` |
| CK2 | a1: `-r`로 D를 이행해 확인 행을 적었다. 그 뒤 메시지를 amend로 다시 써서 기록줄이 사라졌다 | 기록줄은 없지만 계약 파일의 확인 행은 PINS가 맞다 | 통과한다. a1'을 들인다 |
| CK3 | D는 current다. a1: 손으로 명세 파일과 고정을 바꾸고 옛 확인 행을 그대로 두었다 | 확인 행의 PINS가 지금 고정과 맞지 않는다. 사고 판이다 | 건너뛰고 경고한다. `already D` |
| MF1 | a1: `.ait/D`를 CRLF 줄 끝으로 적었다 | 형식이 틀린 판이다 | `warning: skipping malformed '.ait/D' at 'a1'`. 남은 후보로는 `already D` |
| ACC1 | D current, drop X. a1: X 이행(D withdrawn). a2: `.ait/D`를 손으로 current로 바꿨다. a3: Q를 바꿨다. `pull Q` | a3을 들이면 X(fulfilled)가 들어온다. a의 D는 사고 판이라 빠진다. 받는 결과(X fulfilled, D current)는 5.9 d를 어긴다 | `pull Q`는 3에서 멈추고 hint는 `pull Q X`. 그대로 하면 4에서 멈추고, 사고 판 때문에 받을 수 없다는 오류를 쓴다. hint는 `'D' has to be fixed on branch 'a' first`다 |
| ID1 | a2: 새 계약 W를 확인 행 없이 current로 썼다. main에는 W가 없다. `pull W` | W의 후보는 사고 판 하나뿐이다 | `E_ID`가 아니다. 건너뛰고 경고한 뒤 `error: 'W' at 'a' (a2) was changed outside ait's rules`, `hint: 'W' has to be fixed on branch 'a' first` |
| MF2 | a0: Z를 CRLF로. a1: Y를 손으로 current로(확인 행 없음). a2: 새 W(확인 행 없는 current)와 `.ait/notes.txt`. a3: Q. `pull Q` | 합치면 `.ait/Z`, `.ait/Y`, `.ait/W`, `.ait/notes.txt`, specs/z.yaml, specs/w.yaml도 바뀐다. Z는 형식이 틀린 판, Y와 W는 사고 판이다. a3에서 Z와 W가 각각 specs/z.yaml, specs/w.yaml을 a의 내용으로 고정한다 | 통과한다. Q만 받는다. Z, Y는 내 판, W와 notes.txt는 없음으로 둔다. 버린 것마다 경고한다(`warning: skipping '.ait/notes.txt' in 'a2': not a contract file` 등). specs/z.yaml과 specs/w.yaml은 a 쪽에서 버린 판이 고정한 명세이므로 받지 않는다(`rev9.log` MF2-9). z.yaml은 내 Z의 고정대로, w.yaml은 없음으로 둔다 |

### 3.8 명세 파일은 pull이 정한다

| # | 이력 | git에서 확인한 것 | 기대 |
| --- | --- | --- | --- |
| KS1 | a1: Q의 명세 파일에 input을 더했다. main: Q를 q-E로 바꾸고 `Q=HEAD`를 골랐다. a2: D를 바꿨다. `pull D` | Q는 내 판 그대로다(들일 것이 없다). git으로 합치면 `specs/q.yaml`이 충돌하고, 합친 결과는 Q의 고정과 다르다 | 통과한다. `specs/q.yaml`은 Q의 고정대로 둔다(a1의 Q 판이 그 내용을 고정했고, 그 판은 버렸다). `tests/qa.sh`는 코드로 함께 온다 |
| S1 | D와 main의 future K5가 `specs/shared.yaml`을 함께 쓴다(K5는 a가 갈라진 뒤에 만들었다). a1이 그 파일을 고쳤다 | 합친 shared.yaml이 K5의 고정과 다르다 | 통과한다. K5는 같은 커밋에서 다시 고정된다(6.2) |
| SF1 | D와 Q가 `specs/shared.yaml`을 함께 쓴다. a1과 b1이 그 파일을 다르게 고치고 둘을 다시 고정했다. `pull D Q` | 두 계약 모두 남는 판이 2개다. a1과 b1을 함께 합치면 `.ait/D`, `.ait/Q`, shared.yaml이 충돌한다. a1만 합치면 충돌이 없다 | 멈춘다. hint는 `pull D=a1 Q=a1 -m <why>`, `pull D=b1 Q=b1 -m <why>`를 먼저, 그다음 섞은 고르기 둘이다. 섞어 고르면 받는 두 변경이 충돌하므로 코드 충돌이다(결정 29, SF2). 풀어 마치면 validate를 지난다(`flows.log` SF1) |
| CP2 | init에서 tests/d.sh는 코드다(t0). a0: tests/d.sh를 tA로. a1: Q를 바꿨다. main: D가 tests/d.sh를 input으로 고정했다(t0). `pull Q` | git이 합친 tests/d.sh는 tA다. 들이는 이력의 어느 판도 tests/d.sh를 tA로 고정하지 않았다 | 통과한다. tests/d.sh는 tA이고, future D는 tA로 다시 고정되어 알림이 간다(6.2). D가 current면 5에서 멈춘다(S2와 같다) |
| RN1 | a: D를 cancel하고 `specs/d.yaml`을 `src/moved.c`로 옮겼다. main: 명세의 한 행을 고치고 D를 다시 고정했다 | git의 결과에서 `.ait/D`만 충돌하고, `src/moved.c`에는 main이 고친 행이 들어 있다 | 통과한다. D는 cancelled이고 명세 파일을 놓지 않는다. 내 변경이 덮였다는 끝난 쪽 경고를 쓴다. 내 명세 변경은 끝난 쪽에 덮인 내 판의 명세이므로 받지 않는다(PV1과 같다). 그래서 `src/moved.c`는 a의 내용이다(git만으로 합치면 내 행이 들어온다) |
| SP1 | a1: 보통 커밋으로 Q가 고정한 `specs/q.yaml`을 고쳤다(다시 고정하지 않음). a2: D를 바꿨다. `pull D` | a2에서 Q는 그 파일을 q0으로 고정하는데 파일은 q1이다. git이 합친 결과는 q1이다 | 통과한다. q1은 들이는 쪽의 사고이므로 받지 않고 `specs/q.yaml`은 Q의 고정(q0)대로 둔다. `warning: skipping 'specs/q.yaml' at 'a1': it was changed outside ait's rules` |
| CP4 | init에서 tests/d.sh는 코드다(t0). a0: tA로. a1: Q를 바꿨다. main: tE로 쓰고 D가 input으로 고정했다. `pull Q` | tests/d.sh가 git에서 충돌한다. 들이는 쪽에는 그 파일을 선언한 계약이 없다 | 코드 충돌이다. D가 future면 git처럼 멈추고, 마치는 커밋에서 D가 해결한 내용으로 다시 고정된다(v14 7.1.3). D가 current면 5에서 멈추고 hint는 `pull Q future amend D -m <why>`뿐이다 |
| SP2 | a의 D가 tests/d.sh를 새로 고정하며 1행을 고쳤다. main은 같은 파일의 9행을 코드로 고쳤다. `pull D` | git이 합친 파일에 두 변경이 다 있다. D의 고정(a의 내용)과 다르다. 내 쪽에는 그 파일을 고정한 계약이 없다 | 통과한다. 합친 내용을 두고 D(future)를 그 내용으로 다시 고정한다. 내 9행이 사라지지 않는다. D가 current였으면 5에서 멈춘다(결정 10) |
| SP3 | a의 D가 tests/d.sh를 내용은 그대로 두고 새로 고정했다. main은 그 파일을 코드로 고쳤다. `pull D` | 합친 내용은 main의 것이고 D의 고정과 다르다 | 통과한다. 합친 내용을 두고 D를 다시 고정한다. main의 변경이 사라지지 않는다 |
| SP4 | init에서 tests/f.sh는 코드다. a의 D가 그 파일을 새로 고정하며 1행을, main의 K가 새로 고정하며 9행을 고쳤다. `pull D` | git이 합친 파일에 두 변경이 다 있다. D의 고정과도 K의 고정과도 다르다. D는 a의 판을 받고 K는 내 판이다 | 통과한다. 합친 내용을 두고 D와 K를 모두 다시 고정한다. 어느 쪽의 변경도 사라지지 않는다 |
| DL1 | init에서 tests/d.sh는 코드다. main의 D가 그 파일을 새로 고정했다. a가 그 파일을 코드로 지우고 Q를 바꿨다. `pull Q` | git이 합친 결과에서 tests/d.sh가 지워졌다. a 쪽에는 그 파일을 선언한 계약이 없다 | 통과한다. tests/d.sh는 지우지 않고 D의 고정대로 둔다. `warning: skipping the removal of 'tests/d.sh' in 'a1': 'D' pins it` |
| DL2 | init에서 tests/d.sh는 코드다. main: 그 파일 9행을 고치고 D가 input으로 고정했다. a1: 그 파일을 지우고 Q를 바꿨다. `pull Q` | merge-tree는 `CONFLICT (modify/delete)`다 | 통과한다. 지우는 변경은 먼저 빼고 경고한다. 남는 변경은 내 것 하나이므로 충돌이 아니다. D가 current여도 5에서 멈추지 않는다. tests/d.sh는 내 내용이다 |
| SP5 | U1에서 `pull D=a1 -m <why>` | 결과 새 판 V는 D@A와 다른 blob이고, specs/d.yaml의 고정은 같다. merge-tree에서 specs/d.yaml이 충돌한다 | specs/d.yaml은 c-A다(a 쪽은 결과의 고정, 내 c-E는 버린 판의 명세). D를 다시 고정하지 않는다 |
| SP6 | T1(a2의 c-A2가 앞으로 나감). `pull D` | 결과 새 판은 a2의 D와 다른 blob이다. merge-base(HEAD, a2)는 a1이고, 내 쪽 specs/d.yaml은 그대로다 | specs/d.yaml은 c-A2다. 다시 고정하지 않는다 |
| SP7 | E1에서 `pull D Q` | ref 끝 b3의 specs/d.yaml은 공통 조상과 같다. 들이는 커밋 b2의 것은 c-B(결과의 고정)다 | 들이는 쪽은 b2다. specs/d.yaml은 c-B다 |
| SP8 | init의 D가 specs/shared.yaml을 고정한다. a1: D를 cancel하며 그 파일을 s-A로. main: 새 K5가 그 파일을 고정한다(s0). `pull D` | a1의 cancelled D에 `file specs/shared.yaml … s0` 행이 남는다. merge-tree는 충돌 없이 s-A다 | 끝난 판은 고정하지 않으므로 a의 변경은 코드다. 사고 경고 없이 받고 K5를 s-A로 다시 고정한다. K5가 current면 5에서 멈춘다 |
| SF2 | SF1에서 `pull D=a1 Q=b1 -m <why>` | 받는 두 변경(D의 고정 s-A, Q의 고정 s-B)이 shared.yaml에서 충돌한다 | 코드 충돌로 git처럼 멈춘다(결정 29). 마치면 D와 Q가 푼 내용으로 다시 고정되고 알림이 간다. 둘 중 current가 있으면 5에서 멈추고 되돌리기 hint를 준다 |
| SF3 | main은 Y future가 y.yaml p0를 고정한 init이다. common에서 p1, a1에서 Y의 명세 p2와 D를 고쳤다. b1은 common에서 갈라져 Y를 cancelled로 끝내고 Q와 y.yaml p3를 고쳤다. `pull D Q Y` | 결과 Y는 cancelled다. a1의 p2는 버린 판의 명세라 init p0로 대체하고, b1의 p3는 코드로 받는다. 뒤의 git merge base는 common p1이므로 p1→p0와 p1→p3가 충돌한다 | git의 코드 충돌로 멈추고 사용자가 해결한다. base를 p0로 바꾸어 p3를 자동으로 받지 않는다. Y는 cancelled이며 해결 커밋도 끝난 판을 되살리지 않는다(결정 37) |
| SP4c | SP4와 같은데 a의 D와 main의 K가 tests/f.sh의 같은 줄을 다르게 고쳤다 | 두 쪽 모두 결과의 고정이고 git에서 충돌한다. D의 남는 판은 a의 것 하나다(고를 것이 없다) | SF2와 같다. 코드 충돌로 git처럼 멈추고, 마치면 D와 K를 푼 내용으로 다시 고정한다 |
| RN2 | a1: specs/d.yaml을 specs/d2.yaml로 옮기고 6행을 고쳤다(D@A가 d2.yaml을 고정). main: specs/d.yaml 1행을 c-E로(D@E). `pull D=a1 -m <why>` | git만으로 합치면 specs/d2.yaml에 main의 `body: c-E`가 rename을 따라 들어와 D@A의 고정과 다르다 | 내 쪽의 c-E는 버린 판 D@E의 명세이므로 먼저 뺀다. 그 뒤 git이 rename을 따라 합치면 specs/d2.yaml은 D@A의 고정대로다. D를 다시 고정하지 않는다 |
| SP9 | init에서 tests/d.sh는 코드(t0). main: D가 그 파일을 input으로 고정했다. a1: tests/d.sh 1행(코드)과 Q. b1: Q2만. `pull Q Q2`. a1과 b1의 커밋 ID 순서를 바꿔 두 번 | 두 차례 모두 merge base는 init이다. a 쪽의 변경은 a1에서 아무 판도 고정하지 않는다(`rev9.log` SP9) | 커밋 ID와 상관없이 tests/d.sh는 `t1 A`다. D(future)는 6.2로 다시 고정된다(CP2와 같다). 쪽마다 한 번만 보므로 사고 경고는 없다 |
| SP10 | a1: D를 c-A로(ait). a2: 보통 커밋으로 specs/d.yaml을 c-X로(사고). a3: Q. `pull D Q` | 들이는 끝은 a3 하나다. a3의 D는 a1의 판(c-A)이고 파일은 c-X다 | specs/d.yaml은 c-A다. a2의 사고를 경고한다. D를 다시 고정하지 않는다. D가 current여도 5에서 멈추지 않는다. `pull D` 뒤 `pull Q`와 결과가 같다 |
| SP11 | SP10과 같은데 a1의 D가 tests/t.sh를 새로 고정하며 1행을 A로, a2가 보통 커밋으로 1행을 X로 고쳤다. main은 9행을 코드로 고쳤다 | a 쪽의 사고를 그 쪽 고정(1행 A)으로 보고 합치면 1행 A와 9행 E가 함께 남는다(`rev9.log` SP11) | tests/t.sh는 1행 A, 9행 E다. D(future)를 그 내용으로 다시 고정한다 |
| SP12 | a, b, c의 새 계약 A, B, C가 tests/shared.sh를 고정하며 각각 1, 10, 20행을 고쳤다. `pull A B C` | 세 변경 모두 결과의 고정이다. git이 셋을 차례로 합치면 충돌이 없다 | 세 변경이 모두 남는다. A, B, C를 합친 내용으로 다시 고정한다 |
| SP13 | U1에 더해 main의 새 K5가 specs/d.yaml을 함께 고정하고 c-E 때 함께 다시 고정됐다. `pull D=a1 -m <why>` | 내 c-E는 K5(결과)가 고정하고, a의 c-A는 D(결과)가 고정한다. git에서 충돌한다 | 둘 다 받으므로 코드 충돌이다(결정 29). 버린 D@E의 명세라도 결과의 다른 계약이 같은 내용을 고정하면 받는다 |
| MF3 | main의 D가 tests/f.sh를 고정한다(base). a: Q와, state 행이 둘인 형식이 틀린 W. W의 `file` 행은 tests/f.sh를 a의 내용으로 고정한다. `pull Q` | W는 형식이 틀린 판이다(`state future`, `state cancelled`) | state와 상관없이 W의 `file` 행을 센다. a 쪽의 tests/f.sh 변경은 버린 판의 명세이므로 받지 않는다. tests/f.sh는 base다 |

| MD1 | main의 future D가 실행 가능한 tests/mode.sh를 input으로 고정한다(100755). a1은 Q를 고치고 그 파일의 mode만 100644로 바꿨다. `pull Q` | blob은 같지만 mode가 D의 고정과 다르다. git은 mode 변경을 받는다 | mode도 명세 파일 고정이다(2.4.1). 사고를 경고하고 그 쪽의 고정으로 보므로, Q는 받고 tests/mode.sh는 원래 mode와 blob 그대로다 |
| MD2 | MD1과 같은데 a1은 tests/mode.sh의 내용도 바꿨다 | blob과 mode 모두 고정과 다르다 | 사고를 경고하고 mode와 blob을 함께 고정대로 복구한다. blob만 복구하며 실행 권한을 없애지 않는다 |

### 3.9 구조 검사

| # | 이력 | git에서 확인한 것 | 기대 |
| --- | --- | --- | --- |
| N5 | D는 current다. main의 current offer OE가 D를 떠맡았다. a1: D를 되돌렸다. `pull D` | 합치면 `.ait/D`만 바뀐다(D future) | 멈춘다(5.9 d). hint는 `pull D future amend OE -m <why>`. 그대로 하면 통과한다 |
| XT1 | D current, drop X. a1: X 이행(D withdrawn). main: X를 cancel했다. `pull D X` | D의 최신 판은 a1의 withdrawn 하나다(내 판을 포함한다). X는 끝난 판 둘(fulfilled, cancelled)이고 본문이 다르다 | 고르기로 멈춘다. `X=HEAD`는 withdrawn D와 cancelled X로 4를 지나지 못하므로, hint는 `pull D X=a1 -m <why>` 하나다 |
| XT2 | c1: D-O. a1: D c-A, x.c. d1: D c-D. b1: O, x.c. main: x.c. `pull D O` | D가 갈라졌다. `D=a1`이면 어느 차례로도 x.c가 두 번 걸리고, `D=d1`이면 한 번이다 | 고르기 hint는 `D=a1`과 `D=d1`을 모두 준다. `D=a1`을 따르면 4에서 결정 20의 상태로 멈춘다 |
| XT3 | XT2와 같은데 d1도 x.c를 바꿨다 | 두 선택 모두 어느 차례로도 x.c가 두 번 걸린다 | 고르기 hint는 두 선택을 준다. 따를 hint가 없는 멈춤이 아니다 |

### 3.10 확인 검사

| # | 이력 | git에서 확인한 것 | 기대 |
| --- | --- | --- | --- |
| C1 | a1: D를 이행했다(stock.c 1행, 고정 gA, 확인 행). main은 stock.c를 건드리지 않았다 | 합친 stock.c가 gA다 | 통과한다. D는 current이고 미확인이 아니다 |
| C2 | C1과 같은데, main이 stock.c의 9행을 바꿨다 | 충돌 없이 합쳐지지만 합친 stock.c가 gA가 아니다 | 멈춘다(결정 10). hint는 `pull D future amend D -m <why>` 또는 `pull D --force -m <why>` |
| F2 | C2에 더해 둘이 `src/other.c`를 다르게 바꿨다(그 파일을 확인하는 계약은 없다) | other.c가 충돌하고, 합친 stock.c가 gA가 아니다 | 멈춘다. hint는 `pull D future amend D -m <why>`뿐이다. 코드가 충돌할 pull에는 `--force`를 받지 않기 때문이다 |
| C3 | C1과 같은데, main이 1행을 다르게 바꿨다 | `src/stock.c`가 충돌한다 | 멈춘다. hint는 `pull D future amend D -m <why>`뿐이다. 그대로 하면 D는 future로 들어오고, 코드 충돌에서 git처럼 멈춘다 |
| C4 | a1: D(future)와 `src/other.c`를 바꿨다. main: other.c를 다르게 바꿨다. other.c를 확인하는 계약은 없다 | `src/other.c`만 충돌한다 | git처럼 멈춘다. 계약 파일과 명세 파일은 pull이 정한 대로 둔다. 받는 쪽이 해결해 마친다 |
| S2 | S1과 같은데 K5가 current다 | S1과 같다 | 6.2의 다시 고정이 K5의 고정을 바꾸므로 멈춘다. hint는 `pull D future amend K5 -m <why>` 또는 `pull D --force -m <why>` |
| S3 | main의 current K6이 `src/stock.c`를 고정한다(g0). a1이 D를 이행하며 stock.c의 1행을 바꿨다 | 합친 stock.c가 K6의 고정 g0과 다르다 | 멈춘다. hint는 `pull D future amend K6 -m <why>` 또는 `pull D --force -m <why>` |
| S4 | S3과 같은데, K6을 main의 current offer O6이 떠맡았다 | S3과 같다 | 멈춘다. 첫 hint의 절은 `future amend K6 future amend O6`이다(5.9a: 되돌리는 D, 이어서 그것을 떠맡은 offer) |
| IND1 | D는 current이고 main의 current offer O가 떠맡았다. a1은 stock.c 1행을, main은 9행을 바꾸고 각자 D를 다시 확인했다 | D는 갈라졌다. `D=a1`이면 합친 stock.c가 a1의 고정과 다르다 | `pull D=a1 -m <why>`는 5에서 멈춘다. 첫 hint의 절은 `future amend D future amend O`다(5.9a) |
| CK4 | U6에서 `pull D=a1 -m <why>` | 결과 새 판에 a1과 같은 PINS의 `check reported` 행이 있다. 확인 행이 맞고 사고 판이 아니다 | a1을 들인다. 결과는 `check reported`다 |
| CK5 | D는 current이고 `check forced`다. a1: 같은 고정을 `-r`로 다시 확인했다. main: consumer만 고쳤다(`forced` 그대로) | a1의 본문은 D0과 같고 main이 앞으로 나갔다. 두 판의 PINS가 같다. main과 a가 각자 만드는 새 판은 바이트까지 같다 | main은 `kept D`, a는 main의 커밋을 들인다. 둘 다 결과는 `check reported`이고 같은 판으로 모인다(결정 22) |
| CK6 | a1은 `-r`, b1은 `--force`로 같은 코드로 D를 이행했다. main: D0 | 남는 판 2개, 본문 1개 | 멈추지 않는다. 결과 새 판은 `check reported`다(결정 22) |
| CK7 | D는 current이고 `check reported`다. a1: consumer만 고치고 확인 행을 손으로 `forced`로 적었다(같은 PINS, `prev`는 내 판) | 최신 판은 a1의 것 하나이고 내 판을 포함한다. 결정 22로 정한 확인 행은 `reported`다 | a1의 판을 그대로 두지 않는다. a1의 본문에 `check reported`, `prev` {a1의 판}인 새 판을 적고 a1을 들인다 |
| S5 | a가 갈라진 뒤 main에 만든 current K6이 src/stock.c를 고정한다(a1에는 K6이 없다). a1: D를 바꾸고 `src/stock.c`를 `src/inv.c`로 옮겼다. `pull D` | 받는 결과에 stock.c가 없고 inv.c가 있다. HEAD에는 inv.c가 없다 | 5에서 멈춘다. hint는 `pull D future amend K6 -m <why>`뿐이다 |
| RV3b | a1: D를 이행했다. main이 a1을 받은 뒤 `git revert a1`. `pull D` | 남는 판은 a1의 것이고 a1은 HEAD의 조상이다. HEAD의 stock.c는 그 고정과 다르다 | 5에서 멈춘다(D가 새 미확인). `error: 'D' at 'a1' checked code your branch no longer has: 'src/stock.c'`. hint는 `pull D future amend D -m <why>`와 `pull D --force -m <why>`. revert 커밋은 옛 판으로 돌아간 것이라 되돌린 커밋이 아니다(v14 1.5d의 옛 식으로는 되돌린 커밋이었다, `rev8.log` RV3b') |
| NU1 | a1: D를 이행했다. a2와 a3: 일반 커밋이 D의 코드를 차례로 바꿨다 | a1~a3의 `.ait/D`는 같은 blob이고 stock.c는 셋 다 다르다 | (알림) D의 받는 쪽에 a2와 a3의 미확인 사건 알림이 한 번씩 간다(결정 18) |
| NU2 | NU1에 더해 a4가 Q를 바꿨고, main이 stock.c 9행을 바꿨다. `pull D Q` | D는 a4에서 이미 미확인이다. 합친 stock.c는 a4의 것과도, 고정과도 다르다 | 막지 않는다. `warning: 'D' comes in unverified`. 합친 코드로 미확인 사건 알림이 하나 더 간다 |

### 3.11 들일 끝이 여럿 (결정 14, 20, 26)

| # | 이력 | git에서 확인한 것 | 기대 |
| --- | --- | --- | --- |
| CD1 | main은 src/x.c를 수정하고 D를 고쳤다. a는 src/x.c를 지우고 Q를 고쳤다. `pull Q` | git은 modify/delete 충돌을 알린다. 결과 tree의 x.c는 HEAD 내용 그대로이고 충돌 표지가 없지만 stages 1·2가 필요하다 | x.c도 pull이 쓰는 경로다. 로컬 변경이 있으면 받기 전에 멈춘다. 없으면 index에 git의 충돌 stages를 쓰고 merge 상태로 멈춘다. 파일 내용이 같다는 이유로 충돌을 버리지 않는다 |
| MH1 | `pull D Q`. D는 a1에서, Q는 b1에서 들인다 | 차례로 합치면 충돌이 없다 | 통과한다. 부모가 셋인 커밋 하나다 |
| MH2 | MH1과 같은데 a1과 b1이 `src/x.c`를 다르게 바꿨다(main은 x.c를 건드리지 않았다) | a1은 충돌 없이 합쳐지고, b1은 마지막에 `src/x.c`로 충돌한다. 그 상태를 중간 커밋 없이 만들 수 있다(HEAD 그대로, `UU src/x.c`) | 중간 커밋 없이 git처럼 멈춘다. 받을 끝은 a1과 b1 둘 다다 |
| MH3 | MH2를 해결해 마친 뒤 같은 명령을 다시 | 마치는 커밋의 부모는 HEAD, a1, b1이다. D와 Q 모두 들일 것이 없다 | `already D Q` |
| MH4 | MH2에서 바로 `git merge --abort` | HEAD와 작업 폴더가 pull 전 그대로다 | abort는 git의 일이다. 이 경우 아무것도 남지 않는다 |
| MH5 | D와 O는 future이고 연결은 undertaken이다. a1: O 이행(O와 D current), x.c=A. b1: D만 이행(consumer 다름), x.c=B. `pull D=b1 O -m <why>` | 한 끝은 코드 충돌 없이 합쳐지고, 다른 끝이 x.c로 한 번 충돌한다. 마친 결과(D는 b1의 것, O는 a1의 것)는 validate를 지난다 | 중간 커밋이 없으므로 5.9를 어기는 커밋이 생기지 않는다. 마치면 부모 셋 |
| MH6 | `pull D Q`. a1(D)은 x.c를, b1(Q)은 y.c를 바꿨다. main은 x.c와 y.c를 둘 다 바꿨다 | a1과 b1 모두 HEAD와 코드가 충돌한다. 충돌하는 파일은 서로 다르다 | 한 merge 상태로 받고 x.c와 y.c의 충돌로 git처럼 멈춘다(결정 26). 마치면 부모 셋 |
| MH7 | c1: D를 O로 떠맡았다. a1(c 위): D와 x.c. b1(c 위): O와 y.c. main: x.c와 y.c. `pull D O` | 두 끝 모두 내 코드와 충돌하지만 파일이 다르다. 한 merge 상태에 `UU src/x.c`, `UU src/y.c`가 담기고, 마치면 부모가 HEAD, a1, b1이다. 그 뒤 D, O, 떠맡기 기록 모두 들일 것이 없다 | git처럼 멈추고, 마치면 `already D O` |
| MH8 | MH7과 같은데 b1도 x.c를 바꿨다 | 한 끝을 합쳐 x.c가 충돌한 뒤, 다른 끝이 x.c를 또 바꾼다 | 어느 차례로도 x.c가 두 번 걸리므로 아무것도 바꾸지 않고 멈춘다(결정 26). D와 O는 함께만 받을 수 있으므로 hint는 2절 4의 목표 상태다(한 끝을 다른 끝 위로 rebase하거나, 내 브랜치가 한 끝과 깨끗하게 merge되게 한다. 두 상태 모두 `rev9.log` MH11과 scratchpad `r44`에서 받을 수 있음을 확인했다) |
| ORD1 | `pull D Q`. a1(D)은 x.c 1행을, b1(Q)은 x.c 8행과 y.c 1행을 고쳤다. main은 x.c 1행과 y.c 1행을 고쳤다. 커밋 ID는 a1이 작다 | 커밋 ID 순(a1, b1)이면 a1에서 x.c가 충돌하고, b1이 그 x.c를 또 바꾼다. b1, a1 순이면 y.c와 x.c가 한 번씩 충돌한다 | b1, a1 차례로 받아 한 merge 상태에서 git처럼 멈춘다. 커밋 ID 순 때문에 멈추지 않는다 |
| HK1 | MH7의 merge 상태에서 마치는 커밋 | index의 `.ait/D`, `.ait/O`, 떠맡기 기록은 사고 판이 아니고 출발 판(HEAD, a1, b1의 판)을 모두 포함한다 | hook은 그 계약 파일을 그대로 둔다. 누가 정했는지는 묻지 않는다 |
| HK2 | `pull D Q`가 코드 충돌로 멈췄다. b1(Q의 끝)은 Y를 손으로 current로 만든 판(사고)도 가졌다 | Y의 출발 판은 HEAD의 판과 b1의 사고 판이다. pull은 Y를 내 판으로 둔다. 내 판은 사고 판이 아닌 출발 판을 모두 포함한다 | 마치는 커밋의 hook은 b1의 Y를 출발 판에서 빼고 경고한다. Y는 내 판 그대로이고, 마치는 커밋은 사고 판을 만들지 않는다 |
| HK3 | a1: D cancel, x.c. main: D c-E, x.c. `pull D`가 x.c로 멈췄다. 해결하며 `.ait/D`를 c-E(future), `prev` {C, E}로 적었다 | 출발 판은 HEAD의 E(끝난 판을 포함하지 않음)와 a1의 C(cancelled)다. 그 판은 C를 포함하는 future 판이다(결정 28의 사고 판) | 마치는 커밋의 hook이 `E_STATE`로 막는다(7.1.3-4를 레코드마다 본다). hint는 v14 7.1.3-7의 복원(a1의 판으로)이다 |
| MH9 | a1: D c-A, x.c. b0: D c-B. b1(b0 위): Q, x.c. main: x.c. `pull D=a1 Q -m <why>` | a1, b1을 함께 받으면 어느 차례로도 x.c가 두 번 걸린다. `pull D=a1`을 받은 뒤에는 D를 들일 것이 없고(b0의 D@B가 결과에 포함된다), `pull Q`는 b1만 들여 x.c로 한 번 충돌한다 | 4에서 멈춘다. hint는 `take them one at a time: 'ait contract pull D=a1 -m <why>', then 'ait contract pull Q'`다. "함께만"이 아니다 |
| MH10 | CY1에 더해 c1이 x.c 5행을, main이 x.c 5행을 고쳤다. `pull D O` | a1과 b1 모두 c1의 x.c 변경을 담는다. 합친 결과와의 merge base(c1)에서 뒤의 끝은 x.c를 바꾸지 않는다. HEAD와의 merge base(init)에서는 바꾼다 | 한 merge 상태에서 x.c 충돌 한 번으로 git처럼 멈춘다. 마치면 부모 셋이다 |
| MH11 | D-O를 함께 받아야 한다. a1: D, x.c 1행, y.c 5행. b1: O, x.c 8행, y.c 1행. main: x.c 1행, y.c 1행. `pull D O` | 내 x.c는 a1과만, 내 y.c는 b1과만 충돌한다. 두 차례 모두 이미 충돌한 파일을 뒤의 끝이 또 바꾼다. b가 a1을 받으면(b2) 들이는 커밋은 b2 하나이고 x.c, y.c가 한 번씩 충돌한다 | 멈춘다. 문장은 두 파일을 모두 쓴다. hint는 2절 4의 목표 상태다. b가 b1을 a1 위로 rebase하면 끝이 하나가 되어 x.c와 y.c가 한 번씩 충돌하고, main의 y.c가 b1과 깨끗하게 merge되면 b1, a1 차례로 x.c만 한 번 충돌한다. b가 a1을 merge만 한 커밋은 끝을 바꾸지 않는다 |
| MH12 | c1: D-O. a1: D, x.c 1행. b1: O. q1(init에서): Q, x.c 1행. main: x.c 1행. `pull D O Q` | 셋을 함께 받으면 어느 차례로도 x.c가 두 번 걸린다. `pull D O`(a1, b1)는 x.c 한 번, 그 뒤 q1도 한 번이다 | 4에서 멈춘다. hint는 `take them one at a time: 'ait contract pull D O', then 'ait contract pull Q'`다 |
| MH13 | MH9와 같은데 b1의 커밋 ID가 a1보다 작다 | `pull Q`를 먼저 하면 b1이 D@B를 실어 와 3에서 멈춘다. `pull D=a1`을 먼저 하면 MH9와 같다 | hint는 MH9와 같다. 커밋 ID 순서 때문에 결정 20의 상태로 바뀌지 않는다 |
| HK4 | a1: D c-A가 tests/t.sh를 새로 고정(1행 A). b1: D c-B. main: D c-E, tests/t.sh 1행 E(코드). `pull D=a1 -m <why>`가 tests/t.sh로 멈추고, 풀어 마친다 | 결과 판의 `prev`는 D@A, D@B, D@E다. 출발 판은 HEAD와 a1의 판뿐이다. 극대 출발 판으로 다시 정하면 D@B가 빠진다 | 마치는 커밋은 D를 그 판 위에 다시 고정한다(`prev`는 그 판과 그 `prev`). 다음 `pull D`는 `already D`다 |
| HD2 | b의 HEAD에 REV1의 사고 판(future, `prev` {cancelled C, E})이 있다. b가 코드만 바꾼 브랜치 f를 git merge한다 | index의 D는 HEAD의 판 그대로다. f의 D는 D0이다 | 7.1.3-4를 지난다(index 판이 HEAD 판 그대로). 7이 `warning: '.ait/D' keeps an error from HEAD`를 쓴다. 상관없는 merge를 막지 않는다 |

### 3.12 작업 폴더와 shallow clone (2절 0)

| # | 이력 | git에서 확인한 것 | 기대 |
| --- | --- | --- | --- |
| WT1 | a1: D의 provider만 바꿨다. main: D의 조건을 c-E로 바꿨고, 작업 폴더의 `specs/d.yaml`에 커밋하지 않은 초안이 있다. `pull D=a1 -m <why>` | git merge는 시작된다(`.ait/D`만 충돌, specs/d.yaml은 건드리지 않음). pull은 specs/d.yaml을 D@A의 고정대로 써야 한다 | 아무것도 바꾸기 전에 멈춘다. specs/d.yaml은 HEAD의 D가 고정한 계약 경로이므로 hint는 `commit your changes with 'ait contract add specs/d.yaml' and 'ait contract commit -m <why>', or set them aside, before you pull`이다 |
| WT2 | main이 b1(D@B)을 받았다. b가 b1을 revert하고 Q와 x.c를 바꿨다(b3). a1: Q2. main: x.c를 다르게 바꿨고, specs/d.yaml에 커밋하지 않은 초안이 있다. `pull Q Q2` | D는 들일 것이 없다. pull이 쓰는 경로에 specs/d.yaml이 없다. b3은 마지막에 x.c로 충돌한다. pull이 쓰는 경로만 쓰면(`read-tree -m -u`) 초안이 남는다 | 0에서 멈추지 않는다. x.c의 코드 충돌로 git처럼 멈추고, specs/d.yaml의 초안은 그대로다 |
| NEW2 | a1: 새 계약 Q(`specs/q.yaml`). main: 추적하지 않는 `specs/q.yaml`이 있다 | git이 merge를 거절한다 | 아무것도 바꾸기 전에 멈춘다. hint는 `move or remove 'specs/q.yaml' before you pull` |
| IDX1 | main: 자기 커밋이 있고, `src/util.c`를 stage했다. `pull D`(fast-forward가 아니다) | git이 merge를 거절한다 | `E_DIRTY`(`uncommitted changes in the index`) |
| HD1 | main이 자기 D가 고정한 specs/d.yaml을 보통 커밋으로 고쳤다(다시 고정하지 않음). `pull Q` | HEAD의 D 고정과 HEAD의 specs/d.yaml이 다르다 | 0에서 멈춘다(HEAD의 오류, 5.10). 그 파일을 덮어 main의 변경을 버리지 않는다 |
| HD3 | HEAD의 D가 선언한 정의는 tests/data를 input으로 쓰지만 레코드에 input 행이 없다. 정의의 mode·blob 고정은 HEAD와 같다. `pull D` | 기존 고정은 모두 맞지만 정의가 펼치는 file 행 집합이 다르다(2.4.2만 위반) | 0에서 `E_INCOMPLETE`로 멈추고 아무것도 바꾸지 않는다. 5.10의 GOOD 복원 hint를 준다 |
| SS1 | a1: D의 명세를 고쳤다. HEAD는 a1의 조상이고 `pull D`는 fast-forward다. 모르는 session ID를 설정했다 | a1의 판은 내 판을 포함하고, 받는 tree가 a1과 같으므로 요청 커밋을 만들지 않는다. 이미 받은 뒤의 `pull D`도 요청 커밋이 없다 | 설정된 session이 무효이면 fast-forward와 `already` 모두 `E_SESSION`(128)로 멈추고 아무것도 바꾸지 않는다. `AIT_SESSION=-`이면 session 없이 fast-forward하거나 `already`로 끝난다(5.1) |
| SH1 | main은 a의 depth=1 clone이다. a: D를 고친 커밋 위에 코드 커밋 | clone에서는 a의 커밋이 하나만 보이고, 들이는 커밋을 실제와 다르게 고른다. merge-tree는 관계 없는 이력이라며 거절한다 | 멈춘다. hint는 `'a1' can be taken once this clone has its history down to the merge base with HEAD`다 |

## 4. 모든 테스트에서 확인하는 것

시점은 이렇게 고정한다. 명령을 시작할 때의 HEAD, ref, 객체를 쓴다. 2절 3의 되풀이를 마친 뒤의 최신 판 집합을 L(K), 남는 판 집합을 R(K)라 한다.

- **판 누락 없음.** 통과한 뒤 지정한 계약 K의 판은 시작 때의 내 판과 L(K)를 모두 포함한다.
  - 끝난 쪽이 이겨도, 3-way로 앞으로 나가도, 고르기를 해도 같다.
  - 코드 충돌로 멈춘 경우는 마치는 커밋에서 검사한다.
  - U1, U1b, G2, G4b, T1, NT3, KB3, CYC2, PV1~PV3, CK5, IND3, MH7에서 다시 pull하면 들일 것이 없음을 확인했다.
  - 마치는 커밋이 다시 고정해도 그 판 위에 얹으므로 같다(HK4).
- **변경이 알리지 않고 사라지지 않는다.** 어느 쪽의 변경이든 결과에 들어가거나, 사고나 지우기로 경고된다. 옛 판·이미 버린 판의 명세 변경은 받지 않는다. 그 판이 결과에 없기 때문이다(KS1, RV2, PV1, MF2, SP1~SP13, DL1, DL2). 버린 쪽에는 끝난 쪽 경고나 결정 16의 알림이 간다.
- **멈추면 아무것도 바뀌지 않는다.** 6의 코드 충돌과, 도우미 절의 확인 거절 때 6.5·8장대로 확인 과제 파일을 쓰는 것은 예외다. ref·HEAD·index·작업 폴더는 확인 거절로 바뀌지 않는다. abort는 git의 일이다.
- **들어온 계약은 모두 지정한 것이다.** 지정하지 않은 계약의 계약 파일은 바뀌지 않는다. 예외는 6.2의 다시 고정, 도우미 절이 이름을 댄 계약, `--force`로 다시 고정하는 확인 검사에서 막힌 current 계약이다(S1, S3, S4, IND1).
- **결과의 새 판은 형식이 틀린 판도 사고 판도 아니다.** 결과는 validate를 지나고, 새 미확인(9.1의 상태)이 없다.
- **hint를 따르면 다음 단계로 간다.**
  - 같은 거절을 되풀이하지 않는다. 받지 않을 선택(`--force`, 4를 지나지 못하는 고르기)은 hint에 없다.
  - hint의 명령은 원래 이름과 이미 고른 것을 모두 담는다. 결정 26으로 나눠 받으라는 hint만 예외다. 함께 받아야 하는 끝의 묶음마다 명령 하나이고, 앞 명령을 받은 뒤의 상태로 필요한 이름과 고르기만 담는다(MH9, MH12, MH13).
  - 고르기 hint를 따르면 같은 갈라짐으로 다시 멈추지 않는다. 고른 쪽이 그 뒤로 더 나아가도 다시 묻지 않는다(T1).
  - 멈추면 언제나 따를 hint나 목표 상태가 있다. "여기서 할 수 있는 일이 없다"는 쓰지 않는다(ACC1, ID1, MH8).
- **같은 경고가 되풀이되지 않는다.** 끝난 쪽 경고와 갈라짐은 그렇다. 건너뛰기 경고는 그 브랜치를 고칠 때까지 나온다(5.10).
- **멈춤의 횟수.** 0에서 멈추지 않은 pull에서, 고르기로 멈추는 것은 R(K)의 본문이 둘 이상이고 유효한 고르기가 없는 계약이 하나라도 있을 때와 정확히 일치한다.
  - 한 번의 멈춤에서 여러 계약을 함께 고르게 하므로, 멈춘 횟수와 갈라진 계약의 수는 다를 수 있다.
  - 무엇을 고르느냐에 따라 뒤의 갈라짐이 달라질 수 있다(r24 R07). 그래서 전체 횟수가 하나의 최소값과 같기를 요구하지 않는다.

## 5. 규격에서 바뀌는 것

- **0.5, 전제 표, 규칙 8, 10.1, 10.1b**
  - 0.5의 "운반·병합·충돌 해결은 git이 맡는다"를 고친다. 계약 파일은 pull이 정한다. 명세 파일은 pull이 받지 않을 변경(옛 판·버린 판·사고의 명세, 고정한 파일 지우기)만 정해 그 쪽에서 빼고, 병합과 충돌 해결은 코드와 함께 git이 맡는다(기록 107).
  - 전제 표의 "판정할 수 없는 동시 수정은 git의 충돌로 드러난다(7.13)"를 고친다. pull에서는 고르기로 드러난다.
  - 전제 표의 "계약 변경은 남의 코드 없이 받을 수 있어야 하므로 … 따로 만든다(규칙 3)"의 까닭이 결정 3으로 없어졌다. 규칙 3의 까닭을 다시 적는다.
  - pull은 이력째 받는다.
  - 들어오는 계약은 모두 지정한다.
  - 코드는 언제나 함께 들어온다.
  - "아직 이행되지 않은 남의 코드는 받지 않는다"를 지운다.
  - 10.1의 "7.12.1-4" 참조를 고친다.
- **확인 행 (결정 17, 22)**
  - 규칙 1, 규칙 4: 확인은 계약 파일의 확인 행(`check reported|forced <PINS>`)으로 남는다.
  - 1.5a: 확인된 고정은 PINS가 맞는 확인 행이다. 미확인의 조건은 "확인 행이 맞고 provider 코드가 고정과 다르다" 하나뿐이다. "확인된 고정이 아니다"와 "명세 파일이 고정과 다르다"(2.4.1)는 사고로 옮긴다. "그래서"의 기록줄 문장(복사, 조상의 기록)을 확인 행으로 고친다.
  - 2.2.2: `check` 행의 자리(`file` 행 다음, `creator` 앞). 2.2.3: 적힌 확인 행은 무시하고, 관문이 H 판의 것을 두거나 새로 쓴다. 5.9 a: 확인 행은 current D·O에 하나까지 있고(없거나 PINS가 틀리면 사고), future로 되돌리면 지운다. 2.1.2a: 레코드 형식이 바뀌므로 `version`을 정한다.
  - 확인 행을 쓰는 규칙: 같은 PINS를 `reported`로 확인한 판이 있으면 `reported`다. PINS가 바뀔 때만 이번 확인의 종류로 쓴다. 8.5와 7.7.4의 "`--force`면 확인 집합의 계약마다 `forced`"를 이렇게 고친다.
  - 6.5: 같은 내용을 같은 방법으로 다시 확인하면 계약 파일이 그대로다. 6.6: T가 H tree와 같으면 `noop`이다(빈 amend 조건을 뺀다).
  - 기록줄을 확인으로 다루는 곳을 모두 고친다: 5.3, 5.4, 5.14, 6.5 마지막 문단, 7.1.3-2·3(통합 hook이 되돌린 계약의 확인 행을 지우고, current로 남는 계약은 고정이 같은 출발 판의 확인 행을 둔다), 7.1.5, 7.7.4(철회되는 target과 되돌리는 계약은 확인 행을 지운다), 7.9.5(`verified`와 `check` 꼬리), 7.10.1-4, 7.13.1-4("prev와 확인 행은 빼고 본다"), 9.2(확인 행만 바뀐 것은 뜻이 같다), 9.7(payload), 11.1(fixup·squash로 확인이 사라진다는 문장), 13.4(조상의 기록줄로 확인된 고정을 이어 받는 설명).
  - `Ait-Check` 기록줄은 지운다(결정 34). 확인은 확인 행에만 남는다.
- **규칙 5, 1.6 (결정 19):** O를 나중에 이행할 때는 이미 current인 D의 과제도 결과에 있어야 한다. v14 그대로 둔다.
- **사고 판 (결정 8, 21, 기록 67)**
  - 규칙 2: "판은 사고 판이 아닌 `prev`에서 2.3의 전이로 갈 수 있다"는 규칙 2의 데이터 판이다. 규칙 2의 "통합에서도 각 레코드는 출발 판 중 하나(또는 없음)에서 …"와 함께 고친다.
  - 끝난 판을 포함하는 판은 본문이 그 끝난 판 중 하나와 같아야 한다(사고 판을 거쳐 포함하는 것도, 결정 28). 2.3.2와 7.1.3-4를 고친다. 7.1.3-4는 레코드마다 본다. index 판이 HEAD 판 그대로면 v14대로 지난다(HD2). 그 밖에 출발 판 중 끝난 판이나 끝난 판을 포함하는 판이 있으면 끝난 판에서만 출발할 수 있다(HK3). 규칙 9 표의 "통합에서 어느 출발 판에서도 갈 수 없는 레코드"도 이에 맞춘다.
  - 7.1.3의 들어오는 쪽(MERGE_HEAD, I, I의 첫 부모) 출발 판에서 사고 판과 형식이 틀린 판을 빼고 경고한다. pull의 후보와 같은 규칙이다. HEAD의 판은 5.10·7.1.3-7 그대로다(HEAD에 있던 오류로는 거절하지 않는다).
  - 들이는 쪽에서 계약이 고정한 명세 파일이 그 고정과 다르면 들이는 쪽의 사고다(결정 17). pull은 받지 않고 경고한다.
  - 5.10, 5.9 e: 사고 판과 형식이 틀린 판, 고정과 다른 명세 파일을 HEAD의 오류에 더하고, 막는 명령 목록에 pull을 더한다. GOOD의 정의와 7.1.6 pre-push도 맞춘다. pull은 다른 브랜치의 형식이 틀린 판을 `warning: skipping malformed 'PATH' at 'REV'`로 건너뛴다.
- **규칙 4와 미확인·사고라는 말:** "미확인(사고 표시)" 같은 표현을 고친다. 두 말이 섞인 곳을 모두 고친다: 1.5d, 규칙 6, 5.2a, H8, 7.1.5, 7.12, SKILL 51·52·132·142행, `ait-concept.md` 20·26·34행, `ait-phase1-scenarios.md`의 해당 행.
- **1.5b:** "ait 명령이 만드는 커밋은 걸린 계약을 그 커밋에서 확인한다"에 pull의 예외를 적는다. pull은 `-r`을 받지 않고, 멈추거나 `--force`를 받는다.
- **1.5d (결정 27, 기록 99):** 되돌린 커밋의 정의를 1절의 식으로 **바꾼다**(더하지 않는다). K 판이 future이고 어느 부모의 판에도 포함되지 않는 새 판이며, current 판 C가 부모의 판이거나 `prev`에 있고, 부모의 future 판 중 C나 C와 본문이 같은 판을 포함하는 것이 없는 커밋이다. C를 읽을 수 없으면 세지 않는다.
  - "운반되지 않는 prev 객체(11.5)에 기대지 않고"를 지운다. 판정에 필요한 blob이 없으면 세지 않는다고 적고, 11.5에 그래서 판정이 clone마다 다를 수 있다고 적는다(U1z, U1m).
  - 같은 정의를 쓰는 곳이 함께 바뀐다: 1.3의 "내가 되돌린 약속"(그래서 9.3의 `reason future`, 7.8 todo, H3), H5, 5.9a의 AGENTS, 7.1.5의 `is reopened in this merge`, 7.9.5의 `reopened`, H10·H11·H14의 되돌린 쪽.
  - 관문의 되돌리기, 통합 hook의 되돌리기, 다른 가지의 이행을 버린 통합은 새 식으로도 되돌린 커밋이다. 옛 판으로 돌아간 커밋(git revert)과 실어 나른 병합은 아니다(RV3b, RP3, RP5).
- **규칙 6, 7.1.5, 7.12.1-7:** pull의 커밋이 되돌린 커밋이면 `is reopened by this pull`과 H10을 쓴다. 이유는 7.1.5의 조건이 맞을 때만 붙인다(IND4). `comes in unverified`의 조건은 v14 그대로다(내 약속·요구·구독).
- **6.7, 7.7.5의 출력 행:** 요청 커밋을 만드는 pull도 같은 꼴(`request RID`, `check K forced`)을 쓴다.
- **10.1a:** "병합하면 미확인이 되고, 다시 확인한다"를 고친다. pull은 받기 전에 멈춘다(결정 10).
- **규칙 3:** "합치며 current 계약의 고정이 확인 없이 바뀌면 … 되돌린다(7.1.3-3)"에 pull은 되돌리지 않고 멈춘다(2절 5)고 적는다.
- **2.3 표의 경로 칸:** pull의 `--force`(current→current)와 pull의 도우미 절을 더한다.
- **규칙 7:** 남이 amend로 바꾼 판 앞에서 `D=HEAD`를 고르는 것은 옛 판으로 복원하는 hint가 아니라는 설명을 적는다.
- **규칙 9 표와 5.14**
  - pull이 막는 것을 바꾼다: 갈라짐, 지정 검사, 받는 결과의 validate, 확인 검사. 작업 폴더와 shallow는 막는 목록이 아니라 5.2 같은 전제조건이다.
  - `E_MIXED`와 옛 뜻의 `--force`(`--no-verify`)를 지운다.
  - 새 `--force`는 확인을 넘기는 것이다(`check forced`).
- **5.3, 5.9a, pull 문법 (결정 25)**
  - pull 문법: `K=C`, `<D>-<O>=C`, 도우미 절, `--force`, `-m`. 고르기, 도우미 절, `--force`에는 `-m <why>`가 필요하다.
  - 5.3: pull이 규칙대로 스스로 적는 새 판(3-way, 끝난 쪽, 같은 본문 합치기, 6.2 다시 고정)은 ait가 메시지를 쓴다. 이것을 예외로 적는다.
  - 5.9a의 고치는 명령 꼴과 맞춘다. 5.9a의 "들이는 명령은 git이 거절할 것이면…"을 pull의 0단계 검사로 바꾼다. `make it a promise`를 고친다.
- **5.8:** 새 오류의 종류와 첫 행을 정한다(갈라짐, 지정, 구조 검사의 사고 판, 같은 파일이 두 번 충돌, 확인, 작업 폴더, shallow). `E_MIXED`는 pull에 관한 부분만 지운다(관문, contract add, fulfill, commit은 그대로 쓴다). `E_STATE`·`E_FORMAT`·`E_INCOMPLETE`의 "pull이면 7.12.1-5" 참조를 고친다.
- **7.1.3, 5.9a, `E_BUSY`의 마치는 커밋:** 코드 충돌로 멈춘 pull도 같은 마치는 커밋(`git commit --allow-empty --no-edit --cleanup=strip`, 필요하면 `AIT_SESSION=SID ` 앞붙임)으로 마치고, 같은 마치는 hint(`commit the resolution first: 'CMD', then continue as git says`)를 쓴다. 문구는 v14 그대로다. merge 메시지는 pull의 커밋 메시지다.
- **5.2-3, 5.5, 5.7:** 5.2-3의 "(contract pull의 index 검사는 merge 바로 앞에서 한다, 7.12.1-6.)"를 0단계 검사로 옮긴다. 5.5(공개)에 pull을 더한다: CAS는 5.5-3과 같고, 작업 폴더는 `.ait/`만이 아니라 pull이 쓰는 경로 모두에 쓴다. 5.7의 "검사를 지나 git을 실행한 contract pull은 git의 종료 코드"를 고친다. 코드 충돌로 멈추면 git merge와 같은 종료 코드 1이다.
- **5.9a의 판정 시점:** "pull이면 merge 뒤의 HEAD"를 고친다. pull의 hint는 아무것도 바꾸기 전에 쓴다.
- **5.1, 5.13:** 새 판을 적는 pull은 session이 필요하다. 새 판을 적는지와 pull이 쓰는 경로는 1~5 뒤에야 알므로, 5.13의 순서(session 검사, 5.2)와 맞춰 적는다.
- **7.10.0, 7.12 전면 개정** (위 2절)
  - `Ait-Resolves` 따라가기를 지운다.
  - `Ait-Resolves`를 쓰는 곳을 모두 정리한다: 1.7, 5.4, 7.1.5, 7.9.5, 7.9.6, 9.4, 9.7, 10.3.
  - 7.10.0의 분류 중 pull만 쓰던 것(이행 커밋 등)을 정리한다.
- **요청 ID 대신 계약 ID:** 1.7, 5.4, 9.4, 7.9.6, H9·H11·H15, 5.9a의 들이기 hint와 10.2는 `pull RID` 대신 `pull D…`를 쓰고, 받았는지를 판의 포함으로 판정한다. 떠맡기 사건의 hint는 `pull D O`다. `pull D`도 D가 끝인 떠맡기 기록을 보므로 H9와 말이 맞는다(결정 23).
- **2.2.4:** 레코드를 바꾸는 경로에 pull과 `contract push --force`(확인 행)를 더한다.
- **7.1.1, 5.2a, H12:** hook에 post-commit과 post-merge를 더한다(결정 8). "네 hook"을 고친다.
- **여러 끝과 코드 충돌 (결정 20, 26)**
  - 11.2: pull은 `commit-tree`로 커밋을 만든다. 코드 충돌 때는 pull이 merge 상태를 직접 만든다. `git merge --no-ff`가 만드는 꼴(받을 끝 모두, MERGE_MODE `no-ff`)이고, pull이 쓰는 경로만 쓴다. 중간 커밋은 없다. 충돌은 파일마다 한 번이다. 받는 변경끼리 고정 명세 파일에서 충돌하는 것도 코드 충돌이다(결정 29). 마치는 커밋의 7.1.3 다시 고정이 그 파일을 고정한 future 계약을 푼 내용으로 고정한다.
  - 7.1.3~7.1.5: 마치는 커밋의 hook은 데이터만 본다. 계약 파일이 사고 판이 아니고 출발 판을 모두 포함하면 다시 정규화하지 않고 그대로 둔다. pull이 정했는지는 묻지 않는다. 그래서 pull의 로컬 상태 파일은 없다. 그 판을 7.1.3-2로 다시 고정하면 `prev`는 그 판과 그 판의 `prev` 모두다(7.1.3-5의 극대 출발 판이 아니다, 기록 108, HK4).
  - 들이는 커밋(TIE, 결정 11·30): 본문이 같은 남는 판 중 내 판을 포함하는 판의 들이는 커밋을 먼저 고른다. 그다음 (HEAD에 없는 커밋 수, 커밋 OID)다.
  - 6.2 표의 `prev`: 결과 판이 새 판이면, 그 위에 얹는 도우미 절의 `prev`는 그 새 판과 그 새 판의 `prev` 모두다.
  - 11.5의 "이력을 공유하지 않은 판끼리는 충돌로 남을 수 있다"를 고친다. pull에서는 고르기로 멈춘다(M1, MS2).
- **9.1~9.5, 4.3**
  - 사건은 새 판에만 생긴다. 옛 판으로 돌아간 커밋은 사건이 아니다.
  - 계약 파일의 사건은 (계약, 판)으로 센다. 미확인 사건은 (계약, 판, provider 코드 내용)으로 센다(결정 12, 18). 사본을 요청 ID로 가리던 9.5를 지운다.
  - 9.1의 "새 미확인"(상태)은 그대로 두고, 미확인 사건의 정의를 따로 적는다. 9.2-6과 payload의 `unverified`는 미확인 사건을 쓴다.
  - 같은 판의 생성 커밋이 여럿이면, 알림에 보일 커밋 하나를 (HEAD에 없는 커밋 수, 커밋 OID)로 고른다. 받는 쪽에는 그 판을 처음 담은 커밋 모두의 `Ait-Agent`를 더한다.
  - pull이 적은 새 판은 `prev` 중 결과와 뜻이 다른 판이 있으면 R에 든다. 9.2-2를 부모가 하나인 커밋에도 적용하는 것이다.
  - 버려진 변경을 만든 커밋의 `Ait-Agent`를 받는 쪽(9.3)에 더한다(결정 16).
- **11.1:** revert 등에 대한 설명을 결정 8로 바꾼다.
- **13.1:** git 최소 버전을 `merge-tree --write-tree`가 있는 판으로 올린다(사용자가 2.40까지 허용). 2.40 자체는 아직 돌려 보지 않았다.
- **13.3:** 행수 예산의 "contract push·pull … 185"를 새 pull에 맞춰 다시 잡는다(기록 118의 추정). pull 1,400행(판 정하기 330, 들이는 커밋·지정 170, 합치기·명세 판정 280, 확인 검사 200, 받기·merge 상태 200, 0단계·출력 170, 되돌린 커밋 50), push 100행, hook +60, 확인 행 +40. 합계 약 5,500행.
- **5.8:** 2절 끝의 출력 목록(기록 117)을 옮긴다.
- **`ait-mini.md` 26행:** git merge로 받으라는 안내를 지운다(결정 9).
- **시나리오, SKILL, `ait-concept.md`를 함께 맞춘다.** SKILL에는 git merge를 직접 쓰는 길을 적지 않는다. SKILL 100·165·167·171행과 `ait-concept.md` 79~81행(코드 없이 받기, 고를 계약 커밋 hint, `--force`로 코드까지 받기, git merge 충돌 뒤 hook과 `Ait-Resolves`)을 결정 3·9에 맞춘다.

## 6. 결정 없이 정한 것

이 기준에서 사용자가 직접 정하지 않은 것은 모두 [pull-decisions.md](pull-decisions.md)의 "Claude가 허락 없이 정한 것 (기록)"에 하나씩 적었다(1~113). 대부분은 결정 1~30이나 v14를 그대로 적용해 답이 하나로 정해지는 것이고, 답이 여럿이었던 것은 사용자에게 물어 결정 20~30으로 올렸다.
