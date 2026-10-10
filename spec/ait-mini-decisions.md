# ait-mini 구현 결정

## Claude가 허락 없이 정한 것 (사용자 확인 전)

각 항목은 "결정 N에서 도출" 또는 "선택"이다. 2026-10-10의 git 연동 재설계부터 적었다. 그 전의 구현 중 결정은 아직 모으지 않았다.

1. **5.5-4 문구를 `git read-tree -i -m O C` 설계로 바꿈.** "되도록이면 git을 호출한다"(사용자)에서 도출. O는 C에 요청 시작 때의 index 판을 놓은 tree다. 그사이 다른 쪽이 stage한 경로는 git의 two-way merge대로 그대로 두고, 이 요청과 어긋나면 index를 통째로 두고 5.5-5 경고를 쓴다.
2. **sparse로 다루는 조건은 `core.sparseCheckout`이 true이고 `info/sparse-checkout`이 있을 때.** "git이 하는 대로"에서 도출. 패턴 파일이 없으면 git도 sparse로 다루지 않으므로 reapply하지 않는다.
3. **pre-push 입력은 오른쪽부터 읽음.** git 출력 형식(transport.c:1355)에서 도출. 로컬 필드에 공백과 LF가 있어도 된다. 형식에 맞지 않는 남은 입력은 `fatal: bad pre-push input '…'`(128)이다. 문구와 종료 코드는 선택.
4. **symlink이거나 일반 파일이 아닌 명세 파일은 E_INCOMPLETE `'…' is not a regular file`.** 2.1.4와 spec 표에서 도출. 문구는 선택.
5. **대소문자만 다른 경로는 받지 않음(사용자 결정, 7.3.3에 적음).** core.ignorecase에서 HEAD나 index에 대소문자만 다른 경로가 있으면 명세 파일은 E_INCOMPLETE, 계약 범위는 E_SCOPE이고, hint는 git의 경로를 알려 준다. `git add`가 그 경로를 stage하지 않는 것을 따랐다.
6. **noop일 때 index를 맞추지 않음.** 5.5-4 "noop일 때도 같다"에서 도출. 부분 stage 검사가 먼저 막으므로, noop까지 오면 명세 파일의 index 판은 HEAD와 같고 맞출 것이 없다. 문구는 그대로 둔다.
7. **5.5-5의 경고와 hint 문구.** 선택. `committed '…' to '…', but the index and work tree were not updated`, `but … were not updated`, `bring … to '…' in the index`/`in the work tree`, reapply가 실패하면 `but git failed: 'git sparse-checkout': …`.
8. **ID를 만들 수 없을 때의 문구 `unable to get random bytes`.** 선택. git의 tempfile 문구를 따랐다.
9. **`git worktree list`를 `-z`로 읽음.** "git이 하는 대로"에서 도출. 경로에 LF가 든 저장소·worktree는 미지원으로 거절한다(사용자 결정).
10. **libyaml이 받는 대로 받음.** "libyaml 동봉"(사용자 결정)과 ait-mini.md 한계에서 도출. libyaml이 YAML 1.2보다 너그러운 곳(따옴표 바로 뒤의 `#` 주석)도, 엄격한 곳(탭이 놓일 수 있는 자리 일부)도 libyaml이 두는 대로다. CR·NEL·LS·PS만 2.5.1의 "LF"로 막는다(결정 21).
11. **libyaml의 오류 문구를 그대로 씀.** 선택. `bad format: 'FILE': <libyaml의 problem> in line N`. 부분집합 밖의 것(alias, anchor, tag, 지시, 둘째 문서, `>` 블록, scalar가 아닌 키, map이 아닌 문서, 16단 넘는 중첩, 중복 키)은 ait 문구다.
12. **확인 요청·결과 틀의 모양이 libyaml emitter의 모양이 됨.** "표준 escape"와 "libyaml 동봉"(사용자 결정)에서 도출. `tasks`/`results` list는 들여쓰지 않고, body는 값에 맞는 `|`·`|-`·`|2` 표시를 붙이며, 결과 틀의 `input`도 `"…"`다. spec 8.2의 규칙과 예를 이렇게 고쳤다.
13. **결과 틀의 안내 주석을 문서 앞에 둠.** 선택. emitter는 주석을 쓰지 못하므로, 예전처럼 `input`과 `results` 사이가 아니라 맨 앞에 둔다.
14. **문서 하나의 `---`·`...` 표시를 받음(2.5.1에 적음).** 선택. 8.2는 확인 요청 파일을 2.5.1의 부분집합으로 쓴다고 하는데 emitter가 `|+` body 뒤에 `...`를 쓰므로 부분집합이 그것을 담아야 한다. 입력의 `---`까지 받는 것은 YAML 표준을 따른 선택이다. 지시와 둘째 문서는 그대로 막는다.
15. **git의 메시지는 번역하지 않은 영어로 받고, git의 `fatal: `·`error: `를 떼고 ait 문구 안에 넣음.** 선택. git 자식에 `LANGUAGE=C`를 준다(5.6 ait의 메시지는 영어). E_GIT은 5.8 표의 "git 명령과 그 stderr 첫 행"대로 `fatal: git failed: 'git SUB': 이유`다.
16. **intent-to-add는 git처럼 "stage되지 않음".** 5.5-1·7.3.3과 git의 동작에서 도출. 커밋할 tree와 부분 stage 판정은 `git write-tree`(intent-to-add를 뺀다)로, follow의 O는 `git ls-files -s`의 항목(intent-to-add도 든다)으로 한다. 그래서 `git add -N`한 명세 파일은 받고 index가 커밋된 판을 따르며, 커밋이 빼는 코드 경로의 intent-to-add 항목은 git commit처럼 남는다.
17. **명세 정의는 git이 stage한 blob에서 읽음.** 7.3.3("작업 폴더 내용과 mode"를 U에 넣는다)과 2.1.4에서 도출. U에 든 것은 git이 stage한 blob이므로 정의도 그 blob에서 읽는다. 그래서 clean filter를 거친 내용의 inputs를 따른다.
18. **YAML 값에 NUL이 들면 E_FORMAT(`a NUL character`).** 선택(spec은 정하지 않는다). NUL이 든 파일을 이미 `not a text file`로 막는 것과 맞췄다. 표준 escape(`\0`)로 NUL을 쓸 수 있게 되어 생긴 경우다.
19. **명세 파일은 `git add -f`로 stage함(임시 index에 HEAD를 깔고 그 경로의 실제 index 판을 얹은 뒤).** 7.3.3과 "git이 하는 대로"에서 도출. 그래서 core.fileMode, 파일이 디렉터리가 된 경로, submodule 안 경로, sparse 밖 경로를 git add가 다루는 대로 다룬다. 작업 폴더의 모양(symlink를 지나는 경로, 디렉터리, FIFO, 없는 파일)은 ait가 먼저 보고 E_INCOMPLETE, git add가 거절하면 E_GIT이다.
20. **E_FORMAT의 행은 값이 있는 자리의 행.** 5.8 "(파일과 행)"에서 도출. 모르는 키, 값의 종류, engine, input, 결과 파일의 task는 그 값의 행을 쓴다. 키가 없으면 결정 29.
21. **NEL·LS·PS를 거절함.** 선택. 2.5.1은 행바꿈을 LF로만 정하는데 libyaml(YAML 1.1)은 이 셋도 행바꿈으로 읽어 값이 달라지므로, CR과 함께 `a line break other than LF`로 막는다.
22. **정수는 따옴표 없는 10진수, 앞자리 0·부호 없음.** 선택(2.5.1은 "10진 정수"라고만 한다). YAML 1.2는 `010`·`+1`도 정수로 읽지만, `"0"`은 문자열이고 앞자리 0은 YAML 1.1 독자가 8진수로 읽으므로 막았다.
23. **결과 파일의 `+n`은 원문이 값을 그대로 쓴 자리에서만 그 자리에서 바꾸고, 아니면(escape, `|` 블록 등) scalar 전체를 `"…"`로 새로 씀.** 선택. 8.4는 "`+n`만 바꾸고 나머지는 그대로"라고만 한다.
24. **결과 파일 `|` 블록 끝의 빈 행은 libyaml이 알려 주는 그 블록의 범위 끝에서만 뺌. `|+` 블록은 빈 행이 값이라 그대로 둠.** 8.4에서 도출. 예전의 들여쓰기 추정은 블록 안의 빈 행과 따옴표 문자열의 줄바꿈까지 지워 틀렸다(round 8).
25. **H8의 since는 9.1대로 모든 부모를 봄.** 9.1("같은 고정으로 이미 미확인이던 부모가 없는 커밋")과 H8("가장 최근 것")에서 도출. HEAD의 이력을 topo 순서로 보며, 미확인이고 어느 부모도 같은 고정으로 미확인이 아닌 첫 커밋이다. 상한은 없다.
26. **hint 명령 안의 경로는 shell 한 단어로 인용함(`-r` 등).** 5.9a에서 도출(hint 명령은 그대로 실행된다). 산문 속 경로는 git처럼 `'…'`다.
27. **UTF-8이 아닌 경로는 확인 파일에서 git처럼 인용함(사용자 결정, 8.2에 적음).** 경로는 받고, UTF-8 YAML인 확인 요청·결과 틀에만 git의 quote_c_style 형식으로 쓴다. 결과 파일의 task는 그 형식의 이름으로 맞춘다. 다른 출력은 바이트 그대로다.
28. **이미 stage된 명세 파일을 고치라는 hint는 ait-mini에서 `fix 'PATH' and stage it again, then rerun this command`.** 5.9a의 `ait contract add PATH`가 ait-mini에 없어서, hint는 목표 상태만 말한다는 규칙(E_CODE_REQUIRED의 `stage 'X'`와 같은 꼴)을 따랐다. 선택.
29. **키가 없는 E_FORMAT은 그 키가 있어야 할 map의 행을 씀.** 5.8 "(파일과 행)"에서 도출. 그래서 결정 20의 "행이 없다"는 예외를 없앴다.
30. **키가 스키마에 없을 때: 스키마가 아는 가장 가까운 조상이 map(또는 없음)이면 모르는 키, scalar·list이면 그 값의 종류가 틀린 것.** 2.5.1과 8.4에서 도출. 그래서 결과 파일의 `input`이 다르면 값의 종류는 보지 않는다(8.4: input이 다르면 다른 검사 없이 E_NEEDS_RESULTS).
31. **결과 파일의 task 이름은 확인 파일이 쓴 이름(UTF-8이 아니면 git식 인용)으로 맞추고, `+n`을 바꿀 때도 그 이름으로 다시 씀.** 결정 27(사용자 결정)에서 도출.
32. **ait의 트레일러 키 네 개(`Ait-Check`, `Ait-Request`, `Ait-Agent`, `Ait-Session`)는 `trailer.<키>.key=<키>`로 고정해 읽고 씀(사용자 결정).** 그 키 이름을 별칭으로 쓴 사용자 설정(`trailer.ait-check.key` 등)은 대소문자와 무관하게 막힌다. 다른 별칭 이름으로 이 키들을 가리키게 하는 설정까지는 막지 못한다.
