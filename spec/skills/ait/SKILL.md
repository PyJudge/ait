---
name: ait
description: ait keeps the code's expected behavior as contracts, each with a check, on `ait/*` git branches, and shows which contracts a code change may break. Use this skill whenever you work in a repository that has `ait/*` branches or whose AGENTS.md or CLAUDE.md mentions ait, even if the user never says "ait". Read it before you change what the code is expected to do (an output, an expected value, a spec, a behavior you were asked to decide), before you commit, and whenever the user mentions ait, contracts, specs or fulfilling.
---

# ait v1.1

## Concepts

- **Contract**: one thing the code must do, with a check. Example: "reserving more stock than there is is refused and leaves the stock unchanged", checked by running `tests/stock.sh`. Contracts live on `ait/*` git branches, apart from the code, and every agent sees every contract.
- **Demand**: a contract you write when **your** code relies on **other** code. Example: the author of the order code demands the stock behavior above.
- **Offer**: a contract you write when **your** code provides a behavior. An offer **undertakes** a demand when the offer promises to meet that demand.
- **Scope**: where a contract applies, as a path from the repository root: a file `src/stock.c`, lines 10 to 40 `src/stock.c:10-40`, a directory `src/`, the whole repository `/`, or a file not written yet `new:src/stock.c`. Join several scopes with `,`, and quote a path with spaces as one shell word (`"doc/law draft.md"`). A demand names a **consumer** (the code that relies) and a **provider** (the code that provides); an offer names a provider. A line range must exist in HEAD, and in `ait fulfill` in the staged code. Prefer whole files.
- **Spec file**: a YAML file holding the contract's requirement (`body`) and its check. `ait contract new spec <path> exec` (or `prompt`) writes a template that lists the allowed keys. ait copies the spec file into the contract when you write or amend the contract; editing the spec file afterwards does not change the contract.

  ```yaml
  body: when stock is short, refuse the reservation and leave the stock unchanged
  engine: exec
  command: [sh, tests/stock.sh]
  fixtures: [tests/stock.sh, tests/stock-cases.csv]
  ```

- **Fixtures**: the files, besides the provider, that decide the check's result, listed in the spec file as files or line ranges. List the fewest: start from the check's `command` and list the test that `command` runs and the data or expected output that test compares with; leave out the provider (ait already watches the provider), frameworks, logs and other tests. If only part of a file decides the result, list only that part as a line range, such as the one case `tests/all.sh:120-160` in a large test file. A fixture must exist when the contract is written, and directories are not allowed. A file named by `environment` becomes a fixture automatically. ait watches only the provider and the fixtures, so a file left out is never watched.
- **Fulfill**: you run a contract's check on the code you are committing; when the check passes, `ait fulfill` commits the code and records that this code passed the check.
- **`ait status`** lists contracts. Two marks show a contract that a change may break, **whether the contract is yours or another agent's**: `touch` (your staged change alters the contract's provider or fixtures) and `current ?unverified` (the contract's provider or fixtures changed after the contract was fulfilled). ait computes whether a contract is current from the code and the contract's fulfillments; the `state` field in the contract YAML is not updated when the code changes, so the `state` field does not tell you.

When an ait command is used wrongly, ait refuses the command and prints a `hint:` line with the command to run next; run that command rather than working around the refusal.

Read a reference file when you reach its situation:

- [references/start.md](references/start.md): ait is not set up in the repository, you have no session (`no usable ait session`), there are several ait branches, or you share contracts through a remote or want notifications.
- [references/spec-only.md](references/spec-only.md): the repository has a spec but no implementation yet, and several agents start from the spec.
- [references/drafts.md](references/drafts.md): you need to edit contract YAML directly, for a field no `ait future` command sets.

## What to do when

### Most important: you changed what the code is expected to do

ait cannot tell that you changed a behavior; only `ait status` after the change shows which contracts the change may break. Skipping this step is how the code drifts away from its contracts unnoticed, so do it every time.

A change of what the code is expected to do is any change to an output, an expected value, a spec, or a behavior you were asked to decide. A fix that keeps every expected result, such as fixing a memory leak, is not one.

After a change of expected behavior, run `ait status`. For each contract marked `touch` or `current ?unverified`, **yours or another agent's**, do one of these two before you commit:

1. **Keep the contract**: make your code meet the contract as written, and fulfill the contract in the same `ait fulfill` as your change.
2. **Change the contract**: amend the contract (see "A contract's behavior must change"), change every piece of code that relied on the old behavior so that every feature built on the old behavior still works, and fulfill every contract involved in one `ait fulfill`.

Never commit code that leaves a contract broken: other agents build on every contract they see, so a broken contract breaks their code without telling them. For the same reason, a commit that finishes a unit of work leaves no contract `current ?unverified`; an unchecked contract handed on is a broken contract nobody has found yet.

### Other cases

- **Your change creates a behavior that no contract holds yet**: write a demand or an offer for that behavior (the next three items); do not leave the behavior only in the code.
- **You are about to write or amend a contract**: run `ait status <scope>` on the scopes of the new contract. The output lists the contracts already on those files, each with its `spec` lines. Make sure the new contract contradicts none of those contracts.
- **Your code will rely on code that someone else owns, or that nobody has written yet**: `ait future demand <consumer> <provider> <spec file> -m <why>`. `<consumer>` is **your** code that relies on the behavior; `<provider>` is the code that has the behavior, or `new:PATH` or `/` if that code is not written yet.
- **You take on a demand** (assigned to you, or listed in `ait status` with nobody undertaking the demand): `ait future offer <provider> <spec file> as o undertake <D> o -m <why>` writes your offer and undertakes the demand `<D>` in one command (`as o` names the new offer within the command). Use `undertake -p <D> o` if your offer meets only part of the demand.
- **Your code provides a behavior that others may rely on, and no demand asks for that behavior**: `ait future offer <provider> <spec file> -m <why>`.
- **Your implementation is done**: fulfill the contracts.
  1. Stage the change with `git add`. `ait fulfill` commits the **whole** Git stage, so check `git diff --cached` first.
  2. Run `ait fulfill <K>... -m <why>` with the ID of every contract the change affects, **yours and other agents'**. Narrow a provider written as `/`, `DIR/` or `new:PATH` to real files with `<K>=<scope>`, as in `ait fulfill O0123456789ab=src/stock.c -m <why>`.
  3. `ait fulfill` prints `request PATH` and `result PATH`. Run the checks on the request's `tree`, which is the staged code, not on your work tree:

     ```sh
     d=$(mktemp -d)
     GIT_INDEX_FILE="$d/index" git read-tree <tree>
     GIT_INDEX_FILE="$d/index" git checkout-index -a --prefix="$d/tree/"
     ```

  4. Run every task's check yourself; ait runs none. In the result file, fill in each task's `outcome: pass|fail|unknown`, `basis` and `reason`. Write `pass` only for a check you ran and saw pass: a fulfillment is the record other agents trust instead of checking again. If the spec file names an environment, run the environment's `prepare` steps in order (if one fails, the outcome is `unknown`), and afterwards the environment's `cleanup` steps in reverse order for the `prepare` steps that ran.
  5. Run the same `ait fulfill` command again with `-r <result PATH>`.

  `--force` instead of `-r` fulfills without checks and is recorded as `forced`. Use `--force` only for a change you judged trivial, such as a comment or a rename that no check can tell apart; never for a task that failed or a task you could not check, because a forced fulfillment tells other agents the code was checked when it was not.
- **You need to change a test or data file**: run `ait status <file>` first; the output lists the contracts that use the file as a fixture. After the change, `ait status` shows `?changed` on those contracts' `fixtures` line: if the change is right, check those contracts again and fulfill them; if not, restore the file from the commit on the contract's `fulfilled` line. Never weaken a fixture or a test to make a check pass: a weakened check passes while the behavior it guarded is broken.
- **A contract's behavior must change** (the contract is wrong, or the expected behavior was decided differently): `ait future amend <K> - <spec file> -m <why>` for an offer, `ait future amend <K> - - <spec file> -m <why>` for a demand. The arguments are those of `ait future offer` (`<provider> <spec file>`) or `ait future demand` (`<consumer> <provider> <spec file>`), and `-` keeps the old value. Then change every piece of code that relied on the old behavior, and fulfill every contract involved.
- **Another agent's contract blocks your change**: do not break **their** contract. Either make your code meet **their** contract, or amend **their** contract with the reason in `-m` and change every piece of code that relied on **their** contract, as in "Most important: you changed what the code is expected to do".
- **You rework a contract's code over several commits**: `ait future amend <K> -m <why>` with no other arguments reopens the contract (the contract's earlier fulfillment no longer counts); fulfill the contract when the rework is done.
- **You no longer meet a demand that your offer undertook**: `ait release <D> <O> -m <why>`.
- **You will not deliver what your contract promised**: `ait future cancel <K> -m <why>`.
- **A current contract's behavior is removed from the code**: `ait future drop <K> -m <why>`, then `ait fulfill <X>` for the drop contract `X` that the command prints.
- **Someone else changed a contract that your code relies on or provides** (shown in a notification or in `ait status`): check your code against the changed contract, then fulfill again, or amend **your own** contract.
