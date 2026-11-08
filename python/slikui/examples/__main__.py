"""``python -m slikui.examples`` — launcher for the shipped demo suite.

    --list                   print the registry table
    --smoke [NAME|all]       scripted self-checking passes (PNGs into --out)
    (no args)                headless chooser: prints the table, renders
                             ./chooser.png
    --window                 interactive chooser (needs the window host,
                             python/slikui/examples/window.py)
"""

from __future__ import annotations

import argparse
import os
import sys

from . import REGISTRY, load, numbered


def _print_list():
    for num, ex in numbered():
        print(f"{num}  {ex.title:<22} {ex.blurb}")


def _smoke(which, out_dir):
    """Run one example's smoke pass, or every registry entry."""
    os.makedirs(out_dir, exist_ok=True)
    names = [e.name for e in REGISTRY] if which == "all" else [which]
    failed = []
    for name in names:
        mod = load(name)
        png = os.path.join(out_dir, f"{name}_smoke.png")
        try:
            mod.main(["--smoke", png])
        except Exception as exc:  # noqa: BLE001 — report all, exit nonzero
            failed.append(name)
            print(f"[{name}] FAILED: {exc}")
    if failed:
        print(f"smoke: {len(failed)} failure(s): {', '.join(failed)}")
        return 1
    print(f"smoke: {len(names)} example(s) OK — PNGs in {out_dir}/")
    return 0


def _chooser_png(path):
    """Headless chooser: the registry as a clickable-looking surface."""
    import slikui
    from . import _support

    app = slikui.App(460, 64 + 64 * len(REGISTRY))
    try:
        root = app.set_root(slikui.Column, padding=14, gap=8)
        root.add(slikui.Label, "slikui examples", font_size=22)
        for num, ex in numbered():
            row = root.add(slikui.Row, gap=8)
            row.add(slikui.Button, f"{num} {ex.title}")
            row.add(slikui.Label, ex.blurb) \
                .set_color(slikui.Color.from_hex(0x9AA0B5))
        _support.Session(app).advance(2)
        frame = app.render()
        _support.save_image(frame, path)
    finally:
        app.close()
    return path


def main(argv=None):
    p = argparse.ArgumentParser(
        prog="python -m slikui.examples",
        description="Launcher for slikui's shipped Python examples.")
    p.add_argument("--list", action="store_true", help="print the registry")
    p.add_argument("--smoke", nargs="?", const="all", metavar="NAME|all",
                   help="run scripted smoke pass(es), PNGs into --out")
    p.add_argument("--out", default="slikui-examples-out",
                   help="output directory for --smoke PNGs")
    p.add_argument("--window", action="store_true",
                   help="interactive chooser (needs the window host)")
    args = p.parse_args(argv)

    if args.list:
        _print_list()
        return 0
    if args.smoke is not None:
        return _smoke(args.smoke, args.out)
    if args.window:
        try:
            from .window import chooser_window  # noqa: F401 — step 4
        except ImportError:
            print("the interactive chooser needs the window host "
                  "(python/slikui/examples/window.py, rollout step 4)")
            print("meanwhile: --list, or run an example directly:")
            _print_list()
            return 2
    png = _chooser_png("chooser.png")
    _print_list()
    print(f"wrote {png} — run one with: python -m slikui.examples.<name>")
    return 0


if __name__ == "__main__":
    sys.exit(main())
