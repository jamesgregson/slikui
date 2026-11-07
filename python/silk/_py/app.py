"""Headless app shell: Target + Sink + Context with conveniences.

`App` holds every owner (target, sink, context), so the C++ side never sees a
dangling backend even though the bindings are non-owning.
"""

from silk._py.builder import context_set_root
from silk._silk import Context, Sink, Target, find_system_font


class App:
    def __init__(self, width, height, *, font=None, scale=1.0, theme=None):
        self.width = int(width)
        self.height = int(height)
        self.scale = float(scale)
        # Logical frame is width x height; the framebuffer carries the scale.
        self.target = Target(int(self.width * self.scale), int(self.height * self.scale))
        self.sink = Sink(self.target)
        self.sink.set_scale(self.scale)
        self.ctx = Context(self.sink)
        self.ctx.set_frame_size(float(self.width), float(self.height))
        self.ctx.load_font_file(font or find_system_font())
        if theme is not None:
            self.ctx.theme = theme

    def set_root(self, cls, *args, **props):
        return context_set_root(self.ctx, cls, *args, **props)

    def resize(self, width, height):
        self.width, self.height = int(width), int(height)
        self.target.resize(int(self.width * self.scale), int(self.height * self.scale))
        self.ctx.set_frame_size(float(self.width), float(self.height))

    def tick(self, now=0.0, dt=0.0):
        self.ctx.tick(now, dt)

    def handle(self, event):
        self.ctx.handle(event)

    def render(self, clear=True):
        self.ctx.render(clear)
        return self.target.pixels()  # zero-copy (H, W) uint32, ABGR packing

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False
