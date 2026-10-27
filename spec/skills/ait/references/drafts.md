# Writing contract YAML by hand

`ait contract new demand|offer|drop [GROUP]` writes a new contract as a draft in `.ait/`, and `ait contract checkout K...` writes an existing contract there; both print the draft's path as `path PATH`. Edit the draft, then put the draft on the contract branch:

```sh
ait contract add .ait/O0123456789ab.yaml
ait contract commit -m "change the contract"
```

`ait contract add` stages drafts on a contract stage that is separate from the code's Git stage. A new contract written this way starts as `future` (not fulfilled); only `ait fulfill` records a fulfillment. If `ait contract commit` refuses a draft as stale because someone changed the contract after you checked the contract out, move your draft aside, run `ait contract checkout` again for the current version, and reapply only your change; `ait contract checkout` never overwrites a draft. Do not edit fulfillment files or attachment lists by hand.
