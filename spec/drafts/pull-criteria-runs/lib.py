"""Builds real git repositories for the pull criteria and prints the data the criteria judge on.
Nothing here decides an ait outcome: it prints git facts (tips, blobs, introducing commits, merge results)
and the data definitions of the criteria (containment through prev, latest versions, bodies, rule-1 steps)."""
import os, re, shutil, subprocess

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
               consumer="src/order.c", creator="agent-a S1", target=None, prev=None, extra_provider=(), check="reported", inputs=()):
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
                rows = [(spec, f"file {spec} 100644 {self.wblob(spec)} def")]
                rows += [(i, f"file {i} 100644 {self.wblob(i)} input") for i in inputs]
                L += [row for _, row in sorted(rows)]
                if state == "current" and check:
                    L.append(f"check {check} {pins_of(L)}")
            if role == "drop":
                L.append(f"target {target}")
            L.append(f"creator {creator}")
        L += [f"prev {p}" for p in sorted(prev)]
        self.write(rel, "\n".join(L) + "\n")
        return self.wblob(rel)

    def blob_at(self, rev, rel):
        r = self.g("rev-parse", "-q", "--verify", f"{rev}:{rel}", check=False)
        return r.stdout.strip() or None

    def catb(self, oid):
        """raw bytes of an object (text mode would turn CRLF into LF)"""
        r = subprocess.run(["git", "cat-file", "-p", oid], cwd=self.path, capture_output=True, env=ENV)
        return r.stdout if r.returncode == 0 else None

    def cat(self, oid):
        r = self.g("cat-file", "-p", oid, check=False)
        return r.stdout if r.returncode == 0 else None


# ---------------------------------------------------------------- data definitions of the criteria
import hashlib


def pins_of(lines):
    """PINS (v14 5.4): the provider rows and the file rows in the order they are written, each ended by LF,
    fed to `git hash-object --stdin` of the repository. These scripts make SHA-1 repositories only, so this computes it
    directly (a SHA-256 repository would give its own object ID)."""
    rows = [l for l in lines if l.startswith("provider ") or l.startswith("file ")]
    data = "".join(r + "\n" for r in rows).encode()
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def check_ok(text):
    """a current D/O must carry exactly one check line whose PINS match its pins (decision 17)"""
    L = text.splitlines()
    f = fields(text)
    if f.get("role", [""])[0] not in ("demand", "offer") or f["state"][0] != "current":
        return True
    ck = f.get("check", [])
    return len(ck) == 1 and ck[0].split()[0] in ("reported", "forced") and ck[0].split()[1] == pins_of(L)


KEYS = {"role", "state", "consumer", "provider", "file", "input", "check", "target", "creator", "prev"}


ORDER = {  # canonical key order (v14 2.2.2), with the check line of decision 17 after the file rows
    "demand": ["role", "state", "consumer+", "provider+", "file+", "check?", "creator", "prev+"],
    "offer": ["role", "state", "provider+", "file+", "check?", "creator", "prev+"],
    "drop": ["role", "state", "target", "creator", "prev+"],
    "undertake": ["state", "prev+"],
}
STATES = {"demand": {"future", "current", "cancelled", "withdrawn"}, "offer": {"future", "current", "cancelled", "withdrawn"},
          "drop": {"future", "fulfilled", "cancelled"}, "undertake": {"undertaken", "released"}}


def path_ok(x):
    """v14 3.1"""
    if not x or x[0] in "/-" or any(ch in " :," or ord(ch) < 32 or ord(ch) == 127 for ch in x):
        return False
    if any(seg in ("", ".", "..") for seg in x.split("/")):
        return False
    return x != ".ait" and not x.startswith(".ait/")


def scope_key(v, hexoid):
    """one SCOPE row (v14 2.2.2, 3.2, 3.2a) -> its order key (PATH bytes, whole file first, S, E), or None if malformed"""
    t = v.split(" ")
    if len(t) == 1 and t[0].startswith("new:"):
        return (t[0][4:].encode(), 0, 0, 0) if path_ok(t[0][4:]) else None
    if len(t) != 2 or not hexoid(t[1]):
        return None
    sc = t[0]
    if sc == "/":
        return (b"/", 0, 0, 0)
    if sc.endswith("/"):
        return (sc.encode(), 0, 0, 0) if path_ok(sc[:-1]) else None
    path, colon, rng = sc.partition(":")
    if not path_ok(path):
        return None
    if not colon:
        return (path.encode(), 0, 0, 0)
    m = re.fullmatch(r"([1-9][0-9]*)-([1-9][0-9]*)", rng)
    if not m or int(m.group(1)) > int(m.group(2)):
        return None
    return (path.encode(), 1, int(m.group(1)), int(m.group(2)))


def well_formed_raw(raw):
    """the record format (v14 2.2.1, 2.2.2 and decision 17's check line), judged on the raw bytes"""
    if raw is None:
        return False
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        return False
    if not text.endswith("\n") or any((ord(ch) < 32 and ch != "\n") or ord(ch) == 127 for ch in text):
        return False
    lines = text[:-1].split("\n")
    rows = []
    for l in lines:
        k, sp, v = l.partition(" ")
        if not sp or not k or not v or l != l.strip() or "  " in l:
            return False
        rows.append((k, v))
    role = rows[0][1] if rows[0][0] == "role" else ("undertake" if rows[0][0] == "state" else None)
    if role not in ORDER:
        return False
    i = 0
    for slot in ORDER[role]:
        key, many, opt = slot.rstrip("+?"), slot.endswith("+"), slot.endswith("?")
        n = 0
        while i < len(rows) and rows[i][0] == key and (many or n == 0):
            i += 1; n += 1
        if n == 0 and not opt:
            return False
    if i != len(rows):
        return False
    f = {}
    for k, v in rows:
        f.setdefault(k, []).append(v)
    if f["state"][0] not in STATES[role]:
        return False
    if "check" in f:
        ck = f["check"][0].split(" ")
        if role not in ("demand", "offer") or f["state"][0] != "current" or len(ck) != 2 or ck[0] not in ("reported", "forced"):
            return False
    hexoid = lambda x: len(x) == 40 and all(ch in "0123456789abcdef" for ch in x)
    if "check" in f and not hexoid(f["check"][0].split(" ")[1]):
        return False
    for v in f.get("file", []):
        t = v.split(" ")
        if len(t) != 4 or not path_ok(t[0]) or t[1] not in ("100644", "100755") or not hexoid(t[2]) or t[3] not in ("def", "input"):
            return False
    for k in ("consumer", "provider"):
        keys = []
        for v in f.get(k, []):
            sk = scope_key(v, hexoid)
            if sk is None:
                return False
            keys.append(sk)
        if keys != sorted(keys) or len(set(keys)) != len(keys):
            return False
    paths = [v.split(" ")[0] for v in f.get("file", [])]
    if paths != sorted(paths, key=lambda x: x.encode()) or len(set(paths)) != len(paths):
        return False
    if "creator" in f and len(f["creator"][0].split(" ")) != 2:
        return False
    ps = f["prev"]
    if (ps != ["-"] and "-" in ps) or ps != sorted(ps) or len(set(ps)) != len(ps):
        return False
    if ps != ["-"] and not all(hexoid(x) for x in ps):
        return False
    return True


def well_formed(text):
    return text is not None and well_formed_raw(text.encode())


def prevs(text):
    return [l[5:] for l in text.splitlines() if l.startswith("prev ") and l[5:] != "-"]


def body(text):
    """the version minus its prev and check lines (decision 17: the check is judged separately)"""
    return "".join(l + "\n" for l in text.splitlines() if not l.startswith("prev ") and not l.startswith("check "))


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


def rule1(repo, oid, _memo=None):
    """rule 1: the version can be reached by ait's transitions from one of its prev versions that is itself not an
    accident (decision 8: a branch with an accident stays blocked until it is fixed; r33).
    returns True (ok), False (accident) or None (a prev blob is missing: cannot be judged)."""
    memo = {} if _memo is None else _memo
    if oid in memo:
        return memo[oid]
    memo[oid] = None
    t = repo.cat(oid)
    if t is None:
        return None
    if not well_formed_raw(repo.catb(oid)) or not check_ok(t):
        memo[oid] = False
        return False
    role, st, cr = role_state(t)
    ps = prevs(t)
    if not ps:
        memo[oid] = st in ALLOWED.get((role, None), set())
        return memo[oid]
    terms = [a for a in ancestors(repo, oid) - {oid}
             if repo.cat(a) and well_formed_raw(repo.catb(a)) and role_state(repo.cat(a))[1] in TERMINAL]
    if terms and all(body(repo.cat(a)) != body(t) for a in terms):
        memo[oid] = False
        return False  # decision 21, made transitive (r36): a version that contains an ended version must be one of them
    results = []
    for p in ps:
        pt = repo.cat(p)
        if pt is None:
            results.append(None)
            continue
        if rule1(repo, p, memo) is False:
            results.append(False)  # a prev that is an accident is no way in
            continue
        prole, pst, pcr = role_state(pt)
        same_body = body(pt) == body(t)
        ok = prole == role and pcr == cr and (same_body or st in ALLOWED.get((role, pst), set()))
        results.append(ok)
    if any(r is True for r in results):
        memo[oid] = True
    else:
        memo[oid] = None if any(r is None for r in results) else False
    return memo[oid]


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
        if fl.split()[3] != "def":
            continue
        c = repo.cat(fl.split()[2]) or "?"
        out.append("cond=" + c.strip().splitlines()[0].replace("body: ", ""))
    for pv in f.get("provider", []):
        out.append(pv.split()[0].split("/")[-1] + "@" + pv.split()[1][:5])
    if "target" in f:
        out.append("target=" + f["target"][0])
    if "check" in f:
        out.append("check=" + f["check"][0].split()[0] + ("" if check_ok(text) else "(PINS MISMATCH)"))
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


# ---------------------------------------------------------------- revision 4: terminal wins (decision 13), 3-way (decision 15)
TERMINAL = {"cancelled", "withdrawn", "fulfilled"}


def ancestors(repo, v):
    out, todo = set(), [v]
    while todo:
        o = todo.pop()
        if o in out:
            continue
        out.add(o)
        t = repo.cat(o)
        if t:
            todo += prevs(t)
    return out


def unchanged_since_common(repo, v, w):
    """w moved forward from v (decision 15): the newest common versions all have one body B,
    v's body is B and w's is not. If a newest common version cannot be read, it is not known (r30 50)."""
    common = ancestors(repo, v) & ancestors(repo, w)
    maxc = [c for c in common if not any(o != c and contains(repo, o, c) for o in common)]
    if not maxc or any(repo.cat(c) is None for c in maxc):
        return False
    bodies = {body(repo.cat(c)) for c in maxc}
    if len(bodies) != 1:
        return False
    B = next(iter(bodies))
    return body(repo.cat(v)) == B and body(repo.cat(w)) != B


def on_cycle(edges, v):
    seen, todo = set(), list(edges.get(v, ()))
    while todo:
        x = todo.pop()
        if x == v:
            return True
        if x in seen:
            continue
        seen.add(x)
        todo += edges.get(x, ())
    return False


def new_check_line(repo, body_text, pool):
    """decision 22: the check line of a new current version: 'reported' if a version in the pool (this pull's
    latest versions and mine, r30 65) with the same PINS is reported, else 'forced'."""
    f = fields(body_text)
    if f.get("role", [""])[0] not in ("demand", "offer") or f["state"][0] != "current":
        return None
    P = pins_of(body_text.splitlines())
    kinds = set()
    for v in pool:
        t = repo.cat(v)
        for c in fields(t).get("check", []) if t else []:
            if c.split()[1] == P:
                kinds.add(c.split()[0])
    return f"check {'reported' if 'reported' in kinds else 'forced'} {P}"


def compose(body_text, check, prevs_):
    """a version: body, check line (after the file rows, before creator), prev rows"""
    out = []
    for l in body_text.splitlines():
        if l.startswith("creator") and check:
            out.append(check)
        out.append(l)
    return "\n".join(out) + "\n" + "".join(f"prev {p}\n" for p in sorted(set(prevs_)))


def decide(repo, rel, extra=()):
    """the data steps of step 1 for one contract file (revision 5). extra: blobs held by the commits being taken (E1).
    order: skip accidents and malformed versions -> latest -> 3-way at once, versions on a cycle stay (decisions 15, 24)
    -> terminal wins (decision 13) -> the result must contain every latest version (decisions 4, 13, 15)."""
    c = candidates(repo, rel)
    for b in extra:
        if b:
            c.setdefault(b, []).append("taken commit")
    bad = {b for b in c if not well_formed_raw(repo.catb(b))}
    acc = {b for b in c if b not in bad and rule1(repo, b) is False}
    live = [b for b in c if b not in acc and b not in bad]
    lat = [b for b in live if not any(o != b and contains(repo, o, b) for o in live)]
    edges = {v: [w for w in lat if w != v and unchanged_since_common(repo, v, w)] for v in lat}
    cyc = [v for v in lat if on_cycle(edges, v)]
    after3 = [v for v in lat if not edges[v] or v in cyc]
    st = {b: fields(repo.cat(b))["state"][0] for b in after3}
    term = [b for b in after3 if st[b] in TERMINAL]
    rem = term if term and len(term) < len(after3) else list(after3)
    head = repo.blob_at("HEAD", rel)
    pool = list(lat) + ([head] if head and head not in lat else [])
    bodies = {body(repo.cat(v)) for v in rem}
    whole = [v for v in rem + ([head] if head else []) if all(contains(repo, v, x) for x in lat)]
    if not rem:
        kind = "no candidate left (all were skipped)" if (bad or acc) else "no candidate"
    elif head and all(contains(repo, head, x) for x in lat):
        kind = "nothing to take"
    elif len(bodies) == 1:
        B = next(iter(bodies))
        ck = new_check_line(repo, B, pool)
        keep = [v for v in whole if body(repo.cat(v)) == B and (ck is None or ck in repo.cat(v).splitlines())]
        if keep:
            kind = "take " + keep[0][:7] + " as is"
        else:
            kind = f"new version: body of {rem[0][:7]}, prev = latest {[x[:7] for x in lat]}"
    else:
        kind = f"DIVERGED: choose among {[v[:7] for v in rem]}"
    return dict(cands=c, acc=acc, bad=bad, latest=lat, edges=edges, cycle=cyc, after3=after3,
                terminal_won=(rem != after3), remaining=rem, kind=kind, pool=pool)


def dshow(repo, rel, label=None, extra=()):
    d = decide(repo, rel, extra)
    print(f"  [{label or rel}]")
    for b, rs in d["cands"].items():
        t = repo.cat(b)
        print(f"     {b[:7]} at {','.join(rs)}  rule1={rule1(repo, b) if b not in d['bad'] else 'MALFORMED'}  {summary(repo, t)}  prev={[p[:7] for p in prevs(t)]}")
    if d["bad"]:
        print(f"     skipped (malformed): {[b[:7] for b in d['bad']]}")
    if d["acc"]:
        print(f"     skipped (accident): {[b[:7] for b in d['acc']]}")
    fw = [f"{w[:7]} moved forward from {v[:7]}" for v in d["latest"] for w in d["edges"][v]]
    print(f"     latest: {[b[:7] for b in d['latest']]}" + (f"  3-way: {fw}" if fw else "") + (f"  on a cycle: {[b[:7] for b in d['cycle']]}" if d["cycle"] else ""))
    print(f"     after 3-way: {[b[:7] for b in d['after3']]}" + ("  -> terminal wins" if d["terminal_won"] else "") + f"  remaining: {[b[:7] for b in d['remaining']]}")
    print(f"     => {d['kind']}")
    for v in d["remaining"]:
        h = repo.blob_at("HEAD", rel)
        if h and contains(repo, h, v):
            continue
        print(f"     introducing commits of {v[:7]}: {[(repo.name(c), n) for c, n in introducing(repo, rel, v)]}")
    return d


def reopen_commit(repo, commit, rel):
    """decision 27 (records 93, 99): the commit's version V is future and is new, i.e. no parent's version contains it
    (going back to an old version is no event, decision 8 rule 2); a current version C is a parent's version or in V's
    prev; and no parent holds a future version that contains C or a version with C's body (that parent already carried
    the reopen). If a blob it needs is missing (C itself, or one on a parent's walk), it is not counted (records 99,
    110). One formula replaces v14 1.5d's.
    Returns the current versions it reopens."""
    v = repo.blob_at(commit, rel)
    t = repo.cat(v) if v else None
    if not t or fields(t)["state"][0] != "future":
        return []
    parents = repo.out("rev-list", "--parents", "-n1", commit).split()[1:]
    pv = [repo.blob_at(p, rel) for p in parents]
    if any(x and contains(repo, x, v) for x in pv):
        return []
    out = []
    for c in sorted(set(prevs(t)) | {x for x in pv if x}):
        ct = repo.cat(c)
        if not ct or fields(ct)["state"][0] != "current":
            continue
        carried = False
        for x in pv:
            xt = repo.cat(x) if x else None
            if xt and fields(xt)["state"][0] == "future":
                anc = ancestors(repo, x)
                if any(a == c or (repo.cat(a) and body(repo.cat(a)) == body(ct)) for a in anc):
                    carried = True
                elif any(repo.cat(a) is None for a in anc):
                    carried = True  # a blob it needs is missing: it cannot be judged, so it is not counted (record 110)
        if not carried:
            out.append(c)
    return out


def reopen_commit_v14(repo, commit, rel):
    """v14 1.5d as written, for comparison only: K is future in the commit, current in some parent, and the commit's
    version differs from every parent's version."""
    v = repo.blob_at(commit, rel)
    t = repo.cat(v) if v else None
    if not t or fields(t)["state"][0] != "future":
        return False
    pv = [repo.blob_at(p, rel) for p in repo.out("rev-list", "--parents", "-n1", commit).split()[1:]]
    return v not in pv and any(x and fields(repo.cat(x))["state"][0] == "current" for x in pv)


# ---------------------------------------------------------------- pinned spec files (record 107): ait decides which side
# changes are not taken; those are taken out of that side; git merges everything like code (same order and merge bases)
def entry_at(repo, rev, path):
    """a tree entry's (mode, blob), or None; specification files pin both (2.4.1)."""
    text = repo.out("--literal-pathspecs", "ls-tree", "-z", rev, "--", path)
    if not text:
        return None
    mode, kind, oid = text.split("\t", 1)[0].split()
    return mode, oid


def pins_at(repo, rev, path):
    """versions at rev that pin path -> {(mode, blob): [IDs]}. Well-formed versions pin only when future or current
    (2.4.1; ended versions pin nothing). A malformed version's state cannot be trusted: every `file` row naming the
    path counts, read as words split on white space."""
    out = {}
    for rel in repo.out("ls-tree", "-r", "--name-only", rev, ".ait").split():
        raw = repo.catb(repo.blob_at(rev, rel)) or b""
        ok = well_formed_raw(raw)
        text = raw.decode("utf-8", "replace")
        if ok and fields(text).get("state", [""])[0] not in ("future", "current"):
            continue
        for l in text.split("\n"):
            t = l.split()
            if len(t) >= 4 and t[0] == "file" and t[1] == path:
                out.setdefault((t[2], t[3]), []).append(rel[5:])
    return out


def classify(repo, path, base_, rev, result_pins):
    """one side's change of one path -> (verdict, content to give git, warned). result_pins: the blobs the result's
    future/current contracts pin at this path as (mode, blob) pairs (none if it pins none)."""
    b0, b1 = entry_at(repo, base_, path), entry_at(repo, rev, path)
    if b0 == b1:
        return "no change", b1, False
    # a change that is not taken is given to git as the result's pin (or, when the result does not pin the path at
    # one content, as the classification base's content). A later Git base stays unchanged and can conflict (SF3).
    put = next(iter(result_pins)) if len(result_pins) == 1 else b0
    if b1 is not None and b1 in result_pins:
        return "take (the result pins it)", b1, False
    p = pins_at(repo, rev, path)
    if b1 is not None and b1 in p:
        return f"not taken (old or discarded version's spec: {p[b1]})", put, False
    if p:
        own = [x for x in p if x in result_pins]
        who = sorted({i for v in p.values() for i in v})
        if own:
            return f"not taken, warned (changed outside ait's rules; {who} pin it: that side's pin is the result's, taken)", own[0], True
        return f"not taken, warned (changed outside ait's rules; {who} pin it otherwise)", put, True
    if b1 is None and result_pins:
        return "a removal of a path the result pins: not taken, warned", put, True
    return "take (code at that side)", b1, False


def taken_out(repo, rev, contents):
    """a child commit of rev with the given paths set to (mode, blob) pairs (None = absent)"""
    idx = os.path.join(repo.path, ".git", "pull-taken-out-index")
    env = dict(ENV, GIT_INDEX_FILE=idx)
    run(repo.path, "git", "read-tree", rev, env=env)
    for path, entry in contents.items():
        if entry:
            mode, blob = entry
            run(repo.path, "git", "update-index", "--add", "--cacheinfo", f"{mode},{blob},{path}", env=env)
        else:
            run(repo.path, "git", "update-index", "--force-remove", path, env=env)
    t = run(repo.path, "git", "write-tree", env=env).stdout.strip()
    os.remove(idx)
    return repo.out("commit-tree", t, "-p", rev, "-m", "taken out")


def pull_merge(repo, ends, result_pins, head="HEAD"):
    """record 107: every path a side changed (outside .ait/) is classified against that side's merge base with HEAD
    (my side: with the first end); the changes not taken are taken out of that side; git merges HEAD and the ends in
    the given order. Returns (merged commit, conflicting paths, [(side, path, verdict)])."""
    head = repo.out("rev-parse", head)
    ends = [repo.out("rev-parse", e) for e in ends]
    if not ends:
        # Decision 35: no Git merge can reconcile pins when every end was an ancestor.
        out = {}
        for path, pins in result_pins.items():
            if len(pins) != 1:
                raise ValueError("conflicting specification pins without merge ends: " + path)
            pin = next(iter(pins))
            if entry_at(repo, head, path) != pin:
                out[path] = pin
        return taken_out(repo, head, out) if out else head, [], []
    sides = [("my side", head, repo.out("merge-base", head, ends[0]))] + [(repo.name(e), e, repo.out("merge-base", head, e)) for e in ends]
    inputs, notes = [], []
    for who, rev, b in sides:
        out = {}
        for p in repo.out("diff", "--name-only", "--no-renames", b, rev).split():
            if p.startswith(".ait/"):
                continue
            verdict, content, warned = classify(repo, p, b, rev, set(result_pins.get(p, ())))
            notes.append((who, p, verdict))
            if content != entry_at(repo, rev, p):
                out[p] = content
        inputs.append(taken_out(repo, rev, out) if out else rev)
    acc, conflicts = inputs[0], []
    for e in inputs[1:]:
        x = repo.g("merge-tree", "--write-tree", "--name-only", acc, e, check=False)
        ls = x.stdout.splitlines()
        if x.returncode == 1:
            for l in ls[1:]:
                if not l:
                    break
                conflicts.append(l)
        acc = repo.out("commit-tree", ls[0], "-p", acc, "-p", e, "-m", "merged")
    return acc, [c for c in conflicts if not c.startswith(".ait/")], notes


def result_pins(repo, named, head="HEAD"):
    """the pins of the received result before helper clauses and 6.2: the named contracts' result versions, and my
    versions of every other contract (unnamed contracts keep mine). Only future/current versions pin. -> {path: {(mode, blob)}}"""
    head = repo.out("rev-parse", head)
    versions = {}
    for rel in repo.out("ls-tree", "-r", "--name-only", head, ".ait").split():
        if "/undertake/" not in rel:
            versions[rel[5:]] = repo.blob_at(head, rel)
    versions.update(named)
    out = {}
    for i, v in versions.items():
        t = repo.cat(v) if v else None
        if not t or not well_formed(t) or fields(t)["state"][0] not in ("future", "current"):
            continue
        for fl in fields(t).get("file", []):
            parts = fl.split()
            out.setdefault(parts[0], set()).add((parts[1], parts[2]))
    return out


def judge_pinned(repo, path, ends, named, label="", head="HEAD"):
    """print record 107's data for one path (and every change that is not taken). named: {ID: result version}"""
    pm = result_pins(repo, named, head)
    pins = pm.get(path, set())
    acc, conflicts, notes = pull_merge(repo, ends, pm, head)
    for who, p, verdict in notes:
        if p == path or not verdict.startswith("take") and verdict != "no change":
            print(f"     {label}{p} {who}: {verdict}")
    if path in conflicts:
        print(f"     {label}{path}: git conflicts on it (a code conflict, decision 29)")
    else:
        m = entry_at(repo, acc, path)
        txt = (repo.cat(m[1]) or "").splitlines() if m else None
        show = None if txt is None else (txt[0] if len(txt) < 3 else f"{txt[0]} ... {txt[-1]}")
        tail = "" if m in pins or m is None or not pins else " -> 6.2 re-pin (future) / stop at 5 (current)"
        print(f"     {label}{path}: git's merge = {show!r} ; equals a result pin? {m in pins}{tail}")
    other = [c for c in conflicts if c != path]
    if other:
        print(f"     {label}other code conflicts: {other}")
    return acc, conflicts
