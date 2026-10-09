"""Builds real git repositories for the pull criteria and prints the data the criteria judge on.
Nothing here decides an ait outcome: it prints git facts (tips, blobs, introducing commits, merge results)
and the data definitions of the criteria (containment through prev, latest versions, bodies, rule-1 steps)."""
import os, shutil, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
RUNS = os.path.join(HERE, "runs")
ENV = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@t", GIT_COMMITTER_NAME="t",
           GIT_COMMITTER_EMAIL="t@t", GIT_CONFIG_NOSYSTEM="1", HOME=HERE)
_tick = [1700000000]


def run(cwd, *a, input=None, check=True, env=None):
    e = dict(ENV if env is None else env)
    _tick[0] += 60
    e["GIT_AUTHOR_DATE"] = e["GIT_COMMITTER_DATE"] = f"{_tick[0]} +0000"
    r = subprocess.run(list(a), cwd=cwd, input=input, capture_output=True, text=True, env=e)
    if check and r.returncode != 0:
        raise RuntimeError(f"{a} failed ({r.returncode}):\n{r.stdout}\n{r.stderr}")
    return r


class Repo:
    def __init__(self, name):
        self.path = os.path.join(RUNS, name)
        shutil.rmtree(self.path, ignore_errors=True)
        os.makedirs(self.path)
        self.g("init", "-q", "-b", "main")
        self.g("config", "merge.ait.driver", f"python3 {HERE}/driver.py %O %A %B %P")
        os.makedirs(os.path.join(self.path, ".git", "info"), exist_ok=True)
        with open(os.path.join(self.path, ".git", "info", "attributes"), "w") as f:
            f.write(".ait/** merge=ait\n")
        self.short = {}

    # ---- git
    def g(self, *a, check=True, input=None):
        return run(self.path, "git", *a, check=check, input=input)

    def out(self, *a):
        return self.g(*a).stdout.strip()

    def co(self, ref, new=False):
        self.g("checkout", "-q", *(["-b"] if new else []), ref)

    def write(self, rel, text):
        p = os.path.join(self.path, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "w") as f:
            f.write(text)

    def read(self, rel):
        p = os.path.join(self.path, rel)
        return open(p).read() if os.path.exists(p) else None

    def wblob(self, rel):
        """blob OID of the work-tree file, or None"""
        if self.read(rel) is None:
            return None
        return self.out("hash-object", "-w", rel)

    def commit(self, msg, name=None):
        self.g("add", "-A")
        self.g("commit", "-q", "--no-verify", "--allow-empty", "-m", msg)
        c = self.out("rev-parse", "HEAD")
        if name:
            self.short[c] = name
        return c

    def name(self, c):
        return self.short.get(c, c[:7])

    # ---- records (canonical order of spec 2.2.2; IDs are short names here)
    def record(self, id, role="demand", state="future", spec=None, provider="src/stock.c",
               consumer="src/order.c", creator="agent-a S1", target=None, prev=None, extra_provider=()):
        rel = f".ait/{id}" if "-" not in id else f".ait/undertake/{id}"
        if prev is None:
            b = self.wblob(rel)
            prev = [b] if b else ["-"]
        L = []
        if role == "undertake":
            L += [f"state {state}"]
        else:
            L += [f"role {role}", f"state {state}"]
            if role == "demand":
                L.append(f"consumer {consumer} {self.wblob(consumer)}")
            if role in ("demand", "offer"):
                for p in (provider,) + tuple(extra_provider):
                    L.append(f"provider {p} {self.wblob(p)}")
                spec = spec or f"specs/{id.lower()}.yaml"
                L.append(f"file {spec} 100644 {self.wblob(spec)} def")
            if role == "drop":
                L.append(f"target {target}")
            L.append(f"creator {creator}")
        L += [f"prev {p}" for p in sorted(prev)]
        self.write(rel, "\n".join(L) + "\n")
        return self.wblob(rel)

    def blob_at(self, rev, rel):
        r = self.g("rev-parse", "-q", "--verify", f"{rev}:{rel}", check=False)
        return r.stdout.strip() or None

    def cat(self, oid):
        r = self.g("cat-file", "-p", oid, check=False)
        return r.stdout if r.returncode == 0 else None


# ---------------------------------------------------------------- data definitions of the criteria
def prevs(text):
    return [l[5:] for l in text.splitlines() if l.startswith("prev ") and l[5:] != "-"]


def body(text):
    return "".join(l + "\n" for l in text.splitlines() if not l.startswith("prev "))


def fields(text):
    d = {}
    for l in text.splitlines():
        k, _, v = l.partition(" ")
        d.setdefault(k, []).append(v)
    return d


def contains(repo, a, b):
    """a contains b: b is a, or b is reached from a through prev. A missing blob ends that branch."""
    if a == b:
        return True
    seen, todo = set(), [a]
    while todo:
        o = todo.pop()
        if o in seen:
            continue
        seen.add(o)
        t = repo.cat(o)
        if t is None:
            continue
        for p in prevs(t):
            if p == b:
                return True
            todo.append(p)
    return False


ALLOWED = {  # spec 2.3, data only (the check-record side of current is 1.5a's business, not rule 1's)
    ("demand", None): {"future", "current"}, ("offer", None): {"future", "current"},
    ("drop", None): {"future"}, ("undertake", None): {"undertaken"},
    ("demand", "future"): {"future", "cancelled", "current"}, ("offer", "future"): {"future", "cancelled", "current"},
    ("demand", "current"): {"current", "future", "withdrawn"}, ("offer", "current"): {"current", "future", "withdrawn"},
    ("drop", "future"): {"future", "cancelled", "fulfilled"},
    ("undertake", "undertaken"): {"released"}, ("undertake", "released"): {"undertaken"},
}


def role_state(text):
    f = fields(text)
    return (f.get("role", ["undertake"])[0], f["state"][0], f.get("creator", [None])[0])


def rule1(repo, oid):
    """rule 1: the version can be reached from one of its prev versions by ait's transitions.
    returns True (ok), False (accident) or None (a prev blob is missing: cannot be judged)."""
    t = repo.cat(oid)
    role, st, cr = role_state(t)
    ps = prevs(t)
    if not ps:
        return st in ALLOWED.get((role, None), set())
    results = []
    for p in ps:
        pt = repo.cat(p)
        if pt is None:
            results.append(None)
            continue
        prole, pst, pcr = role_state(pt)
        same_body = body(pt) == body(t)
        ok = prole == role and pcr == cr and (same_body or st in ALLOWED.get((role, pst), set()))
        results.append(ok)
    if any(r is True for r in results):
        return True
    return None if any(r is None for r in results) else False


def refs(repo):
    out = repo.out("for-each-ref", "--format=%(refname) %(symref)", "refs/heads", "refs/remotes")
    return [l.split()[0] for l in out.splitlines() if len(l.split()) == 1]


def candidates(repo, rel):
    """tips of the ref list (5.11) and HEAD. -> {blob: [ref names]}"""
    c = {}
    for r in refs(repo):
        b = repo.blob_at(r, rel)
        if b:
            c.setdefault(b, []).append(r.replace("refs/heads/", "").replace("refs/remotes/", ""))
    h = repo.blob_at("HEAD", rel)
    if h:
        c.setdefault(h, []).append("HEAD")
    return c


def latest(repo, rel, skip_accidents=True):
    c = candidates(repo, rel)
    acc = {b for b in c if skip_accidents and rule1(repo, b) is False}
    live = [b for b in c if b not in acc]
    lat = [b for b in live if not any(o != b and contains(repo, o, b) for o in live)]
    return c, acc, lat


def introducing(repo, rel, blob):
    """commits reachable from the ref list whose tree has blob at rel and none of whose parents has it."""
    rs = refs(repo)
    res = []
    for c in repo.out("rev-list", *rs).split():
        if repo.blob_at(c, rel) != blob:
            continue
        ps = repo.out("rev-list", "--parents", "-n1", c).split()[1:]
        if all(repo.blob_at(p, rel) != blob for p in ps):
            n = int(repo.out("rev-list", "--count", c, "^HEAD"))
            res.append((c, n))
    return res


def merge_tree(repo, *heads):
    """git's merge of HEAD with heads, without touching anything (git >= 2.40, honours the merge driver)."""
    if len(heads) == 1:
        r = repo.g("merge-tree", "--write-tree", "--name-only", "HEAD", heads[0], check=False)
        lines = r.stdout.strip().splitlines()
        tree = lines[0] if lines else None
        conflicts = []
        if r.returncode == 1:
            for l in lines[1:]:
                if not l:
                    break
                conflicts.append(l)
        return tree, r.returncode, conflicts
    raise ValueError("one head at a time")


def changed_paths(repo, tree_a, tree_b):
    out = repo.out("diff-tree", "-r", "--name-only", tree_a, tree_b)
    return out.splitlines()


def summary(repo, text):
    f = fields(text)
    out = [f.get("role", ["link"])[0] + "/" + f["state"][0]]
    for fl in f.get("file", []):
        c = repo.cat(fl.split()[2]) or "?"
        out.append("cond=" + c.strip().replace("body: ", ""))
    for pv in f.get("provider", []):
        out.append(pv.split()[0].split("/")[-1] + "@" + pv.split()[1][:5])
    if "target" in f:
        out.append("target=" + f["target"][0])
    return " ".join(out)


def show(repo, rel, label=None):
    c, acc, lat = latest(repo, rel)
    print(f"  [{label or rel}] candidates:")
    for b, rs in c.items():
        flag = " ACCIDENT(rule1)" if b in acc else ""
        r1 = rule1(repo, b)
        print(f"     {b[:7]} at {','.join(rs)}  rule1={r1}{flag}  {summary(repo, repo.cat(b))}  prev={[p[:7] for p in prevs(repo.cat(b))]}")
    bodies = {}
    for b in lat:
        bodies.setdefault(body(repo.cat(b)), []).append(b)
    print(f"     latest: {[b[:7] for b in lat]}  distinct bodies: {len(bodies)}")
    head = repo.blob_at("HEAD", rel)
    for b in lat:
        if head and contains(repo, head, b):
            print(f"     {b[:7]} is contained in HEAD's version")
            continue
        ic = introducing(repo, rel, b)
        print(f"     introducing commits of {b[:7]}: {[(repo.name(c), n) for c, n in ic]}  (name, new commits)")
    return lat


def mt(repo, head, label=None):
    tree, rc, conf = merge_tree(repo, head)
    htree = repo.out("rev-parse", "HEAD^{tree}")
    ch = changed_paths(repo, htree, tree) if tree else []
    print(f"  merge-tree HEAD {label or repo.name(head)}: exit={rc} conflicts={conf}")
    print(f"     paths that differ from HEAD: {ch}")
    return tree, rc, conf, ch


def gitmerge(repo, head, label=None):
    """plain git merge (with the v14 driver) in a throwaway worktree."""
    wt = repo.path + "-wt"
    shutil.rmtree(wt, ignore_errors=True)
    repo.g("worktree", "add", "-q", "--detach", wt, "HEAD")
    r = run(wt, "git", "merge", "--no-edit", "--no-verify", head, check=False)
    u = run(wt, "git", "diff", "--name-only", "--diff-filter=U", check=False).stdout.split()
    print(f"  git merge {label or repo.name(head)}: exit={r.returncode} unmerged={u}")
    run(wt, "git", "merge", "--abort", check=False)
    repo.g("worktree", "remove", "--force", wt)
    return r.returncode, u
