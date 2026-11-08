"""kwargs → setters, and the `add`/`set_root` factories.

No registry to keep in sync: a kwarg name `foo` resolves to the method
`set_foo`, or to `foo` itself when there is no setter (e.g. `on_click=`).
A wrong name is a loud AttributeError, not a silent no-op.
"""

from slikui._slikui import Widget


def _is_bound(cls):
    """True when `cls` is the compiled extension's own class, not a subclass."""
    from slikui import _slikui as ext

    return getattr(ext, cls.__name__, None) is cls


def apply_props(widget, props):
    for name, value in props.items():
        fn = getattr(widget, f"set_{name}", None) or getattr(widget, name)
        fn(value)
    return widget


def container_add(self, cls, *args, **props):
    """self.add(slikui.Label, "hi", font_size=26) → construct + apply kwargs.

    Python-defined widget subclasses (subclassing slikui.Widget) take the
    add_widget() path instead — that is the trampoline seam.
    """
    fn = getattr(self, f"add_{cls.__name__.lower()}", None)
    if fn is not None and _is_bound(cls):
        return apply_props(fn(*args), props)
    if isinstance(cls, type) and issubclass(cls, Widget):
        return apply_props(self.add_widget(cls(*args)), props)
    raise TypeError(f"{cls!r} is not a slikui widget or a custom-widget subclass")


def context_set_root(self, cls, *args, **props):
    """ctx.set_root(slikui.Column, padding=16) → construct + apply kwargs."""
    fn = getattr(self, f"set_root_{cls.__name__.lower()}", None)
    if fn is not None and _is_bound(cls):
        return apply_props(fn(*args), props)
    if isinstance(cls, type) and issubclass(cls, Widget):
        return apply_props(self.set_root_widget(cls(*args)), props)
    raise TypeError(f"{cls!r} is not a slikui widget or a custom-widget subclass")


def try_patch():
    """Attach `add`/`set_root` to the bound classes. True on success."""
    try:
        from slikui._slikui import Context, Widget

        Widget.add = container_add  # inherited by every widget, incl. ScrollArea
        Context.set_root = context_set_root
        return True
    except (AttributeError, TypeError):
        return False
