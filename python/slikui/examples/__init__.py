"""Shipped example suite — `python -m slikui.examples` lists and runs them.

Every module here:

* builds headless (numpy framebuffer; no window, no display, no glfw),
* exposes ``SIZE`` / ``build(app)`` / ``smoke(app, state, session, log)`` /
  ``main()``,
* carries ``# doc: <slug>`` / ``# doc: end`` markers that
  ``tools/extract_snippets.py`` lifts into the docs, so guide pages quote
  real demo code rather than copies that drift.

``REGISTRY`` is the single source of truth for the launcher, the wheel
smoke run (``python -m slikui.examples --smoke all``) and the docs gallery
page. Entries land with their rollout step; numbering is positional, so
titles carry no numbers.
"""

from __future__ import annotations

import importlib
from typing import NamedTuple


class Example(NamedTuple):
    name: str         # module name in this package
    title: str        # display title (no number: numbering is positional)
    blurb: str
    tags: tuple = ()  # "basics", "custom", "input", "render", "window"


REGISTRY: tuple[Example, ...] = (
    Example("hello", "Hello, slikui",
            "The smallest complete example: App, root, add, tick, render.",
            ("basics",)),
    Example("gallery", "Widget gallery",
            "The whole widget set in one headless surface.",
            ("basics",)),
    Example("events", "Event anatomy",
            "What slikui events look like, who receives them, how consumption,"
            " focus routing and the no-capture drag rule work.",
            ("input", "custom")),
    Example("paint", "Custom drawing",
            "Pure-Python widgets that do their own drawing: Meter, Sparkline,"
            " RadialGauge, ClockFace, BoundsDebug.",
            ("custom",)),
)


def load(name: str):
    """Import one example module; REGISTRY, launcher and tooling agree."""
    if name not in {e.name for e in REGISTRY}:
        raise KeyError(f"unknown example {name!r} (try --list)")
    return importlib.import_module(f"{__name__}.{name}")


def numbered():
    """``(num, example)`` pairs with stable ``01``… titles for humans."""
    return [(f"{i:02d}", e) for i, e in enumerate(REGISTRY, 1)]
