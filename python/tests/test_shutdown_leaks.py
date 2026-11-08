"""Shutdown refleak regression: callbacks stored in the C++ tree must be
released while the interpreter is alive.

A Python callback that captures a widget wrapper closes a reference cycle
through C++ ownership (tree -> std::function -> lambda -> wrapper ->
keep_alive -> context wrapper), which the GC cannot traverse: nanobind's
keep_alive records are invisible to tp_traverse. Left alone, the cycle
survives until interpreter finalization, when nanobind can no longer release
Python objects — and reports leaks at exit.

The fix is deterministic teardown: `App.close()` destroys the C++ tree
(releasing every callback it holds) while the interpreter is healthy; it is
wired to `App.__exit__` and `App.__del__`, so function-scoped UIs are
collected cleanly. This test builds exactly the cycle shape above in a
subprocess and fails if nanobind prints a leak report at exit.
"""

import subprocess
import sys

CYCLE_SCRIPT = """\
import slikui

def build():  # function scope: everything should be reclaimed before exit
    ui = slikui.App(160, 90)
    root = ui.set_root(slikui.Column)
    val = root.add(slikui.Label, "0")
    s = root.add(slikui.Slider, 0, 1)
    s.on_change(lambda v: val.set_text(str(v)))             # captures a wrapper
    c = slikui.Color.from_hex(0xFF00FF)
    root.add(slikui.Swatch, c, lambda: True, lambda: None)  # captures a Color
    ui.tick(0.0, 1 / 60)
    ui.render()

build()  # no explicit close/del — App.__del__ must break the cycle
"""


def test_no_leak_report_at_shutdown():
    r = subprocess.run([sys.executable, "-c", CYCLE_SCRIPT],
                       capture_output=True, text=True, timeout=120)
    assert r.returncode == 0, r.stderr
    assert "nanobind: leaked" not in r.stderr, r.stderr


# A Python custom widget that hosts children owns that subtree: its C++
# children live and die with the Python instance, not with the tree. When the
# tree releases it (anchor dropped), a callback deep in its own subtree can
# still pin it: callback -> leaf wrapper -> keep_alive -> ... -> the child
# returned by `custom.add(...)` -> keep_alive -> custom. Nothing then runs
# ~PyWidget, so the subtree and every callback in it leak. Shape distilled
# from examples/showcase.py (a Fill container hosting every page).
CUSTOM_HOST_SCRIPT = """\
import slikui

class Fill(slikui.Widget):
    def measure(self, constraints):
        return self.children[0].measure(constraints)

    def arrange(self, area):
        self.bounds = area
        self.children[0].arrange(area)

def build():
    app = slikui.App(160, 90)
    root = app.set_root(slikui.Column)
    page = root.add(Fill).add(slikui.Column)  # keep_alive: page -> Fill
    btn = page.add(slikui.Row).add(slikui.Button, "go")
    btn.on_click(lambda: btn.set_text("went"))  # subtree callback pins btn
    app.tick(0.0, 1 / 60)
    app.render()
    app.close()

build()
"""


def test_custom_container_subtree_released_at_close():
    r = subprocess.run([sys.executable, "-c", CUSTOM_HOST_SCRIPT],
                       capture_output=True, text=True, timeout=120)
    assert r.returncode == 0, r.stderr
    assert "nanobind: leaked" not in r.stderr, r.stderr
