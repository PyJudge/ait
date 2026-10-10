"""Scenarios for pull-criteria revision 4 (decisions 1-18). Prints git facts and the data steps only;
the ait outcome is written by hand in pull-criteria.md."""
import sys, os, shutil
from lib import *
from scen import base, cond, line, resolve, restore_pinned, bundle_ok, H


def choose(r, rel, keep_blob, latest_blobs, msg, name, merge=None):
    """what pull writes for a choice (revision 5): merge `merge` (if any) without committing, then the record =
    body of keep_blob, check line by decision 22, prev = the latest versions (decisions 4, 13, 15), and its pinned spec files."""
    head = r.blob_at("HEAD", rel)
    if merge:
        r.g("merge", "--no-commit", "--no-ff", merge, check=False)
    b = body(r.cat(keep_blob))
    txt = compose(b, new_check_line(r, b, list(latest_blobs) + ([head] if head else [])), latest_blobs)
    r.write(rel, txt)
    restore_pinned(r, txt)
    return r.commit(msg, name)


def mtx(r, *heads):
    """merge HEAD with heads one by one (no octopus); returns final tree, conflicts per step"""
    cur = "HEAD"
    tree = None
    steps = []
    for h in heads:
        x = r.g("merge-tree", "--write-tree", "--name-only", cur, h, check=False)
        ls = x.stdout.strip().splitlines()
        tree = ls[0]
        conf = []
        if x.returncode == 1:
            for l in ls[1:]:
                if not l:
                    break
                conf.append(l)
        steps.append((r.name(h), x.returncode, conf))
        cur = r.out("commit-tree", tree, "-p", r.out("rev-parse", cur), "-p", h, "-m", "tmp")
    ch = changed_paths(r, r.out("rev-parse", "HEAD^{tree}"), tree)
    print(f"  merge {[r.name(h) for h in heads]} one by one: steps={steps}")
    print(f"     paths that differ from HEAD: {ch}")
    return tree, steps, ch


# ------------------------------------------------------------------ basic and unavoidable decisions
def U():
    H("U1  a1: D c-A ; main: D c-E")
    r = base("U1")
    r.co("a", True); cond(r, "D", "c-A"); vA = r.record("D"); a1 = r.commit("a: D c-A", "a1")
    r.co("main"); cond(r, "D", "c-E"); vE = r.record("D"); r.commit("e: D c-E", "e1")
    dshow(r, ".ait/D", "D")
    choose(r, ".ait/D", vA, [vA, vE], "pull D=a1", "pull", merge=a1)
    dshow(r, ".ait/D", "D after pull D=a1")
    print("  bundle:", bundle_ok(r, "HEAD"))

    H("T1  after U1's 'pull D=a1', a goes on: D c-A2 (a2)")
    r.co("a"); cond(r, "D", "c-A2"); r.record("D"); r.commit("a: D c-A2", "a2"); r.co("main")
    dshow(r, ".ait/D", "D")

    H("T1b  U1 but 'pull D=HEAD' (kept mine), then a goes on: D c-A2")
    r = base("T1b")
    r.co("a", True); cond(r, "D", "c-A"); vA = r.record("D"); r.commit("a: D c-A", "a1")
    r.co("main"); cond(r, "D", "c-E"); vE = r.record("D"); r.commit("e: D c-E", "e1")
    choose(r, ".ait/D", vE, [vA, vE], "pull D=HEAD", "kept")
    dshow(r, ".ait/D", "D right after kept")
    r.co("a"); cond(r, "D", "c-A2"); r.record("D"); r.commit("a: D c-A2", "a2"); r.co("main")
    dshow(r, ".ait/D", "D after a goes on")

    H("U3  a fulfils D (current) ; main: D c-E")
    r = base("U3")
    r.co("a", True); line(r, 1, "A"); r.record("D", state="current"); r.commit("a: fulfil D", "a1")
    r.co("main"); cond(r, "D", "c-E"); r.record("D"); r.commit("e: D c-E", "e1")
    dshow(r, ".ait/D", "D")

    H("U4  a cancels D ; main: D c-E  (terminal wins)")
    r = base("U4")
    r.co("a", True); r.record("D", state="cancelled"); r.commit("a: cancel D", "a1")
    r.co("main"); cond(r, "D", "c-E"); r.record("D"); r.commit("e: D c-E", "e1")
    dshow(r, ".ait/D", "D")

    H("U6  D current ; a and b re-check with different code ; main: D0")
    r = base("U6", d_state="current")
    r.co("a", True); line(r, 1, "A"); r.record("D", state="current"); r.commit("a: recheck", "a1")
    r.co("main"); r.co("b", True); line(r, 1, "B"); r.record("D", state="current"); r.commit("b: recheck", "b1")
    r.co("main")
    dshow(r, ".ait/D", "D")

    H("U7  D current ; a: drop X, fulfil X (D withdrawn) ; main reopens D  (terminal wins)")
    r = base("U7", d_state="current")
    r.co("a", True); r.record("X", role="drop", state="future", target="D"); r.commit("a: drop X", "a1")
    r.record("X", role="drop", state="fulfilled", target="D"); r.record("D", state="withdrawn"); a2 = r.commit("a: fulfil X", "a2")
    r.co("main"); r.record("D", state="future"); r.commit("e: reopen D", "e1")
    dshow(r, ".ait/D", "D")
    mtx(r, a2)

    H("U8  a1: D c-A ; main takes a1 ; a amends a1 to c-A2")
    r = base("U8")
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); r.commit("a: D c-A", "a1")
    r.co("main"); r.g("merge", "-q", "--ff-only", "a")
    r.co("a"); cond(r, "D", "c-A2"); r.record("D", prev=[r.blob_at("main~1", ".ait/D")]); r.g("add", "-A")
    r.g("commit", "-q", "--amend", "--no-verify", "-m", "a: D c-A2"); r.short[r.out("rev-parse", "HEAD")] = "a1'"
    r.co("main")
    dshow(r, ".ait/D", "D")


# ------------------------------------------------------------------ which commit
def G():
    H("G1  main has D@B ; a: D@A ; b merged a and kept B (bM)")
    r = base("G1")
    r.co("b", True); cond(r, "D", "c-B"); vB = r.record("D"); r.commit("b: D c-B", "b1")
    r.co("main"); r.g("merge", "-q", "--ff-only", "b")
    r.co("a", True); r.g("reset", "-q", "--hard", "init"); cond(r, "D", "c-A"); r.record("D"); r.commit("a: D c-A", "a1")
    r.co("b"); bM = resolve(r, "a", "D", vB, "b: merge a, keep B", "bM")
    r.co("main")
    dshow(r, ".ait/D", "D")
    gitmerge(r, "a", "a1")
    mtx(r, bM)

    H("G2  b and f each merged a and kept A ; main: D0")
    r = base("G2")
    r.co("a", True); cond(r, "D", "c-A"); vA = r.record("D"); r.commit("a: D c-A", "a1")
    r.co("main"); r.co("b", True); r.write("src/b.c", "b\n"); r.commit("b: code", "b0")
    cond(r, "D", "c-B"); r.record("D"); r.commit("b: D c-B", "b1"); resolve(r, "a", "D", vA, "b: keep A", "bM")
    r.co("main"); r.co("f", True); cond(r, "D", "c-F"); r.record("D"); r.commit("f: D c-F", "f1"); resolve(r, "a", "D", vA, "f: keep A", "fM")
    r.co("main")
    dshow(r, ".ait/D", "D")

    H("G4b  a: c-x then c-A ; main: c-A directly")
    r = base("G4b")
    r.co("a", True); cond(r, "D", "c-x"); r.record("D"); r.commit("a: c-x", "a1"); cond(r, "D", "c-A"); r.record("D"); r.commit("a: c-A", "a2")
    r.co("main"); cond(r, "D", "c-A"); r.record("D"); r.commit("e: c-A", "e1")
    dshow(r, ".ait/D", "D")

    H("TIE  a: x then same ; b: y then same ; equal new-commit counts")
    r = base("TIE")
    r.co("a", True); cond(r, "D", "x"); r.record("D"); r.commit("a: x", "a1"); cond(r, "D", "same"); r.record("D"); r.write("src/a.c", "A\n"); r.commit("a: same", "a2")
    r.co("main"); r.co("b", True); cond(r, "D", "y"); r.record("D"); r.commit("b: y", "b1"); cond(r, "D", "same"); r.record("D"); r.write("src/b.c", "B\n"); r.commit("b: same", "b2")
    r.co("main")
    dshow(r, ".ait/D", "D")

    H("X1  crossing: c changes Y,Z ; a1 (from c): Y,D ; b1 (from c): Z,O ; main changed Y,Z itself")
    def ex(r):
        cond(r, "Y", "y0"); r.record("Y"); cond(r, "Z", "z0"); r.record("Z")
        r.write("src/o.c", "o\n"); cond(r, "O", "o0"); r.record("O", role="offer", provider="src/o.c")
    r = base("X1", extra=[ex])
    r.co("c", True); cond(r, "Y", "y-c"); r.record("Y"); cond(r, "Z", "z-c"); r.record("Z"); r.commit("c: Y,Z", "c1")
    r.co("a", True); cond(r, "Y", "y-a"); r.record("Y"); cond(r, "D", "d-a"); r.record("D"); a1 = r.commit("a: Y,D", "a1")
    r.co("c"); r.co("b", True); cond(r, "Z", "z-b"); r.record("Z"); cond(r, "O", "o-b"); r.record("O", role="offer", provider="src/o.c"); b1 = r.commit("b: Z,O", "b1")
    r.co("main"); cond(r, "Y", "y-E"); r.record("Y"); cond(r, "Z", "z-E"); r.record("Z"); r.commit("e: Y,Z", "e1")
    for k in "DOYZ":
        dshow(r, f".ait/{k}", k, extra=[r.blob_at(a1, f".ait/{k}"), r.blob_at(b1, f".ait/{k}")])
    mtx(r, a1, b1)

    H("CY1  c: undertake D with O ; a1 (from c): D ; b1 (from c): O")
    r = base("CY1", extra=[lambda r: (r.write("src/o.c", "o\n"), cond(r, "O", "o0"), r.record("O", role="offer", provider="src/o.c"))])
    r.co("c", True); r.record("D-O", role="undertake", state="undertaken"); r.commit("c: undertake", "c1")
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); line(r, 1, "A"); a1 = r.commit("a: D", "a1")
    r.co("c"); r.co("b", True); cond(r, "O", "o-B"); r.record("O", role="offer", provider="src/o.c"); line(r, 9, "B"); b1 = r.commit("b: O", "b1")
    r.co("main")
    for k in (".ait/D", ".ait/O", ".ait/undertake/D-O"):
        dshow(r, k, k)
    print("  'pull D' alone:"); mtx(r, a1)
    print("  'pull D O':"); mtx(r, a1, b1)




# ------------------------------------------------------------------ naming, old contents, deleted records
def N():
    H("K7  main has D and O ; a1 only undertakes D with O ; 'pull D'")
    r = base("K7", extra=[lambda r: (r.write("src/o.c", "o\n"), cond(r, "O", "o"), r.record("O", role="offer", provider="src/o.c"))])
    r.co("a", True); r.record("D-O", role="undertake", state="undertaken"); r.commit("a: undertake", "a1")
    r.co("main")
    dshow(r, ".ait/D", "D"); dshow(r, ".ait/undertake/D-O", "link D-O (D is one end)")

    H("E1b  b1: D c-B ; b2: Q ; b3: git revert b1 ; main: D c-E ; 'pull Q'")
    r = base("E1b", extra=[lambda r: (cond(r, "Q", "q0"), r.record("Q"))])
    r.co("b", True); cond(r, "D", "c-B"); r.record("D"); b1 = r.commit("b: D c-B", "b1")
    cond(r, "Q", "q-B"); r.record("Q"); b2 = r.commit("b: Q", "b2")
    r.g("revert", "--no-edit", b1); r.short[r.out("rev-parse", "HEAD")] = "b3"
    r.co("main"); cond(r, "D", "c-E"); r.record("D"); r.commit("e: D c-E", "e1")
    dshow(r, ".ait/Q", "Q")
    dshow(r, ".ait/D", "D (pass 1: tips and HEAD)")
    dshow(r, ".ait/D", "D (with b2's D added)", extra=[r.blob_at(b2, ".ait/D")])

    H("RV2  main took b1 (D@B) ; b reverted b1 (b2) and changed Q (b3) ; 'pull Q'")
    r = base("RV2", extra=[lambda r: (cond(r, "Q", "q0"), r.record("Q"))])
    r.co("b", True); cond(r, "D", "c-B"); r.record("D"); b1 = r.commit("b: D c-B", "b1")
    r.co("main"); r.g("merge", "-q", "--ff-only", "b")
    r.co("b"); r.g("revert", "--no-edit", b1); r.short[r.out("rev-parse", "HEAD")] = "b2"
    cond(r, "Q", "q-B"); r.record("Q"); b3 = r.commit("b: Q", "b3")
    r.co("main")
    print("  HEAD is an ancestor of b3 (git would fast-forward):", r.g("merge-base", "--is-ancestor", "HEAD", b3, check=False).returncode == 0)
    dshow(r, ".ait/Q", "Q")
    d = dshow(r, ".ait/D", "D (with b3's D added)", extra=[r.blob_at(b3, ".ait/D")])
    print("  b3's D is contained in mine (old):", contains(r, r.blob_at("HEAD", ".ait/D"), r.blob_at(b3, ".ait/D")))
    mtx(r, b3)
    t = mtx.__defaults__ if False else None

    H("DEL1  a1: git rm .ait/Q ; a2: D ; 'pull D'")
    r = base("DEL1", extra=[lambda r: (cond(r, "Q", "q0"), r.record("Q"))])
    r.co("a", True); r.g("rm", "-q", ".ait/Q"); r.commit("a: rm Q", "a1"); cond(r, "D", "c-A"); r.record("D"); a2 = r.commit("a: D", "a2")
    r.co("main")
    mtx(r, a2)
    print("  a2 has .ait/Q?", r.blob_at(a2, ".ait/Q") is not None)

    H("RV3  main took a1 (D@A) then git-reverted it ; 'pull D'")
    r = base("RV3")
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); a1 = r.commit("a: D c-A", "a1")
    r.co("main"); r.g("merge", "-q", "--ff-only", "a"); r.g("revert", "--no-edit", "HEAD"); r.short[r.out("rev-parse", "HEAD")] = "e-revert"
    d = dshow(r, ".ait/D", "D")
    print("  a1 is an ancestor of HEAD:", r.g("merge-base", "--is-ancestor", a1, "HEAD", check=False).returncode == 0)


# ------------------------------------------------------------------ spec files are pull's
def S():
    H("KS1  main chose Q=HEAD over a1's spec change (input added) ; a2 then changes D ; 'pull D'")
    r = base("KS1", extra=[lambda r: (cond(r, "Q", "q0"), r.record("Q"))])
    r.co("a", True); r.write("specs/q.yaml", "body: q0\ninputs: [tests/qa.sh]\n"); r.write("tests/qa.sh", "true\n"); vQa = r.record("Q"); r.commit("a: Q input", "a1")
    r.co("main"); cond(r, "Q", "q-E"); vQe = r.record("Q"); r.commit("e: Q q-E", "e1")
    choose(r, ".ait/Q", vQe, [vQa, vQe], "pull Q=HEAD", "kept")
    r.co("a"); cond(r, "D", "c-A"); r.record("D"); a2 = r.commit("a: D", "a2"); r.co("main")
    dshow(r, ".ait/Q", "Q")
    tree, steps, ch = mtx(r, a2)
    print("  merged specs/q.yaml == Q's pin?", r.out("rev-parse", f"{tree}:specs/q.yaml") == [l for l in r.read('.ait/Q').splitlines() if l.startswith('file ')][0].split()[2])

    H("TF2  D and main's future K5 share shared.yaml ; a1 cancels D ; main changes D (s-E, K5 re-pinned) ; 'pull D'")
    r = base("TF2", extra=[lambda r: r.write("specs/shared.yaml", "body: s0\n")])
    r.record("D", spec="specs/shared.yaml"); r.commit("D shares", "s")
    r.co("a", True); r.record("D", spec="specs/shared.yaml", state="cancelled"); r.commit("a: cancel D", "a1")
    r.co("main"); r.record("K5", spec="specs/shared.yaml", provider="src/order.c"); r.commit("e: K5", "e0")
    r.write("specs/shared.yaml", "body: s-E\n"); r.record("D", spec="specs/shared.yaml"); r.record("K5", spec="specs/shared.yaml", provider="src/order.c"); r.commit("e: D s-E", "e1")
    dshow(r, ".ait/D", "D")
    mtx(r, "a")


# ------------------------------------------------------------------ check lines (decision 17)
def K():
    H("CK1  a1: D made current by hand, no check line")
    r = base("CK1")
    r.co("a", True); line(r, 1, "A"); r.record("D", state="current", check=None); r.commit("a: current without check", "a1")
    r.co("main")
    dshow(r, ".ait/D", "D")

    H("CK2  a1: fulfil D with -r (check line), then the message is rewritten (trailer gone)")
    r = base("CK2")
    r.co("a", True); line(r, 1, "A"); r.record("D", state="current"); r.commit("a: fulfil\n\nAit-Check: D reported x", "a1")
    r.g("commit", "-q", "--amend", "--no-verify", "-m", "a: fulfil"); r.short[r.out("rev-parse", "HEAD")] = "a1'"
    r.co("main")
    print("  trailer left in a:", repr(r.out("log", "-1", "--format=%(trailers:key=Ait-Check,valueonly)", "a")))
    dshow(r, ".ait/D", "D")

    H("CK3  D current ; a1: spec and pins changed by hand, old check line kept")
    r = base("CK3", d_state="current")
    r.co("a", True); cond(r, "D", "c-hand")
    t = r.read(".ait/D"); old_ck = [l for l in t.splitlines() if l.startswith("check ")][0]
    r.record("D", state="current", check=None)
    t2 = r.read(".ait/D").replace("creator", old_ck + "\ncreator", 1); r.write(".ait/D", t2)
    r.commit("a: hand edit", "a1")
    r.co("main")
    dshow(r, ".ait/D", "D")

    H("NU1  D current (a1 fulfils) ; a2 plain commit changes D's code ; a3 changes it again")
    r = base("NU1")
    r.co("a", True); line(r, 1, "A"); r.record("D", state="current"); r.commit("a: fulfil", "a1")
    line(r, 2, "X"); r.commit("a: plain code change", "a2"); line(r, 2, "Y"); r.commit("a: plain code change again", "a3")
    for c in ("a~2", "a~1", "a"):
        print(f"  {c}: .ait/D={r.blob_at(c, '.ait/D')[:7]} stock.c={r.blob_at(c, 'src/stock.c')[:7]}")


# ------------------------------------------------------------------ several ends, working tree
def M():
    H("MH2  'pull D Q' : D from a1, Q from b1 ; a1 and b1 change src/x.c differently")
    r = base("MH2", extra=[lambda r: (cond(r, "Q", "q0"), r.record("Q"), r.write("src/x.c", "x=0\n"))])
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); r.write("src/x.c", "x=A\n"); a1 = r.commit("a", "a1")
    r.co("main"); r.co("b", True); cond(r, "Q", "q-B"); r.record("Q"); r.write("src/x.c", "x=B\n"); b1 = r.commit("b", "b1")
    r.co("main")
    mtx(r, a1, b1) if r.out("rev-parse", a1) < r.out("rev-parse", b1) else mtx(r, b1, a1)

    H("WT1  a1 changes D's provider only ; main changed D's condition ; main has an uncommitted draft in specs/d.yaml")
    r = base("WT1")
    r.co("a", True); line(r, 3, "A"); r.record("D"); a1 = r.commit("a", "a1")
    r.co("main"); cond(r, "D", "c-E"); r.record("D"); r.commit("e", "e1"); r.write("specs/d.yaml", "body: draft\n")
    x = r.g("merge", "--no-commit", "--no-ff", a1, check=False)
    print("  git merge exit:", x.returncode, " status:", r.out("status", "--short").replace("\n", " | "))
    r.g("merge", "--abort", check=False); r.g("checkout", "--", "specs/d.yaml")

    H("NEW2  a1 makes Q (specs/q.yaml) ; main has an untracked specs/q.yaml")
    r = base("NEW2")
    r.co("a", True); cond(r, "Q", "q"); r.record("Q"); a1 = r.commit("a", "a1")
    r.co("main"); r.write("specs/q.yaml", "my data\n")
    x = r.g("merge", "--ff-only", a1, check=False)
    print("  git merge exit:", x.returncode, (x.stderr.strip().splitlines() or [""])[0])


def Z():
    H("NT2  a1: D c-A, b1: D c-B ; A and B both 'pull D=HEAD' at the same time")
    r = base("NT2")
    r.co("a", True); cond(r, "D", "c-A"); vA = r.record("D"); a1 = r.commit("a: c-A", "a1")
    r.co("main"); r.co("b", True); cond(r, "D", "c-B"); vB = r.record("D"); b1 = r.commit("b: c-B", "b1")
    r.co("a"); choose(r, ".ait/D", vA, [vA, vB], "a: pull D=HEAD", "a2")
    r.co("b"); choose(r, ".ait/D", vB, [vA, vB], "b: pull D=HEAD", "b2")
    r.co("main")
    dshow(r, ".ait/D", "D seen from main")
    H("NT3  NT2, then A keeps its own again (a3) and B pulls D")
    r.co("a"); vA2 = r.blob_at("HEAD", ".ait/D"); vB2 = r.blob_at("b", ".ait/D")
    choose(r, ".ait/D", vA2, [vA2, vB2], "a: pull D=HEAD again", "a3")
    r.co("b")
    dshow(r, ".ait/D", "D seen from b")
    r.co("main")

    H("KB2  after the hint 'D=a1 or D=b1', b merges a and keeps B (bM) ; main then runs 'pull D=a1'")
    r = base("KB2")
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); r.commit("a: c-A", "a1")
    r.co("main"); r.co("b", True); cond(r, "D", "c-B"); vB = r.record("D"); r.commit("b: c-B", "b1")
    resolve(r, "a", "D", vB, "b: merge a, keep B", "bM"); r.co("main")
    dshow(r, ".ait/D", "D")
    H("KB3  KB2, main takes bM, then 'pull D=HEAD'")
    r.g("merge", "-q", "--ff-only", "b")
    dshow(r, ".ait/D", "D")

    H("IND1  D current, undertaken by main's current O ; a1 re-checks D with line1 ; main re-checked with line9")
    def ex(r):
        r.write("src/o.c", "o\n"); cond(r, "O", "o0")
        r.record("O", role="offer", state="current", provider="src/o.c")
        r.record("D-O", role="undertake", state="undertaken")
    r = base("IND1", d_state="current", extra=[ex])
    r.co("a", True); line(r, 1, "A"); r.record("D", state="current"); a1 = r.commit("a: recheck", "a1")
    r.co("main"); line(r, 9, "E"); r.record("D", state="current"); r.commit("e: recheck", "e1")
    dshow(r, ".ait/D", "D")
    tree, steps, ch = mtx(r, a1)
    pin = [l for l in r.cat(r.blob_at(a1, ".ait/D")).splitlines() if l.startswith("provider")][0].split()[2]
    print("  merged stock.c == a1's pin?", r.out("rev-parse", f"{tree}:src/stock.c") == pin)

    H("IDX1  main has its own commit and a staged new file src/util.c ; 'pull D' (not a fast-forward)")
    r = base("IDX1")
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); a1 = r.commit("a", "a1")
    r.co("main"); r.write("src/m.c", "m\n"); r.commit("e: code", "e1"); r.write("src/util.c", "u\n"); r.g("add", "src/util.c")
    x = r.g("merge", "--no-commit", "--no-ff", a1, check=False)
    print("  git merge exit:", x.returncode, (x.stderr.strip().splitlines() or [""])[0][:90])

    H("SH1  a: D c-A then a code-only commit ; main is a depth=1 clone of a")
    r = base("SH1")
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); r.commit("a: D", "a1"); r.write("src/z.c", "z\n"); r.commit("a: code", "a2")
    r.co("main")
    cl = r.path + "-shallow"; shutil.rmtree(cl, ignore_errors=True)
    run(RUNS, "git", "clone", "-q", "--depth=1", "--no-single-branch", "file://" + r.path, cl)
    c = Repo.__new__(Repo); c.path = cl; c.short = {}
    run(cl, "git", "checkout", "-q", "-b", "e", "origin/main")
    print("  shallow: commits of origin/a visible:", c.out("rev-list", "--count", "origin/a"), " (a really has 3)")
    print("  introducing commits of a's D as computed here:", [(cc[:7], n) for cc, n in introducing(c, ".ait/D", c.blob_at("origin/a", ".ait/D"))])
    x = c.g("merge-tree", "--write-tree", "HEAD", "origin/a", check=False)
    print("  merge-tree exit:", x.returncode, (x.stderr.strip().splitlines() or [""])[0][:80])


if __name__ == "__main__":
    for f in sys.argv[1:]:
        globals()[f]()
