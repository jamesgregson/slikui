"""kwargs → setters, and the `add`/`set_root` factories.

No registry to keep in sync: a kwarg name `foo` resolves to the method
`set_foo`, or to `foo` itself when there is no setter (e.g. `on_click=`).
A wrong name is a loud AttributeError, not a silent no-op.
"""


def apply_props(widget, props):
    for name, value in props.items():
        fn = getattr(widget, f"set_{name}", None) or getattr(widget, name)
        fn(value)
    return widget


def container_add(self, cls, *args, **props):
    """self.add(silk.Label, "hi", font_size=26) → construct + apply kwargs."""
    fn = getattr(self, f"add_{cls.__name__.lower()}")
    return apply_props(fn(*args), props)


def context_set_root(self, cls, *args, **props):
    """ctx.set_root(silk.Column, padding=16) → construct + apply kwargs."""
    fn = getattr(self, f"set_root_{cls.__name__.lower()}")
    return apply_props(fn(*args), props)


def try_patch():
    """Attach `add`/`set_root` to the bound classes. True on success."""
    try:
        from silk._silk import Context, Widget

        Widget.add = container_add  # inherited by every widget, incl. ScrollArea
        Context.set_root = context_set_root
        return True
    except (AttributeError, TypeError):
        return False
