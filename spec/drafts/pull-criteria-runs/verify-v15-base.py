"""SF3: filtering a discarded specification does not change Git's merge base."""
from pathlib import Path
import shutil
import tempfile

source = Path(__file__).with_name("lib.py")
ns = {"__file__": str(source)}
exec(compile(source.read_text(), str(source), "exec"), ns)
scratch = tempfile.mkdtemp(prefix="ait-v15-base-")
ns["RUNS"] = scratch
ns["ENV"].pop("HOME", None)
try:
    r = ns["Repo"]("base")
    r.write("src/stock.c", "int stock;\n")
    r.write("src/order.c", "int order;\n")
    for contract, content in (("D", "d0"), ("Q", "q0"), ("Y", "p0")):
        r.write("specs/" + contract.lower() + ".yaml", "body: " + content + "\n")
        r.record(contract)
    head = r.commit("init")
    r.co("common", new=True)
    r.write("specs/y.yaml", "body: p1\n")
    r.record("Y")
    common = r.commit("common")
    r.co("a", new=True)
    r.write("specs/y.yaml", "body: p2\n")
    r.record("Y")
    r.write("specs/d.yaml", "body: d-A\n")
    da = r.record("D")
    a = r.commit("a")
    r.co(common)
    r.co("b", new=True)
    yb = r.record("Y", state="cancelled")
    r.write("specs/y.yaml", "body: p3\n")
    r.write("specs/q.yaml", "body: q-B\n")
    qb = r.record("Q")
    b = r.commit("b")
    r.co("main")
    pins = ns["result_pins"](r, {"D": da, "Q": qb, "Y": yb})
    assert "specs/y.yaml" not in pins
    assert r.out("merge-base", a, b) == common
    for ends in ([a, b], [b, a]):
        result, conflicts, notes = ns["pull_merge"](r, ends, pins)
        assert conflicts == ["specs/y.yaml"], (ends, conflicts)
        assert any(p == "specs/y.yaml" and verdict.startswith("not taken")
                   for side, p, verdict in notes), notes
    assert r.out("rev-parse", "HEAD") == head
    assert r.out("show", common + ":specs/y.yaml") == "body: p1"
    print("both merge orders retain Git's base and report the p0/p3 conflict")
finally:
    shutil.rmtree(scratch)
