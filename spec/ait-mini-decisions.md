# ait-mini 구현 결정

## Claude가 허락 없이 정한 것 (사용자 확인 전)

각 항목은 "결정 N에서 도출" 또는 "선택"이다. 2026-10-10의 git 연동 재설계부터 적었다. 그 전의 구현 중 결정은 아직 모으지 않았다.

1. **5.5-4 문구를 `git read-tree -i -m O C` 설계로 바꿈.** "되도록이면 git을 호출한다"(사용자)에서 도출. O는 C에 요청 시작 때의 index 판을 놓은 tree다. 그사이 다른 쪽이 stage한 경로는 git의 two-way merge대로 그대로 두고, 이 요청과 어긋나면 index를 통째로 두고 5.5-5 경고를 쓴다.
2. **sparse로 다루는 조건은 `core.sparseCheckout`이 true이고 `info/sparse-checkout`이 있을 때.** "git이 하는 대로"에서 도출. 패턴 파일이 없으면 git도 sparse로 다루지 않으므로 reapply하지 않는다.
3. **pre-push 입력은 오른쪽부터 읽음.** git 출력 형식(transport.c:1355)에서 도출. 로컬 필드에 공백과 LF가 있어도 된다. 형식에 맞지 않는 남은 입력은 `fatal: bad pre-push input '…'`(128)이다. 문구와 종료 코드는 선택.
4. **symlink이거나 일반 파일이 아닌 명세 파일은 E_INCOMPLETE `'…' is not a regular file`.** 2.1.4와 spec 표에서 도출. 문구는 선택.
5. **대소문자를 가리지 않는 파일시스템에서 대소문자만 다른 경로 둘은 index 항목 둘이 됨.** "git이 하는 대로"에서 도출(`git update-index`의 동작). 막지 않는다.
6. **noop일 때 index를 맞추지 않음.** 5.5-4 "noop일 때도 같다"에서 도출. 부분 stage 검사가 먼저 막으므로, noop까지 오면 명세 파일의 index 판은 HEAD와 같고 맞출 것이 없다. 문구는 그대로 둔다.
7. **5.5-5의 경고와 hint 문구.** 선택. `committed '…' to '…', but the index and work tree were not updated`, `but … were not updated`, `bring … to '…' in the index`/`in the work tree`, reapply가 실패하면 `but git failed: 'git sparse-checkout': …`.
8. **ID를 만들 수 없을 때의 문구 `unable to get random bytes`.** 선택. git의 tempfile 문구를 따랐다.
9. **`git worktree list`를 `-z`로 읽음.** "git이 하는 대로"에서 도출. 경로에 LF가 든 저장소·worktree는 미지원으로 거절한다(사용자 결정).
10. **libyaml이 받는 대로 받음.** "libyaml 동봉"(사용자 결정)에서 도출. 그래서 YAML 1.2가 막는 일부 형식도 받는다(따옴표 바로 뒤의 `#` 주석). 여러 행 plain·따옴표 scalar, `? k` 키처럼 YAML이 허용하는 형식도 받는다. 예전 파서는 막았다.
11. **libyaml의 오류 문구를 그대로 씀.** 선택. `bad format: 'FILE': <libyaml의 problem> in line N`. 부분집합 밖의 것(alias, anchor, tag, 지시, 둘째 문서, `>` 블록, scalar가 아닌 키, map이 아닌 문서, 16단 넘는 중첩, 중복 키)은 ait 문구다.
12. **확인 요청·결과 틀의 모양이 libyaml emitter의 모양이 됨.** "표준 escape"와 "libyaml 동봉"(사용자 결정)에서 도출. `tasks`/`results` list는 들여쓰지 않고, body는 값에 맞는 `|`·`|-`·`|2` 표시를 붙이며, 결과 틀의 `input`도 `"…"`다. spec 8.2의 규칙과 예를 이렇게 고쳤다.
13. **결과 틀의 안내 주석을 문서 앞에 둠.** 선택. emitter는 주석을 쓰지 못하므로, 예전처럼 `input`과 `results` 사이가 아니라 맨 앞에 둔다.
14. **문서 하나의 `---`·`...` 표시를 받음(2.5.1에 적음).** "libyaml 동봉·표준 escape"(사용자 결정)에서 도출. emitter는 끝 행바꿈을 지키는 `|+` body 뒤에 `...`를 쓰므로, 받지 않으면 ait가 쓴 파일을 ait가 거절한다. 지시와 둘째 문서는 그대로 막는다.
15. **git의 stderr 행은 git의 `fatal: `·`error: `를 떼고 ait 문구 안에 넣음.** 선택. ait가 제 접두어를 붙이므로 `error: … error: …`로 겹치지 않게 한다. E_GIT은 5.8 표의 "git 명령과 그 stderr 첫 행"대로 `fatal: git failed: 'git SUB': 이유`다. ait가 직접 만드는 E_GIT(`cat-file`이 멈춤, tree가 아님)도 이 모양이다.
16. **시작 때의 index는 두 사실로 읽음.** 5.5-1에서 도출. 커밋할 tree(T0)는 `git write-tree`(git commit처럼 intent-to-add를 뺀다), index 항목은 `git ls-files -s`(intent-to-add도 빈 blob으로 든다)다. 부분 stage 판정과 follow의 O는 index 항목으로 한다. 그래서 `git add -N`한 명세 파일은 예전처럼 "partly staged"다.
