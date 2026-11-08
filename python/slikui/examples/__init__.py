"""Shipped example suite — the usage reference that matches this exact
version of slikui. ``python -m slikui.examples`` lists and runs them;
``--where`` prints this directory, ``--contract`` prints the custom-widget
event contract measured live.

Where to look:

    hello.py      App, set_root, add(cls, **props), tick, render → numpy
    gallery.py    every built-in widget in one tree
    events.py     custom widget event anatomy: what arrives, in what order
    paint.py      custom drawing: Meter, Sparkline, RadialGauge, ClockFace,
                  BoundsDebug (paint_over)
    controls.py   Knob, XYPad, Joystick (capture-mouse drags, DoubleClick,
                  KeyRelease), LimitSlider (decorating a built-in)
    keyboard.py   focus (Tab, FocusGained/Lost), shortcuts, Snake
    renderer.py   the render contract; plugging in your own renderer
    window.py     a live GLFW app (_window.py is the host)

Custom-widget contract (subclass ``slikui.Widget``, call
``super().__init__()``; add with ``parent.add(MyWidget, args...)``):

* Override ``measure(constraints) -> Size`` (return
  ``constraints.clamp(Size(w, h))``), ``paint(env)`` (draw into
  ``env.list`` in absolute coordinates, ``self.bounds``), ``on_event(e) ->
  bool`` (True = consumed), and as needed ``tick(time)``, ``focusable()``,
  ``stretch_cross()``, ``expands_main()``, ``arrange(area)``,
  ``clip_rect()``, ``paint_over(env)``, ``attached(context)``.
* Layout runs in ``tick()``, not ``render()``: bounds are zero until the
  first tick, and ``self.context`` is None until then.
* Paint order: ``paint``, then the children, then ``paint_over``.
* Mouse events (MouseMove/Press/Release, Click, DoubleClick, Scroll) go to
  the widget under the pointer — or the one that called
  ``self.context.capture_mouse(self)`` — and bubble to ancestors until one
  returns True. Every button is delivered; check ``e.button``. Capture on
  press and ``release_mouse`` on release for drags.
* Click fires after a press + release on the same widget; ``e.clicks``
  counts consecutive clicks; DoubleClick follows the Click with
  ``clicks == 2``.
* MouseEnter/MouseLeave and FocusGained/FocusLost go to the affected widget
  only. FocusLost also arrives when the app loses focus mid-drag.
* KeyPress/KeyRelease go to the focused widget (else the hovered one); Tab,
  Escape and shortcut chords are consumed first. TextInput needs focus.
* Custom widgets draw no text — compose ``Label`` widgets beside them.
* Built-in widget classes can't be subclassed in Python; wrap one in a
  custom widget and decorate it with ``paint_over`` (LimitSlider).
* Keep the wrappers ``add()`` returns (``self.slider = self.add(...)``):
  they are views of widgets the tree owns, valid until ``app.close()``.
* ``int(e.type)`` works; ``e.type.name`` / ``e.key.name`` give names;
  ``repr()`` of events and geometry is readable.

Every module here builds headless (numpy framebuffer; no window, no
display, no glfw), exposes ``SIZE`` / ``build(app)`` / ``smoke(app, state,
session, log)`` / ``main()``, and carries ``# doc: <slug>`` / ``# doc: end``
markers that ``tools/extract_snippets.py`` lifts into the docs.
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
            "What a custom widget receives and in what order: enter/leave,"
            " click/double-click, captured drags, focus, keys.",
            ("input", "custom")),
    Example("paint", "Custom drawing",
            "Pure-Python widgets that do their own drawing: Meter, Sparkline,"
            " RadialGauge, ClockFace, BoundsDebug.",
            ("custom",)),
    Example("controls", "Extended controls",
            "Knob, XYPad and Joystick with full mouse + keyboard handling;"
            " LimitSlider decorates a built-in.",
            ("custom", "input")),
    Example("keyboard", "Keyboard",
            "Focus traversal, focus rings, global shortcuts, and Snake.",
            ("input", "custom")),
    Example("renderer", "Render contract",
            "One tree through the C++ and Python renderers (pixel-identical),"
            " and the skeleton of your own.",
            ("render",)),
    Example("window", "Live window",
            "A small app in a real GLFW window; the same tour runs headless.",
            ("window",)),
)


def load(name: str):
    """Import one example module; REGISTRY, launcher and tooling agree."""
    if name not in {e.name for e in REGISTRY}:
        raise KeyError(f"unknown example {name!r} (try --list)")
    return importlib.import_module(f"{__name__}.{name}")


def numbered():
    """``(num, example)`` pairs with stable ``01``… titles for humans."""
    return [(f"{i:02d}", e) for i, e in enumerate(REGISTRY, 1)]
