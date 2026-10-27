---
name: ait
description: ait records what the code is expected to do (specs, as conditions with checks) on separate `ait/*` git branches, and shows when the code no longer matches what was checked. Use it in any repository that has `ait/*` branches or whose AGENTS.md or CLAUDE.md mentions ait - whenever you change anything that could change what the code is expected to do (an output, an expected value, a spec, a behavior you were asked to decide), and before you commit - and whenever the user mentions ait, contracts, specs or fulfilling.
---

# ait v1.1

A contract is a condition on the code with a check. A **demand** is a condition your code relies on from other code; an **offer** is one your code provides, and it **undertakes** the demands it meets. You **fulfill** a contract once its check passes on the code you commit. The heart of ait is seeing whether a new implementation or a new condition conflicts with the contracts already there; fulfill only after you have checked that it does not.

Setting up, sessions, remotes and notifications: [references/start.md](references/start.md). No implementation yet: [references/spec-only.md](references/spec-only.md). Writing contract YAML by hand: [references/drafts.md](references/drafts.md). ait follows its errors with a `hint:`; do what it says.

## When the expected behavior changes

Whenever you change something that could change what the code is expected to do (an output, an expected value, a spec, a behavior you were asked to decide), run `ait status` to see whether a contract is affected; a change that leaves every expected result as it was, such as fixing a memory leak, does not need it. You need not know beforehand whether you are fulfilling something; status tells you. A contract shown as `touch` (your change alters what it checks) or `current ?unverified` (its code changed after it was fulfilled) is affected, whoever made it: if the new code does not conflict with its condition, fulfill it with your change; if it conflicts, do not fulfill, but fix the code or reopen the condition. If the new behavior is in no contract yet, write it into one, not into the code alone. Before writing or changing a condition, check with `ait status <scope>` that it does not conflict with the contracts on that scope.

Whether a contract is current is computed from the code and its fulfillments; do not judge it from the `state` in its YAML. A commit that finishes a unit of work leaves no `current ?unverified`.

## Conditions and contracts

Scopes are paths from the repository root: `src/stock.c`, `src/stock.c:10-40`, `src/`, `/`, and `new:src/stock.c` for a file not there yet; join several with `,`. Prefer whole files. `/`, `DIR/` and `new:` promise in future and are narrowed with `K=SCOPES` when fulfilling.

Get a condition file template with `ait contract new spec specs/stock.yaml exec` (or `prompt`).

```yaml
body: when stock is short, refuse the reservation and leave the stock unchanged
engine: exec
command: [sh, tests/stock.sh]
fixtures: [tests/stock.sh, tests/stock-cases.csv]
```

`fixtures` are the files the check reads besides the provider. Keep them to the minimum the check's result depends on: only a change in what is listed shows as `touch` or `?unverified`. Do not weaken a fixture or a test to make a check pass.

A condition file is copied into the contract. Editing the file does not change the contract; give it to `future amend` again.

```sh
ait future demand src/order.c src/stock.c specs/stock.yaml -m "stock condition orders rely on"
ait future offer src/stock.c specs/stock.yaml as o undertake D0123456789ab o -m "start the implementation"
ait future amend O0123456789ab - specs/new-stock.yaml -m "new condition"   # D: consumer provider FILES; O: provider FILES; - keeps
ait future amend O0123456789ab -m "rework"                                 # reopen: the old fulfillment no longer counts
ait future cancel O0123456789ab -m "stop the promise"
ait release D0123456789ab O0123456789ab -m "let go of this demand"
ait future drop O0123456789ab -m "withdraw the guarantee"                  # then fulfill the X it makes
```

`undertake D O` undertakes wholly, `undertake -p D O` partially.

## Fulfilling

1. `git add` the change. **fulfill commits the whole Git stage**, so check `git diff --cached`.
2. `ait fulfill <K>... -m <why>`, with every affected contract's ID.
3. It prints `request PATH` and `result PATH`. Check the request's `tree` (the index), not your work tree:

   ```sh
   d=$(mktemp -d)
   GIT_INDEX_FILE="$d/index" git read-tree <tree>
   GIT_INDEX_FILE="$d/index" git checkout-index -a --prefix="$d/tree/"
   ```

4. Run every task's check yourself (ait runs none) and fill in `outcome: pass|fail|unknown`, `basis` and `reason`. A pass needs evidence from a check you ran. With an environment, run its `prepare` steps in order (any failure: `unknown`) and its `cleanup` in reverse for those that ran.
5. Rerun as the hint says, with `-r <result>`.

`--force` instead of `-r` is only for a change you judged truly trivial; it is recorded as `forced`. Never use it for a `fail` or for work you could not check.
