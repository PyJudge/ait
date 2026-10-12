#!/usr/bin/env python3
"""
State explorer for ait: random sequences of ait and git operations in a fresh
repository, with invariants checked after every step. A sequence that breaks
one is shrunk and written as a shell script that reproduces it.

Invariants
  I1  exit codes are 0, 1, 128 or 129, and nothing crashes
  I2  a refused ait write command changes nothing (HEAD, index, work tree)
  I3  an ait command that succeeds leaves HEAD no new error of 5.10
      ('ait hook post-commit' against HEAD^ warns of none)
  I4  a hint that gives one 'ait ...' command to run now, with nothing to fill
      in, is taken when run as given in a copy of the repository, after the
      states it names first (restore, remove): it succeeds,
      or says itself that it needs results or that local changes stop it, or
      that another contract, or an error the repository already had (todo
      warns of it) other than the one the hint answered, stops it (5.9a, user
      decision 52); advice such as 'when ready' is not checked
  I5  'ait todo' succeeds in every state

usage: tools/explore.py [--ait DIR] [--seeds N] [--first S] [--steps K] [--jobs J] [--out DIR]
"""
import argparse
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ProcessPoolExecutor, as_completed

AIT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CODE = ["src/a.c", "src/b.c", "lib/c.c"]
DEFS = ["specs/p.yaml", "specs/q.yaml", "specs/x.yaml"]
READERS = ("todo", "peek", "session")
SETUP = """git init -q r && cd r
mkdir src lib specs tests
for f in src/a.c src/b.c lib/c.c; do seq 1 20 | sed "s|^|$f |" > $f; done
echo 'body: p holds' > specs/p.yaml; echo 'body: q holds' > specs/q.yaml
printf '#!/bin/sh\nexit 0\n' > tests/x.sh; chmod +x tests/x.sh
printf 'body: x passes\nengine: exec\ncommand: [./tests/x.sh]\ninputs: [tests/x.sh]\n' > specs/x.yaml
git add -A && git commit -qm base
ait init >/dev/null
SA=$(ait session open --agent a)
SB=$(ait session open --agent b 2>/dev/null)
echo "SA=$SA"; echo "SB=$SB"
"""


class Run:
    """One sequence of steps in its own repository."""

    def __init__(self, root, ait_dir):
        self.root = root
        self.dir = os.path.join(root, "r")
        self.vars = {}
        self.n = 0
        self.seen = {"ok": 0, "refused": 0, "hints": 0}
        self.env = dict(os.environ, HOME=root, GIT_CONFIG_NOSYSTEM="1", GIT_EDITOR=":", LC_ALL="C",
                        PATH=ait_dir + os.pathsep + os.environ["PATH"])
        self.env.pop("AIT_SESSION", None)
        with open(os.path.join(root, ".gitconfig"), "w") as f:
            f.write("[user]\n\tname = t\n\temail = t@t\n[init]\n\tdefaultBranch = master\n")
        for line in self.sh(SETUP, cwd=root)[1].splitlines():
            name, _, value = line.partition("=")
            self.vars[name] = value

    def tick(self):
        self.n += 60
        stamp = "@%d +0000" % (1700000000 + self.n)
        self.env["GIT_COMMITTER_DATE"] = self.env["GIT_AUTHOR_DATE"] = stamp

    def sh(self, script, cwd=None):
        self.tick()
        script = re.sub(r"\$([DOX][0-9]+|S[AB])(?![0-9])", lambda m: self.vars.get(m.group(1), m.group(0)), script)
        p = subprocess.run(["sh", "-c", script], cwd=cwd or self.dir, env=self.env,
                           capture_output=True, text=True, errors="replace")
        return p.returncode, p.stdout, p.stderr

    def snapshot(self):
        """HEAD, the index, and every file of the work tree with its content and executable bit"""
        files = []
        for top, dirs, names in os.walk(self.dir):
            dirs[:] = [d for d in dirs if d != ".git"]
            for name in names:
                path = os.path.join(top, name)
                if os.path.islink(path):
                    files.append((path, "link", os.readlink(path)))
                else:
                    with open(path, "rb") as f:
                        files.append((path, os.access(path, os.X_OK), f.read()))
        return self.sh("git rev-parse HEAD; git ls-files -s")[1], sorted(files)

    def bind(self, out):
        for line in out.splitlines():
            m = re.match(r"new ([DOX][0-9a-f]{12})", line)
            if m:
                kind = m.group(1)[0]
                k = len(self.ids(kind))
                self.vars["%s%d" % (kind, k)] = m.group(1)

    def ids(self, kind):
        return ["$" + v for v in sorted(self.vars) if v[0] == kind and v[1:].isdigit()]


def scope(rng):
    return rng.choice(["src/a.c", "src/b.c", "lib/c.c", "src/a.c:2-5", "src/", "new:src/n.c", "src/a.c,lib/c.c"])


def step(rng, run):
    """The next step: (setup, command), both shell text; the command is the one judged."""
    d, o = run.ids("D"), run.ids("O")
    k = d + o
    f = rng.choice(DEFS)
    line = rng.randint(1, 20)
    choices = [
        ("", "ait future offer %s %s -m o" % (scope(rng), f)),
        ("", "ait future demand %s %s %s -m d" % (rng.choice(CODE), scope(rng), f)),
        ("", "ait offer %s %s --force -m o" % (rng.choice(CODE), f)),
        ("", "ait demand %s %s %s --force -m d" % (rng.choice(CODE), rng.choice(CODE), f)),
        ("sed -i.bak '%ds/$/ x/' %s && rm -f %s.bak && git add -A" % (line, rng.choice(CODE), rng.choice(CODE)),
         rng.choice(["ait commit -m c", "ait commit --force -m c", "git commit -qm c"])),
        ("echo 'body: changed %d' > %s && ait contract add %s >/dev/null" % (line, f, f),
         rng.choice(["ait contract commit -m s", "git commit -qm s"])),
        ("", "git revert --no-edit HEAD"),
        ("", "ait todo"),
        ("echo 'exit %d' >> tests/x.sh && ait contract add specs/x.yaml tests/x.sh >/dev/null" % (line % 2),
         "ait contract commit -m e"),
        ("git rm -q %s" % rng.choice(DEFS + ["tests/x.sh"]), "git commit -qm gone"),
        # what only a commit that skips the hooks leaves at HEAD (5.10)
        ("git rm -q %s" % rng.choice(DEFS + ["tests/x.sh"]), "git commit -q --no-verify -m nv"),
        ("printf 'body: b\\ninputs: [tests/none.sh]\\n' > %s && git add %s" % (f, f), "git commit -q --no-verify -m nv"),
        ("rm -f tests/x.sh && ln -s ../src/a.c tests/x.sh && git add -A tests", "git commit -q --no-verify -m nv"),
    ]
    if k:
        x = rng.choice(k)
        choices += [
            ("", "ait future amend %s -m a" % x),
            ("", "ait future amend %s %s%s %s -m a" % (x, "- " if x in d else "", scope(rng), rng.choice(DEFS + ["-"]))),
            ("", "ait amend %s%s -m a" % (x, rng.choice(["", " --force"]))),
            ("", "ait future cancel %s -m c" % x),
            ("git add -A", "ait fulfill %s%s -m f" % (x, rng.choice(["", " --force"]))),
            ("sed -i.bak 's/^state .*/state %s/' .ait/%s && rm -f .ait/%s.bak && ait contract add .ait/%s >/dev/null"
             % (rng.choice(["future", "current", "cancelled"]), x, x, x), "ait contract commit -m w"),
            ("sed -i.bak 's/^state .*/state %s/' .ait/%s && rm -f .ait/%s.bak && git add .ait/%s"
             % (rng.choice(["future", "current", "cancelled", "withdrawn"]), x, x, x), "git commit -q --no-verify -m nv"),
            ("sed -i.bak '%s' .ait/%s && rm -f .ait/%s.bak && ait contract add .ait/%s >/dev/null"
             % (rng.choice(["s|^provider .*|provider src/b.c|", "s|^creator .*|creator z S000000000000|",
                            "s|^prev .*|prev junk|", "/^file /d"]), x, x, x),
             rng.choice(["ait contract commit -m w", "git commit -qm w --no-verify"])),
            ("git add -A", "ait fulfill %s=%s --force -m f" % (x, rng.choice(CODE))),
            ("", "ait contract pull %s" % x),
            ("", "ait contract pull %s=HEAD -m p" % x),
            ("", "ait todo %s" % x),
        ]
    if d and o:
        x, y = rng.choice(d), rng.choice(o)
        choices += [("", "ait undertake %s %s -m u" % (x, y)), ("", "ait release %s %s -m r" % (x, y)),
                    ("", "ait future offer %s %s as o undertake %s o -m u" % (scope(rng), f, x))]
    if rng.random() < 0.15:
        b = "b%d" % rng.randint(0, 2)
        choices.append(("git switch -q -c %s 2>/dev/null || git switch -q %s" % (b, b), "git commit -q --allow-empty -m on"))
        choices.append(("git switch -q master", "git merge --no-edit %s || git merge --abort" % b))
        choices.append(("git switch -q master", "git merge --no-edit %s || { git checkout -q --theirs -- . 2>/dev/null; "
                        "git add -A && git commit -q --no-edit; } || git merge --abort" % b))
    setup, cmd = rng.choice(choices)
    if cmd.startswith("ait ") and rng.random() < 0.15:
        cmd = "AIT_SESSION=$SB " + cmd
    return setup, cmd


def repair(text):
    """
    The states a hint names before its command (5.9a: 'restore P... as in SRC',
    'remove P...'), as shell that makes them, and the text after them
    """
    prep = []
    while True:
        m = re.match(r"(restore|remove) ((?:'[^']*' ?)+?)(?: as in '([^']+)')?(?:, then |, )", text)
        if not m or (m.group(1) == "restore") != bool(m.group(3)):
            return prep, text
        for path in re.findall(r"'([^']*)'", m.group(2)):
            if m.group(3):
                prep.append("{ git cat-file -e '%s:%s' 2>/dev/null && git checkout -q '%s' -- '%s' || "
                            "git rm -q -rf --ignore-unmatch -- '%s'; }" % (m.group(3), path, m.group(3), path, path))
            else:
                prep.append("{ git rm -q -rf --ignore-unmatch -- '%s'; rm -rf '%s'; }" % (path, path))
        text = text[m.end():]


def hint_commands(err):
    """
    I4's hint: the first of an output, as the others follow it in turn (5.8),
    when it gives one 'ait ...' command to run now with nothing to fill in, after
    the states it names first; advice for later or for a condition ('when
    ready', 'to need it again') is git's kind
    """
    for line in err.splitlines():
        if not line.startswith("hint: "):
            continue
        prep, rest = repair(line[6:])
        m = re.fullmatch(r"(?:run )?'(ait [^']*)'(?:, then rerun this command)?", rest)
        cmds = re.findall(r"'(ait [^']*)'", rest)
        if len(cmds) == 1 and not re.search(r"<[^w]", cmds[0]) and (m or (
                not prep and rest.endswith("'") and not re.search(r"then|restore|once|push|next time|when ready|need it again", rest))):
            yield line, " && ".join(prep + [cmds[0].replace("<why>", "x")])
        return


def taken(run, cmd, clean):
    """CMD run in a copy of the repository, its local changes set aside first when CLEAN: (taken, stderr)."""
    copy = tempfile.mkdtemp(dir=run.root)
    shutil.rmtree(copy)
    shutil.copytree(run.dir, copy, symlinks=True)
    top = run.sh("git rev-parse --show-toplevel")[1].strip()
    new = subprocess.run(["git", "rev-parse", "--show-toplevel"], cwd=copy, capture_output=True,
                         text=True, env=run.env).stdout.strip()
    sessions = os.path.join(copy, ".git/ait/sessions")
    for name in os.listdir(sessions) if os.path.isdir(sessions) else []:
        path = os.path.join(sessions, name)
        with open(path) as f:
            text = f.read()
        with open(path, "w") as f:
            f.write(text.replace("worktree %s\n" % top, "worktree %s\n" % new))

    def key(path):
        return subprocess.run(["git", "hash-object", "--stdin"], input=path, capture_output=True,
                              text=True, env=run.env).stdout.strip()
    pointer = os.path.join(copy, ".git/ait/worktrees", key(top))
    if os.path.exists(pointer):
        os.rename(pointer, os.path.join(copy, ".git/ait/worktrees", key(new)))
    if clean:
        subprocess.run(["sh", "-c", "git reset -q --hard && git clean -qfdx"], cwd=copy, env=run.env,
                       capture_output=True)
    p = subprocess.run(["sh", "-c", cmd], cwd=copy, env=run.env, capture_output=True, text=True, errors="replace")
    shutil.rmtree(copy)
    return p.returncode == 0 or "error: check results required" in p.stderr, p.stderr


def first_error(err):
    """the message of the first error of ERR, or of its first warning, without the word before it"""
    lines = err.splitlines()
    return next((l.split(": ", 1)[1] for l in lines if l.startswith(("error:", "fatal:"))), None) or \
        next((l.split(": ", 1)[1] for l in lines if l.startswith("warning:")), "")


def replay(run, line, cmd, answered):
    """
    I4: CMD, from a hint that answered the error ANSWERED, is taken as given, or
    once local changes are set aside: what stops it there is its own to say
    (5.9a local changes, user decision 52)
    """
    ok, err = taken(run, cmd, False)
    first = first_error(err)
    # local changes stop it, which it says itself (choice 228), or it runs once they are set aside
    if ok or re.match(r"(uncommitted changes in|contract changes and code changes are mixed)", first) or \
            taken(run, cmd, True)[0]:
        return None
    # a refusal about another contract than the one the hint names is that command's own report (user decision 52)
    named, hinted = set(re.findall(r"[DOX][0-9a-f]{12}", first)), set(re.findall(r"[DOX][0-9a-f]{12}", cmd))
    if named and hinted and not named & hinted:
        return None
    # so is an error the repository already had, which the hint's judgment did not read (user decision 52)
    warned = [l for l in run.sh("ait todo")[2].splitlines() if l.startswith("warning:")]
    if named and first != answered and all(any(k in l for l in warned) for k in named):
        return None
    return "I4 hint not taken: %s\n%s" % (line, err)


def check(run, setup, cmd):
    """Run one step; the first broken invariant, or None."""
    if setup:
        run.sh(setup)
    before = run.snapshot()
    rc, out, err = run.sh(cmd)
    run.bind(out)
    run.seen["refused" if rc else "ok"] += 1
    if rc not in (0, 1, 128, 129) or "BUG" in err or "AddressSanitizer" in err:
        return "I1 exit %d of '%s'\n%s" % (rc, cmd, err)
    bare = re.sub(r"^AIT_SESSION=\S+ ", "", cmd)
    writes = bare.startswith("ait ") and bare.split()[1] not in READERS
    if writes and rc and run.snapshot() != before:
        return "I2 refused '%s' changed the repository\n%s" % (cmd, err)
    if writes and not rc and "\nrequest R" in "\n" + out:
        prc, _, perr = run.sh("ait hook post-commit")
        if "warning:" in perr:
            return "I3 '%s' left HEAD a new error\n%s" % (cmd, perr)
    for line, hinted in hint_commands(err):
        run.seen["hints"] += 1
        broken = replay(run, line, hinted, first_error(err))
        if broken:
            return broken
    trc, _, terr = run.sh("ait todo")
    if trc:
        return "I5 ait todo exit %d\n%s" % (trc, terr)
    return None


def play(steps, ait_dir):
    """Steps from scratch; (index, violation) of the first broken invariant, or None."""
    root = tempfile.mkdtemp(prefix="ait-explore-")
    try:
        run = Run(root, ait_dir)
        for i, (setup, cmd) in enumerate(steps):
            broken = check(run, setup, cmd)
            if broken:
                return i, broken
        return None
    finally:
        shutil.rmtree(root, ignore_errors=True)


def generate(seed, count, ait_dir):
    """A random sequence played as it is made; (steps, index, violation or None, what was seen)."""
    rng = random.Random(seed)
    root = tempfile.mkdtemp(prefix="ait-explore-")
    try:
        run = Run(root, ait_dir)
        steps = []
        for _ in range(count):
            steps.append(step(rng, run))
            broken = check(run, *steps[-1])
            if broken:
                return steps, len(steps) - 1, broken, run.seen
        return steps, None, None, run.seen
    finally:
        shutil.rmtree(root, ignore_errors=True)


def kind(violation):
    return violation.split()[0]


def shrink(steps, violation, ait_dir):
    """Drop steps while the same invariant still breaks at the last one."""
    want = kind(violation)
    i = 0
    while i < len(steps) - 1:
        trial = steps[:i] + steps[i + 1:]
        got = play(trial, ait_dir)
        if got and got[0] == len(trial) - 1 and kind(got[1]) == want:
            steps, violation = trial, got[1]
        else:
            i += 1
    return steps, violation


def script(steps, violation):
    lines = ["#!/bin/sh"] + ["# " + line for line in violation.splitlines()] + [
             'cd "$(mktemp -d)"', 'export HOME=$PWD GIT_CONFIG_NOSYSTEM=1 GIT_EDITOR=: LC_ALL=C',
             "git config --global user.name t; git config --global user.email t@t; git config --global init.defaultBranch master",
             'N=0; tick() { N=$((N+60)); export GIT_COMMITTER_DATE="@$((1700000000+N)) +0000" GIT_AUTHOR_DATE="@$((1700000000+N)) +0000"; }',
             "tick", SETUP.strip(), "bind() { for k in D O X; do i=$(set | grep -c \"^$k[0-9]*=\"); "
             "for id in $(sed -n \"s/^new \\($k[0-9a-f]*\\).*/\\1/p\" out); do eval \"$k$i=$id\"; i=$((i+1)); done; done; }"]
    for setup, cmd in steps:
        if setup:
            lines.append("tick; " + setup)
        lines.append("tick; %s > out 2> err; cat out err; bind" % cmd)
    return "\n".join(lines) + "\n"


def explore(seed, count, ait_dir, out):
    steps, index, violation, seen = generate(seed, count, ait_dir)
    if not violation:
        return seed, None, seen
    steps, violation = shrink(steps[: index + 1], violation, ait_dir)
    path = os.path.join(out, "seed%d.sh" % seed)
    with open(path, "w") as f:
        f.write(script(steps, violation))
    return seed, "%s (%d steps): %s" % (path, len(steps), violation.splitlines()[0]), seen


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ait", default=AIT_DIR)
    ap.add_argument("--seeds", type=int, default=20)
    ap.add_argument("--first", type=int, default=1)
    ap.add_argument("--steps", type=int, default=30)
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    ap.add_argument("--out", default=os.path.join(tempfile.gettempdir(), "ait-explore"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    bad = 0
    total = {"ok": 0, "refused": 0, "hints": 0}
    with ProcessPoolExecutor(a.jobs) as pool:
        jobs = [pool.submit(explore, s, a.steps, os.path.abspath(a.ait), a.out)
                for s in range(a.first, a.first + a.seeds)]
        for job in as_completed(jobs):
            seed, found, seen = job.result()
            print("seed %d: %s" % (seed, found or "ok"), flush=True)
            bad += found is not None
            for k in total:
                total[k] += seen[k]
    print("# %d of %d seeds broke an invariant; commands %d taken, %d refused; %d hints run" %
          (bad, a.seeds, total["ok"], total["refused"], total["hints"]))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
