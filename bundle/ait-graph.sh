#!/bin/sh
# Let Git display the orphan contract branches; current state is computed by ait status.
[ $# -gt 0 ] || set -- '--branches=ait/*' '--remotes=*/ait/*'
exec git log --graph --oneline --decorate "$@"
