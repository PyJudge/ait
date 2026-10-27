# A repository with no implementation yet

Write the spec as contracts first, then implement. Contracts live on the ait branch, which every worktree of the clone sees at once; code files live in each worktree and reach another worktree only through Git commits and merges.

1. Each agent opens a session in the agent's own code worktree and runs `ait status /`.
2. Write a spec file whose `body` states one requirement of the spec. While no implementation exists to run a check against, use a prompt spec file (`body` only); change the spec file to exec with `ait future amend` once a runnable check exists.
3. A demand on code that does not exist yet can name `/` as the provider. Set the demand's consumer to code that already exists and relies on the requirement, such as the spec document.

```yaml
body: when stock is short, refuse the reservation and leave the stored stock unchanged
```

```sh
ait future demand doc/spec.md / specs/stock.yaml -m "stock reservation requirement"
```

4. The agent who implements the requirement sees the demand on the shared contract branch, writes the same spec file in that agent's own worktree, and writes an offer that undertakes the demand. A spec file that another agent has not committed is not in your worktree.

```sh
ait future offer / specs/stock.yaml as stock undertake D0123456789ab stock -m "start the stock implementation"
```

5. Write the implementation and its test, and stage both. Fulfilling the offer also fulfills the demand that the offer wholly undertakes, so narrow the `/` provider of both contracts in one `ait fulfill` by giving both IDs.

```sh
git add src/stock.sh tests/stock.sh
ait fulfill O0123456789ab=src/stock.sh D0123456789ab=src/stock.sh -m "implement stock reservation"
# check the tree of the printed request, fill in the result file, then rerun with the same scopes:
ait fulfill O0123456789ab=src/stock.sh D0123456789ab=src/stock.sh -r /path/to/check-result.yaml -m "implement stock reservation"
```

To promise a file that is not in HEAD yet, name the file as `new:src/stock.sh`; when fulfilling, narrow `new:src/stock.sh` to `src/stock.sh` once the file is staged. A line range can be given to `ait future amend` only for lines already in HEAD.

While one agent implements, the other agent keeps working against the contract as written. When a contract changes, `ait status /` and the notifications show the change; change your code to meet the changed contract. Merge finished code branches with Git, then run `ait status /` on the merged code to see which contracts are current.
