"""The shipped example suite must run headless on every wheel.

Mirrors what the wheel self-test runs (`python -m slikui.examples --smoke
all`): every REGISTRY entry gets its scripted smoke pass. Examples
self-check via _support.Checks, so these tests only assert the harness
contract — the contract exists, the smoke produced a summary, and the PNG
is real.
"""

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
