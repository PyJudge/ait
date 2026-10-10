# ait 1단계 시나리오 (수용 시험)

판: v15 · 기준 규격: `ait-phase1-spec.md` v15

이 시나리오들이 1단계의 수용 시험이다. 모두 통과해야 1단계를 완료한 것으로 본다. `contract pull`(7.12)의 시험은 `drafts/pull-criteria.md` 3절의 150행이고, `t/pull/`에 그대로 옮겨 두었다(T33). 각 시나리오는 shell 시험(git `t/*.sh` 방식)으로 그대로 옮길 수 있게 썼다. 괄호 안의 숫자는 규격(`ait-phase1-spec.md`)의 절 번호이고, "규칙 N"은 규격 첫머리의 규칙이다.

## 0. 공통 준비와 표기

### 0.1 고정 장치

```sh
T=$(mktemp -d); export T HOME=$T/home GIT_CONFIG_NOSYSTEM=1 GIT_EDITOR=:; mkdir -p "$HOME"
git config --global user.name t; git config --global user.email t@t; git config --global init.defaultBranch master
N=0; tick() { N=$((N+60)); export GIT_COMMITTER_DATE="@$((1700000000+N)) +0000" GIT_AUTHOR_DATE="@$((1700000000+N)) +0000"; }
git init -q --bare "$T/origin.git"
git init -q "$T/r" && cd "$T/r" && git remote add origin "$T/origin.git"
mkdir src; for i in $(seq 1 50); do echo "o$i"; done > src/order.c
for i in $(seq 1 50); do echo "s$i"; done > src/stock.c
tick; git add -A && git commit -qm base
tick; ait init
git push -q origin master
```

커밋을 만드는 명령 앞에는 항상 `tick`을 둔다. 명세 파일은 필요할 때 작업 폴더에 만든다.

prompt 정의(body만 있다):

| 파일 | 내용 |
| --- | --- |
| `specs/p.yaml` | `body: 재고가 0이면 예약 요청을 거절한다.` |
| `specs/low.yaml` | `body: 최저가가 보장될 것.` |
| `specs/w.yaml` | `body: 주문 집계는 하루 단위다.` |

exec 정의에 쓰는 스크립트. 아래 각 `tests/X`마다 exec 정의 `specs/X.yaml`이 있다고 본다(0.2의 줄임). 1단계의 ait는 이 스크립트를 실행하지 않는다. 내용은 확인하는 에이전트가 돌릴 명령의 예이고, 시험이 보는 것은 정의와 그 고정(`file` 행), 그리고 확인 요청 파일에 실린 안내다.

| 파일 | mode | 내용 |
| --- | --- | --- |
| `tests/pass.sh` | 755 | `#!/bin/sh` / `exit 0` |
| `tests/guard.sh` | 755 | `#!/bin/sh` / `grep -q guard src/stock.c` |
| `tests/nobroken.sh` | 755 | `#!/bin/sh` / `! grep -rq BROKEN src/` |
| `tests/long.sh` | 755 | `#!/bin/sh`, 주석 10행(`# l2`..`# l11`), `exit 0` |
| `tests/lowest.sh` | 755 | `#!/bin/sh` / `grep -q lowest src/stock.c` |
| `tests/highest.sh` | 755 | `#!/bin/sh` / `grep -q highest src/stock.c` |
| `tests/both.sh` | 755 | `#!/bin/sh` / `grep -q lowest src/stock.c && grep -q highest src/stock.c` |

알림은 `ait listen`으로만 받는다(규격 7.2.5, 9.5). 시나리오가 "S의 listener"라 하면 S의 worktree에서 `AIT_SESSION=S ait listen >> $T/inbox.S &`를 띄운 것이다.

### 0.2 표기

- `ok`: exit 0.
- `err X`: 오류 종류 X(규격 5.8)다. 종료 코드가 5.8의 값이고, stderr의 첫 행이 5.8의 첫 행 고정 부분으로 시작한다(예: `err E_SCOPE`는 종료 코드 128과 `fatal: bad scope:`).
- `불변`: 다음 세 출력이 명령 전후에 같다.
  - `git rev-parse HEAD`
  - `git ls-files -s`
  - `git status --porcelain --untracked-files=all`
- `rec(ID)`: `git show HEAD:.ait/ID`의 출력.
- `blob(REV:PATH)`: `git rev-parse REV:PATH`의 출력.
- `ev(S)`: `$T/inbox.S`의 `ait-event-end` 행 수.
- `ses(S)`: session 파일의 내용.
- `settle`: listener가 도는 모든 session의 tip이 현재 ref 목록과 같아질 때까지 기다린다(최대 10초).
- `wait_ev S n`: `ev(S) ≥ n`이 될 때까지 최대 10초 기다린다.
- `p`: `specs/p.yaml`.
- **명세 파일 자리의 `tests/X.sh`**(또는 `tests/X`): exec 정의 `specs/X.yaml`의 줄임이다. 그 정의는 `body: X`, `engine: exec`, `command: [./tests/X.sh]`, `inputs: [tests/X.sh]`다. 그래서 그 계약의 `file` 행은 `specs/X.yaml`(100644)과 `tests/X.sh`(100755) 두 행이다. 정의 파일도 스크립트와 함께 미리 만들어 둔다.
- **결과 채우기** `fill W OUT [TASK=OUT2]…`: 결과 틀 W의 모든 과제를 `outcome: OUT`, `basis: t`, `reason: t`로 채운다. `TASK=OUT2`로 지정한 과제는 OUT2로 채운다.
- **`-r pass`**(`-r fail`): 명령 끝의 이 표기는 다음 세 단계의 줄임이다. 기대는 셋째 실행의 것이다.
  1. 같은 명령을 `-r` 없이 실행한다. `err E_NEEDS_RESULTS`이고 `불변`이며, stdout에 `check-result <W>`가 있다.
  2. `fill <W> pass`(`fail`)로 채운다.
  3. `-r <W>`를 붙여 다시 실행한다.
- `<W>`·`<Q>`: 직전 명령이 쓴 `check-result`·`check-request` 경로(`<git-dir>/ait/check-result.yaml`·`check-request.yaml`).
- Q·W의 내용을 보는 곳은 YAML 부분집합(2.5.1)으로 읽은 값을 비교한다. 두 파일은 8.2의 쓰기 규칙(문자열은 언제나 `"…"`)으로 쓰이므로 표기가 아니라 값이 맞으면 된다. 예: `command: [./tests/pass.sh]`는 `command: ["./tests/pass.sh"]`와 같다.
- `pins(REV, ID)`: `git show REV:.ait/ID | grep -E '^(provider|file) ' | git hash-object --stdin`의 출력이다. 그 커밋에서 그 계약의 provider 행과 `file` 행을 레코드의 순서대로(각 행은 LF로 끝난다) 이은 바이트열의 해시다(5.4). 확인 행 `check reported <pins>`·`check forced <pins>`의 `<pins>`는 그 판을 가진 커밋 REV에서의 `pins(REV, ID)`다.
- 대문자 session 변수(`A` 등)는 `ait session open`의 출력값이다.
- 메시지를 생략한 쓰기 명령에는 `-m m`이 붙는다고 본다.
- "session 없이"는 `AIT_SESSION=-`로 실행한다는 뜻이다(5.1). 그냥 `AIT_SESSION`이 없으면 그 worktree의 기본 session이 쓰인다.
- "준비를 다시 한다": 0.1 고정 장치로 새 저장소를 만들고 그 시나리오의 준비를 반복한다.
- hint 문구를 인용한 곳은 규격의 문구를 그대로 적었다. 시험은 그중 명령과 ID(인자 포함)를 정확히 맞추고, 나머지 문구는 맞추지 않아도 된다(5.6). `warning:` 행, 5.8의 첫 행, 그리고 7.1.2의 경고 블록의 `warning:` 행과 계약 행처럼 규격이 "문구가 규범"이라 한 것은 정확히 맞춘다. hint의 git 명령은 마치는 커밋뿐이고, git으로 할 일은 이룰 상태로 쓴다(5.9a).
- 사건에 대한 `(reason …)` 표기는 그 이유 행이 있다는 뜻이다. 다른 이유 행(예: 그 계약을 만든 에이전트의 `reason future demand`)도 함께 있을 수 있다. 처음 몇 행을 정확히 적은 곳만 정확히 맞춘다.

---

## T01 초기화 (7.1.1, 2.1.2a)

준비: 0.1을 `ait init` 직전까지 한 저장소.

1. `tick; ait init` → `ok`. stdout은 `init created <OID>` 한 행이고, OID는 새 HEAD다.
   - 새 커밋은 `.ait/init` 하나만 더한다. 내용은 `version 2\n`이고, 제목은 `[CONTRACT] ait init`이며 기록줄이 없다.
   - 다음 여섯 hook이 모두 아래 내용으로 생긴다. 실행 권한이 있다. `reference-transaction` hook은 만들지 않는다.
     - `pre-commit`, `pre-merge-commit`, `commit-msg`, `pre-push`, `post-commit`, `post-merge`
     - 내용: `#!/bin/sh\ncommand -v ait >/dev/null 2>&1 || exit 0\nexec ait hook <이름> "$@"\n`
   - attributes에 `.ait/** merge=ait`이 한 행 있다.
   - config `merge.ait.driver`의 값은 `command -v ait >/dev/null 2>&1 && exec ait merge-driver %O %A %B %P || exec git merge-file %A %O %B`이고, `merge.ait.recursive`는 `binary`다(merge base끼리의 내부 병합에는 driver가 돌지 않는다, 7.1.1-3).
2. 다시 `ait init` → `ok`. stdout은 `init exists`이고, 커밋도 hook도 설정도 바뀌지 않는다.
3. **기존 hook은 덮어쓰지 않는다**(7.1.1-1): 준비를 다시 하고(`ait init` 전) `.git/hooks/pre-commit`에 다른 셸 hook(`#!/bin/sh` / `echo legacy >&2`, 755)을 둔 뒤 `tick; ait init`:
   - `err E_STATE`(`error: not allowed: hook '<realpath $T/r>/.git/hooks/pre-commit' is not set up for ait`). hint는 `make '<그 경로>' run 'ait hook pre-commit' with its arguments and its stdin, and stop when that fails; git runs a hook only if it is executable. In a shell hook, these lines after the first line do it …:`, 넣을 행 `if command -v ait >/dev/null 2>&1; then ait hook pre-commit "$@" || exit $?; fi`, 그리고 `then rerun this command`다.
   - 기존 파일은 그대로이고 나머지 다섯은 설치된다. init 커밋은 없다(stdout이 비어 있고 HEAD 불변. 5·6을 하지 않는다).
   - 그 행을 둘째 행으로 넣고 다시 `tick; ait init` → `init created <OID>`. 그 hook은 ait hook이 있는 것으로 보므로 H12도 없다(5.2a). `git commit`에서 ait의 pre-commit과 `legacy`가 모두 돈다. ait가 거절하면(`.ait/`를 고친 일반 커밋) 그 hook은 ait의 종료 코드로 끝나고, ait가 없는 PATH에서는 `legacy`만 돌고 커밋된다.
   - 넣은 뒤 `chmod -x`로 실행 권한을 빼면 git이 그 hook을 부르지 않으므로 ait hook이 없는 것으로 본다: `ait init` → 같은 `E_STATE`와 hint(실행 권한을 다시 주면 켜진다).
   - 셸이 아닌 기존 `commit-msg`(`#!/usr/bin/env python3`, 755)도 hint는 같다. 그 안에서 `subprocess.run(["ait", "hook", "commit-msg", *sys.argv[1:]], check=True)`처럼 부르게 하면 다음 `ait init`은 ait hook이 있는 것으로 본다(`ait`·`hook`·`commit-msg` 사이에 따옴표·쉼표·공백만 있다, 7.1.1-1).
   - 기존 `pre-push`(셸)면 넣을 행은 7.1.1-1의 세 행이고, 넣은 뒤 `git push`에서 ait와 기존 hook이 같은 stdin을 받는다. ait의 pre-push가 거절하면 hook의 종료 코드는 ait의 것이다(`E_FORMAT`이면 128, 7.1.7).
   - `git config core.hooksPath .husky`(저장소 안)이고 `.husky/pre-commit`(셸, 755)만 있으면 `ait init`은 `.husky` 아래에 아무것도 쓰지 않고, 여섯 hook마다 같은 `E_STATE`와 hint를 쓴다(경로는 `<realpath $T/r>/.husky/<이름>`. 없는 다섯은 hint대로 `#!/bin/sh`부터 새로 쓰고 `chmod +x`한다). `.husky`는 worktree 안이므로 hint 끝에 `then commit it: stage '<경로>' and run 'ait commit -m <why>'`가 있다.
   - `git config core.hooksPath .husky/_`이고 `.husky/_/.gitignore`가 `*`(도구가 만드는, 무시되는 디렉터리)이면 여섯 hook의 hint 끝에 `then commit it`이 없다(무시되는 파일이다, 7.1.1-1).
   - 기존 셸 pre-commit에 `ait hook pre-commit`이 주석 행(`# ait hook pre-commit "$@"`)에만 있으면 ait hook으로 보지 않는다: `ait init` → 같은 `E_STATE`와 hint(7.1.1-1).
4. git 저장소 밖에서 `ait init` → `err E_GIT`, hint는 `run this command inside a git repository`이고 git 명령은 없다. 저장소를 만들지 않는다.
5. **hook이 없는 clone**(5.2a, H12): 1 뒤 `git push -q origin master`. `git clone -q "$T/origin.git" "$T/c1"`의 c1에서 `ait peek` → `ok`(session이 없어도 된다, 5.1), hint가 없다(`.ait/init`은 레코드가 아니고, 레코드가 없으면 미확인이나 사고가 생길 계약도 없다). r에서 session을 열고 `tick; ait future offer src/stock.c p; git push -q origin master`. 새 clone c2에서 같은 peek → `ok`, stderr에 `hint: this clone is not set up for ait; run 'ait init'`이 있다. c2에서 `ait init` 뒤에는 그 hint가 없다.

## T02 git 통과 (7.14)

1. `ait status --porcelain`의 출력과 종료 코드가 `git status --porcelain`과 같다.
2. `ait rev-parse --verify no-such-ref`의 stderr와 종료 코드가 git과 같다.
3. 인자 없이 `ait` → 사용법을 출력하고 exit 1.

## T03 session (5.1, 7.2)

1. `S=$(ait session open --agent agent-a)` → `ok`, SID 한 행(`^S[0-9a-f]{12}$`).
2. `ait session list`에 `session $S` / `agent agent-a` / `worktree <realpath $T/r>` / `default yes` / `subs 0` / `listening no`가 이 순서로 있다(구독이 없으므로 `sub` 행이 없다).
   - `ait sub src/order.c` → stdout `sub scope src/order.c`. 이제 `ait session list`는 `subs 1`, `sub scope src/order.c`, `listening no` 순서다(7.2.2). `ait unsub src/order.c` → stdout `unsub scope src/order.c`, 다시 `subs 0`이다. 한 번 더 `ait unsub src/order.c` → `ok`, stdout이 비어 있다.
3. 다음은 모두 `err E_USAGE`다.
   - `--agent 'a b'`
   - `--agent ''`
   - `--hook x`(1단계에 없는 옵션)
   - `--agent unknown`(session 없는 기록의 자리로 예약된 이름이다, 7.2.1)
4. session 없이(`AIT_SESSION=-`) `ait future offer src/stock.c p` → `err E_SESSION`, `불변`. `AIT_SESSION` 없이(unset) 같은 명령 → `ok`. 이 worktree의 기본 session S로 동작하고 creator는 `agent-a <S>`다.
5. 모르는 SID로 `ait future offer …`, `ait peek` → 둘 다 `err E_SESSION`.
6. 다른 worktree(`git worktree add -q $T/w2 -b w2`)에서 이 SID를 쓰면 `ait future offer …`, `ait peek …` → 둘 다 `err E_SESSION`. hint에 `cd '<realpath $T/r>'`(그 session의 worktree)가 있다(5.8).
7. `ait session close $S` → `ok`. 같은 SID로 다시 close → `err E_SESSION`. 이제 `AIT_SESSION` 없이 `ait future offer …` → `err E_SESSION`(기본 session도 지워졌다).
8. **session의 worktree가 지워졌으면 같은 이름으로 다시 연다**: 6처럼 w2에서 `S2=$(ait session open --agent agent-b)`. `W2=$(realpath $T/w2)`를 적어 두고 `rm -rf $T/w2` 뒤 r에서 `AIT_SESSION=$S2 ait peek` → `err E_SESSION`. hint는 `cd`가 아니라 `its worktree is gone; close it: 'ait session close <S2>', then open a session with the same name in the worktree of your branch: 'ait session open --agent agent-b'`다(5.8. 이름은 그 session의 agent로 채운다).
   - 그 worktree의 등록이 남아 있으므로 같은 브랜치의 worktree는 `git worktree remove --force $T/w2` 뒤에 만들 수 있다: `git worktree add -q $T/w2b w2; cd $T/w2b; ait session close <S2>; ait session open --agent agent-b` → `ok`이고, 그 session의 worktree는 w2b, HEAD는 w2 브랜치다. `git worktree remove`는 그 등록 하나만 지운다.
   - (앞 항목 대신, 8의 상태에서) 닫지 않고 r에서 `ait session open --agent agent-b` → `ok`(새 SID). stderr에 `warning: agent name 'agent-b' is also used by session '<S2>' (worktree '$W2')`가 있고, hint는 `its worktree is gone; close it: 'ait session close <S2>', and keep this one if this worktree is on your branch, otherwise …`다(7.2.1. r은 master다).
9. **쓰지 못하면 git처럼 끝나고 고칠 곳을 준다**(5.7, 5.8 `E_IO`): `chmod a-w "$(git rev-parse --git-common-dir)/ait/sessions"` 뒤 `ait session open --agent agent-c` → 종료 코드 128, stderr 첫 행은 `fatal: write error:`로 시작하고 hint는 `make '<그 경로>' writable (free space, permissions), then rerun this command`다(그 경로는 sessions 디렉터리다. session 파일을 쓸 임시 파일을 그 안에 만들지 못했다, 5.8). 끝나면 `chmod u+w`로 되돌린다.
10. **커밋이 없는 브랜치에서는 init을 먼저 한다**(5.2-1, 5.8 `E_BUSY`): `git switch -q --orphan o` 뒤 `ait future offer src/stock.c p` → `err E_BUSY`, hint는 `run 'ait init' first`다. 그대로 `tick; ait init` → `init created <OID>`(빈 tree에 `.ait/init`만 담은 첫 커밋).

## T04 도우미 등록과 커밋 구조 (2.2, 5.3–5.5, 6, 7.3)

준비:
- R0에서 A(agent-a)를 연다.
- `specs/p.yaml`, `specs/guard.yaml`, `tests/guard.sh`는 추적하지 않는 파일로 둔다.
- `echo o51 >> src/order.c; git add src/order.c; echo s51 >> src/stock.c; H0=$(git rev-parse HEAD)`

```sh
tick; ait future demand src/order.c:10-40 new:src/stock_v2.c specs/p.yaml,tests/guard.sh as d -m "요구 등록"
```

1. `ok`. stdout은 정확히 `request R…`, `new D… d` 두 행이다.
2. `HEAD^`가 `$H0`이다. name-status는 `A .ait/<D>`, `A specs/guard.yaml`, `A specs/p.yaml`, `A tests/guard.sh` 네 행이다.
3. `rec(D)`:
   ```
   role demand
   state future
   consumer src/order.c:10-40 <blob($H0:src/order.c)>
   provider new:src/stock_v2.c
   file specs/guard.yaml 100644 <blob(HEAD:specs/guard.yaml)> def
   file specs/p.yaml 100644 <blob(HEAD:specs/p.yaml)> def
   file tests/guard.sh 100755 <blob(HEAD:tests/guard.sh)> input
   creator agent-a <A>
   prev -
   ```
4. 메시지는 `[CONTRACT] 요구 등록`, 빈 행, `Ait-Request`·`Ait-Agent: agent-a`·`Ait-Session: <A>` 순서다.
5. 다른 작업은 보존된다.
   - staged는 `src/order.c`뿐이다.
   - unstaged는 `src/stock.c`뿐이다.
   - `git status --porcelain -- .ait specs tests`는 비어 있다.
6. `ses(A)`에 `sub` 행이 없다. 관문은 구독을 더하지 않는다. 내 약속과 내 요구의 사건은 구독 없이 온다(6.8, 9.3).
7. 다음 실패 경우는 모두 `불변`이다.
   - 없는 명세 파일 → `err E_INCOMPLETE`.
   - `specs/p.yaml`가 부분 stage된 상태 → `err E_INCOMPLETE`.
   - 심볼릭 링크를 명세 파일로 지정 → `err E_INCOMPLETE`.
   - `.ait/x`를 명세 파일로 지정 → `err E_SCOPE`.

## T05 범위 검증 (3.1–3.4)

R0, session A. 실패는 모두 `불변`이다.

다음은 모두 `err E_SCOPE`다(FILES는 `p`).
- A 자리에 다음 값을 넣은 경우:
  - `src/order.c:10-51`, `:0-3`, `:5-3`, `:05-6`, `src/order.c:3`, `:a-b`
  - `new:src/x.c:1-2`, `new:src/order.c`
  - `src/missing.c`, `src`
  - `../x`, `src/../order.c`, `./src/order.c`, `.ait/x`, `.ait`, `src//order.c`, `src/order.c/`, `src/a,b.c`
- current demand의 provider로 `new:`를 준 경우
- `ait offer new:src/x.c p`(범위 검사가 확인 결과보다 먼저다, 5.13)

다음은 `ok`다.
- `ait demand new:src/c.c src/stock.c p -r pass`
- `ait future offer new:src/x.c p`

peek 범위:
- `ait peek src/order.c:10-51` → `err E_SCOPE`.
- `ait peek src/nothing.c` → `ok`. 출력은 머리말 2행과 빈 행뿐이다.

## T06 메시지 (5.3, 5.4)

`CL`은 `future offer src/stock.c p`다.

다음은 모두 `err E_USAGE`이고 `불변`이다.
- 메시지 없음
- `-m x -F f`
- `-m "   "`
- `ait future offer src/stock.c -m m p`(옵션은 절과 절 사이에만 온다. 절 안의 위치 인자 사이에는 올 수 없다, 7.3.1)
- `-m a -m '# ------------------------ >8 ------------------------' -m b`(git의 자르기 행. 그 아래가 메시지에서 빠지므로 붙인 기록줄이 읽히지 않는다, 5.3). 첫 행은 5.3이 정한 `error: the message hides the trailers: '# ------------------------ >8 ------------------------'`이고, 이어서 `usage: ait …` 행이 있으며 종료 코드는 129다(5.8의 `E_USAGE`). hint는 `remove that line from the message`다. `-F`로 준 메시지에 그 행이 있어도 같다.

다음은 성공한다.
1. `-m "[CONTRACT] 이미"` → 제목이 `[CONTRACT] 이미`.
2. `-m "[CONTRACT]x"` → 제목이 `[CONTRACT] [CONTRACT]x`.
3. `-m a -m b` → 메시지 전체가 `[CONTRACT] a`, 빈 행, `b`, 빈 행, 기록줄 3행이다.
4. `printf 'from stdin\n' | ait CL -F -` → 제목이 `[CONTRACT] from stdin`.
5. `$T/r`에서 `ait CL -F ../msg.txt` → `$T/msg.txt`의 내용이 메시지가 된다.
6. `-m s -m "Ait-Agent: evil"` → 성공. 본문 문단은 기록줄로 읽히지 않는다: `git log -1 --format='%(trailers:key=Ait-Agent,valueonly)'`는 `agent-a` 한 행이다.
7. `-m a -m --- -m b`(본문에 `---` 행이 있다) → 성공. `---`는 메시지를 자르지 않으므로 붙인 기록줄을 5.4의 파서로 다시 읽어도 그대로 읽힌다(5.3): `git log -1 --format='%(trailers:key=Ait-Request,valueonly)'`가 이 요청의 RID 한 행이다.

## T07 묶음·alias·원자성 (7.3)

1. 아래 명령 → `ok`.

   ```sh
   tick; ait future demand src/order.c src/stock.c p as d future offer src/stock.c tests/guard.sh as o undertake d o
   ```

   - stdout에 `new D d`, `new O o`, `undertaken D O` 행이 이 순서로 있다.
   - name-status: `A .ait/D`, `A .ait/O`, `A .ait/undertake/D-O`, `A specs/guard.yaml`, `A specs/p.yaml`, `A tests/guard.sh`.
   - undertake 파일 내용은 `state undertaken\nprev -\n`이다(D가 future일 때 맺었다, 2.2.2).
2. `ait contract future offer src/order.c p`와 `ait ct future offer src/order.c p` → 둘 다 `ok`(앞에 붙여도 된다).
3. 다음은 모두 `err E_ID`이고 `불변`이다. 마지막 절이 실패하면 앞 절도 기록되지 않는다.
   - `ait undertake x O`
   - `ait undertake d O future demand … as d`
   - `ait future offer src/stock.c p as o2 undertake D Oaaaaaaaaaaaa`
4. 다음은 모두 `err E_USAGE`다.
   - alias 중복
   - 예약어나 형식에 맞지 않는 alias(`undertake`, `D1`, `1a`)
   - `fulfill`을 절로 씀
   - 알 수 없는 토큰
   - 인자 부족
   - `-r`를 두 번 씀, 파일 없이 `-r`만 씀
   - `-r`와 `--force`를 함께 씀
   - `--attest`, `--check`(없는 옵션)

## T08 undertake·release·cancel·no-op (1.6, 2.3, 7.3.2)

준비:

```sh
tick; ait future demand src/order.c src/stock.c p as d future offer src/stock.c p as o future offer src/stock.c p as o2 offer src/stock.c p as o3 undertake d o -r pass
```

1. `ait undertake O D` → `err E_ID`.
2. `ait undertake D O` → `noop`(이미 undertaken. 바뀌는 것이 없다).
3. `ait release D O2` → `noop`(undertaken이 아님).
4. `ait undertake D O3` → `err E_STATE`, `불변`. current offer O3는 future demand D를 떠맡을 수 없다(확인된 O가 확인되지 않은 D를 포함할 수 없다, 1.6). stderr의 hint에 `ait future offer src/stock.c specs/p.yaml as o undertake <D> o -m <why>`가 있다(future offer로 떠맡는다. D의 provider와 정의, 5.8).
5. `tick; ait future cancel O` → `ok`. 떠맡긴 상태로도 cancel할 수 있다.
   - `rec(O)`의 state는 cancelled이고, prev는 이전 판이다.
   - `ait todo D` → `undertaken-by <O> cancelled agent-a`.
6. `tick; ait release D O` → `released <D> <O>`. cancel에 release가 필요하지는 않다.
7. 다음 결과:
   - `ait undertake D O` → `err E_STATE`(O가 cancelled). hint는 `undertake it with a future offer: 'ait future offer src/stock.c specs/p.yaml as o undertake <D> o -m <why>'`다(끝난 O로는 떠맡을 수 없다, 5.9a의 전이 hint 10).
   - `ait undertake D O2 release D O2` → stdout이 정확히 `noop`이고 `불변`이다. undertake 파일도 생기지 않는다.
8. 다음 결과:
   - `ait future cancel O3`(current) → `err E_STATE`.
   - `ait future cancel O`(이미 cancelled) → `noop`.
9. current끼리도 된다: `tick; ait demand src/order.c src/stock.c p as d4 undertake d4 O3 -r pass` → `ok`. 과제는 새 D4의 `+1:specs/p.yaml` 하나다(undertake는 확인하지 않는다).

## T09 drop·amend (1.4, 2.3, 7.3.2)

준비:

```sh
tick; ait offer src/stock.c:1-10 p as o1 offer src/order.c p as o4 future demand src/order.c src/stock.c p as d1 -r pass
```

1. `tick; ait future drop O1 as x` → `rec(X)`는 `role drop` / `state future` / `target <O1>` / `creator agent-a <A>` / `prev -`. 철회할 범위는 적지 않는다. `ait peek <X>`의 `scope`는 `src/stock.c:1-10`이다(target의 범위).
2. `ait future drop D1` → `err E_STATE`(future는 철회 대상이 아니다). `ait future drop X` → `err E_ID`.
3. `tick; ait future amend D1 - src/stock.c:5-9 -` → `changed D1`. provider는 `src/stock.c:5-9 <blob>`이다.
4. `ait future amend D1 src/order.c - -m m` → `err E_USAGE`(demand의 인자는 셋이다). `ait future amend D1 - - -` → `noop`(바꿀 것이 없는 future).
5. `tick; ait future amend D1 - - tests/pass.sh` → file 행은 `file specs/pass.yaml 100644 <blob> def`와 `file tests/pass.sh 100755 <blob> input` 두 행이다(정의와 그 input).
6. current도 amend할 수 있지만 확인 결과가 필요하다: `ait amend O1 src/stock.c p` → `err E_NEEDS_RESULTS`, 과제 `<O1>:specs/p.yaml`. `-r pass`로 → `changed <O1>`, `check <O1> reported`.
   - **빈 amend**(바꿀 필드 없는 amend, 6.5): `ait amend O4` → `err E_NEEDS_RESULTS`, 과제 `<O4>:specs/p.yaml`. `tick; ait amend O4 -r pass` → `noop`. O4는 같은 고정을 이미 `reported`로 확인했으므로 확인 행까지 그대로이고, 커밋이 생기지 않는다(2.2.2, 6.5, 6.6).
   - O4를 `--force`로 만들었다면(`check forced <PINS>`) 같은 `-r pass` → `request <R>`, `check <O4> reported`이고, 레코드는 확인 행만 `check reported <pins(HEAD, O4)>`로 바뀐다(PINS는 그대로다. 테스트로 통과한 사실이 더해졌다, 2.2.2). 뜻은 같으므로 알림은 없다(9.2).
   - 끝나면 준비를 다시 하고 1을 다시 한다.
7. `tick; ait future amend X O4` → target은 `<O4>`다.
8. **한 계약을 여러 drop이 겨눌 수 있다**: `tick; ait future drop O4 as x2` → `ok`. `tick; ait fulfill X2` → `fulfilled <X2>`, `withdrawn <O4>`(걸린 계약이 없어 과제가 없다). 이어서 `ait fulfill X` → `err E_STATE`(target이 current가 아니다). hint는 H2의 X 형 `'<O4>' can no longer be withdrawn; run 'ait future cancel <X> -m <why>'`다(5.9a의 전이 hint).
   - `ait todo <X>`의 stderr hint는 `'<O4>' can no longer be withdrawn; run 'ait future cancel <X> -m <why>'`(H2)다. `ait fulfill <X>`를 권하는 H10은 없다(drop의 H10은 target이 current일 때만 쓴다, 5.9a).
9. `ait peek <O4>`에 `drop <X>` 행이 있다(ID 인자는 view와 상관없다). `tick; ait future cancel X` → `ok`. 이후 `ait peek <O4>`에 `drop` 행이 없다(cancelled인 drop은 보이지 않는다, 7.9.4).
10. **target이 되돌려진 drop**: 준비를 다시 하고 1을 한다(X는 O1을 겨눈다). `tick; ait future amend O1` → `changed <O1>`(O1은 future가 된다, 1.5d). `ait todo <X>`의 stderr hint는 `'<O1>' is not current now; '<X>' can be fulfilled after '<O1>' is fulfilled again, or run 'ait future cancel <X> -m <why>'`(H2)이고 H10은 없다. `ait fulfill X` → `err E_STATE`(target이 current가 아니다).

## T10 current 등록의 확인 결과 (6.5, 8)

각 명령 앞에 `tick`을 둔다. 2와 3은 1의 W를 쓰고, 그 밖의 항목은 준비를 다시 한 새 저장소에서 따로 한다(앞 항목이 HEAD와 명세 파일을 바꾸기 때문이다).

1. `ait offer src/stock.c tests/pass.sh` → `err E_NEEDS_RESULTS`, `불변`.
   - stdout에 `task +1:specs/pass.yaml exec`, `check-request <Q>`, `check-result <W>`가 있다.
   - Q에는 `input`, `tree`, 과제 `+1:specs/pass.yaml`의 `role offer`, `provider src/stock.c`, `engine exec`, `body pass`, `command: [./tests/pass.sh]`, `success: [0]`이 있다(값으로 비교한다, 0.2).
   - W에는 같은 `input`과 과제 하나가 빈 outcome으로 있다.
2. `fill <W> pass` 후 같은 명령에 `-r <W>` → `ok`.
   - stdout에 `check <O> reported`가 있다. `rec(O)`에 확인 행 `check reported <pins(HEAD, O)>`가 `file` 행 다음, `creator` 행 앞에 있다(2.2.2). 기록줄에는 확인 행이 없다(5.4).
   - 메시지는 `[CONTRACT] m`, 빈 행, W의 내용(`task:` 값의 `+1`을 `<O>`로 바꾸고 `|` 블록 끝의 빈 행을 뺀 것, 8.4), 빈 행, 기록줄(`Ait-Request`·`Ait-Agent`·`Ait-Session`) 순서다.
3. (2 대신) `fill <W> fail` 또는 `fill <W> unknown`으로 → `err E_CHECK_FAILED`, `불변`. stderr에 `+1:specs/pass.yaml`이 있다. fail이면 hint는 코드 먼저 `commit code that meets it first: stage <your code> and run 'ait commit -m <why>', then rerun this command`(관문은 HEAD의 코드를 확인하고, provider `src/stock.c`는 코드다)와, O가 이 요청이 새로 만드는 current 계약이므로 약속으로 하기 `if it does not hold yet, make it a promise: 'ait future offer src/stock.c specs/pass.yaml -m <why>'`다(같은 명령의 `offer` 절을 `future offer`로 바꾸고 옵션은 `-m <why>`만 남긴다, 5.8, 5.9a). O는 HEAD에 없으므로 되돌리기 hint는 없다. unknown이면 hint는 `check it where its environment is available (see '<Q>') and rerun with -r '<W>'`, `if it is trivial, add --force (recorded as forced)`, 그리고 fail과 같은 약속으로 하기이고, `commit code …`는 없다(5.8. 계약마다의 hint는 fail·unknown 모두 같다).
4. prompt 정의도 같다: `ait offer src/stock.c p -r pass` → `check <O> reported`.
5. 정의가 여럿이면 과제도 여럿이다: `ait offer src/stock.c p,tests/pass.sh` → `task +1:specs/p.yaml prompt`, `task +1:specs/pass.yaml exec`. 다음은 모두 `err E_FORMAT`, `불변`이다.
   - 과제 하나를 지운 결과
   - 없는 과제를 더한 결과
   - 같은 과제를 두 번 쓴 결과
   - basis가 빈 pass
   - outcome이 `ok`
6. 확인 입력: `-r` 없이 같은 명령을 두 번 실행하면 두 W의 `input`이 같다(새 레코드의 무작위 ID는 들어가지 않는다). W를 채운 뒤 작업 폴더의 `specs/p.yaml`를 고치고 `-r <W>` → `err E_NEEDS_RESULTS`(확인 입력이 바뀌었다). 새 W가 쓰인다.
7. ait는 아무것도 실행하지 않는다: 작업 폴더의 `tests/pass.sh`를 `#!/bin/sh` / `touch $T/ran` 으로 바꿔 둔 채 1~2를 해도 `$T/ran`이 생기지 않는다.
8. `ait future offer src/stock.c tests/pass.sh` → `ok`. future는 결과가 필요 없다.
9. `--force`: `ait offer src/stock.c tests/pass.sh --force` → `ok`, `check <O> forced`. `rec(O)`의 확인 행은 `check forced <pins(HEAD, O)>`이고 결과 문단은 없다. `-r <W> --force` → `err E_USAGE`.
10. `+n`은 새 레코드 순서다: `ait future drop <K> offer src/stock.c p`(K는 current)의 과제 이름은 `+2:specs/p.yaml`이다(drop이 `+1`). `-r pass`로 커밋하면 메시지의 결과에서 `+2`가 새 O의 ID로 바뀌어 있다.

## T11 직접 작성: contract new · add · commit (6, 7.4–7.6)

1. `ait contract new demand` → 출력은 `path .ait/D<12hex>` 한 행이다(이하 Dn).
   - 작업 폴더의 Dn 내용은 `role demand\nstate future\nconsumer PATH\nprovider PATH\nfile PATH\n`이다.
   - stage된 것은 없다.
2. 틀을 채운다.
   - Dn: 키 순서를 바꾸고, BLOB·creator·prev 없이 쓴다. provider `src/stock.c`, consumer `src/order.c:1-5`, file `specs/q.yaml` 한 행(정의만 적는다).
   - exec 정의 `specs/q.yaml`(`body: q`, `engine: exec`, `command: [./tests/q.sh]`, `inputs: [tests/q.sh]`)와 `tests/q.sh`(755, pass)를 만든다.
   - `ait contract add .ait/Dn specs/q.yaml tests/q.sh` → `staged` 세 행.
3. `tick; ait contract commit -m "직접 작성"` → `request`, `new Dn`.
   - `rec(Dn)`은 정규형이다. 범위 BLOB, `file specs/q.yaml 100644 <blob> def`와 관문이 펼친 `file tests/q.sh 100755 <blob> input`, `creator agent-a <A>`, `prev -`가 채워져 있다.
   - 작업 폴더의 Dn도 정규형으로 바뀐다.
   - 작업 폴더는 깨끗하다.
4. 직접 수정: `specs/q.yaml`를 고친 뒤 `contract add`, `tick; contract commit` → `changed Dn`. 다시 고정되고, prev는 이전 판이다.
4a. **명세 정의 틀**(7.4.1a): `ait contract new spec specs/t.yaml exec` → stdout은 `path specs/t.yaml` 한 행이고, 파일 내용은 7.4.1a의 exec 틀과 같다. stage된 것은 없다. `body`의 자리표시 행을 `t`로, `command`를 `[./tests/t.sh]`로, `inputs`를 `[tests/t.sh]`로 바꾸고 `tests/t.sh`(755, pass)를 만들면 `tick; ait future offer src/stock.c specs/t.yaml -m t` → `ok`(주석은 그대로 둬도 된다). 같은 경로로 다시 `ait contract new spec specs/t.yaml prompt` → `err E_STATE`, 파일은 그대로다. `ait contract new spec specs/u.yaml prompt`의 내용은 prompt 틀과 같다.
5. 거절 경우. 각 항목 뒤에 4a를 마친 뒤의 HEAD로 되돌린다(`git reset -q --hard <그 HEAD>; git clean -fdq`. 커밋한 항목도 그렇다).

| # | 조작 | 기대 |
| --- | --- | --- |
| a | `ait contract add src/stock.c` | `err E_MIXED`, stage 없음 |
| b | `tests/q.sh`와 `specs/q.yaml`를 고친 뒤 `ait contract add specs/q.yaml` | `err E_INCOMPLETE`, `unstaged tests/q.sh`, stage 없음(작업 폴더에 있지만 주어진 PATH만 stage하면 어긋난다, 7.5.2). hint는 `stage them together: 'ait contract add specs/q.yaml tests/q.sh'`다(이 명령의 PATH에 더한 것, 5.8) |
| c | `specs/q.yaml`와 `src/stock.c`를 고침, `contract add specs/q.yaml`, `git add src/stock.c`, `contract commit` | `err E_MIXED`, `impl src/stock.c`. hint는 `unstage the code ('src/stock.c'), then rerun this command; when the code is ready, stage it and run 'ait commit -m <why>'`다(5.8) |
| d | 추적하지 않는 `.ait/D222222222222`가 있는 상태에서 `contract commit` | `err E_INCOMPLETE` |
| e | Dn의 state를 current로 고쳐 add·commit | `err E_STATE`. state만 바꿨으므로 hint는 `fulfill it instead: restore '.ait/Dn' as in 'HEAD', then 'ait fulfill Dn -m <why>' to get the tasks`다(5.9a의 전이 hint 4. 다른 행도 바꿨으면 복원 대신 `set 'state future' back in '.ait/Dn', then 'ait contract add .ait/Dn' and 'ait contract commit -m <why>'`다) |
| f | creator를 바꿔 add·commit | `err E_STATE`. hint는 `restore '.ait/Dn' as in 'HEAD'`다 |
| f2 | creator와 함께 provider 행도 고쳐 add·commit | `err E_STATE`. 고친 것을 잃지 않게 hint는 `restore it` 대신 `remove the 'creator' line from '.ait/Dn' (HEAD's value is kept), then 'ait contract add .ait/Dn' and 'ait contract commit -m <why>'`다(5.9a 전이 hint 2, 2.2.3). 그대로 하면 `changed Dn`이고 creator는 HEAD의 것이다 |
| g | `git rm -q .ait/Dn` 후 `contract commit` | `err E_STATE`(`E_INCOMPLETE`가 아니다. 지운 레코드의 계약은 관련 경로 검사에서 빠지고 관문이 판정한다, 7.6.1). hint는 f와 같은 복원 명령이다(레코드는 지우지 않는다, 2.1.5, 전이 hint 2) |
| h | stage 없이 `contract commit` | stdout `noop`, exit 0 |
| h2 | Dn이 선언한 `specs/q.yaml`을 작업 폴더에서만 고치고(stage하지 않는다) `contract commit` | stdout은 `noop`과 `unstaged specs/q.yaml` 두 행, exit 0. stderr에 `hint: stage it: 'ait contract add specs/q.yaml', then rerun this command`가 있고 `warning:`은 없다(7.6.1. 커밋되지 않았다는 것은 위험이 아니라 이 명령의 결과다, 규칙 6) |
| i | `.ait/Dxyz`를 add·commit | `err E_FORMAT`. 파일 이름이 ID가 아니므로 레코드 경로가 아니고(2.1.3) 고칠 수 없다. hint는 `remove '.ait/Dxyz'`이고, 그 경로가 HEAD에 없으므로 `ait contract commit`은 잇지 않는다(5.10). 그대로 하면 `ait contract commit` → `noop`(T가 H tree와 같다, 6.6) |
| u | `.ait/Dn`의 provider 행을 고치면서 마지막 LF를 지우고 add·commit | `err E_FORMAT`. 틀린 판이 이 요청에만 있으므로 hint는 GOOD 복원이 아니라 `fix it in this request (see the error line), then 'ait contract add .ait/Dn' and 'ait contract commit -m <why>'`다(5.10. 복원하면 고친 provider를 잃는다) |
| j | `.ait/init`의 내용을 `version 3`으로 바꿔 add·commit | `err E_FORMAT`. hint는 `rewrite '.ait/init' as the single line 'version 2' with mode 100644, then 'ait contract add .ait/init' and 'ait contract commit -m <why>'`다(2.1.2a, 5.10. init 파일은 내용이 한 가지뿐이다) |
| j2 | 준비를 다시 하되 0.1의 `ait init` 대신 `echo 'version 1' > .ait/init; git add .ait/init; tick; git commit -qm bad`(ait-mini의 init 파일. 바른 init 파일이 있었던 커밋이 없다). `ait session open --agent agent-a` 뒤 `ait future offer src/stock.c p` | `err E_FORMAT`, `불변`. hint는 `rewrite '.ait/init' as the single line 'version 2' with mode 100644, then 'ait contract add .ait/init' and 'ait contract commit -m <why>'`다(5.10). 그대로 하면 `ok`이고 `git show HEAD:.ait/init`은 `version 2`다 |
| k | `.ait/Dxyz`를 `git add` 후 `tick; git commit -q --no-verify -m bad`로 HEAD에 넣고(끝나면 `git reset -q --hard HEAD^`) `ait future offer src/order.c p` | `err E_FORMAT`, `불변`. hint는 `remove '.ait/Dxyz'`이고, 그 경로가 HEAD에 있으므로 이어서 `then 'ait contract commit -m <why>'`가 있다(5.10). 그대로 하면 `ait contract commit` → `ok`이고 `.ait/Dxyz`를 지운 커밋이 생긴다(T가 H tree와 다르다, 6.6) |
| l | 작업 폴더의 `.ait/Dn`에 `# x` 행을 덧붙인 채 `cd src`에서 `ait future offer src/stock.c p` | `err E_DIRTY`, `불변`. 버리는 hint는 `or drop them: restore '.ait/' to HEAD's version and remove the new files there`다(경로는 cwd와 상관없이 최상위 기준이다, 5.9a). 그대로 하면 같은 명령이 `ok`다 |
| m | `specs/q.yaml`과 `src/stock.c`를 고쳐 둘 다 `git add`한 뒤 `cd src`에서 `tick; git commit -qm x` | `err E_GUARD`(pre-commit). hint는 `unstage 'specs/q.yaml' and commit the rest; …`다(5.8) |
| n | `.ait/Dn`의 consumer 행을 `consumer src/order.c:1-6`으로 고친 채(stage하지 않는다) `src/stock.c`를 고쳐 `git add`하고 `ait commit -m x` | `err E_DIRTY`(`.ait/`), `불변`. 남기는 hint 앞에 `unstage 'src/stock.c' first`가 있다(이어지는 `ait contract commit`은 stage된 코드를 받을 수 없다, 5.9a의 로컬 변경) |
| o | `specs/q.yaml`의 `inputs`에서 `tests/q.sh`를 빼고 `git rm -q tests/q.sh` 뒤 `ait contract add specs/q.yaml` | `staged specs/q.yaml`(정의에서 뺀 `tests/q.sh`는 관련 경로가 아니다, 7.5.2). 이어서 `tick; ait contract commit -m x` → `changed Dn`이고 `rec(Dn)`에 `tests/q.sh` 행이 없다. 지운 `tests/q.sh`도 그 커밋에 든다(HEAD에서 선언된 명세 파일이므로 계약 경로다, 6.1) |
| p | `tests/q.sh`만 지우고(정의는 그대로) `ait contract add specs/q.yaml` | `err E_INCOMPLETE`, `missing tests/q.sh`, stage 없음. hint는 `restore 'tests/q.sh' from HEAD, or stop declaring it in 'specs/q.yaml'`이고 `ait contract add tests/q.sh`는 없다(5.8) |
| q | `specs/q.yaml`과 `src/stock.c`를 고친 채(stage하지 않는다) `tick; git commit -qam x` | `err E_GUARD`(pre-commit). hook이 받은 index는 `-a`의 임시 index이므로 hint는 `commit without -a or paths: stage the code ('src/stock.c') and commit it, then commit them through ait: 'ait contract add specs/q.yaml' and 'ait contract commit -m <why>'`다(빼도 듣지 않는다, 5.8) |
| q2 | q처럼 하되 먼저 `.ait/<Dn>`을 고쳐 `git add .ait/<Dn>`해 둔다 | `err E_GUARD`. `commit without -a or paths` 앞에 `unstage '.ait/<Dn>' first`가 있다(이 worktree의 index에 stage된 계약 경로도 빼야 `-a` 없는 commit이 지난다, 5.8). hint를 차례로 따르면 코드 커밋과 계약 커밋이 따로 생긴다 |
| s | `ait contract new drop`이 만든 `.ait/<X>`에 `target <Dn>`을 써서(Dn은 future) add·commit | `err E_STATE`. hint는 `'<Dn>' is not current; a drop withdraws a current contract`, 그 drop을 빼는 `remove '.ait/<X>'`(HEAD에 없는 새 레코드다), 그리고 Dn이 이 session의 future이므로 `if it is no longer needed, cancel it`과 `set 'state cancelled' in '.ait/<Dn>'`이다(5.9a 전이 hint 8, contract commit의 형) |
| r | (마지막에 한다) `tick; ait future cancel Dn` 뒤 작업 폴더의 `.ait/<Dn>`의 state를 `future`로, provider를 `src/stock.c:1-3`으로 고쳐 add·commit | `err E_STATE`. hint는 `'<Dn>' is closed`, `to need it again, move your change to a new contract in this request: 'ait contract new demand', fill it with 'consumer src/order.c:1-5', 'provider src/stock.c:1-3', 'file specs/q.yaml', then 'ait contract add' it`, 그리고 `restore '.ait/<Dn>' as in 'HEAD'` 차례다(5.9a 전이 hint 3, contract commit의 형. 고친 provider는 요청의 판에서 채운다. 먼저 복원하면 그것을 잃는다. 도우미 `ait future demand`는 이 요청이 `.ait/`를 차지해 `E_DIRTY`다). 그대로 하고 `tick; ait contract commit -m x` → `request`, `new <새 ID>`. 새 계약의 provider는 `src/stock.c:1-3`이고 `rec(Dn)`은 cancelled 그대로다 |

## T12 current 계약의 명세 파일 수정과 mode (2.4, 6.2, 6.5)

준비:
- `tests/c.sh`(755, `#!/bin/sh` / `exit 0`)와 그 exec 정의 `specs/c.yaml`을 만들고 `tick; ait offer src/stock.c tests/c.sh as o -r pass`
- `specs/n.yaml`(`body: n`)을 만들고 `tick; ait offer src/order.c specs/n.yaml as n -r pass`

1. `tests/c.sh`를 고쳐 add 후 `contract commit` → `err E_NEEDS_RESULTS`, 과제 `<O>:specs/c.yaml`. 같은 명령의 `-r fail` → `err E_CHECK_FAILED`. stderr의 hint는 contract commit의 코드 먼저(이 요청은 `.ait/`를 바꾸지 않았다) `unstage 'tests/c.sh' first`, `stage <your code> and run 'ait commit -m <why>'`, `ait contract add tests/c.sh`, `ait contract commit -m <why>`를 차례로 담은 것과, O가 HEAD에서 current이므로 요청 안의 되돌리기 `to change '<O>' without a check, reopen it: set 'state future' in '.ait/<O>', then 'ait contract add .ait/<O>' and 'ait contract commit -m <why>'`다(5.8, 5.9a의 고치는 명령). 따로 실행하는 `ait future amend <O>`는 없다(관문이 거절한 요청은 그 요청 안에서 고친다).
2. `tick; contract commit -r pass` → `changed <O>`, `check <O> reported`. `tests/c.sh`의 `input` 행이 새 blob으로 다시 고정된다.
3. `specs/n.yaml`를 고쳐 `contract add` 후 `tick; contract commit -r pass` → `check <N> reported`.
4. `specs/c.yaml`에서 `engine`·`command` 행을 지워 prompt로 바꾸고(inputs는 남긴다) add 후 `tick; contract commit -r pass` → `check <O> reported`. 과제는 `<O>:specs/c.yaml prompt`이고, `rec(O)`의 `specs/c.yaml` 행이 새 blob으로 다시 고정된다.
5. 준비를 다시 한 뒤, 같은 파일을 쓰는 future D가 있을 때(`tick; ait future demand src/order.c src/stock.c tests/c.sh as d`) `tests/c.sh`를 고쳐 `contract add` 후 `tick; contract commit -r pass` → `changed <D>`, `changed <O>`. 과제는 `<O>:specs/c.yaml` 하나다(D는 future라 결과가 필요 없다).
6. 작업 폴더의 `.ait/<O>`에 `file specs/n.yaml` 행을 더해 `contract add .ait/<O>` 후 `tick; contract commit -r pass` → `changed <O>`, `check <O> reported`. 과제는 `<O>:specs/c.yaml`과 `<O>:specs/n.yaml`이다(current의 명세 파일 목록도 바꿀 수 있지만 결과가 필요하다).
7. **관문의 변경에 걸린 계약도 함께 확인한다**(1.5b, 6.5): 준비를 다시 한 뒤 명세 파일 `specs/n.yaml`을 provider로 하는 current offer M을 둔다(`tick; ait offer specs/n.yaml p as m -r pass`). `specs/n.yaml`을 고쳐 `contract add specs/n.yaml` 후 `contract commit` → `err E_NEEDS_RESULTS`, `불변`. 과제는 `<M>:specs/p.yaml`과 `<N>:specs/n.yaml`이다(N은 고정이 바뀌고, M은 이 변경에 걸린다). stderr의 hint에 `set 'state future' in '.ait/<M>'`과 `set 'state future' in '.ait/<N>'`이 있다(확인 집합 중 HEAD에서 current인 계약마다 요청 안의 되돌리기, 5.8, 5.9a).
   - 같은 변경에서 `fill <W> pass <M>:specs/p.yaml=fail` 후 `contract commit -r <W>` → `err E_CHECK_FAILED`, `불변`. hint는 M의 provider `specs/n.yaml`이 명세 파일이므로 명세 파일 고치기 `fix 'specs/n.yaml', then 'ait contract add specs/n.yaml' and 'ait contract commit -m <why>'`이고, M이 HEAD에서 current이므로 이어서 요청 안의 되돌리기 `set 'state future' in '.ait/<M>'`이 있다(5.8, 5.9a). 조건을 바꾸는 `ait future amend <M> - <new defs>` 형은 없다(관문에서는 조건 변경이 이미 이 요청이다).
   - `tick; contract commit -r pass` → stdout은 `request <R>`, `changed` 두 행(M·N, ID 순), `check` 두 행(`<M> reported`·`<N> reported`, ID 순)이다. `rec(M)`의 provider는 `specs/n.yaml <blob(HEAD:specs/n.yaml)>`로 다시 고정되고, `ait peek now <M>`에 `unverified` 행이 없다(관문은 미확인을 만들지 않는다, 1.5a).
   - 준비와 M을 다시 둔 뒤 같은 변경을 `tick; contract commit --force`로 → `check <M> forced`, `check <N> forced`.
   - **이행에서는 명세 파일 고치기를 도우미로 먼저 한다**(5.9a): 준비와 M을 다시 둔 뒤 `tick; ait future offer specs/n.yaml p as m2`. stage 없이 `ait fulfill M2`로 W를 받아 `fill <W> fail` 후 `ait fulfill M2 -r <W>` → `err E_CHECK_FAILED`, `불변`. hint는 `fix 'specs/n.yaml' and commit it first: 'ait amend <N> - specs/n.yaml -m <why>', then rerun this command`다(이행은 명세 파일을 담을 수 없고, 도우미는 stage된 변경을 커밋하지 않는다). M2는 HEAD에서 future이므로 되돌리기 hint는 없다.
   - **provider가 명세 파일이면 커밋 안내도 계약 입구다**(5.9a의 stage 규칙): 준비와 M을 다시 둔 뒤 아무것도 바꾸지 않고 `ait amend M -r fail`(빈 amend) → `err E_CHECK_FAILED`, `불변`. hint는 명세 파일 고치기의 도우미 형으로, `specs/n.yaml`을 선언한 N의 절을 같은 명령에 더한 `ait amend <M> amend <N> - specs/n.yaml -m <why>`다(M의 provider `specs/n.yaml`은 명세 파일이고 이 요청이 싣지 않았다. offer의 형, 5.9a). `stage`와 `ait commit`은 없다(명세 파일은 `ait commit`으로 커밋할 수 없다, 7.7a). M은 HEAD에서 current이므로 요청 안의 되돌리기 `ait future amend <M> -m <why>`(같은 명령의 `amend` 절을 `future amend`로 바꾼 것)도 있다(5.8).
8. **되돌리며 조건을 바꾸면 결과가 필요 없다**(1.5b, 10.5): 준비를 다시 한 뒤 1처럼 `tests/c.sh`를 고쳐 add해 둔다. 1의 hint대로 `.ait/<O>`의 `state current`를 `state future`로 고쳐 `ait contract add .ait/<O>`, `tick; ait contract commit -m 조건` → `ok`, stdout은 `request <R>`, `changed <O>` 두 행이다(`check` 행이 없다). `rec(O)`는 `state future`이고 `tests/c.sh`의 `input` 행이 새 blob으로 다시 고정된다. 되돌리는 계약은 고정이 바뀌어도 확인 집합에 들지 않는다(1.5b). 다시 current가 되려면 이행한다(H10).
9. **레코드를 바꾼 직접 작성 요청은 요청 안에서 고친다**(5.9a의 고치는 명령): 준비를 다시 한 뒤 작업 폴더의 `.ait/<O>`의 provider 행을 `provider src/stock.c:1-5`로 고쳐 `ait contract add .ait/<O>`, `contract commit`으로 W를 받아 `fill <W> fail` 후 `contract commit -r <W>` → `err E_CHECK_FAILED`, `불변`.
   - hint에 코드 먼저 형(`unstage`, `ait commit`)이 없다. 이 요청이 `.ait/`를 바꿨으므로 `ait commit`은 `E_DIRTY`에 막힌다(5.2-3). 따로 실행하는 `ait future amend <O>`도 같은 까닭으로 없다.
   - hint에 요청 안의 되돌리기 `to change '<O>' without a check, reopen it: set 'state future' in '.ait/<O>', then 'ait contract add .ait/<O>' and 'ait contract commit -m <why>'`가 있다. 그대로 하면 `tick; ait contract commit -m x` → `ok`, `changed <O>`이고 `check` 행이 없다. `rec(O)`는 `state future`, provider `src/stock.c:1-5 <blob>`이다.

## T13 원시 편집은 막지 않되 드러나고, 공유에서 막힌다 (규칙 1·9, 1.5a, 2.4, 5.10, 7.1.2, 7.1.6, 7.6, 7.9.5)

준비: `tick; ait future offer src/stock.c tests/pass.sh as o; git push -q origin master`.

1. `echo '# x' >> tests/pass.sh; tick; git commit -qam raw` → exit ≠ 0, stderr에 `error: contract files cannot be changed by a plain commit:`와 `hint: … ait contract add …`, HEAD 불변.
2. `tick; git commit -q --no-verify -am raw` → 커밋된다(규칙 9: 의도적 우회는 막지 않는다). post-commit이 `warning: spec files do not match the contract:`로 시작하는 행과 `E_INCOMPLETE`의 HEAD 형 hint(`ait contract commit -m <why>`, 5.8)를 쓴다(고정과 다른 명세 파일은 사고다, 7.1.8). `ait peek <O>`의 이력 마지막 행은 `event - ? edited tests/pass.sh`다.
3. `ait contract push origin` → exit ≠ 0. stderr 첫 행이 `error: spec files do not match the contract:`로 시작하고(7.10.1-3. raw 커밋은 명세 파일만 바꿨으므로 계약 커밋이라 `E_EMPTY`가 아니다), origin은 그대로다. 그냥 `git push origin master`도 pre-push hook이 같은 이유로 거절한다(7.1.6).
4. stock.c를 바꿔 stage → `ait fulfill O` → `err E_INCOMPLETE`. stdout 보고에 `invalid incomplete …`과 `ready no`가 있다. 끝나면 되돌린다.
5. 복구: `tick; ait contract commit -m "판 다시 고정"` → `changed <O>`. 이력에 `event <R> agent-a respecified tests/pass.sh`가 더해진다. 이후 `ait contract push origin` → `ok`.
6. 위조: `.ait/<O>`의 state를 손으로 current로 바꾸고 `tick; git commit -q --no-verify -am forge`(Cf).
   - 커밋된다. post-commit이 `warning: '<O>' at HEAD was changed outside ait's rules`와 `hint: restore '.ait/<O>' as in '<Cf^>', then 'ait contract commit -m <why>'`를 쓴다(확인 행이 없는 current는 사고 판이다, 1.5a, 2.3.3, 7.1.8).
   - `ait peek <O>`의 이력 마지막 행이 `event - ? fulfilled`다. 기록줄이 없는 우회의 흔적이 드러난다. 미확인이 아니므로 `unverified` 행은 없고, `ait peek now <O>`는 O를 보여 주며 stderr에 같은 경고와 hint를 쓴다(5.10).
   - `ait future offer src/order.c p` → `err E_ACCIDENT`, `불변`, 같은 hint. hint대로 복원하면 `changed <O>`이고 `rec(O)`는 `state future`, prev는 `blob(<Cf^>:.ait/<O>)`다(5.10).
7. **revert로 지운 레코드도 보인다**: 준비를 다시 하고 O를 만든 요청 커밋을 `git revert --no-edit`한다(`.ait/<O>`가 지워진다. 충돌 없는 revert는 git이 커밋 hook을 부르지 않는다). `ait peek <O>` → `id <O>`와 `event - ? deleted <revert OID>` 행.
8. **손으로 다시 고정한 것은 확인이 아니다**(1.5a, 2.3.3, 7.9.5): 준비를 다시 한 뒤 `tick; ait offer src/order.c:1-5 p as k -r pass`(Rk). order.c 3행을 바꾸고, `.ait/<K>`의 provider 행의 BLOB을 `git hash-object src/order.c`의 값으로 손으로 고쳐(범위 `1-5`는 그대로다. 곧 다시 고정이다, 1.5e) `tick; git commit -q --no-verify -am repin`(Cr).
   - K의 확인 행은 Rk의 고정의 PINS 그대로이므로 Cr의 고정(`pins(Cr, K)`)과 맞지 않는다. 그래서 K의 판은 사고 판이다. 미확인이 아니다.
   - post-commit이 `warning: '<K>' at HEAD was changed outside ait's rules`와 `hint: restore '.ait/<K>' as in '<Rk의 커밋>', then 'ait contract commit -m <why>'`를 쓴다(7.1.8). `ait peek now <K>`는 provider `src/order.c:1-5`(`?` 없음)를 보여 주고 `unverified` 행은 없으며, stderr에 같은 경고와 hint를 쓴다.
   - `ait peek <K>`의 이력 마지막 행은 `event - ? changed`다. 다시 고정만 했으므로 `amended`가 아니고, 확인 행이 바뀌지 않았으므로 `verified`도 아니다.
   - `tick; ait amend K -r pass` → `err E_ACCIDENT`, `불변`, 같은 hint. hint대로 `git --literal-pathspecs restore --source=<Rk의 커밋> --staged --worktree -- .ait/<K>; tick; ait contract commit -m 복구` → `changed <K>`. 이제 K의 확인 행은 맞고 order.c 3행이 고정과 다르므로 `ait peek now <K>`에 `unverified yes`가 있다(1.5a).
   - `tick; ait amend K -r pass`(R) → `check <K> reported`. `rec(K)`의 확인 행은 `check reported <pins(HEAD, K)>`이고, `ait peek now <K>`에 `unverified` 행이 없다. 마지막 event는 `event <R> agent-a verified check reported`다.
9. **형식이 틀린 레코드**(5.10): 준비를 다시 한 뒤 `.ait/<O>`의 마지막 LF를 지우고(2.2.1) `tick; git commit -q --no-verify -am bad`(Cb). G는 `Cb^`의 OID다(바른 판이 있는 가장 최근 커밋).
   - `ait peek` → `ok`, O 블록은 없다. stderr에 `warning: skipping malformed '.ait/<O>' at '`로 시작하는 행과 `hint: restore '.ait/<O>' as in '<G>', then 'ait contract commit -m <why>'`가 있다(얽힌 레코드 중 GOOD과 다른 것은 O뿐이다, 5.10).
   - O와 무관한 `ait future offer src/order.c p` → `err E_FORMAT`, `불변`, 같은 hint(관문은 형식이 틀린 레코드를 만나면 거절한다, 5.10).
   - stock.c를 고쳐 stage하고 `ait fulfill O` → `err E_FORMAT`, `불변`. hint는 복원 명령, `unstage 'src/stock.c' first`, `ait contract commit -m <why>`, `stage 'src/stock.c' again`, `then rerun this command` 차례다(contract commit은 코드를 받지 않고, 다시 할 이행은 그 코드를 확인한다, 5.9a의 로컬 변경). 끝나면 `git reset -q --hard`(HEAD는 Cb 그대로다).
   - `git switch -q -c ok <Cb^>`(바른 판)에서 `git push origin master:refs/heads/x` → pre-push가 master의 OID(Cb)를 검사해 거절한다. hint는 복원 명령이 아니라 `'refs/heads/master' (<Cb>) cannot be fixed here; where that commit is HEAD on a branch, run 'ait contract commit -m <why>' to get the fix`다(push하는 이름이 지금 브랜치 ok가 아니다, 7.1.6). `git switch -q master; ait contract commit -m x` → `err E_FORMAT`과 위의 복원 hint다. 끝나면 `git switch -q master; git branch -qD ok`.
   - master에서 `git tag bad; git push origin bad` → pre-push가 Cb를 검사해 거절하고, hint는 HEAD를 고치는 복원 명령이 아니라 `'refs/tags/bad' (<Cb>) cannot be fixed here; …`다(Cb가 HEAD여도 여기서 고친 커밋으로 tag는 움직이지 않는다, 7.1.6). 끝나면 `git tag -d bad`.
   - `git switch -q --detach master; git push origin HEAD:refs/heads/y` → pre-push가 받은 이름은 `HEAD`이지만 HEAD가 떼어져 있어 여기서는 ait가 기록할 수 없으므로 hint는 `'HEAD' (<Cb>) cannot be fixed here; …`다(7.1.6). 끝나면 `git switch -q master`.
   - hint대로 `git --literal-pathspecs restore --source=<G> --staged --worktree -- .ait/<O>; tick; ait contract commit -m 복구` → `changed <O>`. `rec(O)`의 prev는 `blob(<G>:.ait/<O>)`다(틀린 판은 계약의 판이 아니므로 마지막 바른 판을 잇는다, 5.10).
10. **우회로 어긋난 상태는 마지막 바른 판으로 복원한다**(5.10, 5.9 d, 5.9a의 전이 hint 1): 준비를 다시 한 뒤 `tick; ait offer src/order.c p as k -r pass`, `tick; ait future drop K as x`(Rx). `.ait/<X>`의 state를 손으로 `fulfilled`로 바꾸고 `tick; git commit -q --no-verify -am forge`(K는 current 그대로다).
   - **HEAD의 오류를 지닌 채 합칠 수 있다**(7.1.3-7, 7.1.4): `git switch -q -c side <Rx^>; echo n > side.txt; git add side.txt; tick; git commit -qm side; git switch -q master; tick; git merge -q --no-edit side`(M) → 성공. stderr에 `warning: '.ait/<X>' keeps an error from HEAD`와 `hint: run 'ait contract commit -m <why>' to get the fix`가 있다(오류가 가리키는 X가 HEAD에서도 구조 오류를 가지므로 HEAD에 이미 있던 오류다, 7.1.3-7. pre-merge-commit이 쓰고, commit-msg는 읽기만 한다).
   - `ait future offer src/stock.c p` → `err E_STATE`, `불변`(validate d: fulfilled X의 target이 withdrawn이 아니다). hint는 `restore '.ait/<X>' as in '<Rx의 커밋>', then 'ait contract commit -m <why>'`와 `then rerun this command`다. 이 거절은 후보에 남은 HEAD의 오류 때문이다. M에서 첫 부모를 따라(M, forge 커밋, Rx) 처음으로 X와 K가 함께 5.9 d를 지나는 커밋이 Rx이고, K의 판은 그것과 같으므로 넣지 않는다(5.10).
   - 그대로 하면 `tick; ait contract commit -m 복구` → `changed <X>`(복원한 후보에는 그 오류가 없으므로 관문을 지난다, 5.10). `rec(X)`는 `state future`이고 prev는 `blob(<Rx의 커밋>:.ait/<X>)`다(위조한 판은 계약의 판이 아니므로 잇지 않는다, 5.10). 이어서 같은 offer → `ok`.

## T14 peek (7.9)

준비:

```sh
tick; ait offer src/stock.c:10-20 p as o1 -m s1 -r pass
tick; ait future drop O1 as x future offer src/stock.c:10-20 tests/guard.sh as o2 -m s2
tick; ait demand src/order.c src/stock.c:15-15 p as d3 -m s3 -r pass
export AIT_SESSION=-
```

1. `ait peek now src/stock.c:20-20`의 출력이 정확히 다음과 같다.
   ```
   view now
   session -

   id <O1>
   role offer
   state current
   provider src/stock.c:10-20
   creator agent-a <A>
   drop <X>
   match src/stock.c:20-20 provider
   file specs/p.yaml prompt
       재고가 0이면 예약 요청을 거절한다.

   ```

2. 아래 명령마다 보이는 레코드(ID 순):

| 명령 | 보이는 레코드 |
| --- | --- |
| `ait peek src/stock.c:20-20` | O2 |
| `ait peek after src/stock.c:15-15` | D3, O2 |
| `ait peek src/order.c` | D3 |
| `ait peek specs/p.yaml` | D3 |
| `ait peek now specs/p.yaml` | D3, O1 |
| `ait peek src/` | D3, O2 |
| `ait peek /` | D3, O2 |
| `ait peek now` | D3, O1 |
| `ait peek X` | X(event 행 포함) |
| `ait peek src/stock.c:20-25` | O2 전체 |

3. `ait peek D000000000000` → `err E_ID`.
4. **peek은 읽기만 한다**: `AIT_SESSION=$A ait peek src/stock.c:20-20 <D3> src/ /` → `ok`, `ses(A)`에 `sub` 행이 생기지 않는다(7.9.7).
5. **구독은 `ait sub`로 더한다**(7.2.4): `AIT_SESSION=$A ait sub src/stock.c:20-20 <D3> src/ /` → stdout은 다음 네 행뿐이다(행 사이의 순서는 맞추지 않는다).
   ```
   sub scope src/stock.c:20-20
   sub id <D3>
   sub scope src/
   sub scope /
   ```
   - `ses(A)`에 `sub scope src/stock.c:20-20 <blob(HEAD:src/stock.c)>`, `sub id <D3>`, `sub scope src/`, `sub scope /`가 있다. `ait session list`의 A 블록은 `subs 4`와 그 `sub …` 행 넷을 `listening` 행 앞에 보인다(7.2.2).
   - 같은 명령을 다시 → `ok`, stdout이 비어 있다(이미 있는 구독은 더하지 않는다). `ses(A)`는 그대로다.
   - `AIT_SESSION=$A ait sub` → `err E_USAGE`. `AIT_SESSION=$A ait sub D000000000000` → `err E_ID`(HEAD에도 ref 목록에도 없다). session 없이 `ait sub src/` → `err E_SESSION`.
   - `AIT_SESSION=$A ait unsub src/stock.c:20-20 <D3>` → stdout은 `unsub scope src/stock.c:20-20`과 `unsub id <D3>` 두 행이다(범위 ARG는 HEAD에서의 표시로, ID ARG는 `sub id`와 맞춘다). `sub scope src/`와 `sub scope /`는 남는다. 인자 없이 `AIT_SESSION=$A ait unsub` → `unsub scope src/`, `unsub scope /`, `ses(A)`에 `sub` 행이 없다.

## T15 이력: 누가, 어떤 방향으로, 어떻게 (7.9.5)

준비: 같은 worktree에서 A(agent-a)와 B(agent-b) session을 연다. `AIT_SESSION`을 바꿔 가며 실행한다.

1. A: `tick; ait future demand src/order.c src/stock.c tests/guard.sh as d`(R1)
2. B:
   - `tick; ait future offer src/stock.c tests/pass.sh as o undertake D o`(R2)
   - `tick; ait future amend O src/stock.c:1-10 -`(R3)
3. B: stock.c 1행을 `s1 guard`로 바꾸고 stage → `tick; ait fulfill O -r pass`(R4). 이 이행으로 D도 함께 이행된다(T19).
4. `ait peek <D>`의 event 행이 정확히 다음과 같다.
   ```
   event <R1> agent-a created-future
   event <R2> agent-b undertaken <O>
   event <R4> agent-b fulfilled via <O> check reported
   ```
5. `ait peek <O>`의 event 행이 정확히 다음과 같다.
   ```
   event <R2> agent-b created-future
   event <R2> agent-b undertaken <D>
   event <R3> agent-b amended
   event <R4> agent-b fulfilled check reported
   ```
6. 사본은 한 번만 나온다.
   - R1 커밋에서 브랜치 c를 만들고 c에서 `tick; git cherry-pick <R2 커밋>`.
   - master에서 `tick; git merge --no-edit c` → 충돌 없이 기록줄 없는 병합 커밋이 생긴다(driver가 O의 나중 판을 고른다).
   - `ait peek <D>`와 `ait peek <O>`의 event 행이 4·5와 같다. 사본은 RID와 레코드 blob이 원본과 같으므로 한 번만 쓰고, 병합 커밋의 `.ait/<O>`는 한 부모의 판과 같으므로 쓰지 않는다(7.9.5).

## T16 행 사상과 미확인 표시 (1.5a, 3.7–3.9, 3.11)

준비: `tick; ait offer src/stock.c:20-30 p as o -r pass`. 아래 변경은 각각 일반 커밋으로 만든다.

| 단계 | 커밋 | `ait peek now O`의 provider |
| --- | --- | --- |
| 1 | 맨 앞에 `n1`..`n5` 삽입 | `src/stock.c:25-35` |
| 2 | 원래 `s22`..`s24` 삭제 | `src/stock.c:25-32 ?changed` |
| 3 | 남은 `s20`·`s21`·`s25`..`s30` 삭제 | `src/stock.c:20-30 ?deleted` |
| 4 | `git rm src/stock.c` | `src/stock.c:20-30 ?missing` |

1. 단계 1 뒤:
   - 행이 밀렸을 뿐 범위의 코드는 그대로이므로 `?`가 없다(미확인이 아니다).
   - `peek now src/stock.c:35-35` → O가 보인다.
   - `:36-36`, `:24-24` → 0건.
2. 단계 2부터 O는 미확인이다. 바뀐 것이든 사라진 것이든 같다. 막히는 것은 없다: 단계 4 뒤에도 `tick; ait future offer src/order.c p` → `ok`.
3. 단계 3 뒤: `peek now src/stock.c:1-1` → O가 보인다(사상하지 못한 범위는 보수적으로 파일 전체와 겹친다).
4. `tick; ait future offer new:src/v2.c p as v` 후 `src/v2.c`를 만들어 커밋 → V의 provider가 `new:src/v2.c ?exists`.
5. 준비를 다시 하고 `git config diff.algorithm histogram; git config diff.external false; git config diff.interHunkContext 10`을 둔 뒤 단계 1~3을 반복해도 표와 1~3의 결과가 같다.
6. mode만 바꾼 커밋(`chmod +x src/stock.c`)은 미확인을 만들지 않는다(11.7). 준비를 다시 하고 그 커밋을 하면 `ait peek now O`의 provider에 `?`가 없고, pre-commit 경고도 없다.

## T17 fulfill 기본 (7.7)

준비: `tick; ait future offer src/stock.c:1-10 tests/guard.sh as o`.

1. stage 없이 `ait fulfill O` → `err E_NEEDS_RESULTS`, `불변`. 이행에 필요한 것은 확인 결과뿐이다(규칙 4). stdout 보고는 정확히 다음과 같다(`task` 앞의 빈 행은 7.7.6의 틀에 있는 빈 행이다).
   ```
   id <O>
   provider <O> src/stock.c:1-10

   task <O>:specs/guard.yaml exec
   check-request <Q>
   check-result <W>
   ready yes
   ```
   - Q의 `tree`는 `git write-tree`의 값이다(stage된 상태). 과제에 `body: guard`와 `command: [./tests/guard.sh]`가 있다.
   - 같은 명령의 `-r fail` → `err E_CHECK_FAILED`(stock.c에 guard가 없다).
2. 5행을 `s5 guard`로 바꾸고 `tick; git commit -qam impl`. 이어서 stage 없이 `tick; ait fulfill O -r pass` → `ok`.
   - stdout: `request <R>`, `fulfilled <O>`, `check <O> reported`.
   - 커밋의 name-status는 `M .ait/<O>` 하나뿐이다.
   - 메시지는 `[FULFILL] m`, 빈 행, 결과, 빈 행, 기록줄 순서다. `rec(O)`의 확인 행은 `check reported <pins(HEAD, O)>`다. PINS는 다시 고정한 뒤의 고정(아래 provider 행과 `file` 행)의 해시다(7.7.4).
   - `rec(O)`의 provider는 `src/stock.c:1-10 <blob(HEAD:src/stock.c)>`다. 확인한 tree로 다시 고정된다(7.7.4). `ait peek now O`에 `unverified` 행이 없다.
3. `tick; ait future offer src/order.c tests/pass.sh as o2`. order.c와 `tests/pass.sh`를 고쳐 stage → `ait fulfill O2` → `err E_MIXED`, 보고에 `mixed tests/pass.sh`와 `ready no`.
4. `git reset -q --hard` 후:
   - `ait fulfill O2 O2` → `err E_USAGE`. alias나 메시지가 없을 때도 `err E_USAGE`.
   - 없는 ID → `err E_ID`.
   - 이미 current인 O → `err E_STATE`. hint는 `'<O>' is already current; to check it again, run 'ait amend <O> -m <why>' to get the tasks`다(5.9a의 전이 hint).
   - `tick; ait fulfill O2 --force` → `ok`, `check <O2> forced`. `rec(O2)`의 확인 행은 `check forced <pins(HEAD, O2)>`이고 결과 문단은 없다. 결과 대신 판단으로 넘긴 것이 기록된다(8.5).
5. **확인할 코드가 없으면 이행되지 않는다**: `tick; ait future offer new:src/g.c p as o3`. `ait fulfill O3` → `err E_CODE_REQUIRED`(`E_FORMAT`이 아니다. 이행 후 상태라면 current의 provider가 `new:`여서 validate a에 걸리지만, 확인할 코드를 먼저 본다, 5.13-7, 7.7.3-3·4), `불변`. 보고에 `code-required <O3>`와 `ready no`가 있고 과제 행(`task`·`check-request`·`check-result`)은 없다(확인할 수 없다, 7.7.6). hint에 아직 쓰지 않은 `new:` 형 `write 'src/g.c'`와 `stage 'src/g.c'`(stage 규칙), 그리고 `rerun with '<O3>=<new scope>'`가 있고, O3는 내가 만든 future이므로 `ait future cancel <O3> -m <why>`가 있다. `ait future amend`와 `ait future drop`은 없다(되돌리기 안내는 current 계약에만, 철회는 current에만 쓴다, 5.8). `--force`로도 같다(5.14).
6. 결과는 보고다. ait는 진위를 보장하지 않고 드러낸다(0.4a): `tick; ait future offer src/stock.c tests/guard.sh as o4`. stock.c 5행의 `guard`를 지워 stage(이제 guard가 없다) → `tick; ait fulfill O4 -r pass` → `ok`. `git log -1 --format=%B`에 그 결과(과제·outcome·basis)가 있다.
7. **코드가 없는 약속의 todo**(H10): 준비를 다시 한 뒤 `tick; git rm -q src/stock.c; git commit -qm rm`(O는 future이므로 경고가 없다). `ait todo` → O 블록의 provider는 `src/stock.c:1-10 ?missing`이다. stderr의 hint(H10)는 `'<O>' has no code under its scope; if it was moved, run 'ait fulfill <O>=<new scope> -m <why>' to get the tasks`와, O가 A의 계약이므로 이어지는 `if it is gone for good, run 'ait future cancel <O> -m <why>'`다. `when ready, stage the code`로 시작하는 형은 없다(provider를 HEAD에서 찾을 수 없다, 5.9a).
   - 준비를 다시 하고 provider를 `src/stock.c:1-10,src/order.c:1-5`로 둔 future offer O로 같은 일을 하면, H10의 옮김 형은 `ait fulfill <O>=src/order.c:1-5,<new scope> -m <why>`다(KEPT는 그 명령이 실행될 tree에서 찾을 수 있는 조각 `src/order.c:1-5`다. stage된 것이 없어 T0가 HEAD와 같다, 5.9a).

## T18 비대칭 ①: demand는 먼저 이행될 수 있다 (1.6, 7.7)

준비(T18–T20 공통): 같은 worktree에서 A(agent-a)와 B(agent-b) session을 열고, `AIT_SESSION`을 바꿔 가며 실행한다(T15와 같다). "A가", "B가"는 그 session으로 실행한다는 뜻이다. stage와 작업 폴더는 둘이 함께 쓴다.
- A가 `tick; ait future demand src/order.c src/stock.c tests/lowest.sh as d -m "최저가를 내놓아라"`.
- B가 `tick; ait future offer src/stock.c tests/highest.sh as o undertake D o -m "최고가와 최저가를 내놓겠다"`.

1. stock.c 1행을 `s1 lowest`로 바꿔 stage한다.
   - A가 `ait fulfill D` → `err E_NEEDS_RESULTS`. 과제는 `<D>:specs/lowest.yaml` 하나다. D 자신의 명세 파일만 본다.
   - `tick; ait fulfill D -r pass`(R1) → `ok`. stdout에 `fulfilled <D>`가 있고 `via`는 없다. `rec(O)`는 future 그대로다.
2. B가 stage 없이 `ait fulfill O` → `err E_NEEDS_RESULTS`. 과제는 `<D>:specs/lowest.yaml`과 `<O>:specs/highest.yaml`이다. D는 이미 current이지만 O가 떠맡았으므로 다시 확인한다. 되돌리기 hint는 없다(D를 되돌려도 O의 이행이 D를 함께 이행하며 다시 확인한다, 규칙 5, 5.9a).
   - `fill <W> pass <O>:specs/highest.yaml=fail` 후 `-r <W>` → `err E_CHECK_FAILED`. 최고가는 아직 없다.
3. stock.c 2행을 `s2 highest`로 바꿔 stage. B가 `tick; ait fulfill O -r pass`(R3) → `ok`.
   - stdout에 `fulfilled <O>`, `check <D> reported`, `check <O> reported`가 있다.
   - D는 이미 current이므로 `fulfilled <D>` 행은 없다. D는 확인되었으므로 범위가 다시 고정된다.
4. 이미 이행된 D가 깨졌다고 보고하면 O도 이행할 수 없다: 2의 상태에서 `fill <W> pass <D>:specs/lowest.yaml=fail` 후 `-r <W>` → `err E_CHECK_FAILED`. O는 B가 만든 offer이므로 놓기 `to fulfill '<O>' without '<D>', release it first: 'AIT_SESSION=<B> ait release <D> <O> -m <why>', then rerun this command`도 있다(5.9a. 같은 worktree의 기본 session은 A이므로 접두어가 붙는다). hint에 `ait future amend <D> -m <why>`는 없고, 조건을 바꾸는 `to change its condition, reopen it with the new one: 'AIT_SESSION=<B> ait future amend <D> - - <new defs> -m <why>', then rerun this command`(demand의 형. B는 기본 session이 아니다)가 있다(5.8).
5. `ait peek <D>`의 event 행은 `… created-future`, `… undertaken <O>`, `event <R1> agent-a fulfilled check reported`, `event <R3> agent-b verified check reported`다. 1의 이행에 `via`가 없다.

## T19 비대칭 ②: offer는 떠맡은 demand 없이 이행될 수 없다 (1.6, 7.7.2)

준비(A와 B는 T18처럼 같은 worktree의 두 session이다):
- A가 `tick; ait future demand src/order.c src/stock.c tests/guard.sh as d`. D의 조건은 stock.c에 `guard`가 있는 것이다.
- B가 `tick; ait future offer src/stock.c:1-10 tests/pass.sh as o undertake D o`.

1. stock.c 1행을 `s1 x`로 바꿔 stage → B가 `ait fulfill O` → `err E_NEEDS_RESULTS`.
   - 과제는 `<D>:specs/guard.yaml`과 `<O>:specs/pass.yaml`이다. 보고에 `provider <D> src/stock.c`와 `provider <O> src/stock.c:1-10`이 있다.
   - `fill <W> pass <D>:specs/guard.yaml=fail` 후 `-r <W>` → `err E_CHECK_FAILED`. **O 자신의 과제가 pass여도, 떠맡은 요구를 충족하지 못하면 이행할 수 없다.**
2. 1행을 `s1 guard`로 바꿔 stage → `tick; ait fulfill O -r pass` → `ok`.
   - stdout에 `fulfilled <O>`, `fulfilled <D> via <O>`, `check <D> reported`, `check <O> reported` 행이 있다.
   - `rec(D)`의 state가 current가 된다(고아가 되지 않는다).
3. 함께 떠맡고 각자 일부만 맡은 경우(새 저장소):
   - `tick; ait future demand src/order.c src/stock.c tests/both.sh as d2 future offer src/stock.c:1-10 tests/pass.sh as o1 future offer src/stock.c:40-50 tests/pass.sh as o2 undertake d2 o1 undertake d2 o2`
   - stock.c 1행을 `s1 lowest`, 45행을 `s45 highest`로 바꿔 stage. `ait fulfill O1 O2` → `err E_NEEDS_RESULTS`. 보고가 `ready yes`다. 과제는 `<D2>:specs/both.yaml`(한 번), `<O1>:specs/pass.yaml`, `<O2>:specs/pass.yaml`이다.
   - `tick; ait fulfill O1 O2 -r pass` → `fulfilled <O1>`, `fulfilled <O2>`, `fulfilled <D2> via <O1> <O2>` 행이 있다.
4. A의 listener가 돌고 있으면, 2의 이행 사건이 A에게 간다. A와 B는 같은 worktree에 있으므로 A의 HEAD에서 D는 이미 current다(`reason demand <D>`, 9.3).

## T20 todo와 이행 준비 보고: 내 할 일과 그 진척 (7.7.6, 7.8)

준비(A와 B는 T18처럼 같은 worktree의 두 session이다):
- A(agent-a)가 `specs/low.yaml`와 `tests/guard.sh`를 만들고 `tick; ait future demand src/order.c src/stock.c specs/low.yaml,tests/guard.sh as d`.
- B(agent-b)가 `tick; ait future offer src/stock.c:1-10 tests/pass.sh as o undertake D o`.

1. B가 `ait todo` → `ok`. stdout은 정확히 O 블록 하나다.
   ```
   id <O>
   role offer
   state future
   creator agent-b
   provider src/stock.c:1-10
   file specs/pass.yaml exec
       pass
       $ ./tests/pass.sh
   file tests/pass.sh input
   undertakes <D> future agent-a
   undertakes-provider <D> src/stock.c
   undertakes-file <D> specs/guard.yaml exec
       guard
       $ ./tests/guard.sh
   undertakes-file <D> specs/low.yaml prompt
       최저가가 보장될 것.
   undertakes-file <D> tests/guard.sh input

   ```
2. A가 `ait todo` → D 블록이 나온다. 블록에 `undertaken-by <O> future agent-b`, `file specs/low.yaml prompt`(body 포함), `file specs/guard.yaml exec`(body와 `$ ./tests/guard.sh` 포함), `file tests/guard.sh input`이 있다. D는 O와 상관없이 먼저 이행될 수 있으므로 기다릴 것이 없다.
3. B가 `ait fulfill O` → `err E_NEEDS_RESULTS`. stdout 보고:
   ```
   id <O>
   provider <D> src/stock.c
   provider <O> src/stock.c:1-10

   task <D>:specs/guard.yaml exec
   task <D>:specs/low.yaml prompt
   task <O>:specs/pass.yaml exec
   check-request <Q>
   check-result <W>
   ready yes
   ```
4. stock.c 1행을 `s1 guard`로 바꿔 stage한 뒤 B가 `ait fulfill O` → `err E_NEEDS_RESULTS`. 보고의 `provider` 행과 `task` 행은 3과 같고(D·O는 future이므로 provider 표시에 `?changed`가 붙지 않는다, 3.8) `ready yes`다. 결과 틀의 `input`은 3과 다르다(stage된 변경이 확인 입력에 들어간다, 8.3). 그래서 3의 결과 틀로는 이행할 수 없다(`E_NEEDS_RESULTS`).
5. 명세 파일 변경이 stage되어 있으면 이행할 수 없다: `tests/pass.sh` 끝에 `# x`를 더해 `git add`한 뒤 `ait fulfill O` → `err E_MIXED`, 보고에 `mixed tests/pass.sh`, `ready no`. 끝나면 되돌린다.
6. `ait todo src/order.c`(범위) → D 블록을 보인다.
7. session 없이(`AIT_SESSION=- ait todo`) → 모든 future(D, O)를 보인다.
8. `todo`와 `-r` 없는 `fulfill`은 커밋하지 않는다(`불변`). 아무것도 실행하지 않는다: 작업 폴더의 `tests/guard.sh` 내용을 `#!/bin/sh` / `touch $T/ran; exit 1`로 바꿔 두고(stage하지 않음) `ait todo`, `ait fulfill O` → `$T/ran`이 생기지 않는다. 끝나면 되돌린다.
9. 4의 상태에서 `tick; ait fulfill O -r pass` → `ok`, `fulfilled <D> via <O>`, `check <D> reported`, `check <O> reported`. 커밋 메시지의 결과에 과제 셋이 있다.
10. 미확인은 범위의 todo에 나온다: 9 뒤에 stock.c 3행을 바꾸는 일반 커밋을 한다. `ait todo src/stock.c` → O 블록(`state current`, `provider src/stock.c:1-10 ?changed`, `unverified yes`)과 D 블록(`provider src/stock.c ?changed`, `unverified yes`). 인자 없는 `ait todo`에서 A에게는 D가 나오고(내 요구에 기대는 코드가 있다), B에게는 O가 나오지 않는다(current offer의 미확인은 만든 쪽의 할 일이 아니다, 7.8.1).

## T21 prompt 정의와 확인 결과 (1.2, 7.7, 8)

준비: `tick; ait future demand src/order.c src/stock.c specs/low.yaml as d`. stock.c를 바꿔 stage한다.

1. `ait todo` → D 블록에 `file specs/low.yaml prompt`와 `    최저가가 보장될 것.`이 있다. 구현 목표인 조건이다.
2. `ait fulfill D` → `err E_NEEDS_RESULTS`. stdout에 `task <D>:specs/low.yaml prompt`, `check-request <Q>`, `check-result <W>`, `ready yes`.
   - Q에는 `input`, `tree`, 과제 `<D>:specs/low.yaml`, `engine: prompt`, body가 있다. `command`는 없다.
3. 다시 `ait fulfill D` → Q·W의 경로와 `input` 값이 2와 같다(확인 입력이 같다).
4. `fill <W> pass` 후 `tick; ait fulfill D -r <W>` → `ok`, `check <D> reported`. 커밋 메시지에 결과(과제, outcome, basis)가 있다.
5. 각 항목마다 준비를 다시 하고 2를 다시 해 W를 받은 뒤 확인한다.
   - `fill <W> fail` 후 `-r <W>` → `err E_CHECK_FAILED`.
   - basis가 빈 pass → `err E_FORMAT`.
   - 결과를 채운 뒤 stock.c를 더 바꿔 stage하고 같은 결과로 → `err E_NEEDS_RESULTS`(확인 입력이 바뀌었다). 새 Q·W가 쓰인다.
   - `ait fulfill D -r $T/none.yaml`(없는 파일) → 종료 코드 128, `fatal: could not read`.
   - `fill <W> fail` 후 `-r <W>`로 거절된 뒤에도 W의 내용(outcome·basis)은 그대로다. 거절이 결과 틀을 다시 쓰지 않는다(8.4).
   - W를 채운 뒤 `-r` 없이 같은 명령을 다시 실행해도 W는 그대로다(input이 같다).
6. 커밋 hook은 결과를 요구하지 않는다. stock.c를 바꾸는 일반 커밋 → 성공, 경고도 없다.

## T22 걸린 계약: 내 커밋이 바꾸는 current 계약을 함께 확인한다 (1.5b, 7.1.2, 7.7.3-4·5, 7.7.4, 7.7.6)

준비:

```sh
tick; ait offer src/stock.c:1-10 tests/nobroken.sh as k future offer src/stock.c tests/pass.sh as o -r pass
```

1. stock.c 40행만 바꿔 stage → `ait fulfill O` → `err E_NEEDS_RESULTS`. 과제는 `<O>:specs/pass.yaml` 하나이고 `touch` 행이 없다. 변경이 K의 범위 1-10에 닿지 않는다.
2. 그 대신 5행을 `BROKEN`으로 바꿔 stage → `ait fulfill O` → 보고에 `touch <K>`가 있고 과제에 `<K>:specs/nobroken.yaml`이 더해진다. `ait todo O`의 끝에 `index` 블록과 `touch <K>`가 있다. `fill <W> pass <K>:specs/nobroken.yaml=fail` 후 `-r <W>` → `err E_CHECK_FAILED`. hint에 T0의 형 `make the code meet the failed task(s) and stage it, then rerun without -r to get the new tasks`가 있고, K가 HEAD에서 current이므로(걸린 계약) 되돌리기 hint `to change '<K>' without a check, reopen it: 'ait future amend <K> -m <why>', then rerun this command`와 조건을 바꾸는 `ait future amend <K> - <new defs> -m <why>`(offer의 형, 10.5)가 있다(이행이 거절했으므로 도우미를 먼저 실행하고 다시 실행한다, 5.8, 5.9a).
   - `fill <W> pass <K>:specs/nobroken.yaml=unknown` 후 `-r <W>` → `err E_CHECK_FAILED`. hint는 `check it where its environment is available (see '<Q>') and rerun with -r '<W>'`, `if it is trivial, add --force (recorded as forced)`, 그리고 K가 HEAD에서 current이므로 `ait future amend <K> -m <why>`와 `ait future amend <K> - <new defs> -m <why>`다(5.8. 되돌리기 hint는 fail·unknown 모두 같다). `make the code meet …`는 없다.
3. 5행을 되돌리고 10행 뒤에 `BROKEN`을 삽입해 stage → `touch <K>`(접촉 구간 [10,11]).
   - **행 범위로 본 peek은 경계의 걸림을 놓칠 수 있다**: 이때 `ait peek now src/stock.c:11-11`(삽입한 자리)은 K를 보이지 않는다(HEAD에서 K의 1-10과 겹치지 않는다, 3.9). `ait peek now src/stock.c`(파일)는 K를 보인다. 그래서 바꿀 코드에 걸린 current 계약은 파일로 본다(SKILL).
4. 일반 커밋은 막지 않는다: 3의 변경으로 `tick; git commit -qm wip` → 성공. stderr에 `warning: this commit leaves 1 current contract(s) unverified`와 `    <K> offer src/stock.c:1-10 (agent-a)` 행이 있다.
   - K는 미확인이 된다: `ait peek now <K>` → `provider src/stock.c:1-10 ?changed`. `ait todo src/stock.c`에 K 블록이 나온다.
   - 이어서 stage 없이 `ait fulfill O` → `touch` 행이 없고 과제는 O의 것뿐이다. 앞선 커밋이 바꾼 코드는 다시 묻지 않는다(1.5b). K는 `?changed`로 남는다.
5. 복원하면 미확인이 풀린다: 11행의 `BROKEN`을 지우고 40행을 바꿔 stage → `touch` 행이 없다(지운 11행은 K의 범위 밖이다). `tick; ait fulfill O -r pass` → `ok`. 이제 K의 범위는 고정 때와 같으므로 `ait peek now <K>`에 `?`가 없다.
6. 확인하면 다시 고정된다: 준비를 다시 한 뒤 5행을 `s5 y`로 바꿔 stage → `tick; ait fulfill O -r pass`(R) → `ok`, `check <K> reported`, `check <O> reported`.
   - `rec(K)`의 provider는 `src/stock.c:1-10 <blob(HEAD:src/stock.c)>`다. `ait peek now <K>`의 provider에 `?`가 없다.
   - `ait peek <K>`의 마지막 event는 `event <R> agent-a verified check reported`다.
7. 이미 범위를 잃은 계약은 걸리지 않는다: 준비를 다시 한 뒤 stock.c 1~10행을 지우는 일반 커밋을 한다(K는 `?deleted`, 이때 경고에 ` (no code under its scope)`가 붙는다). stock.c 마지막 행을 바꿔 stage하고 `ait fulfill O` → `err E_NEEDS_RESULTS`. 보고에 `touch <K>`도 `code-required <K>`도 `<K>:` 과제도 없다(HEAD에서 이미 provider를 찾을 수 없으므로 걸리지 않는다, 1.5b). K는 이미 미확인으로 드러났다. 그 뒤 stock.c를 바꾸는 `git commit`의 경고에도 K가 없다.
8. **이 커밋에서 범위를 잃는 계약이 있으면 커밋하지 않는다**(1.5b, 7.7.3-4): 준비를 다시 한 뒤 stock.c 1~10행을 지워 stage하고 `ait fulfill O` → `err E_CODE_REQUIRED`, `불변`. stderr 첫 행은 `error: no code under the contract's scope:`로 시작한다.
   - 보고에 `provider <K> src/stock.c:1-10 ?deleted`, `code-required <K>`, `touch <K>`, `ready no`가 있고 `unverified` 행은 없다(7.7.6). 과제 행(`task`·`check-request`·`check-result`)도 없고 결과 틀은 쓰이지 않는다(확인할 수 없다, 7.7.6, 8.4).
   - stderr의 hint(5.8 `E_CODE_REQUIRED`의 fulfill·commit 형)에 `rerun with '<K>=<new scope>'`(남은 조각이 없으므로 KEPT가 없다, 5.9a)와 `ait future amend <K> -m <why>`가 있다. 작업 폴더에도 그 코드가 없으므로 stage 명령은 없다. K는 내가 만든(agent-a) current 계약이므로 철회 hint `ait future drop <K> as x -m <why>`와 `then 'ait fulfill <O> <X> -m <why>'`도 있다(이 명령의 선택한 대상 O에 X를 더한 fulfill이다. stage된 삭제가 그 커밋에 함께 들고 K는 그 커밋 뒤 withdrawn이므로 확인하지 않는다. `with nothing staged`는 없다, 5.9a).
   - `ait todo`의 끝에 `index` 행, 이어서 `code-required <K>`, `touch <K>` 행이 이 순서로 있다(7.8.2).
   - `tick; ait fulfill O --force` → 같은 `err E_CODE_REQUIRED`, `불변`(`--force`는 확인 결과만 넘긴다, 5.14. 확인할 코드는 결과보다 먼저 본다, 5.13).
   - **새 위치를 주면 그 위치로 확인한다**: 준비를 다시 한 뒤 stock.c 1~10행을 지우고 같은 내용을 새 파일 `src/stock2.c`에 넣어 stage → `ait fulfill O` → 같은 `err E_CODE_REQUIRED`. `tick; ait fulfill O K=src/stock2.c:1-10 -r pass` → `ok`, stdout에 `fulfilled <O>`, `check <K> reported`, `check <O> reported`가 있다. `rec(K)`의 provider는 `src/stock2.c:1-10 <blob(HEAD:src/stock2.c)>`이고 `ait peek now <K>`에 `?`도 `unverified` 행도 없다.

## T23 철회의 이행 (1.4, 7.7)

준비:

```sh
tick; ait demand src/order.c src/stock.c p as d offer src/stock.c tests/pass.sh as o undertake d o -r pass
```

이어서 stock.c 50행을 바꾸는 일반 커밋을 하나 둔다(`tick; git commit -qam impl`). O와 D의 고정 이후 코드가 이미 바뀐 상태다(둘 다 미확인). 이 커밋의 stderr는 `warning: this commit leaves 2 current contract(s) unverified`로 시작하고 D와 O를 담는다. D의 되돌리기 hint는 `ait future amend <D> future amend <O> -m <why>`다(D는 current O가 떠맡은 current D다, 7.1.2, 5.9a). O의 것은 `ait future amend <O> -m <why>`다.

1. `tick; ait future drop O as x` → `rec(X)`는 `role drop` / `state future` / `target <O>` / `creator agent-a <A>` / `prev -`. D와 O의 연결은 그대로다.
2. stock.c를 바꿔 stage한다.
   - `ait fulfill X` → `err E_NEEDS_RESULTS`. 과제는 `<D>:specs/p.yaml` 하나다(D가 걸린 계약이다. provider `src/stock.c` 전체에 닿는다). 철회되는 O는 이행 후 withdrawn이므로 과제가 없다.
   - `tick; ait fulfill X -r pass` → `fulfilled <X>`, `withdrawn <O>`, `check <D> reported`. 떠맡긴 상태로도 철회된다.
3. `ait peek now <D>` → `undertake <O> withdrawn`. 연결은 남고 다른 끝의 state가 함께 보인다. D는 다시 확인되어 `unverified` 행이 없다.
4. **확인할 것이 없으면 결과도 필요 없다**: 준비를 다시 하고 1까지 한다. stage 없이 `tick; ait fulfill X` → `fulfilled <X>`, `withdrawn <O>`(걸린 계약이 없어 과제가 없다). D는 미확인으로 남는다.
5. **철회되는 D는 떠맡은 O의 이행에서도 확인하지 않는다**(1.5b 확인 집합, 1.6): 준비 대신 `tick; ait demand src/order.c src/stock.c tests/guard.sh as d future offer src/stock.c p as o undertake d o -r pass`(future O가 current D를 떠맡는다. 일반 커밋은 두지 않는다), 이어서 `tick; ait future drop D as x`. stage 없이 `ait fulfill O X` → `err E_NEEDS_RESULTS`. 과제는 `<O>:specs/p.yaml` 하나이고 `<D>:specs/guard.yaml`은 없다(D는 이 커밋 뒤 withdrawn이므로 O가 떠맡았어도 확인 집합에 들지 않는다).
   - `tick; ait fulfill O X -r pass` → stdout에 `fulfilled <O>`, `fulfilled <X>`, `withdrawn <D>`, `check <O> reported`가 있고 `check <D>` 행과 `via`는 없다. `.ait/undertake/<D>-<O>`는 `state undertaken` 그대로다(O가 current이고 D가 withdrawn이면 5.9 d를 어기지 않는다).

## T24 `new:` 범위의 이행 (3.11, 7.7.4)

1. `tick; ait future offer new:src/guard.c tests/pass.sh as o`. 파일 없이 `ait fulfill O` → `err E_CODE_REQUIRED`.
2. 파일을 만들어 stage 후 `tick; ait fulfill O -r pass` → provider가 `src/guard.c <blob>`이 된다.
3. `tick; ait future demand new:src/c.c src/stock.c tests/pass.sh as d`. stock.c를 바꿔 stage하고 `tick; ait fulfill D -r pass` → consumer는 `new:src/c.c`로 남는다(T0에 c.c가 없다).

## T25 결과 파일 (8.4)

준비: `tick; ait future offer src/stock.c p,tests/pass.sh as o`. stock.c를 바꿔 stage하고 `ait fulfill O`로 W를 받는다. 표의 각 행은 이 준비 상태에서 따로 한다(첫 행이 성공하면 O가 current가 되기 때문이다). 과제는 `<O>:specs/p.yaml`, `<O>:specs/pass.yaml`이다.

| W를 이렇게 고쳐 `ait fulfill O -r <W>` | 기대 |
| --- | --- |
| 모두 pass | `ok` |
| 하나를 `unknown`(reason 없이) | `err E_CHECK_FAILED`. stderr에 그 과제와, hint `check it where its environment is available (see '<Q>') and rerun with -r '<W>'`·`if it is trivial, add --force (recorded as forced)`가 있다. HEAD에서 current인 계약이 확인 집합에 없으므로 되돌리기 hint는 없다(5.8) |
| `input` 값을 바꿈 | `err E_NEEDS_RESULTS` |
| `input` 값을 바꾸고 과제 하나를 지움 | `err E_NEEDS_RESULTS`(input을 먼저 본다) |
| `input` 행을 지움 | `err E_FORMAT` |
| 과제 순서를 바꿔 모두 pass | `ok`(이름으로 맞춘다) |
| `results`에 모르는 키 `note: x`를 더함 | `err E_FORMAT` |
| YAML 부분집합 밖(anchor `&a`) | `err E_FORMAT` |

`ok`가 아닌 경우는 모두 `불변`이다.

## T26 공개 경합·동기화 실패·부분 stage 보존 (5.5)

ait는 결과 파일을 확인 결과 단계에서 연다(8.4). 시험은 이름 있는 파이프로 그 사이에 끼어든다. 백그라운드 작업은 ait가 파이프를 열 때까지(H를 읽은 뒤) `exec 3>`에서 기다린다.

1. `tick; ait future offer src/stock.c p as o`. stock.c를 바꿔 stage하고 `ait fulfill O`로 W를 받아 `fill <W> pass`.
   - `mkfifo $T/res`. 백그라운드로 `(exec 3>$T/res; B=$(git commit-tree -p HEAD -m bump HEAD^{tree}); git update-ref refs/heads/master $B; cat <W> >&3) &`를 띄우고 `tick; ait fulfill O -r $T/res` → `err E_HEAD_MOVED`. HEAD 제목은 `bump`이고, stage는 그대로 남는다.
2. 준비를 다시 하고 1처럼 W를 받고 `mkfifo $T/res`를 다시 한다. 백그라운드 작업은 `(exec 3>$T/res; touch .git/index.lock; cat <W> >&3) &`로 한다. `tick; ait fulfill O -r $T/res` → `ok`, stderr에 경고와 복원 명령이 있다.
   - `rm .git/index.lock` 후 쓰기 → `err E_DIRTY`.
   - 안내된 복원 명령을 실행하면 다시 `ok`.
3. 준비를 다시 한 뒤 `tick; ait future offer src/stock.c:1-10 tests/pass.sh as o3`. stock.c 1행을 `s1 A`로 바꿔 stage하고, 다시 `s1 B`로 바꿔 unstaged로 둔다. `tick; ait fulfill O3 -r pass` → HEAD의 stock.c는 `s1 A`, 작업 폴더는 `s1 B` 그대로.
4. **그사이 브랜치를 바꾸면 동기화하지 않는다**: 준비를 다시 하고 1처럼 W와 FIFO를 마련한다. 백그라운드 작업은 `(exec 3>$T/res; git switch -q -c other; cat <W> >&3) &`. `tick; ait fulfill O -r $T/res` → `ok`. master에는 이행 커밋이 생기고, stderr에 `but the index and work tree were not updated`로 끝나는 경고가 있다. other의 index와 작업 폴더는 그대로다.

## T27 쓰기 전제조건 (5.2)

미리 `tick; ait future offer src/stock.c p as o`를 해 둔다.

1. `.ait`가 dirty일 때(수정, 추적하지 않는 파일, stage된 수정) `ait future cancel O` → `err E_DIRTY`. stderr에 남기는 명령과 버리는 명령이 있다.
2. 충돌 merge가 진행 중이면 → 다음 명령이 모두 `err E_BUSY`.
   - `future cancel`
   - `fulfill`
   - `contract commit`
   - `contract pull`
3. detached HEAD에서 쓰기, 또는 커밋 없는 저장소에서 session을 연 뒤 쓰기 → `err E_BUSY`. detached HEAD의 hint는 `run this command with HEAD on a branch`이고 브랜치를 바꾸는 git 명령은 없다(5.8).

## T28 통합: merge driver·해결·재방송·되돌리기 (7.1.3–7.1.5)

준비(master): `tick; ait future offer src/stock.c tests/long.sh as o future demand src/order.c src/stock.c tests/long.sh as d undertake d o -m base`(Hbase).

1. **체인 자동 선택**(merge driver가 나중 판을 고른다):
   - x1(Hbase^에서 분기, 다른 커밋 하나)에서 `tick; git cherry-pick Hbase`.
   - master에서 `tick; ait future amend O src/stock.c:1-5 -`.
   - x1에서 `tick; git merge --no-edit master` → 충돌 없음, `HEAD^2`가 있음, `.ait/<O>`는 master 판.
2. **동시 수정 해결**:
   - y1과 y2(둘 다 Hbase에서 분기)가 각자 `tests/long.sh` 2행을 다르게 고치고 `contract add tests/long.sh`·`contract commit`을 한다.
   - y1에서 `tick; git merge y2` → 충돌.
   - 2행을 합쳐 쓰고 `git checkout --ours .ait/<O> .ait/<D>; git add -A; tick; git commit --no-edit` → 성공.
   - 결과: 두 레코드가 병합된 파일로 다시 고정된다. prev는 두 행(OID 순)이다. 메시지에 새 `Ait-Request`가 있다.
2a. **물려받은 두 prev는 새 해결이 아니다**(7.1.5, 7.10.1-2): 2 뒤 y1에서 `git push -q origin y1`. y1에서 갈라진 브랜치 cc에 order.c를 바꾸는 일반 커밋을 두고, y1에서 `tick; git merge --no-ff --no-edit cc`(M) → 충돌 없이 끝난다.
   - M에는 기록줄이 없다: `git log -1 --format='%(trailers:key=Ait-Request,valueonly)'`가 비어 있다. index의 레코드는 모두 출발 판(y1의 판, prev 두 행) 그대로이고, 출발 판의 prev를 물려받은 것은 새 사실이 아니다.
   - y1에서 `ait contract push origin` → `err E_EMPTY`. 내보낼 커밋은 order.c 커밋과 중립 병합 M뿐이다.
3. **한쪽 판만 골라도 재방송**:
   - z1과 z2가 O의 provider를 서로 다르게 amend한다.
   - merge 충돌을 `--ours`로 해결하고 커밋한다.
   - 결과: prev 두 행, 새 RID.
   - `ait peek <O>`의 마지막 event는 `event <새 RID> agent-a resolved`다.
4. **merge는 결과를 요구하지 않는다**:
   - K를 만들기 전의 master에서 브랜치 bb를 만들고, bb에서 stock.c 3행을 `BROKEN`으로 바꿔 커밋한다(일반 커밋).
   - master에서 `tick; ait offer src/stock.c:1-10 tests/nobroken.sh as k -r pass`.
   - master에 다른 커밋을 하나 둔 뒤 `tick; git merge --no-edit bb` → 성공. `ait peek now <K>` → `provider src/stock.c:1-10 ?changed`(미확인으로 드러난다). 레코드는 바뀌지 않았으므로 K는 되돌려지지 않는다(7.1.3-3은 고정이 current인 출발 판 어느 것과도 다른 current만 되돌린다. commit-msg의 정리 확인도 바꿀 것이 없어 막지 않는다, 7.1.5).
   - commit-msg가 stderr에 `warning: '<K>' is unverified after this merge`와 hint `ait amend <K> -m <why>`(H7)를 쓴다(이 병합 커밋이 만든 새 미확인이다, 7.1.5).
5. **자동 병합된 명세 파일**:
   - w1이 `tests/long.sh` 2행, w2가 11행을 고쳐 각자 `contract add tests/long.sh`·`contract commit`.
   - merge → 레코드에서 driver 충돌이 난다.
   - `--ours`로 해결하고 `git commit` → pre-commit이 병합된 파일 기준으로 다시 고정한다.
6. **cherry-pick 충돌**:
   - c1(Hbase에서 분기)이 `tests/long.sh` 2행을 고쳐 `contract add`·`contract commit`(Rc1). master도 2행을 다르게 고쳐 `contract add`·`contract commit`.
   - master에서 `tick; git cherry-pick <Rc1 커밋>` → long.sh와 `.ait/<O>`·`.ait/<D>`가 충돌한다(동시 수정).
   - long.sh를 합쳐 쓰고 레코드는 `--ours`로 정리한 뒤 `git add -A; tick; git cherry-pick --continue` → 성공.
   - 메시지의 `Ait-Request`는 Rc1이 아닌 새 RID(Rn)다. 이후 `ait contract pull <O> <D>` → `already <O> <D>`(c1의 판이 HEAD 판의 prev에 있으므로 HEAD 판에 포함된다, 7.12).
7. **cherry-pick이 건드리지 않은 레코드는 그대로**: master와 c2가 서로 다른 레코드 E를 각자 고친 상태에서, c2의 다른 충돌 커밋을 cherry-pick해 해결한다 → E의 prev는 한 행 그대로이고, E에는 해결 판이 생기지 않는다.
8. **각자 다시 고정한 것은 충돌이 아니다**: master에서 `tick; ait offer src/stock.c tests/nobroken.sh as k2 -r pass`. 브랜치 x와 y가 각자 stock.c의 다른 행(5행, 40행)을 바꿔 `ait commit -r pass`. x에서 `tick; git merge --no-edit y` → 충돌 없이 끝난다. `.ait/<K2>`는 x의 판이고, 합쳐진 stock.c는 x의 고정과 다르므로 `ait peek now <K2>` → `provider src/stock.c ?changed`. commit-msg가 stderr에 `warning: '<K2>' is unverified after this merge`와 H7(`ait amend <K2> -m <why>`)을 쓴다(두 부모에서 K2는 미확인이 아니었다, 7.1.5, 9.1).
   - 같은 것을 `contract pull`로 받으면 미확인이 생기지 않는다. pull은 받기 전에 멈추고 고르기와 확인 hint를 준다(7.12.1, 7.12.5. T33의 기준 3.10 행).
9. **통합에서 어느 출발 판에서도 갈 수 없는 레코드는 거절**(7.1.3-3·4·7): 2의 충돌에서 `tests/long.sh`와 레코드를 모두 ours로 정리하고(`git checkout --ours tests/long.sh .ait/<O> .ait/<D>`) `.ait/<O>`의 state를 손으로 current로 고쳐 `git add -A; git commit` → `err E_STATE`(pre-commit). O에는 current인 출발 판이 없으므로 되돌리지 않고(7.1.3-3), future에서 current로는 이행으로만 간다(7.1.3-4, 2.3.2). stderr의 hint에 `restore '.ait/<O>' as in '<y1의 HEAD OID>' and stage the result`가 있다(y1의 판은 validate를 지나고, 두 판은 서로에게 갈 수 있으므로 7.13.1-5로도 ours다, 7.1.3-7). 그대로 하고 `git commit` → 성공.
   - **current였던 적이 없는 계약은 되돌리지 않는다**: 같은 충돌에서 `tests/long.sh`의 2행을 합쳐 쓴 채 같은 조작(레코드는 ours, `.ait/<O>`의 state를 current로)을 하고 `git add -A; git commit` → `err E_STATE`. O는 어느 출발 판에서도 current가 아니므로 고정이 바뀌어도 되돌리기 대상이 아니고(7.1.3-3), future에서 current로는 이행으로만 간다(7.1.3-4). `is reopened` 경고는 없다.
10. **통합에서 고친 명세 파일은 다시 고정된다**: 충돌 없는 브랜치를 `git merge --no-commit --no-ff`하고, HEAD의 future 계약 O·D가 선언한 `tests/long.sh`를 손으로 고쳐 `git add`, `git commit` → 성공. O·D의 `file` 행이 고친 판으로 다시 고정되고, future이므로 미확인은 없다.
11. **결과 없이 합쳐진 current 계약의 조건은 약속으로 되돌린다**(규칙 3·4, 7.1.3-3): master에 current offer K(`tick; ait offer src/stock.c:1-10 tests/long.sh as k -r pass`)를 두고, 거기서 갈라진 k1과 k2가 각자 `tests/long.sh`의 2행과 11행을 고쳐 `contract add tests/long.sh`·`tick; contract commit -r pass`(k2의 것이 Rk2). k1에서 `tick; git merge k2` → `.ait/<K>`가 충돌한다(양쪽이 각자 조건을 바꿨다. 같은 파일을 쓰는 준비의 O·D도 충돌한다). `git checkout --ours .ait/<K> .ait/<O> .ait/<D>; git add -A; tick; git commit --no-edit` → 성공(`tests/long.sh`는 자동 병합된다).
    - stderr에 `warning: '<K>' is reopened in this merge: its pins changed without a check`와, 이 커밋의 `Ait-Agent`인 A가 되돌린 쪽이므로 H10 `when ready, stage the code and run 'ait fulfill <K> -m <why>' to get the tasks`가 있다(7.1.5). `is unverified after this merge` 경고는 없다(K는 future가 되었으므로 미확인이 아니다).
    - `rec(K)`: `state future`. file 행은 합쳐진 `tests/long.sh`로 다시 고정되고, provider 행은 그대로다(`src/stock.c:1-10 <고정 때의 blob>`). prev는 두 행(k1·k2 판의 blob, OID 순)이다. 확인 행은 없다(future로 되돌리면 지운다, 7.1.3-3). 메시지에 새 `Ait-Request`가 있다.
    - `ait peek now src/stock.c`에 K가 없다(ID 인자는 view와 상관없이 보이므로 범위로 본다, 7.9.3). `ait peek <K>`의 마지막 event는 `event <새 RID> agent-a reopened`다.
    - 그 뒤 stock.c 3행을 바꾸는 `git commit`의 경고에는 K가 없다(future는 걸리지 않는다).
    - hint대로 stage 없이 `tick; ait fulfill K -r pass` → `fulfilled <K>`, `check <K> reported`. `ait peek now src/stock.c`에 K가 보이고 `unverified` 행이 없다.
    - **판단으로 넘기기**: 준비를 다시 하고 merge까지 한다. `tick; ait fulfill K --force -m 사소함` → `fulfilled <K>`, `check <K> forced`(8.5).
    - **통합 hook이 되돌리면 그 통합을 한 쪽이 되돌린 쪽이다**(1.5d): 준비를 다시 하고 같은 worktree에 B(agent-b) session을 더 연다(기본 session은 A 그대로다). merge와 `--ours` 정리까지 한 뒤 커밋만 B로 한다(`tick; AIT_SESSION=$B git commit --no-edit`). 메시지의 `Ait-Agent`는 agent-b다(7.1.5). B의 `AIT_SESSION=$B ait todo`에 K 블록이 나오고 hint(H10)는 `AIT_SESSION=<B> ait fulfill <K> -m <why>`다(이 병합 커밋의 K 판은 어느 부모의 판에도 포함되지 않는 future 판이고, 부모의 K 판이 current이며, 그것을 포함하는 future 판이 부모에 없으므로 되돌린 커밋이다. 그 요청의 `Ait-Agent`가 agent-b다, 1.5d. B는 기본 session이 아니므로 명령 앞에 `AIT_SESSION=<B> `가 붙는다, 5.9a). A의 `ait todo`의 K 블록 hint는 H14 `'<K>' was reopened by 'agent-b'; develop against it, or, if it is no longer needed, run 'ait future cancel <K> -m <why>'`이고 `ait fulfill <K>`(H10)는 없다(K는 A가 만들었지만 되돌린 쪽은 B다, 5.9a).
12. **같은 계약을 겨눈 두 drop은 합쳐진다**: master에서 current offer K3(`ait offer src/order.c p as k3 -r pass`)를 둔다. 브랜치 d1과 d2가 각자 `tick; ait future drop K3`을 건다. d1에서 `tick; git merge --no-edit d2` → 충돌 없이 끝난다(5.9는 drop의 수를 보지 않는다). `ait peek <K3>`에 `drop` 행이 둘이다.
13. **통합에서 되돌린 D를 떠맡은 current O도 되돌린다**(7.1.3-3, 5.9 d): `tests/long.sh`와 같은 내용의 `tests/long2.sh`와 그 exec 정의 `specs/long2.yaml`을 만든다. master에서 `tick; ait demand src/order.c src/stock.c tests/long2.sh as d2 offer src/stock.c:1-10 p as o2 undertake d2 o2 -r pass`. 거기서 갈라진 m1과 m2가 각자 `tests/long2.sh`의 2행과 11행을 고쳐 `contract add tests/long2.sh`·`tick; contract commit -r pass`(과제는 `<D2>:specs/long2.yaml` 하나다). m1에서 `tick; git merge m2` → `.ait/<D2>`가 충돌한다. `git checkout --ours .ait/<D2>; git add -A; tick; git commit --no-edit` → 성공.
    - stderr에 `warning: '<D2>' is reopened in this merge: its pins changed without a check`와 `warning: '<O2>' is reopened in this merge: it undertakes reopened '<D2>'`가 있다(O2의 고정은 바뀌지 않았고 D2를 포함하므로 함께 되돌린다, 7.1.5). hint(H10)에 `ait fulfill <D2> -m <why>`와 `ait fulfill <O2> -m <why>`가 있다.
    - `rec(D2)`·`rec(O2)`의 state는 future다. O2의 행은 state 말고 바뀌지 않았지만, D2를 포함하므로 함께 되돌려진다(규칙 5). `.ait/undertake/<D2>-<O2>`는 `state undertaken` 그대로다. `rec(O2)`의 prev는 병합 전 O2 판의 blob 한 행이다(두 출발 판이 같아 극대 판이 하나다, 7.1.3-5).
    - stage 없이 `tick; ait fulfill O2 -r pass` → stdout에 `fulfilled <D2> via <O2>`와 `fulfilled <O2>`가 있다. 과제는 `<D2>:specs/long2.yaml`과 `<O2>:specs/p.yaml`이다.
14. **cherry-pick 해결도 확인 행으로 판정한다**(7.1.3-3, 1.5a): 준비를 다시 하고 master에 11의 K를 둔다. 거기서 갈라진 c3이 `tests/long.sh` 2행을 고쳐 `contract add tests/long.sh`·`tick; contract commit -r pass`(Rk3. `rec(K)`에 `check reported <pins(Rk3, K)>`). master도 2행을 다르게 고쳐 같은 방법으로 커밋한다. master에서 `tick; git cherry-pick <Rk3 커밋>` → `tests/long.sh`와 `.ait/<K>`·`.ait/<O>`·`.ait/<D>`가 충돌한다. long.sh를 합쳐 쓰고 레코드는 `--ours`로 정리한 뒤 `git add -A; tick; git cherry-pick --continue` → 성공.
    - 메시지의 `Ait-Request`는 Rk3이 아닌 새 RID다.
    - K는 합친 long.sh로 다시 고정되어 고정이 current인 출발 판 어느 것과도 다르므로 확인이 아니다. hook이 K를 되돌리고 확인 행을 지운다: stderr에 `warning: '<K>' is reopened in this merge: its pins changed without a check`가 있고 `rec(K)`의 state는 future다.
15. **밀려난 판은 prev에 들지 않는다**(7.1.3-5, 7.1.5): 준비를 다시 하고 Hbase에서 브랜치 p1과 p2를 만든다. p1에서 `tick; ait future amend O src/stock.c:1-5 -`(Rp1), p2에서 `tick; ait future amend O src/stock.c:6-9 -`(Rp2). master에서 `tick; git merge -s ours --no-ff --no-commit p1 p2`(octopus. O의 출발 판은 HEAD 판 A, p1 판 B, p2 판 C이고, A는 B·C의 prev DAG에 있다) 뒤 `tick; git commit --no-edit` → 성공.
    - `rec(O)`는 prev를 빼면 A와 같다(provider `src/stock.c <blob>`). prev는 B·C 판의 blob 두 행(OID 순)이다. A는 밀려났고, 남은 극대 판 B·C는 뜻이 다르므로 충돌 해결이다(index가 어느 극대 판과도 같지 않아도 그렇다). `rec(D)`의 prev는 그대로 한 행이다(세 출발 판이 같다).
    - 메시지에 새 `Ait-Request`가 있다.
16. **validate를 지나게 하는 판으로 복원한다**(7.1.3-7, 5.9 d): 준비를 다시 하고 master에서 `tick; ait offer src/order.c p as j1 offer src/stock.c:1-5 p as j2 -r pass`, `tick; ait future drop J1 as xj`. 거기서 브랜치 d1과 d2를 만든다. d1에서 `tick; ait fulfill XJ` → `fulfilled <XJ>`, `withdrawn <J1>`. d2에서 `tick; ait future amend XJ J2`, `tick; ait fulfill XJ` → `fulfilled <XJ>`, `withdrawn <J2>`(같은 drop을 다른 target으로 이행했다). d1에서 `tick; git merge d2` → `.ait/<XJ>`만 충돌한다. `.ait/<J1>`은 d1만, `.ait/<J2>`는 d2만 바꿨으므로 git이 tree 수준에서 바꾼 쪽의 판(둘 다 withdrawn)을 고르고, merge driver는 돌지 않는다(driver는 양쪽이 모두 바꾼 경로에만 돈다).
    - `git checkout --ours .ait/<XJ>; git add -A; tick; git commit --no-edit` → `err E_STATE`(validate d: J2는 withdrawn인데 그것을 겨눈 fulfilled drop이 없다). 각 레코드는 prev를 빼면 출발 판 그대로이므로 전이 검사(7.1.3-4)는 지났다. 거절되기 전에 hook은 2~5의 결과를 index에 쓴다(7.1.3-6): XJ의 극대 판은 d1·d2 판 둘이고 뜻이 다르므로 index의 XJ는 prev가 두 행이 된다.
    - stderr의 hint에 `restore '.ait/<J2>' as in '<d1의 HEAD OID>' and stage the result`가 있다(J2를 d1의 current 판으로 되돌리면 validate를 지난다. d2의 판은 지나지 못한다, 7.1.3-7).
    - 그대로 하고 `git commit --no-edit` → 성공. 첫 실행이 index에 쓴 XJ의 두 행 prev는 다시 돈 전이 검사에 걸리지 않는다(7.1.3-4는 prev를 빼고 비교한다). `rec(J2)`는 `state current`, `rec(J1)`은 `state withdrawn`이고, `rec(XJ)`는 `state fulfilled`, `target <J1>`, prev 두 행(d1·d2 판의 blob, OID 순)이다. `rec(J2)`의 prev는 d2 판(withdrawn)의 blob 한 행이다(J2의 극대 판은 d2 판 하나이고 index는 그와 다르다, 7.1.3-5).
17. **current인 출발 판의 고정과만 비교한다**(7.1.3-3): 준비를 다시 하고 master에서 `tick; ait offer src/stock.c:1-10 p as k -r pass`. 거기서 브랜치 f와 c를 만든다. f에서 `tick; ait future amend K src/stock.c:1-5 -`(Rf. 되돌리며 범위를 바꾼다), c에서 `tick; ait amend K src/stock.c:1-8 - -r pass`(current 그대로 범위를 바꾼다). c에서 `tick; git merge f` → `.ait/<K>`가 충돌한다. `git checkout --ours .ait/<K>`(c 판, current) 뒤 그 provider 행만 f 판의 provider 행(`git show f:.ait/<K>`의 것)으로 바꾸고 `git add -A; tick; git commit --no-edit` → 성공(`E_STATE`가 아니다).
    - hook이 K를 되돌린다: index의 K는 current이고 그 고정은 current인 출발 판(c 판)의 것과 다르다(7.1.3-3). f 판의 고정과는 같지만 f 판은 future이므로 비교하지 않는다(그 고정은 확인된 적이 없다). 모든 출발 판과 비교했다면 되돌리지 않아 전이 검사에서 막혔을 것이다. 되돌린 K는 prev를 빼면 f 판과 같으므로 전이 검사(7.1.3-4)를 지난다. 그래서 결과는 future 판을 남기고 current 판을 버린 것이고, stderr의 경고는 `warning: '<K>' is reopened in this merge: it keeps the future version over the current one`이다(7.1.5의 첫 이유. 고정도 current인 c 판과 다르지만 처음 맞는 이유 하나만 쓴다).
    - `rec(K)`: `state future`, provider `src/stock.c:1-5 <blob>`. prev는 두 행(c·f 판의 blob, OID 순)이다(뜻이 다른 극대 판 둘, 7.1.3-5). 메시지에 새 `Ait-Request`가 있다.
18. **통합 중에 새로 생긴 레코드의 prev는 `-`다**(7.1.3-5): 준비를 다시 하고 Hbase에서 갈라진 브랜치 nb에 order.c를 바꾸는 일반 커밋을 둔다. master에서 `tick; git merge --no-commit --no-ff nb`. 이 상태에서 `ait contract new offer` → `path .ait/<On>`(쓰기 명령이 아니므로 merge 중에도 된다, 5.2). `specs/p.yaml`을 만들고, `.ait/<On>`을 정규형으로 쓴다: `role offer` / `state future` / `provider src/stock.c <blob(HEAD:src/stock.c)>` / `file specs/p.yaml 100644 <git hash-object specs/p.yaml의 값> def` / `creator agent-a <A>` / `prev <blob(HEAD:.ait/<O>)>`(일부러 틀린 prev). `git add -A; tick; git commit --no-edit` → 성공.
    - `rec(On)`의 prev는 `-` 한 행이다. On은 어느 출발 판에도 없으므로 극대 판이 없다.
    - 메시지에 새 `Ait-Request`가 있고(On은 어느 출발 판과도 다르다, 7.1.5), `ait peek <On>`의 첫 event는 `event <그 RID> agent-a created-future`다.
    - 같은 일을 `tick; AIT_SESSION=- git commit --no-edit`로 하면 → 성공(hook은 session 때문에 git 조작을 막지 않는다, 5.1). `rec(On)`의 creator는 `unknown -`이고(써 둔 `agent-a <A>`는 새 레코드이므로 덮인다, 7.1.3-2) 메시지의 `Ait-Agent`는 `unknown`이다. 그래서 On은 누구의 "내가 만든 계약"도 아니다(2.2.2).
19. **함께 복원해야 지나는 레코드**(7.1.3-7): 준비를 다시 하고 master에서 `tick; ait offer src/stock.c:1-5 p as j2 -r pass`. 거기서 브랜치 d1과 d2를 만든다. d2에서 `tick; ait future drop J2 as x2`, `tick; ait fulfill X2` → `fulfilled <X2>`, `withdrawn <J2>`. d1에서 `tick; ait future offer src/order.c p -m d1`. d1에서 `tick; git merge --no-commit --no-ff d2` → 충돌 없이 멈춘다(J2는 d2만 바꿨고 X2는 d2에만 있다). `git --literal-pathspecs checkout HEAD -- .ait/<J2>`(J2를 d1의 current 판으로 둔다) 뒤 `git commit --no-edit` → `err E_STATE`(validate d: X2는 fulfilled인데 target J2가 withdrawn이 아니다).
    - X2의 출발 판은 d2의 판(지금 index와 같다)과 없음뿐이므로, X2 하나를 어느 출발 판으로 복원해도 지나지 못한다(없음으로 되돌리는 것은 레코드를 지우는 것이다, 7.1.3-1). 그래서 hint는 그 오류에 얽힌 X와 target을 함께 한 출발 판으로 되돌린다: `restore '.ait/<J2>' '.ait/<X2>' as in '<d2의 OID>' and stage the result`다(7.1.3-7).
    - 그대로 하고 `git commit --no-edit` → 성공. `rec(J2)`는 `state withdrawn`, `rec(X2)`는 `state fulfilled`다.
20. **자동 병합도 레코드를 버리지 못한다**(7.1.5, 7.1.3-1·7): 준비를 다시 한다. Hbase에서 브랜치 sb를 만들고 sb에서 `tick; ait future offer src/order.c tests/long.sh as ob -m ob`(Rb. `tests/long.sh`는 이미 명세 파일이므로 Rb는 `.ait/<Ob>` 하나만 더한다). master에 order.c를 바꾸는 일반 커밋을 둔 뒤 `tick; git merge -s ours --no-ff --no-edit sb`:
    - exit ≠ 0이고 HEAD는 그대로다. pre-merge-commit에는 MERGE_HEAD가 없어 validate만 하고(통과한다), commit-msg가 7.1.3을 읽기만 하며 돌려 sb의 출발 판에 있는 `.ait/<Ob>`가 index에 없는 것을 본다(7.1.5, 7.1.3-1). stderr에 `error: not allowed:`로 시작하는 행과 그 오류의 hint `restore '.ait/<Ob>' as in '<sb의 OID>' and stage the result`(7.1.3-7. Ob의 명세 파일 `tests/long.sh`는 index에 있으므로 넣지 않는다), 이어서 `then run 'git commit --allow-empty --no-edit --cleanup=strip'`가 있다(마치는 커밋, 7.1.3). `finish it with …`은 index만 바꿔야 할 때의 문구이므로 여기에는 없다. merge는 끝나지 않고 남는다: `git rev-parse -q --verify MERGE_HEAD`가 sb의 OID다.
    - `tick; git commit --no-edit` → `err E_STATE`(pre-commit, 7.1.3-1), HEAD는 그대로다. hint에 `restore '.ait/<Ob>' as in '<sb의 OID>' and stage the result`가 있다(Ob의 출발 판은 sb의 것뿐이고, 그 판으로 복원하면 validate를 지난다, 7.1.3-7).
    - 그대로 하고 `git commit --no-edit` → 성공. `rec(Ob)`는 sb의 판 그대로이고(prev `-`), 병합 커밋에는 기록줄이 없다(레코드가 모두 출발 판과 같다, 7.1.5). `ait contract pull <Ob>` → `already <Ob>`(sb의 판이 HEAD 판이다).
    - 복원 대신 `git merge --abort` → HEAD는 merge 전 그대로이고, `ait contract pull <Ob>` → `included <Ob> from '<Rb의 커밋>'`이다(Ob의 판이 HEAD에 없다).
    - **새 명세 파일까지 버리면 함께 복원한다**: 준비를 다시 하되 sb에서 새 명세 정의 `specs/new.yaml`(`body: new`)을 만들어 `tick; ait future offer src/order.c specs/new.yaml as ob -m ob`(Rb). 같은 `-s ours` 병합 → commit-msg가 막고, hint는 `restore '.ait/<Ob>' 'specs/new.yaml' as in '<sb의 OID>' and stage the result`다(Ob의 판이 선언한 명세 파일이 index에 없다, 7.1.3-7). 그대로 하고 `git commit --no-edit` → 성공. 레코드만 복원했다면 묶음 무결성(2.4)에 막혔을 것이다.
21. **부모에서 current가 아니면 되돌린 것이 아니다**(1.5d, 7.1.5): 준비를 다시 하고 master에서 `tick; ait offer src/stock.c:1-10 p as k -r pass`. 거기서 브랜치 g를 만들어 `tick; ait amend K src/stock.c:1-8 - -r pass`(Rg. current 그대로). master에서 `tick; ait future amend K src/stock.c:1-5 -`(K는 future). master에서 `tick; git cherry-pick <Rg 커밋>` → `.ait/<K>`가 충돌한다. `git checkout --ours .ait/<K>` 뒤 provider 행을 `provider src/stock.c:1-6 <blob(HEAD:src/stock.c)>`로 고쳐(통합 hook은 범위를 다시 고정하지 않으므로 정규형으로 쓴다, 7.1.3-2) `git add -A; tick; git cherry-pick --continue` → 성공.
    - `is reopened` 경고가 없다. 부모(HEAD)에서 K는 future였으므로 이 커밋은 K의 되돌린 커밋이 아니다(current인 출발 판은 들어오는 쪽 I에만 있다, 7.1.3의 부모, 1.5d). `ait peek <K>`의 마지막 event는 `reopened`가 아니라 `amended`다.
22. **state만 바꾼 해결도 되돌리기다**(7.1.5): 준비를 다시 하고 master에서 `tick; ait offer src/stock.c:1-10 p as k -r pass`. 거기서 갈라진 x와 y가 각자 `tick; ait amend K src/stock.c:1-8 - -r pass`, `tick; ait amend K src/stock.c:1-9 - -r pass`. x에서 `tick; git merge y` → `.ait/<K>`가 충돌한다. `git checkout --ours .ait/<K>` 뒤 `state current`만 `state future`로 고쳐 `git add -A; tick; git commit --no-edit` → 성공.
    - stderr에 `warning: '<K>' is reopened in this merge`가 있고 그 행 끝에 이유가 없다(future인 출발 판이 없고, 고정은 current인 x 판과 같고, 떠맡은 D도 없다. 이유를 알 수 없으면 사실만 쓴다, 7.1.5). 이어서 H10(`ait fulfill <K> -m <why>`)이 있다.
23. **기록줄의 agent가 없으면 되돌린 쪽이 없다**(1.5d, H10, H14): 11의 준비와 merge, `--ours` 정리까지 한 뒤 커밋을 `tick; AIT_SESSION=- git commit --no-edit`로 한다 → 성공. 메시지의 `Ait-Agent`는 `unknown`이다(7.1.5). stderr의 `is reopened` 경고 뒤에 H10은 없다(현재 session이 없다).
    - A의 `ait todo`에 K 블록이 나오고 hint는 H10 `when ready, stage the code and run 'ait fulfill <K> -m <why>' to get the tasks`다. H14는 없다(되돌린 쪽이 없으므로 K는 A가 만든, 되돌린 쪽이 없는 future offer다, 5.9a).
24. **해결은 먼저 커밋하고 git의 안내로 잇는다**(7.1.3의 마치는 커밋과 마치는 hint, 7.13.1-5): 준비를 다시 하고 master에서 `tick; ait offer src/stock.c:1-10 p as k -r pass`. 거기서 브랜치 rb를 만들어 `tick; ait future amend K src/stock.c:1-5 -`(Rr). master에서 `tick; ait future amend K src/stock.c:1-6 -`. rb에서 `tick; git rebase master` → `.ait/<K>`가 충돌한다. driver의 hint는 `keep 'theirs' for '.ait/<K>' and stage it (…)`와 `then commit it through ait: 'ait contract add .ait/<K>' and 'ait contract commit -m <why>' (…)`다(서로에게 갈 수 있고 rebase 디렉터리가 있으므로 이 브랜치의 Rr 쪽인 theirs다, 7.13.1-5).
    - 이 상태에서 `ait future offer src/order.c p` → `err E_BUSY`, hint는 `a git rebase is in progress; finish or abort it as 'git status' says, then rerun this command`와 그 상태의 마치는 hint `commit the resolution first: 'git commit --allow-empty --allow-empty-message -C REBASE_HEAD', then continue as git says`다(통합 상태다, 5.8).
    - 거기서 `.ait/<O>`도 작업 폴더에서만 고쳐 둔 채 같은 명령 → 위의 hint에 `; keep your contract changes out of it, since only ait commits contract files`가 이어진다(마치는 커밋은 stage된 것만 싣는다, 5.9a). 끝나면 `git --literal-pathspecs checkout -- .ait/<O>`.
    - hint대로 고르고 `git add -A; tick; git commit --allow-empty --allow-empty-message -C REBASE_HEAD` → 해결 커밋에 새 `Ait-Request`가 있고(pre-commit·commit-msg가 돌았다. REBASE_HEAD와 MERGE_MSG가 있어 통합 상태다. 두 판이 모두 극대 판이고 뜻이 다르므로 충돌 해결이다, 7.1.3-5), 제목과 작성자는 Rr의 것이다. 이어서 `git rebase --continue` → 성공.
    - `git commit` 없이 `git rebase --continue`만 하면 hook이 돌지 않아 7.1.3의 정리와 새 기록줄이 없다(11.1).
    - **apply 백엔드**: 준비를 다시 하고 같은 충돌을 `tick; git rebase --apply master`로 낸다. driver의 hint는 같다. 마치는 커밋 `git commit --allow-empty --allow-empty-message -C REBASE_HEAD`로 커밋하면 해결 커밋에 새 `Ait-Request`가 있다. 이어서 `git rebase --continue` → git이 빈 패치로 멈추고 `--skip`을 권한다(그 패치는 이미 커밋됐다). `git rebase --skip` → 성공. 커밋한 뒤 `ait future offer src/order.c p`를 하면 `E_BUSY`의 hint는 `a git rebase is in progress; finish or abort it as 'git status' says, then rerun this command`뿐이다(통합 상태가 아니고 바뀐 계약 경로도 없다).
25. **cherry-pick의 해결도 먼저 커밋한다**(7.1.3의 마치는 hint): 준비를 다시 하고 브랜치 c4에서 `tick; ait future amend O src/stock.c:1-5 -`(Rc), master에서 `tick; ait future amend O src/stock.c:1-6 -`. master에서 `tick; git cherry-pick <Rc 커밋>` → `.ait/<O>`가 충돌하고, driver의 hint는 `keep 'ours' for '.ait/<O>' and stage it ('ours' is HEAD; 'theirs' is the incoming commit, your stashed change, or in a rebase your own commit)`와 `then commit it through ait: 'ait contract add .ait/<O>' and 'ait contract commit -m <why>' (if a git operation stopped, 'ait contract add' says how to finish it first)`다(7.13.1-5). 그대로 `--ours`로 고르고 `git add -A`. `ait contract add .ait/<O>` → `err E_BUSY`, hint는 `a git cherry-pick is in progress; …`와 `commit the resolution first: 'git commit --allow-empty --no-edit --cleanup=strip', then continue as git says`다(통합 상태다, 5.8).
    - 바로 `git cherry-pick --continue` → pre-commit은 돌지만 git은 hook이 돌기 전의 index로 빈 커밋이라 보고 멈추고 `--skip`을 권한다(해결이 HEAD와 같았다. hook이 정리한 index는 남는다). 그것이 마치는 hint가 먼저 커밋하라는 까닭이다(7.1.3).
    - 대신 `tick; git commit --allow-empty --no-edit --cleanup=strip` → 성공. 메시지에 새 `Ait-Request`가 있다(7.1.5). 이어서 `ait contract pull <O>` → `already <O>`(c4의 판이 HEAD 판의 prev에 있다).
26. **git 조작 중에는 git의 안내로 돌려보낸다**(5.8 `E_BUSY`, 7.1.3): 준비를 다시 하고 브랜치 pk에 계약 커밋 둘(`tick; ait future offer src/order.c p as p1`, `tick; ait future offer src/order.c p as p2`)을 둔다. master에서 order.c를 고쳐 `git add`한 채 `git cherry-pick <첫째> <둘째>` → git이 거절하고 `sequencer/todo`만 남는다. `ait future offer src/stock.c p` → `err E_BUSY`, hint는 `a git cherry-pick is in progress; finish or abort it as 'git status' says, then rerun this command`이고 마치는 hint는 없다(통합 상태가 아니다). 그 밖의 로컬 변경 hint도 없다(다시 실행할 때는 조작이 끝난 뒤다, 5.8). 고르기를 어떻게 마칠지는 git의 일이다.
    - `git am`이 멈춘 상태(`rebase-apply/applying`)에서도 hint는 같은 꼴(`a git am is in progress; …`)이다. `git am`은 이 hook들을 부르지 않으므로 통합 상태가 아니다(7.1.3, 11.1).
27. **편집하려고 멈춘 rebase는 통합 상태가 아니다**(7.1.3, 5.8 `E_BUSY`, 5.9a의 진행 중인 git 조작): 준비를 다시 하고 `GIT_SEQUENCE_EDITOR`로 첫 `pick`을 `edit`로 바꿔 `tick; git rebase -i HEAD~1` → 멈춘다(REBASE_HEAD는 있지만 MERGE_MSG가 없다). `.ait/<O>`를 고쳐 `git add`한 뒤:
    - `ait future offer src/stock.c p` → `err E_BUSY`. hint는 `a git rebase is in progress; finish or abort it as 'git status' says, then rerun this command; keep your contract changes out of it, since only ait commits contract files`이고 마치는 hint는 없다(통합 상태가 아니다. 그대로 이으면 git이 계약 파일을 hook 없이 커밋한다, 5.9a).
    - `tick; git commit -qm x` → `err E_GUARD`(통합 상태가 아니므로 일반 pre-commit이다, 7.1.2). hint의 `ait contract add`·`ait contract commit` 앞에 `first finish or abort the git rebase in progress ('git status'); keep your contract changes out of it, since only ait commits contract files`가 있다.

## T29 merge driver 단위 (7.13)

1. OURS와 THEIRS가 같으면 → exit 0.
2. THEIRS.prev가 OURS → THEIRS를 택함.
3. THEIRS → V1 → OURS 순서로 이어지면 → THEIRS.
4. OURS.prev가 THEIRS → OURS를 택함.
5. THEIRS의 prev가 두 개(X, OURS) → THEIRS.
6. 같은 V0에서 갈라짐 → exit 1, 충돌 표지.
7. DAG 중간 blob이 없음 → 6과 같다.
8. 형식 오류 → 6과 같다.
9. 같은 V0(current, provider `src/stock.c:20-30 <B0>`)에서 갈라졌고, 양쪽 모두 V0을 다시 고정한 판이다(1.5e): prev를 빼면 provider 행의 PATH가 같고, S-E는 V0의 범위를 B0에서 그쪽 BLOB으로 사상한 것이며(3.7), 나머지 행은 V0과 같다. 예: OURS는 맨 앞에 5행을 더한 blob B1로 `src/stock.c:25-35 <B1>`, THEIRS는 40행만 바꾼 blob B2로 `src/stock.c:20-30 <B2>`(B0·B1·B2는 저장소에 있다) → OURS 그대로, exit 0.
10. 9와 같지만 한쪽의 S-E가 사상한 범위와 다르다(예: THEIRS가 `src/stock.c:20-29 <B2>`. 범위를 옮겼다) → 6과 같다. 한쪽의 `file` 행이 V0과 다를 때도 6과 같다.
11. 충돌이면 stderr hint에 고를 쪽과 경로가 있다(driver의 넷째 인자). V0(future)에서 OURS는 범위를 amend하고 THEIRS는 cancel했으면 `keep 'theirs' for '<PATH>' and stage it`이 있다(OURS의 판에서 THEIRS의 판으로만 2.3의 전이로 갈 수 있으므로 THEIRS, 7.13.1-5). 양쪽이 모두 범위만 바꿨으면(서로에게 갈 수 있다) `keep 'ours'`다.
12. **이 브랜치의 판**(7.13.1-5): 11의 "범위만 바꾼" 충돌을 git 디렉터리의 상태만 바꿔 돌린다. `rebase-merge/`나 `applying` 없는 `rebase-apply/`가 있으면 `keep 'theirs'`, `rebase-apply/applying`도 있으면(`git am`) `keep 'ours'`, 아무것도 없으면 `keep 'ours'`다. 둘째 hint 행은 언제나 `then commit it through ait: …`다.

## T30 두 worktree 왕복 — 핵심 흐름 (7.12, 9, 10.2)

준비:

```sh
cd "$T/r"; git worktree add -q "$T/wa" -b a; git worktree add -q "$T/wb" -b b
cd "$T/wa"; A=$(ait session open --agent agent-a); AIT_SESSION=$A ait listen >> $T/inbox.$A &
cd "$T/wb"; B=$(ait session open --agent agent-b); AIT_SESSION=$B ait sub src/stock.c; AIT_SESSION=$B ait listen >> $T/inbox.$B &
```

1. wa(A): `tests/guard.sh`를 만들고 `tick; ait future demand src/order.c:10-40 src/stock.c tests/guard.sh as d -m "가드 요구"`(C1·R1). `settle` 후:
   - `ev(B)` = 1.
   - 사건의 처음 8행(B는 구독으로 받았고, B에게 맞는 hint가 없으므로 `hint:` 행이 없다. 들이는 명령은 hint만 준다, 9.4):
     ```
     ait-event 1
     request <R1>
     recipient <B>
     ref refs/heads/a
     reason sub scope src/stock.c
     in-head no
     contract <D> created-future

     ```
   - `ev(A)` = 0.
2. wb: `tick; ait contract pull D` → `included <D> from '<C1>'`(D의 판이 처음 들어간 커밋 C1과 그 조상을 받는다. fast-forward, 7.12.6).
3. wb(B): `ait todo` → D는 보이지 않는다(B가 만든 future가 없다). `ait peek src/stock.c`는 D를 보이고, stderr의 hint(H11)에 `ait future offer src/stock.c specs/guard.yaml as o undertake <D> o -m <why>`가 있다(D를 떠맡은 offer가 어디에도 없다. D의 provider와 정의).
4. wb(B): `tick; ait future offer src/stock.c tests/pass.sh as o undertake D o -m "맡겠다"`(R2). `settle` 후 `ev(A)` = 1(`reason future demand <D>`, `in-head no`).
   - wb(B): `ait peek src/stock.c`의 hint(H11)는 이제 `ait fulfill <O> -m <why>`(H10)다(D를 떠맡은 내 O가 HEAD에 있다).
   - wa(A): `ait peek src/stock.c`의 hint(H11)는 `'<O>' on 'refs/heads/b' undertakes it; include it and develop against it`과 그것을 들이는 명령 `ait contract pull <D> <O>`다(O가 다른 ref에만 있다. R2는 떠맡기 기록 `<D>-<O>`도 더하므로 D와 O를 함께 지정한다. 이름은 계약 ID 순이다, 5.9a의 들이기, 7.12.3).
5. wb(B):
   - `ait todo` → O 블록에 `undertakes <D> future agent-a`와 `undertakes-file <D> specs/guard.yaml exec`와 `undertakes-file <D> tests/guard.sh input`이 있다.
   - `ait fulfill O` → `err E_NEEDS_RESULTS`, `ready yes`. 과제는 `<D>:specs/guard.yaml`과 `<O>:specs/pass.yaml`이다. 확인 결과 없이는 이행되지 않는다.
6. wb: stock.c 1행을 `s1 guard`로 바꾸고 `tick; git commit -qam impl`.
   - `ait fulfill O -m "가드 구현"` → `err E_NEEDS_RESULTS`, `ready yes`. 과제는 `<D>:specs/guard.yaml`과 `<O>:specs/pass.yaml`이다.
   - `tick; ait fulfill O -m "가드 구현" -r pass`(R3) → `fulfilled <O>`, `fulfilled <D> via <O>`.
   - `settle` 후 `ev(A)` = 2(`reason future demand <D>`, `in-head no`). 그 사건의 커밋 메시지에 B가 낸 결과가 있다. 그 사건의 hint(H15)는 `'<D>' is fulfilled; include it and build on it`과 그것을 들이는 명령 `ait contract pull <D> <O>`다(D는 A가 만든 demand다. R3은 D와 O를 함께 이행했으므로 둘을 지정한다, 5.9a의 들이기, 7.12.3).
7. wa: `tick; ait contract pull D O` → `included <D> from '<R3의 커밋>'`, `included <O> from '<R3의 커밋>'`(fast-forward). D·O의 판이 처음 들어간 커밋 R3과 그 조상 R2·impl을 받는다. 코드(impl)는 지정 없이 함께 온다(규칙 8). wa의 stock.c 1행은 `s1 guard`다. `ait peek now src/stock.c`는 D와 O를 보이며, 둘 다 current이고 서로 undertake로 이어져 있고, `unverified` 행이 없다.
8. 다시 `settle`해도 `ev`는 늘지 않는다.

## T31 받는 쪽 계산 (9.2, 9.3)

준비(master, session W):

```sh
tick; ait future offer src/order.c:1-5 p as q future demand src/order.c:40-45 src/order.c:1-5 specs/w.yaml as dd undertake dd q
tick; ait future demand src/stock.c:45-50 src/order.c:1-5 p as d2 undertake d2 Q
tick; ait offer src/order.c:20-25 p as z -r pass
```

이어서 worktree w1~w9(각자 master에서 분기)를 만들고 session S1~S9를 연다. agent 이름은 모두 다르고 W의 agent와도 다르다(내 약속·내 요구로 받는 것이 끼어들지 않게, 9.3). S6만 listener를 띄우지 않고, 나머지는 모두 띄운다. 각 session의 구독(`ait sub …`):

| S1 | S2 | S3 | S4 | S5 | S6 | S7 | S8 | S9 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `src/stock.c:1-10` | `src/stock.c:30-40` | `src/`, `DD` | `Q` | `Z` | `src/` | `D2` | `specs/w.yaml` | `/` |

W가 아래를 차례로 실행하고, 매번 `settle`한다. S6은 listener가 없어 받지 않는다. 마지막에 S6의 listener를 띄우면 그동안 S6에 맞는 사건이 순서대로 한꺼번에 나온다.

1. `ait future offer src/stock.c:5-8 p` → S1, S3, S9만 받는다.
2. `ait future amend DD - - p` → R은 DD 하나다(9.2). 받는 쪽은 다음과 같다.
   - S3: 한 건에 `reason sub id <DD>`와 `reason sub scope src/`를 모두 받는다. 그 사건의 hint는 `ait contract pull <DD>`를 앞에 붙인 `ait peek <DD>`다(H3: 내 구독인 계약의 조건이 바뀌었다. 새 조건이 S3의 HEAD에 없으므로 들이기를 붙인다, 5.9a).
   - S8: `reason sub scope specs/w.yaml`로 받는다(9.2에서 비교한 `prev` 판의 명세 파일이다, 9.3).
   - S9: 받는다.
   - S8과 S9의 사건 hint도 `ait contract pull <DD>`와 `ait peek <DD>`다(H3의 보는 쪽에는 id 구독과 범위 구독이 모두 든다).
   - S4와 S7은 받지 않는다. 떠맡은 상대(Q, 그리고 Q를 거친 D2)는 R에 들지 않는다.
3. `ait future amend Q src/order.c:1-4 -` → R은 Q 하나다. S3(`reason sub scope src/`), S4(`reason sub id <Q>`), S9가 받는다. S1, S2, S5, S7, S8은 받지 않는다(S7의 D2는 Q의 상대일 뿐 R에 들지 않는다).
4. `ait future drop Z` → S5가 `reason sub id <Z>`로 받는다(X의 target, 9.2-4). S3, S9도 받는다. S1, S2, S4, S7, S8은 받지 않는다.
   - 세 사건의 hint는 모두 H6 `if your code relies on it, demand it yourself: 'ait future demand <your code> src/order.c:20-25 specs/p.yaml -m <why>'`다(Z는 그들이 구독하는 남의 계약이고, Z를 겨눈 drop이 생겼다. Z의 provider와 정의가 받는 쪽 HEAD에 있으므로 들이기는 없다, 5.9a).
5. S1에서 `ait unsub` 후 W가 `ait future offer src/stock.c:1-2 p` → S1은 받지 않는다.
6. **판을 합친 커밋은 `prev` 판의 경로로도 맞춘다**(9.2-1, 9.3): 준비에 worktree w10과 session S10(구독 `src/order.c:6-9`)을 더한다. W가 브랜치 m1에서 `tick; ait future amend Q src/order.c:6-9 -`(Rq1)을, master에서 `tick; ait future amend Q src/order.c:1-3 -`(Rq2)를 한다. master에서 `tick; git merge m1` → `.ait/<Q>`가 충돌하고, `git checkout --ours .ait/<Q>; git add -A; tick; git commit --no-edit`(M). 각 요청과 M마다 `settle`한다.
   - M에서 Q의 판은 새 판이고 `prev`는 Rq1의 판과 Rq2의 판이다(뜻이 다른 극대 판 둘, 7.1.3-5). M의 판(Rq2와 같은 뜻)은 Rq1의 판과 뜻이 다르므로 Q는 R에 든다(9.2-1).
   - S10은 Rq1을 받고(`reason sub scope src/order.c:6-9`), M도 받는다. M의 C판 provider `src/order.c:1-3`은 S10의 범위와 겹치지 않지만, 비교한 `prev` 판 Rq1의 provider `src/order.c:6-9`가 겹친다(9.3). Rq2는 받지 않는다.

## T32 전달 신뢰성 (9.5, 9.6, 4.4·4.5)

준비: T30처럼 A와 B를 둔다. B를 열기 **전에** A가 r0을 만든다. 그다음 B를 열고 `ait sub src/stock.c`로 구독하고 B의 listener를 띄운다. r0~r4는 A가 만드는 요청이고, 모두 provider가 `src/stock.c`에 걸린 future offer다.

0. `settle` → `ev(B)` = 0. B가 열리기 전의 사건은 나오지 않는다(열 때의 ref가 tip이다).
1. **listener가 없는 동안의 사건은 다음에 나온다**: B의 listener를 SIGTERM으로 멈춘다(exit 0). A가 r1, r2를 만든다 → `ev(B)` = 0. B의 listener를 다시 띄우면 → `wait_ev B 2`, r1이 r2보다 먼저 나온다.
2. 다시 `settle`해도 `ev(B)`는 2 그대로다(seen).
3. **읽는 쪽이 사라지면 다음에 다시 나온다**: B의 listener를 멈추고 `{ AIT_SESSION=$B ait listen; echo $? > $T/rc; } | true &`로 띄운다. A가 r3을 만든다 → 그 listener는 쓰기 실패로 끝나고 `$T/rc`는 141이다(EPIPE면 git처럼 SIGPIPE로 끝난다, 5.7). seen에 r3이 없다. B의 listener를 원래대로 다시 띄우면 → `wait_ev B 3`(r3이 다시 나온다).
4. **동시 작업에 교착이 없다**: B의 listener가 도는 동안 B의 session으로 `ait sub src/order.c`를 다섯 번 실행하고(첫 번째만 `sub scope src/order.c`를 쓰고 나머지는 stdout이 비어 있다) A가 r4를 만든다 → `wait_ev B 4`, `ses(B)`에 `sub scope src/order.c`가 한 행 있다. 구독이 바뀌어 받는 상태가 달라진 차례는 내보내기 전에 멈추고 다음 차례에 새 상태로 다시 판정하므로(9.5-3), r4는 한 번만 나온다.
5. 같은 session으로 두 번째 `ait listen` → `err E_BUSY`, stderr에 `a listener is already running for session '<B>'`. `ait session list`의 B 블록은 `listening yes`다.
6. **사본은 새 키만 알린다**(9.5-3): A가 `tests/pass.sh`를 쓰는 r5(`tick; ait future offer src/stock.c tests/pass.sh as o5`)를 만든다 → `wait_ev B 5`. 이어서 A가 `tests/pass.sh` 끝에 `# x`를 더하고 `tick; git commit -q --amend --no-verify -a --no-edit`(C5'. 기록줄과 레코드는 r5 그대로이고, 실제 `tests/pass.sh`만 O5의 고정과 다르다) → `wait_ev B 6`. O5의 판(`version` 키)은 r5 때 보냈지만 실제 `tests/pass.sh`(`spec` 키)가 다르므로 새 사건이다. 같은 일을 `tests/pass.sh`를 바꾸지 않고 `--amend`로 메시지만 고쳐 하면 키가 모두 seen에 있으므로 사건이 나오지 않는다(같은 판은 몇 커밋에 생기든 한 번이다).

## T33 contract pull (7.12)

`contract pull`의 시험은 `drafts/pull-criteria.md` 3절의 기대 테스트 150행이다. 행마다 git으로 만든 이력과 그 이력에서 확인한 사실, 그리고 pull의 기대 결과를 적었고, 모두 실제 git으로 이력을 만들어 확인했다(`drafts/pull-criteria-runs/`). `t/pull/`에 그대로 옮겨 두었다(`make test-pull`). 각 행의 `fact`는 이력이 그 행이 말하는 상태인지 보고, `want`는 pull의 결과를 본다. 4절의 "모든 테스트에서 확인하는 것"도 함께 본다.

| 기준 3절 | 규격 | `t/pull/` |
| --- | --- | --- |
| 3.1 기본 | 7.12의 낱말, 7.12.1 | `t60-pull-basic.sh` |
| 3.2 고르기 | 7.12.1의 갈라짐과 `K=C`, 1.5d | `t61-pull-choose.sh` |
| 3.3 끝난 쪽이 이긴다 | 7.12의 남는 판, 2.3.2, 2.3.3 | `t62-pull-terminal.sh` |
| 3.4 한쪽만 바뀌었으면 고르지 않는다 | 7.12의 앞으로 나감 | `t63-pull-threeway.sh` |
| 3.5 어느 커밋을 들이나 | 7.12의 들이는 커밋, 7.12.2 | `t64-pull-which-commit.sh` |
| 3.6 들어오는 계약은 모두 지정한다 | 7.12.3 | `t65-pull-naming.sh` |
| 3.7 옛 판, 지운 레코드, 사고 판, 형식이 틀린 판 | 2.3.3, 7.12.1 | `t66-pull-accident.sh` |
| 3.8 명세 파일은 pull이 정한다 | 7.12의 받는 결과 | `t67-pull-spec-files.sh` |
| 3.9 구조 검사 | 7.12.4 | `t68-pull-structure.sh` |
| 3.10 확인 검사 | 7.12.5 | `t69-pull-check.sh` |
| 3.11 들일 끝이 여럿 | 7.12의 합친 결과, 7.12.4, 7.12.6, 7.1.3 | `t70-pull-ends.sh` |
| 3.12 작업 폴더와 shallow clone | 7.12.0 | `t71-pull-worktree.sh` |

그 밖에:

1. 지정한 계약에 후보 판이 하나도 없으면(없는 ID) → `err E_ID`.
2. `.ait`가 dirty → `err E_DIRTY`(5.2-3).
3. 고르기 없이 `-m`만 준 pull은 된다. 고르기·도우미 절·`--force`가 있는데 `-m`이 없으면 → `err E_USAGE`(5.3).
4. `ait contract fetch` → `err E_USAGE`(없는 명령). 받아 오기는 `git fetch`나 `ait listen --fetch`로 한다.

## T34 다른 코드베이스와 계약 주고받기 (7.10, 7.11, 7.12)

준비: R0을 push한다. `git clone -q "$T/origin.git" "$T/c2"; cd "$T/c2"; ait init`. C session을 열고 `ait sub src/stock.c`, C의 listener를 띄운다.

1. r(A): `tick; ait future offer src/stock.c p -m remote`(R) → `ait contract push origin` → `pushed origin master`, `request <R>`.
2. c2에서 `git fetch -q origin`만 실행 → `wait_ev C 1`. 사건의 ref 행은 정확히 `ref refs/remotes/origin/master`.
3. c2: `tick; ait contract pull <O>` → `included <O> from '<R의 커밋>'`(1의 offer가 O다).
4. c2: C의 listener를 멈추고 `--fetch 1`을 붙여 다시 띄운다 → A가 또 push하면 `git fetch` 없이 `wait_ev C 2`.
5. **계약 변경이 없으면 contract push가 아니다**: r에서 order.c만 바꾼 일반 커밋 뒤 `ait contract push origin` → `err E_EMPTY`, `hint: push it with git`. `--force -m x`를 줘도 같다(`E_EMPTY`를 먼저 본다, 7.10.1-2). `git push origin master`는 된다.
   - **내보낼 미확인이 있으면 git push 앞에 알린다**(5.8 `E_EMPTY`): r에서 `tick; ait offer src/order.c:1-5 p as k5 -r pass; ait contract push origin`. 이어서 order.c 3행을 바꾸는 일반 커밋 → `ait contract push origin` → `err E_EMPTY`. hint는 H7 `check '<K5>' again: run 'ait amend <K5> -m <why>' …`이 `push it with git` 앞에 있다(내보낼 커밋에 K5의 새 미확인이 있고 H에서도 미확인이다, 7.10.1-4). 끝나면 이 항목 전의 커밋으로 되돌린다(`git reset -q --hard <그 커밋>`).
6. **섞여 있으면 통째로 올린다**: r에서 일반 커밋 하나와 계약 커밋 하나(`ait future offer src/order.c p`) 뒤 `ait contract push origin` → `pushed origin master`. 두 커밋이 모두 원격에 있다.
7. **미확인 상태는 막는다**: r에서 `tick; ait offer src/stock.c:1-10 p as k -r pass`(Rk) 뒤 stock.c 3행을 바꾸는 일반 커밋(K는 미확인). `ait contract push origin` → `err E_STATE`, origin은 그대로다. stderr 첫 행은 `error: not allowed: unverified current contract(s) '<K>'`로 시작하고, hint는 H7(`ait amend <K> -m <why>`)과 `or, if it is trivial, take responsibility: 'ait contract push origin --force -m <why>'`다(K의 provider를 찾을 수 있다. REMOTE는 이 push의 remote다, 5.8).
   - `tick; ait contract push origin --force -m 사소함` → `ok`. git push 전에 r의 master에 요청 커밋 Rf가 하나 공개된다(7.10.1-4): 제목 `[CONTRACT] 사소함`, 기록줄 `Ait-Request: <Rf>`·`Ait-Agent: agent-a`·`Ait-Session: <A>`. `rec(K)`의 provider는 지금 stock.c로 다시 고정되고(1.5e), 확인 행은 `check forced <pins(Rf, K)>`이며(PINS가 바뀌었다, 2.2.2), r에서 K는 더 미확인이 아니다.
   - stdout은 `pushed origin master`, 새로 공개된 요청마다 `request` 행(`request <Rk>`와 `request <Rf>`), `check <K> forced` 순서다. origin의 master는 Rf다.
   - c2의 listener가 미확인을 만든 커밋의 `change` 사건(`unverified <K>`)을 받는다. Rf의 사건은 받지 않는다(다시 고정과 확인 행만 바뀌었으므로 R이 비었다, 9.2).
8. **두 번째 push는 noop**: 성공한 `ait contract push origin` 직후 같은 명령을 다시 하면 `noop`, exit 0(내보낼 커밋이 없다).
9. **remote 이름만 받는다**: `ait contract push "$T/origin.git"`(URL) → `err E_USAGE`. detached HEAD에서 `ait contract push` → `err E_BUSY`.
10. **원격에서 지워진 브랜치의 추적 ref는 원격 끝이 아니다**(7.10.1-1·4): 준비를 다시 한다. r에서 `tick; ait offer src/stock.c:1-10 p as k -r pass`(Rk) 뒤 stock.c 3행을 바꾸는 일반 커밋(K는 미확인). `git push -q origin HEAD:refs/heads/tmp`로 올린 뒤(r에 `refs/remotes/origin/tmp`가 생긴다) 원격에서만 지운다(`git --git-dir="$T/origin.git" update-ref -d refs/heads/tmp`). `ait contract push origin` → `err E_STATE`, stderr 첫 행은 `error: not allowed: unverified current contract(s) '<K>'`로 시작하고 origin의 master는 그대로다. r에 남은 `refs/remotes/origin/tmp`가 그 커밋들을 가리켜도 `ls-remote`가 알리지 않으므로 Rk와 미확인을 만든 커밋은 내보낼 커밋에 든다.
11. **원격의 다른 브랜치와 같은 새 브랜치**(7.10.1-1·5): 준비를 다시 한다. r에서 `git switch -q -c nb origin/master`(더한 커밋이 없다) → `ait contract push origin` → `ok`, stdout은 `pushed origin nb` 한 행이고 `request` 행은 없다. 내보낼 커밋이 비었지만 원격의 nb가 H가 아니므로(없다) 5로 가서 올린다. origin에 nb가 생기고 그 OID는 master와 같다.
12. **init 커밋도 계약 커밋이다**(2.1.2a, 7.10.0): 새 빈 bare 원격 `$T/o2.git`과, 코드 커밋 하나만 있는 새 저장소를 만들고 `git remote add o2 "$T/o2.git"; git push -q o2 master` 뒤 `tick; ait init`(init 커밋, `.ait/init`만 더한다). `ait contract push o2` → `pushed o2 master`이고 `err E_EMPTY`가 아니다(`.ait/init`은 `.ait/` 아래 경로이므로 계약 경로이고, init 커밋은 계약 커밋이다). init 커밋은 요청이 아니므로 `request` 행은 없다.

## T35 검토 고리 (10.3)

준비:
- 저자 clone r(A)와 검토자 clone c1(R1, 브랜치 rv1), c2(R2, 브랜치 rv2)를 만들고 모두 `ait init`.
- 검토자는 `ait sub /`. 저자와 검토자 모두 listener를 띄운다(`--fetch` 없이. 받아 오기는 단계마다 `git fetch`로 한다).

1. A: `docs/design.yaml`(`body: v1`), `tests/t_basic.sh`(pass) → `tick; ait future demand new:src/ait.c new:src/ait.c docs/design.yaml,tests/t_basic.sh as d -m "설계 v1"`(Rd) → push.
2. c1, c2: `git fetch` → 각자 1건(`reason sub scope /`, `in-head no`).
3. c1: `tick; ait contract pull <D>` → `included <D> from '<Rd의 커밋>'`. `docs/design.yaml`의 body를 `v1-r1`로 고쳐 `contract add`·`tick; contract commit`(R_r1) → `git push origin rv1`. c2도 같은 방식으로 `v1-r2`(R_r2, rv2).
4. A: fetch → `wait_ev A 2`(`reason future demand <D>`).
5. A: `ait contract pull <D>` → `err E_DIVERGED`, `불변`. 첫 행은 `error: '<D>' has diverged between`으로 시작하고 R_r1·R_r2의 커밋을 `'<OID>' (origin/rv1)`·`'<OID>' (origin/rv2)` 꼴로 쓴다. A의 판은 두 판의 공통 조상이고 두 검토자가 모두 바꿨으므로 A의 판은 3-way로 빠지고 두 판이 남는다(7.12). hint는 두 커밋 중 하나를 고르는 `pick one: 'ait contract pull <D>=<OID> -m <why>' or …`다.
6. A: `tick; ait contract pull <D>=<R_r1의 커밋> -m "r1을 먼저"` → `included <D> from '<R_r1의 커밋>'`, `request <R_p>`.
   - `rec(D)`는 r1의 본문에 prev가 두 행(r1·r2 판, OID 순)인 새 판이다(고른 판은 고르지 않은 판도 포함한다, 7.12).
   - `docs/design.yaml`은 `v1-r1`이다. r2의 명세 변경은 고르지 않은 판의 것이므로 받지 않는다(7.12의 받는 결과). rv2의 커밋은 들이지 않는다(들이는 커밋은 고른 판의 것이다).
7. A가 body를 `v1-r1+r2`로 고쳐 `contract add docs/design.yaml`·`tick; contract commit -m 합침`(R_res) → push.
   - `ait peek <D>`의 마지막 event는 `event <R_res> agent-a respecified docs/design.yaml`다.
8. c1, c2 fetch. c2가 받는 R_p 사건에는 `reason sub scope /`와 `reason overridden <D>`가 있다(c2의 변경이 고르기로 버려졌다, 9.3). c1이 받는 R_res 사건의 hint(H3, c1의 구독 `/`)는 `ait contract pull <D>`를 앞에 붙인 `ait peek <D>`다(5.9a의 들이기).
9. c1, c2: `tick; ait contract pull <D>` → `included <D> from '<R_res의 커밋>'`. R_res의 판이 r1·r2 판을 모두 포함하므로 고르지 않는다. 세 clone의 D와 `docs/design.yaml`이 같아진다.
10. A가 `tick; ait future cancel D -m 취소`를 push → c1, c2가 fetch하면 받는다(`reason sub scope /`). cancel은 만든 쪽이 자기 계약을 거두는 것이다(10.1a).

## T36 elsewhere (7.9.6)

준비: wa(a)와 wb(b). wa에서 `tick; ait future demand src/order.c src/stock.c p as d -m e1`(C1, R1).

1. wb에서 `ait peek src/stock.c` → 레코드는 0건이고, 다음 블록이 정확히 나온다.
   ```
   elsewhere <R1>
   ref refs/heads/a
   agent agent-a
   subject [CONTRACT] e1
   contract <D> created-future

   ```
   - 들일 방법은 hint가 준다(7.9.6): stderr의 hint(H11)에 `ait contract pull <D>`와 `ait future offer src/stock.c specs/p.yaml as o undertake <D> o -m <why>`가 있다(D를 떠맡은 offer가 어디에도 없고, D가 HEAD에 없으므로 들이기를 앞에 붙인다, 5.9a).
2. wb에서 `ait peek <D>` → 같은 블록이 나온다(`E_ID`가 아니다).
3. wa가 cancel(R2)한 뒤 wb에서 peek → R1과 R2가 모두 보인다.
4. wb에서 `contract pull <D>` 후에는 elsewhere가 없다(R1·R2의 판이 HEAD 판에 포함된다, 7.9.6).
5. **HEAD에 없는 계약을 다루면 들이는 명령을 준다**(5.8 `E_ID`, 5.9a의 들이기): 준비를 다시 하고 wb에서 B(agent-b)를 연다. wb에서 `ait future offer src/stock.c p as o undertake <D> o -m x` → `err E_ID`, `불변`. hint에 `ait contract pull <D>`와 `then rerun this command`가 있다.
   - 준비를 다시 하되 wa에서 R1 전에 order.c를 바꾸는 일반 커밋을 두어도 hint는 같다. 그 일반 커밋은 D의 판이 처음 들어간 커밋의 조상이므로 함께 온다(규칙 8).

## T37 git만 써도 알림, hook의 무해함 (7.1, 5.1)

준비: T30과 같다.

1. **ait 없이 해결 커밋**:
   - wa에서 `tick; ait future offer src/stock.c p as o`.
   - a1과 a2(a에서 분기)가 O의 provider를 다르게 amend한다(`src/stock.c:1-5`, `src/stock.c:6-9`).
   - wa에서 `git merge a1` 다음 `tick; git merge a2` → 충돌. `git checkout --theirs .ait/<O>; git add -A; tick; git commit`.
   - B가 새 RID 사건을 받는다(`in-head no`). 해결 커밋의 O 판은 a1·a2의 판을 prev에 담은 새 판이고, a1의 판과 뜻이 다르다(9.2-1).
2. **그냥 fetch**: 다른 clone이 push한 요청을 wb에서 `git fetch`만 해도 B가 받는다.
3. **이중 전달 없음**: A의 ait 쓰기 한 번에 `ev(B)`는 정확히 1 는다.
4. **ait 없는 PATH**(`mkdir $T/bin; ln -s "$(command -v git)" $T/bin/git` 후 `PATH=$T/bin:/bin`. 같은 git을 쓰되 ait만 없다):
   - `git commit`, `git checkout -b`, `git merge`가 모두 성공한다(hook 통과). 양쪽이 떨어진 행(예: 한쪽은 state 행, 다른 쪽은 creator 행)을 손으로 고친 `.ait` 레코드의 merge도 git 기본 병합으로 진행된다(driver의 대체 경로).
   - `.ait` 수정도 커밋된다. ait가 없는 곳의 커밋은 hook이 통과시키는 우회이며(규칙 9, 11.1), 이력에 `?`로 남는다.
5. **무효 session**: `AIT_SESSION=S000000000000`인 셸에서도 git 명령은 모두 성공한다(hook은 무효인 session을 없는 것으로 본다, 5.1).
6. **남은 REBASE_HEAD**: wa에서 rebase를 시작해 코드 파일(src/order.c) 충돌로 멈춘 뒤 `git rebase --quit; git switch -C a; git reset -q --hard`(충돌 항목을 지운다. `git rev-parse --git-path REBASE_HEAD`는 남고 `rebase-merge`는 없다). 이어서 `.ait/<O>`를 고친 일반 `git commit` → `E_GUARD`(hook이 꺼지지 않는다. 진행 중인 rebase는 5.2-2대로 `rebase-merge`·`rebase-apply` 디렉터리로 보므로 남은 REBASE_HEAD만으로는 통합 상태가 아니다). `git reset -q -- .ait; git checkout -q HEAD -- .ait`로 복원한 뒤 `ait future offer src/stock.c p` → `ok`(E_BUSY가 아니다).

## T38 경로와 cwd (5.12)

1. 하위 디렉터리 `src/`에서 `ait future offer src/stock.c p` → 루트에서 실행한 것과 같다.
2. 하위 디렉터리에서 `.ait`가 dirty → `err E_DIRTY`.
3. `src/재고.c`(비ASCII 이름)에 범위를 건 current 계약 K가 있을 때, 그 파일을 바꿔 stage하고 같은 파일의 future offer를 `ait fulfill`하면 보고에 `touch <K>`가 있다. 그 변경을 일반 커밋하면 `ait peek now <K>`의 provider가 `src/재고.c ?changed`다.
4. literal 경로: 명세 파일 `specs/a*b.yaml`에 대해 `ait contract add 'specs/a*b.yaml'`는 그 파일만 다룬다. `specs/axb.yaml`는 영향받지 않는다.
5. **사용자 환경이 범위 계산과 기록줄 읽기를 바꾸지 못한다**: `GIT_DIFF_OPTS=--unified=10`과 `git config trailer.separators %`를 둔 셸에서 T16의 단계 1~2와 T15를 반복해도 결과가 같다.

## T39 확인 요청에 실리는 exec 정의와 환경 (1.2, 2.5, 8.2)

준비: `specs/environments/e.yaml`을 둔다.

```yaml
cwd: sub
env:
  inherit: [PATH, NEEDED]
  set: {MODE: test}
  required: [NEEDED]
prepare:
  - [sh, -c, "echo ready > prepared"]
cleanup:
  - [rm, -f, prepared]
```

`sub/conf.txt`를 만들어 일반 커밋으로 넣고(프로젝트 파일이다), exec 정의 `specs/envt.yaml`(`body: envt`, `engine: exec`, `command: [sh, -c, "test -f prepared && test -f conf.txt"]`, `environment: e`)을 둔다.

1. `ait offer src/stock.c specs/envt.yaml` → `err E_NEEDS_RESULTS`, `불변`.
   - Q의 과제 `+1:specs/envt.yaml`에 `command`, `success: [0]`, 그리고 `environment` 아래에 `cwd: sub`, `env`(inherit·set·required), `prepare`, `cleanup`이 e.yaml 그대로 실려 있다.
   - ait는 prepare를 실행하지 않는다: 어디에도 `prepared` 파일이 생기지 않는다.
2. `ait offer src/stock.c specs/envt.yaml -r pass` → `ok`, `check <O> reported`. 계약의 `file` 행은 `specs/envt.yaml … def`와 `specs/environments/e.yaml … input` 두 행이다. `sub/conf.txt`는 명세 파일이 아니다.
3. 형식은 언제나 검사한다: e.yaml에 모르는 키 `foo: 1`을 더한 환경으로 등록하면 → `err E_FORMAT`. prompt 정의에 `command`가 있으면 → `err E_FORMAT`(2.5.6). `engine: judge` → `err E_FORMAT`(2.5.4).
4. `sub/conf.txt`를 일반 커밋으로 고치면 → 성공(프로젝트 파일이다). `specs/environments/e.yaml`을 일반 커밋으로 고치면 → `E_GUARD`(환경 정의는 명세 파일이다).
5. 고정되지 않는 파일은 경고한다: exec 정의 `specs/u.yaml`(`body: u`, `engine: exec`, `command: [sh, tests/u.sh]`, inputs 없음)과 `tests/u.sh`를 두고 `tick; ait future offer src/stock.c specs/u.yaml` → `ok`, stderr에 `warning: 'tests/u.sh' is not pinned`와, hint에 `add it to 'inputs'`가 있다. 계약의 `file` 행은 `specs/u.yaml` 하나다.
6. **인자는 환경의 cwd에서 읽고 `.`·`./`를 정리해 비교한다**(1.2):
   - 0.2의 줄임 정의(`command: [./tests/X.sh]`, `inputs: [tests/X.sh]`)로 `tick; ait future offer src/stock.c tests/pass.sh` → `ok`, `is not pinned` 경고가 없다(`./tests/pass.sh`를 정리하면 고정된 `tests/pass.sh`다).
   - `sub/run.sh`를 일반 커밋으로 넣고, exec 정의 `specs/cw.yaml`(`body: cw`, `engine: exec`, `command: [sh, ./run.sh]`, `environment: e`)로 `tick; ait future offer src/stock.c specs/cw.yaml` → `ok`, stderr에 `warning: 'sub/run.sh' is not pinned`와 hint `if it is test material, add it to 'inputs' in 'specs/cw.yaml'`이 있다(`./run.sh`는 e의 cwd `sub` 기준이다). `specs/cw.yaml`에 `inputs: [sub/run.sh]`를 더해 같은 등록을 하면 경고가 없다.

## T40 ait listen (7.2.5, 9.5)

준비: T30처럼 wa(A)와 wb(B)를 두되 B의 listener는 띄우지 않는다.

1. wb에서 `ait listen src/order.c > $T/out 2> $T/err &`(B). `ses(B)`에 `sub scope src/order.c`가 더해지고, `ait session list`의 B는 `listening yes`다. `sub scope src/order.c` 행은 `$T/err`에 있고 `$T/out`에는 없다(stdout은 사건만, 7.2.5).
2. wa에서 A가 `tick; ait future demand src/order.c src/stock.c p as d`(R1) → 2초 안에 `$T/out`에 `ait-event 1`로 시작하고 `request <R1>`을 담은 payload와 `ait-event-end` 한 행이 생긴다.
3. 같은 사건은 다시 나오지 않는다. A가 무관한 파일만 바꾼 요청을 만들면 `$T/out`은 늘지 않는다.
4. listener를 멈춘 뒤 생긴 사건은 다음 `ait listen`에서 나온다(tip이 남아 있다).
5. `--fetch 1`: 다른 clone이 push한 요청이 `git fetch` 없이도 2초 안에 나온다.
6. SIGTERM → exit 0.
7. B의 listener를 `--fetch 1`로 다시 띄운다. 원격에 닿지 못하게 하면(remote URL을 없는 경로로 바꿈) stderr에 `warning:`을 쓰고 listener는 계속 돈다. 같은 clone 안의 사건은 계속 나온다.
8. 듣고 있는 동안 `ait session close <B>` → `err E_BUSY`, hint는 listener를 먼저 멈추라는 것이다. listener를 멈춘 뒤에는 닫힌다.

## T41 기본 session (5.1)

1. wa에서 `ait session open --agent agent-a`(A) 후, 새 셸(환경변수 없음)에서 `ait future offer src/stock.c p` → `ok`, creator `agent-a <A>`.
2. 같은 worktree에서 `ait session open --agent agent-c`(C) → 기본 session은 A 그대로다(남의 이름으로 기록되지 않게). C로 일하려면 `AIT_SESSION=$C`를 준다. `ait session list`에서 A만 `default yes`다.
3. `ait session close $C` → 기본 session은 A 그대로다. `ait session close $A` → 기본 session이 없어진다. `AIT_SESSION` 없이 쓰기 → `err E_SESSION`.
4. wb의 기본 session은 wa와 따로다.
5. **재시작**(7.2.2, 5.8 `E_SESSION`): 준비를 다시 한다(wa와 wb가 있고 session은 없다). wa에서 `A=$(ait session open --agent agent-a)`, wb에서 `B=$(ait session open --agent agent-b)`. 에이전트 b가 재시작해 wa에서 `ait session list`를 보면 B 블록에 `agent agent-b`, `worktree <realpath $T/wb>`, `default yes`가 있다(`default`는 그 session이 자기 worktree의 기본 session인지다. 명령을 실행한 worktree와 상관없다). wa에서 `AIT_SESSION=$B ait peek` → `err E_SESSION`, hint에 `cd '<realpath $T/wb>'`가 있다(그 session의 worktree). `cd $T/wb; ait peek` → `ok`, 머리말의 둘째 행이 `session <B>`다(wb의 기본 session).

## T42 미확인: current 계약의 코드를 바꾸고, 옮기고, 지우기 (1.5a, 3.8, 7.3.2 amend, 9.1)

준비: T30처럼 wa(A)와 wb(B)를 둔다(B는 `src/stock.c`를 구독한다). wa에서 A가 `tick; ait offer src/stock.c:1-10 tests/nobroken.sh as k -m k -r pass`(R1)를 하고 `ait sub <K>`로 K를 구독한다(current offer를 만든 쪽은 구독해야 미확인을 받는다, 9.3). `settle`.

1. **옮겨도 막히지 않고 드러난다**: wa에서 stock.c 1~10행을 지우고 같은 내용을 새 파일 `src/stock2.c`에 넣는다. `tick; git add -A; git commit -qm refactor`(C) → 성공. stderr에 K를 담은 경고가 있다(7.1.2).
   - `ait peek now <K>` → `provider src/stock.c:1-10 ?deleted`.
   - `settle` 후 B가 사건 하나를 더 받는다. 처음 8행은 다음과 같고, 그 뒤에 C의 `git show`가 온다.
     ```
     ait-event 1
     change <C>
     recipient <B>
     ref refs/heads/a
     reason sub scope src/stock.c
     in-head no
     unverified <K>

     ```
   - B의 사건에는 hint가 없다(B의 HEAD에 K가 없으므로 H7·H8의 조건이 맞지 않는다). A도 받는다(`reason sub id <K>`, `in-head yes`). A의 사건 hint(H8)에 `'<K>' has no code under its scope (unverified since '<C>')`, `ait amend <K> <new scope> - -m <why>`, `ait future amend <K> -m <why>`가 있고, K가 A의 계약이므로 `ait future drop <K> as x -m <why>`와 `with nothing staged`도 있다. 변경 사건은 발신 session이 없으므로(9.5) 커밋을 만든 A 자신도 받는다.
2. 쓰기는 막히지 않는다: `tick; ait future demand src/order.c src/stock.c p` → `ok`(D2. K는 HEAD에서 이미 provider를 찾을 수 없으므로 이 요청에 걸리지 않는다, 1.5b). 공유는 미확인 상태에서 멈춘다. 아래 순서대로 한다.
   - `ait contract push origin` → `err E_STATE`(K가 미확인이다). hint는 H8(`'<K>' has no code under its scope (unverified since '<C>')`, `ait future amend <K> -m <why>`, `ait future drop <K> as x -m <why>`)이고, `ait contract push origin --force` 행은 없다(K의 provider를 찾을 수 없다, 5.8).
   - `ait contract push origin --force -m x` → `err E_CODE_REQUIRED`, `불변`, hint는 H8이다. 확인 커밋을 만들지 않고 올리지도 않는다(K의 코드를 찾을 수 없어 사소하다고 확인할 대상이 없다, 7.10.1-4, 5.14).
   - `git push origin a`로는 올라간다. 그 뒤 `ait contract push origin` → `noop`(내보낼 커밋이 없고 원격의 a가 H다, 7.10.1-1).
2a. **확인할 코드가 없으면 관문도 결과를 보지 않는다**(6.5, 5.13-7): wa(A)에서 `ait amend K -m x`(빈 amend) → `err E_CODE_REQUIRED`, `불변`. stdout에 `task`·`check-request`·`check-result` 행이 없고 확인 요청 파일과 결과 틀(`<git-dir>/ait/check-request.yaml`·`check-result.yaml`)은 명령 전과 같다(확인 결과 단계 전에 멈추고, 확인할 수 없으므로 쓰지 않는다, 6.5, 8.4). `tick; ait amend K -m x --force` → 같은 `err E_CODE_REQUIRED`, `불변`(`--force`는 결과만 넘긴다, 5.14). hint(5.8 `E_CODE_REQUIRED`의 관문 형)는 같은 명령을 고친 것이다: 범위 옮기기 `ait amend <K> <new scope> - -m <why>`(남은 조각이 없으므로 KEPT가 없다)와 되돌리기 `ait future amend <K> -m <why>`(5.9a의 고치는 명령). 작업 폴더에 그 코드가 없으므로 코드 먼저 형은 없다. 철회 hint도 없다(철회는 이행이 필요하므로 관문의 요청 안에서 할 수 없다. 되돌린 뒤 H10이 cancel을 준다, 5.8).
3. `ait todo src/stock.c` → K 블록(`state current`, `provider src/stock.c:1-10 ?deleted`).
4. **처음 한 번만 알린다**: 2 뒤의 B의 사건 수를 n이라 하자. wa에서 stock.c 20행을 바꾸는 일반 커밋을 두 번 더 한다. `settle` 후 B의 사건 수는 n 그대로다(K는 이미 미확인이다).
5. **계약을 옮긴다**: `tick; ait amend K src/stock2.c:1-10 - -m "stock2로 옮김"`(offer의 인자는 `B FILES`, 명세 파일은 `-`로 그대로) → `err E_NEEDS_RESULTS`, 과제 `<K>:specs/nobroken.yaml`. `-r pass`로(R5) → `changed <K>`, `check <K> reported`.
   - `ait peek now <K>` → `provider src/stock2.c:1-10`(`?` 없음). `ait peek <K>`의 마지막 event는 `event <R5> agent-a amended check reported`다.
   - B가 R5 사건을 받는다(9.2에서 비교한 `prev` 판의 provider가 `src/stock.c`와 겹친다, 9.3). 그 사건의 hint(H3, 내 구독인 K의 범위가 바뀌었다)는 `ait contract pull <D2> <K>`를 앞에 붙인 `ait peek <K>`다(5.9a의 들이기. K의 판이 처음 들어간 R5와 그 조상(R1, C, 2의 요청, 4의 커밋)이 함께 오고, 2의 요청이 만든 D2도 들어오므로 지정 검사가 D2를 더한다, 7.12.3. 이름은 계약 ID 순이다).
6. **제자리 수정과 다시 확인**: stock2.c 3행을 바꾸는 일반 커밋(C6) → `ait peek now <K>` → `provider src/stock2.c:1-10 ?changed`. A가 `change <C6>` 사건을 받는다(`reason sub id <K>`, `unverified <K>`).
   - 3행을 원래대로 되돌리는 일반 커밋 → `?`가 사라지고, 새 사건은 없다(되돌림은 새 미확인이 아니다).
   - 다시 3행을 바꾸는 일반 커밋(C6') → A가 `change <C6'>`를 받는다(다시 새 미확인이 되었다).
   - 옮기지 않고 다시 확인한다: `ait amend K src/stock2.c:1-10 -` → `err E_NEEDS_RESULTS`. `fill <W> pass <K>:specs/nobroken.yaml=fail`로 내면 → `err E_CHECK_FAILED`, `불변`. K는 미확인으로 남는다. 범위를 준 amend는 빈 amend가 아니라 K의 고정을 HEAD로 바꾸는 요청이고, 관문은 HEAD의 코드를 확인한다. 그래서 stderr의 hint는 도우미의 코드 먼저 `commit code that meets it first: stage <your code> and run 'ait commit -m <why>', then rerun this command`다(`src/stock2.c`는 코드다, 5.8, 5.9a). K는 HEAD에서 current이므로 요청 안의 되돌리기 `ait future amend <K> src/stock2.c:1-10 - -m <why>`(같은 명령의 `amend` 절을 `future amend`로 바꾼 것)도 있다. 조건을 바꾸는 `<new defs>` 형은 없다(관문의 형이 아니다, 5.8).
   - `tick; ait amend K src/stock2.c:1-10 - -r pass`(R6) → `changed <K>`, `check <K> reported`. `?`가 사라지고, 마지막 event는 `event <R6> agent-a verified check reported`다.
7. **기록줄 있는 커밋에서 확인하면 변경 사건이 없다**: `tick; ait future offer src/stock2.c tests/pass.sh as o7`. stock2.c 5행을 바꿔 stage하고 `tick; ait fulfill O7 -r pass` → 보고에 `touch <K>`가 있었고, `check <K> reported`. 이 커밋은 요청 사건 하나만 만든다(`change` 사건이 없다). K는 다시 고정되어 `?`가 없다.
8. **merge로 들어온 변경**: 준비를 다시 한다. R1 전의 커밋에서 갈라진 브랜치 bb가 stock.c 3행을 바꾸는 커밋을 한다(bb에는 K가 없으므로 사건이 아니다). wa에서 `tick; git merge --no-edit bb`(M) → 충돌 없이 끝난다. K는 `?changed`이고, A가 `change <M>` 사건을 받는다(`unverified <K>`). M의 stock.c가 bb 쪽과 같아도 같다(9.1의 새 미확인).
9. **amend를 거듭해도 한 번**: 8과 따로, 6까지 한 저장소에서(K는 확인된 상태) stock2.c 4행을 바꾸는 일반 커밋 C9를 하고, 이어서 `git commit --amend`로 메시지만 두 번 고친다. A가 받는 `change` 사건은 하나다(세 커밋의 미확인 사건 키 `unverified <K> <판> <provider 코드 내용>`이 같다, 9.1, 9.5). amend로 4행을 다시 다르게 바꾸면 provider 코드 내용이 달라 새 사건이다.
10. **이력에 누가 깨뜨렸는지 남는다**: 9 뒤 `ait peek <K>`의 마지막 event는 `event - ? unverified <OID>`다. OID는 HEAD에 남은(마지막으로 고친) 커밋이다.
11. **rebase된 요청 사본도 알린다**: 9의 변경(4행)을 되돌리는 일반 커밋을 한다(K는 다시 확인된 고정과 같다). 이어서 stock2.c 6행을 바꿔 stage하고 `tick; ait commit -r pass`로 요청 R11을 만들고 `settle`한다(다시 고정과 확인 행만 바뀌었으므로 R이 비고 받는 쪽은 없다). R11의 부모에서 갈라진 브랜치에 stock2.c 9행을 바꾼 일반 커밋 Co를 두고, wa에서 `git rebase Co`로 R11을 그 위로 옮긴다(6행과 9행은 떨어져 있어 충돌 없이 끝난다). 사본 R11'는 RID가 같지만 그 커밋에서 K가 새 미확인이다(Co의 9행이 R11의 고정과 다르다). A가 R11'의 사건을 받는다(`unverified <K>`. 새 키다). R11'는 R11의 기록줄을 가진 요청 커밋이므로 둘째 행은 `request <R11>`이다.
12. **옮기지 않고 철회한다**: 준비를 다시 하고 1의 커밋까지 한다. `tick; ait future drop K as x` → `ok`. 이어서 `tick; ait fulfill X` → `fulfilled <X>`, `withdrawn <K>`. 걸린 계약이 없어 과제가 없으므로 `-r` 없이 된다.
13. **current offer를 만든 쪽은 구독이 없으면 미확인을 받지 않는다**: 준비를 다시 하고 5까지 한다(K는 stock2.c에서 확인된 상태). A가 `ait unsub` → stdout `unsub id <K>`. 이어서 wa에서 stock2.c 3행을 바꾸는 일반 커밋 → A는 받지 않는다(9.3).
14. **기록줄이 모자란 커밋은 요청이 아니라 변경이다**(5.4, 9.1): 준비를 다시 하고 5까지 한다. stock2.c 3행을 바꾸고 `tick; git commit -qam x -m "Ait-Request: R000000000001"`(C14. `Ait-Agent`·`Ait-Session`이 없다) → `settle` 후 A가 받는 사건의 둘째 행은 `change <C14>`이고 `unverified <K>`가 있다(`request` 사건이 아니다).
15. **들여온 미확인도 알린다**(7.1.5, 7.12.5): 준비를 다시 한다. wb(B): `tick; ait contract pull K` → `included <K> from '<R1의 커밋>'`. 이어서 wa에서 1의 커밋(C)을 한다.
   - wb(B): `ait contract pull K` → `already <K>`. C는 K의 판을 바꾸지 않았으므로 들일 것이 없다(pull은 판이 처음 들어간 커밋까지만 받는다, 규칙 8).
   - wa(A): `tick; ait future offer src/order.c p as o15`(R15). wb(B): `tick; ait contract pull O15` → `included <O15> from '<R15의 커밋>'`(fast-forward). C도 R15의 조상이므로 함께 온다. stderr에 `warning: '<K>' comes in unverified`와 H8 hint(`'<K>' has no code under its scope (unverified since '<C>')`, `ait amend <K> <new scope> - -m <why>`, `ait future amend <K> -m <why>`)가 있다. 들이는 커밋 R15에서 K는 같은 고정으로 이미 미확인이므로 pull은 멈추지 않는다. 새 HEAD에서 미확인이고 옛 HEAD에서는 아니던, B의 구독 `src/stock.c`에 맞는 계약이므로 경고한다(7.12.5). K는 B의 계약이 아니므로 `ait future drop`은 없다(5.9a).
   - 준비를 다시 하고 B가 R1을 들인 뒤 1의 커밋까지 한다. wb에서 order.c를 바꾸는 일반 커밋을 두고 `tick; git merge --no-edit a` → 충돌 없이 끝난다. commit-msg가 stderr에 `warning: '<K>' comes in unverified`와 H8 hint(`ait amend <K> <new scope> - -m <why>`, `ait future amend <K> -m <why>`)를 쓴다(7.1.5). `is unverified after this merge` 경고는 없다(MERGE_HEAD에서 K는 같은 고정으로 이미 미확인이다, 9.1).
16. **철회 예약을 건 쪽도 받는다**(9.3 `reason future drop`, H3): 준비를 다시 한다. wb(B): `tick; ait contract pull K` → `included <K> from '<R1의 커밋>'`. `tick; ait future drop K as x -m "철회 예정"`(X는 B의 약속이고 그 상대는 K다, 1.3). wa(A): `tick; ait amend K src/stock.c:1-5 - -m 좁힘 -r pass`(R16) → `changed <K>`, `check <K> reported`. `settle` 후 B가 받는 R16 사건에 `reason future drop <X>`와 `contract <K> amended`로 시작하는 행이 있고, hint(H3)에 `ait contract pull <K>`와 `ait peek <K>`가 있다. `ait todo <X>`는 없다(X는 K를 겨눈 drop이다. `ait todo M`은 M이 K 자신이거나 K를 떠맡은 offer일 때만 쓴다. 새 범위가 B의 HEAD에 없으므로 들이기를 앞에 붙인다, 5.9a).

## T43 ait commit과 커밋 안내 (1.5b, 7.1.2, 7.7.3-4, 7.7a)

준비: `tick; ait offer src/stock.c:1-10 tests/nobroken.sh as k -r pass`. `tick; ait future offer src/order.c p as o`.

1. **걸린 계약이 없으면 그냥 커밋**: order.c를 바꿔 stage → `tick; ait commit -m "주문 정리"` → `ok`, 출력 없음. 새 커밋의 제목은 `주문 정리`이고 기록줄이 없다. HEAD tree는 index와 같다.
2. 바뀐 것이 없으면 `ait commit -m x` → `noop`, `불변`.
3. **걸린 계약이 있으면 함께 확인**: stock.c 5행을 바꿔 stage → `ait commit -m "재고 정리"` → `err E_NEEDS_RESULTS`, `불변`. 보고는 차례로 `provider <K> src/stock.c:1-10 ?changed`, `touch <K>`, 빈 행(7.7.6의 틀), `task <K>:specs/nobroken.yaml exec`, `check-request …`, `check-result …`, `ready yes`이고 `id` 행이 없다.
   - `tick; ait commit -m "재고 정리" -r pass` → `request <R>`, `check <K> reported`.
   - 커밋에는 stock.c와 `.ait/<K>`가 함께 바뀌어 있다. 제목은 `재고 정리`(접두어 없음)이고, 결과와 기록줄이 있다. `rec(K)`의 확인 행은 `check reported <pins(HEAD, K)>`다(PINS가 바뀌었다).
   - K는 다시 고정되어 `?`가 없다. `ait peek <K>`의 마지막 event는 `event <R> agent-a verified check reported`다.
4. **하찮으면 `--force`**: stock.c 6행을 바꿔 stage → `tick; ait commit -m 오타 --force` → `check <K> forced`. 결과 문단은 없고 K는 다시 고정된다. `ait peek <K>`의 마지막 event는 `… verified check forced`다.
5. 명세 파일을 stage하면 `ait commit` → `err E_MIXED`(계약 변경은 `contract commit`으로). 보고에 `mixed <PATH>`와 `ready no`가 있다. 끝나면 되돌린다.
6. **`git commit`의 안내**: stock.c 7행을 바꿔 stage하고 `tick; git commit -qm raw` → 성공. stderr는 다음과 같다. `warning:` 행과 계약 행은 정확히, hint 행은 명령과 ID를 맞춘다(7.1.2, 0.2. K의 정의는 `body: nobroken`, `command: [./tests/nobroken.sh]`).
   ```
   warning: this commit leaves 1 current contract(s) unverified
       <K> offer src/stock.c:1-10 (agent-a)
           nobroken
           $ ./tests/nobroken.sh
   warning: their receivers are notified after this commit
   hint: next time, to check and commit in one step, use 'ait commit' instead of 'git commit'
   hint: to change '<K>' without a check, reopen it: 'ait future amend <K> -m <why>'
   hint: check '<K>' again: run 'ait amend <K> -m <why>' to get the tasks
   ```
   - `<K>` 자리에는 K의 ID가 나오고, `<why>`는 글자 그대로다. PROVIDER는 HEAD에서의 표시다.
   - future O의 provider(order.c)만 바꾼 커밋에는 안내가 없다. future는 걸리지 않는다.
7. **consumer는 걸리지 않는다**: `tick; ait demand src/order.c:1-5 src/stock.c:40-50 p as d -r pass`. order.c 3행만 바꾼 일반 커밋 → 안내가 없고, `ait peek now <D>`의 consumer에 `?`가 없으며, D는 미확인이 아니다.
8. **코드를 옮기는 `ait commit`은 계약도 옮겨야 커밋된다**(1.5a, 1.5b, 7.7.3-4): 준비를 다시 한다(K가 확인된 상태). stock.c 1~10행을 지우고 같은 내용을 `src/stock2.c`에 넣어 stage → `ait commit -m 이동` → `err E_CODE_REQUIRED`, `불변`. 걸린 K를 확인할 코드가 이 커밋에 없으므로 커밋하지 않는다(커밋하면 이 커밋이 사고를 만든다).
   - stderr 첫 행은 `error: no code under the contract's scope:`로 시작한다. 보고에 `provider <K> src/stock.c:1-10 ?deleted`, `code-required <K>`, `touch <K>`, `ready no`가 있고 `id` 행과 `unverified` 행, 과제 행(`task`·`check-request`·`check-result`)은 없다(확인할 수 없다, 7.7.6).
   - hint(5.8)에 `rerun with '<K>=<new scope>'`(남은 조각이 없으므로 KEPT가 없다)와 `ait future amend <K> -m <why>`가 있고, K는 이 session의 계약이므로 철회 hint `ait future drop <K> as x -m <why>`와 `then 'ait fulfill <X> -m <why>'`(commit을 fulfill로 바꿔 `<X>`를 더한 것, 5.9a)도 있다. `--force`로도 같다(5.14).
   - 같은 변경을 계약과 함께 옮기면 미확인이 없다: `tick; ait commit K=src/stock2.c:1-10 -m 이동 -r pass` → `request <R>`, `check <K> reported`. K의 provider는 `src/stock2.c:1-10 <blob(HEAD:src/stock2.c)>`이고 `ait peek now <K>`에 `unverified` 행이 없다(7.7a.1). 여러 커밋에 걸쳐 옮길 때는 T45처럼 되돌린다.
   - **되돌리면 다시 실행할 명령에서 그 계약의 범위를 뺀다**(5.9a): 같은 변경에서 `ait commit K=src/stock2.c:1-10 -m 이동`으로 W를 받아 `fill <W> fail` 후 `-r <W>` → `err E_CHECK_FAILED`, `불변`. 되돌리기 hint는 `to change '<K>' without a check, reopen it: 'ait future amend <K> -m <why>', then rerun 'ait commit -m <why>'`다(되돌린 K는 걸린 계약이 아니므로 `K=`를 두면 `E_SCOPE`다, 7.7.1).
9. **session 없이**: 준비를 다시 한다. `AIT_SESSION=-`에서 order.c만 바꾼 `ait commit -m x` → `ok`(확인할 것이 없으면 session을 보지 않는다). stock.c 2행을 바꾼 `AIT_SESSION=- ait commit -m y` → `err E_SESSION`.
10. **이행과 함께 옮기면 구할 수 있다**: 준비를 다시 한다. stock.c 1~10행을 `src/stock2.c`로 옮기고 order.c도 바꿔 stage → `tick; ait fulfill O K=src/stock2.c:1-10 -r pass` → `check <K> reported`이고 K의 provider는 `src/stock2.c:1-10 <blob>`다. `ait fulfill K=src/stock2.c:1-10`(선택한 대상 없음) → `err E_USAGE`.
11. **두 diff의 정렬이 달라도 고정을 기준으로 잡는다**(1.5b, 3.10, 3.11): 준비를 다시 한 뒤 `src/f.c`를 8행 `c`, `a`, `b`, `b`, `c`, `c`, `a`, `b`(한 행에 하나)로 만들어 일반 커밋하고 `tick; ait offer src/f.c:4-4 p as f -r pass`. 2행을 `b`로 바꾸는 일반 커밋 → 경고가 없다(고정 판→HEAD의 diff0은 2행에만 닿는다). 이어서 1행 뒤에 `a`를 삽입해 stage한다(파일은 `c a b b b c c a b`).
   - `ait commit -m x` → `err E_NEEDS_RESULTS`, `불변`. 보고에 `touch <F>`와 `task <F>:specs/p.yaml prompt`가 있다. HEAD→T0의 diff0(1행 뒤 삽입)은 F의 4행에 닿지 않지만(`touches` 거짓), 고정 판→T0의 diff0은 늘어난 `b`를 4행 뒤에 두어 F의 범위에 닿는다(HEAD에서 `changed_since` 거짓, T0에서 참). 그래서 F는 이 변경에 걸린다.
   - `tick; ait commit -m x -r pass` → `request <R>`, `check <F> reported`. `ait peek now <F>`에 `unverified` 행이 없다(이 커밋이 미확인을 만들지 않는다).

## T44 추상 계약과 범위 목록 (1.5c, 3.2a, 3.4, 7.7.1)

준비: `src/runner.c`, `src/cli.c`(각 20행)와 `README.md`를 일반 커밋으로 넣는다. exec 정의 `specs/test-a.yaml`(`body: test-a`, `engine: exec`, `command: [./tests/pass.sh]`, `inputs: [tests/pass.sh]`).

1. `tick; ait future offer / specs/test-a.yaml as o -m "코드베이스가 테스트 A를 통과"` → `ok`. `rec(O)`의 provider는 `/ <HEAD^의 root tree OID>`(등록 직전 HEAD의 tree)이고, `ait peek O`는 `provider /`.
2. 추상은 future에만: `ait offer / specs/test-a.yaml` → `err E_SCOPE`. `ait future offer src/ p` → `ok`(provider `src/ <tree>`). `ait future demand src/order.c / p` → `ok`. `ait future demand / src/stock.c p` → `err E_SCOPE`(consumer는 추상이 될 수 없다).
3. 추상 계약은 좁혀야 current가 된다: runner.c 5행을 바꾸고 cli.c 3행을 바꾸는 일반 커밋을 한다. stage 없이 `ait fulfill O` → `err E_SCOPE`. stderr의 hint에 `ait fulfill <O>=<scope> -m <why>`가 있다(좁히지 않으면 current가 될 수 없다).
4. 좁혀서 이행한다: `tick; ait fulfill O=src/runner.c,src/cli.c:1-10 -r pass` → `fulfilled <O>`. `rec(O)`의 provider는 두 행, `src/cli.c:1-10 <blob>`과 `src/runner.c <blob>`(PATH 순)이다. `ait peek now src/cli.c:5-5`는 O를 보이고, `src/cli.c:15-15`는 보이지 않는다.
5. 범위 밖으로 좁히면 거절: 1처럼 `future offer src/ p as o2`를 만들고 runner.c를 바꿔 stage → `ait fulfill O2=README.md` → `err E_SCOPE`(`src/` 아래가 아니다). 확인 집합에 없는 계약에 `=`를 주면 → `err E_SCOPE`.
6. 범위 목록의 판정: 4 뒤 cli.c 15행만 바꾼 일반 커밋 → O에 걸리지 않는다(경고 없음, `?` 없음). runner.c를 바꾼 일반 커밋 → 걸리고 `ait peek now <O>`의 `provider src/runner.c ?changed` 행에만 `?`가 붙는다.
7. 범위 목록 문법: `ait future offer src/cli.c,src/cli.c p` → `err E_SCOPE`(있는 파일이지만 같은 범위를 두 번 주었다, 3.2a). `ait future demand src/order.c,src/cli.c src/stock.c p` → `rec(D)`에 consumer 두 행 `consumer src/cli.c <blob>`, `consumer src/order.c <blob>`(PATH 순)이 있다.
8. **추상 범위는 추상 구독에만 알린다**: 준비를 다시 하고 1만 한다(O는 아직 추상이다). agent 이름이 서로 다르고 O의 creator와도 다른 session B와 C를 열고 각자 listener를 띄운다. B가 `ait sub src/runner.c`, C가 `ait sub /`로 구독한 상태에서 `tick; ait future amend O - p`(future의 명세 파일 교체, 결과가 필요 없다)를 하면 C만 받는다.

## T45 되돌리기: 확정된 코드를 여러 커밋에 걸쳐 바꾸기 (1.5d, 10.4, 9.1)

준비: T30처럼 wa(A)와 wb(B). A가 `tick; ait future demand src/order.c src/stock.c specs/p.yaml as d`. B가 pull 뒤 `tick; ait future offer src/stock.c tests/pass.sh as o undertake D o`, stock.c를 바꿔 stage하고 `tick; ait fulfill O -r pass`(D도 current). A가 pull. `settle`.

1. **되돌린다**: B가 `tick; ait future amend O future amend D -m "inventory로 옮길 예정"`(Rr) → stdout에 `changed <D>`, `changed <O>`. `rec(O)`·`rec(D)`의 state는 future이고, 연결은 `state undertaken` 그대로다. `ait peek now src/stock.c`에 O도 D도 없다(지금 확인된 것이 없다). `ait peek <D>`의 마지막 event는 `event <Rr> agent-b reopened`다. `settle` 후 A가 이 요청을 받는다(`reason demand <D>`, `contract <D> reopened`). 그 사건의 hint(H14)는 `ait contract pull <D> <O>`를 앞에 붙인 `'<D>' was reopened by 'agent-b'; develop against it, or, if it is no longer needed, run 'ait future cancel <D> -m <why>'`다(D는 A가 만들었고 B가 되돌렸다. cancel할 future D가 A의 HEAD에 없으므로 들이기를 붙인다, 5.9a).
2. **되돌린 동안의 변경은 미확인이 아니다**: B가 stock.c의 로직을 `src/inventory.c`로 옮기는 일반 커밋을 세 번 한다(inventory.c를 만드는 커밋 포함, stock.c에는 호출 한 줄을 남긴다). `warning:`이 없다(걸린 current 계약이 없다). `settle` 후 A가 받는 `change` 사건이 없다. B의 `ait todo`(인자 없음)에 D와 O가 나온다. O는 B가 만든 약속이고, D는 creator가 agent-a지만 B가 되돌린 약속이다(Rr의 D 판은 부모의 current 판을 prev로 둔 future 새 판이므로 Rr은 되돌린 커밋이고, 그 뒤로 D가 다시 current가 된 적이 없다. Rr의 `Ait-Agent`가 agent-b다, 1.5d, 7.8.1). stderr의 hint(H10)에 `ait fulfill <D> -m <why>`와 `ait fulfill <O> -m <why>`가 있다(둘 다 B가 이행할 약속이다. D는 B가 만든 것이 아니므로 H5가 아니다, 5.9a).
3. **다시 이행한다**: B가 `ait fulfill O -m 옮김` → `err E_NEEDS_RESULTS`. 보고에 `provider <D> src/stock.c`와 `provider <O> src/stock.c`가 있고(D·O는 future이므로 코드가 바뀌어도 `?changed`가 붙지 않는다, 3.8), 과제는 `<D>:specs/p.yaml`과 `<O>:specs/pass.yaml`이다(O가 D를 떠맡았다). `tick; ait fulfill O=src/inventory.c D=src/inventory.c -m 옮김 -r pass` → `fulfilled <O>`, `fulfilled <D> via <O>`, `check <D> reported`, `check <O> reported`. O와 D의 provider는 `src/inventory.c <blob>`이고 `unverified` 행이 없다. A가 요청 사건을 받는다.
4. **되돌린 채 그만두면 약속으로 남는다**: 준비를 다시 하고 1을 한다. A가 `tick; ait contract pull D O` → `included <D> from '<Rr의 커밋>'`, `included <O> from '<Rr의 커밋>'`. 이어서 B가 2를 한 뒤 `tick; ait future cancel O -m 중단`(Rx) → `ok`, stdout은 `request <Rx>`, `changed <O>` 두 행이다. D는 future로 남는다(되돌린 계약은 미확인이 아니라 열린 약속이다).
   - `settle` 후 A가 Rx 사건을 받는다. A의 HEAD에서 O는 A의 future demand D를 떠맡은 future offer, 곧 A의 약속 D의 상대이므로 `reason future demand <D>`가 있다(R은 O뿐이다, 9.3). Rx에서 D를 떠맡은 future offer가 없지만 hint(H5, `… undertake <D> o …`)는 없다. D에는 되돌린 쪽(agent-b)이 있으므로 D를 이행할 약속은 되돌린 쪽에 있다(H5는 되돌린 쪽이 있는 demand를 빼고 쓴다, 5.9a). 이 사건에는 hint가 없다(Rx는 D를 되돌리지 않았으므로 H14도 아니다).
   - A가 `tick; ait contract pull O` → `included <O> from '<Rx의 커밋>'`. 2의 코드 커밋도 Rx의 조상이므로 함께 온다(규칙 8). 받은 뒤 O는 cancelled이고 D는 future 그대로다.
5. **되돌리지 않은 계약은 미확인이 된다**: 준비에 A의 current offer K(`ait offer src/stock.c:40-50 p as k -r pass`)를 더하고 A가 `ait sub <K>`로 구독한다. B가 K를 pull한 뒤 1을 한다(O·D만 되돌린다). B의 2의 커밋이 K의 범위도 바꾸면, pre-commit 경고에는 K만 나오고 A가 `change` 사건(`reason sub id <K>`, `unverified <K>`)을 받는다.
6. **current O가 떠맡은 D는 혼자 되돌릴 수 없다**(1.5d, 1.6, 5.9 d): 준비를 다시 한다. B가 `ait future amend D -m 되돌림` → `err E_STATE`, `불변`. stderr 첫 행은 `error: not allowed:`로 시작하고, hint는 `'<D>' is undertaken by current '<O>'; reopen them in the same request: 'ait future amend <D> future amend <O> -m <why>'`다(5.8).
   - hint대로 `tick; ait future amend D future amend O -m 되돌림`(Rd) → stdout은 `request <Rd>`와 `changed` 두 행(D·O, ID 순)이다. `rec(D)`·`rec(O)`의 state는 future다.
   - **직접 작성이면 그 요청 안에서 고친다**(5.8 `E_STATE`, 5.9a): 준비를 다시 하고 `.ait/<D>`의 state를 future로 고쳐 `ait contract add .ait/<D>`, `ait contract commit -m 되돌림` → `err E_STATE`, `불변`. hint는 `.ait/<O>`에도 `state future`를 쓰라는 것과 `ait contract add`·`ait contract commit`이고, 따로 실행하는 `ait future amend`는 없다(이 요청이 `.ait/`를 차지해 도우미는 `E_DIRTY`다). 그대로 하면 `changed` 두 행(D·O)으로 커밋된다.
   - B의 `ait todo`에 D 블록과 O 블록이 나온다(D는 B가 되돌린 약속이다, 1.5d).
   - wa(A): `tick; ait contract pull D O` → `included <D> from '<Rd의 커밋>'`, `included <O> from '<Rd의 커밋>'`. `specs/low.yaml`을 만들고 `tick; ait future amend D - - specs/low.yaml -m 조건`(Rc). `settle` 후 B가 Rc 사건을 받는다. 이유 행에 `reason future demand <D>`(B가 되돌린 D는 B의 약속이다, 9.3)와 `reason future offer <O>`(D는 B의 약속 O의 상대다)가 있고, hint(H3)에 `ait contract pull <D>`, `ait todo <D>`, `ait todo <O>`가 있다(K인 D 자신과 D를 상대로 둔 O가 모두 B의 약속이다. 새 조건이 B의 HEAD에 없으므로 들이기를 앞에 붙인다, 5.9a).
7. **되돌린 요구: 만든 쪽, 되돌린 쪽, 다른 쪽**(1.5d, 9.3, H3, H11, H14): 준비를 다시 한다. wa(A): `tick; ait demand src/order.c src/stock.c:40-50 p as d8 -m d8 -r pass`(R8. 아무 offer도 떠맡지 않은 current demand). wb(B): `tick; ait contract pull D8`, `tick; ait future amend D8 -m 되돌림`(R8r).
   - `settle` 후 A가 R8r 사건을 받는다(`reason demand <D8>`). hint(H14)는 `ait contract pull <D8>`을 앞에 붙인 `'<D8>' was reopened by 'agent-b'; develop against it, or, if it is no longer needed, run 'ait future cancel <D8> -m <why>'`다.
   - wb(B)의 `ait peek src/stock.c`는 D8을 보이고, hint(H11)는 `ait fulfill <D8> -m <why>`(H10)다(D8을 떠맡은 offer가 어디에도 없고, B가 되돌린 쪽이다).
   - wa(A): `tick; ait contract pull D8` → `included <D8> from '<R8r의 커밋>'`. A의 `ait todo`에 D8 블록이 나오고(creator가 A인 약속이다) hint는 H14다. H5(`… undertake <D8> o …`)도 H10(`ait fulfill <D8>`)도 없다(D8에는 되돌린 쪽 agent-b가 있다, 5.9a). A의 `ait peek src/stock.c`의 hint(H11)는 H14 `'<D8>' was reopened by 'agent-b'; develop against it, or, if it is no longer needed, run 'ait future cancel <D8> -m <why>'`다(D8은 A가 만들었고 남이 되돌렸다, 5.9a). 다른 에이전트의 peek이면 `'<D8>' was reopened by 'agent-b'; develop against it`이다.
   - wa(A): `tick; ait future amend D8 - - specs/low.yaml -m 조건`(R8c). `settle` 후 B가 받는 R8c 사건에 `reason future demand <D8>`가 있고(B가 되돌린 D8은 B의 약속이다, 1.3), hint(H3)에 `ait contract pull <D8>`과 `ait todo <D8>`이 있다(새 조건이 B의 HEAD에 없으므로 들이기를 앞에 붙인다, 5.9a).
8. **남이 만든 offer도 되돌리면 내 약속이지만, 거두게 하지 않는다**(1.3, 1.5d, H10, H11, 5.9a): 준비를 다시 한다. wa(A): `tick; ait future amend D future amend O -m 되돌림`(Ra. O는 B가 만들었다).
   - A의 `ait todo`에 D 블록과 O 블록이 나오고, stderr의 hint(H10)에 `ait fulfill <D> -m <why>`와 `ait fulfill <O> -m <why>`가 있다(둘 다 A가 되돌린 약속이다).
   - A의 `ait peek src/stock.c`의 hint(H11)는 `ait fulfill <O> -m <why>`(H10)다(D를 떠맡은 future offer O가 A의 약속이다).
   - wb(B): `tick; ait contract pull D O`. B의 `ait todo`에 O 블록이 나오고(creator가 B인 약속이다), hint는 H14 `'<O>' was reopened by 'agent-a'; develop against it, or, if it is no longer needed, run 'ait future cancel <O> -m <why>'`다. H10(`ait fulfill <O>`)은 없다(O에는 되돌린 쪽 agent-a가 있고, 이행은 되돌린 쪽의 약속이다, 5.9a).
   - B의 `ait peek src/stock.c`의 hint(H11)도 H14 `'<O>' was reopened by 'agent-a'; …`다(D를 떠맡은 future offer O는 B가 만들었지만 남이 되돌렸다, 5.9a). `'<O>' undertakes it; develop against it`이나 H10은 아니다.
   - wb(B): stock.c 2행을 바꿔 stage하고 `tick; ait fulfill D -r pass`(Rf. D만 이행한다, 1.6) → `fulfilled <D>`. `settle` 후 A가 받는 Rf 사건에 `reason future offer <O>`가 있지만(D는 A의 약속 O의 상대다) hint에 `ait future cancel <O>`(H1)는 없다. H1은 D를 떠맡은, 내가 만든 offer에만 쓴다. O는 A가 되돌렸을 뿐 A의 계약(creator가 A)이 아니다(5.9a).
9. **되돌린 쪽은 여럿일 수 있다**(1.5d): 준비를 다시 한다(wa와 wb의 HEAD가 같다). wa(A)와 wb(B)가 각자 `tick; ait future amend D future amend O -m 되돌림`(Ra, Rb. 두 요청의 레코드 판은 같다). wb에서 `tick; git merge --no-edit a` → 충돌 없이 끝난다. wa에서 `git merge -q --ff-only b`. 이제 A의 `ait todo`에 O 블록이, B의 `ait todo`에 D 블록이 나온다. Ra와 Rb는 각자 되돌린 커밋이다. wb의 병합 커밋은 D·O의 판이 부모의 판(Ra·Rb가 만든 같은 판)과 같으므로 되돌린 커밋이 아니다. 그 뒤로 D·O가 current가 된 적이 없으므로 agent-a와 agent-b가 모두 되돌린 쪽이다(1.5d).
10. **`-s ours`로 이행을 버린 통합은 K를 되돌린 것이다**(1.5d, 7.1.5, 7.1.3-5): 준비를 다시 한다(wa와 wb의 HEAD가 같다). wa(A): `tick; ait future amend O future amend D -m 되돌림`(Ra). wb(B): `tick; ait contract pull D O`, stock.c 2행을 바꿔 stage하고 `tick; ait fulfill O -r pass`(Rf) → `fulfilled <O>`, `fulfilled <D> via <O>`. wa(A): `tick; git merge -s ours --no-ff --no-edit b` → exit ≠ 0, merge가 진행 중으로 남는다(O·D의 극대 판은 Rf의 판이고 index는 Ra의 판이므로 prev를 고쳐야 하므로 commit-msg가 막는다, 7.1.5·7.1.3-5). 막힌 오류는 없고 index만 바꿔야 하므로 stderr 첫 행은 `error: not allowed: this merge needs its records pinned and validated again`이고 hint는 `finish it with 'git commit --allow-empty --no-edit --cleanup=strip' (its pre-commit hook must run 'ait hook pre-commit')`다(마치는 커밋, 7.1.3). `tick; git commit --allow-empty --no-edit --cleanup=strip`(Rm) → 성공.
    - `rec(O)`·`rec(D)`는 prev를 빼면 Ra의 판(future)과 같고, prev는 각자 Rf의 판의 blob 한 행이다. 메시지에 새 `Ait-Request`(Rm)와 `Ait-Agent: agent-a`가 있다(7.1.5). 극대 판이 하나이므로 충돌 해결이 아니다(7.1.3-5).
    - Rm의 stderr에 `warning: '<D>' is reopened in this merge: it keeps the future version over the current one`와 같은 `<O>`의 경고가 있고, 이어서 H10(`ait fulfill <D> -m <why>`, `ait fulfill <O> -m <why>`)이 있다(prev를 빼면 future인 출발 판 Ra의 판과 같다. Rm의 `Ait-Agent`인 A가 되돌린 쪽이다, 7.1.5). 이력에서 Rm의 event는 `reopened`다(7.9.5).
    - Rm의 O·D 판은 어느 부모의 판에도 포함되지 않는 future 새 판이고(prev가 Rf의 판이다), 부모 Rf의 판이 current이며, 다른 부모 Ra의 future 판은 그것을 포함하지 않는다. 그래서 Rm은 되돌린 커밋이다. Ra도 되돌린 커밋이지만 그 뒤 Rf에서 다시 current가 되었다. 그래서 되돌린 쪽은 Rm의 agent-a다(1.5d). 다른 에이전트가 이 병합을 했다면 그 에이전트가 되돌린 쪽이 된다. A의 `ait todo`에 D 블록과 O 블록이 나오고, hint(H10)에 `ait fulfill <D> -m <why>`와 `ait fulfill <O> -m <why>`가 있다.
    - wb(B): `tick; ait amend O src/stock.c:1-3 - -m 좁힘 -r pass`(R10. b에서 O는 current다). `settle` 후 A가 받는 R10 사건에 `reason future offer <O>`가 있다(A의 HEAD에서 O는 A가 되돌린 약속이다, 9.3).
11. **D를 되돌린 쪽은 남의 O가 떠맡고 있어도 D를 이행할 약속을 받는다**(H11의 순서): 준비 대신 A가 `tick; ait demand src/order.c src/stock.c:40-50 p as d -m d -r pass`(Rd). B가 `tick; ait contract pull D`, `tick; ait future offer src/stock.c tests/pass.sh as o undertake D o -m 착수`(Ro. O는 future, D는 current). A가 `tick; ait contract pull D O`, `tick; ait future amend D -m 되돌림`(O가 future이므로 D 혼자 되돌릴 수 있다, 5.9 d).
    - A의 `ait peek src/stock.c`의 hint(H11)는 `ait fulfill <D> -m <why>`(D에 H10)다. `'<O>' undertakes it; develop against it`은 없다(A가 D를 되돌린 쪽이다, 5.9a). A의 `ait todo`의 hint도 같다.

## T46 착수와 정리: 떠맡은 요구는 내 약속으로 받고, 끝나면 받지 않는다 (6.8, 7.2.4, 7.9.6, 9.3, 10.1a)

준비(시험 편의상 C는 wa, E는 wb에 두 번째 session으로 연다):

```sh
cd "$T/r"; git worktree add -q "$T/wa" -b a; git worktree add -q "$T/wb" -b b
cd "$T/wa"; A=$(ait session open --agent agent-a); C=$(ait session open --agent agent-c); AIT_SESSION=$A ait listen >> $T/inbox.$A &
cd "$T/wb"; B=$(ait session open --agent agent-b); E=$(ait session open --agent agent-e); AIT_SESSION=$B ait sub src/stock.c; AIT_SESSION=$B ait listen >> $T/inbox.$B &
```

wa(A): `tick; ait future demand src/order.c / p as d -m "어딘가가 막아야 한다"`(R1). `settle`.

1. **다른 브랜치의 추상 요구도 착수할 때 보인다**: `ev(B)` = 0이다(추상 범위의 사건은 구체 범위 구독으로 오지 않는다, 9.3). wb(B)에서 `ait peek src/stock.c` → 레코드는 0건이고, `elsewhere <R1>` 블록에 `contract <D> created-future`가 있다. 들일 방법은 블록의 행이 아니라 hint가 준다(7.9.6). stderr의 hint(H11)에 `if your change implements it`, `ait contract pull <D>`, `ait future offer / specs/p.yaml as o undertake <D> o -m <why>`가 있다(D는 추상이고 HEAD에 없으며, D를 떠맡은 offer가 어디에도 없다, 5.9a).
2. **착수**: wb(B): `ait sub src/order.c src/` → stdout `sub scope src/order.c`, `sub scope src/`. `tick; ait contract pull D` → `included <D> from '<R1의 커밋>'`. `tick; ait future offer src/stock.c p as o undertake D o -m 착수`(R2).
   - `ses(B)`에 `sub id` 행이 없다(관문은 구독을 더하지 않는다, 6.8). D는 B의 약속 O의 상대이므로 구독 없이 받는다(`reason future offer <O>`, 9.3).
   - wb(B): `ait todo`의 stderr hint(H10)에 `ait fulfill <O> <D>=<scope> -m <why>`가 있다(O가 떠맡은 future D가 추상이다, 5.9a).
   - `settle` 후 A가 R2를 받는다(`reason future demand <D>`).
   - **내가 건 요구를 떠맡은 약속이 바뀌어도 받는다**: wa(A): `tick; ait contract pull D O` → 성공(fast-forward. R2가 더한 O와 떠맡기 기록 `<D>-<O>`가 들어온다). wb(B): `tick; ait future amend O src/stock.c:1-20 - -m 좁힘`(R2a). `settle` 후 A가 R2a 사건을 받는다. 그 사건의 `contract` 행은 `contract <O> amended` 하나뿐이고(R은 O뿐이다), 이유는 `reason future demand <D>`다(A의 HEAD에서 O는 D를 떠맡은 future offer, 곧 A의 약속 D의 상대다, 1.3·9.3).
3. **이행하면 내 약속이 끝난다**: wb: stock.c 1행을 바꿔 stage한다. `D=` 없이 `ait fulfill O` → `err E_SCOPE`, `불변`, hint에 `ait fulfill <O> <D>=<scope> -m <why>`가 있다(current가 되는 추상 D에 범위가 없으므로 같은 명령에 `D=<scope>`를 더한 것, 5.8). `tick; ait fulfill O D=src/stock.c -r pass`(R3) → stdout에 `fulfilled <O>`와 `fulfilled <D> via <O>`가 있다.
   - `ses(B)`의 `sub` 행은 그대로다(구독은 `unsub`으로만 지운다, 7.2.4). O가 current가 되어 B의 약속이 아니므로, 이후 D·O의 사건은 B의 구독에 맞을 때만 온다.
   - `settle` 후 A가 R3을 받는다(`reason future demand <D>`).
4. **떠맡은 동안 받고, 놓으면 받지 않는다**: wa(C): `tick; AIT_SESSION=$C ait future demand src/stock.c src/order.c:1-5 p as d4 future demand src/stock.c src/order.c:6-9 p as d5 -m "주문 앞부분"`(R4). wb(B): `tick; ait contract pull D4 D5` → `included <D4> from '<R4의 커밋>'`, `included <D5> from '<R4의 커밋>'`. b가 앞서 있으므로 pull이 커밋 M4를 만든다(부모는 b의 HEAD와 R4의 커밋, 7.12.6). M4는 요청 커밋이 아니다(모든 계약 파일이 한 부모의 판과 같다). `settle` 후 어느 inbox에도 M4의 사건이 없다(새 판도 새 미확인도 없다, 9.1). `tick; ait future offer src/order.c:1-9 p as o4 undertake D4 o4 undertake D5 o4 -m 착수` → `ses(B)`에 `sub id` 행이 없다.
   - wa(C): `tick; AIT_SESSION=$C ait future amend D5 - - specs/low.yaml -m 고침`(R4a). `settle` 후 B가 받는 R4a 사건에 `reason future offer <O4>`가 있고(B의 HEAD에서 B의 약속 O4가 D5를 떠맡았다), `in-head no`이며, hint(H3)에 `ait contract pull <D5>`와 `ait todo <O4>`가 있다(새 조건이 B의 HEAD에 없으므로 들이기를 앞에 붙인다, 5.9a).
   - wb(B): `tick; ait release D5 O4 -m "D5는 놓는다"`. wa(C): `tick; AIT_SESSION=$C ait future amend D5 - - p -m 되돌림`(R4b). `settle` 후 B가 받는 R4b 사건에는 `reason future offer` 행이 없다(B는 `src/`를 구독하므로 `reason sub scope src/`로는 받는다). 그 사건의 hint(H3)는 `ait contract pull <D5>`를 앞에 붙인 `ait peek <D5>`이고 `ait todo <O4>`는 없다(O4는 D5를 더 떠맡지 않으므로 구독으로 받는 쪽의 형이다, 5.9a).
   - wb(B): `tick; ait future cancel O4 -m 중단` → `ses(B)`의 `sub` 행은 그대로다. `ait unsub src/order.c` → stdout은 `unsub scope src/order.c` 한 행이고, `sub scope src/order.c`만 없어지며 `sub scope src/`·`sub scope src/stock.c`는 남는다(7.2.4).
   - `ait peek <D4>` → D4는 future 그대로이고 `undertake <O4> cancelled`가 있다. `ses(B)`는 바뀌지 않는다(7.9.7).
   - **다른 에이전트가 다시 떠맡는다**: wb(E): `tick; AIT_SESSION=$E ait future offer src/order.c:1-5 p as o7 undertake D4 o7 -m 착수`(R5). `settle` 후 B가 R5를 받는다(`reason sub scope src/`). 그 사건에 `reason future offer` 행은 없다(B의 O4는 cancelled다).
5. **다시 확인만 한 것은 아무에게도 가지 않는다**: wa(A): `tick; ait offer src/stock.c:1-10 tests/nobroken.sh as k -m k -r pass`. A가 `ait sub <K>`로 구독한다. wa(C): stock.c 5행을 바꿔 stage → `tick; AIT_SESSION=$C ait commit -m 정리 -r pass`(R6) → `check <K> reported`. `settle` 후:
   - A의 inbox에도 B의 inbox에도 `request <R6>`이 없다(A는 K를 구독하고 B는 `src/`를 구독해도, 다시 고정만 한 레코드는 아무와도 맞지 않는다, 9.3).
   - `ses(C)`에 `sub id <K>`가 없다(6.8).
6. **`--force`로 넘긴 것도 가지 않는다**: wa(C): stock.c 6행을 바꿔 stage → `tick; AIT_SESSION=$C ait commit -m 오타 --force`(R7) → `check <K> forced`. `settle` 후 A·B 모두 R7을 받지 않는다. 넘긴 쪽과 `forced`는 `ait peek <K>`의 이력에 남는다.
7. **미확인은 받는 쪽에 간다**: wa(C): stock.c 8행을 바꾸는 일반 커밋(C8) → `settle` 후 A가 `change <C8>` 사건을 받는다(`reason sub id <K>`, `unverified <K>`). `reason future …`·`reason demand` 행은 없다(current offer는 만든 쪽의 약속도 요구도 아니다, 9.3). B도 받는다(`reason sub scope src/`).

## T47 출력이 다음 할 일을 알린다 (규칙 7, 5.8, 5.9a, 6.7a, 7.2, 9.7)

준비: T46의 준비를 그대로 하되, A의 listener를 띄운 직후 `PA=$!`로 두고, E의 listener도 띄운다(`AIT_SESSION=$E ait listen >> $T/inbox.$E &`). wa(A): `tick; ait future demand src/order.c src/stock.c p as d -m 요구`(R1). `settle`.

1. **session**: wa에서 `ait session open --agent agent-a` → `ok`. stderr에 `warning: agent name 'agent-a' is also used by session '<A>'`가 있고, hint에 `ait session close`와 `AIT_SESSION=`이 있다. 그 SID로 `ait session close`. A의 listener가 도는 동안 `AIT_SESSION=$A ait session close` → `err E_BUSY`, hint에 `kill $PA`가 있다.
   - 준비에서 wa에 C를 열 때 stderr에 `warning: this worktree is used by session '<A>' (agent 'agent-a')`가 있고, hint는 `close this one: 'ait session close <C>', then open a session in a worktree of your own: 'ait session open --agent agent-c' there`다(7.2.1. worktree를 만드는 git 명령은 없다).
2. **HEAD에 없는 변경을 알린다**: wb(B): `tick; ait contract pull D`. wa(A): `tick; ait future amend D - - specs/low.yaml -m 고침`(R2). wb(B): `tick; ait future offer src/stock.c:20-30 p as o undertake D o -m 착수` → `ok`. stderr에 `warning: '<D>' has changes not in HEAD: '<R2의 커밋>' on 'refs/heads/a'`와, 그것을 들이는 명령 `ait contract pull <D>`를 담은 hint가 있다(5.9a의 들이기, H9). `tick; ait contract pull D` → `included <D> from '<R2의 커밋>'`. b가 앞서 있으므로 pull이 커밋 M2를 만든다(7.12.6). `settle` 후 A·B·E 어느 inbox에도 M2의 사건이 없다(M2의 D 판은 R2의 판 그대로이고, O와 연결은 b의 판 그대로다. 새 판도 새 미확인도 없다, 9.1).
3. **함께 떠맡으면 알린다**: 먼저 wb(E): `AIT_SESSION=$E ait peek src/stock.c` → stderr의 hint(H11)에 `'<O>' undertakes it; develop against it`이 있다(D를 떠맡은 남의 O가 HEAD에 있다). 그래도 E의 일이 그것 없이 나아갈 수 없으면 함께 떠맡는다(10.1a): `tick; AIT_SESSION=$E ait future offer src/stock.c p as o2 undertake D o2 -m 착수` → `ok`. stderr에 `warning: '<D>' is also undertaken by '<O>'`가 있고, hint에 `ait future cancel <O2>`가 있다.
4. **조건이 바뀌면 떠맡은 쪽에 hint**: wa(A): `tick; ait future amend D - - p -m 되돌림`(R3). `settle` 후 B가 받는 R3 사건에 `reason future offer <O>`와 `contract <D> respecified`로 시작하는 행이 있고, hint(H3)에 `ait contract pull <D>`와 `ait todo <O>`가 있다(새 조건이 B의 HEAD에 없으므로 들이기를 앞에 붙인다, 5.9a). E가 받는 R3 사건에는 `reason future offer <O2>`가 있고, hint에는 `ait todo <O2>`가 있다.
5. **남이 이행하면 남은 쪽에 hint**: wb(B): `tick; ait contract pull D`. stock.c 1행을 바꿔 stage → `tick; ait fulfill O -r pass`(R4) → `fulfilled <D> via <O>`. `settle` 후 E가 받는 R4 사건에 `contract <D> fulfilled via <O>` 행이 있고, hint는 H1 하나이고 그 명령은 `AIT_SESSION=<E> ait future cancel <O2> -m <why>`다(O2는 E의 계약이다. E는 wb의 기본 session이 아니므로 명령 앞에 `AIT_SESSION=<E> `가 붙는다, 5.9a). 되돌리기 hint는 없다. A가 받는 R4 사건의 hint는 H15 `'<D>' is fulfilled; include it and build on it`과 `ait contract pull <D> <O>`다(D는 A가 만든 demand이고 R4의 판은 wa의 HEAD에 없다. R4는 O도 바꿨다).
   - **남은 연결은 미확인을 막지 않는다**: `.ait/undertake/<D>-<O2>`는 `state undertaken` 그대로다. 떠맡기는 current D를 바꾸는 약속이 아니다(1.6). E가 O2를 그대로 둔 채 stock.c 1행을 다시 바꾸는 일반 커밋(C5, `AIT_SESSION=$E git commit`)을 하면 D는 미확인이 된다. 그 커밋의 경고는 D만 담고(O의 범위 `src/stock.c:20-30`에는 닿지 않는다), D의 되돌리기 hint는 `AIT_SESSION=<E> ait future amend <D> future amend <O> -m <why>`다(D는 current O가 떠맡은 current D이고, E는 wb의 기본 session이 아니다, 7.1.2, 5.9a). 그리고 A가 `change <C5>` 사건(`reason future demand <D>`, `unverified <D>`)을 받는다. A의 HEAD에서 D는 아직 future다(A는 R4를 들이지 않았다). 이어서 `git revert --no-edit HEAD`로 복원한다.
   - **current 계약을 바꾸려면 되돌린다**: E가 `AIT_SESSION=$E ait future amend D -m 바꿀 예정` → `err E_STATE`, `불변`. hint는 `'<D>' is undertaken by current '<O>'; reopen them in the same request: 'AIT_SESSION=<E> ait future amend <D> future amend <O> -m <why>'`다(D를 떠맡은 current O가 있다, 1.5d, 5.9 d). hint대로 `tick; AIT_SESSION=$E ait future amend D future amend O -m 바꿀 예정` → stdout에 `changed <D>`와 `changed <O>`가 있고, D와 O의 state는 future다(남의 O도 되돌릴 수 있다, 전제 1). `settle` 후 B가 받는 이 요청의 사건(`reason future offer <O>`)의 hint는 H14 `'<O>' was reopened by 'agent-e'; develop against it, or, if it is no longer needed, run 'ait future cancel <O> -m <why>'`다(O는 B가 만들었고 E가 되돌렸다. 같은 브랜치의 커밋이므로 들이기는 없다, 5.9a). 그 뒤 stock.c 1행을 바꾸는 일반 커밋은 경고가 없고(걸린 current 계약이 없다), 미확인도 아니다. `ait peek now src/stock.c`는 D도 O도 보이지 않는다.
6. **맡은 쪽이 없어지면 요구를 건 쪽에 hint**: wa(A): `tick; ait future demand src/order.c src/stock.c:20-30 p as d6 -m 요구6`(R5). wb(B): `tick; ait contract pull D6`, `tick; ait future offer src/stock.c:20-30 p as o6 undertake D6 o6 -m 착수`(R6). wa(A): `ait contract pull D6 O6` → `err E_UNNAMED`, `불변`. b의 이력이 D·O·O2도 바꿨으므로 hint는 그 이름을 더한 `name them too: 'ait contract pull …'`(계약 ID 순)다(7.12.3). hint대로 `tick; ait contract pull …` → 성공. wb의 일반 커밋도 R6의 조상이므로 함께 온다. wb(B): `tick; ait future cancel O6 -m 중단`(R7). `settle` 후 A가 R7 사건을 받는다. A의 HEAD에서 O6은 A의 future demand D6을 떠맡은 future offer, 곧 A의 약속 D6의 상대이므로 `reason future demand <D6>`가 있다(R은 O6뿐이다, 9.3). hint(H5)에 `ait future offer src/stock.c:20-30 specs/p.yaml as o undertake <D6> o -m <why>`가 있다(R7에서 D6을 떠맡은 future offer가 없다).
7. **미확인 hint**: wa(A): `tick; ait offer src/order.c:40-45 tests/nobroken.sh as k -m k -r pass`, `ait sub <K>`. order.c 41행을 바꾸는 일반 커밋(C8) → A가 받는 `change <C8>` 사건은 `in-head yes`이고, hint에 `ait amend <K> -m`이 있다. A가 그 hint대로 `tick; ait amend K -r pass`로 다시 확인한다(바꿀 필드가 없는 빈 amend다, 6.5).
   - **hint는 지금 상태로 판정한다**: A의 listener를 멈추고 order.c 42행을 바꾸는 일반 커밋(C9)을 한 뒤, 같은 방법으로 다시 확인하고 listener를 다시 띄운다. A는 `change <C9>` 사건(`unverified <K>`)을 받지만 hint는 없다(HEAD에서 K는 더는 미확인이 아니다, 9.7).
8. **오류 hint**:
   - wb(B): `ait fulfill o -m x` → `err E_USAGE`(7.7.1). hint에 `aliases are local to one command`가 있다. `ait future cancel o -m x` → `err E_ID`, hint에 같은 문구가 있다.
   - wb(E): `AIT_SESSION=$E ait fulfill <O2> -m x -r $T/none.yaml` → `fatal: could not read`(128), hint에 `rerun without -r`가 있다.
   - `AIT_SESSION=$E ait fulfill <O2> -m x` → `err E_NEEDS_RESULTS`(과제는 O2와, 되돌려져 함께 이행될 D의 정의다). `fill <W> pass`로 채운 뒤 한 과제의 `basis`를 지우고 `-r <W>` → `err E_FORMAT`, hint에 `a pass needs a basis`가 있다. `fill <W> fail` 후 `-r <W>` → `err E_CHECK_FAILED`, hint에 `make the code meet the failed task(s) and stage it, then rerun without -r to get the new tasks`가 있다(확인한 tree가 T0인 fulfill의 형. 고친 코드는 확인 입력이 달라 새 과제를 받아야 한다, 5.8). D는 O2가 떠맡아 함께 이행되고 O2는 E가 만들었으므로 놓기 `AIT_SESSION=<E> ait release <D> <O2> -m <why>`도 있다(5.9a).
   - **남의 계약을 거두라고 하지 않는다**(5.8, 5.9a): wa(C): `AIT_SESSION=$C ait future cancel <K> -m x` → `err E_STATE`(current는 cancel할 수 없다). hint에 `ait future amend <K> -m <why>`가 있고 `ait future drop`은 없다(K는 C의 계약이 아니다). wa(A): `ait future cancel <K> -m x` → `err E_STATE`, hint에 `ait future amend <K> -m <why>`와 `ait future drop <K> as x -m <why>`, `with nothing staged`가 있다(K는 A의 계약이다).
9. **stage된 변경이 있으면 들이기 전에 치우라고 한다**(7.12.0, 5.9a의 들이기, 6.7a): 준비를 다시 한다. wa(A): `tick; ait offer src/stock.c:1-10 tests/nobroken.sh as k -m k -r pass`(Rk). wb(B): `tick; ait contract pull K` → `included <K> from '<Rk의 커밋>'`(fast-forward), `tick; ait future offer src/order.c p as o9 -m o9`(b가 앞선다). wa(A): `tick; ait amend K src/stock.c:1-8 - -m 좁힘 -r pass`(Ra).
   - wb(B): stock.c 5행을 바꿔 stage하고 `ait fulfill O9` → `err E_NEEDS_RESULTS`, `불변`. 보고에 `touch <K>`가 있고 과제는 `<K>:specs/nobroken.yaml`과 `<O9>:specs/p.yaml`이다. stderr에 `warning: '<K>' has changes not in HEAD: '<Ra의 커밋>' on 'refs/heads/a'`가 있다. K는 이 요청의 R에는 없고 확인 집합에만 있다(다시 고정할 뿐이다. H9는 R ∪ 확인 집합에 쓴다, 6.7a). 그 hint는 `first commit or set aside your changes`와 들이는 명령 `ait contract pull <K>`다(index에 stage된 변경이 있고, b가 앞서 있어 받는 결과가 merge다, 7.12.0, 5.9a).
   - 감싸지 않고 `tick; ait contract pull K` → `err E_DIRTY`, `불변`. 첫 행은 `error: uncommitted changes in the index`이고, hint는 `first commit or set aside your changes, then rerun this command`다(받는 결과가 merge이고 index가 HEAD와 다르다. 받는 결과를 정한 뒤 아무것도 바꾸기 전에 본다, 7.12.0).
   - `s=$(git stash create) && git reset -q --hard && tick && ait contract pull K && git stash apply --index "$s"`(공유 stash 목록을 쓰지 않는다) → `included <K> from '<Ra의 커밋>'`. b에 pull의 커밋이 생기고, stock.c 5행의 변경은 다시 stage된 채 남는다. 이어서 `ait fulfill O9`에는 `has changes not in HEAD` 경고가 없고, 보고에 `touch <K>`가 그대로 있다(K의 provider는 이제 `src/stock.c:1-8`이다).

10. **내가 만든 요구가 남의 손으로 끝나면 대신할 요구를 건다**(H6): 준비를 다시 한다. wa(A): `tick; ait demand src/order.c src/stock.c:1-10 p as dc -r pass`(Rdc. A가 만든 current demand). wb(E): `tick; ait contract pull DC`, `tick; AIT_SESSION=$E ait future drop <DC> as x -m 정리`, 이어서 아무것도 stage하지 않은 채 `tick; AIT_SESSION=$E ait fulfill <X> -m 정리`(Rw). `settle` 후 A가 받는 Rw 사건에 `reason demand <DC>`와 `contract <DC> withdrawn <X>`가 있고, hint(H6)는 `if your code relies on it, demand it yourself: 'ait future demand <your code> src/stock.c:1-10 specs/p.yaml -m <why>'`다(DC의 provider와 정의, 5.9a). E는 남의 계약을 철회했지만 ait는 그것을 막지 않는다(전제 1, 규칙 9).
11. **fast-forward라도 pull이 쓸 경로에 로컬 변경이 있으면 먼저 치우라고 한다**(5.9a의 로컬 변경, 7.12.0): 준비를 다시 한다. wb(B): `tick; ait contract pull D`(fast-forward. b가 a와 같다). wa(A): `tick; ait future amend D - - specs/low.yaml -m 고침`(R2). wb(B): 다른 내용의 `specs/low.yaml`을 만들어 `git add specs/low.yaml`(그 경로는 wb의 HEAD에서 계약 경로가 아니다), stock.c 1행을 바꿔 stage한 뒤 `ait fulfill D` → `err E_NEEDS_RESULTS`(커밋하지 않으므로 b는 그대로이고 R2의 들이기는 fast-forward다). stderr의 H9 hint는 `commit or set aside your changes to 'specs/low.yaml' before you pull`과 `ait contract pull <D>`다(R2가 쓰는 `specs/low.yaml`에 stage된 변경이 있다. 그 경로는 HEAD에서 코드다, 7.12.0). 그대로 `tick; ait contract pull D`를 하면 `err E_DIRTY`, `불변`이고 첫 행은 `error: your local changes to 'specs/low.yaml' would be overwritten by this pull`이다(fast-forward여도 pull이 쓰는 경로면 막는다, 7.12.0).
    - 그 대신 `specs/low.yaml`을 stage하지 않고 같은 내용으로 작업 폴더에만 두면(추적하지 않는 파일), hint는 `move or remove 'specs/low.yaml' before you pull`과 `ait contract pull <D>`다. 그대로 pull하면 `err E_DIRTY`이고 첫 행은 `error: untracked working tree files would be overwritten by this pull: 'specs/low.yaml'`이다(git처럼 내용이 같아도 추적하지 않는 파일을 덮어쓰지 않는다).

---

## 부록 A. 규칙–시나리오 대응

| 규격 | 시나리오 |
| --- | --- |
| 0.4a·8 확인과 확인 결과 | T10, T12, T17, T20, T21, T25, T26, T39 |
| 규칙 1·9 막지 않되 드러냄 | T13, T16, T17-6, T21-6, T22-4, T28-4, T37-4, T42 |
| 1.2·2.5 명세 정의(command 인자의 cwd·`./` 정리) | T10, T12, T20, T21, T39, T39-6 |
| 1.5a 미확인·확인 행과 PINS·2.3.3 사고 판 | T09-6, T10-2, T13-6, T13-8, T16, T17-2, T20-10, T22, T28-4, T28-14, T33, T34-7, T38, T42, T43-7, T43-11 |
| 7.1.8 post-commit·post-merge의 경고(HEAD의 오류, 옛 판으로 돌아감) | T13-2, T13-6, T13-8, T33 |
| 1.5b 걸린 계약(touches·고정 기준)·확인 집합(그 커밋 뒤에 current인 것) | T12-7, T12-8, T17-5, T22, T22-3, T23, T23-5, T38, T42-2a, T42-6, T42-7, T43, T43-11, T47-9 |
| 1.5c 추상 계약·3.2a 범위 목록 | T44, T46-1, T46-2, T46-3 |
| 1.3 약속의 상대·규칙 6·9.3 받는 쪽(내 약속·내 요구·구독)·7.2.4 구독·10.1a 착수·10.1b 받기 | T03-2, T14-5, T30-4, T31, T31-4, T42-5, T42-13, T42-15, T42-16, T45-1, T45-4, T45-6, T45-7, T45-8, T45-10, T46, T46-4, T47, T47-5, T30-6, T30-7, T33 |
| 규칙 7·5.8·5.9a·6.7a·7.2·9.7 출력의 안내(hint) | T01-3, T01-5, T03-6, T03-8, T03-9, T03-10, T06, T08-4, T08-7, T09-8, T09-10, T10-3, T11-5, T11-5e, T11-5f, T11-5f2, T11-5g, T11-5j2, T11-5l, T11-5m, T11-5n, T11-5p, T11-5q, T11-5q2, T11-5r, T11-5s, T11-5u, T12-1, T12-7, T12-9, T13-9, T13-10, T17-4, T17-5, T17-7, T18-2, T18-4, T22-2, T22-8, T23, T25, T28-8, T28-9, T28-11, T28-13, T28-16, T28-19, T28-20, T28-21, T28-22, T28-23, T28-24, T28-25, T28-26, T28-27, T29-11, T30-3, T30-4, T30-7, T31-2, T31-4, T33, T34-5, T35-8, T36-1, T36-5, T39-6, T41-5, T42-1, T42-2, T42-2a, T42-5, T42-6, T42-15, T42-16, T43-6, T43-8, T44-3, T45-1, T45-2, T45-4, T45-6, T45-7, T45-8, T45-10, T45-11, T46-1, T46-2, T46-3, T46-4, T47, T47-9, T47-11 |
| 5.9a 들이기(계약을 지정한 pull, 지정 검사가 더할 이름, 로컬 변경이 있으면 먼저 치우라는 안내) | T30-4, T31-2, T35-8, T36-1, T36-5, T42-5, T42-16, T45-1, T45-6, T45-7, T46-1, T46-4, T47-2, T47-4, T47-9, T47-11 |
| 5.9a 고치는 명령(관문은 요청 안에서, 이행·commit은 도우미 먼저, 관문의 코드 먼저)·되돌리기 hint·철회 hint·약속으로 하기 | T10-3, T12-1, T12-7, T12-8, T12-9, T17-5, T18-2, T18-4, T22-2, T22-8, T25, T42-1, T42-2, T42-2a, T42-6, T43-8, T45-6, T47-5, T47-8 |
| 5.9a 인자(최상위 기준 경로, KEPT)·로컬 변경 | T11-5l, T11-5m, T11-5n, T11-5q, T11-5q2, T13-9, T17-7, T22-8, T28-9, T28-20, T28-26, T47-9, T47-11 |
| 5.9a 받는 쪽별 hint(H1·H2·H4 내가 만든 것, H3·H6 구독, H5·H10·H11·H14 되돌린 쪽, H15 내 요구의 이행) | T09-8, T17-7, T28-11, T28-23, T31-2, T31-4, T35-8, T42-16, T45-1, T45-4, T45-7, T45-8, T45-10, T45-11, T46-4, T47-3, T47-5, T47-6, T47-10, T30-6 |
| 1.5d 되돌리기·되돌린 쪽(되돌린 커밋, 나란한 가지, 통합 hook의 되돌리기, 이행을 버린 통합)·5.9 d·10.4 | T09-10, T12-8, T23, T28-9, T28-11, T28-13, T28-14, T28-17, T28-21, T28-22, T28-23, T45, T45-9, T45-10, T45-11, T47-5 |
| 5.6–5.8 출력과 오류 형식(5.7의 SIGPIPE, `E_IO`) | 0.2의 `err`, T03-9, T06, T13-1, T21-5, T28-26, T32-3, T43-6, T47-8 |
| 5.9a 전이 hint·5.10 HEAD의 틀린 레코드 복원 | T08-4, T08-7, T09-8, T09-10, T11-5e, T11-5f, T11-5f2, T11-5g, T11-5r, T11-5s, T13-9, T13-10, T17-4, T45-6, T47-5, T47-8 |
| 5.8 `E_BUSY`·5.9a 진행 중인 git 조작·7.1.3 마치는 커밋과 마치는 hint | T28-24, T28-25, T28-26, T28-27 |
| 1.6·7.7.2 undertake의 비대칭과 함께 이행 | T08, T15, T18, T19, T20, T28-13, T30, T45-8 |
| 2.2–2.4 레코드·prev·무결성·5.10 형식 오류 | T04, T07, T09, T11, T11-5, T11-5j2, T11-5u, T12, T13, T13-10, T28, T28-18(creator `unknown -`) |
| 2.3 전이 | T08, T08-7, T09, T11, T11-5e, T11-5f2, T11-5g, T11-5r, T11-5s, T13-10, T17, T23, T28-9, T45-6 |
| 3 범위 | T05, T16, T17, T22, T24, T38, T43-11 |
| 4 runtime | T03, T03-3(`unknown` 예약), T03-8, T32, T40, T41 |
| 5 공통(5.3 메시지와 자르기 행, 7.12.0 index의 E_DIRTY 포함) | T01-5, T03–T06, T03-8, T11-5l, T26, T27, T28-26, T28-27, T38, T41, T47-9 |
| 6 관문·current 등록 | T04–T12, T42-2a |
| 7.1 hook·통합(7.1.5 commit-msg가 7.1.3을 읽기만 하며 자동 병합을 막는다) | T01, T01-3, T13, T13-10, T21, T22, T23, T28, T28-20, T28-24, T28-25, T28-26, T28-27, T37, T42-15, T43-6, T45-10, T47-5 |
| 7.1.3 통합 정리(정규화→되돌리기→전이→prev→쓰기→validate)·7.1.5 기록줄과 경고 | T13-10, T15-6, T28-2, T28-2a, T28-3, T28-8, T28-9, T28-11, T28-13, T28-14, T28-15, T28-16, T28-17, T28-18, T28-19, T28-20, T28-21, T28-22, T28-23, T28-24, T28-25, T42-15, T45-10 |
| 7.3 도우미 | T04–T10, T42, T45-6 |
| 7.4–7.6 직접 작성 | T11, T11-5f2, T11-5g, T11-5h2, T11-5o, T11-5p, T11-5r, T11-5s, T11-5u, T12, T12-9, T13, T35 |
| 7.7 fulfill | T17–T26, T23-5, T30, T46-3, T47-9 |
| 7.7a commit | T43 |
| 7.8 todo | T08, T09-8, T17-7, T20, T21, T22, T28-11, T28-23, T45-2, T45-7, T45-8, T45-9, T45-10, T46-2 |
| 7.9 peek·이력·elsewhere | T05, T14, T15, T16, T18, T22, T22-3, T36, T42, T45-7, T45-8, T46-1 |
| 7.10–7.12 contract push·pull(이력째 받기, 지정, 고르기, 들인 미확인의 경고, 기준 3절의 150행) | T13, T28-2a, T28-8, T28-20, T30, T33–T35, T34-5, T34-12, T42-2, T42-15, T45-4, T47-6, T47-9, T47-11 |
| 7.13 driver | T28, T28-24, T28-25, T29, T29-12, T35 |
| 7.9.5 이력의 `verified`·`unverified`·사본 | T13-6, T13-8, T15-6, T42-10 |
| 9 알림(9.2-1 `prev`의 판, 9.3 버려진 내 변경, 9.5 사건의 키) | T30–T32, T31-6, T34, T35, T37, T40, T42, T45, T46, T46-4, T47, T47-2 |
| 5.14·8.5 `--force` | T10-9, T12-7, T17-4, T22-8, T28-8, T28-11, T34-7, T42-2, T42-2a, T42-15, T43-4 |
