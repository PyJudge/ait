# Setting up, sessions, remotes and notifications

Git 2.53 or later is required. ait takes only its own commands; do ordinary Git work with `git`.

1. `ait init` in the repository. Contracts live on `ait/<name>` branches, apart from the code; `.ait/` in the work tree holds drafts.
2. Reuse your session from `ait session list` (same agent name and worktree), or `ait session open --agent <name>`. Each agent has its own worktree and session; `AIT_SESSION=<SID>` names another.
3. `ait status /` shows every contract; `ait status <scope>` those whose consumer or checked scopes overlap the scope (a scope, not an ID).
4. With several ait branches, `ait checkout ait/<name>` picks the one to work against; the code work tree stays. `ait switch ait/<name>` enters the contract branch itself, not for code work.

Worktrees of one clone share the `ait/*` refs, so contracts on the same ait branch are visible at once. With a remote:

```sh
git push -u origin ait/master   # once, with the name on status's first line
ait contract push
ait contract pull [<remote>]
ait sub src/stock.c             # or an ID; ait unsub to remove
ait listen --fetch 30           # one per session; stop with SIGINT or SIGTERM
```

listen also notifies about your promises and your current demands, but not about a code change that makes a contract `?unverified`: look at `ait status` for that. ait never pushes code commits.
