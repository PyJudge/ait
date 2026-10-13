from lib import *
import scen, flows, os, shutil
scen.H("CY1 again: 'pull D O' takes a1 and b1 in one command")
r = scen.base("CY1b", extra=[lambda r: (r.write("src/o.c", "o\n"), scen.cond(r, "O", "o0"), r.record("O", role="offer", provider="src/o.c"))])
r.co("c", True); r.record("D-O", role="undertake", state="undertaken"); r.commit("c: undertake D with O", "c1")
r.co("a", True); scen.cond(r, "D", "c-A"); r.record("D"); scen.line(r, 1, "A"); a1 = r.commit("a: amend D", "a1")
r.co("c"); r.co("b", True); scen.cond(r, "O", "o-B"); r.record("O", role="offer", provider="src/o.c"); scen.line(r, 9, "B"); b1 = r.commit("b: amend O", "b1")
r.co("main")
# merge a1, then b1, without touching the branch; then one commit with parents HEAD, a1, b1
t1 = r.out("merge-tree", "--write-tree", "HEAD", a1).splitlines()[0]
tmp = r.out("commit-tree", t1, "-p", "HEAD", "-p", a1, "-m", "tmp")
x = r.g("merge-tree", "--write-tree", tmp, b1, check=False)
t2 = x.stdout.splitlines()[0]
print("  second merge exit:", x.returncode)
m = r.out("commit-tree", t2, "-p", "HEAD", "-p", a1, "-p", b1, "-m", "pull D O")
r.g("update-ref", "refs/heads/main", m); r.g("reset", "-q", "--hard")
print("  parents of the pull commit:", [r.name(p) for p in r.out("rev-list", "--parents", "-n1", "HEAD").split()[1:]])
print("  validate (5.9 b-e):", flows.validate(r.path) or "pass")
for k in (".ait/D", ".ait/O", ".ait/undertake/D-O"):
    c, acc, lat = latest(r, k)
    head = r.blob_at("HEAD", k)
    print(f"  {k}: all latest contained in HEAD's -> {all(contains(r, head, v) for v in lat)}")
