# Containers

The layout primitives. Usage patterns and worked examples live in
[Layout](../guide/layout.md); this page is the API reference.

## Container

The shared base: padding/gap/cross-axis. All containers stack children along
their main axis, distribute extra main-axis space to children whose
`expands_main()` is true, and align the cross axis with `set_cross`.

| API | Signature |
|-----|-----------|
| `set_padding` | `(float all)` · `(float h, float v)` · `(Padding)` |
| `set_gap` | `(float px)` |
| `set_cross` | `(CrossMode m)` — `Stretch`, `Start`, `Center` |
| `add<T>(args...)` | construct + own a child, returns `T&` |

## Column / Row

`Column` stacks top-to-bottom, `Row` left-to-right. Children with
`stretch_cross() == true` (the default) stretch across the cross axis;
fixed-size controls like buttons and switches opt out and hug.

## Panel

A bordered, rounded container — a `Container` that paints a surface.

| API | Signature |
|-----|-----------|
| `set_background` | `(Color)` |
| `set_border` | `(Color)` |
| `set_radius` | `(float)` |

## Spacer / Divider

`Spacer` soaks up remaining main-axis space (its `expands_main()` is true).
`Divider` draws a rule between siblings. Neither takes children.

## Expander / Accordion

| API | Signature |
|-----|-----------|
| `Expander::set_title` | `(std::string)` |
| `Expander::set_open` | `(bool, bool fire_callback = false)` |
| `Expander::on_toggle` | `(std::function<void(bool)>)` |
| `Accordion::add_section` | `(title, open = false) -> Expander&` |
| `Accordion::set_exclusive` | `(bool)` — default `true` |

Closed `Expander` children get zero frames and skip hit-testing — collapsing
is free.

## Splitter

| API | Signature |
|-----|-----------|
| `Splitter(vertical)` | construct with `true` for stacked panes |
| `set_fraction` | `(float 0–1)` |
| `on_change` | `(std::function<void(float)>)` |
