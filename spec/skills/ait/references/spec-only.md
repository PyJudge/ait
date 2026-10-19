# A repository with no implementation yet

Write the spec as contracts first, keeping apart the ait branch you work against and the code HEAD. Worktrees of the same clone share the contract branches, while the code files are in each worktree.

1. Each agent opens a session in its own code worktree and checks `ait status /`.
2. Make a condition file with a condition of the spec as its `body`. If the check cannot run because there is no implementation, use a prompt definition (`body` only). Switch it to an exec definition once a runnable check exists.
3. A demand on an implementation that does not exist yet can promise `/` as its provider. Set its consumer to a using-side scope that already exists, such as the spec file.

```yaml
body: when stock is short, refuse the reservation and leave the stored stock unchanged
```

```sh
ait future demand doc/spec.md / specs/stock.yaml -m "stock reservation condition"
```

4. The agent taking the implementation sees that demand on the same contract branch, prepares the condition file in its own worktree, and undertakes it. Do not assume another worktree's uncommitted condition file is in your work tree too.

```sh
ait future offer / specs/stock.yaml as stock undertake D0123456789ab stock -m "start the stock implementation"
```

5. Write the implementation and its checks, and check the code stage. Narrow the abstract providers of the whole offer and of the demand together when fulfilling. Name both IDs so that the conditions of contracts that rely on each other change in the same request.

```sh
git add src/stock.sh tests/stock.sh
ait fulfill O0123456789ab=src/stock.sh D0123456789ab=src/stock.sh -m "implement stock reservation"
# check the tree of the printed request, fill in the result, and rerun with the same scopes:
ait fulfill O0123456789ab=src/stock.sh D0123456789ab=src/stock.sh -r /path/to/check-result.yaml -m "implement stock reservation"
```

To name in future a file that is not in HEAD yet, use `new:src/stock.sh`. When fulfilling, narrow it to a concrete scope once the file is in the index. To give a line range to future amend directly, the code must be in HEAD first.

While one side is working, the other keeps working against the condition of that promise. When a condition changes, look at `ait status /` and the notifications and adjust to the agreed condition. Merge finished code branches with Git, and check the contracts' current judgment again on the merged code.
