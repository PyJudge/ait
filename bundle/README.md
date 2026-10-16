# ait 실험 묶음

ait 실행 파일(macOS arm64)과 그 스킬을 묶은 것이다. 이 ait는 1단계 명세(v15)에서 알림(`listen`·`sub`)·`peek`·`future drop`·`contract push`·merge driver를 뺀 것이다. 남의 계약은 `contract pull`로 받는다.

```sh
sh install.sh            # ait를 ~/.local/bin에 설치(PREFIX로 바꿀 수 있다). PATH의 첫 ait가 모든 저장소의 hook을 돈다
sh setup.sh ~/my-project # 있는 git 저장소에 스킬과 CLAUDE.md 안내를 넣고 ait init
sh try.sh ~/ait-play     # 명세만 있는 실험 저장소와 에이전트 a, b의 worktree를 만든다
sh ait-graph.sh          # (저장소 안에서) 계약 커밋의 그래프와, 커밋마다 바뀐 계약의 종류·상태
```

- `bin/ait`: ait. git 2.53 이상이 필요하다.
- `skills/ait/`: 스킬. setup.sh와 try.sh는 그 저장소의 `.claude/skills/`에 넣는다. 모든 프로젝트에서 쓰려면 `~/.claude/skills/`에 복사한다.
- 레코드 형식은 `.ait/init`의 `version 2`다. 옛 판(`version 1`)으로 시작한 저장소에서는 이 ait가 `.ait/init`을 고치라는 hint를 주며 멈춘다(계약이 없으면 그대로 따르면 된다).
- setup.sh는 `ait init`으로 그 저장소의 현재 브랜치에 `.ait/init`을 커밋하고 hook 넷을 깐다. 스킬과 CLAUDE.md의 `## ait` 절은 커밋하지 않고 둔다.
