"""Scenarios added in pull-criteria revision 7 (r36 review). Prints git facts and the data steps only;
the ait outcome is written by hand in pull-criteria.md."""
import sys, os, shutil
from lib import *
from scen import base, cond, line, restore_pinned, H
from scen4 import choose
from scen5 import clone, code_conflicts
from scen6 import steps26


def lines10(prefix):
    return "".join(f"{prefix}{i}\n" for i in range(1, 11))


def edit(r, path, old, new):
    r.write(path, r.read(path).replace(old, new))


# ------------------------------------------------------------------ ended versions (decision 21 made transitive)
def END():
    H("REV4  main holds cancelled C ; b: A (future by hand from C, accident), then a hand merge V with prev {A, M} (M valid, from init)")
    r = base("REV4")
    C = r.record("D", state="cancelled"); r.commit("c1: cancel D", "c1")
    r.g("checkout", "-q", "-b", "m", "init"); cond(r, "D", "c-M"); M = r.record("D"); r.commit("m1: c-M", "m1")
    r.g("checkout", "-q", "-b", "b", "main"); A = r.record("D", state="future"); r.commit("b1: future by hand", "b1")
    r.g("merge", "-q", "--no-edit", "m", check=False)
    cond(r, "D", "c-M"); V = r.record("D", prev=[A, M]); r.commit("b2: hand merge, prev {A, M}", "b2")
    r.co("main"); r.g("branch", "-D", "m")
    print(f"  A rule1={rule1(r, A)} ; M rule1={rule1(r, M)} ; V contains cancelled C? {contains(r, V, C)} ; V rule1={rule1(r, V)}")
    dshow(r, ".ait/D", "D on main")


# ------------------------------------------------------------------ pinned spec files: both directions (record 89)
def SPEC():
    H("SP2  a's D newly declares tests/d.sh and edits its line 1 ; main edits line 9 of it as code ; 'pull D'")
    r = base("SP2", extra=[lambda r: r.write("tests/d.sh", lines10("l"))])
    r.co("a", True); edit(r, "tests/d.sh", "l1\n", "l1 A\n"); r.write("specs/d.yaml", "body: c0\ninputs: [tests/d.sh]\n")
    D = r.record("D", inputs=["tests/d.sh"]); r.commit("a1: D pins tests/d.sh", "a1")
    r.co("main"); edit(r, "tests/d.sh", "l9\n", "l9 E\n"); r.commit("e1: code edit", "e1")
    tree, rc, conf = merge_tree(r, "a")
    m = r.out("rev-parse", tree + ":tests/d.sh"); pin = [l for l in r.cat(D).splitlines() if "tests/d.sh" in l][0].split()[3]
    mine_decl = [l for l in r.cat(r.blob_at("HEAD", ".ait/D")).splitlines() if "tests/d.sh" in l]
    print(f"  merge-tree exit={rc} ; merged has both edits? {'l1 A' in r.cat(m) and 'l9 E' in r.cat(m)} ; D's pin == merged? {pin == m} ; my side declares tests/d.sh? {bool(mine_decl)}")

    H("SP3  a's D declares tests/d.sh without changing it ; main edits it as code ; 'pull D'")
    r = base("SP3", extra=[lambda r: r.write("tests/d.sh", lines10("l"))])
    r.co("a", True); r.write("specs/d.yaml", "body: c0\ninputs: [tests/d.sh]\n"); D = r.record("D", inputs=["tests/d.sh"]); r.commit("a1: D pins tests/d.sh", "a1")
    r.co("main"); edit(r, "tests/d.sh", "l9\n", "l9 E\n"); r.commit("e1: code edit", "e1")
    tree, rc, conf = merge_tree(r, "a")
    m = r.out("rev-parse", tree + ":tests/d.sh"); pin = [l for l in r.cat(D).splitlines() if "tests/d.sh" in l][0].split()[3]
    print(f"  merge-tree exit={rc} ; merged == HEAD's (main's edit)? {m == r.blob_at('HEAD', 'tests/d.sh')} ; D's pin == merged? {pin == m}")

    H("SP4  tests/f.sh is code at init ; a's D newly pins it and edits line 1 ; main's K newly pins it and edits line 9 ; 'pull D'")
    def ex(r):
        r.write("tests/f.sh", lines10("f")); cond(r, "K", "k0"); r.record("K")
    r = base("SP4", extra=[ex])
    r.co("a", True); edit(r, "tests/f.sh", "f1\n", "f1 A\n"); r.write("specs/d.yaml", "body: c0\ninputs: [tests/f.sh]\n")
    D = r.record("D", inputs=["tests/f.sh"]); r.commit("a1: D pins tests/f.sh, edits f1", "a1")
    r.co("main"); edit(r, "tests/f.sh", "f9\n", "f9 E\n"); r.write("specs/k.yaml", "body: k0\ninputs: [tests/f.sh]\n")
    K = r.record("K", inputs=["tests/f.sh"]); r.commit("e1: K pins tests/f.sh, edits f9", "e1")
    tree, rc, conf = merge_tree(r, "a")
    m = r.out("rev-parse", tree + ":tests/f.sh")
    pd = [l for l in r.cat(D).splitlines() if "tests/f.sh" in l][0].split()[3]; pk = [l for l in r.cat(K).splitlines() if "tests/f.sh" in l][0].split()[3]
    print(f"  merge-tree exit={rc} conflicts={conf} ; merged has both edits? {'f1 A' in r.cat(m) and 'f9 E' in r.cat(m)} ; a's D pin == merged? {pd == m} ; my K pin == merged? {pk == m}")
    dshow(r, ".ait/D", "D"); dshow(r, ".ait/K", "K")

    H("DL1  tests/d.sh is code at init ; main's D newly pins it (input) ; a deletes it as code and changes Q ; 'pull Q'")
    r = base("DL1", extra=[lambda r: (r.write("tests/d.sh", "t0\n"), cond(r, "Q", "q0"), r.record("Q"))])
    r.co("a", True); os.remove(os.path.join(r.path, "tests/d.sh")); cond(r, "Q", "q-A"); r.record("Q"); r.commit("a1: delete tests/d.sh, Q", "a1")
    r.co("main"); r.write("specs/d.yaml", "body: c0\ninputs: [tests/d.sh]\n"); r.record("D", inputs=["tests/d.sh"]); r.commit("e1: D pins tests/d.sh", "e1")
    tree, rc, conf = merge_tree(r, "a")
    gone = not r.g("rev-parse", "-q", "--verify", tree + ":tests/d.sh", check=False).stdout.strip()
    decl = [p for c in r.out("rev-list", "a", "^HEAD").split() for p in r.out("ls-tree", "-r", "--name-only", c, ".ait").split() if "tests/d.sh" in (r.cat(r.blob_at(c, p)) or "")]
    print(f"  merge-tree exit={rc} ; tests/d.sh deleted in the merged tree? {gone} ; main's D pins it? {'tests/d.sh' in r.cat(r.blob_at('HEAD', '.ait/D'))} ; a's side declares it? {decl or 'none'}")

    H("HD1  main edits its own D's pinned spec in a plain commit (no re-pin) ; then 'pull Q'")
    r = base("HD1")
    r.write("specs/d.yaml", "body: edited by hand\n"); r.commit("e1: plain edit of a pinned spec", "e1")
    pin = [l for l in r.cat(r.blob_at("HEAD", ".ait/D")).splitlines() if l.startswith("file ")][0].split()[3]
    print(f"  HEAD's D pins specs/d.yaml at {pin[:7]} ; HEAD's specs/d.yaml is {r.blob_at('HEAD', 'specs/d.yaml')[:7]} (a bundle error in HEAD, 2.4.1)")


# ORD1 (order of the ends) was rebuilt with a1 < b1: scen8.py


# ------------------------------------------------------------------ reopen commits (decision 27, record 88)
def REOPEN():
    H("U1d  a1 fulfils D (C) ; main changes D's condition (H) ; main 'pull D=HEAD future amend D (consumer)'")
    r = base("U1d", extra=[lambda r: r.write("src/other.c", "o\n")])
    r.co("a", True); line(r, 1, "A"); C = r.record("D", state="current"); r.commit("a: fulfil D", "a1")
    r.co("main"); cond(r, "D", "c-H"); Hv = r.record("D"); r.commit("e: c-H", "e1")
    bH = body(r.cat(Hv))
    V1 = r.g("hash-object", "-w", "--stdin", input=compose(bH, None, [C, Hv])).stdout.strip()
    b2 = bH.replace(f"consumer src/order.c {r.blob_at('HEAD', 'src/order.c')}", f"consumer src/other.c {r.blob_at('HEAD', 'src/other.c')}")
    r.write(".ait/D", compose(b2, None, [V1, C, Hv])); r.commit("pull D=HEAD future amend D", "pull")
    print(f"  V1 (future, contains C) in any commit? no ; the pull commit reopens: {[c[:7] for c in reopen_commit(r, 'HEAD', '.ait/D')]} (C = {C[:7]})")

    H("RP3  main took D=a1 (V1: a's current body, prev {C, mine}) ; then a reopened C (R) ; main pulls D")
    r = base("RP3")
    r.co("a", True); line(r, 1, "A"); C = r.record("D", state="current"); r.commit("a: fulfil D", "a1")
    r.co("main"); cond(r, "D", "c-E"); E = r.record("D"); r.commit("e: c-E", "e1")
    choose(r, ".ait/D", C, [C, E], "pull D=a1", "pickA", merge="a")
    r.co("a"); R = r.record("D", state="future"); r.commit("a: reopen D", "a2")
    r.co("main")
    d = dshow(r, ".ait/D", "D on main")
    choose(r, ".ait/D", R, d["latest"], "pull D (3-way: a's reopen moved forward)", "pull2", merge="a")
    print(f"  main's pull commit reopens: {[c[:7] for c in reopen_commit(r, 'HEAD', '.ait/D')]} (expected none: it carried a's reopen)")
    # RP4 was rebuilt with data ait can make (D0 forced, a re-checks with -r): scen8.py


# ------------------------------------------------------------------ the finishing commit's start versions (record 90)
def FIN():
    H("HK2  'pull D Q' stops on a code conflict ; b1 (Q's end) also holds Y made current by hand (an accident)")
    def ex(r):
        cond(r, "Q", "q0"); r.record("Q"); cond(r, "Y", "y0"); r.record("Y"); r.write("src/x.c", "x=0\n")
    r = base("HK2", extra=[ex])
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); r.write("src/x.c", "x=A\n"); a1 = r.commit("a1", "a1")
    r.co("main"); r.co("b", True); line(r, 1, "Y"); Yb = r.record("Y", state="current", check=None); cond(r, "Q", "q-B"); r.record("Q"); b1 = r.commit("b1: Q, and Y current by hand", "b1")
    r.co("main"); r.write("src/x.c", "x=E\n"); r.commit("e1", "e1")
    Yh = r.blob_at("HEAD", ".ait/Y")
    print(f"  start versions of Y: HEAD {Yh[:7]} (rule1={rule1(r, Yh)}), b1 {Yb[:7]} (rule1={rule1(r, Yb)}) ; pull keeps Y as mine")
    print(f"  the kept Y contains every start version? {all(contains(r, Yh, s) for s in (Yh, Yb))} ; ... every start version that is not an accident? {contains(r, Yh, Yh)}")


if __name__ == "__main__":
    for f in (sys.argv[1:] or ["END", "SPEC", "REOPEN", "FIN"]):
        globals()[f]()
