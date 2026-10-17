# 구현이 아직 없는 저장소

명세를 먼저 계약으로 적되, 상대하는 ait 브랜치와 코드 HEAD를 구분한다. 같은 clone의 worktree들은 계약 브랜치를 공유하며 코드 파일은 각자의 worktree에 있다.

1. 각 에이전트는 자기 코드 worktree에서 session을 열고 `ait status /`를 확인한다.
2. 명세의 조건을 `body`로 쓴 조건 파일을 만든다. 구현이 없어 검사를 실행할 수 없으면 prompt 정의(`body`만)를 쓴다. 실행 가능한 검사가 생기면 exec 정의로 바꾼다.
3. 아직 없는 구현에 대한 demand는 provider를 `/`로 약속할 수 있다. consumer는 명세 파일처럼 이미 있는 사용 쪽 범위로 둔다.

```yaml
body: 재고가 모자라면 예약을 거절하고 저장된 재고는 바꾸지 않는다
```

```sh
ait future demand doc/spec.md / specs/stock.yaml -m "재고 예약 조건"
```

4. 구현을 맡는 에이전트는 같은 계약 브랜치에서 그 demand를 보고, 조건 파일을 자기 worktree에 준비하여 떠맡는다. 다른 worktree의 미커밋 조건 파일이 내 작업 폴더에도 있다고 가정하지 않는다.

```sh
ait future offer / specs/stock.yaml as stock undertake D0123456789ab stock -m "재고 구현 착수"
```

5. 구현과 검사를 작성하고 코드 stage를 확인한다. 통째 offer와 demand의 추상 provider를 함께 좁혀 이행한다. 두 ID를 명시하여 서로 기대는 계약의 조건을 같은 요청에서 바꾼다.

```sh
git add src/stock.sh tests/stock.sh
ait fulfill O0123456789ab=src/stock.sh D0123456789ab=src/stock.sh -m "재고 예약 구현"
# 출력된 request의 tree를 검사하고 result를 채운 뒤 같은 범위로 재실행:
ait fulfill O0123456789ab=src/stock.sh D0123456789ab=src/stock.sh -r /path/to/check-result.yaml -m "재고 예약 구현"
```

아직 HEAD에 없는 파일을 future에서 미리 지정하려면 `new:src/stock.sh`를 쓴다. 이행할 때 그 파일이 index에 존재한 뒤 구체 범위로 좁힌다. 행 범위를 future amend에 직접 주려면 해당 코드가 먼저 HEAD에 있어야 한다.

한쪽이 작업 중이어도 다른 쪽은 그 약속의 조건을 기준으로 자기 일을 계속한다. 조건이 바뀌면 `ait status /`와 알림을 보고 합의한 조건에 맞춰 수정한다. 완료한 코드 브랜치는 Git으로 합치고, 계약의 현재 판정은 합친 코드에서 다시 확인한다.
