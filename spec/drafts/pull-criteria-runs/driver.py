#!/usr/bin/env python3
"""v14 7.13.1 merge driver, steps 1, 2, 3 and 5 (step 4 -- both sides re-pinned a current -- is not needed here).
Called by git as: driver.py %O %A %B %P ; writes the result into %A."""
import subprocess, sys

base, ours, theirs, path = sys.argv[1:5]


def oid(p):
    return subprocess.run(["git", "hash-object", p], capture_output=True, text=True).stdout.strip()


def text_of_blob(o):
    r = subprocess.run(["git", "cat-file", "-p", o], capture_output=True, text=True)
    return r.stdout if r.returncode == 0 else None


def prevs(text):
    return [l[5:] for l in text.splitlines() if l.startswith("prev ") and l[5:] != "-"]


def dag(start_text):
    seen, todo = set(), prevs(start_text)
    while todo:
        o = todo.pop()
        if o in seen:
            continue
        seen.add(o)
        t = text_of_blob(o)
        if t is None:          # missing blob: this branch of the DAG ends here
            continue
        todo += prevs(t)
    return seen


A, B = open(ours).read(), open(theirs).read()
if A == B:                                   # 1
    sys.exit(0)
if oid(ours) in dag(B):                      # 2 theirs contains ours
    open(ours, "w").write(B)
    sys.exit(0)
if oid(theirs) in dag(A):                    # 3 ours contains theirs
    sys.exit(0)
open(ours, "w").write("<<<<<<< ours\n" + A + "=======\n" + B + ">>>>>>> theirs\n")   # 5
sys.exit(1)
