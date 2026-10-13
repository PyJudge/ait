"""Scenarios added in pull-criteria revision 8 (r39 review). Prints git facts and the data steps only;
the ait outcome is written by hand in pull-criteria.md."""
import sys, os, shutil
from lib import *
from scen import base, cond, line, restore_pinned, H
from scen4 import choose
from scen5 import clone, code_conflicts
from scen6 import steps26
from scen7 import lines10, edit


# ------------------------------------------------------------------ pinned spec files, judged per side (records 89, 100)
def judge(r, path, ends, named, label=""):
    """record 107 (lib.judge_pinned): ait decides which side changes are not taken; git merges the rest.
    named: {ID: the named contract's result version}"""
    return judge_pinned(r, path, ends, named, label)


def pin_of(r, blob, path):
    return [l.split()[3] for l in r.cat(blob).splitlines() if l.startswith(f"file {path} ")][0]


def SPEC():
    H("SP5  U1 with 'pull D=a1' : the result is a new version V (body of a's D@A)")
    r = base("SP5")
    r.co("a", True); cond(r, "D", "c-A"); DA = r.record("D"); a1 = r.commit("a1: D c-A", "a1")
    r.co("main"); cond(r, "D", "c-E"); DE = r.record("D"); r.commit("e1: D c-E", "e1")
    V = compose(body(r.cat(DA)), None, [DA, DE])
    Vb = r.g("hash-object", "-w", "--stdin", input=V).stdout.strip()
    tree, rc, conf = merge_tree(r, "a")
    print(f"  merge-tree exit={rc} conflicts={conf} ; V == D@A? {Vb == DA} ; V's pin == D@A's pin? {pin_of(r, Vb, 'specs/d.yaml') == pin_of(r, DA, 'specs/d.yaml')}")
    judge(r, "specs/d.yaml", [a1], {"D": Vb})

    H("SP6  T1 : after 'pull D=a1', a writes c-A2 (a2) ; 'pull D' (a2 moved forward ; the result is a new version)")
    choose(r, ".ait/D", DA, [DA, DE], "pull D=a1", "pickA", merge="a")
    r.co("a"); cond(r, "D", "c-A2"); DA2 = r.record("D"); a2 = r.commit("a2: D c-A2", "a2"); r.co("main")
    d = dshow(r, ".ait/D", "D on main")
    print(f"  merge-base(HEAD, a2) = {r.name(r.out('merge-base', 'HEAD', a2))}")
    judge(r, "specs/d.yaml", [a2], {"D": DA2})

    H("SP7  E1 : b1 D@B ; b2 Q ; b3 reverts b1 ; 'pull D Q' takes b2 : the side is the taken commit, not the ref tip")
    def ex(r):
        cond(r, "Q", "q0"); r.record("Q")
    r = base("SP7", extra=[ex])
    r.co("b", True); cond(r, "D", "c-B"); DB = r.record("D"); b1 = r.commit("b1: D@B", "b1")
    cond(r, "Q", "q-B"); r.record("Q"); b2 = r.commit("b2: Q", "b2")
    r.g("revert", "--no-edit", b1); r.co("main")
    print(f"  b's tip specs/d.yaml == init's? {r.blob_at('b', 'specs/d.yaml') == r.blob_at('init', 'specs/d.yaml')}")
    print("   [side = the ref tip b3]")
    judge(r, "specs/d.yaml", ["b"], {"D": DB, "Q": r.blob_at(b2, ".ait/Q")})
    print("   [side = the taken commit b2]")
    judge(r, "specs/d.yaml", [b2], {"D": DB, "Q": r.blob_at(b2, ".ait/Q")})

    H("SP8  init's D pins specs/shared.yaml ; a1 cancels D and edits shared.yaml ; main: new K5 pins it ; 'pull D'")
    r = base("SP8", extra=[lambda r: (r.write("specs/shared.yaml", "body: s0\n"), r.record("D", spec="specs/shared.yaml", prev=["-"]))])
    r.co("a", True); r.record("D", state="cancelled", spec="specs/shared.yaml"); r.write("specs/shared.yaml", "body: s-A\n"); a2 = r.commit("a1: cancel D, edit shared.yaml", "a1")
    r.co("main"); K5 = r.record("K5", spec="specs/shared.yaml"); r.commit("e1: K5 pins shared.yaml", "e1")
    tree, rc, conf = merge_tree(r, "a")
    print(f"  a1's D: {summary(r, r.cat(r.blob_at('a', '.ait/D')))} with a file row for shared.yaml ; merge-tree exit={rc}")
    dshow(r, ".ait/D", "D")
    print("   [ended versions do not pin]")
    judge(r, "specs/shared.yaml", [a2], {"D": r.blob_at(a2, ".ait/D")})

    H("SF2  SF1 with the mixed choice 'pull D=a1 Q=b1' : D and Q both pin specs/shared.yaml")
    def ex(r):
        r.write("specs/shared.yaml", "body: s0\n"); r.record("D", spec="specs/shared.yaml", prev=["-"])
        r.record("Q", spec="specs/shared.yaml", creator="agent-q S9")
    r = base("SF2", extra=[ex])
    r.co("a", True); r.write("specs/shared.yaml", "body: s-A\n"); DA = r.record("D", spec="specs/shared.yaml"); r.record("Q", spec="specs/shared.yaml", creator="agent-q S9"); a1 = r.commit("a1", "a1")
    r.co("main"); r.co("b", True); r.write("specs/shared.yaml", "body: s-B\n"); r.record("D", spec="specs/shared.yaml"); QB = r.record("Q", spec="specs/shared.yaml", creator="agent-q S9"); b1 = r.commit("b1", "b1")
    r.co("main")
    dshow(r, ".ait/D", "D"); dshow(r, ".ait/Q", "Q")
    print(f"  result pins of shared.yaml: D (a1's body) s-A, Q (b1's body) s-B")
    judge(r, "specs/shared.yaml", [a1, b1], {"D": DA, "Q": QB})

    H("SP4c  as SP4, but a's D and main's K edit the same line of tests/f.sh ; 'pull D'")
    def ex(r):
        r.write("tests/f.sh", lines10("f")); cond(r, "K", "k0"); r.record("K")
    r = base("SP4c", extra=[ex])
    r.co("a", True); edit(r, "tests/f.sh", "f1\n", "f1 A\n"); r.write("specs/d.yaml", "body: c0\ninputs: [tests/f.sh]\n")
    D = r.record("D", inputs=["tests/f.sh"]); a1 = r.commit("a1: D pins tests/f.sh, edits f1", "a1")
    r.co("main"); edit(r, "tests/f.sh", "f1\n", "f1 E\n"); r.write("specs/k.yaml", "body: k0\ninputs: [tests/f.sh]\n")
    K = r.record("K", inputs=["tests/f.sh"]); r.commit("e1: K pins tests/f.sh, edits f1", "e1")
    tree, rc, conf = merge_tree(r, "a")
    print(f"  merge-tree exit={rc} conflicts={conf}")
    dshow(r, ".ait/D", "D"); dshow(r, ".ait/K", "K")
    judge(r, "tests/f.sh", [a1], {"D": D})

    for st in ("future", "current"):
        H(f"DL2-{st}  tests/d.sh is code at init ; main edits its line 9 and D ({st}) pins it ; a1 deletes it and changes Q ; 'pull Q'")
        r = base(f"DL2-{st}", extra=[lambda r: (r.write("tests/d.sh", lines10("t")), cond(r, "Q", "q0"), r.record("Q"))])
        r.co("a", True); os.remove(os.path.join(r.path, "tests/d.sh")); cond(r, "Q", "q-A"); r.record("Q"); a1 = r.commit("a1: delete tests/d.sh, Q", "a1")
        r.co("main"); edit(r, "tests/d.sh", "t9\n", "t9 E\n"); r.write("specs/d.yaml", "body: c0\ninputs: [tests/d.sh]\n")
        D = r.record("D", state=st, inputs=["tests/d.sh"]); r.commit("e1: D pins tests/d.sh", "e1")
        x = r.g("merge-tree", "--write-tree", "HEAD", "a", check=False)
        print(f"  merge-tree exit={x.returncode} : {[l for l in x.stdout.splitlines() if l.startswith('CONFLICT')]}")
        judge(r, "tests/d.sh", [a1], {"Q": r.blob_at(a1, ".ait/Q")})

    H("RN2  a1 moves specs/d.yaml to specs/d2.yaml and edits its line 6 (D@A pins d2.yaml) ; main edits line 1 (D@E) ; 'pull D=a1'")
    r = base("RN2", extra=[lambda r: (r.write("specs/d.yaml", "body: c0\n# l2\n# l3\n# l4\n# l5\n# l6\n"), r.record("D", prev=["-"]))])
    r.co("a", True); r.g("mv", "specs/d.yaml", "specs/d2.yaml"); edit(r, "specs/d2.yaml", "# l6\n", "# l6 A\n")
    DA = r.record("D", spec="specs/d2.yaml"); a1 = r.commit("a1: move to d2.yaml, edit l6", "a1")
    r.co("main"); edit(r, "specs/d.yaml", "body: c0\n", "body: c-E\n"); r.record("D"); r.commit("e1: c-E", "e1")
    tree, rc, conf = merge_tree(r, "a")
    m = r.blob_at(tree, "specs/d2.yaml")
    print(f"  merge-tree exit={rc} ; git's specs/d2.yaml: {r.cat(m).splitlines()[0]!r} ... {r.cat(m).splitlines()[-1]!r} ; == D@A's pin? {m == pin_of(r, DA, 'specs/d2.yaml')} ; specs/d.yaml in git's tree? {bool(r.blob_at(tree, 'specs/d.yaml'))}")
    judge(r, "specs/d2.yaml", [a1], {"D": DA})


# ------------------------------------------------------------------ reopen commits: one formula (records 93, 99)
def REOPEN():
    H("RP5  RP3's pull commit under v14 1.5d as written, and under the one formula")
    r = base("RP5")
    r.co("a", True); line(r, 1, "A"); C = r.record("D", state="current"); r.commit("a: fulfil D", "a1")
    r.co("main"); cond(r, "D", "c-E"); E = r.record("D"); r.commit("e: c-E", "e1")
    choose(r, ".ait/D", C, [C, E], "pull D=a1", "pickA", merge="a")
    r.co("a"); R = r.record("D", state="future"); r.commit("a: reopen D", "a2"); r.co("main")
    d = decide(r, ".ait/D")
    choose(r, ".ait/D", R, d["latest"], "pull D", "pull2", merge="a")
    print(f"  v14 1.5d as written: reopen commit? {reopen_commit_v14(r, 'HEAD', '.ait/D')} ; one formula: reopens {[c[:7] for c in reopen_commit(r, 'HEAD', '.ait/D')]}")

    H("RP4  D0 current with 'check forced' ; main reopens it (R) ; a re-checks it with -r (C', same body) ; main pulls D")
    r = base("RP4", extra=[lambda r: r.record("D", state="current", check="forced", prev=["-"])])
    D0 = r.blob_at("init", ".ait/D")
    r.co("a", True); Cp = r.record("D", state="current", check="reported"); r.commit("a: re-check with -r", "a1")
    r.co("main"); R = r.record("D", state="future"); r.commit("e: reopen D", "e1")
    print(f"  D0: {summary(r, r.cat(D0))} ; C': {summary(r, r.cat(Cp))} prev={[p[:7] for p in prevs(r.cat(Cp))]} ; body(C') == body(D0)? {body(r.cat(Cp)) == body(r.cat(D0))}")
    d = dshow(r, ".ait/D", "D on main")
    # the result's body is mine (R), so pull takes no commit: a commit on HEAD (1절 들이는 커밋, G4b)
    choose(r, ".ait/D", R, d["latest"], "pull D (3-way ; kept D)", "pull")
    print(f"  the pull commit's parents: {len(r.out('rev-list', '--parents', '-n1', 'HEAD').split()) - 1} ; v14 1.5d as written: {reopen_commit_v14(r, 'HEAD', '.ait/D')} ; one formula: reopens {[c[:7] for c in reopen_commit(r, 'HEAD', '.ait/D')]}")

    H("RP6  a1 fulfils D (C) and main takes it ; x1 reopens, x2 c-B ; y1 (from init) c-B ; main 'pull D'")
    r = base("RP6")
    r.co("a", True); C = r.record("D", state="current"); a1 = r.commit("a1: fulfil D (code as is)", "a1")
    r.co("main"); r.g("merge", "-q", "--ff-only", "a")
    r.co("x", True); r.record("D", state="future"); r.commit("x1: reopen D", "x1"); cond(r, "D", "c-B"); Fx = r.record("D"); r.commit("x2: c-B", "x2")
    r.g("checkout", "-q", "-b", "y", "init"); cond(r, "D", "c-B"); Fy = r.record("D"); y1 = r.commit("y1: c-B", "y1")
    r.co("main")
    d = dshow(r, ".ait/D", "D on main")
    print(f"  Fx contains my C? {contains(r, Fx, C)} ; Fy contains my C? {contains(r, Fy, C)}")
    for pick, end, keep in (("x2 (decision 30)", "x", Fx), ("y1", "y", Fy)):
        r.g("checkout", "-q", "-B", "tmp", "main")
        choose(r, ".ait/D", keep, d["latest"], f"pull D (TIE takes {end})", "p-" + end, merge=end)
        hist = r.out("rev-list", "HEAD").split()
        print(f"   [TIE takes {pick}] any current version in V's prev? {any(fields(r.cat(p))['state'][0] == 'current' for p in prevs(r.cat(r.blob_at('HEAD', '.ait/D'))))} ; x1 in this history? {r.out('rev-parse', 'x~1') in hist} ; v14 1.5d as written: {reopen_commit_v14(r, 'HEAD', '.ait/D')} ; one formula: reopens {[c[:7] for c in reopen_commit(r, 'HEAD', '.ait/D')]}")
        r.co("main"); r.g("branch", "-D", "tmp")

    H("RV3b'  a1 fulfils D ; main fast-forwards to a1, then 'git revert a1' : is the revert commit a reopen commit?")
    r = base("RV3c")
    r.co("a", True); line(r, 1, "A"); C = r.record("D", state="current"); r.commit("a1: fulfil D", "a1")
    r.co("main"); r.g("merge", "-q", "--ff-only", "a"); r.g("revert", "--no-edit", "HEAD")
    print(f"  HEAD's D == init's D0? {r.blob_at('HEAD', '.ait/D') == r.blob_at('init', '.ait/D')} ; parent's D contains it? {contains(r, r.blob_at('HEAD~1', '.ait/D'), r.blob_at('HEAD', '.ait/D'))}")
    print(f"  v14 1.5d as written: {reopen_commit_v14(r, 'HEAD', '.ait/D')} ; one formula: reopens {[c[:7] for c in reopen_commit(r, 'HEAD', '.ait/D')]}")

    H("IND4  a1 fulfils D (C) ; main c-E (E) ; main 'pull D=a1 future amend D'")
    r = base("IND4")
    r.co("a", True); line(r, 1, "A"); C = r.record("D", state="current"); r.commit("a1: fulfil D", "a1")
    r.co("main"); cond(r, "D", "c-E"); E = r.record("D"); r.commit("e1: c-E", "e1")
    b = body(r.cat(C))
    V1 = r.g("hash-object", "-w", "--stdin", input=compose(b, new_check_line(r, b, [C, E]), [C, E])).stdout.strip()
    b2 = b.replace("state current", "state future")
    r.g("merge", "--no-commit", "--no-ff", "a", check=False)
    t = compose(b2, None, [V1, C, E]); r.write(".ait/D", t); restore_pinned(r, t); r.commit("pull D=a1 future amend D", "pull")
    V2 = r.blob_at("HEAD", ".ait/D")
    print(f"  one formula: reopens {[c[:7] for c in reopen_commit(r, 'HEAD', '.ait/D')]} (C = {C[:7]}, V1 = {V1[:7]})")
    print(f"  v14 7.1.5 reason 'keeps the future version': body(V2) == body of a future start version (E)? {body(r.cat(V2)) == body(r.cat(E))}")

    H("U1z  U1c, then a clone z that holds main only")
    r = base("U1z")
    r.co("a", True); line(r, 1, "A"); C = r.record("D", state="current"); r.commit("a1: fulfil D", "a1")
    r.co("main"); cond(r, "D", "c-E"); E = r.record("D"); r.commit("e1: c-E", "e1")
    choose(r, ".ait/D", E, [C, E], "pull D=HEAD", "kept")
    z = os.path.join(RUNS, "U1z-clone"); shutil.rmtree(z, ignore_errors=True)
    run(RUNS, "git", "clone", "-q", "--no-local", "--single-branch", "-b", "main", "file://" + r.path, z)
    from scen5 import attach
    c = attach(z)
    print(f"  here: reopens {[x[:7] for x in reopen_commit(r, 'HEAD', '.ait/D')]} ; clone z: has C's blob? {c.cat(C) is not None} ; reopens {[x[:7] for x in reopen_commit(c, 'HEAD', '.ait/D')]}")


# ------------------------------------------------------------------ the order of the ends and split hints (records 92, 101)
def ORDER():
    H("ORD1  'pull D Q' : a1 (D) edits x.c line 1 ; b1 (Q) edits x.c line 8 and y.c line 1 ; main edited x.c line 1 and y.c line 1 ; a1 < b1")
    def ex(r):
        cond(r, "Q", "q0"); r.record("Q"); r.write("src/x.c", lines10("x")); r.write("src/y.c", lines10("y"))
    r = base("ORD1", extra=[ex])
    r.co("main"); r.co("b", True); cond(r, "Q", "q-B"); r.record("Q"); edit(r, "src/x.c", "x8\n", "x8 B\n"); edit(r, "src/y.c", "y1\n", "y1 B\n"); b1 = r.commit("b1", "b1")
    r.co("main"); r.co("a", True)
    k = 0
    while True:
        cond(r, "D", "c-A"); r.record("D"); edit(r, "src/x.c", "x1\n", "x1 A\n"); a1 = r.commit("a1" + "." * k, "a1")
        if a1 < b1:
            break
        r.g("reset", "-q", "--hard", "init"); k += 1
    r.co("main"); edit(r, "src/x.c", "x1\n", "x1 E\n"); edit(r, "src/y.c", "y1\n", "y1 E\n"); r.commit("e1", "e1")
    print(f"  commit-ID order: {[r.name(h) for h in sorted([a1, b1])]}")
    cur, conflicted = r.out("rev-parse", "HEAD"), []
    for h in (a1, b1):
        bb = r.out("merge-base", cur, h)
        again = [p for p in r.out("diff", "--name-only", bb, h).split() if p in conflicted]
        rc, tree, code = code_conflicts(r, cur, h)
        print(f"   order a1, b1: {r.name(h)} changes already-conflicted {again} ; conflicts now {code}")
        conflicted += code; cur = r.out("commit-tree", tree, "-p", cur, "-p", h, "-m", "tmp")
    steps26(r, [a1, b1])

    H("MH9  a1: D c-A, x.c ; b0: D c-B ; b1 (on b0): Q, x.c ; main: x.c ; 'pull D=a1 Q' then the split commands in turn")
    def ex(r):
        cond(r, "Q", "q0"); r.record("Q"); r.write("src/x.c", "x=0\n")
    r = base("MH9", extra=[ex])
    r.co("a", True); cond(r, "D", "c-A"); DA = r.record("D"); r.write("src/x.c", "x=A\n"); a1 = r.commit("a1", "a1")
    r.co("main"); r.co("b", True); cond(r, "D", "c-B"); DB = r.record("D"); r.commit("b0", "b0")
    cond(r, "Q", "q-B"); r.record("Q"); r.write("src/x.c", "x=B\n"); b1 = r.commit("b1", "b1")
    r.co("main"); r.write("src/x.c", "x=E\n"); e1 = r.commit("e1", "e1")
    dshow(r, ".ait/D", "D")
    print("   [both at once: ends a1, b1]")
    steps26(r, [a1, b1], decided=("specs/d.yaml",))
    print("   [first 'pull D=a1 -m <why>' (end a1 only): x.c conflicts once ; the resolution keeps main's x.c]")
    r.g("merge", "--no-commit", "--no-ff", "a", check=False)
    t = compose(body(r.cat(DA)), None, [DA, DB]); r.write(".ait/D", t); restore_pinned(r, t)
    r.write("src/x.c", "x=E\n"); r.commit("pull D=a1 (x.c conflict resolved as main's)", "pickA")
    dshow(r, ".ait/D", "D after the first command")
    dshow(r, ".ait/Q", "Q after the first command")
    print("   [then 'pull Q' (end b1 only) ; specs/d.yaml: b's change is the discarded D@B's spec, decided by pull]")
    steps26(r, [b1], decided=("specs/d.yaml",))

    for variant, mine in (("MH8b", None), ("MH8c", "x=A\n")):
        H(f"{variant}  MH8 (a1 and b1 both change x.c, D-O taken together), but main's x.c is {'unchanged' if mine is None else 'the same as a1'}")
        def ex2(r):
            r.write("src/o.c", "o\n"); cond(r, "O", "o0"); r.record("O", role="offer", provider="src/o.c")
            r.write("src/x.c", "x=0\n"); r.write("src/y.c", "y=0\n")
        r = base(variant, extra=[ex2])
        r.co("c", True); r.record("D-O", role="undertake", state="undertaken"); r.commit("c1", "c1")
        r.co("a", True); cond(r, "D", "c-A"); r.record("D"); r.write("src/x.c", "x=A\n"); a1 = r.commit("a1", "a1")
        r.co("c"); r.co("b", True); cond(r, "O", "o-B"); r.record("O", role="offer", provider="src/o.c"); r.write("src/x.c", "x=B\n"); r.write("src/y.c", "y=B\n"); b1 = r.commit("b1", "b1")
        r.co("main"); r.write("src/y.c", "y=E\n")
        if mine:
            r.write("src/x.c", mine)
        r.commit("e1", "e1")
        steps26(r, [a1, b1])

    H("MH10  CY1, plus c1 edits x.c line 5 and main edits x.c line 5 ; 'pull D O'")
    def ex2(r):
        r.write("src/o.c", "o\n"); cond(r, "O", "o0"); r.record("O", role="offer", provider="src/o.c"); r.write("src/x.c", lines10("x"))
    r = base("MH10", extra=[ex2])
    r.co("c", True); r.record("D-O", role="undertake", state="undertaken"); edit(r, "src/x.c", "x5\n", "x5 C\n"); r.commit("c1", "c1")
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); a1 = r.commit("a1", "a1")
    r.co("c"); r.co("b", True); cond(r, "O", "o-B"); r.record("O", role="offer", provider="src/o.c"); b1 = r.commit("b1", "b1")
    r.co("main"); edit(r, "src/x.c", "x5\n", "x5 E\n"); r.commit("e1", "e1")
    tr = r.g("merge-tree", "--write-tree", "HEAD", a1, check=False).stdout.split()[0]
    acc = r.out("commit-tree", tr, "-p", "HEAD", "-p", a1, "-m", "tmp")
    print(f"  files b1 changes from merge-base(accumulated, b1) = {r.name(r.out('merge-base', acc, b1))}: {r.out('diff', '--name-only', r.out('merge-base', acc, b1), b1).split()}")
    print(f"  files b1 changes from merge-base(HEAD, b1) = {r.name(r.out('merge-base', 'HEAD', b1))}: {r.out('diff', '--name-only', r.out('merge-base', 'HEAD', b1), b1).split()}")
    steps26(r, [a1, b1])


# ------------------------------------------------------------------ the finishing commit's start versions (record 102)
def FIN():
    H("HK3  a1: D cancel, x.c ; main: D c-E, x.c ; 'pull D' stops on x.c ; the resolution writes .ait/D as c-E (future), prev {C, E}")
    r = base("HK3", extra=[lambda r: r.write("src/x.c", "x=0\n")])
    r.co("a", True); C = r.record("D", state="cancelled"); r.write("src/x.c", "x=A\n"); a1 = r.commit("a1: cancel D, x.c", "a1")
    r.co("main"); cond(r, "D", "c-E"); E = r.record("D"); r.write("src/x.c", "x=E\n"); r.commit("e1: c-E, x.c", "e1")
    dshow(r, ".ait/D", "D")
    N = r.g("hash-object", "-w", "--stdin", input=compose(body(r.cat(E)), None, [C, E])).stdout.strip()
    ends = [x for x in ancestors(r, E) - {E} if fields(r.cat(x))["state"][0] in TERMINAL]
    print(f"  start versions: HEAD E (contains an ended version? {bool(ends)}), a1 C ({fields(r.cat(C))['state'][0]})")
    print(f"  the resolution N (future, prev {{C, E}}): contains C? {contains(r, N, C)} ; rule1 = {rule1(r, N)}")


if __name__ == "__main__":
    for f in (sys.argv[1:] or ["SPEC", "REOPEN", "ORDER", "FIN"]):
        globals()[f]()
