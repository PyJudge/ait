"""Scenarios added in pull-criteria revision 9 (r42 review). Prints git facts and the data steps only;
the ait outcome is written by hand in pull-criteria.md."""
import sys, os, shutil
from lib import *
from scen import base, cond, line, restore_pinned, resolve, H
from scen4 import choose
from scen5 import attach, code_conflicts
from scen6 import steps26
from scen7 import lines10, edit


def results_of(r, d, pick=None):
    """the result version decide() leads to (a new version is composed and stored): body of the remaining (or the picked)
    version, prev = the latest versions"""
    keep = pick or d["remaining"][0]
    return r.g("hash-object", "-w", "--stdin", input=compose(body(r.cat(keep)), None, d["latest"])).stdout.strip()


# ------------------------------------------------------------------ pinned spec files: ait decides, git merges (record 107)
def SPEC():
    H("PV1-9  from main's e1 ; 'pull D' takes a2 (TIE) ; the result D is cancelled and pins nothing")
    r = base("PV1r")
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); r.commit("a: c-A", "a1"); T1 = r.record("D", state="cancelled"); a2 = r.commit("a: cancel", "a2")
    r.co("main"); r.co("b", True)
    for c in ("c-x", "c-A"):
        cond(r, "D", c); r.record("D"); r.commit(f"b: {c}", f"b-{c}")
    r.record("D", state="cancelled"); r.commit("b: cancel", "b3")
    r.co("main"); cond(r, "D", "c-E"); r.record("D"); r.commit("e: c-E", "e1")
    x = r.g("merge-tree", "--write-tree", "--name-only", "HEAD", a2, check=False)
    print(f"  plain merge-tree HEAD a2: exit={x.returncode} {[l for l in x.stdout.splitlines()[1:] if l][:2]}")
    judge_pinned(r, "specs/d.yaml", [a2], {"D": T1})

    H("MF2-9  a0: Z in CRLF ; a1: Y current by hand ; a2: new W (current, no check) ; a3: Q ; 'pull Q'")
    def ex(r):
        for k in ("Z", "Y", "Q"):
            cond(r, k, f"{k.lower()}0"); r.record(k)
    r = base("MF2r", extra=[ex])
    r.co("a", True)
    cond(r, "Z", "z-A"); r.record("Z"); t = r.read(".ait/Z"); r.write(".ait/Z", t.replace("\n", "\r\n")); r.commit("a0: Z CRLF", "a0")
    line(r, 1, "Y"); r.record("Y", state="current", check=None); r.commit("a1: Y current by hand", "a1")
    cond(r, "W", "w-A"); r.record("W", state="current", check=None); r.commit("a2: W", "a2")
    cond(r, "Q", "q-A"); r.record("Q"); a3 = r.commit("a3: Q", "a3")
    r.co("main")
    for p in ("specs/q.yaml", "specs/z.yaml", "specs/w.yaml"):
        judge_pinned(r, p, [a3], {"Q": r.blob_at(a3, ".ait/Q")})

    for order in ("a1<b1", "b1<a1"):
        H(f"SP9  main's D pins tests/d.sh (t0) ; a1: tests/d.sh line 1 as code, and Q ; b1: Q2 only ; 'pull Q Q2' ; {order}")
        def ex(r):
            r.write("tests/d.sh", lines10("t")); cond(r, "Q", "q0"); r.record("Q"); cond(r, "Q2", "p0"); r.record("Q2")
        r = base("SP9-" + order.replace("<", ""), extra=[ex])
        r.write("specs/d.yaml", "body: c0\ninputs: [tests/d.sh]\n"); D = r.record("D", inputs=["tests/d.sh"]); r.commit("e1: D pins tests/d.sh", "e1")
        r.g("checkout", "-q", "-b", "b", "init"); cond(r, "Q2", "p-B"); r.record("Q2"); b1 = r.commit("b1: Q2", "b1")
        k = 0
        while True:
            r.g("checkout", "-q", "-B", "a", "init"); edit(r, "tests/d.sh", "t1\n", "t1 A\n"); cond(r, "Q", "q-A"); r.record("Q")
            a1 = r.commit("a1: tests/d.sh, Q" + "." * k, "a1")
            if (a1 < b1) == (order == "a1<b1"):
                break
            k += 1
        r.co("main")
        ends = sorted([a1, b1])
        print(f"  ends in commit-ID order: {[r.name(e) for e in ends]}")
        judge_pinned(r, "tests/d.sh", ends, {"Q": r.blob_at(a1, ".ait/Q"), "Q2": r.blob_at(b1, ".ait/Q2")})

    H("SP10  a1: D c-A ; a2: a plain commit edits specs/d.yaml (c-X, no re-pin) ; a3: Q ; 'pull D Q' (end a3), and 'pull D' then 'pull Q'")
    r = base("SP10", extra=[lambda r: (cond(r, "Q", "q0"), r.record("Q"))])
    r.co("a", True); cond(r, "D", "c-A"); DA = r.record("D"); a1 = r.commit("a1: D c-A", "a1")
    cond(r, "D", "c-X"); r.commit("a2: plain edit of specs/d.yaml", "a2")
    cond(r, "Q", "q-A"); r.record("Q"); a3 = r.commit("a3: Q", "a3")
    r.co("main")
    print(f"  a3's D == a1's D@A? {r.blob_at(a3, '.ait/D') == DA} ; a3's specs/d.yaml = {r.cat(r.blob_at(a3, 'specs/d.yaml')).strip()!r}")
    print("   ['pull D Q': end a3]")
    judge_pinned(r, "specs/d.yaml", [a3], {"D": DA, "Q": r.blob_at(a3, ".ait/Q")})
    print("   ['pull D': end a1]")
    judge_pinned(r, "specs/d.yaml", [a1], {"D": DA})

    H("SP11  as SP10, but a1's D newly pins tests/t.sh with line 1 A ; a2 edits its line 1 to X by a plain commit ; main edits line 9 as code")
    def ex(r):
        r.write("tests/t.sh", lines10("t")); cond(r, "Q", "q0"); r.record("Q")
    r = base("SP11", extra=[ex])
    r.co("a", True); edit(r, "tests/t.sh", "t1\n", "t1 A\n"); r.write("specs/d.yaml", "body: c-A\ninputs: [tests/t.sh]\n"); DA = r.record("D", inputs=["tests/t.sh"]); r.commit("a1", "a1")
    edit(r, "tests/t.sh", "t1 A\n", "t1 X\n"); r.commit("a2: plain edit", "a2"); cond(r, "Q", "q-A"); r.record("Q"); a3 = r.commit("a3: Q", "a3")
    r.co("main"); edit(r, "tests/t.sh", "t9\n", "t9 E\n"); r.commit("e1: code", "e1")
    m, _ = judge_pinned(r, "tests/t.sh", [a3], {"D": DA, "Q": r.blob_at(a3, ".ait/Q")})
    print(f"  lines 1 and 9 of the merge: {[l for l in r.cat(r.blob_at(m, 'tests/t.sh')).splitlines() if l.startswith(('t1', 't9'))]}")

    H("SP12  new A, B, C on a, b, c each pin tests/shared.sh and edit lines 1, 10, 20 ; 'pull A B C'")
    r = base("SP12", extra=[lambda r: r.write("tests/shared.sh", "".join(f"line{i}\n" for i in range(1, 21)))])
    named, ends = {}, []
    for br, k, n in (("a", "A", 1), ("b", "B", 10), ("c", "C", 20)):
        r.g("checkout", "-q", "-b", br, "init"); edit(r, "tests/shared.sh", f"line{n}\n", f"line{n} {br}\n")
        r.write(f"specs/{k.lower()}.yaml", "body: x\ninputs: [tests/shared.sh]\n"); named[k] = r.record(k, inputs=["tests/shared.sh"])
        ends.append(r.commit(f"{br}1", f"{br}1"))
    r.co("main")
    m, _ = judge_pinned(r, "tests/shared.sh", sorted(ends), named)
    print(f"  the merge has all three edits? {all(x in r.cat(r.blob_at(m, 'tests/shared.sh')) for x in ('line1 a', 'line10 b', 'line20 c'))}")

    H("MF3  main's D pins tests/f.sh (base) ; a: Q, and a malformed W (two state rows) whose file row pins tests/f.sh at a's content ; 'pull Q'")
    def ex(r):
        r.write("tests/f.sh", "echo base\n"); cond(r, "Q", "q0"); r.record("Q")
        r.write("specs/d.yaml", "body: c0\ninputs: [tests/f.sh]\n"); r.record("D", inputs=["tests/f.sh"], prev=["-"])
    r = base("MF3", extra=[ex])
    r.co("a", True); cond(r, "Q", "q-A"); r.record("Q"); r.write("tests/f.sh", "echo incoming\n"); cond(r, "W", "w0")
    r.record("W", inputs=["tests/f.sh"]); r.write(".ait/W", r.read(".ait/W").replace("state future\n", "state future\nstate cancelled\n"))
    a1 = r.commit("a1: Q, malformed W", "a1"); r.co("main")
    print(f"  W well-formed? {well_formed_raw(r.catb(r.blob_at(a1, '.ait/W')))} ; its state rows: {[l for l in r.cat(r.blob_at(a1, '.ait/W')).splitlines() if l.startswith('state')]}")
    judge_pinned(r, "tests/f.sh", [a1], {"Q": r.blob_at(a1, ".ait/Q")})

    H("SP13  U1, where main's new K5 shares specs/d.yaml and was re-pinned with c-E ; 'pull D=a1'")
    r = base("SP13")
    r.co("a", True); cond(r, "D", "c-A"); DA = r.record("D"); a1 = r.commit("a1: D c-A", "a1")
    r.co("main"); r.record("K5", spec="specs/d.yaml", creator="agent-k S7", prev=["-"]); r.commit("e0: K5 shares specs/d.yaml", "e0")
    cond(r, "D", "c-E"); r.record("D"); r.record("K5", spec="specs/d.yaml", creator="agent-k S7"); r.commit("e1: D c-E (K5 re-pinned)", "e1")
    judge_pinned(r, "specs/d.yaml", [a1], {"D": DA})


# ------------------------------------------------------------------ the finishing commit (record 108)
def FIN():
    H("HK4  a1: D c-A newly pins tests/t.sh (line 1 A) ; b1: D c-B ; main: D c-E, tests/t.sh line 1 E as code ; 'pull D=a1' stops on tests/t.sh")
    r = base("HK4", extra=[lambda r: r.write("tests/t.sh", lines10("t"))])
    r.co("a", True); edit(r, "tests/t.sh", "t1\n", "t1 A\n"); r.write("specs/d.yaml", "body: c-A\ninputs: [tests/t.sh]\n"); DA = r.record("D", inputs=["tests/t.sh"]); a1 = r.commit("a1", "a1")
    r.g("checkout", "-q", "-b", "b", "init"); cond(r, "D", "c-B"); DB = r.record("D"); r.commit("b1", "b1")
    r.co("main"); edit(r, "tests/t.sh", "t1\n", "t1 E\n"); cond(r, "D", "c-E"); DE = r.record("D"); r.commit("e1", "e1")
    d = decide(r, ".ait/D")
    V = results_of(r, d, DA)
    print(f"  latest: {[x[:7] for x in d['latest']]} (D@A, D@B, D@E) ; pull's V = body of D@A, prev = latest")
    judge_pinned(r, "tests/t.sh", [a1], {"D": V})
    # the user resolves tests/t.sh to 't1 M' and runs the finishing commit; the hook re-pins future D
    r.g("merge", "--no-commit", "--no-ff", a1, check=False)
    r.write(".ait/D", r.cat(V)); edit(r, "tests/t.sh", r.read("tests/t.sh"), r.cat(r.blob_at("HEAD", "tests/t.sh")).replace("t1 E", "t1 M"))
    tb = r.wblob("tests/t.sh")
    def repin(prev_):
        t = "".join((f"file tests/t.sh 100644 {tb} input\n" if l.startswith("file tests/t.sh ") else l + "\n") for l in body(r.cat(V)).splitlines())
        return compose(t, None, prev_)
    starts = [r.blob_at("HEAD", ".ait/D"), DA]
    for label, prev_ in (("7.1.3-5 (prev = the maximal start versions)", starts), ("record 108 (prev = the kept version and its prev)", [V] + prevs(r.cat(V)))):
        nv = r.g("hash-object", "-w", "--stdin", input=repin(prev_)).stdout.strip()
        missing = [x[:7] for x in d["latest"] if not contains(r, nv, x)]
        print(f"   [{label}] the re-pinned D contains every latest version? {not missing} {('missing ' + str(missing)) if missing else ''}")
    r.g("merge", "--abort")

    H("HD2  b's HEAD holds the accident V of REV1 (future, prev {cancelled C, E}) ; b runs a plain git merge of a code-only branch f")
    r = base("HD2", extra=[lambda r: r.write("src/f.c", "f0\n")])
    r.co("a", True); C = r.record("D", state="cancelled"); r.commit("a1: cancel D", "a1")
    r.co("main"); r.g("merge", "-q", "--ff-only", "a")
    r.g("checkout", "-q", "-b", "b", "init"); cond(r, "D", "c-E"); E = r.record("D"); r.commit("b1: c-E", "b1")
    resolve(r, "main", "D", E, "bM: git merge main, .ait/D resolved by hand as c-E (future), prev {C, E}", "bM")
    V = r.blob_at("HEAD", ".ait/D")
    r.g("checkout", "-q", "-b", "f", "init"); r.write("src/f.c", "f-f\n"); r.commit("f1: code only", "f1"); r.co("b")
    x = r.g("merge", "--no-commit", "--no-ff", "--no-verify", "f", check=False)
    print(f"  V rule1={rule1(r, V)} (contains cancelled C? {contains(r, V, C)}) ; git merge f: exit={x.returncode} ; index D == HEAD's V? {r.out('rev-parse', ':.ait/D') == V} ; f's D = D0? {r.blob_at('f', '.ait/D') == r.blob_at('init', '.ait/D')}")
    r.g("merge", "--abort")


# ------------------------------------------------------------------ TIE and the reopen commit (decision 30, records 99, 110)
def REOPEN():
    for order in ("x1<y1", "y1<x1"):
        H(f"RP7  a1 fulfils D (C) ; main takes it ; x1 reopens D and writes c-B in one commit ; y1 (from init) c-B ; 'pull D' ; {order}")
        r = base("RP7-" + order.replace("<", ""))
        r.co("a", True); C = r.record("D", state="current"); r.commit("a1: fulfil D", "a1")
        r.co("main"); r.g("merge", "-q", "--ff-only", "a")
        r.g("checkout", "-q", "-b", "y", "init"); cond(r, "D", "c-B"); Fy = r.record("D"); y1 = r.commit("y1: c-B", "y1")
        k = 0
        while True:
            r.g("checkout", "-q", "-B", "x", "main"); R = r.record("D", state="future"); cond(r, "D", "c-B"); Fx = r.record("D", prev=[C])
            x1 = r.commit("x1: reopen and c-B" + "." * k, "x1")
            if (x1 < y1) == (order == "x1<y1"):
                break
            k += 1
        r.co("main")
        d = dshow(r, ".ait/D", "D on main")
        print(f"  Fx contains my C? {contains(r, Fx, C)} ; Fy contains my C? {contains(r, Fy, C)}")
        for pick, end in (("Fx (decision 30)", "x"), ("Fy", "y")):
            r.g("checkout", "-q", "-B", "tmp", "main")
            choose(r, ".ait/D", Fx if end == "x" else Fy, d["latest"], f"pull D (TIE takes {end})", "p-" + end, merge=end)
            print(f"   [TIE takes {pick}] the pull commit reopens {[c[:7] for c in reopen_commit(r, 'HEAD', '.ait/D')]} ; x1 in this history? {r.out('rev-parse', 'x') in r.out('rev-list', 'HEAD').split()}")
            r.co("main"); r.g("branch", "-D", "tmp")

    H("U1m  a1 fulfils D (C) ; main takes it ; a reopens (R1) then writes c-R (R2) ; a squashes them ; clone z lacks R1 ; z: 'pull D=a2s'")
    r = base("U1m")
    r.co("a", True); C = r.record("D", state="current"); a1 = r.commit("a1: fulfil D", "a1")
    r.co("main"); r.g("merge", "-q", "--ff-only", "a")
    r.co("a"); R1 = r.record("D", state="future"); r.commit("a2: future amend D", "a2")
    cond(r, "D", "c-R"); R2 = r.record("D"); r.commit("a3: c-R", "a3")
    r.g("reset", "-q", "--soft", a1); r.commit("a2s: squash", "a2s")
    r.co("main")
    z = os.path.join(RUNS, "U1m-z"); shutil.rmtree(z, ignore_errors=True)
    run(RUNS, "git", "clone", "-q", "--no-local", "--no-single-branch", "file://" + r.path, z)
    c = attach(z)
    dz = decide(c, ".ait/D")
    print(f"  clone z: has R1's blob? {c.cat(R1) is not None} ; latest {[x[:7] for x in dz['latest']]} ; {dz['kind']}")
    choose(c, ".ait/D", R2, dz["latest"], "pull D=a2s -m <why>", "pullz", merge=c.out("rev-parse", "origin/a"))
    pz = c.out("rev-parse", "HEAD")
    r.g("fetch", "-q", z, f"{pz}:refs/heads/zmain")
    print(f"  z's pull commit: reopens in z {[x[:7] for x in reopen_commit(c, pz, '.ait/D')]} ; the same commit here (R1 readable): {[x[:7] for x in reopen_commit(r, pz, '.ait/D')]}")


# ------------------------------------------------------------------ split commands and the decision-20 state (record 109)
def SPLIT():
    def link(r):
        r.write("src/o.c", "o\n"); cond(r, "O", "o0"); r.record("O", role="offer", provider="src/o.c")
        r.write("src/x.c", lines10("x")); r.write("src/y.c", lines10("y"))

    H("MH11  D-O taken together ; a1: D, x.c line 1, y.c line 5 ; b1: O, x.c line 8, y.c line 1 ; main: x.c line 1, y.c line 1 ; 'pull D O'")
    r = base("MH11", extra=[link])
    r.co("c", True); r.record("D-O", role="undertake", state="undertaken"); r.commit("c1", "c1")
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); edit(r, "src/x.c", "x1\n", "x1 A\n"); edit(r, "src/y.c", "y5\n", "y5 A\n"); a1 = r.commit("a1", "a1")
    r.co("c"); r.co("b", True); cond(r, "O", "o-B"); r.record("O", role="offer", provider="src/o.c"); edit(r, "src/x.c", "x8\n", "x8 B\n"); edit(r, "src/y.c", "y1\n", "y1 B\n"); b1 = r.commit("b1", "b1")
    r.co("main"); edit(r, "src/x.c", "x1\n", "x1 E\n"); edit(r, "src/y.c", "y1\n", "y1 E\n"); r.commit("e1", "e1")
    print(f"  HEAD vs a1 code conflicts {code_conflicts(r, 'HEAD', a1)[2]} ; HEAD vs b1 {code_conflicts(r, 'HEAD', b1)[2]}")
    steps26(r, [a1, b1])
    print("   [the decision-20 state: b includes a1 (b2 = b1 merged with a1)]")
    r.co("b"); r.g("merge", "-q", "--no-edit", a1, check=False); b2 = r.commit("b2: b takes a1", "b2"); r.co("main")
    print(f"  a1 is an ancestor of b2? {r.g('merge-base', '--is-ancestor', a1, b2, check=False).returncode == 0}")
    steps26(r, [b2])

    H("XT2/XT3  c1: D-O ; a1: D c-A, x.c ; d1: D c-D (XT3: also x.c) ; b1: O, x.c ; main: x.c ; 'pull D O'")
    for name, d_xc in (("XT2", False), ("XT3", True)):
        r = base(name, extra=[link])
        r.co("c", True); r.record("D-O", role="undertake", state="undertaken"); r.commit("c1", "c1")
        r.co("a", True); cond(r, "D", "c-A"); r.record("D"); edit(r, "src/x.c", "x1\n", "x1 A\n"); a1 = r.commit("a1", "a1")
        r.co("c"); r.co("d", True); cond(r, "D", "c-D"); r.record("D")
        if d_xc:
            edit(r, "src/x.c", "x1\n", "x1 D\n")
        d1 = r.commit("d1", "d1")
        r.co("c"); r.co("b", True); cond(r, "O", "o-B"); r.record("O", role="offer", provider="src/o.c"); edit(r, "src/x.c", "x1\n", "x1 B\n"); b1 = r.commit("b1", "b1")
        r.co("main"); edit(r, "src/x.c", "x1\n", "x1 E\n"); r.commit("e1", "e1")
        print(f"  [{name}] D: {decide(r, '.ait/D')['kind']}")
        for pick, e in (("D=a1", a1), ("D=d1", d1)):
            print(f"   [{name} {pick}: ends {r.name(e)}, b1]"); steps26(r, [e, b1])

    H("MH12  c1: D-O ; a1: D, x.c line 1 ; b1: O ; q1 (from init): Q, x.c line 1 ; main: x.c line 1 ; 'pull D O Q'")
    def ex(r):
        link(r); cond(r, "Q", "q0"); r.record("Q")
    r = base("MH12", extra=[ex])
    r.co("c", True); r.record("D-O", role="undertake", state="undertaken"); r.commit("c1", "c1")
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); edit(r, "src/x.c", "x1\n", "x1 A\n"); a1 = r.commit("a1", "a1")
    r.co("c"); r.co("b", True); cond(r, "O", "o-B"); r.record("O", role="offer", provider="src/o.c"); b1 = r.commit("b1", "b1")
    r.g("checkout", "-q", "-b", "q", "init"); cond(r, "Q", "q-Q"); r.record("Q"); edit(r, "src/x.c", "x1\n", "x1 Q\n"); q1 = r.commit("q1", "q1")
    r.co("main"); edit(r, "src/x.c", "x1\n", "x1 E\n"); r.commit("e1", "e1")
    print("   [all three ends]"); steps26(r, [a1, b1, q1])
    print("   [a group per command: 'pull D O' (a1, b1)]"); steps26(r, [a1, b1])
    print("   [then 'pull Q' (q1) on top of it]")
    cur = r.out("rev-parse", "HEAD")
    for h in sorted([a1, b1], key=lambda x: r.out("rev-parse", x)):
        x = r.g("merge-tree", "--write-tree", cur, h, check=False); cur = r.out("commit-tree", x.stdout.split()[0], "-p", cur, "-p", h, "-m", "tmp")
    print(f"  q1 after 'pull D O' conflicts in {code_conflicts(r, cur, q1)[2]} (one end, so once)")

    H("MH13  as MH9, but b1's commit ID is smaller than a1's ; the split commands are searched in order")
    def ex(r):
        cond(r, "Q", "q0"); r.record("Q"); r.write("src/x.c", "x=0\n")
    r = base("MH13", extra=[ex])
    r.g("checkout", "-q", "-b", "b", "init"); cond(r, "D", "c-B"); DB = r.record("D"); r.commit("b0", "b0")
    cond(r, "Q", "q-B"); r.record("Q"); r.write("src/x.c", "x=B\n"); b1 = r.commit("b1", "b1")
    k = 0
    while True:
        r.g("checkout", "-q", "-B", "a", "init"); cond(r, "D", "c-A"); DA = r.record("D"); r.write("src/x.c", "x=A\n"); a1 = r.commit("a1" + "." * k, "a1")
        if b1 < a1:
            break
        k += 1
    r.co("main"); r.write("src/x.c", "x=E\n"); r.commit("e1", "e1")
    print(f"  commit-ID order: {[r.name(h) for h in sorted([a1, b1])]}")
    q_in = [c for c, n in introducing(r, ".ait/Q", r.blob_at(b1, ".ait/Q"))]
    print(f"  'pull Q' first would take {[r.name(c) for c in q_in]} ; b1 carries D@B? {r.blob_at(b1, '.ait/D') == DB} -> it needs D named, and D=a1 needs a1 (step 3)")
    print("   [order a1 first: 'pull D=a1', then 'pull Q' as in MH9]")
    r.g("merge", "--no-commit", "--no-ff", "a", check=False)
    t = compose(body(r.cat(DA)), None, [DA, DB]); r.write(".ait/D", t); restore_pinned(r, t); r.write("src/x.c", "x=E\n"); r.commit("pull D=a1", "pickA")
    print(f"  then D: {decide(r, '.ait/D')['kind']} ; Q: {decide(r, '.ait/Q')['kind']}")
    steps26(r, [b1], decided=("specs/d.yaml",))


if __name__ == "__main__":
    for f in (sys.argv[1:] or ["SPEC", "FIN", "REOPEN", "SPLIT"]):
        globals()[f]()
