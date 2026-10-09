"""Scenarios for pull-criteria (r26). Each prints git facts and the data definitions; the expected ait outcome
is written by hand in pull-criteria.md from these facts."""
import sys
from lib import *

STOCK = "".join(f"line{i} v=0\n" for i in range(1, 10))


def base(name, d_state="future", extra=()):
    r = Repo(name)
    r.write("src/stock.c", STOCK)
    r.write("src/order.c", "order\n")
    r.write("specs/d.yaml", "body: c0\n")
    r.record("D", state=d_state)
    for e in extra:
        e(r)
    msg = "init"
    if d_state == "current":
        msg += "\n\nAit-Check: D reported pins0"
    r.commit(msg, "init")
    r.g("tag", "init")
    return r


def cond(r, id, text, spec=None):
    r.write(spec or f"specs/{id.lower()}.yaml", f"body: {text}\n")


def line(r, n, v, path="src/stock.c"):
    t = r.read(path).splitlines(True)
    t[n - 1] = f"line{n} v={v}\n"
    r.write(path, "".join(t))


def resolve(r, other, id, keep_body_of, msg, name):
    """merge `other`; the driver conflicts on .ait/<id>; resolve as an ait conflict resolution:
    body of `keep_body_of` (a blob), prev = the two maximal versions."""
    r.g("merge", "--no-commit", "--no-ff", other, check=False)
    ours = r.blob_at("HEAD", f".ait/{id}")
    theirs = r.blob_at(other, f".ait/{id}")
    t = body(r.cat(keep_body_of)) + "".join(f"prev {p}\n" for p in sorted([ours, theirs]))
    r.write(f".ait/{id}", t)
    return r.commit(msg, name)


def H(t):
    print("\n" + "=" * 8, t)


# ------------------------------------------------------------------ basic
def A():
    H("A1/A2  new contract Q made on a; main has none")
    r = base("A1")
    r.co("a", True)
    cond(r, "Q", "q-A"); r.record("Q"); r.commit("a: new Q", "a1")
    r.co("main")
    show(r, ".ait/Q", "Q")
    mt(r, "a")
    r.g("merge", "-q", "--ff-only", "a")
    print("  -- after main fast-forwards to a (A2):")
    show(r, ".ait/Q", "Q")

    H("A3  pull D Q: D has one latest, Q has two")
    r = base("A3", extra=[lambda r: (cond(r, "Q", "q0"), r.record("Q"))])
    r.co("a", True)
    cond(r, "D", "c-A"); r.record("D"); cond(r, "Q", "q-A"); r.record("Q"); r.commit("a: D, Q", "a1")
    r.co("main"); r.co("b", True)
    cond(r, "Q", "q-B"); r.record("Q"); r.commit("b: Q", "b1")
    r.co("main")
    show(r, ".ait/D", "D"); show(r, ".ait/Q", "Q")


# ------------------------------------------------------------------ U: decisions that cannot be avoided
def U():
    H("U1  a: D cond c-A ; main: D cond c-E")
    r = base("U1")
    r.co("a", True); cond(r, "D", "c-A"); vA = r.record("D"); a1 = r.commit("a: D c-A", "a1")
    r.co("main"); cond(r, "D", "c-E"); vE = r.record("D"); r.commit("e: D c-E", "e1")
    show(r, ".ait/D", "D")
    mt(r, a1, "a1")
    print("  -- simulate 'pull D=a1': merge a1, .ait/D = body(D@A) with prev {D@A, D@E}")
    r.g("merge", "--no-commit", "--no-ff", a1, check=False)
    r.write(".ait/D", body(r.cat(vA)) + "".join(f"prev {p}\n" for p in sorted([vA, vE])))
    r.write("specs/d.yaml", "body: c-A\n")
    r.commit("pull D=a1", "pull")
    show(r, ".ait/D", "D after pull D=a1")
    print("  rule1 of the new version:", rule1(r, r.blob_at("HEAD", ".ait/D")))

    H("U1b  same, but 'pull D=HEAD' (keep mine): a commit on main, prev {D@E, D@A}, nothing merged")
    r = base("U1b")
    r.co("a", True); cond(r, "D", "c-A"); vA = r.record("D"); r.commit("a: D c-A", "a1")
    r.co("main"); cond(r, "D", "c-E"); vE = r.record("D"); r.commit("e: D c-E", "e1")
    r.write(".ait/D", body(r.cat(vE)) + "".join(f"prev {p}\n" for p in sorted([vA, vE])))
    r.commit("pull D=HEAD", "pull")
    show(r, ".ait/D", "D after pull D=HEAD")
    print("  a's history in main?", r.g("merge-base", "--is-ancestor", "a", "HEAD", check=False).returncode == 0)

    H("U2  a: D@A ; b: D@B then resolves a to B side (D@B') ; f: D@F then resolves a to F side (D@F') ; main: D0")
    r = base("U2")
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); r.commit("a: D c-A", "a1")
    r.co("main"); r.co("b", True); cond(r, "D", "c-B"); vB = r.record("D"); r.commit("b: D c-B", "b1")
    resolve(r, "a", "D", vB, "b: merge a, keep B", "bM")
    r.co("main"); r.co("f", True); cond(r, "D", "c-F"); vF = r.record("D"); r.commit("f: D c-F", "f1")
    resolve(r, "a", "D", vF, "f: merge a, keep F", "fM")
    r.co("main")
    show(r, ".ait/D", "D")

    H("U3  a: fulfils D (current, pins new stock.c) ; main: D cond c-E")
    r = base("U3")
    r.co("a", True); line(r, 1, "A"); r.record("D", state="current"); r.commit("a: fulfil D\n\nAit-Check: D reported pinsA", "a1")
    r.co("main"); cond(r, "D", "c-E"); r.record("D"); r.commit("e: D c-E", "e1")
    show(r, ".ait/D", "D")

    H("U4  a: cancels D ; main: D cond c-E")
    r = base("U4")
    r.co("a", True); r.record("D", state="cancelled"); r.commit("a: cancel D", "a1")
    r.co("main"); cond(r, "D", "c-E"); r.record("D"); r.commit("e: D c-E", "e1")
    show(r, ".ait/D", "D")

    H("U6  D current ; a and b each change the code and re-check (same condition) ; main: D0")
    r = base("U6", d_state="current")
    r.co("a", True); line(r, 1, "A"); r.record("D", state="current"); r.commit("a: recheck\n\nAit-Check: D reported pinsA", "a1")
    r.co("main"); r.co("b", True); line(r, 1, "B"); r.record("D", state="current"); r.commit("b: recheck\n\nAit-Check: D reported pinsB", "b1")
    r.co("main")
    show(r, ".ait/D", "D")

    H("U7  D current ; a: drop X on D, fulfils X (D withdrawn) ; main: reopens D (future)")
    r = base("U7", d_state="current")
    r.co("a", True)
    r.record("X", role="drop", state="future", target="D"); r.commit("a: drop X", "a1")
    r.record("X", role="drop", state="fulfilled", target="D"); r.record("D", state="withdrawn"); a2 = r.commit("a: fulfil X", "a2")
    r.co("main"); r.record("D", state="future"); r.commit("e: reopen D", "e1")
    show(r, ".ait/D", "D")
    mt(r, a2, "a2 (for pull D=a2)")

    H("U8  a1: D@A ; main takes a1 ; a amends a1 (D@A2, prev D0) ; main pulls D")
    r = base("U8")
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); r.commit("a: D c-A", "a1")
    r.co("main"); r.g("merge", "-q", "--ff-only", "a")
    r.co("a"); cond(r, "D", "c-A2"); r.record("D", prev=[r.blob_at("main~1", ".ait/D")]); r.g("add", "-A")
    r.g("commit", "-q", "--amend", "--no-verify", "-m", "a: D c-A2"); r.short[r.out("rev-parse", "HEAD")] = "a1'"
    r.co("main")
    show(r, ".ait/D", "D")


# ------------------------------------------------------------------ G: which commit to take
def G():
    H("G1  main has D@B (from b) ; a: D@A ; b resolves a to B side (D@B')")
    r = base("G1")
    r.co("b", True); cond(r, "D", "c-B"); vB = r.record("D"); r.commit("b: D c-B", "b1")
    r.co("main"); r.g("merge", "-q", "--ff-only", "b")
    r.co("a", True); r.g("reset", "-q", "--hard", "init"); cond(r, "D", "c-A"); r.record("D"); r.commit("a: D c-A", "a1")
    r.co("b"); bM = resolve(r, "a", "D", vB, "b: merge a, keep B", "bM")
    r.co("main")
    show(r, ".ait/D", "D")
    gitmerge(r, "a", "a (the commit of D@A)")
    mt(r, bM, "bM")

    H("G2  a: D@A ; b: D@B, resolves a to A side (D@B') ; f: D@F, resolves a to A side (D@F') ; main: D0")
    r = base("G2")
    r.co("a", True); cond(r, "D", "c-A"); vA = r.record("D"); r.commit("a: D c-A", "a1")
    r.co("main"); r.co("b", True); r.write("src/b.c", "b\n"); r.commit("b: code", "b0")
    cond(r, "D", "c-B"); r.record("D"); r.commit("b: D c-B", "b1")
    bM = resolve(r, "a", "D", vA, "b: merge a, keep A", "bM")
    r.co("main"); r.co("f", True); cond(r, "D", "c-F"); r.record("D"); r.commit("f: D c-F", "f1")
    fM = resolve(r, "a", "D", vA, "f: merge a, keep A", "fM")
    r.co("main")
    lat = show(r, ".ait/D", "D")
    print("  git merge b then f (main):")
    wt = r.path + "-wt2"
    shutil.rmtree(wt, ignore_errors=True)
    r.g("worktree", "add", "-q", "--detach", wt, "HEAD")
    x = run(wt, "git", "merge", "--no-edit", "--no-verify", "b", check=False)
    y = run(wt, "git", "merge", "--no-edit", "--no-verify", "f", check=False)
    print(f"     merge b exit={x.returncode} ; merge f exit={y.returncode} unmerged={run(wt, 'git', 'diff', '--name-only', '--diff-filter=U', check=False).stdout.split()}")
    run(wt, "git", "merge", "--abort", check=False); r.g("worktree", "remove", "--force", wt)
    print("  -- simulate pull D: one body; take the introducing commit with fewest new commits (fM), prev {D@B', D@F'}")
    r.g("merge", "--no-commit", "--no-ff", fM, check=False)
    r.write(".ait/D", body(r.cat(vA)) + "".join(f"prev {p}\n" for p in sorted(lat)))
    r.commit("pull D", "pull")
    show(r, ".ait/D", "D after pull")

    H("G3  main has D@A (from a) ; b: D@B then resolves a to B side (D@B')")
    r = base("G3")
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); r.commit("a: D c-A", "a1")
    r.co("main"); r.g("merge", "-q", "--ff-only", "a")
    r.co("b", True); r.g("reset", "-q", "--hard", "init"); cond(r, "D", "c-B"); vB = r.record("D"); b1 = r.commit("b: D c-B", "b1")
    bM = resolve(r, "a", "D", vB, "b: merge a, keep B", "bM")
    r.co("main")
    show(r, ".ait/D", "D")
    gitmerge(r, b1, "b1 (the commit of D@B)")
    gitmerge(r, bM, "bM (the commit of D@B')")


# ------------------------------------------------------------------ N: every changed contract must be named
def N():
    H("N1  a: a1 changes Q, a2 changes D ; main pulls D")
    r = base("N1", extra=[lambda r: (cond(r, "Q", "q0"), r.record("Q"))])
    r.co("a", True); cond(r, "Q", "q-A"); r.record("Q"); r.commit("a: Q", "a1")
    cond(r, "D", "c-A"); r.record("D"); r.commit("a: D", "a2")
    r.co("main")
    show(r, ".ait/D", "D")
    mt(r, "a", "a2")

    H("W1  a: a0 code-only commit (src/util.c), a1 changes D")
    r = base("W1")
    r.co("a", True); r.write("src/util.c", "util\n"); r.commit("a: util", "a0")
    cond(r, "D", "c-A"); r.record("D"); r.commit("a: D", "a1")
    r.co("main")
    show(r, ".ait/D", "D")
    mt(r, "a", "a1")

    H("W2  b: offer O2 made and fulfilled, then D@B")
    r = base("W2")
    r.co("b", True); r.write("src/o2.c", "o2\n"); cond(r, "O2", "o2"); r.record("O2", role="offer", provider="src/o2.c"); r.commit("b: O2", "b1")
    r.write("src/o2.c", "o2 impl\n"); r.record("O2", role="offer", state="current", provider="src/o2.c"); r.commit("b: fulfil O2\n\nAit-Check: O2 reported p", "b2")
    cond(r, "D", "c-B"); r.record("D"); r.commit("b: D", "b3")
    r.co("main")
    show(r, ".ait/D", "D")
    mt(r, "b", "b3")

    H("K1  a: makes offer O and undertakes D with it, then fulfils O ; main pulls O")
    r = base("K1")
    r.co("a", True); r.write("src/o.c", "o\n"); cond(r, "O", "o"); r.record("O", role="offer", provider="src/o.c")
    r.record("D-O", role="undertake", state="undertaken"); r.commit("a: offer O, undertake D", "a1")
    r.write("src/o.c", "o impl\n"); r.record("O", role="offer", state="current", provider="src/o.c"); r.commit("a: fulfil O\n\nAit-Check: O reported p", "a2")
    r.co("main")
    show(r, ".ait/O", "O")
    mt(r, "a", "a2")

    H("K6  O is main's offer (future in base) ; a undertakes D with O and changes D ; main fulfils O ; main pulls D")
    r = base("K6", extra=[lambda r: (r.write("src/o.c", "o\n"), cond(r, "O", "o"), r.record("O", role="offer", provider="src/o.c", creator="agent-e S9"))])
    r.co("a", True); r.record("D-O", role="undertake", state="undertaken"); r.commit("a: undertake D with O", "a1")
    cond(r, "D", "c-A"); r.record("D"); r.commit("a: D c-A", "a2")
    r.co("main"); r.write("src/o.c", "o impl\n"); r.record("O", role="offer", state="current", provider="src/o.c", creator="agent-e S9"); r.commit("e: fulfil O\n\nAit-Check: O reported p", "e1")
    show(r, ".ait/D", "D")
    tree, rc, conf, ch = mt(r, "a", "a2")
    print("  merged state: O =", fields(r.cat(r.out("rev-parse", f"{tree}:.ait/O")))["state"], " D =", fields(r.cat(r.out("rev-parse", f"{tree}:.ait/D")))["state"],
          " D-O =", fields(r.cat(r.out("rev-parse", f"{tree}:.ait/undertake/D-O")))["state"])

    H("K7  main has D and O ; a only undertakes D with O")
    r = base("K7", extra=[lambda r: (r.write("src/o.c", "o\n"), cond(r, "O", "o"), r.record("O", role="offer", provider="src/o.c"))])
    r.co("a", True); r.record("D-O", role="undertake", state="undertaken"); r.commit("a: undertake", "a1")
    r.co("main")
    show(r, ".ait/D", "D"); show(r, ".ait/O", "O"); show(r, ".ait/undertake/D-O", "link D-O")

    H("N5  D current, main's O_E current undertakes D ; a reopens D")
    def ex(r):
        r.write("src/oe.c", "oe\n"); cond(r, "OE", "oe")
        r.record("OE", role="offer", state="current", provider="src/oe.c", creator="agent-e S9")
        r.record("D-OE", role="undertake", state="undertaken")
    r = base("N5", d_state="current", extra=[ex])
    r.co("a", True); r.record("D", state="future"); r.commit("a: reopen D", "a1")
    r.co("main")
    show(r, ".ait/D", "D")
    mt(r, "a", "a1")


# ------------------------------------------------------------------ C/S: what my contracts and the received one checked
def C():
    H("C1  a fulfils D (line1 -> A, pins it) ; main did not touch stock.c")
    r = base("C1")
    r.co("a", True); line(r, 1, "A"); r.record("D", state="current"); r.commit("a: fulfil D\n\nAit-Check: D reported pinsA", "a1")
    gA = r.blob_at("HEAD", "src/stock.c")
    r.co("main")
    tree, *_ = mt(r, "a", "a1")
    print("  merged stock.c == pinned gA:", r.out("rev-parse", f"{tree}:src/stock.c") == gA)
    print("  check record in a1's history:", "Ait-Check: D" in r.out("log", "-1", "--format=%B", "a"))

    H("C2  as C1, but main changed line9 of stock.c")
    r = base("C2")
    r.co("a", True); line(r, 1, "A"); r.record("D", state="current"); r.commit("a: fulfil D\n\nAit-Check: D reported pinsA", "a1")
    gA = r.blob_at("HEAD", "src/stock.c")
    r.co("main"); line(r, 9, "E"); r.commit("e: line9", "e1")
    tree, *_ = mt(r, "a", "a1")
    print("  merged stock.c == pinned gA:", r.out("rev-parse", f"{tree}:src/stock.c") == gA)

    H("C3  as C1, but main changed line1 differently")
    r = base("C3")
    r.co("a", True); line(r, 1, "A"); r.record("D", state="current"); r.commit("a: fulfil D\n\nAit-Check: D reported pinsA", "a1")
    r.co("main"); line(r, 1, "E"); r.commit("e: line1", "e1")
    mt(r, "a", "a1")

    H("C4  a changes D (future) and src/other.c ; main changed src/other.c differently (no contract checks other.c)")
    r = base("C4", extra=[lambda r: r.write("src/other.c", "x=0\n")])
    r.co("a", True); r.write("src/other.c", "x=A\n"); cond(r, "D", "c-A"); r.record("D"); r.commit("a: D, other", "a1")
    r.co("main"); r.write("src/other.c", "x=E\n"); r.commit("e: other", "e1")
    mt(r, "a", "a1")

    H("S1  main's own future K5 shares specs/shared.yaml with D ; a changes that spec (D@A)")
    def ex(r):
        r.write("specs/shared.yaml", "body: s0\n")
    r = base("S1", extra=[ex])
    r.record("D", spec="specs/shared.yaml"); r.commit("D declares shared", "s")
    r.co("a", True); r.write("specs/shared.yaml", "body: s-A\n"); r.record("D", spec="specs/shared.yaml"); r.commit("a: shared spec", "a1")
    r.co("main"); r.record("K5", spec="specs/shared.yaml", provider="src/order.c", creator="agent-e S9"); r.commit("e: K5", "e1")
    tree, *_ = mt(r, "a", "a1")
    k5pin = [l for l in r.read(".ait/K5").splitlines() if l.startswith("file ")][0].split()[3]
    print("  merged shared.yaml blob:", r.out("rev-parse", f"{tree}:specs/shared.yaml")[:7], " K5 pins:", k5pin[:7])

    H("S3  main's current K6 provides src/stock.c (pinned g0) ; a fulfils D changing stock.c line1")
    r = base("S3")
    r.co("a", True); line(r, 1, "A"); r.record("D", state="current"); r.commit("a: fulfil D\n\nAit-Check: D reported pinsA", "a1")
    r.co("main"); cond(r, "K6", "k6"); r.record("K6", state="current", provider="src/stock.c", creator="agent-e S9"); r.commit("e: K6\n\nAit-Check: K6 reported p", "e1")
    g0 = r.blob_at("HEAD", "src/stock.c")
    tree, *_ = mt(r, "a", "a1")
    print("  merged stock.c == K6's pin g0:", r.out("rev-parse", f"{tree}:src/stock.c") == g0)


# ------------------------------------------------------------------ R: rule 1 and rule 2
def R():
    H("R1  a: cancels D (a1), then hand-edits it back to future with prev a1 (a2: not an ait transition)")
    r = base("R1")
    r.co("a", True); r.record("D", state="cancelled"); r.commit("a: cancel", "a1")
    r.record("D", state="future"); r.commit("a: hand edit", "a2")
    r.co("main")
    show(r, ".ait/D", "D")

    H("R2  a1: D@A ; main takes a1 ; a: git revert a1 (back to D0 blob)")
    r = base("R2")
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); r.commit("a: D c-A", "a1")
    r.co("main"); r.g("merge", "-q", "--ff-only", "a")
    r.co("a"); r.g("revert", "--no-edit", "HEAD"); r.short[r.out("rev-parse", "HEAD")] = "a2(revert)"
    print("  a's D after revert == init's D:", r.blob_at("a", ".ait/D") == r.blob_at("init" if False else "main~1", ".ait/D"))
    r.co("main")
    show(r, ".ait/D", "D (main took a1 before the revert)")

    H("R2b  same, main never took a1")
    r = base("R2b")
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); r.commit("a: D c-A", "a1")
    r.g("revert", "--no-edit", "HEAD")
    r.co("main")
    show(r, ".ait/D", "D")

    H("R3  a1: D@A ; main takes a1 ; a: revert (a2), undo the revert (a3), then reopens/amends back to c0 with ait (a4)")
    r = base("R3")
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); r.commit("a: D c-A", "a1")
    r.co("main"); r.g("merge", "-q", "--ff-only", "a")
    r.co("a"); r.g("revert", "--no-edit", "HEAD"); r.short[r.out("rev-parse", "HEAD")] = "a2(revert)"
    r.g("revert", "--no-edit", "HEAD"); r.short[r.out("rev-parse", "HEAD")] = "a3(undo)"
    cond(r, "D", "c0"); r.record("D"); a4 = r.commit("a: amend D back to c0 (ait)", "a4")
    r.co("main")
    show(r, ".ait/D", "D")
    mt(r, a4, "a4")


# ------------------------------------------------------------------ copies, several heads, missing objects
def X():
    H("CP1  a1: D@A ; b: code b0 then cherry-pick of a1 (b1, same blob)")
    r = base("CP1")
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); r.commit("a: D c-A", "a1")
    r.co("main"); r.co("b", True); r.write("src/b.c", "b\n"); r.commit("b: code", "b0")
    r.g("cherry-pick", "a"); r.short[r.out("rev-parse", "HEAD")] = "b1(pick)"
    r.co("main")
    show(r, ".ait/D", "D")

    H("MH1/MH2  pull D Q: D from a, Q from b (two heads)")
    for name, clash in (("MH1", False), ("MH2", True)):
        r = base(name, extra=[lambda r: (cond(r, "Q", "q0"), r.record("Q"), r.write("src/x.c", "x=0\n"))])
        r.co("a", True); cond(r, "D", "c-A"); r.record("D")
        if clash:
            r.write("src/x.c", "x=A\n")
        r.commit("a: D", "a1")
        r.co("main"); r.co("b", True); cond(r, "Q", "q-B"); r.record("Q")
        if clash:
            r.write("src/x.c", "x=B\n")
        r.commit("b: Q", "b1")
        r.co("main")
        wt = r.path + "-wt"
        shutil.rmtree(wt, ignore_errors=True)
        r.g("worktree", "add", "-q", "--detach", wt, "HEAD")
        x = run(wt, "git", "merge", "--no-edit", "--no-verify", "a", "b", check=False)
        print(f"  {name}: git merge a b (octopus) exit={x.returncode} {x.stdout.strip().splitlines()[-1:]} {x.stderr.strip().splitlines()[-1:]}")
        run(wt, "git", "merge", "--abort", check=False); run(wt, "git", "reset", "-q", "--hard", check=False)
        r.g("worktree", "remove", "--force", wt)

    H("M1  a: D@B then D@B2 (prev D@B); a's history rewritten so no commit holds D@B ; main: D0")
    r = base("M1")
    r.co("a", True); cond(r, "D", "c-B"); r.record("D"); r.commit("a: D@B", "a1")
    cond(r, "D", "c-B2"); r.record("D"); r.commit("a: D@B2", "a2")
    t = r.out("commit-tree", "HEAD^{tree}", "-p", "init", "-m", "a: rewritten")
    r.g("update-ref", "refs/heads/a", t); r.short[t] = "a'"
    r.g("reflog", "expire", "--expire=now", "--all")
    r.co("main")
    show(r, ".ait/D", "D (same repository; the blob of D@B is still in the object store)")
    clone = r.path + "-clone"
    shutil.rmtree(clone, ignore_errors=True)
    run(RUNS, "git", "clone", "-q", "--no-local", r.path, clone)
    c = Repo.__new__(Repo); c.path = clone; c.short = r.short
    run(clone, "git", "checkout", "-q", "-b", "e", "origin/main")
    show(c, ".ait/D", "D (a fresh clone: D@B never fetched)")




def E():
    H("G4  a: D@A (c-A) ; main: D@E with the same condition c-A, made separately (same body, different prev? no: same prev D0 -> same blob)")
    r = base("G4")
    r.co("a", True); r.write("src/a.c", "a\n"); r.commit("a: code", "a0"); cond(r, "D", "c-A"); r.record("D"); r.commit("a: D c-A", "a1")
    r.co("main"); cond(r, "D", "c-A"); r.record("D"); r.commit("e: D c-A", "e1")
    show(r, ".ait/D", "D")

    H("G4b  same body, different prev: a: D@A1 (c-x) then D@A (c-A) ; main: D@E (c-A) directly from D0")
    r = base("G4b")
    r.co("a", True); cond(r, "D", "c-x"); r.record("D"); r.commit("a: D c-x", "a1"); cond(r, "D", "c-A"); r.record("D"); r.commit("a: D c-A", "a2")
    r.co("main"); cond(r, "D", "c-A"); r.record("D"); r.commit("e: D c-A", "e1")
    show(r, ".ait/D", "D")

    H("E1  b1: D@B, b2: Q changed, b3: D put back to D0 by git revert of b1 ; main pulls Q")
    r = base("E1", extra=[lambda r: (cond(r, "Q", "q0"), r.record("Q"))])
    r.co("b", True); cond(r, "D", "c-B"); r.record("D"); b1 = r.commit("b: D c-B", "b1")
    cond(r, "Q", "q-B"); r.record("Q"); b2 = r.commit("b: Q", "b2")
    r.g("revert", "--no-edit", b1); r.short[r.out("rev-parse", "HEAD")] = "b3(revert b1)"
    r.co("main")
    show(r, ".ait/D", "D"); show(r, ".ait/Q", "Q")
    tree, rc, conf, ch = mt(r, b2, "b2 (Q's introducing commit)")
    print("  merged D:", summary(r, r.cat(r.out("rev-parse", f"{tree}:.ait/D"))))


def F():
    H("SF1  D and Q share specs/shared.yaml ; a changes it and re-pins both ; b changes it differently and re-pins both")
    def ex(r):
        r.write("specs/shared.yaml", "body: s0\n")
    r = base("SF1", extra=[ex])
    r.record("D", spec="specs/shared.yaml"); r.record("Q", spec="specs/shared.yaml"); r.commit("D, Q share", "s")
    r.co("a", True); r.write("specs/shared.yaml", "body: s-A\n"); r.record("D", spec="specs/shared.yaml"); r.record("Q", spec="specs/shared.yaml"); a1 = r.commit("a: shared", "a1")
    r.co("main"); r.co("b", True); r.write("specs/shared.yaml", "body: s-B\n"); r.record("D", spec="specs/shared.yaml"); r.record("Q", spec="specs/shared.yaml"); b1 = r.commit("b: shared", "b1")
    r.co("main")
    show(r, ".ait/D", "D"); show(r, ".ait/Q", "Q")
    print("  choice D=a1 Q=b1 takes both a1 and b1:")
    wt = r.path + "-wt"
    shutil.rmtree(wt, ignore_errors=True)
    r.g("worktree", "add", "-q", "--detach", wt, "HEAD")
    x = run(wt, "git", "merge", "--no-edit", "--no-verify", a1, b1, check=False)
    print(f"     git merge a1 b1: exit={x.returncode} unmerged={run(wt, 'git', 'diff', '--name-only', '--diff-filter=U', check=False).stdout.split()}")
    run(wt, "git", "merge", "--abort", check=False); run(wt, "git", "reset", "-q", "--hard", check=False)
    r.g("worktree", "remove", "--force", wt)
    print("  choice D=a1 Q=a1 takes only a1:")
    mt(r, a1, "a1")


if __name__ == "__main__":
    for f in sys.argv[1:] or ["A", "U", "G", "N", "C", "R", "X", "E", "F"]:
        globals()[f]()
