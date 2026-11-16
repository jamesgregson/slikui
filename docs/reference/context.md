# Context

`slikui::Context` owns the tree, routes input, lays out, animates and paints.
One context per thread; drive it with
[handle -> tick -> render](../guide/core-concepts.md#the-frame-loop).

## Tree

| API | Notes |
|-----|-------|
| `set_root<T>(args...) -> T&` | construct + own the root |
| `set_root(std::unique_ptr<Widget>)` | take ownership of a pre-built tree |
| `root()` | current root or `nullptr` |
| `find(id)` | search the tree for `Widget::id` |
| `clear_tree()` | destroy the tree now (releases callbacks) |

## Frame

| API | Notes |
|-----|-------|
| `handle(const Event&)` | route one event; popups close at the end, never mid-dispatch |
| `tick(Time{now, dt})` | attach -> measure -> arrange -> animation |
| `render(bool clear = true)` | paint the tree + popups + shells into the sink |
| `set_frame_size(Vec2)` | logical frame size (backends derive it from resize events) |

## Popups

Owned by the context; destroyed at the end of the event that closes them.

| API | Notes |
|-----|-------|
| `push_popup(widget, pos)` | unique_ptr or borrowed reference overload |
| `push_popup(widget, anchor, PopupDir)` | anchored popups (`Below`, `Right`, ...) |
| `open_context_menu(widget, pos)` | open a `Menu` as a floating popup |
| `close_popups()` / `close_topmost_popup()` / `close_popups_above(widget)` | dismissal |
| `has_popups()` / `popup_count()` / `popup(i)` / `popup_open(w)` | inspection |

## Dialogs, toasts

The shell layer above popups - see
[Shells & dialogs](widgets-shells.md).

| API | Notes |
|-----|-------|
| `open_dialog(unique_ptr<D>)` | owned dialog |
| `open_dialog(D&)` | **borrowed** - keep the Python object alive |
| `close_dialog(widget)` | explicit close |
| `dialog_count()` / `dialog(i)` | inspection |
| `push_toast(widget, seconds)` | auto-dismissing corner notice; returns the widget |
| `toast_count()` / `toast(i)` | inspection |

Toasts are their own passive layer (above popups, below dialogs): never
hit-tested, so clicks pass through them, and they neither close nor are
closed by the popup stack. Several toasts stack upward from the
bottom-right corner, newest lowest.

## Focus, capture, shortcuts

| API | Notes |
|-----|-------|
| `focused()` / `set_focus(widget*)` | key/text routing target |
| `capture_mouse(widget*)` / `release_mouse(widget*)` | receive every move/release while dragging |
| `add_shortcut(Key, mods, fn)` | global accelerator; fires before widget dispatch, modifier required |
| `clear_shortcuts()` | |

## Theme, font, clipboard, cursor

| API | Notes |
|-----|-------|
| `theme() -> Theme&` | mutate freely; widgets read it every paint ([Theming](../guide/theming.md)) |
| `load_font_file(path)` / `load_font_memory(data, len)` | the body font ([Text & fonts](../guide/text-fonts.md)) |
| `add_font(family, path, style, face)` / `add_font_memory(...)` / `has_font(family)` | more faces by family name: `"heading"`, `"mono"`, your own |
| `text_size(s, opts)` / `text_metrics(opts)` | measure text for `measure()` ([Drawing text](../guide/custom-widgets.md#drawing-text)) |
| `text_layout(s, wrap_width, opts) -> TextBlock` | a kept layout: draw, hit-test, caret, selection |
| `set_clipboard_source(fn)` / `set_clipboard_sink(fn)` | host clipboard for text fields |
| `clipboard_text()` / `set_clipboard_text(str)` | direct clipboard access |
| `set_cursor_host(fn)` | map `set_cursor` requests to system cursors |
| `next_texture_id()` | ids for custom `TexUpload`s - never reused for different content |

Tooltips need no context API: any widget's `set_tooltip` makes the context
show a popup after a ~0.6 s hover.
