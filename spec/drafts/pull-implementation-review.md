# pull 구현의 검증 경계

독립 검수에서 나온 사례는 아래 경계의 실패를 찾는 출발점으로 쓴다. 같은 출력이 나오는지만 보지 않고, 판정에 사용한 데이터와 실패 뒤 보존할 상태를 검사한다.

| 경계 | 원인 | 공통 규칙 | 같은 원인을 찾는 질문 | 회귀 검사 |
| --- | --- | --- | --- | --- |
| index·작업 폴더 갱신 | Git의 보호 조건을 일부만 흉내 내고 staged 상태를 정상 기준으로 삼음 | pull의 사전 검사와 실제 갱신은 Git unpack-trees에 맡긴다. ait는 쓰는 경로의 staged·unstaged 변경과 ignored를 포함한 untracked 데이터만 더 엄격히 거절한다 | index와 작업 파일이 서로 다른데 작업 파일만 HEAD와 같으면? 삭제·symlink·directory/file 전환이면? 쓰지 않는 경로의 변경은 보존되는가? | `t/pull/t74-pull-safety.sh`, 기존 `t70`, `t71` |
| 범위의 위치와 존재 | 파일 존재를 범위 존재로 간주하고 사상 실패를 파일 전체로 바꿈 | `locate_scope`가 위치·변경·missing·deleted·nobase를 함께 판정한다. 확인·다시 고정·표시·hint는 이 결과를 사용한다 | 파일이 남아 있어도 확인한 행이 모두 사라졌는가? 기준 blob을 찾을 수 없는가? 일부 범위만 살아남았는가? 강제 확인이나 helper가 실패를 우회하는가? | `t/pull/t73-pull-ranges.sh` |
| 범위 목록의 정규형 | S/E를 문자열로 정렬하고 사상 뒤 중복을 남김 | consumer/provider는 PATH, 파일 전체, 숫자 S/E 순서로 정렬한다. 사상한 범위와 hint의 범위는 같은 `normalize_scopes`를 거친다 | 2와 10의 순서는? 서로 다른 범위가 같은 행으로 합쳐지면? CHECK/PINS와 실행 명령이 같은 정규형을 쓰는가? | `t/pull/t73-pull-ranges.sh` |
| 수신 계약의 적법성 | blob 검증만으로 commit/path의 mode·path 검증을 대신함 | `record_from_entry`는 tree entry를 검증한다. entry의 오류와 blob의 prev 이력은 별도 캐시로 다룬다. 형식이 틀린 계약이 선언한 변경도 제외한다 | 같은 blob이 정상·잘못된 mode의 ref에 모두 있으면? 잘못된 연결을 자동 추가하면? 정상 계약을 함께 받으며 잘못된 계약의 명세만 받아들이면? | `t/pull/t75-pull-selection.sh` |
| 기존 미확인과 요청 여부 | 소개 커밋을 실제 부모 대신 쓰고 merge 형태로 요청 여부를 추정함 | HEAD와 실제 받는 끝의 집합을 만든다. 기존 미확인은 부모의 current·같은 PINS·미확인으로 판정한다. 요청 여부는 최종 계약 blob이 모든 부모 blob과 다른지로 판정하고 session·RID·기록줄·출력에 같은 결과를 쓴다 | 무관한 끝 하나가 추가되면 판정이 바뀌는가? 모든 계약이 부모 판 그대로인데 session이나 RID를 요구하는가? 반대로 진짜 새 판의 요청을 빠뜨리는가? | `t/pull/t76-pull-parents.sh`, 기존 새 판·force·helper 검사 |
| closure와 선택 | 최초 이름의 선택만 해결하고 추가로 발견한 임시 결과를 확정 결과처럼 취급함 | 선택을 한 단계 진행할 때마다 closure를 계산한다. 발견한 모든 unresolved 선택을 해결한 뒤에만 명령을 제시한다 | 선택 뒤 새 이름이 갈라져 있으면? 연결을 통해 더 발견되면? 제시한 각각의 명령을 실제 실행하면 성공하는가? | `t/pull/t75-pull-selection.sh` |
| 실행 명령과 출처 | 정렬된 결과 이름을 사용자 지정 이름으로 대신 쓰거나 마치는 명령의 session을 빠뜨림 | 기본 메시지는 직접 지정한 ID와 그 순서를 사용한다. 비기본 session은 ait 명령과 허용된 Git 마치는 명령 모두에 붙인다 | 자동 추가 이름이 메시지에 섞이는가? hint를 복사해 마친 커밋이 안내받은 session을 기록하는가? | `t/pull/t76-pull-parents.sh` |

## Git에 맡길 판단과 ait가 계산할 판단

Git의 조상 끝 제거는 `merge-base --independent HEAD END…` 한 번으로 계산한다. 제외할 계약 변경을 담은 side tree를 만들고 나면 실제 merge와 merge base 선택은 Git이 맡는다. 계약의 prev 포함, 사고 판, 확인 행, closure는 Git commit ancestry와 다르므로 ait가 계산한다.

병합 차례 탐색은 OID 순서를 유지하되 부분 차례마다 진행한다. 이미 충돌한 경로를 다시 바꾸는 끝은 그 자리에서 탈락시키고 그 아래 차례를 실행하지 않는다. 결과 계약 overlay와 각 끝의 filtered side는 차례마다 다시 만들지 않는다. 소개 커밋과 HEAD에 없는 커밋 수는 HEAD를 포함한 캐시 키로 계산하여 split preview의 가상 HEAD와 실제 HEAD를 섞지 않는다.

## 남은 검수의 기준

새 반례는 수정 전 바이너리에서 실패해야 한다. 거절 검사는 종료 코드뿐 아니라 ref, index 내용, 작업 파일·symlink·untracked 데이터, merge 상태를 함께 검사한다. hint는 문자열 검사에 더해 제시한 명령을 실행한다. 정규화 검사는 저장한 행과 CHECK/PINS를 함께 검사한다. 전체 `make test` 통과 뒤에도 줄 상한은 별도로 확인하며, 행 압축이나 상한 변경으로 중복 구현을 가리지 않는다.
