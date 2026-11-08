# Python examples

The wheel ships a curated demo suite as `slikui.examples`. Every example is
standalone (copy one file into your app and it works), self-validating, and
drives the real event dispatch path.

```sh
python -m slikui.examples --list        # what's shipped
python -m slikui.examples --smoke all   # scripted self-check of every example
```

## The suite

| # | Example | Shows |
|---|---|---|
| 01 | `hello` | first frame, minimal tree, headless render ([screenshot](#see-them)) |
| 02 | `gallery` | widget survey — most built-in widgets in one tall page |
| 03 | `events` | event anatomy + `EventProbe`, a custom widget that narrates every event into a LogView |
| 04 | `paint` | drawing cookbook: Sparkline, RadialGauge, ClockFace, Meter, BoundsDebug |

## See them

The frames below are the examples' own `--smoke` output — post-interaction
frames captured by the same scripted pass CI runs:

![The hello smoke pass: the minimal first-frame tree, its Primary button
clicked by the synthetic press](../images/examples-hello.png)

![The gallery smoke pass: a dropdown popup open, a switch mid-toggle and the
slider scrubbed to 92 — one tall page of nearly every built-in widget](../images/examples-gallery.png)

![The events smoke pass: the EventProbe's log narrating hover, click, drag,
scroll and focus-routed keys, exactly as the dispatch rules deliver them](../images/examples-events.png)

![The paint smoke pass: meter scrubbed to 0.70, sparkline tracking the
pointer, the clock face advanced 0.5 s and the BoundsDebug overlay on](../images/examples-paint.png)

## Every example has the same four modes

```sh
python -m slikui.examples.paint              # headless render -> paint.png
python -m slikui.examples.paint --png out.png
python -m slikui.examples.paint --smoke      # scripted interaction pass, self-checking
python -m slikui.examples.paint --window     # live GLFW window (needs [demo] extra)
```

- **Default** — render one frame to `./NAME.png`; no display, no glfw.
- **`--smoke`** — synthetic input (click, drag, scroll, keys) against a fixed
  clock; asserts and prints a transcript. This is what CI runs, so the
  examples double as the wheel's acceptance test.
- **`--window`** — the same demo live: GLFW callbacks feed real events, the
  core renders into its framebuffer, and a thin premultiplied blit puts it on
  screen. Needs `pip install slikui-python[demo]`; the host is imported
  lazily, so the base wheel never pulls in glfw or PyOpenGL.

## EventProbe in depth

`events` deserves a closer look if you're wiring input — it demonstrates the
routing rules the core actually enforces (focus-routed keys and text, no
mouse capture, no dispatched key releases) and how to time a double-click
against the tick clock. It has its own section in
[Input & events](input-events.md#watching-events-the-shipped-eventprobe-demo).

## Custom widgets in depth

`paint` is the worked custom-widget example — measure/arrange/paint, direct
DrawList drawing, tick animation, and a debug overlay. See
[Custom widgets](custom-widgets.md) for the guide and
the screenshot-capture contract (`--docs-image`).
