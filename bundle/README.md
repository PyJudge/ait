# ait-mini 실험 묶음

ait-mini 실행 파일(macOS arm64)과 ait-mini 스킬을 묶은 것이다.

```sh
sh install.sh          # ait를 ~/.local/bin에 설치(PREFIX로 바꿀 수 있다). PATH에 있어야 hook이 ait를 부른다
sh try.sh ~/ait-play   # 명세만 있는 실험 저장소와 에이전트 a, b의 worktree를 만든다
sh ait-graph.sh        # (실험 저장소 안에서) 계약 커밋의 그래프와, 커밋마다 바뀐 계약의 종류·상태
```

- `bin/ait`: ait-mini. git 2.36 이상이 필요하다.
- `skills/ait-mini/`: 스킬. try.sh는 실험 저장소의 `.claude/skills/`에 넣으므로 다른 프로젝트에는 영향이 없다. 모든 프로젝트에서 쓰려면 `~/.claude/skills/`에 복사한다.
- try.sh가 만드는 저장소: `doc/spec.md`(조항 셋), 구현 없음, `ait init` 완료, `a/`와 `b/` worktree. 각 worktree에서 Claude Code를 하나씩 열어 맡을 조항을 주면 된다.
