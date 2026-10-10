"""Regression for the model's full specification-file pins (2.4.1), in temporary repositories."""
from pathlib import Path
import os
import shutil
import sys
import tempfile

source = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).with_name("lib.py")
ns = {"__file__": str(source)}
exec(compile(source.read_text(), str(source), "exec"), ns)
scratch = tempfile.mkdtemp(prefix="ait-v15-mode-")
ns["RUNS"] = scratch
ns["ENV"].pop("HOME", None)
try:
    r = ns["Repo"]("mode")
    r.write("src/stock.c", "int value;\n")
    r.write("src/order.c", "int order;\n")
    r.write("specs/d.yaml", "body: c0\ninputs: [tests/t.sh]\n")
    r.write("tests/t.sh", "#!/bin/sh\nexit 0\n")
    os.chmod(Path(r.path) / "tests/t.sh", 0o755)
    contract = "D000000000001"
    r.record(contract, spec="specs/d.yaml", inputs=("tests/t.sh",),
             creator="agent-a S000000000001")
    rel = ".ait/" + contract
    r.write(rel, r.read(rel).replace("file tests/t.sh 100644 ", "file tests/t.sh 100755 "))
    base = r.commit("pin executable input")
    blob = r.blob_at(base, "tests/t.sh")
    pins = {("100755", blob)}
    for change_content in (False, True):
        r.g("reset", "--hard", base)
        os.chmod(Path(r.path) / "tests/t.sh", 0o644)
        if change_content:
            r.write("tests/t.sh", "#!/bin/sh\nexit 1\n")
        changed = r.commit("outside change")
        verdict, replacement, warned = ns["classify"](r, "tests/t.sh", base, changed, pins)
        assert warned, (change_content, verdict)
        assert replacement == ("100755", blob), (change_content, replacement)
        restored = ns["taken_out"](r, changed, {"tests/t.sh": replacement})
        mode, kind, restored_blob = r.out("ls-tree", restored, "tests/t.sh").split("\t", 1)[0].split()
        assert (mode, restored_blob) == ("100755", blob)
    # An ordinary code file keeps its own mode; it is not a specification pin.
    r.g("reset", "--hard", base)
    r.write("tests/code.sh", "#!/bin/sh\nexit 0\n")
    ordinary = r.commit("ordinary file")
    os.chmod(Path(r.path) / "tests/code.sh", 0o755)
    changed = r.commit("ordinary mode change")
    verdict, replacement, warned = ns["classify"](r, "tests/code.sh", ordinary, changed, set())
    assert verdict == "take (code at that side)" and not warned
    assert replacement[0] == "100755"
    print("mode-only and content changes restore the complete pin; ordinary mode changes are taken")
finally:
    shutil.rmtree(scratch)
