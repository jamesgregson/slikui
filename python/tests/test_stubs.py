"""The committed type stub (python/slikui/_slikui.pyi) must match the built
extension — regenerate with `python tools/gen_stubs.py` after binding
changes."""

import ast
import os
import pathlib
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
STUB = ROOT / "python" / "slikui" / "_slikui.pyi"


def test_stub_is_valid_python():
    ast.parse(STUB.read_text(encoding="utf-8"))


def test_stub_matches_built_module():
    # stubgen output can vary across the wheel matrix's Python versions;
    # drift is a dev-time check (local runs and the regular test job).
    if os.environ.get("CIBUILDWHEEL"):
        pytest.skip("stub drift is checked outside the wheel matrix")
    if not (ROOT / "third_party" / "nanobind" / "src" / "stubgen.py").exists():
        pytest.skip("nanobind stubgen not available (sdist without third_party?)")
    sys.path.insert(0, str(TOOLS))
    try:
        import gen_stubs
    finally:
        sys.path.pop(0)
    assert gen_stubs.generate() == STUB.read_text(encoding="utf-8"), (
        "python/slikui/_slikui.pyi is stale — run: python tools/gen_stubs.py")
