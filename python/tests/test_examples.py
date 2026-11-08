"""The shipped example suite must run headless on every wheel.

Mirrors what the wheel self-test runs (`python -m slikui.examples --smoke
all`): every REGISTRY entry gets its scripted smoke pass. Examples
self-check via _support.Checks, so these tests only assert the harness
contract — the contract exists, the smoke produced a summary, and the PNG
is real.
"""

import pathlib

import pytest

from slikui.examples import REGISTRY, load
from slikui.examples import _support


@pytest.mark.parametrize("example", REGISTRY, ids=lambda e: e.name)
def test_smoke_pass(example, tmp_path):
    mod = load(example.name)
    png = tmp_path / f"{example.name}_smoke.png"
    summary = _support.run_smoke(example.name, mod.SIZE, mod.build, mod.smoke,
                                 png=str(png), scale=getattr(mod, "SCALE", 1.0))
    assert png.exists() and png.stat().st_size > 0
    assert summary, "smoke should report a summary dict"


@pytest.mark.parametrize("example", REGISTRY, ids=lambda e: e.name)
def test_registry_contract(example):
    """SIZE / build / smoke / main — what the launcher and driver consume."""
    mod = load(example.name)
    for attr in ("NAME", "SIZE", "build", "smoke", "main"):
        assert hasattr(mod, attr), (example.name, attr)
    assert mod.NAME == example.name


def test_launcher_list(capsys):
    from slikui.examples import __main__ as launcher

    assert launcher.main(["--list"]) == 0
    out = capsys.readouterr().out
    for example in REGISTRY:
        assert example.title in out


def test_hello_cli_smoke(tmp_path):
    hello = load("hello")
    assert hello.main(["--smoke", str(tmp_path / "hello_smoke.png")]) == 0
    assert (tmp_path / "hello_smoke.png").exists()


def test_paint_docs_image(tmp_path):
    """The --docs-image mode renders an example's DOCS trees (the exact
    images the docs pages embed, e.g. docs/images/custom_widget.png)."""
    paint = load("paint")
    png = tmp_path / "custom_widget.png"
    assert paint.main(["--docs-image", "custom_widget", str(png)]) == 0
    assert png.exists() and png.stat().st_size > 0


@pytest.mark.parametrize("example", REGISTRY, ids=lambda e: e.name)
def test_smoke_is_leak_free(example, tmp_path):
    """Run each smoke pass in a fresh interpreter and fail on nanobind's
    shutdown leak report — examples get copied into apps, so a reference
    cycle in one (e.g. a custom widget holding a callback that captures one
    of its own descendants) would be copied too."""
    import subprocess
    import sys

    out = subprocess.run(
        [sys.executable, "-m", f"slikui.examples.{example.name}",
         "--smoke", str(tmp_path / "s.png")],
        capture_output=True, text=True, cwd=tmp_path, timeout=120)
    assert out.returncode == 0, out.stdout + out.stderr
    assert "leaked" not in out.stderr, out.stderr


def test_launcher_contract_and_where(capsys):
    from slikui.examples import __main__ as launcher

    assert launcher.main(["--contract"]) == 0
    out = capsys.readouterr().out
    for kind in ("MouseEnter", "Click(clicks=1)", "DoubleClick(clicks=2)",
                 "MouseLeave", "KeyRelease", "FocusLost"):
        assert kind in out, kind
    assert launcher.main(["--where"]) == 0
    where = capsys.readouterr().out.strip()
    assert (pathlib.Path(where) / "events.py").exists()


def _doc_regions():
    """(module, slug, text) for every `# doc:` region in the examples."""
    import re

    pkg = pathlib.Path(load("hello").__file__).parent
    for path in sorted(pkg.glob("[a-z]*.py")):
        src = path.read_text(encoding="utf-8")
        for m in re.finditer(r"# doc: (\S+)\n(.*?)# doc: end", src, re.S):
            yield path.stem, m.group(1), m.group(2)


def test_doc_regions_are_copy_paste_safe():
    """Snippets the docs quote must run outside this package: no harness
    imports inside a widget, no references to private helpers."""
    regions = list(_doc_regions())
    assert regions, "no doc regions found"
    for mod, slug, text in regions:
        assert "_support." not in text, f"{mod}:{slug} uses the private harness"


def _undefined_names(src):
    """Globals a region reads but neither defines nor imports."""
    import builtins
    import symtable

    top = symtable.symtable(src, "<region>", "exec")
    defined = {s.get_name() for s in top.get_symbols()
               if s.is_assigned() or s.is_imported()}
    used = set()

    def walk(t):
        for s in t.get_symbols():
            if (s.is_global() or (t is top and s.is_referenced())) \
                    and not s.is_assigned():
                used.add(s.get_name())
        for c in t.get_children():
            walk(c)

    walk(top)
    return sorted(used - defined - set(dir(builtins)))


@pytest.mark.parametrize("region", list(_doc_regions()),
                         ids=lambda r: f"{r[0]}:{r[1]}")
def test_doc_region_is_self_contained(region):
    """A quoted snippet pasted alone into a fresh file must run: every name
    it uses is imported or defined inside it."""
    mod, slug, text = region
    assert _undefined_names(text) == [], f"{mod}:{slug} needs names from outside"
    exec(compile(text, f"{mod}:{slug}", "exec"), {"__name__": "snippet"})


@pytest.mark.parametrize("example", REGISTRY, ids=lambda e: e.name)
def test_example_file_runs_when_copied(example, tmp_path):
    """Copy one example file out of the package and run it as a script."""
    import shutil
    import subprocess
    import sys

    src = pathlib.Path(load(example.name).__file__)
    copy = tmp_path / src.name
    shutil.copy(src, copy)
    out = tmp_path / "smoke.png"
    r = subprocess.run([sys.executable, str(copy), "--smoke", str(out)],
                       cwd=tmp_path, capture_output=True, text=True, timeout=120)
    assert r.returncode == 0, r.stdout + r.stderr
    assert out.stat().st_size > 1000


def test_launcher_runs_by_name_or_number(tmp_path, monkeypatch, capsys):
    from slikui.examples import __main__ as launcher

    monkeypatch.chdir(tmp_path)
    assert launcher.main(["hello"]) == 0
    assert launcher.main(["1"]) == 0  # number from --list
    assert (tmp_path / "hello.png").exists()
    launcher.main(["--list"])
    out = capsys.readouterr().out
    for example in REGISTRY:  # the launchable name is shown
        assert f"  {example.name} " in out
