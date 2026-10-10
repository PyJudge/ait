# 명세만 있고 구현이 전혀 없을 때

명세 문서는 있는데 그것을 구현한 코드가 아직 하나도 없을 때의 절차다. 이 절차를 따르면 명세의 조항 하나하나가 계약이 되고, 누가 어느 조항을 맡았는지, 어느 조항이 어떤 확인을 거쳐 구현됐는지가 기록으로 남는다. 구현된 뒤에는 그 코드를 고칠 때마다 해당 조항이 걸려 다시 확인을 요구하므로, 명세가 계속 원본으로 남는다.

일부라도 구현이 있으면 이 절차가 아니라 SKILL.md의 일반 흐름("코드를 쓰다 '이건 지켜져야 해'라고 판단했다")을 따른다.

## 흐름

1. 명세의 조항마다 명세 정의를 쓰고, **추상 future demand**를 건다.
2. 조항에 착수할 때 그 demand를 **추상 future offer**로 떠맡는다. 하나씩, 손댈 때.
3. 구현을 마치면 **fulfill로 범위를 실제 파일로 좁혀** current로 만든다.

## 1. 조항마다 demand를 건다

```sh
# specs/cN.yaml: 명세 N조의 조건과 확인 방법. 절 하나(future demand …)가 demand 하나, 조항 하나다
ait future demand doc/spec.md / specs/c1.yaml \
    future demand doc/spec.md / specs/c2.yaml \
    future demand doc/spec.md / specs/c3.yaml -m "명세 1-3조"
```

- **코드보다 먼저, 따로 커밋한다.** 다른 에이전트는 이 계약 커밋을 찾아(SKILL.md "남의 계약을 들인다") 코드 없이 들인다.
- **demand 하나에 조항 하나.** 계약은 통째로 이행되고 통째로 다시 확인된다. 명세 전체를 demand 하나로 걸면 전부 구현할 때까지 이행할 수 없다. 이행한 뒤에는 어느 코드를 고치든 명세 전체를 다시 확인해야 한다. 위처럼 한 명령에 절로 이어 걸면 한 커밋에 모두 걸리고, 출력의 `new D…` 행이 조항마다 하나씩 나온다.
- **consumer는 명세 문서.** 이 조건에 기대는 것은 명세 자체다. consumer에는 추상 범위(`/`, `src/`)를 쓸 수 없다(추상 범위는 future의 provider에만 쓴다).
- **provider는 `/`(전체).** 어디에 구현될지 아직 모르기 때문이다. 짐작이 가면 `src/`처럼 좁혀도 된다.
- **명세 정의의 `body`는 그 조항.** 명령으로 확인할 수 있으면 `engine: exec`와 그 조항을 검사하는 테스트를 `command`·`inputs`에 둔다. 명세 문서 전체를 `inputs`에 넣지 않는다. 한 줄만 고쳐도 모든 조항이 미확인이 된다.

## 2. 착수할 때 하나씩 떠맡는다

```sh
ait todo doc/spec.md                     # 조항들의 demand. undertaken-by 행이 없으면 아무도 맡지 않은 것
ait future offer src/ specs/c1.yaml as o undertake D… o -m "명세 1조 착수"
```

- 미리 여러 조항을 잡아 두지 않는다. 잡고만 있으면 남이 그 약속을 기다리므로 병목이다. 손대는 조항만 떠맡고, 그만두면 바로 거둔다(`ait future cancel O… -m …`).
- offer의 provider도 추상 범위(`src/`, `/`)로 둔다. 어느 파일에 구현할지는 구현하면서 정해진다.
- 조항 몇 개를 한 구현으로 해낸다면 offer 하나로 함께 떠맡는다(`… as o undertake D1… o undertake D2… o`). 그래도 demand는 조항마다 따로 남으므로, 한 조항이 바뀌면 그 demand만 다시 확인한다.
- 다른 조항이 아직 구현되지 않았어도, 그 demand의 조건대로 있다고 보고 개발한다.

## 3. 구현한 뒤 범위를 좁혀 이행한다

```sh
git add <구현>; ait commit -m "명세 1조 구현"     # 코드만 커밋. future는 걸리지 않으므로 확인이 없다
ait fulfill O…=src/a.c D…=src/a.c -m "명세 1조 구현"                   # -r 없이: 과제를 받는다
ait fulfill O…=src/a.c D…=src/a.c -r <check-result> -m "명세 1조 구현"  # 확인해 current로
```

- 따로 amend할 필요가 없다. future 계약에 `ait amend O…`를 하면 `noop`이다. fulfill이 추상 범위를 구현한 파일로 좁히고, 확인을 거쳐 offer와 떠맡은 demand를 함께 current로 만든다.
- 좁힌 범위는 실제로 그 조건을 구현한 파일이다. 여러 파일이면 `,`로 잇는다(`O…=src/a.c,src/b.c`).
- 확인 결과를 내는 법은 SKILL.md의 "확인 결과를 내는 법"과 같다.

## 그 뒤

- 이행된 조항의 코드를 고치면 그 계약이 걸린다. SKILL.md의 "current 계약의 코드를 고친다"를 따른다.
- **명세가 바뀌면** 그 조항의 명세 정의(`specs/cN.yaml`)를 고치고 다시 고정한다. 구현이 이미 맞으면 `ait amend D… -m "명세 N조 변경"`으로 확인해 current로 둔다. 다시 구현해야 하면 `ait future amend D… -m "명세 N조 변경"`(offer가 떠맡고 있으면 `future amend O…`도 한 요청에 함께)으로 약속으로 되돌린 뒤 다시 구현하고 이행한다.
- **조항이 새로 생기면** 1부터 같은 절차로 건다. **조항이 없어지면** 그 demand를 거둔다. future면 `ait future cancel D…`, current면 `ait future amend D…` 다음 요청으로 `ait future cancel D…`다.
