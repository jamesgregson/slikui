"""kwargs -> setters, and the `add`/`set_root` factories.

No registry to keep in sync: a kwarg name `foo` resolves to the method
`set_foo`, or to `foo` itself when there is no setter (e.g. `on_click=`).
A wrong name is a loud AttributeError, not a silent no-op.

Custom widget classes (Python subclasses of slikui.Widget) also take
constructor keywords: a kwarg named in the class's ``__init__`` signature goes
to the constructor; the rest go to setters as above.
"""

import inspect

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


def _split_ctor_kwargs(cls, props):
    """(constructor kwargs, setter kwargs) for a custom widget class.

    Names in ``cls.__init__``'s signature go to the constructor. With a
    ``**kwargs`` catch-all, names that don't resolve to a setter/attribute
    go there too (so typos still fail loudly somewhere).
    """
    if not props:
        return {}, props
    try:
        params = list(inspect.signature(cls.__init__).parameters.values())[1:]
    except (TypeError, ValueError):  # the bound base __init__: no signature
        return {}, props
    named = {p.name for p in params
             if p.kind in (p.POSITIONAL_OR_KEYWORD, p.KEYWORD_ONLY)}
    var_kw = any(p.kind == p.VAR_KEYWORD for p in params)
    ctor, setters = {}, {}
    for name, value in props.items():
        settable = hasattr(cls, f"set_{name}") or hasattr(cls, name)
        if name in named or (var_kw and not settable):
            ctor[name] = value
        else:
            setters[name] = value
    return ctor, setters


def _make_custom(cls, args, props):
    ctor, setters = _split_ctor_kwargs(cls, props)
    return cls(*args, **ctor), setters


def container_add(self, cls, *args, **props):
    """self.add(slikui.Label, "hi", font_size=26) -> construct + apply kwargs.

    Python-defined widget subclasses (subclassing slikui.Widget) take the
    add_widget() path instead - that is the trampoline seam - and receive
    the kwargs their ``__init__`` names: ``self.add(Knob, value=3, tooltip="t")``
    calls ``Knob(value=3)`` then ``set_tooltip("t")``.
    """
    fn = getattr(self, f"add_{cls.__name__.lower()}", None)
    if fn is not None and _is_bound(cls):
        return apply_props(fn(*args), props)
    if isinstance(cls, type) and issubclass(cls, Widget):
        widget, setters = _make_custom(cls, args, props)
        return apply_props(self.add_widget(widget), setters)
    raise TypeError(f"{cls!r} is not a slikui widget or a custom-widget subclass")


def context_set_root(self, cls, *args, **props):
    """ctx.set_root(slikui.Column, padding=16) -> construct + apply kwargs."""
    fn = getattr(self, f"set_root_{cls.__name__.lower()}", None)
    if fn is not None and _is_bound(cls):
        return apply_props(fn(*args), props)
    if isinstance(cls, type) and issubclass(cls, Widget):
        widget, setters = _make_custom(cls, args, props)
        return apply_props(self.set_root_widget(widget), setters)
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
