"""RV3/RV4: restore compatible specification pins without re-merging ancestors."""
from pathlib import Path
import shutil
import sys
import tempfile

source = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).with_name("lib.py")
ns = {"__file__": str(source)}
exec(compile(source.read_text(), str(source), "exec"), ns)
scratch = tempfile.mkdtemp(prefix="ait-v15-zero-ends-")
ns["RUNS"] = scratch
ns["ENV"].pop("HOME", None)
try:
    r = ns["Repo"]("zero")
    r.write("src/stock.c", "int stock;\n")
    r.write("src/order.c", "int order;\n")
    r.write("specs/shared.yaml", "body: old\n")
    base = r.commit("old specification")
    r.write("specs/shared.yaml", "body: selected\n")
    a = r.commit("selected specification")
    pin = ns["entry_at"](r, a, "specs/shared.yaml")
    r.g("revert", "--no-edit", a)
    head = r.out("rev-parse", "HEAD")
    assert r.g("merge-base", "--is-ancestor", a, head, check=False).returncode == 0
    restored, conflicts, notes = ns["pull_merge"](r, [], {"specs/shared.yaml": {pin}})
    assert not conflicts
    assert ns["entry_at"](r, restored, "specs/shared.yaml") == pin
    assert r.out("rev-parse", "HEAD") == head
    assert ns["entry_at"](r, restored, "src/stock.c") == ns["entry_at"](r, head, "src/stock.c")
    other_blob = ns["entry_at"](r, base, "specs/shared.yaml")
    other_mode = ("100755", pin[1])
    for incompatible in (other_blob, other_mode):
        try:
            ns["pull_merge"](r, [], {"specs/shared.yaml": {pin, incompatible}})
        except ValueError as exc:
            assert str(exc) == "conflicting specification pins without merge ends: specs/shared.yaml"
        else:
            raise AssertionError("incompatible pins were accepted")
    print("zero ends restore compatible pins; conflicting blobs or modes are rejected")
finally:
    shutil.rmtree(scratch)
