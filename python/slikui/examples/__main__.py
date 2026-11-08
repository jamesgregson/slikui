"""``python -m slikui.examples`` — launcher for the shipped demo suite.

    --list                   print the registry table
    --where                  print this package's directory (read the sources)
    --contract               print what a custom widget receives for each
                             input action, measured live on this build
    --smoke [NAME|all]       scripted self-checking passes (PNGs into --out)
    (no args)                headless chooser: prints the table, renders
                             ./chooser.png

Run one example live with ``python -m slikui.examples.<name> --window``.
"""

from __future__ import annotations

import argparse
import os
import sys

from . import REGISTRY, load, numbered


def _print_list():
    for num, ex in numbered():
        print(f"{num}  {ex.title:<22} {ex.blurb}")


def _contract():
    """Drive a recording widget through scripted input; print what arrived.

    Measured, not documented: the table is whatever this build delivers.
    """
    import slikui
    from . import _support

    T = slikui.EventType

    class Recorder(slikui.Widget):
        def __init__(self):
            super().__init__()
            self.log = []

        def measure(self, constraints):
            return constraints.clamp(slikui.Size(0, 40))

        def focusable(self):
            return True

        def on_event(self, e):
            self.log.append(e.type.name + (f"(clicks={e.clicks})" if e.clicks else ""))
            if e.type == T.MousePress and e.button == slikui.MouseButton.MouseLeft:
                self.context.capture_mouse(self)
            if e.type == T.MouseRelease:
                self.context.release_mouse(self)
            return True

    app = slikui.App(200, 120)
    try:
        root = app.set_root(slikui.Column, padding=0, gap=10)
        w = root.add(Recorder)
        root.add(slikui.Label, "elsewhere")  # y 50..
        session = _support.Session(app)
        session.advance(1)
        inside, outside = (20, 20), (20, 80)
        R, M = slikui.MouseButton.MouseRight, slikui.MouseButton.MouseMiddle
        # (label, frames to wait first, action); 30 frames = 0.5 s, beyond
        # the multi-click time, so only the "second click" pairs up.
        steps = [
            ("pointer enters", 30, lambda: _support.move(app, inside)),
            ("left click", 30, lambda: _support.click(app, inside)),
            ("second click within 0.1 s", 6, lambda: _support.click(app, inside)),
            ("press, drag out, release (capture)", 30,
             lambda: (_support.press(app, inside), _support.move(app, outside),
                      _support.release(app, outside))),
            ("pointer leaves", 30, lambda: _support.move(app, outside)),
            ("right click", 30, lambda: (_support.move(app, inside),
                                         _support.click(app, inside, R))),
            ("middle click", 30, lambda: _support.click(app, inside, M)),
            ("wheel", 30, lambda: _support.wheel(app, inside, 1.0)),
            ("key A down/up (focused)", 30, lambda: _support.tap(app, slikui.Key.A)),
            ("type 'x' (focused)", 30, lambda: _support.type_text(app, "x")),
            ("Tab (consumed by Context)", 30, lambda: _support.tap(app, slikui.Key.Tab)),
            ("app loses focus", 30, lambda: app.handle(slikui.focus_lost())),
        ]
        print(f"slikui {slikui.__version__}: events a custom widget receives\n")
        for label, frames, act in steps:
            n = len(w.log)
            session.advance(frames)
            act()
            got = ", ".join(w.log[n:]) or "(nothing)"
            print(f"  {label:<36} {got}")
    finally:
        app.close()
    return 0


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
    p.add_argument("--where", action="store_true",
                   help="print the examples directory")
    p.add_argument("--contract", action="store_true",
                   help="print the custom-widget event contract, measured live")
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
    if args.where:
        print(os.path.dirname(os.path.abspath(__file__)))
        return 0
    if args.contract:
        return _contract()
    if args.smoke is not None:
        return _smoke(args.smoke, args.out)
    if args.window:
        print("run one example live:  python -m slikui.examples.<name> --window")
        _print_list()
        return 0
    png = _chooser_png("chooser.png")
    _print_list()
    print(f"wrote {png} — run one with: python -m slikui.examples.<name>")
    return 0


if __name__ == "__main__":
    sys.exit(main())
