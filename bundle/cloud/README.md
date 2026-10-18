# ait v1.1 Claude 클라우드 묶음

Claude 클라우드 세션(Ubuntu 24.04, x86_64)에서 ait를 쓰게 하는 묶음이다. 실행 파일 대신 소스를 싣고, 세션의 VM이 자기 컴파일러(gcc)로 빌드한다. 계획과 근거는 ait 저장소의 `spec/drafts/v1.1-cloud-plan.md`다.

| 파일 | 하는 일 |
|---|---|
| `src/` | ait 소스(`COMMIT`의 커밋), 시험(`t/`), 스킬, 설치기 |
| `COMMIT` | 이 묶음을 만든 ait 커밋 |
| `setup-script.sh` | 클라우드 환경의 setup script. git-core PPA에서 git(2.53 이상)을 받는다 |
| `session-start.sh` | SessionStart hook. 클라우드에서만 `src/`를 빌드해 `/usr/local`에 설치한다 |
| `settings.json` | 저장소의 `.claude/settings.json`에 넣을 hook |

## 넣는 법

1. 이 폴더를 쓰는 저장소의 `tools/ait/`에 푼다(`tar -xzf ait-v1.1-cloud.tar.gz` 뒤 `ait-cloud/`를 `tools/ait/`로 옮긴다).
2. `settings.json`의 hook을 저장소의 `.claude/settings.json`에 넣는다(파일이 없으면 그대로 복사한다). 커밋하고 push한다.
3. claude.ai의 클라우드 환경 설정에서 **Setup script** 칸에 `setup-script.sh`의 내용을 넣는다. 네트워크는 기본값 **Trusted**면 된다(`ppa.launchpadcontent.net`, `keyserver.ubuntu.com`에 닿는 것을 2026-10-18에 확인했다).

## 첫 세션에서 확인할 것

v1.1은 Linux에서 Docker(arm64)의 Ubuntu 24.04(gcc 13, 일반 사용자와 root)와 Alpine(musl)으로 git 2.53.0에서 빌드·시험했다(77개 통과). x86_64와 PPA의 git(지금 2.55)에서는 아직 돌리지 않았으므로 첫 세션에서 한 번 시험을 돌린다.

```sh
git --version
command -v ait
cp -R tools/ait/src /tmp/ait-test && make -C /tmp/ait-test test
```

그다음 계획의 8장 순서대로 확인한다. 특히 `ait contract push`가 `ait/<name>`을 GitHub에 올리는지 본다. VM은 세션마다 새것이라, 계약은 push한 것만 남는다.

## 주의

- 이 묶음은 v1.1이다. 저장소가 아직 v2 레코드(`.ait/init`이 `version 2`)면 먼저 v1.1로 옮긴다.
- hook은 `CLAUDE_CODE_REMOTE`가 `true`일 때만 돈다. 로컬 세션에서는 아무것도 하지 않는다.
- 같은 커밋이 이미 설치된 VM(재개한 세션)에서는 빌드하지 않는다.
