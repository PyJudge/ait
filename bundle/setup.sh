#!/bin/sh
# Initialize ait and install repository-local agent instructions; do not commit code.
set -e
here=$(cd "$(dirname "$0")" && pwd)
case $(uname -s) in *_NT-*) x=.exe ;; *) x= ;; esac
if [ -x "$here/bin/ait$x" ]; then
	ait_bin="$here/bin/ait$x"
else
	ait_bin="$here/../../bin/ait$x"
fi
[ -x "$ait_bin" ] || { echo "fatal: install this release first" >&2; exit 128; }
top=$(git -C "${1:-.}" rev-parse --show-toplevel 2>/dev/null) || {
	echo "fatal: '${1:-.}' is not in a git work tree" >&2; exit 128;
}
# Fail before replacing any existing agent instructions.
(cd "$top" && "$ait_bin" init)
# the agents there run the ait found on PATH, which may be another ait than this one
on_path=$(command -v ait 2>/dev/null || true)
[ -z "$x" ] || [ ! -f "$on_path$x" ] || on_path=$on_path$x
if [ -z "$on_path" ] || ! cmp -s "$ait_bin" "$on_path"; then
	echo "warning: the ait on PATH ('${on_path:-none}') is not this release's; agents in '$top' run that one" >&2
	echo "hint: put '$(cd "$(dirname "$ait_bin")" && pwd)' first on PATH" >&2
fi
for runtime in .agents .claude; do
	dest="$top/$runtime/skills/ait"
	[ ! -L "$dest" ] || { echo "fatal: '$dest' is a symbolic link" >&2; exit 128; }
	mkdir -p "$dest"
	cp -R "$here/skills/ait/." "$dest/"
done
for entry in AGENTS.md CLAUDE.md; do
	if ! grep -q '^## ait$' "$top/$entry" 2>/dev/null; then
		[ ! -s "$top/$entry" ] || printf '\n' >>"$top/$entry"
		cat >>"$top/$entry" <<'MD'
## ait

이 저장소는 ait v1.1의 별도 계약 브랜치를 사용한다. 코드를 고치거나 커밋하기 전에 ait 스킬(`.agents/skills/ait/SKILL.md`, Claude Code에서는 `.claude/skills/ait/SKILL.md`)을 읽고 따른다.
MD
	fi
done
cat <<MSG
set up: $top
  .agents/skills/ait/, .claude/skills/ait/  current skill
  AGENTS.md, CLAUDE.md                    agent entry points
  ait/* branch, reference-transaction hook initialized by ait

Code HEAD and the Git stage were preserved. Agent files are not committed.
Run setup in each new worktree, or commit these agent files before creating it.
Each agent opens its own session there: ait session open --agent <name>
MSG
