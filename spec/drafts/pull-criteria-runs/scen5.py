"""Scenarios added in pull-criteria revision 5 (r30 review, decisions 20-25). Prints git facts and the data steps only;
the ait outcome is written by hand in pull-criteria.md."""
import sys, os, shutil
from lib import *
from scen import base, cond, line, resolve, restore_pinned, H
from scen4 import choose
import flows

HERE = os.path.dirname(os.path.abspath(__file__))


def attach(path):
    r = Repo.__new__(Repo)
    r.path, r.short = path, {}
    return r


def clone(r, name):
    p = os.path.join(RUNS, name)
    shutil.rmtree(p, ignore_errors=True)
    run(RUNS, "git", "clone", "-q", "--no-local", "--no-single-branch", "file://" + r.path, p)
    c = attach(p)
    c.g("config", "merge.ait.driver", f"python3 {HERE}/driver.py %O %A %B %P")
    return c


def code_conflicts(r, a, b, decided=()):
    """merge-tree of a and b: (exit, tree, code paths that conflict). Contract files (.ait/) and the spec files the
    received result pins (`decided`, given by the scenario) are decided by pull, so their conflicts are not code conflicts."""
    x = r.g("merge-tree", "--write-tree", "--name-only", a, b, check=False)
    ls = x.stdout.splitlines()
    paths = []
    for l in ls[1:]:
        if not l:
            break
        paths.append(l)
    return x.returncode, ls[0], [p for p in paths if not p.startswith(".ait/") and p not in decided]


def steps(r, ends):
    """decision 20: take the ends one at a time in commit-ID order, a clean one first; report what each step does"""
    rest = sorted(ends, key=lambda h: r.out("rev-parse", h))
    cur, out = r.out("rev-parse", "HEAD"), []
    while rest:
        for h in rest:
            rc, tree, code = code_conflicts(r, cur, h)
            if not code:
                out.append((r.name(r.out("rev-parse", h)), "no code conflict"))
                cur = r.out("commit-tree", tree, "-p", cur, "-p", h, "-m", "tmp")
                rest.remove(h)
                break
        else:
            out.append(("ends with code conflicts", [(r.name(r.out("rev-parse", h)), code_conflicts(r, cur, h)[2]) for h in rest]))
            break
    print(f"  decision 20 steps: {out}")
    return out


# ------------------------------------------------------------------ 3-way: cycles, missing blobs (decision 24, r30 50)
def CYC():
    H("CYC1  a1 c-P, b1 c-Q, c1 c-R ; a keeps P after seeing c1, b keeps Q after seeing a1, c keeps R after seeing b1")
    r = base("CYC1")
    v = {}
    for br, c in (("a", "c-P"), ("b", "c-Q"), ("c", "c-R")):
        r.co("main"); r.co(br, True); cond(r, "D", c); v[c] = r.record("D"); r.commit(f"{br}: D {c}", f"{br}1")
    r.co("a"); a2 = choose(r, ".ait/D", v["c-P"], [v["c-P"], v["c-R"]], "a: pull D=HEAD (saw c1)", "a2")
    r.co("b"); b2 = choose(r, ".ait/D", v["c-Q"], [v["c-P"], v["c-Q"]], "b: pull D=HEAD (saw a1)", "b2")
    r.co("c"); c2 = choose(r, ".ait/D", v["c-R"], [v["c-Q"], v["c-R"]], "c: pull D=HEAD (saw b1)", "c2")
    r.co("main")
    d = dshow(r, ".ait/D", "D on main")
    H("CYC2  CYC1, then main picks D=b2 ; then main, a, b, c pull D")
    lat = d["latest"]
    vb2 = r.blob_at("b", ".ait/D")
    choose(r, ".ait/D", vb2, lat, "main: pull D=b2", "pick", merge="b")
    for br in ("main", "a", "b", "c"):
        r.co(br); dshow(r, ".ait/D", f"D on {br}")
    r.co("main")


def KC():
    H("KC2  T1's history (main chose D=a1, then a went on to c-A2) ; 'pull D=a2'")
    r = base("KC2")
    r.co("a", True); cond(r, "D", "c-A"); vA = r.record("D"); a1 = r.commit("a: D c-A", "a1")
    r.co("main"); cond(r, "D", "c-E"); vE = r.record("D"); r.commit("e: D c-E", "e1")
    choose(r, ".ait/D", vA, [vA, vE], "pull D=a1", "pull", merge=a1)
    r.co("a"); cond(r, "D", "c-A2"); vA2 = r.record("D"); r.commit("a: D c-A2", "a2"); r.co("main")
    d = dshow(r, ".ait/D", "D")
    mine = r.blob_at("HEAD", ".ait/D")
    print(f"  a2's version is the only remaining one: {d['remaining'] == [vA2]} ; does it contain mine? {contains(r, vA2, mine)}")


def MS():
    H("MS2  y1 c-K ; q1 c-M, q2 keeps M after seeing y1 ; p2 picks y1 after seeing q1 ; q is squashed ; fresh clone")
    r = base("MS2")
    r.co("y", True); cond(r, "D", "c-K"); K = r.record("D"); r.commit("y: c-K", "y1")
    r.co("main"); r.co("q", True); cond(r, "D", "c-M"); M = r.record("D"); r.commit("q: c-M", "q1")
    cond(r, "D", "c-M"); r.record("D", prev=[M, K]); r.commit("q: pull D=HEAD (saw y1)", "q2")
    r.co("main"); r.co("p", True); r.g("merge", "-q", "--no-edit", "y")
    cond(r, "D", "c-K"); r.record("D", prev=[M, K]); r.commit("p: pull D=y1 (saw q1 and y1)", "p2")
    r.co("q"); r.g("reset", "-q", "--soft", "main"); r.commit("q: squashed", "qS")
    r.co("main")
    dshow(r, ".ait/D", "D in the same repository")
    c = clone(r, "MS2-clone")
    print(f"  fresh clone: M's blob present? {c.cat(M) is not None}  K's? {c.cat(K) is not None}")
    dshow(c, ".ait/D", "D in a fresh clone")


# ------------------------------------------------------------------ terminal versions (decisions 13, 21 ; r30 47, 48)
def TW():
    H("U4b  main edits D and edits it back (body = D0) ; a cancels D")
    r = base("U4b")
    r.co("a", True); r.record("D", state="cancelled"); r.commit("a: cancel D", "a1")
    r.co("main"); cond(r, "D", "c-tmp"); r.record("D"); r.commit("e: c-tmp", "e1"); cond(r, "D", "c0"); r.record("D"); r.commit("e: back to c0", "e2")
    print("  body(mine) == body(D0)?", body(r.cat(r.blob_at("HEAD", ".ait/D"))) == body(r.cat(r.blob_at("init", ".ait/D"))))
    dshow(r, ".ait/D", "D")

    H("PV1  a: c-A then cancel ; b: c-x, c-A then cancel ; main: c-E (also at 'om', a stand-in for origin/main)")
    r = base("PV1")
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); r.commit("a: c-A", "a1"); T1 = r.record("D", state="cancelled"); r.commit("a: cancel", "a2")
    r.co("main"); r.co("b", True)
    for c in ("c-x", "c-A"):
        cond(r, "D", c); r.record("D"); r.commit(f"b: {c}", f"b-{c}")
    T2 = r.record("D", state="cancelled"); r.commit("b: cancel", "b3")
    r.co("main"); cond(r, "D", "c-E"); E = r.record("D"); r.commit("e: c-E", "e1"); r.g("branch", "om")
    d = dshow(r, ".ait/D", "D")
    print("  body(T1) == body(T2)?", body(r.cat(T1)) == body(r.cat(T2)))
    choose(r, ".ait/D", T1, d["latest"], "pull D (new version, prev = latest)", "pull", merge="a")
    dshow(r, ".ait/D", "D after the pull ; again")

    H("PV2  a: cancel ; b: c-B ; main: c-E")
    r = base("PV2")
    r.co("a", True); T = r.record("D", state="cancelled"); r.commit("a: cancel", "a1")
    r.co("main"); r.co("b", True); cond(r, "D", "c-B"); r.record("D"); r.commit("b: c-B\n\nAit-Agent: agent-b", "b1")
    r.co("main"); cond(r, "D", "c-E"); r.record("D"); r.commit("e: c-E", "e1")
    d = dshow(r, ".ait/D", "D")
    choose(r, ".ait/D", T, d["latest"], "pull D (new version, prev = latest)", "pull", merge="a")
    dshow(r, ".ait/D", "D after the pull ; again")
    vb = r.blob_at("b", ".ait/D")
    for c, n in introducing(r, ".ait/D", vb):
        print(f"  b's version is in prev: {vb in prevs(r.cat(r.blob_at('HEAD', '.ait/D')))} ; it was made by {r.name(c)} "
              f"with Ait-Agent {r.out('log', '-1', '--format=%(trailers:key=Ait-Agent,valueonly)', c)!r}")

    H("PV3  D current, drop X ; main fulfils X (D withdrawn) ; a changes D's consumer only")
    r = base("PV3", d_state="current", extra=[lambda r: r.record("X", role="drop", target="D")])
    r.co("a", True); r.write("src/order.c", "order A\n"); r.record("D", state="current"); r.commit("a: consumer", "a1")
    r.co("main"); r.record("X", role="drop", state="fulfilled", target="D"); W = r.record("D", state="withdrawn"); r.commit("e: fulfil X", "e1")
    d = dshow(r, ".ait/D", "D")
    choose(r, ".ait/D", W, d["latest"], "pull D (kept D)", "kept")
    dshow(r, ".ait/D", "D after kept ; again")

    H("TT1  a: cancel D ; b: fulfil D, drop X, fulfil X (D withdrawn) ; main: c-E")
    r = base("TT1")
    r.co("a", True); T = r.record("D", state="cancelled"); r.commit("a: cancel", "a1")
    r.co("main"); r.co("b", True); line(r, 1, "B"); r.record("D", state="current"); r.commit("b: fulfil D", "b1")
    r.record("X", role="drop", target="D"); r.commit("b: drop X", "b2")
    r.record("X", role="drop", state="fulfilled", target="D"); r.record("D", state="withdrawn"); r.commit("b: fulfil X", "b3")
    r.co("main"); cond(r, "D", "c-E"); r.record("D"); r.commit("e: c-E", "e1")
    d = dshow(r, ".ait/D", "D")
    choose(r, ".ait/D", T, d["latest"], "pull D=a1", "pick", merge="a")
    v = r.blob_at("HEAD", ".ait/D")
    print(f"  result {v[:7]}: rule1={rule1(r, v)} state={fields(r.cat(v))['state'][0]} contains mine and every latest? "
          f"{all(contains(r, v, x) for x in d['latest'])}")

    H("REV1  main took a1 (D cancel) ; b: c-E, then git merge main, .ait/D resolved by hand as c-E future with prev {T, E}")
    r = base("REV1")
    r.co("a", True); T = r.record("D", state="cancelled"); r.commit("a: cancel", "a1")
    r.co("main"); r.g("merge", "-q", "--ff-only", "a")
    r.g("checkout", "-q", "-b", "b", "init"); cond(r, "D", "c-E"); E = r.record("D"); r.commit("b: c-E", "b1")
    x = r.g("merge", "--no-commit", "--no-ff", "main", check=False)
    print("  git merge main on b: exit", x.returncode, "unmerged", r.out("diff", "--name-only", "--diff-filter=U").split())
    cond(r, "D", "c-E"); N = r.record("D", prev=[T, E]); r.commit("b: merge main, keep c-E", "bM")
    r.co("main")
    print(f"  N={N[:7]} state={fields(r.cat(N))['state'][0]} prev holds the cancelled T? {T in prevs(r.cat(N))}")
    dshow(r, ".ait/D", "D")


# ------------------------------------------------------------------ check lines (decision 22)
def CK():
    H("CK4  U6, then 'pull D=a1'")
    r = base("CK4", d_state="current")
    r.co("a", True); line(r, 1, "A"); vA = r.record("D", state="current"); r.commit("a: recheck", "a1")
    r.co("main"); r.co("b", True); line(r, 1, "B"); r.record("D", state="current"); r.commit("b: recheck", "b1")
    r.co("main")
    d = dshow(r, ".ait/D", "D")
    choose(r, ".ait/D", vA, d["latest"], "pull D=a1", "pick", merge="a")
    v = r.blob_at("HEAD", ".ait/D")
    print(f"  result: {fields(r.cat(v)).get('check')} check_ok={check_ok(r.cat(v))} rule1={rule1(r, v)}")

    H("CK5  D current, checked with --force ; a1: the same pins checked again with -r ; main: consumer only")
    r = base("CK5", d_state="current", extra=[lambda r: r.record("D", state="current", check="forced", prev=["-"])])
    print("  init check:", fields(r.cat(r.blob_at("HEAD", ".ait/D"))).get("check"))
    r.co("a", True); a1v = r.record("D", state="current", check="reported"); r.commit("a: recheck -r", "a1")
    r.co("main"); r.write("src/order.c", "order E\n"); e1v = r.record("D", state="current", check="forced"); r.commit("e: consumer", "e1")
    print("  body(a1) == body(D0)?", body(r.cat(a1v)) == body(r.cat(r.blob_at("init", ".ait/D"))),
          " a1 and e1 PINS equal?", fields(r.cat(a1v))["check"][0].split()[1] == fields(r.cat(e1v))["check"][0].split()[1])
    res = {}
    for br in ("main", "a"):
        r.co(br)
        d = dshow(r, ".ait/D", f"D on {br}")
        b = body(r.cat(d["remaining"][0]))
        res[br] = compose(b, new_check_line(r, b, d["pool"]), d["latest"])
        print(f"  new version on {br}: check = {new_check_line(r, b, d['pool'])}")
    print("  main's and a's new versions are byte-identical?", res["main"] == res["a"])
    r.co("main")

    H("CK6  a1 fulfils D with -r, b1 fulfils D with --force, same code")
    r = base("CK6")
    r.co("a", True); line(r, 1, "A"); r.record("D", state="current", check="reported"); r.commit("a: fulfil -r", "a1")
    r.co("main"); r.co("b", True); line(r, 1, "A"); r.record("D", state="current", check="forced"); r.commit("b: fulfil --force", "b1")
    r.co("main")
    d = dshow(r, ".ait/D", "D")
    b = body(r.cat(d["remaining"][0]))
    print("  new version's check line:", new_check_line(r, b, d["pool"]))


# ------------------------------------------------------------------ helper clauses on a new version (r30 51)
def IND():
    H("IND2  a fulfils D (current) ; main: D c-E ; main 'pull D=a1 future amend D' ; then a fresh clone at init pulls D")
    r = base("IND2")
    r.co("a", True); line(r, 1, "A"); vA = r.record("D", state="current"); r.commit("a: fulfil D", "a1")
    r.co("main"); cond(r, "D", "c-E"); vE = r.record("D"); r.commit("e: c-E", "e1")
    r.g("merge", "--no-commit", "--no-ff", "a", check=False)
    bA = body(r.cat(vA))
    V1 = r.g("hash-object", "-w", "--stdin", input=compose(bA, new_check_line(r, bA, [vA, vE]), [vA, vE])).stdout.strip()
    amended = bA.replace("state current", "state future")
    for label, prev in (("revision 4: prev = the version pull decided (V1)", [V1]), ("revision 5: prev = V1's prev", [vA, vE])):
        r.write(".ait/D", compose(amended, None, prev)); restore_pinned(r, r.read(".ait/D"))
        r.g("add", "-A"); t = r.out("write-tree")
        c = r.out("commit-tree", t, "-p", "HEAD", "-p", "a", "-m", "pull D=a1 future amend D")
        r.g("branch", "-f", "res", c)
        cl = clone(r, "IND2-clone")
        cl.g("checkout", "-q", "-B", "b", "origin/main~1")  # an agent still at main before the pull
        cl.g("branch", "-D", "main")
        print(f"  -- {label}: V1 in the clone? {cl.cat(V1) is not None}")
        dshow(cl, ".ait/D", "D seen from the clone")
    r.g("merge", "--abort", check=False)


# ------------------------------------------------------------------ malformed, entangled accident, hints (r30 54, 55, 58)
def BAD():
    H("MF1  a1 writes .ait/D with CRLF line ends")
    r = base("MF1")
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); t = r.read(".ait/D"); r.write(".ait/D", t.replace("\n", "\r\n")); r.commit("a: CRLF", "a1")
    r.co("main")
    dshow(r, ".ait/D", "D")

    H("ACC1  D current, drop X ; a1 fulfils X (D withdrawn) ; a2 turns D back to current by hand ; a3 changes Q ; 'pull Q'")
    r = base("ACC1", d_state="current", extra=[lambda r: (r.record("X", role="drop", target="D"), cond(r, "Q", "q0"), r.record("Q"))])
    r.co("a", True); r.record("X", role="drop", state="fulfilled", target="D"); r.record("D", state="withdrawn"); r.commit("a: fulfil X", "a1")
    r.record("D", state="current"); r.commit("a: D back to current by hand", "a2")
    cond(r, "Q", "q-A"); r.record("Q"); a3 = r.commit("a: Q", "a3")
    r.co("main")
    held = {k: r.blob_at(a3, k) for k in (".ait/D", ".ait/X", ".ait/Q")}
    for k in (".ait/Q", ".ait/X", ".ait/D"):
        dshow(r, k, k, extra=[held[k]])
    wt = r.path + "-wt"; shutil.rmtree(wt, ignore_errors=True)
    r.g("worktree", "add", "-q", "--detach", wt, "HEAD")
    run(wt, "git", "merge", "--no-commit", "--no-ff", a3, check=False)
    for k in (".ait/D",):
        open(os.path.join(wt, k), "w").write(r.cat(r.blob_at("HEAD", k)))
    print("  received result with X and Q from a3, D mine (a's D skipped): validate:", flows.validate(wt) or "pass")
    run(wt, "git", "merge", "--abort", check=False); r.g("worktree", "remove", "--force", wt)

    H("F2  C2 (a1 fulfils D, main changes stock.c line 9) and both change src/other.c")
    r = base("F2", extra=[lambda r: r.write("src/other.c", "o0\n")])
    r.co("a", True); line(r, 1, "A"); r.record("D", state="current"); r.write("src/other.c", "oA\n"); r.commit("a: fulfil D", "a1")
    r.co("main"); line(r, 9, "E"); r.write("src/other.c", "oE\n"); r.commit("e: code", "e1")
    tree, rc, conf = merge_tree(r, "a")
    pin = [l for l in r.cat(r.blob_at("a", ".ait/D")).splitlines() if l.startswith("provider ")][0].split()[2]
    print(f"  merge-tree HEAD a1: exit={rc} conflicts={conf} ; merged stock.c == D's pin? {r.out('rev-parse', tree + ':src/stock.c') == pin}")

    H("XT1  D current, drop X ; a1 fulfils X (D withdrawn) ; main cancels X ; 'pull D X'")
    r = base("XT1", d_state="current", extra=[lambda r: r.record("X", role="drop", target="D")])
    r.co("a", True); r.record("X", role="drop", state="fulfilled", target="D"); r.record("D", state="withdrawn"); r.commit("a: fulfil X", "a1")
    r.co("main"); r.record("X", role="drop", state="cancelled", target="D"); r.commit("e: cancel X", "e1")
    dshow(r, ".ait/D", "D"); dshow(r, ".ait/X", "X")


# ------------------------------------------------------------------ spec files and code (r30 52), notifications (r30 56)
def CODE():
    H("CP2  tests/d.sh is code ; a0 changes it (tA) ; a1 changes Q ; main e1 makes D pin it as an input (t0) ; 'pull Q'")
    r = base("CP2", extra=[lambda r: (r.write("tests/d.sh", "t0\n"), cond(r, "Q", "q0"), r.record("Q"))])
    r.co("a", True); r.write("tests/d.sh", "tA\n"); r.commit("a: code", "a0"); cond(r, "Q", "q-A"); r.record("Q"); a1 = r.commit("a: Q", "a1")
    r.co("main"); r.write("specs/d.yaml", "body: c0\ninputs: [tests/d.sh]\n"); r.record("D", inputs=["tests/d.sh"]); r.commit("e: D pins tests/d.sh", "e1")
    tree, rc, conf = merge_tree(r, a1)
    tA = r.out("rev-parse", f"{a1}:tests/d.sh")
    pinned_by = []
    for c in r.out("rev-list", a1, "^HEAD").split():
        for p in r.out("ls-tree", "-r", "--name-only", c, ".ait").split():
            t = r.cat(r.blob_at(c, p)) or ""
            if any(l.split()[1:3] == ["tests/d.sh", "100644"] and l.split()[3] == tA for l in t.splitlines() if l.startswith("file ")):
                pinned_by.append((r.name(c), p))
    print(f"  D pins tests/d.sh = {[l.split()[3][:7] for l in r.read('.ait/D').splitlines() if 'tests/d.sh' in l]} (t0 = {r.out('rev-parse', 'HEAD:tests/d.sh')[:7]})")
    print(f"  merge-tree HEAD a1: exit={rc} ; tests/d.sh in the merged tree = {r.out('rev-parse', tree + ':tests/d.sh')[:7]} (tA = {tA[:7]})")
    print(f"  versions in the taken history that pin tests/d.sh at tA: {pinned_by or 'none'}")
    dshow(r, ".ait/Q", "Q")

    H("NU2  D current (a1) ; a2, a3 plain code changes (unverified) ; a4 changes Q ; main changes stock.c line 9 ; 'pull D Q'")
    r = base("NU2", extra=[lambda r: (cond(r, "Q", "q0"), r.record("Q"))])
    r.co("a", True); line(r, 1, "A"); r.record("D", state="current"); r.commit("a: fulfil", "a1")
    line(r, 2, "X"); r.commit("a: code", "a2"); line(r, 2, "Y"); r.commit("a: code again", "a3")
    cond(r, "Q", "q-A"); r.record("Q"); a4 = r.commit("a: Q", "a4")
    r.co("main"); line(r, 9, "E"); r.commit("e: code", "e1")
    pin = [l for l in r.cat(r.blob_at(a4, ".ait/D")).splitlines() if l.startswith("provider ")][0].split()[2]
    tree, rc, conf = merge_tree(r, a4)
    print(f"  D's pin {pin[:7]} ; a3/a4 stock.c {r.out('rev-parse', a4 + ':src/stock.c')[:7]} (unverified there) ; "
          f"merged stock.c {r.out('rev-parse', tree + ':src/stock.c')[:7]} (exit {rc})")

    H("CP3  a1 and b1 each write the same D version by hand (agent-a, agent-b) ; main: c-E")
    r = base("CP3")
    r.co("a", True); cond(r, "D", "c-A"); vA = r.record("D"); r.commit("a: c-A\n\nAit-Agent: agent-a", "a1")
    r.co("main"); r.co("b", True); cond(r, "D", "c-A"); vB = r.record("D"); r.commit("b: c-A\n\nAit-Agent: agent-b", "b1")
    r.co("main"); cond(r, "D", "c-E"); r.record("D"); r.commit("e: c-E", "e1")
    print("  same blob?", vA == vB)
    dshow(r, ".ait/D", "D")
    for c, n in introducing(r, ".ait/D", vA):
        print(f"  introducing {r.name(c)} ({n} new) Ait-Agent={r.out('log', '-1', '--format=%(trailers:key=Ait-Agent,valueonly)', c)!r}")

    H("RN1  a cancels D and moves specs/d.yaml to src/moved.c ; main edits the spec and re-pins D")
    r = base("RN1")
    r.write("specs/d.yaml", "".join(f"row{i}: 0\n" for i in range(30))); r.record("D"); r.commit("long spec", "s")
    r.co("a", True); r.record("D", state="cancelled"); r.g("mv", "specs/d.yaml", "src/moved.c"); r.commit("a: cancel D, move spec", "a1")
    r.co("main"); t = r.read("specs/d.yaml").replace("row15: 0", "row15: E"); r.write("specs/d.yaml", t); r.record("D"); r.commit("e: spec row15", "e1")
    tree, rc, conf = merge_tree(r, "a")
    print(f"  merge-tree: exit={rc} conflicts={conf} ; src/moved.c row15 in the merged tree:",
          [l for l in r.out("cat-file", "-p", tree + ":src/moved.c").splitlines() if l.startswith("row15")])


# ------------------------------------------------------------------ several ends (decision 20), working tree
def MH():
    H("MH2'  'pull D Q' : D from a1, Q from b1 ; a1 and b1 change src/x.c differently ; main did not touch x.c")
    r = base("MH2r", extra=[lambda r: (cond(r, "Q", "q0"), r.record("Q"), r.write("src/x.c", "x=0\n"))])
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); r.write("src/x.c", "x=A\n"); a1 = r.commit("a", "a1")
    r.co("main"); r.co("b", True); cond(r, "Q", "q-B"); r.record("Q"); r.write("src/x.c", "x=B\n"); b1 = r.commit("b", "b1")
    r.co("main"); h0 = r.out("rev-parse", "HEAD")
    st = steps(r, [a1, b1])
    first, last = (a1, b1) if st[0][0] == "a1" else (b1, a1)  # the clean end first
    t1 = r.out("merge-tree", "--write-tree", "HEAD", first)
    c1 = r.out("commit-tree", t1, "-p", "HEAD", "-p", first, "-m", "tmp")
    m2 = r.g("merge-tree", "--write-tree", c1, last, check=False)
    r.g("read-tree", "-m", "-u", "HEAD", m2.stdout.split()[0])
    stages = [l for l in m2.stdout.splitlines()[1:] if "\t" in l]
    r.g("update-index", "--index-info", input="".join(f"0 {'0' * 40}\t{p}\n" for p in {l.split(chr(9))[1] for l in stages}) + "".join(l + "\n" for l in stages))
    open(os.path.join(r.path, ".git/MERGE_HEAD"), "w").write(f"{r.out('rev-parse', first)}\n{r.out('rev-parse', last)}\n")
    open(os.path.join(r.path, ".git/MERGE_MSG"), "w").write("pull D Q\n"); open(os.path.join(r.path, ".git/MERGE_MODE"), "w").write("no-ff")
    print(f"  one merge state: status={r.out('status', '--porcelain').splitlines()} HEAD unchanged? {r.out('rev-parse', 'HEAD') == h0}")
    H("MH4  MH2' and 'git merge --abort'")
    r.g("merge", "--abort")
    print(f"  after abort: HEAD unchanged? {r.out('rev-parse', 'HEAD') == h0} status={r.out('status', '--porcelain').splitlines()}")
    H("MH3  MH2' again, resolved and finished ; then the same command again")
    r.g("read-tree", "-m", "-u", "HEAD", m2.stdout.split()[0])
    r.g("update-index", "--index-info", input="".join(f"0 {'0' * 40}\t{p}\n" for p in {l.split(chr(9))[1] for l in stages}) + "".join(l + "\n" for l in stages))
    open(os.path.join(r.path, ".git/MERGE_HEAD"), "w").write(f"{r.out('rev-parse', first)}\n{r.out('rev-parse', last)}\n")
    open(os.path.join(r.path, ".git/MERGE_MSG"), "w").write("pull D Q\n"); open(os.path.join(r.path, ".git/MERGE_MODE"), "w").write("no-ff")
    r.write("src/x.c", "x=resolved\n"); r.g("add", "src/x.c"); r.g("commit", "-q", "--no-edit", "--no-verify")
    print("  parents of the finishing commit:", [r.name(p) for p in r.out("rev-list", "--parents", "-n1", "HEAD").split()[1:]])
    dshow(r, ".ait/D", "D"); dshow(r, ".ait/Q", "Q")

    H("MH5  D and O future, undertaken ; a1 fulfils O (and D), x.c=A ; b1 fulfils D only (other consumer), x.c=B ; 'pull D=b1 O'")
    def ex(r):
        r.write("src/o.c", "o\n"); cond(r, "O", "o0"); r.record("O", role="offer", provider="src/o.c")
        r.record("D-O", role="undertake", state="undertaken"); r.write("src/x.c", "x=0\n")
    r = base("MH5", extra=[ex])
    r.co("a", True); line(r, 1, "A"); r.record("D", state="current"); r.record("O", role="offer", provider="src/o.c", state="current"); r.write("src/x.c", "x=A\n"); a1 = r.commit("a: fulfil O (and D)", "a1")
    r.co("main"); r.co("b", True); r.write("src/order.c", "order B\n"); line(r, 1, "A"); vDb = r.record("D", state="current"); r.write("src/x.c", "x=B\n"); b1 = r.commit("b: fulfil D", "b1")
    r.co("main")
    dshow(r, ".ait/D", "D"); dshow(r, ".ait/O", "O")
    st = steps(r, [a1, b1])
    wt = r.path + "-wt"; shutil.rmtree(wt, ignore_errors=True)
    r.g("worktree", "add", "-q", "--detach", wt, "HEAD")
    for h in ([a1, b1] if st[0][0] == "a1" else [b1, a1]):
        run(wt, "git", "merge", "--no-commit", "--no-ff", h, check=False)
        run(wt, "git", "checkout", "--theirs", "--", ".", check=False)
        open(os.path.join(wt, ".ait/D"), "w").write(r.cat(vDb)); open(os.path.join(wt, ".ait/O"), "w").write(r.cat(r.blob_at(a1, ".ait/O")))
        open(os.path.join(wt, "src/x.c"), "w").write("x=resolved\n")
        run(wt, "git", "add", "-A"); run(wt, "git", "commit", "-q", "--no-verify", "-m", "step")
    print("  the one result (D from b1, O from a1): validate:", flows.validate(wt) or "pass")
    r.g("worktree", "remove", "--force", wt)

    H("MH6  'pull D Q' : D from a1 changes x.c, Q from b1 changes y.c ; main changed both x.c and y.c")
    r = base("MH6", extra=[lambda r: (cond(r, "Q", "q0"), r.record("Q"), r.write("src/x.c", "x=0\n"), r.write("src/y.c", "y=0\n"))])
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); r.write("src/x.c", "x=A\n"); a1 = r.commit("a", "a1")
    r.co("main"); r.co("b", True); cond(r, "Q", "q-B"); r.record("Q"); r.write("src/y.c", "y=B\n"); b1 = r.commit("b", "b1")
    r.co("main"); r.write("src/x.c", "x=E\n"); r.write("src/y.c", "y=E\n"); r.commit("e", "e1")
    steps(r, [a1, b1])
    for h in (a1, b1):
        x = r.g("merge-tree", "--write-tree", "--name-only", "HEAD", h, check=False)
        print(f"  alone: merge-tree HEAD {r.name(h)} exit={x.returncode} {x.stdout.split()[1:2]}")

    H("WT2  main took b1 (D@B) ; b reverts b1 and changes Q and x.c (b3) ; a1: Q2 ; main: x.c differently, uncommitted draft in specs/d.yaml ; 'pull Q Q2'")
    r = base("WT2", extra=[lambda r: (cond(r, "Q", "q0"), r.record("Q"), cond(r, "Q2", "p0"), r.record("Q2"), r.write("src/x.c", "x=0\n"))])
    r.co("b", True); cond(r, "D", "c-B"); r.record("D"); b1 = r.commit("b: D@B", "b1")
    r.co("main"); r.g("merge", "-q", "--ff-only", "b")
    r.co("b"); r.g("revert", "--no-edit", b1); cond(r, "Q", "q-B"); r.record("Q"); r.write("src/x.c", "x=B\n"); b3 = r.commit("b: Q, x.c", "b3")
    r.co("main"); r.co("a", True); cond(r, "Q2", "p-A"); r.record("Q2"); a1 = r.commit("a: Q2", "a1")
    r.co("main"); r.write("src/x.c", "x=E\n"); r.commit("e: x.c", "e1"); r.write("specs/d.yaml", "body: my draft\n")
    dshow(r, ".ait/D", "D", extra=[r.blob_at(b3, ".ait/D")])
    st = steps(r, [a1, b3])
    first, last = (a1, b3) if st[0][0] == "a1" else (b3, a1)
    c1 = r.out("commit-tree", r.out("merge-tree", "--write-tree", "HEAD", first), "-p", "HEAD", "-p", first, "-m", "tmp")
    m2 = r.g("merge-tree", "--write-tree", c1, last, check=False)
    t = m2.stdout.split()[0]
    # the received result keeps D and its spec file as mine (b3's D is an old version)
    r.g("read-tree", "--index-output=" + os.path.join(r.path, ".git", "tmp-index"), t)
    env = dict(ENV, GIT_INDEX_FILE=os.path.join(r.path, ".git", "tmp-index"))
    for p in (".ait/D", "specs/d.yaml"):
        run(r.path, "git", "update-index", "--add", "--cacheinfo", f"100644,{r.blob_at('HEAD', p)},{p}", env=env)
    t2 = run(r.path, "git", "write-tree", env=env, check=False).stdout.strip() or t
    print(f"  paths where the received result differs from HEAD: {changed_paths(r, r.out('rev-parse', 'HEAD^{tree}'), t2)}")
    x = r.g("read-tree", "-m", "-u", "HEAD", t2, check=False)
    print(f"  read-tree -m -u HEAD <result>: exit={x.returncode} ; draft kept? {r.read('specs/d.yaml') == 'body: my draft' + chr(10)}")
    r.g("reset", "-q", "--hard")


if __name__ == "__main__":
    for f in (sys.argv[1:] or ["CYC", "KC", "MS", "TW", "CK", "IND", "BAD", "CODE", "MH"]):
        globals()[f]()
