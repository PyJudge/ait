from lib import *
import os
r = Repo.__new__(Repo); r.path = os.path.join(RUNS, "CY1"); r.short = {}
a1 = r.out("rev-parse", "a"); b1 = r.out("rev-parse", "b")
print("step 1: 'pull D O=a1' takes a1 only (O as it is in a1)")
print("  O in a1 == O in main (O itself unchanged)?", r.blob_at(a1, ".ait/O") == r.blob_at("main", ".ait/O"))
r.g("merge", "-q", "--no-edit", "--no-verify", a1)
print("  after: main has link D-O?", r.blob_at("HEAD", ".ait/undertake/D-O") is not None,
      " D is a1's?", r.blob_at("HEAD", ".ait/D") == r.blob_at(a1, ".ait/D"))
print("step 2: 'pull O' takes b1")
print("  commits of b1 not in HEAD:", [r.out("log", "-1", "--format=%s", c) for c in r.out("rev-list", b1, "^HEAD").split()])
show(r, ".ait/O", "O after step 1")
mt(r, b1, "b1")
