from lib import *
from scen import base, cond, line, H
import shutil

H("CY1  c: D-O undertaken (shared ancestor, not in main) ; a1 (from c): D amended ; b1 (from c): O amended")
r = base("CY1", extra=[lambda r: (r.write("src/o.c", "o\n"), cond(r, "O", "o0"), r.record("O", role="offer", provider="src/o.c"))])
r.co("c", True); r.record("D-O", role="undertake", state="undertaken"); r.commit("c: undertake D with O", "c1")
r.co("a", True); cond(r, "D", "c-A"); r.record("D"); line(r, 1, "A"); a1 = r.commit("a: amend D, code line1", "a1")
r.co("c"); r.co("b", True); cond(r, "O", "o-B"); r.record("O", role="offer", provider="src/o.c"); line(r, 9, "B"); b1 = r.commit("b: amend O, code line9", "b1")
r.co("main")
show(r, ".ait/D", "D"); show(r, ".ait/O", "O"); show(r, ".ait/undertake/D-O", "link D-O")
print("  pull D alone takes a1:"); mt(r, a1, "a1")
print("  pull O alone takes b1:"); mt(r, b1, "b1")
print("  is a1 an ancestor of b1 or the reverse?",
      r.g("merge-base", "--is-ancestor", a1, b1, check=False).returncode == 0,
      r.g("merge-base", "--is-ancestor", b1, a1, check=False).returncode == 0)
# sequential two-way merges in both orders (code only matters here)
for order, (x, y) in (("a1 then b1", (a1, b1)), ("b1 then a1", (b1, a1))):
    t1 = r.out("merge-tree", "--write-tree", "HEAD", x).splitlines()[0]
    c1 = r.out("commit-tree", t1, "-p", "HEAD", "-p", x, "-m", "tmp")
    rr = r.g("merge-tree", "--write-tree", c1, y, check=False)
    t2 = rr.stdout.splitlines()[0]
    print(f"  {order}: second merge exit={rr.returncode}  stock.c={r.out('rev-parse', t2 + ':src/stock.c')[:7]}")
