"""Follow the hints of the criteria on real repositories and check the resulting state with v14's validate (5.9 b-e)
and the 'checked code unchanged' condition of 1.5a. The expected outcomes are the criteria's; this only checks them."""
import os, glob
from lib import *
import scen
from scen import H


def records(path):
    """records of a work tree: {ID: fields}, links: {(D, O): fields}"""
    recs, links = {}, {}
    for p in glob.glob(os.path.join(path, ".ait", "*")):
        if os.path.isfile(p):
            recs[os.path.basename(p)] = fields(open(p).read())
    for p in glob.glob(os.path.join(path, ".ait", "undertake", "*")):
        d, o = os.path.basename(p).split("-")
        links[(d, o)] = fields(open(p).read())
    return recs, links


def wblob(path, rel):
    p = os.path.join(path, rel)
    if not os.path.exists(p):
        return None
    return run(path, "git", "hash-object", rel).stdout.strip()


def validate(path):
    recs, links = records(path)
    errs = []
    st = lambda i: recs[i]["state"][0]
    for i, f in recs.items():
        role = f["role"][0]
        if role == "drop" and (f["target"][0] not in recs or recs[f["target"][0]]["role"][0] == "drop"):
            errs.append(f"b: {i} target")
        if role == "drop" and st(i) == "fulfilled" and st(f["target"][0]) != "withdrawn":
            errs.append(f"d: fulfilled {i} target not withdrawn")
        if role in ("demand", "offer") and st(i) == "withdrawn":
            if not any(g["role"][0] == "drop" and g["target"][0] == i and g["state"][0] == "fulfilled" for g in recs.values()):
                errs.append(f"d: withdrawn {i} without fulfilled drop")
        if role in ("demand", "offer") and st(i) in ("future", "current"):
            for fl in f.get("file", []):
                pth, mode, blob = fl.split()[:3]
                if wblob(path, pth) != blob:
                    errs.append(f"e: {i} pins {pth}")
    for (d, o), f in links.items():
        if d not in recs or o not in recs:
            errs.append(f"c: link {d}-{o}")
            continue
        if f["state"][0] == "undertaken" and st(o) == "current" and st(d) == "future":
            errs.append(f"d: current {o} undertakes future {d}")
    return errs


def unverified(path):
    """current D/O whose provider file differs from its pin (1.5a, the code side)"""
    recs, _ = records(path)
    out = []
    for i, f in recs.items():
        if f["role"][0] in ("demand", "offer") and f["state"][0] == "current":
            for pv in f.get("provider", []):
                pth, blob = pv.split()[:2]
                if wblob(path, pth) != blob:
                    out.append(f"{i}:{pth}")
    return out


def take(repo, heads, label):
    """merge heads into a throwaway work tree; contract-file conflicts are left for the caller to decide."""
    wt = repo.path + "-flow"
    shutil.rmtree(wt, ignore_errors=True)
    repo.g("worktree", "add", "-q", "--detach", wt, "HEAD")
    r = run(wt, "git", "merge", "--no-commit", "--no-ff", "--no-verify", *heads, check=False)
    print(f"  [{label}] merge {heads}: exit={r.returncode}")
    return wt


def set_record(wt, rel, text):
    with open(os.path.join(wt, rel), "w") as f:
        f.write(text)


def reopen(wt, id):
    """future amend K on a current K: state future, prev = the version in the work tree"""
    p = os.path.join(wt, ".ait", id)
    old = open(p).read()
    prev = run(wt, "git", "hash-object", "-w", f".ait/{id}").stdout.strip()
    new = "".join(l + "\n" for l in old.splitlines() if not l.startswith("prev ") and not l.startswith("check ")).replace("state current", "state future") + f"prev {prev}\n"
    set_record(wt, f".ait/{id}", new)


def repin_file(wt, id, spec):
    p = os.path.join(wt, ".ait", id)
    old = open(p).read()
    prev = run(wt, "git", "hash-object", "-w", f".ait/{id}").stdout.strip()
    b = run(wt, "git", "hash-object", "-w", spec).stdout.strip()
    L = []
    for l in old.splitlines():
        if l.startswith("prev "):
            continue
        if l.startswith(f"file {spec} "):
            parts = l.split(); parts[3] = b; l = " ".join(parts)
        L.append(l)
    set_record(wt, f".ait/{id}", "\n".join(L) + f"\nprev {prev}\n")


def repin_on_top(wt, id, spec):
    """the finishing commit's re-pin of a future contract (7.1.3-2) on top of the version in the work tree:
    prev = that version and its prev (record 108)"""
    p = os.path.join(wt, ".ait", id)
    old = open(p).read()
    cur = run(wt, "git", "hash-object", "-w", f".ait/{id}").stdout.strip()
    b = run(wt, "git", "hash-object", "-w", spec).stdout.strip()
    L, ps = [], [cur]
    for l in old.splitlines():
        if l.startswith("prev "):
            ps.append(l[5:])
            continue
        if l.startswith(f"file {spec} "):
            parts = l.split(); parts[3] = b; l = " ".join(parts)
        L.append(l)
    set_record(wt, f".ait/{id}", "\n".join(L) + "\n" + "".join(f"prev {p}\n" for p in sorted(set(ps)) if p != "-"))


def report(wt, label):
    print(f"     {label}: validate={validate(wt) or 'pass'}  unverified={unverified(wt) or 'none'}")


def done(repo, wt):
    run(wt, "git", "merge", "--abort", check=False)
    repo.g("worktree", "remove", "--force", wt)


def K6():
    H("flow K6: pull D O -> 5.9 d ; hint adds 'future amend O'")
    r = scen.Repo.__new__(scen.Repo); r.path = os.path.join(RUNS, "K6"); r.short = {}
    a2 = r.out("rev-parse", "a")
    wt = take(r, [a2], "pull D O")
    report(wt, "as merged")
    reopen(wt, "O"); report(wt, "with 'future amend O'")
    done(r, wt)


def N5():
    H("flow N5: pull D -> 5.9 d ; hint adds 'future amend OE'")
    r = scen.Repo.__new__(scen.Repo); r.path = os.path.join(RUNS, "N5"); r.short = {}
    wt = take(r, [r.out("rev-parse", "a")], "pull D")
    report(wt, "as merged")
    reopen(wt, "OE"); report(wt, "with 'future amend OE'")
    done(r, wt)


def U7():
    H("flow U7: pull D=a2 X")
    r = scen.Repo.__new__(scen.Repo); r.path = os.path.join(RUNS, "U7"); r.short = {}
    a2 = r.out("rev-parse", "a")
    mine = r.blob_at("HEAD", ".ait/D"); theirs = r.blob_at(a2, ".ait/D")
    wt = take(r, [a2], "pull D=a2 X")
    set_record(wt, ".ait/D", body(r.cat(theirs)) + "".join(f"prev {p}\n" for p in sorted([mine, theirs])))
    report(wt, "D = a2's body, prev both")
    done(r, wt)


def SF1():
    H("flow SF1: the mixed choice D=a1 Q=b1 (decision 29: a code conflict on shared.yaml), and D=a1 Q=a1")
    r = scen.Repo.__new__(scen.Repo); r.path = os.path.join(RUNS, "SF1"); r.short = {}
    a1 = r.out("rev-parse", "a"); b1 = r.out("rev-parse", "b")
    da, db = r.blob_at(a1, ".ait/D"), r.blob_at(b1, ".ait/D")
    qa, qb = r.blob_at(a1, ".ait/Q"), r.blob_at(b1, ".ait/Q")
    wt = take(r, [a1, b1], "D=a1 Q=b1 (both ends ; contract files decided by pull)")
    print(f"     unmerged: {run(wt, 'git', 'diff', '--name-only', '--diff-filter=U').stdout.split()}")
    set_record(wt, ".ait/D", body(r.cat(da)) + "".join(f"prev {p}\n" for p in sorted([da, db])))
    set_record(wt, ".ait/Q", body(r.cat(qb)) + "".join(f"prev {p}\n" for p in sorted([qa, qb])))
    set_record(wt, "specs/shared.yaml", "body: s-M (resolved by hand)\n")
    repin_on_top(wt, "D", "specs/shared.yaml"); repin_on_top(wt, "Q", "specs/shared.yaml")
    report(wt, "D=a1 Q=b1, resolved, D and Q re-pinned on top (record 108)")
    done(r, wt)
    wt = take(r, [a1], "D=a1 Q=a1")
    set_record(wt, ".ait/Q", body(r.cat(qa)) + "".join(f"prev {p}\n" for p in sorted([qa, qb])))
    report(wt, "D=a1 Q=a1")
    done(r, wt)


def S1():
    H("flow S1/S2: K5 shares the spec ; future K5 is re-pinned (6.2)")
    r = scen.Repo.__new__(scen.Repo); r.path = os.path.join(RUNS, "S1"); r.short = {}
    wt = take(r, [r.out("rev-parse", "a")], "pull D")
    report(wt, "as merged")
    repin_file(wt, "K5", "specs/shared.yaml"); report(wt, "K5 re-pinned")
    done(r, wt)


def C2():
    H("flow C2: D current, my line9 merged in")
    r = scen.Repo.__new__(scen.Repo); r.path = os.path.join(RUNS, "C2"); r.short = {}
    wt = take(r, [r.out("rev-parse", "a")], "pull D")
    report(wt, "as merged")
    reopen(wt, "D"); report(wt, "with 'future amend D'")
    done(r, wt)


def S3():
    H("flow S3: my current K6 provides stock.c")
    r = scen.Repo.__new__(scen.Repo); r.path = os.path.join(RUNS, "S3"); r.short = {}
    wt = take(r, [r.out("rev-parse", "a")], "pull D")
    report(wt, "as merged")
    reopen(wt, "K6"); report(wt, "with 'future amend K6'")
    done(r, wt)


if __name__ == "__main__":
    for f in (K6, N5, U7, SF1, S1, C2, S3):
        f()
