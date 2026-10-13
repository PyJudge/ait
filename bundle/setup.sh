#!/bin/sh
# Set up an existing git repository for ait (v2): the ait-v2 skill in its .claude/skills, a CLAUDE.md
# section that points agents to it, and 'ait init' (hooks, and a commit of .ait/init on the current branch).
#   sh setup.sh [REPO]      (default: the repository of the current directory)
set -e
here=$(cd "$(dirname "$0")" && pwd)
command -v ait >/dev/null 2>&1 || { echo "fatal: ait is not on PATH; run 'sh $here/install.sh' first" >&2; exit 128; }
cmp -s "$here/bin/ait" "$(command -v ait)" ||
	echo "warning: the ait on PATH ('$(command -v ait)') is not this bundle's; the hooks will run that one" >&2
top=$(git -C "${1:-.}" rev-parse --show-toplevel 2>/dev/null) || { echo "fatal: '${1:-.}' is not in a git repository" >&2; exit 128; }
git -C "$top" rev-parse -q --verify HEAD >/dev/null || { echo "fatal: '$top' has no commit yet; commit something first" >&2; exit 128; }
git -C "$top" symbolic-ref -q HEAD >/dev/null || { echo "fatal: HEAD is detached in '$top'; check out a branch first" >&2; exit 128; }
mkdir -p "$top/.claude/skills"
rm -rf "$top/.claude/skills/ait-v2"
cp -R "$here/skills/ait-v2" "$top/.claude/skills/"
if ! grep -q '^## ait$' "$top/CLAUDE.md" 2>/dev/null; then
	[ ! -s "$top/CLAUDE.md" ] || printf '\n' >> "$top/CLAUDE.md"
	cat >> "$top/CLAUDE.md" <<'MD'
## ait

이 저장소는 여러 에이전트가 함께 고치며, 서로 기대는 조건을 ait 계약으로 적어 둔다. 코드를 고치거나 커밋하기 전에 ait-v2 스킬(`.claude/skills/ait-v2/SKILL.md`)을 읽고 그 절차를 따른다. ait의 `hint:`는 그대로 따른다.
MD
fi
cd "$top" && ait init
cat <<MSG
set up: $top
  .claude/skills/ait-v2/   the skill (not committed; commit it if every clone should have it)
  CLAUDE.md                its '## ait' section (not committed)
  .ait/init, hooks         committed and installed by 'ait init'

Give each agent its own worktree and branch, for example:
  git -C "$top" worktree add ../a -b a
and open one Claude Code session there per agent: "너는 에이전트 a다. ait-v2 스킬대로 일한다. <맡길 일>".
MSG
