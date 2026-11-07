"""silk — Python bindings for the silk C++ UI library.

Distribution `silk-ui`, import package `silk`, compiled module `silk._silk`.
Layering: `_silk` binds the C++ API 1:1; `silk/_py` adds a thin Pythonic
surface (kwargs → setters, App shell, pixel helpers). See
docs/python-bindings-plan.md.
"""

from silk._silk import (  # noqa: F401 — re-exported
    Accordion,
    Button,
    ButtonVariant,
    Checkbox,
    Color,
    Column,
    Container,
    Context,
    CrossMode,
    DrawCmd,
    DrawCmdType,
    DrawList,
    Divider,
    Dropdown,
    Expander,
    IRenderSink,
    Image,
    ImageFit,
    Label,
    ListView,
    Panel,
    ProgressBar,
    Radio,
    RenderSink,
    Row,
    ScrollArea,
    Sink,
    Slider,
    Spacer,
    Spinner,
    Swatch,
    Switch,
    Tabs,
    Target,
    TextArea,
    TexFormat,
    TextField,
    Theme,
    ThemePreset,
    Time,
    Vec2,
    Widget,
    apply_preset,
    find_system_font,
    theme_presets,
    use_theme,
)
from silk._py import builder
from silk._py.app import App
from silk._py.renderer import CanvasSink
from silk._py.events import (
    focus_lost,
    frame_resize,
    key_press,
    key_release,
    mods,
    mouse_click,
    mouse_move,
    mouse_press,
    mouse_release,
    scroll,
    text_input,
    text_input_str,
)
from silk._py.pixels import to_rgba
from silk._silk import (  # noqa: F401 — input enums
    EventType,
    Key,
    Mod,
    MouseButton,
    set_leak_warnings,
)

__version__ = "0.1.0"

# Pythonic sugar on the bound classes: `container.add(silk.Label, "hi")` and
# `ctx.set_root(silk.Column)`. nanobind classes are heap types, so type-level
# assignment is allowed; if a future build forbids it, use the module-level
# helpers `silk.add(container, ...)` / `silk.set_root(context, ...)`.
_PATCHED = builder.try_patch()

__all__ = [
    "Accordion", "App", "Button", "ButtonVariant", "Checkbox", "Color",
    "Column", "Container", "Context", "CrossMode", "Divider", "Dropdown",
    "Expander", "Image", "ImageFit", "Label", "ListView", "Panel",
    "ProgressBar", "Radio", "Row", "ScrollArea", "Sink", "Slider",
    "Spacer", "Spinner", "Swatch", "Switch", "Tabs", "Target", "TextArea",
    "TextField", "Theme", "ThemePreset", "Time", "Vec2", "Widget",
    "__version__", "add", "apply_preset", "find_system_font",
    "set_root", "theme_presets", "to_rgba", "use_theme",
    # events
    "EventType", "Key", "Mod", "MouseButton",
    "focus_lost", "frame_resize", "key_press", "key_release", "mods",
    "mouse_click", "mouse_move", "mouse_press", "mouse_release",
    "scroll", "text_input", "text_input_str",
]


def add(container, cls, *args, **props):
    """Fallback for `container.add(cls, *args, **props)` if type patching is off."""
    return builder.container_add(container, cls, *args, **props)


def set_root(context, cls, *args, **props):
    """Fallback for `context.set_root(cls, *args, **props)` if type patching is off."""
    return builder.context_set_root(context, cls, *args, **props)
