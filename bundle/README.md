# ait v1.1 배포 묶음

현재 빌드의 실행 파일, 에이전트 스킬, 설치·저장소 설정·실험 스크립트다. `VERSION`은 ait 판, `PLATFORM`은 실행 파일을 빌드한 OS·CPU다. 실행 파일은 `PLATFORM`에 적힌 OS·CPU에서만 설치된다(macOS는 11 이상). Git 2.53 이상이 필요하다. Git 커밋을 만들 사용자 이름·이메일도 설정되어 있어야 한다.

## 설치와 프로젝트 설정

압축을 푼 `ait/` 안에서 실행한다.

```sh
sh install.sh
export PATH="$HOME/.local/bin:$PATH"
sh "$HOME/.local/share/ait/setup.sh" /path/to/project
```

`PREFIX=/원하는/위치 sh install.sh`로 설치 위치를 바꾼다. 실행 파일은 `PREFIX/bin/ait`, 스킬과 보조 스크립트는 `PREFIX/share/ait/`에 설치한다. PATH 설정은 사용하는 셸의 설정에도 넣는다.

`setup.sh`는 Git 작업 폴더에서 `ait init`을 실행하고 `.agents/skills/ait/`, `.claude/skills/ait/`, `AGENTS.md`·`CLAUDE.md`의 ait 안내를 준비한다. 기존 코드 HEAD와 Git stage를 커밋하지 않는다. 첫 코드 커밋이 없는 저장소에서도 쓸 수 있다. 추가 worktree마다 setup을 실행하거나, 에이전트 파일을 먼저 커밋한 뒤 worktree를 만든다.

각 에이전트는 자기 worktree에서 session을 연다.

```sh
ait session list
ait session open --agent agent-a  # 재사용할 자기 session이 없을 때
ait status /
```

## v1.1의 저장 방식

- 계약과 이행은 코드와 독립된 `ait/<name>` 브랜치에 저장된다. `.ait/`는 작업 폴더의 초안 자리다. `.ait/init`은 사용하지 않는다. 구판 레코드를 자동 변환하는 설치기는 아니다.
- `ait init`은 알림용 `reference-transaction` hook 하나를 준비한다. 다른 hook이 있으면 덮어쓰지 않고 polling을 사용한다. hook은 Git과 셸로 동작한다.
- 명세 정의는 계약 안의 `specs`로 복사된다. `inputs`는 쓰지 않는다. `environment`는 지원한다.
- 상태는 `ait status`, 일반 코드 커밋은 `git commit`, 확인·이행은 `ait fulfill`이다. 계약 초안의 stage는 일반 Git stage와 별개다.
- `listen`·`sub`·`unsub`, `future drop`, `contract push`·`pull`을 지원한다. 같은 clone의 worktree는 계약 ref를 공유한다. 원격의 계약은 `ait contract pull [remote]`로 받으며 ID를 인자로 주지 않는다.

## 작은 실험

```sh
sh "$HOME/.local/share/ait/try.sh" "$HOME/ait-play"
cd "$HOME/ait-play/main"
sh "$HOME/.local/share/ait/ait-graph.sh"
```

`main/`에는 명세, `a/`·`b/`에는 각각 코드 브랜치와 에이전트 스킬이 준비된다. 구현이 없을 때의 진행은 스킬의 `references/spec-only.md`를 따른다. 그래프는 Git의 실제 계약 브랜치 이력이고, 지금 코드에서의 계약 판정은 `ait status /`로 본다.
