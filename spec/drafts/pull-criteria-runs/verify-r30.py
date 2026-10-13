"""r31: my own reproduction of the key r30 review claims. Prints git facts and the criteria's data only.
The decide() lines use the current rules of lib.py, so they show the fixed outcome (r40: rerun with prev - on the first version)."""
import os, shutil, subprocess, sys
from lib import *

HERE = os.path.dirname(os.path.abspath(__file__))


def attach(path):
    r = Repo.__new__(Repo)
    r.path, r.short = path, {}
    return r


def base(name, extra=None):
    r = Repo(name)
    r.write("src/stock.c", "stock 0\n")
    r.write("src/order.c", "order 0\n")
    r.write("specs/d.yaml", "body: c0\n")
    for k, v in (extra or {}).items():
        r.write(k, v)
    d0 = r.record("D", prev=["-"])
    r.commit("init", "init")
    return r, d0


def setcond(r, cond, **kw):
    r.write("specs/d.yaml", f"body: {cond}\n")
    return r.record("D", **kw)


def V1():
    print("== V1 three-way cycle (Claude B B1, Codex B 1)")
    r, d0 = base("V1")
    v = {}
    for br, cond in (("a", "c-P"), ("b", "c-Q"), ("c", "c-R")):
        r.co("main"); r.co(br, new=True)
        v[cond] = setcond(r, cond); r.commit(f"{br}1", f"{br}1")
    P, Q, R = v["c-P"], v["c-Q"], v["c-R"]
    r.co("a"); a2 = setcond(r, "c-P", prev=[P, R]); r.commit("a2: D=HEAD after seeing c1", "a2")
    r.co("b"); b2 = setcond(r, "c-Q", prev=[P, Q]); r.commit("b2: D=HEAD after seeing a1", "b2")
    r.co("c"); c2 = setcond(r, "c-R", prev=[Q, R]); r.commit("c2: D=HEAD after seeing b1", "c2")
    r.co("main")
    n = {a2: "a2", b2: "b2", c2: "c2"}
    for x in (a2, b2, c2):
        for y in (a2, b2, c2):
            if x != y and unchanged_since_common(r, x, y):
                print(f"   {n[y]} moved forward from {n[x]}")
    S = [a2, b2, c2]
    simul = [x for x in S if not any(y != x and unchanged_since_common(r, x, y) for y in S)]
    print(f"   all pairs on the same set -> remaining {[n[x] for x in simul]}")
    d = decide(r, ".ait/D")
    print(f"   lib.decide (the current rules): remaining {[n.get(x, x[:7]) for x in d['remaining']]} => {d['kind']}")


def V2():
    print("== V2 future version whose prev holds a cancelled version (Claude B B2)")
    r, d0 = base("V2")
    r.co("a", new=True); T = r.record("D", state="cancelled"); r.commit("a1 cancel", "a1")
    r.co("main"); r.g("merge", "-q", "--ff-only", "a")
    r.co("init" if False else "a~1"); r.g("checkout", "-q", "-b", "b")
    E = setcond(r, "c-E"); r.commit("b1 c-E", "b1")
    N = setcond(r, "c-E", prev=[T, E]); r.commit("bM: hand-resolved merge result, future with prev {T,E}", "bM")
    r.co("main")
    print(f"   N rule1={rule1(r, N)}  N contains T? {contains(r, N, T)}  state(N)={fields(r.cat(N))['state'][0]}")
    d = decide(r, ".ait/D")
    print(f"   main (HEAD's D = cancelled): latest={[x[:7] for x in d['latest']]} terminal_won={d['terminal_won']} => {d['kind']}")


def V3():
    print("== V3 newest common version whose blob is missing (Claude B B3, Codex A F4)")
    r, d0 = base("V3")
    r.co("y", new=True); K = setcond(r, "c-K"); r.commit("y1 c-K", "y1")
    r.co("main"); r.co("q", new=True); M = setcond(r, "c-M"); q1 = r.commit("q1 c-M", "q1")
    w = setcond(r, "c-M", prev=[M, K]); r.commit("q2 D=HEAD after seeing y1", "q2")
    r.co("main"); r.co("p", new=True)
    r.g("merge", "-q", "--no-edit", "y")
    v = setcond(r, "c-K", prev=[M, K]); r.commit("p2 D=y1 after seeing q1 and y1", "p2")
    r.co("q"); r.g("reset", "-q", "--soft", "main"); r.commit("q squashed", "qS")
    r.co("main")
    d = decide(r, ".ait/D")
    print(f"   same repo: M present? {r.cat(M) is not None}  remaining={[x[:7] for x in d['remaining']]} => {d['kind']}")
    cl = os.path.join(RUNS, "V3-clone"); shutil.rmtree(cl, ignore_errors=True)
    run(RUNS, "git", "clone", "-q", "--no-local", "--no-single-branch", "file://" + r.path, cl)
    c = attach(cl)
    c.g("config", "merge.ait.driver", f"python3 {HERE}/driver.py %O %A %B %P")
    d = decide(c, ".ait/D")
    print(f"   fresh clone: M present? {c.cat(M) is not None}  K present? {c.cat(K) is not None}")
    print(f"   fresh clone: latest={[x[:7] for x in d['latest']]} remaining={[x[:7] for x in d['remaining']]} => {d['kind']}")
    print(f"   (v={v[:7]} body c-K, w={w[:7]} body c-M)")


def V4():
    print("== V4 a blob that is in no tree does not travel (Claude A H1)")
    d = os.path.join(RUNS, "V4"); shutil.rmtree(d, ignore_errors=True); os.makedirs(d)
    src, bare, rcv = (os.path.join(d, x) for x in ("src", "bare.git", "rcv"))
    run(d, "git", "init", "-q", "-b", "main", src)
    v1 = run(src, "git", "hash-object", "-w", "--stdin", input="state current\nprev 0000\n").stdout.strip()
    os.makedirs(os.path.join(src, ".ait"))
    open(os.path.join(src, ".ait/D"), "w").write(f"state future\nprev {v1}\n")
    run(src, "git", "add", "-A"); run(src, "git", "commit", "-qm", "pull D=a1 future amend D")
    run(d, "git", "init", "-q", "--bare", bare)
    run(src, "git", "push", "-q", "file://" + bare, "main")
    run(d, "git", "clone", "-q", "file://" + bare, rcv)
    for p in (src, bare, rcv):
        e = run(p, "git", "cat-file", "-e", v1, check=False).returncode
        print(f"   {os.path.basename(p)}: cat-file -e {v1[:7]} -> exit {e}")


def V5():
    print("== V5 several ends: git's octopus precedent, and one merge state with two MERGE_HEADs")
    r, d0 = base("V5", {"src/x.c": "x 0\n", "src/y.c": "y 0\n"})
    r.co("a", new=True); r.write("src/x.c", "x A\n"); a1 = r.commit("a1 x=A", "a1")
    r.co("main"); r.co("b", new=True); r.write("src/y.c", "y B\n"); b1 = r.commit("b1 y=B", "b1")
    r.co("main"); r.write("src/x.c", "x H\n"); h = r.commit("main x=H", "H")
    for order in (("a", "b"), ("b", "a")):
        res = r.g("merge", "--no-edit", *order, check=False)
        st = r.out("status", "--porcelain")
        mh = r.read(".git/MERGE_HEAD")
        print(f"   git merge {' '.join(order)}: exit {res.returncode}; HEAD moved? {r.out('rev-parse','HEAD') != h}; "
              f"MERGE_HEAD lines={len(mh.split()) if mh else 0}; status={st.splitlines()}")
        print("      " + " | ".join(l for l in (res.stdout + res.stderr).splitlines() if l.strip())[:300])
        if mh:
            r.g("merge", "--abort")
    # one merge state holding both ends, made with plumbing (no intermediate commit)
    print("   -- plumbing: merge b cleanly, then a with a conflict, one state, MERGE_HEAD = b1 a1")
    t1 = r.out("merge-tree", "--write-tree", "HEAD", "b")
    c1 = r.out("commit-tree", t1, "-p", "HEAD", "-p", "b", "-m", "tmp")       # object only, no ref moves
    m2 = r.g("merge-tree", "--write-tree", "--name-only", c1, "a", check=False)
    print(f"      second step merge-tree exit {m2.returncode}: {m2.stdout.split()[1:]}")
    r.g("read-tree", "--reset", "-u", m2.stdout.split()[0])
    ls = r.g("merge-tree", "--write-tree", c1, "a", check=False).stdout.splitlines()[1:]
    stages = [l for l in ls if l and "\t" in l]
    r.g("update-index", "--index-info", input="".join(f"0 {'0'*40}\tsrc/x.c\n" for _ in [0]) + "".join(l + "\n" for l in stages))
    open(os.path.join(r.path, ".git/MERGE_HEAD"), "w").write(f"{b1}\n{a1}\n")
    open(os.path.join(r.path, ".git/MERGE_MSG"), "w").write("pull D Q\n")
    print(f"      status: {r.out('status', '--porcelain').splitlines()}  HEAD still H? {r.out('rev-parse','HEAD') == h}")
    r.g("merge", "--abort")
    print(f"      after merge --abort: HEAD={r.name(r.out('rev-parse','HEAD'))} status={r.out('status','--porcelain').splitlines()} "
          f"MERGE_HEAD? {os.path.exists(os.path.join(r.path, '.git/MERGE_HEAD'))}")


def V6():
    print("== V6 which hooks run on abort and on the finishing commit")
    r, d0 = base("V6", {"src/x.c": "x 0\n"})
    log = os.path.join(r.path, ".git", "hooks.log")
    hd = os.path.join(r.path, ".git", "hooks")
    for h in ("pre-commit", "pre-merge-commit", "commit-msg", "post-commit", "post-merge", "post-checkout", "reference-transaction", "prepare-commit-msg"):
        p = os.path.join(hd, h)
        open(p, "w").write(f"#!/bin/sh\necho {h} >> {log}\ncat >/dev/null 2>&1 </dev/null\nexit 0\n")
        os.chmod(p, 0o755)
    r.co("a", new=True); r.write("src/x.c", "x A\n"); r.g("add", "-A"); r.g("commit", "-qm", "a1")
    r.co("main"); r.write("src/x.c", "x H\n"); r.g("add", "-A"); r.g("commit", "-qm", "H")
    def take():
        s = open(log).read().split() if os.path.exists(log) else []
        open(log, "w").close()
        return [x for x in s if x != "reference-transaction"]
    take()
    print(f"   git merge a: exit {r.g('merge', '--no-edit', 'a', check=False).returncode} hooks={take()}")
    r.g("merge", "--abort"); print(f"   git merge --abort: hooks={take()}")
    r.g("merge", "--no-edit", "a", check=False); take()
    r.write("src/x.c", "x R\n"); r.g("add", "-A"); r.g("commit", "--no-edit", "-q")
    print(f"   finishing git commit: hooks={take()}")


def V9():
    print("== V9 a user merge driver runs during merge-tree and can write to the work tree (Codex A F1)")
    r, d0 = base("V9", {"src/x.c": "x 0\n"})
    with open(os.path.join(r.path, ".git", "info", "attributes"), "a") as f:
        f.write("src/x.c merge=user\n")
    flag = os.path.join(r.path, "driver-was-here")
    r.g("config", "merge.user.driver", f"touch {flag}; exit 1")
    r.co("a", new=True); r.write("src/x.c", "x A\n"); r.commit("a1", "a1")
    r.co("main"); r.write("src/x.c", "x H\n"); r.commit("H", "H")
    m = r.g("merge-tree", "--write-tree", "HEAD", "a", check=False)
    print(f"   merge-tree exit {m.returncode}; driver wrote into the work tree? {os.path.exists(flag)}")


def V8():
    print("== V8 git keeps the other side's change to a file my contract pinned later (Claude B B5)")
    r, d0 = base("V8", {"tests/d.sh": "t0\n"})
    r.co("a", new=True); r.write("tests/d.sh", "tA\n"); r.commit("a0 code", "a0")
    r.co("main"); r.write("src/order.c", "order 1\n"); r.commit("main e1", "e1")
    m = r.g("merge-tree", "--write-tree", "HEAD", "a", check=False)
    t = m.stdout.split()[0]
    print(f"   merge-tree exit {m.returncode}; tests/d.sh in the merged tree: {r.out('cat-file', '-p', t + ':tests/d.sh')}")


if __name__ == "__main__":
    for n in (sys.argv[1:] or ["V1", "V2", "V3", "V4", "V5", "V6", "V8", "V9"]):
        globals()[n]()
