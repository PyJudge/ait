"""Scenarios added in pull-criteria revision 6 (r33 review, decisions 26-27). Prints git facts and the data steps only;
the ait outcome is written by hand in pull-criteria.md."""
import sys, os, shutil
from lib import *
from scen import base, cond, line, restore_pinned, H
from scen4 import choose
from scen5 import clone, code_conflicts
import flows


def steps26(r, ends, decided=()):
    """decision 26 (record 86, r36): try the orders of the ends, first by their commit IDs; in an order, an end that
    changes a file that already conflicts makes the order fail, conflicts or not. Take the first order where each file
    conflicts at most once; if none, stop."""
    import itertools
    oid = {h: r.out("rev-parse", h) for h in ends}
    for order in sorted(itertools.permutations(ends), key=lambda o: [oid[h] for h in o]):
        cur, conflicted, out = r.out("rev-parse", "HEAD"), [], []
        for h in order:
            base_ = r.out("merge-base", cur, h)
            again = [p for p in r.out("diff", "--name-only", base_, h).split() if p in conflicted]
            if again:
                out = None
                break
            rc, tree, code = code_conflicts(r, cur, h, decided)
            out.append((r.name(oid[h]), "conflicts in " + str(code) if code else "no code conflict"))
            conflicted += code
            cur = r.out("commit-tree", tree, "-p", cur, "-p", h, "-m", "tmp")
        if out is not None:
            print(f"  decision 26: order {out} ; files in conflict: {conflicted}")
            return out, conflicted
    print(f"  decision 26: STOP: in every order some file would conflict twice ({[r.name(oid[h]) for h in ends]})")
    return None, None


def reopen_version(repo, v):
    """decision 27 (record 87): a new future version whose prev holds a current version C, and no future version
    in its prev contains C"""
    t = repo.cat(v)
    if fields(t)["state"][0] != "future":
        return []
    ps = prevs(t)
    out = []
    for c in ps:
        ct = repo.cat(c)
        if ct and fields(ct)["state"][0] == "current":
            carried = [p for p in ps if p != c and repo.cat(p) and fields(repo.cat(p))["state"][0] == "future" and contains(repo, p, c)]
            if not carried:
                out.append(c)
    return out


# ------------------------------------------------------------------ accidents (record 67)
def ACC():
    H("REV2  REV1, then b2 writes c-F on top of the accident (prev N)")
    r = base("REV2")
    r.co("a", True); T = r.record("D", state="cancelled"); r.commit("a: cancel", "a1")
    r.co("main"); r.g("merge", "-q", "--ff-only", "a")
    r.g("checkout", "-q", "-b", "b", "init"); cond(r, "D", "c-E"); E = r.record("D"); r.commit("b: c-E", "b1")
    r.g("merge", "--no-commit", "--no-ff", "main", check=False)
    cond(r, "D", "c-E"); N = r.record("D", prev=[T, E]); r.commit("b: hand resolution (accident)", "bM")
    cond(r, "D", "c-F"); N2 = r.record("D"); r.commit("b: c-F on top", "b2")
    r.co("main")
    print(f"  N rule1={rule1(r, N)} ; N2 rule1={rule1(r, N2)} (prev={[p[:7] for p in prevs(r.cat(N2))]}) ; N2 contains T? {contains(r, N2, T)}")
    dshow(r, ".ait/D", "D on main")

    H("REV3  main holds only the cancelled D (C) ; b: P (future from C by hand), W (future, prev P)")
    r = base("REV3")
    C = r.record("D", state="cancelled"); r.commit("c1: cancel", "c1")
    r.co("b", True); P = r.record("D", state="future"); r.commit("b1: future by hand", "b1")
    cond(r, "D", "c-W"); W = r.record("D"); r.commit("b2: on top", "b2"); r.co("main")
    print(f"  P rule1={rule1(r, P)} ; W rule1={rule1(r, W)} ; W contains C? {contains(r, W, C)}")
    dshow(r, ".ait/D", "D on main")

    H("ID1  a2 writes a new contract W as current without a check line ; main has no W ; 'pull W'")
    r = base("ID1")
    r.co("a", True); cond(r, "W", "w-A"); r.record("W", state="current", check=None); r.commit("a: W current, no check", "a2")
    r.co("main")
    dshow(r, ".ait/W", "W")


# ------------------------------------------------------------------ files (records 68-71)
def FILES():
    H("SP1  a1: a plain commit edits Q's pinned spec (no re-pin) ; a2: D c-A ; 'pull D'")
    r = base("SP1", extra=[lambda r: (cond(r, "Q", "q0"), r.record("Q"))])
    r.co("a", True); r.write("specs/q.yaml", "body: q1\n"); r.commit("a1: plain commit edits specs/q.yaml", "a1")
    cond(r, "D", "c-A"); r.record("D"); a2 = r.commit("a2: D c-A", "a2")
    r.co("main"); r.write("src/x.c", "x\n"); r.commit("e: code", "e1")
    qa = r.cat(r.blob_at(a2, ".ait/Q"))
    pin = [l for l in qa.splitlines() if l.startswith("file specs/q.yaml")][0].split()[3]
    tree, rc, conf = merge_tree(r, a2)
    print(f"  at a2: Q pins specs/q.yaml at {pin[:7]} ; the file there is {r.blob_at(a2, 'specs/q.yaml')[:7]} (declared on the incoming side, differs from its pin)")
    print(f"  merge-tree HEAD a2: exit={rc} ; merged specs/q.yaml = {r.out('rev-parse', tree + ':specs/q.yaml')[:7]} ; HEAD's = {r.blob_at('HEAD', 'specs/q.yaml')[:7]}")
    dshow(r, ".ait/Q", "Q")

    H("CP4  tests/d.sh is code ; a0: tA ; a1: Q ; main: tE and D pins tests/d.sh as an input ; 'pull Q'")
    r = base("CP4", extra=[lambda r: (r.write("tests/d.sh", "t0\n"), cond(r, "Q", "q0"), r.record("Q"))])
    r.co("a", True); r.write("tests/d.sh", "tA\n"); r.commit("a: code", "a0"); cond(r, "Q", "q-A"); r.record("Q"); a1 = r.commit("a: Q", "a1")
    r.co("main"); r.write("tests/d.sh", "tE\n"); r.write("specs/d.yaml", "body: c0\ninputs: [tests/d.sh]\n"); r.record("D", inputs=["tests/d.sh"]); r.commit("e: D pins tests/d.sh", "e1")
    x = r.g("merge-tree", "--write-tree", "--name-only", "HEAD", a1, check=False)
    declared = [p for c in r.out("rev-list", a1, "^HEAD").split() for p in r.out("ls-tree", "-r", "--name-only", c, ".ait").split()
                if "tests/d.sh" in (r.cat(r.blob_at(c, p)) or "")]
    print(f"  merge-tree HEAD a1: exit={x.returncode} {x.stdout.splitlines()[1:2]} ; contracts on the incoming side that declare tests/d.sh: {declared or 'none'}")

    H("PV1b  PV1: the cancelled result pins no spec ; git merges specs/d.yaml as code")
    r = base("PV1b")
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); r.commit("a: c-A", "a1"); r.record("D", state="cancelled"); r.commit("a: cancel", "a2")
    r.co("main"); cond(r, "D", "c-E"); r.record("D"); r.commit("e: c-E", "e1")
    d = decide(r, ".ait/D")
    print(f"  result: {d['kind']} (state {fields(r.cat(d['remaining'][0]))['state'][0]})")
    rc, tree, code = code_conflicts(r, "HEAD", "a")
    x = r.g("merge-tree", "--write-tree", "--name-only", "HEAD", "a", check=False)
    print(f"  merge-tree HEAD a2: exit={x.returncode} conflicts={[l for l in x.stdout.splitlines()[1:] if l][:2]} ; specs/d.yaml is pinned by no contract of the result")

    H("MF2  a0: Z in CRLF ; a1: Y made current by hand ; a2: new W (current, no check) and .ait/notes.txt ; a3: Q ; 'pull Q'")
    def ex(r):
        for k in ("Z", "Y", "Q"):
            cond(r, k, f"{k.lower()}0"); r.record(k)
    r = base("MF2", extra=[ex])
    r.co("a", True)
    cond(r, "Z", "z-A"); r.record("Z"); t = r.read(".ait/Z"); r.write(".ait/Z", t.replace("\n", "\r\n")); r.commit("a0: Z CRLF", "a0")
    line(r, 1, "Y"); r.record("Y", state="current", check=None); r.commit("a1: Y current by hand", "a1")
    cond(r, "W", "w-A"); r.record("W", state="current", check=None); r.write(".ait/notes.txt", "hello\n"); r.commit("a2: W, notes", "a2")
    cond(r, "Q", "q-A"); r.record("Q"); a3 = r.commit("a3: Q", "a3")
    r.co("main")
    tree, rc, conf = merge_tree(r, a3)
    print(f"  paths the merge would change: {changed_paths(r, r.out('rev-parse', 'HEAD^{tree}'), tree)}")
    for k in (".ait/Q", ".ait/Z", ".ait/Y", ".ait/W"):
        d = decide(r, k, extra=[r.blob_at(a3, k)])
        print(f"  {k}: malformed={[b[:7] for b in d['bad']]} accident={[b[:7] for b in d['acc']]} => {d['kind']}")

    H("WT3  WT1: specs/d.yaml, which has the uncommitted draft, is pinned by HEAD's D")
    r = base("WT3")
    print(f"  HEAD's D pins: {[l.split()[1] for l in r.cat(r.blob_at('HEAD', '.ait/D')).splitlines() if l.startswith('file ')]}")

    H("S5  main's current K6 pins src/stock.c ; a1 changes D and moves src/stock.c to src/inv.c ; 'pull D'")
    r = base("S5")
    r.co("a", True); cond(r, "D", "c-A"); r.g("mv", "src/stock.c", "src/inv.c"); r.record("D", provider="src/inv.c"); a1 = r.commit("a: D, move stock.c", "a1")
    r.co("main"); cond(r, "K6", "k0"); r.record("K6", state="current", provider="src/stock.c", consumer="src/order.c"); r.commit("main: K6 after a branched", "e1")
    print(f"  incoming parent has K6? {r.blob_at(a1, '.ait/K6') is not None}")
    tree, rc, conf = merge_tree(r, a1)
    print(f"  merge-tree exit={rc} ; in the result: stock.c? {bool(r.g('rev-parse', '-q', '--verify', tree + ':src/stock.c', check=False).stdout.strip())} inv.c? "
          f"{bool(r.g('rev-parse', '-q', '--verify', tree + ':src/inv.c', check=False).stdout.strip())} ; in HEAD: inv.c? {r.blob_at('HEAD', 'src/inv.c') is not None}")


# ------------------------------------------------------------------ results, checks, reopen (records 72-74, decision 27)
def RES():
    H("CK7  H: D current reported ; a1: consumer only, check line written forced by hand (same PINS), prev H")
    r = base("CK7", d_state="current")
    r.co("a", True); r.write("src/order.c", "order A\n"); V = r.record("D", state="current", check="forced"); r.commit("a1", "a1"); r.co("main")
    d = dshow(r, ".ait/D", "D")
    print(f"  a1's check: {fields(r.cat(V))['check']} ; decision 22 over latest and mine: {new_check_line(r, body(r.cat(V)), d['pool'])}")

    H("IND3  a1 fulfils D ; main: c-E ; agA: 'pull D=a1 future amend D' (prev = V1 and V1's prev) ; agB: 'pull D=a1' (= V1) ; each pulls the other")
    r = base("IND3")
    r.co("a", True); line(r, 1, "A"); vA = r.record("D", state="current"); r.commit("a: fulfil D", "a1")
    r.co("main"); cond(r, "D", "c-E"); vE = r.record("D"); r.commit("e: c-E", "e1")
    r.co("main"); r.co("agB", True); V1 = choose(r, ".ait/D", vA, [vA, vE], "B: pull D=a1", "B1", merge="a"); V1 = r.blob_at("HEAD", ".ait/D")
    r.co("main"); r.co("agA", True)
    r.g("merge", "--no-commit", "--no-ff", "a", check=False)
    bA = body(r.cat(vA))
    v1 = r.g("hash-object", "-w", "--stdin", input=compose(bA, new_check_line(r, bA, [vA, vE]), [vA, vE])).stdout.strip()
    W = compose(bA.replace("state current", "state future"), None, [v1, vA, vE])
    r.write(".ait/D", W); restore_pinned(r, W); r.commit("A: pull D=a1 future amend D", "A1")
    print(f"  B's result is A's intermediate V1? {V1 == v1}")
    for br in ("agA", "agB"):
        r.co(br); dshow(r, ".ait/D", f"D on {br}")
    r.co("main")
    c = clone(r, "IND3-clone"); c.g("checkout", "-q", "-B", "x", "origin/main"); c.g("branch", "-D", "main", check=False)
    c.g("update-ref", "-d", "refs/remotes/origin/agB")
    c.g("reflog", "expire", "--expire=now", "--all"); c.g("gc", "-q", "--prune=now")
    print(f"  a clone without B: V1 present? {c.cat(v1) is not None}")
    dshow(c, ".ait/D", "D in a clone without B")
    r.co("main")

    H("U1c  a1 fulfils D (current) ; main: c-E ; main 'pull D=HEAD' ; and T1r: a reopens D, main takes it")
    r = base("U1c")
    r.co("a", True); line(r, 1, "A"); vA = r.record("D", state="current"); r.commit("a: fulfil D", "a1")
    r.co("main"); cond(r, "D", "c-E"); vE = r.record("D"); r.commit("e: c-E", "e1")
    choose(r, ".ait/D", vE, [vA, vE], "pull D=HEAD", "kept")
    k = r.blob_at("HEAD", ".ait/D")
    print(f"  kept version: state={fields(r.cat(k))['state'][0]} ; the kept commit reopens (decision 27): {[c[:7] for c in reopen_commit(r, 'HEAD', '.ait/D')]} (a's = {vA[:7]})")
    r = base("T1r", d_state="current")
    r.co("a", True); line(r, 1, "A"); vA = r.record("D", state="current"); r.commit("a: recheck", "a1")
    vR = r.record("D", state="future"); r.commit("a: reopen D (future amend)", "a2")
    r.co("main"); r.write("src/order.c", "order E\n"); r.commit("e: code only", "e1")
    d = dshow(r, ".ait/D", "D (a reopened D ; main did not change it)")
    print(f"  a's reopen commit a2 reopens (decision 27 / v14 1.5d): {[c[:7] for c in reopen_commit(r, 'a', '.ait/D')]} (a's own = {vA[:7]}) ; main writes no commit of its own for D")

    H("MS3  C's prev is a blob that is missing ; V (body of C) and W (changed) both have prev C")
    r = base("MS3")
    gone = "a35e538afcff9e0211e10d27139aef29b335ebe8"
    C = r.record("D", prev=[gone]); r.commit("C (prev is a missing blob)", "c1")
    r.co("a", True); cond(r, "D", "c-W"); r.record("D"); r.commit("a: W", "a1")
    r.co("main"); V = r.record("D"); r.commit("e: same body, prev C", "e1")
    print(f"  missing blob present? {r.cat(gone) is not None} ; body(V) == body(C)? {body(r.cat(V)) == body(r.cat(C))}")
    dshow(r, ".ait/D", "D")

    H("RV3b  a1 fulfils D ; main takes a1, then git revert a1 ; 'pull D'")
    r = base("RV3b")
    r.co("a", True); line(r, 1, "A"); vA = r.record("D", state="current"); r.commit("a: fulfil D", "a1")
    r.co("main"); r.g("merge", "-q", "--ff-only", "a"); r.g("revert", "--no-edit", "HEAD")
    d = dshow(r, ".ait/D", "D")
    pin = [l for l in r.cat(vA).splitlines() if l.startswith("provider ")][0].split()[2]
    print(f"  a1 is an ancestor of HEAD? {r.g('merge-base', '--is-ancestor', 'a', 'HEAD', check=False).returncode == 0} ; "
          f"D@A pins stock.c {pin[:7]} ; HEAD's stock.c {r.blob_at('HEAD', 'src/stock.c')[:7]}")


# ------------------------------------------------------------------ links and several ends (decisions 23, 26)
def ENDS():
    H("K8  c1 undertakes D with O1 ; d1 undertakes D2 with O1 ; 'pull D'")
    def ex(r):
        r.write("src/o.c", "o\n"); cond(r, "O1", "o0"); r.record("O1", role="offer", provider="src/o.c")
        cond(r, "D2", "d0"); r.record("D2")
    r = base("K8", extra=[ex])
    r.co("c", True); r.record("D-O1", role="undertake", state="undertaken"); r.commit("c1", "c1")
    r.co("main"); r.co("d", True); r.record("D2-O1", role="undertake", state="undertaken"); r.commit("d1", "d1")
    r.co("main")
    links = {}
    for ref in ("c", "d"):
        for p in r.out("ls-tree", "-r", "--name-only", ref, ".ait/undertake").split():
            if r.blob_at("HEAD", p) is None:
                links[p] = ref
    print(f"  link records not in HEAD: {links}")
    names = {"D"}
    while True:
        more = {e for p in links for e in os.path.basename(p).split("-") if names & set(os.path.basename(p).split("-"))} - names
        if not more:
            break
        names |= more
    print(f"  names closed under 'a link with a named end': {sorted(names)}")

    def ex2(r):
        r.write("src/o.c", "o\n"); cond(r, "O", "o0"); r.record("O", role="offer", provider="src/o.c")
        r.write("src/x.c", "x=0\n"); r.write("src/y.c", "y=0\n")
    H("MH7  c1 undertakes D with O ; a1 (on c): D, x.c ; b1 (on c): O, y.c ; main: x.c and y.c ; 'pull D O'")
    r = base("MH7", extra=[ex2])
    r.co("c", True); r.record("D-O", role="undertake", state="undertaken"); r.commit("c1", "c1")
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); r.write("src/x.c", "x=A\n"); a1 = r.commit("a1", "a1")
    r.co("c"); r.co("b", True); cond(r, "O", "o-B"); r.record("O", role="offer", provider="src/o.c"); r.write("src/y.c", "y=B\n"); b1 = r.commit("b1", "b1")
    r.co("main"); r.write("src/x.c", "x=E\n"); r.write("src/y.c", "y=E\n"); h0 = r.commit("e1", "e1")
    out, conflicted = steps26(r, [a1, b1])
    # one merge state: code from the steps, contract files from a1 / b1 / c1 as decided (here: as is)
    cur = h0
    infos = []
    for h in sorted([a1, b1], key=lambda x: r.out("rev-parse", x)):
        x = r.g("merge-tree", "--write-tree", cur, h, check=False)
        infos += [l for l in x.stdout.splitlines()[1:] if "\t" in l and l.split("\t")[1].startswith("src/")]
        cur = r.out("commit-tree", x.stdout.split()[0], "-p", cur, "-p", h, "-m", "tmp")
    idx = os.path.join(r.path, ".git", "pull-index")
    env = dict(ENV, GIT_INDEX_FILE=idx)
    run(r.path, "git", "read-tree", r.out("rev-parse", cur + "^{tree}"), env=env)
    for k in (".ait/D", ".ait/O", ".ait/undertake/D-O", "specs/d.yaml", "specs/o.yaml"):
        src = a1 if k.endswith("D") or k.endswith("d.yaml") else b1 if k.endswith("O") or k.endswith("o.yaml") else "c"
        run(r.path, "git", "update-index", "--add", "--cacheinfo", f"100644,{r.blob_at(src, k)},{k}", env=env)
    t = run(r.path, "git", "write-tree", env=env).stdout.strip()
    r.g("read-tree", "-m", "-u", "HEAD", t)
    paths = sorted({l.split("\t")[1] for l in infos})
    r.g("update-index", "--index-info", input="".join(f"0 {'0' * 40}\t{p}\n" for p in paths) + "".join(l + "\n" for l in infos))
    open(os.path.join(r.path, ".git/MERGE_HEAD"), "w").write(f"{a1}\n{b1}\n")
    open(os.path.join(r.path, ".git/MERGE_MSG"), "w").write("pull D O\n"); open(os.path.join(r.path, ".git/MERGE_MODE"), "w").write("no-ff")
    print(f"  one merge state: {r.out('status', '--porcelain').splitlines()}")
    for k in (".ait/D", ".ait/O", ".ait/undertake/D-O"):
        v = r.out("rev-parse", f":{k}")
        starts = [x for x in (r.blob_at("HEAD", k), r.blob_at(a1, k), r.blob_at(b1, k)) if x]
        print(f"  HK1 data: index {k} rule1={rule1(r, v)} contains every start version? {all(contains(r, v, s) for s in starts)}")
    r.write("src/x.c", "x=R\n"); r.write("src/y.c", "y=R\n"); r.g("add", "src/x.c", "src/y.c"); r.g("commit", "-q", "--no-edit", "--no-verify")
    print("  finishing commit parents:", [r.name(p) for p in r.out("rev-list", "--parents", "-n1", "HEAD").split()[1:]])
    for k in (".ait/D", ".ait/O", ".ait/undertake/D-O"):
        print(f"  then {k}: {decide(r, k)['kind']}")

    H("MH8  as MH7, but b1 also changes x.c (O and x.c, y.c)")
    r = base("MH8", extra=[ex2])
    r.co("c", True); r.record("D-O", role="undertake", state="undertaken"); r.commit("c1", "c1")
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); r.write("src/x.c", "x=A\n"); a1 = r.commit("a1", "a1")
    r.co("c"); r.co("b", True); cond(r, "O", "o-B"); r.record("O", role="offer", provider="src/o.c"); r.write("src/x.c", "x=B\n"); b1 = r.commit("b1", "b1")
    r.co("main"); r.write("src/x.c", "x=E\n"); r.commit("e1", "e1")
    steps26(r, [a1, b1])

    H("MH6'  'pull D Q' : a1 (D) changes x.c, b1 (Q) changes y.c ; main changed both")
    r = base("MH6r", extra=[lambda r: (cond(r, "Q", "q0"), r.record("Q"), r.write("src/x.c", "x=0\n"), r.write("src/y.c", "y=0\n"))])
    r.co("a", True); cond(r, "D", "c-A"); r.record("D"); r.write("src/x.c", "x=A\n"); a1 = r.commit("a", "a1")
    r.co("main"); r.co("b", True); cond(r, "Q", "q-B"); r.record("Q"); r.write("src/y.c", "y=B\n"); b1 = r.commit("b", "b1")
    r.co("main"); r.write("src/x.c", "x=E\n"); r.write("src/y.c", "y=E\n"); r.commit("e", "e1")
    steps26(r, [a1, b1])


if __name__ == "__main__":
    for f in (sys.argv[1:] or ["ACC", "FILES", "RES", "ENDS"]):
        globals()[f]()
