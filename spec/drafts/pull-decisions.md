# `ait contract pull`: 정한 것 (2026-10-09, r24 검수 뒤)

r24 검수 뒤 사용자와 하나씩 정한 것을 적는다. `pull-criteria.md`의 "내용만 복사해 새 커밋을 만든다"는 방식은 버렸다. 새 기준은 이 결정들로 다시 쓴다.

1. **이력째 받는다.**
   - `pull K`는 K의 받을 판이 처음 들어간 커밋을 받는다. 그 커밋과 그 앞의 커밋이 모두 내 이력에 들어온다. 그 뒤의 커밋은 받지 않는다.
   - 근거: ait는 버전 관리 프로그램이다. 어떤 코드가 필요한지 가려낼 수 없다. 예를 들어 고정된 함수가 고정되지 않은 내부 유틸 함수를 부를 수 있다.
2. **들어오는 계약은 모두 지정한다.** 들어오는 이력이 바꾸는 계약을 모두 지정하지 않으면 거절하고, 아무것도 바꾸지 않는다.
   - 예: B의 이행 앞에 A의 이행이 있으면 `pull B`는 거절된다. hint는 `ait contract pull A B`다.
   - 리스너가 A도 본 다음에 받게 하기 위해서다.
3. **코드 커밋은 언제나 받는다.**
   - 계약 변경이 아닌 커밋은 지정 없이 받는다(1의 유틸 예).
   - v14의 `E_MIXED`, "아직 이행되지 않은 남의 코드는 받지 않는다", 그것을 넘기던 pull의 `--force`를 뺀다.
4. **최신이 둘 이상이면 고른다.**
   - 지정한 계약의 최신 판(`prev`로 다른 판에 포함되지 않는 판)의 내용이 둘 이상이면 거절한다.
   - hint는 판을 커밋 이름으로 고르는 명령이다: `pull B=c2`. 내 판이 최신 중 하나면 `B=HEAD`도 선택지에 든다.
   - 고른 쪽의 이력만 받는다. 받은 계약 파일의 `prev`에는 고르지 않은 판도 적는다. 그래서 다음 pull에서 같은 갈라짐으로 다시 멈추지 않는다.
   - 고르지 않은 쪽에는 알림이 간다.
   - 합치려면 하나를 골라 받은 뒤 그 계약을 고친다(amend).
   - 누군가 이미 합쳐 둔 판이 있으면 최신이 하나이므로 그 커밋을 받는다.
5. **이미 받았는지는 판으로 판단한다.** 내 계약 파일이 최신 판들을 모두 포함하면 `already K`다. 요청 ID로 판단하지 않는다.
6. **내 계약이 확인한 것이 바뀌면 받기 전에 거절한다.**
   - 받으면 내 current 계약이 확인한 코드나 명세 파일이 바뀌는 경우, 받기 전에 거절하고 아무것도 바꾸지 않는다.
   - hint:
     - `reopen it first: 'ait future amend K -m <why>', then rerun this command`
     - K를 내 current offer O가 떠맡고 있으면 `'K' is undertaken by current 'O'; reopen them in the same request: 'ait future amend O future amend K -m <why>', then rerun this command`
     - 또는 `ait contract pull B --force -m <why>`. 사소하다고 판단해 받는 것이고, K의 `forced` 확인 기록으로 남는다. v14에 없는 새 길이다.
   - 명세 파일을 공유하는 내 계약이 future면 거절하지 않는다. 새 내용으로 다시 고정한다(6.2).

7. **떠맡기 기록은 D와 O 둘 다의 변경이다.**
   - `.ait/undertake/D-O`가 바뀌는 것은 D와 O의 변경으로 본다(9.2-3과 같다). 그래서 들어오는 이력에 떠맡기나 release가 있으면 2에 따라 D와 O를 모두 지정해야 받는다.
   - 따로 둔 offer→demand 규칙(옛 J8)은 없앤다.
   - 예: 내 current offer O에 남이 떠맡기를 붙인 이력을 `pull D`로 받으려 하면 거절된다. hint는 `ait contract pull D O`다.

## 함께 고칠 용어

- hint의 `make it a promise`(5.9a)는 `reopen`·`future`로 고친다. "promise"는 쓰지 않는다.
