# Writing contract YAML by hand

`ait contract new demand|offer|drop [GROUP]` or `ait contract checkout K...` writes a draft in `.ait/` and prints its `path`. Edit it, then:

```sh
ait contract add .ait/O0123456789ab.yaml
ait contract commit -m "change the contract"
```

The contract stage is separate from the code's Git stage. A contract written this way starts in future; only `ait fulfill` records a fulfillment. checkout never overwrites a draft: if one is stale, move it aside, check out the current version, and apply only your change again. Do not edit fulfillment files or attachment lists by hand.
