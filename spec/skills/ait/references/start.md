# Setting up, sessions, remotes and notifications

ait needs Git 2.53 or later. ait takes only its own commands; do ordinary Git work with `git`.

1. Run `ait init` in the repository. Contracts live on `ait/<name>` branches, apart from the code; `.ait/` in the work tree holds contract drafts.
2. Reuse your session from `ait session list` (the one with your agent name and your worktree), or open one with `ait session open --agent <name>`. Each agent has its own worktree and session. Set `AIT_SESSION=<SID>` to act as another session.
3. `ait status /` shows every contract. `ait status <scope>` shows the contracts whose consumer, provider or fixtures overlap the scope; the argument is a scope, not a contract ID.
4. If there are several ait branches, `ait checkout ait/<name>` picks the ait branch to work against; the code work tree does not change. `ait switch ait/<name>` checks out the contract branch itself; do no code work while on the contract branch.

All worktrees of one clone share the `ait/*` refs, so a contract written in one worktree is visible in the others at once. To share contracts through a remote:

```sh
git push -u origin ait/master   # once; use the ait branch name on the first line of ait status
ait contract push
ait contract pull [<remote>]
ait sub src/stock.c             # subscribe to a scope or a contract ID; ait unsub removes the subscription
ait listen --fetch 30           # one listener per session; stop the listener with SIGINT or SIGTERM
```

`ait listen` notifies a session when a contract changes that is the session's promise (a contract the agent wrote that is not current yet) or the other side of that promise, the agent's current demand, or a subscribed scope or ID. `ait listen` does not notify when a code change makes a contract `current ?unverified`; run `ait status` to see those contracts. ait never pushes code commits.
