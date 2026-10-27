---
name: ait
description: ait records what the code is expected to do (specs, as conditions with checks) on separate `ait/*` git branches, and shows when the code no longer matches what was checked. Use it in any repository that has `ait/*` branches or whose AGENTS.md or CLAUDE.md mentions ait - whenever you change anything that could change what the code is expected to do (an output, an expected value, a spec, a behavior you were asked to decide), and before you commit - and whenever the user mentions ait, contracts, specs or fulfilling.
---

# ait v1.1

Write a condition your code relies on from other code as a demand, and a condition you provide as an offer. When you start, an offer undertakes a demand; when the implementation and its checks are done, you fulfill it. The heart of ait is seeing whether a new implementation or a new condition conflicts with the contracts already there. Fulfill only after you have checked that it does not. If there is no implementation yet, read [A repository with no implementation yet](references/spec-only.md).

## Start

1. Run `ait init` in the repository. Contracts live on `ait/<name>` branches, independent of the code. `.ait/` on a code branch is the place for drafts; do not write `.ait/init`.
2. In `ait session list`, reuse the session whose agent name and worktree match yours. If there is none, `ait session open --agent <name>`. To name another session, use `AIT_SESSION=<SID>`. Give each agent its own code worktree and session.
3. See all contracts with `ait status /`, and narrow to your work with `ait status src/stock.c`. A PATH argument is a scope, not a contract ID filter: it shows the contracts whose consumer or checked scopes (provider, `fixtures`, environment definition files) overlap it.
4. If there are several ait branches, choose the one to work against with `ait checkout ait/<name>`. Your code work tree stays as it is. `ait switch ait/<name>` actually enters the contract branch; do not use it for code work.

Git 2.53 or later is required. ait takes only the commands below. Do ordinary Git work with `git`.

## Contracts and conditions

Scopes are relative to the repository root: `src/stock.c`, `src/stock.c:10-40`, `src/`, `/`, and `new:src/stock.c` for a file that does not exist yet. Join several with `,`. Quote a path with spaces as one shell word (`"doc/law draft.md"`); output shows such paths in double quotes, as git does. Line ranges are pinned at HEAD, except in a fulfilling request and in `K=SCOPES`, where they are pinned at the index. Prefer whole-file scopes.

Get a template for a definition file with `ait contract new spec specs/stock.yaml exec` or `prompt`.

```yaml
body: when stock is short, refuse the reservation and leave the stock unchanged
engine: exec
command: [sh, tests/stock.sh]
success: [0]
fixtures: [tests/stock.sh, tests/stock-cases.csv]
```

`body` is required. exec needs `command`; an omitted `success` is `[0]`. prompt uses only `body` and `fixtures`. exec may add `environment: NAME`, which refers to `specs/environments/NAME.yaml` in the index being fulfilled. ait does not run checks; the agent runs and confirms them itself.

In `fixtures`, list the code-side files the check reads (tests, data, sources) as files or line ranges (`tests/stock.sh:10-40`), as for provider. Keep them to the minimum the check's result depends on (only a change in what is listed shows as `touch` or `?unverified`). The files must exist when the contract is made, and directories are not allowed. Fulfilling records their content fingerprints along with the provider's, and environment definition files are included on their own. Changing a fixture or an environment definition after fulfilling makes the contract `current ?unverified`, with `?changed` on the `fixtures` line of status. If the change is right, check again and fulfill; if not, restore the version at the fulfillment file's `commit`. Before changing tests or data, run `ait status tests/stock.sh` to see the contracts that hold that file in their checked scopes. Do not weaken a fixture to make a check pass.

Before writing or changing a condition, run `ait status <scope>` to see the contracts on that scope, and check that the new condition does not conflict with theirs.

Whenever you change something that could change what the code is expected to do (an output, an expected value, a spec, a behavior you were asked to decide), run `ait status` to see whether a contract is affected; a change that leaves every expected result as it was, such as fixing a memory leak, does not need it. You need not know beforehand whether you are fulfilling something; status tells you. A contract shown as `touch` or `current ?unverified` is affected: handle it as in step 3 below. If the new behavior is in no contract yet, write it into one (`future demand|offer`, or `future amend`), not into the code alone.

A condition file is copied into the contract's `specs`. Editing the original file does not change the contract; give the edited file to amend again. Do not assume another worktree has the original file.

```sh
ait future demand src/order.c src/stock.c specs/stock.yaml -m "stock condition orders rely on"
ait future offer src/stock.c specs/stock.yaml as o undertake D0123456789ab o -m "start the implementation"
```

Replace the example IDs with those in the actual output. `as o` works only within the same request. `undertake D O` undertakes wholly, `undertake -p D O` partially. One live demand cannot be undertaken both wholly and partially.

Abstract providers such as `/`, `DIR/` and `new:PATH` are for promising in future. When fulfilling, narrow them to existing files or line ranges.

## Changing code and fulfilling

1. Before changing anything, check the contracts, their conditions and their `undertakes` and `undertaken-by` relations in `ait status <scope>`. Whether a contract is current is computed from the code and the fulfillment records; do not judge it from the `state` stored in the contract YAML.
2. Implement the code and `git add` what is needed. **fulfill turns the whole current Git stage into a code commit.** Check what will be committed with `git diff --cached`.
3. In `ait status`, look at `touch` (contracts whose checked scopes this change alters) and `current ?unverified` (contracts whose code changed after they were fulfilled). A code-side `?unverified` sends no notification. Even for another agent's contract, check that the new implementation does not conflict with its condition; if it does not, fulfill it now by giving its ID too. If it conflicts, do not fulfill: fix the code, or, if the condition has to change, reopen it as in "Changing, reopening and withdrawing conditions" below.
4. Run `ait fulfill <K>... -m "reason for the change"`.
5. On `check results required`, read `request PATH` and `result PATH` from stdout. The request's `tree` is the index tree to check; do not check other contents of the work tree in its place. If needed, check that tree out into a temporary directory.

   ```sh
   top=$(git rev-parse --show-toplevel)
   d=$(mktemp -d)
   GIT_INDEX_FILE="$d/index" git -C "$top" read-tree <tree>
   GIT_INDEX_FILE="$d/index" git -C "$top" checkout-index -a --prefix="$d/tree/"
   ```

6. Check each task and fill in `outcome: pass|fail|unknown`, `basis` and `reason` in the result file. With an environment definition, run its `prepare` steps in order; if any fails, do not check, and the outcome is `unknown`. Whatever failed, run `cleanup` in reverse order for the `prepare` steps that ran, and write its failures only in `reason`. A pass needs real evidence from the check. Do not write pass for a check you did not run. Do not change `condition-ids` or task names.
7. Run the same command again with `-r <result path>`. If the code changed, check again. `condition-ids` only confirms that the contract's conditions match; it does not bind the check results to the code.

```sh
ait fulfill O0123456789ab -r /path/to/check-result.yaml -m "implement stock reservation"
ait fulfill O0123456789ab=src/stock.c -r /path/to/check-result.yaml -m "settle where it is implemented"
```

Give the same narrowed scopes in the first run and in the `-r` rerun.

Fulfilling an offer also fulfills the live demands it wholly undertook. Fulfilling a demand may include the non-current offers that partially undertook it. Check every task in the request file.

A commit that finishes a unit of work leaves no `current ?unverified`: before it, check every one in `ait status` and fulfill it in that commit, or fix the code. An ordinary code commit is `git commit`. A fulfillment makes the code commit through Git's hooks and signing, then records the fulfillment on the contract branch. With an empty stage it records the current HEAD. If recording the contract fails after the code commit, ait reports `committed ... but ... not fulfilled` separately; do not treat that as success.

Use `--force` instead of `-r` only for a change you judged truly trivial; it is recorded as `forced`. Do not use it to pass work whose spec or implementation you could not check. If a task is `fail`, do not pass it with `--force`; fix the code and check again.

## Changing, reopening and withdrawing conditions

```sh
ait future amend O0123456789ab - specs/new-stock.yaml -m "new condition"
ait future amend O0123456789ab -m "rework over several commits"
ait future cancel O0123456789ab -m "stop the promise"
ait release D0123456789ab O0123456789ab -m "let go of undertaking this demand"
ait future drop O0123456789ab -m "propose withdrawing the current guarantee"
```

The amend arguments are `consumer provider FILES` for D, `provider FILES` for O, and `target` for drop. A lone `-` argument keeps what is there. A future amend without arguments reopens with a new condition ID, so an earlier fulfillment no longer makes the contract current. Fulfill again after the rework.

One request can chain clauses. Reopening the condition of a demand that a whole offer relies on may require reopening that offer in the same request. Each clause's transition is checked when it is applied, while mixed undertakings and relying pairs are checked over the whole request, so follow the error's hint.

To withdraw a current guarantee, check the X that `future drop K` made and run `ait fulfill X -m "withdraw"`. The target becomes withdrawn. Do not revive an ended contract directly, and do not edit fulfillment files or attachment lists by hand.

## Drafts written directly

`ait contract new demand|offer|drop [GROUP]` or `ait contract checkout K...` makes a draft in `.ait/`. Edit the fields you need, then put it in the separate contract stage. The example below is for a contract without a group; with a group, use the PATH of the `path PATH` that checkout printed.

```sh
ait contract checkout O0123456789ab
ait contract add .ait/O0123456789ab.yaml
ait contract commit -m "change the contract draft"
```

Contract add and commit do not include the user's code Git stage. A new contract written directly also starts in future, and only fulfill records a fulfillment. If an error says a draft is stale, keep the existing draft elsewhere, then get the current version with checkout. Apply only the intended change to the new draft again, then add and commit. checkout does not overwrite an existing draft.

## Same clone, remotes and notifications

Worktrees of the same clone share the `ait/*` refs. When they work against the same ait branch, other agents' contracts are visible at once; do not pull them by ID. To exchange with a remote, set up the contract branch's Git upstream. Replace `ait/master` below with the actual name on the first line of status.

```sh
git push -u origin ait/master   # when setting the upstream the first time
ait contract push
ait contract pull              # takes an optional remote name only
ait sub src/stock.c
ait sub O0123456789ab
ait listen --fetch 30
```

`ait sub` takes an ID or a scope (there is no `sub id K` syntax to type). A scope subscription notifies changes to contracts whose consumer or checked scopes overlap that scope. Remove a subscription with `ait unsub <ID or shown scope>`. listen writes the current session's new events to stdout, and only one runs per session. Stop it with SIGINT or SIGTERM. It also notifies about your promises and your current demands. It does not notify requests made by its own session.

When contract branches conflict (`contract branches conflict`), resolve it with Git on the contract branch as the `contract pull` hint says, then return to the code branch. An ordinary code merge and a contract branch merge are separate. ait never sends code commits to a remote by itself.

Check `current ?unverified`, `?changed`, `?missing`, `?deleted` and `?nobase` (on the provider and `fixtures` lines) and `touch` in `status`. status warns about format errors, and writes refuse them. `git config hides the trailers` means the user's Git configuration conflicts with the record trailers; find the configuration that causes it. Neither the skill nor the installer overwrites the configuration.
