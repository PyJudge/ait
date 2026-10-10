from lib import *
from scen import base, cond, line, H


def anc(repo, v):
    out, todo = set(), [v]
    while todo:
        o = todo.pop()
        if o in out:
            continue
        out.add(o)
        t = repo.cat(o)
        if t:
            todo += prevs(t)
    return out


def three_way(repo, a, b):
    """version-level 3-way: maximal common ancestors; if one side's body equals their (single) body, the other side moved forward"""
    common = anc(repo, a) & anc(repo, b)
    maxc = [c for c in common if not any(o != c and contains(repo, o, c) for o in common)]
    bodies = {body(repo.cat(c)) for c in maxc}
    if len(bodies) != 1:
        return f"maximal common {[c[:7] for c in maxc]} with {len(bodies)} bodies -> real divergence"
    B = bodies.pop()
    if body(repo.cat(a)) == B:
        return f"{a[:7]} unchanged since common {[c[:7] for c in maxc]} -> {b[:7]} moved forward (no decision)"
    if body(repo.cat(b)) == B:
        return f"{b[:7]} unchanged since common {[c[:7] for c in maxc]} -> {a[:7]} moved forward (no decision)"
    return f"both changed since common {[c[:7] for c in maxc]} -> real divergence"


H("T1  U1 then: main chose a1 (D@A over D@E); later a continues D@A2 from D@A")
r = base("T1")
r.co("a", True); cond(r, "D", "c-A"); vA = r.record("D"); a1 = r.commit("a: D c-A", "a1")
r.co("main"); cond(r, "D", "c-E"); vE = r.record("D"); r.commit("e: D c-E", "e1")
r.g("merge", "--no-commit", "--no-ff", a1, check=False)
r.write(".ait/D", body(r.cat(vA)) + "".join(f"prev {p}\n" for p in sorted([vA, vE]))); r.write("specs/d.yaml", "body: c-A\n")
vM = None; r.commit("pull D=a1", "pullA"); vM = r.blob_at("HEAD", ".ait/D")
r.co("a"); cond(r, "D", "c-A2"); vA2 = r.record("D"); r.commit("a: D c-A2", "a2")
r.co("main")
show(r, ".ait/D", "D")
print("  3-way:", three_way(r, vA2, vM))

H("T1b  same, but main had kept its own (D=HEAD); a continues D@A2")
r = base("T1b")
r.co("a", True); cond(r, "D", "c-A"); vA = r.record("D"); r.commit("a: D c-A", "a1")
r.co("main"); cond(r, "D", "c-E"); vE = r.record("D"); r.commit("e: D c-E", "e1")
r.write(".ait/D", body(r.cat(vE)) + "".join(f"prev {p}\n" for p in sorted([vA, vE]))); r.commit("pull D=HEAD", "kept"); vM = r.blob_at("HEAD", ".ait/D")
r.co("a"); cond(r, "D", "c-A2"); vA2 = r.record("D"); r.commit("a: D c-A2", "a2")
r.co("main")
print("  3-way:", three_way(r, vA2, vM))

H("X1  crossing: main changed Y and Z itself ; c changes Y and Z ; a1 (from c): Y again + D ; b1 (from c): Z again + O")
def ex(r):
    cond(r, "Y", "y0"); r.record("Y"); cond(r, "Z", "z0"); r.record("Z")
    r.write("src/o.c", "o\n"); cond(r, "O", "o0"); r.record("O", role="offer", provider="src/o.c")
r = base("X1", extra=[ex])
r.co("c", True); cond(r, "Y", "y-c"); r.record("Y"); cond(r, "Z", "z-c"); r.record("Z"); r.commit("c: Y, Z", "c1")
r.co("a", True); cond(r, "Y", "y-a"); r.record("Y"); cond(r, "D", "d-a"); r.record("D"); a1 = r.commit("a: Y, D", "a1")
r.co("c"); r.co("b", True); cond(r, "Z", "z-b"); r.record("Z"); cond(r, "O", "o-b"); r.record("O", role="offer", provider="src/o.c"); b1 = r.commit("b: Z, O", "b1")
r.co("main"); cond(r, "Y", "y-E"); r.record("Y"); cond(r, "Z", "z-E"); r.record("Z"); r.commit("e: Y, Z", "e1")
for k in "DOYZ":
    show(r, f".ait/{k}", k)
print("  taking a1 alone changes:", end=" "); mt(r, a1, "a1")
print("  taking b1 alone changes:", end=" "); mt(r, b1, "b1")
print("  Z in a1 is Z@c; is it a latest?", r.blob_at(a1, ".ait/Z") in latest(r, ".ait/Z")[2])
print("  Y in b1 is Y@c; is it a latest?", r.blob_at(b1, ".ait/Y") in latest(r, ".ait/Y")[2])
