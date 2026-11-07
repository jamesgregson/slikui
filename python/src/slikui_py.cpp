// slikui/_slikui — nanobind bindings, M0 slice.
//
// Bound here: values (Color, Vec2, Time), the software backend
// (Target — owning or borrowed-from-numpy — and Sink), Context, and the first
// widgets (Widget/Container/Column/Row/Spacer/Label/Button).
//
// Binding rules (see docs/python-bindings-plan.md):
//   * C++ fluent setters return a base-class reference (no deducing-this).
//     Each setter is re-bound as a method returning `self` with
//     rv_policy::reference, so Python chains work and identity is stable —
//     nanobind re-attaches the existing wrapper for an object it wrapped.
//   * The tree owns children (unique_ptr in C++); Python holds non-owning
//     views. Factory methods (`add_*`, `set_root_*`) return
//     reference_internal so the owner keeps the view alive.
//   * Every call holds the GIL; the software rasterizer is milliseconds-fast.

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/trampoline.h>
#include <nanobind/stl/function.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/vector.h>

#include <slikui/slikui.hpp>
#include <slikui/backends/software/software.hpp>
#include <slikui/text/system_fonts.hpp>
#include <slikui/version.hpp>

#include <cstdint>
#include <functional>
#include <utility>

namespace nb = nanobind;
using namespace slikui;

namespace {

// Bind one C++ setter `W& W::f(V)` under `name`, returning the same wrapper.
template <typename W, typename... Bs, typename V>
void def_setter(nb::class_<W, Bs...>& cls, const char* name, W& (W::*f)(V)) {
    cls.def(name, [f](W& w, V v) -> W& { return (w.*f)(std::move(v)); },
            nb::rv_policy::reference, nb::arg("value"));
}

// Swatch — a small clickable color chip that highlights when its
// `is_selected` callback says so. Python can't subclass C++ widgets without a
// trampoline, so the demo's custom widget lives here and drives Python
// callbacks for both directions (state read + edit).
class Swatch : public slikui::Widget {
public:
    Swatch(slikui::Color c, nb::callable is_selected, nb::callable on_pick)
        : Widget(""), color_(c), is_selected_(std::move(is_selected)),
          on_pick_(std::move(on_pick)) {}

    slikui::Size measure(const slikui::Constraints& c) override { return c.clamp({22.f, 22.f}); }
    bool stretch_cross() const override { return false; }

    void paint(const slikui::PaintEnv& env) const override {
        nb::gil_scoped_acquire gil;
        bool sel = false;
        try {
            sel = nb::cast<bool>(is_selected_());
        } catch (const std::exception&) {
        }
        env.list.rect(bounds, color_, 4.f);
        env.list.hollow_rect(bounds, env.theme.border, 1, 4.f);
        if (sel) env.list.hollow_rect(bounds.inflate(2, 2), env.theme.accent, 2, 6.f);
    }

    bool on_event(const slikui::Event& e) override {
        if (e.type == slikui::Event::Type::MousePress && enabled && bounds.contains(e.pos)) {
            nb::gil_scoped_acquire gil;
            try {
                on_pick_();
            } catch (const std::exception&) {
            }
            return true;
        }
        return false;
    }

    const char* widget_type() const override { return "Swatch"; }

private:
    slikui::Color color_;
    nb::callable is_selected_;
    nb::callable on_pick_;
};

// `host.add_<Name>(ctor args)` factories, bound on Widget so every widget can
// host children. ScrollArea re-binds the whole set: its C++ add<T> routes
// children into the scrolling content column instead of direct children.
template <typename Host, typename... Bs>
void bind_add(nb::class_<Host, Bs...>& cls) {
    cls.def("add_column", [](Host& h) -> Column& { return h.template add<Column>(); },
            nb::rv_policy::reference_internal)
        .def("add_row", [](Host& h) -> Row& { return h.template add<Row>(); },
             nb::rv_policy::reference_internal)
        .def("add_spacer", [](Host& h) -> Spacer& { return h.template add<Spacer>(); },
             nb::rv_policy::reference_internal)
        .def("add_divider",
             [](Host& h, bool horizontal) -> Divider& {
                 return h.template add<Divider>(horizontal);
             },
             nb::arg("horizontal") = true, nb::rv_policy::reference_internal)
        .def("add_panel", [](Host& h) -> Panel& { return h.template add<Panel>(); },
             nb::rv_policy::reference_internal)
        .def("add_expander",
             [](Host& h, std::string title) -> Expander& {
                 return h.template add<Expander>(std::move(title));
             },
             nb::arg("title") = "", nb::rv_policy::reference_internal)
        .def("add_accordion", [](Host& h) -> Accordion& { return h.template add<Accordion>(); },
             nb::rv_policy::reference_internal)
        .def("add_label",
             [](Host& h, std::string text) -> Label& {
                 return h.template add<Label>(std::move(text));
             },
             nb::arg("text") = "", nb::rv_policy::reference_internal)
        .def("add_button",
             [](Host& h, std::string text) -> Button& {
                 return h.template add<Button>(std::move(text));
             },
             nb::arg("text") = "", nb::rv_policy::reference_internal)
        .def("add_checkbox",
             [](Host& h, std::string label, bool checked) -> Checkbox& {
                 return h.template add<Checkbox>(std::move(label), checked);
             },
             nb::arg("label") = "", nb::arg("checked") = false,
             nb::rv_policy::reference_internal)
        .def("add_radio",
             [](Host& h, std::string label, bool checked) -> Radio& {
                 return h.template add<Radio>(std::move(label), checked);
             },
             nb::arg("label") = "", nb::arg("checked") = false,
             nb::rv_policy::reference_internal)
        .def("add_switch", [](Host& h) -> Switch& { return h.template add<Switch>(); },
             nb::rv_policy::reference_internal)
        .def("add_swatch",
             [](Host& h, Color color, nb::callable is_selected, nb::callable on_pick) -> Swatch& {
                 return h.template add<Swatch>(color, std::move(is_selected),
                                               std::move(on_pick));
             },
             nb::arg("color"), nb::arg("is_selected"), nb::arg("on_pick"),
             nb::rv_policy::reference_internal)
        .def("add_slider",
             [](Host& h, float min, float max) -> Slider& {
                 return h.template add<Slider>(min, max);
             },
             nb::arg("min") = 0.f, nb::arg("max") = 1.f,
             nb::rv_policy::reference_internal)
        .def("add_progressbar",
             [](Host& h) -> ProgressBar& { return h.template add<ProgressBar>(); },
             nb::rv_policy::reference_internal)
        .def("add_spinner", [](Host& h) -> Spinner& { return h.template add<Spinner>(); },
             nb::rv_policy::reference_internal)
        .def("add_textfield",
             [](Host& h) -> TextField& { return h.template add<TextField>(); },
             nb::rv_policy::reference_internal)
        .def("add_textarea",
             [](Host& h) -> TextArea& { return h.template add<TextArea>(); },
             nb::rv_policy::reference_internal)
        .def("add_scrollarea",
             [](Host& h) -> ScrollArea& { return h.template add<ScrollArea>(); },
             nb::rv_policy::reference_internal)
        .def("add_listview",
             [](Host& h) -> ListView& { return h.template add<ListView>(); },
             nb::rv_policy::reference_internal)
        .def("add_tabs", [](Host& h) -> Tabs& { return h.template add<Tabs>(); },
             nb::rv_policy::reference_internal)
        .def("add_dropdown",
             [](Host& h) -> Dropdown& { return h.template add<Dropdown>(); },
             nb::rv_policy::reference_internal)
        .def("add_image",
             [](Host& h, std::string name) -> Image& {
                 return h.template add<Image>(std::move(name));
             },
             nb::arg("name"), nb::rv_policy::reference_internal);
}

// PyRenderSink — trampoline so Python classes can implement the
// IRenderSink contract (upload()/render()). This is what makes a pure-Python
// backend possible: slikui pushes a DrawList, Python decides where pixels go.
struct PyRenderSink : IRenderSink {
    NB_TRAMPOLINE(IRenderSink);

    void upload(const TexUpload& up) override { NB_OVERRIDE_PURE(upload, up); }

    void render(const DrawList& list, Vec2 framebuffer_size) override {
        NB_OVERRIDE_PURE(render, list, framebuffer_size);
    }
};

} // namespace

NB_MODULE(_slikui, m) {
    m.doc() = "slikui Python bindings — thin layer over the C++20 UI library.";
    m.attr("__version__") = SLIKUI_VERSION_STRING;
    // GUI apps legitimately hold widget wrappers until process exit; the
    // shutdown-order "leak" report is noise there. Opt-out for apps (tests
    // keep the checker on).
    m.def("set_leak_warnings", &nb::set_leak_warnings, nb::arg("enabled"));

    // ------------------------------------------------------------------ //
    // Values
    // ------------------------------------------------------------------ //

    nb::class_<Vec2>(m, "Vec2")
        .def(nb::init<float, float>(), nb::arg("x") = 0.f, nb::arg("y") = 0.f)
        .def_rw("x", &Vec2::x)
        .def_rw("y", &Vec2::y)
        .def("__eq__", [](const Vec2& a, const Vec2& b) { return a == b; },
             nb::arg("other"), nb::is_operator());

    nb::class_<Color>(m, "Color")
        .def(nb::init<std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t>(),
             nb::arg("r") = 0, nb::arg("g") = 0, nb::arg("b") = 0, nb::arg("a") = 255)
        .def_rw("r", &Color::r)
        .def_rw("g", &Color::g)
        .def_rw("b", &Color::b)
        .def_rw("a", &Color::a)
        .def_static("from_hex",
                    [](std::uint32_t rgb, std::uint8_t alpha) { return Color::from_hex(rgb, alpha); },
                    nb::arg("rgb"), nb::arg("alpha") = 255)
        .def("with_alpha", &Color::with_alpha, nb::arg("alpha"))
        .def("faded", &Color::faded, nb::arg("f"))
        .def("lerp", &Color::lerp, nb::arg("other"), nb::arg("t"))
        .def("to_abgr", &Color::to_abgr)
        .def_static("black", &Color::black)
        .def_static("white", &Color::white)
        .def("__eq__", [](const Color& a, const Color& b) { return a == b; },
             nb::arg("other"), nb::is_operator());

    nb::class_<Time>(m, "Time")
        .def("__init__",
             [](Time* t, double now, double dt) { new (t) Time{now, dt}; },
             nb::arg("now") = 0.0, nb::arg("dt") = 0.0)
        .def_rw("now", &Time::now)
        .def_rw("dt", &Time::dt);

    nb::enum_<CrossMode>(m, "CrossMode")
        .value("Stretch", CrossMode::Stretch)
        .value("Center", CrossMode::Center)
        .value("Start", CrossMode::Start);

    nb::class_<Rect>(m, "Rect")
        .def(nb::init<float, float, float, float>(), nb::arg("x") = 0.f,
             nb::arg("y") = 0.f, nb::arg("w") = 0.f, nb::arg("h") = 0.f)
        .def_rw("x", &Rect::x)
        .def_rw("y", &Rect::y)
        .def_rw("w", &Rect::w)
        .def_rw("h", &Rect::h)
        .def("contains", &Rect::contains, nb::arg("p"))
        .def("__eq__", [](const Rect& a, const Rect& b) { return a == b; },
             nb::arg("other"), nb::is_operator());

    // ------------------------------------------------------------------ //
    // Input events (one vocabulary, translated by the window layer)
    // ------------------------------------------------------------------ //

    nb::enum_<Key>(m, "Key")
        .value("Unknown", Key::Unknown)
        .value("Enter", Key::Enter)
        .value("Tab", Key::Tab)
        .value("Backspace", Key::Backspace)
        .value("Delete", Key::Delete)
        .value("Escape", Key::Escape)
        .value("Space", Key::Space)
        .value("Apostrophe", Key::Apostrophe)
        .value("Comma", Key::Comma)
        .value("Minus", Key::Minus)
        .value("Period", Key::Period)
        .value("Slash", Key::Slash)
        .value("Left", Key::Left)
        .value("Right", Key::Right)
        .value("Up", Key::Up)
        .value("Down", Key::Down)
        .value("Home", Key::Home)
        .value("End", Key::End)
        .value("PageUp", Key::PageUp)
        .value("PageDown", Key::PageDown)
        .value("Insert", Key::Insert)
        .value("CapsLock", Key::CapsLock)
        .value("F1", Key::F1)
        .value("F2", Key::F2)
        .value("F3", Key::F3)
        .value("F4", Key::F4)
        .value("F5", Key::F5)
        .value("F6", Key::F6)
        .value("F7", Key::F7)
        .value("F8", Key::F8)
        .value("F9", Key::F9)
        .value("F10", Key::F10)
        .value("F11", Key::F11)
        .value("F12", Key::F12);

    nb::enum_<Mod>(m, "Mod", nb::is_arithmetic())
        .value("ModNone", ModNone)
        .value("ModShift", ModShift)
        .value("ModCtrl", ModCtrl)
        .value("ModAlt", ModAlt)
        .value("ModSuper", ModSuper);

    nb::enum_<MouseButton>(m, "MouseButton", nb::is_arithmetic())
        .value("MouseLeft", MouseLeft)
        .value("MouseMiddle", MouseMiddle)
        .value("MouseRight", MouseRight);

    nb::enum_<Event::Type>(m, "EventType")
        .value("MouseMove", Event::Type::MouseMove)
        .value("MousePress", Event::Type::MousePress)
        .value("MouseRelease", Event::Type::MouseRelease)
        .value("Scroll", Event::Type::Scroll)
        .value("KeyPress", Event::Type::KeyPress)
        .value("KeyRelease", Event::Type::KeyRelease)
        .value("TextInput", Event::Type::TextInput)
        .value("FocusLost", Event::Type::FocusLost)
        .value("FrameResize", Event::Type::FrameResize);

    nb::class_<Event>(m, "Event")
        .def("__init__",
             [](Event* e, Event::Type type) { new (e) Event{}; e->type = type; },
             nb::arg("type"))
        .def_rw("type", &Event::type)
        .def_rw("pos", &Event::pos)
        .def_rw("delta", &Event::delta)
        .def_rw("scroll", &Event::scroll)
        .def_rw("button", &Event::button)
        .def_rw("key", &Event::key)
        .def_rw("codepoint", &Event::codepoint)
        .def_rw("mods", &Event::mods)
        .def_prop_ro("shift", &Event::shift)
        .def_prop_ro("ctrl", &Event::ctrl)
        .def_prop_ro("alt", &Event::alt);

    // ------------------------------------------------------------------ //
    // Render contract: TexUpload, DrawCmd, DrawList, and the RenderSink
    // trampoline. A Python class subclassing RenderSink receives every
    // frame slikui produces — this is the "your renderer" seam.
    // ------------------------------------------------------------------ //

    nb::enum_<TexFormat>(m, "TexFormat")
        .value("Alpha8", TexFormat::Alpha8)
        .value("Rgba8", TexFormat::Rgba8);

    // TexUpload is passed to a sink's upload() override; it borrows the pixel
    // buffer, which stays valid for the duration of the call only.
    nb::class_<TexUpload>(m, "TexUpload")
        .def_prop_ro("id", [](const TexUpload& t) { return t.id; })
        .def_prop_ro("width", [](const TexUpload& t) { return t.width; })
        .def_prop_ro("height", [](const TexUpload& t) { return t.height; })
        .def_prop_ro("format", [](const TexUpload& t) { return t.format; })
        .def_prop_ro("pixels",
                     [](const TexUpload& t) {
                         return nb::bytes((const char*)t.pixels,
                                          std::size_t(t.width) * std::size_t(t.height) *
                                              (t.format == TexFormat::Rgba8 ? 4 : 1));
                     });

    nb::enum_<DrawCmd::Type>(m, "DrawCmdType")
        .value("Rect", DrawCmd::Type::Rect)
        .value("Line", DrawCmd::Type::Line)
        .value("Glyph", DrawCmd::Type::Glyph)
        .value("Image", DrawCmd::Type::Image)
        .value("ClipPush", DrawCmd::Type::ClipPush)
        .value("ClipPop", DrawCmd::Type::ClipPop);

    nb::class_<DrawCmd>(m, "DrawCmd") // read-only: produced by slikui, consumed by sinks
        .def(nb::init<>())
        .def_ro("type", &DrawCmd::type)
        .def_ro("rect", &DrawCmd::rect)
        .def_ro("radius", &DrawCmd::radius)
        .def_ro("thickness", &DrawCmd::thickness)
        .def_ro("hollow", &DrawCmd::hollow)
        .def_ro("color", &DrawCmd::color)
        .def_ro("tex_id", &DrawCmd::tex_id)
        .def_ro("uv", &DrawCmd::uv);

    nb::class_<DrawList>(m, "DrawList")
        .def_prop_ro("commands", [](const DrawList& l) { return l.commands(); })
        .def_prop_ro("size", [](const DrawList& l) { return l.size(); })
        .def("__len__", [](const DrawList& l) { return l.size(); })
        .def("__getitem__",
             [](const DrawList& l, std::size_t i) { return l.commands().at(i); })
        .def("__iter__",
             [](const DrawList& l) { return nb::cast(l.commands()).attr("__iter__")(); });

    nb::class_<IRenderSink>(m, "IRenderSink"); // abstract base, never constructed directly

    nb::class_<PyRenderSink, IRenderSink>(m, "RenderSink")
        .def(nb::init<>());

    // ------------------------------------------------------------------ //
    // Software backend (CPU rasterizer)
    // ------------------------------------------------------------------ //

    nb::class_<software::Target>(m, "Target")
        .def(nb::init<int, int>(), nb::arg("width"), nb::arg("height"))
        // Borrowed framebuffer: a (H, W) uint32 numpy array IS the framebuffer.
        .def("__init__",
             [](software::Target* t, nb::ndarray<nb::numpy, std::uint32_t, nb::ndim<2>> arr) {
                 if (arr.stride(1) != 1 || arr.stride(0) != arr.shape(1))
                     throw nb::value_error("pixels must be a C-contiguous (H, W) uint32 array");
                 new (t) software::Target(const_cast<std::uint32_t*>(arr.data()),
                                          static_cast<int>(arr.shape(1)),
                                          static_cast<int>(arr.shape(0)));
             },
             nb::arg("pixels"), nb::keep_alive<0, 1>())
        .def_prop_ro("width", &software::Target::width)
        .def_prop_ro("height", &software::Target::height)
        // Owned mode only: re-allocates the framebuffer (zero-initialized,
        // so a transparent UI layer can render with clear=False).
        .def("resize", &software::Target::resize, nb::arg("width"), nb::arg("height"))
        .def("clear", &software::Target::clear, nb::arg("color"))
        // Zero-copy view of the framebuffer, (H, W) uint32; keeps the target alive.
        .def("pixels",
             [](nb::handle self) -> nb::ndarray<nb::numpy, std::uint32_t, nb::ndim<2>> {
                 auto* t = nb::cast<software::Target*>(self);
                 std::size_t shape[2] = {static_cast<std::size_t>(t->height()),
                                         static_cast<std::size_t>(t->width())};
                 return nb::ndarray<nb::numpy, std::uint32_t, nb::ndim<2>>(t->data(), 2, shape,
                                                                           self);
             },
             nb::rv_policy::reference_internal);

    nb::class_<software::Sink, IRenderSink>(m, "Sink")
        .def(nb::init<software::Target&>(), nb::arg("target"), nb::keep_alive<0, 1>())
        .def("set_target", &software::Sink::set_target, nb::arg("target"),
             nb::keep_alive<0, 1>())
        .def("set_scale", &software::Sink::set_scale, nb::arg("scale"))
        .def_prop_ro("scale", &software::Sink::scale);

    // ------------------------------------------------------------------ //
    // Context
    // ------------------------------------------------------------------ //

    nb::class_<Context>(m, "Context")
        .def(nb::init<IRenderSink&>(), nb::arg("sink"), nb::keep_alive<0, 1>())
        .def("set_frame_size",
             [](Context& c, float w, float h) { c.set_frame_size({w, h}); },
             nb::arg("width"), nb::arg("height"))
        .def_prop_ro("frame_size", [](Context& c) { return c.frame_size(); })
        .def("tick", [](Context& c, Time t) { c.tick(t); }, nb::arg("time"))
        .def("tick",
             [](Context& c, double now, double dt) { c.tick(Time{now, dt}); },
             nb::arg("now") = 0.0, nb::arg("dt") = 0.0)
        .def("render", &Context::render, nb::arg("clear") = true)
        .def("load_font_file", &Context::load_font_file, nb::arg("path"))
        .def("set_root_column", [](Context& c) -> Column& { return c.set_root<Column>(); },
             nb::rv_policy::reference_internal)
        .def("set_root_row", [](Context& c) -> Row& { return c.set_root<Row>(); },
             nb::rv_policy::reference_internal)
        .def_prop_rw("theme",
                     [](Context& c) -> Theme& { return c.theme(); },
                     [](Context& c, const Theme& t) { c.theme() = t; },
                     nb::rv_policy::reference_internal)
        .def("next_texture_id", &Context::next_texture_id)
        .def("find", [](Context& c, const std::string& widget_id) -> Widget* {
                 return c.find(widget_id);
             },
             nb::arg("id"), nb::rv_policy::reference)
        .def("handle", &Context::handle, nb::arg("event"))
        .def("close_popups", &Context::close_popups)
        .def("clear_tree", &Context::clear_tree)
        .def_prop_ro("has_popups", &Context::has_popups)
        .def_prop_ro("popup_count", &Context::popup_count)
        .def("popup",
             [](Context& c, std::size_t i) -> Widget* {
                 return i < c.popup_count() ? c.popup(i) : nullptr;
             },
             nb::arg("index"), nb::rv_policy::reference);

    // ------------------------------------------------------------------ //
    // Widgets
    // ------------------------------------------------------------------ //

    auto widget_cls = nb::class_<Widget>(m, "Widget")
        .def_rw("id", &Widget::id)
        .def_rw("visible", &Widget::visible)
        .def_rw("enabled", &Widget::enabled)
        .def_prop_ro("type", [](Widget& w) { return std::string(w.widget_type()); })
        .def_prop_ro("tooltip", [](Widget& w) { return w.tooltip(); })
        .def_prop_ro("bounds", [](Widget& w) { return w.bounds; })
        .def_prop_ro("children",
                     [](Widget& w) {
                         nb::list out;
                         for (const auto& c : w.children) out.append(c.get());
                         return out;
                     },
                     nb::rv_policy::reference) // non-owning: the tree owns them
        .def("find",
             [](Widget& w, const std::string& widget_id) -> Widget* { return w.find(widget_id); },
             nb::arg("id"), nb::rv_policy::reference)
        .def("set_tooltip",
             [](Widget& w, std::string t) -> Widget& { return w.set_tooltip(std::move(t)); },
             nb::arg("text"), nb::rv_policy::reference);
    bind_add(widget_cls);

    nb::class_<Container, Widget>(m, "Container")
        .def("set_padding",
             [](Container& c, float all) -> Container& { return c.set_padding(all); },
             nb::arg("all"), nb::rv_policy::reference)
        .def("set_gap",
             [](Container& c, float g) -> Container& { return c.set_gap(g); },
             nb::arg("value"), nb::rv_policy::reference)
        .def("set_cross",
             [](Container& c, CrossMode mode) -> Container& { return c.set_cross(mode); },
             nb::arg("value"), nb::rv_policy::reference);

    auto column_cls = nb::class_<Column, Container>(m, "Column");
    auto row_cls = nb::class_<Row, Container>(m, "Row");
    auto spacer_cls = nb::class_<Spacer, Widget>(m, "Spacer");
    (void)column_cls; (void)row_cls; (void)spacer_cls;

    auto panel_cls = nb::class_<Panel, Container>(m, "Panel");
    def_setter(panel_cls, "set_background", &Panel::set_background);
    def_setter(panel_cls, "set_border", &Panel::set_border);
    def_setter(panel_cls, "set_radius", &Panel::set_radius);
    panel_cls.def_rw("draw_border", &Panel::draw_border_);

    auto divider_cls = nb::class_<Divider, Widget>(m, "Divider");
    (void)divider_cls;

    nb::class_<Swatch, Widget>(m, "Swatch")
        .def("__init__",
             [](Swatch* s, Color color, nb::callable is_selected, nb::callable on_pick) {
                 new (s) Swatch(color, std::move(is_selected), std::move(on_pick));
             },
             nb::arg("color"), nb::arg("is_selected"), nb::arg("on_pick"));

    nb::enum_<Button::Variant>(m, "ButtonVariant")
        .value("Default", Button::Variant::Default)
        .value("Primary", Button::Variant::Primary)
        .value("Danger", Button::Variant::Danger)
        .value("Ghost", Button::Variant::Ghost);

    auto label = nb::class_<Label, Widget>(m, "Label");
    def_setter(label, "set_text", &Label::set_text);
    def_setter(label, "set_font_size", &Label::set_font_size);
    def_setter(label, "set_color", &Label::set_color);
    def_setter(label, "set_wrap_width", &Label::set_wrap_width);

    auto button = nb::class_<Button, Widget>(m, "Button");
    def_setter(button, "set_text", &Button::set_text);
    def_setter(button, "set_variant", &Button::set_variant);
    button.def("set_padding",
               [](Button& b, float h, float v) -> Button& { return b.set_padding(h, v); },
               nb::arg("h"), nb::arg("v"), nb::rv_policy::reference)
           .def("on_click",
                [](Button& b, std::function<void()> f) -> Button& {
                    return b.on_click(std::move(f)); // holds the callable alive
                },
                nb::arg("callback"), nb::rv_policy::reference);

    auto checkbox = nb::class_<Checkbox, Widget>(m, "Checkbox");
    def_setter(checkbox, "on_change", &Checkbox::on_change);
    checkbox.def("set_checked",
                 [](Checkbox& c, bool v, bool fire) -> Checkbox& {
                     return c.set_checked(v, fire);
                 },
                 nb::arg("value"), nb::arg("fire_callback") = false,
                 nb::rv_policy::reference)
             .def_prop_ro("checked", &Checkbox::checked);

    auto radio = nb::class_<Radio, Widget>(m, "Radio");
    def_setter(radio, "on_change", &Radio::on_change);
    radio.def("set_checked",
             [](Radio& r, bool v, bool fire) -> Radio& {
                 return r.set_checked(v, fire);
             },
             nb::arg("value"), nb::arg("fire_callback") = false,
             nb::rv_policy::reference)
         .def_prop_ro("checked", &Radio::checked);

    auto swtch = nb::class_<Switch, Widget>(m, "Switch");
    def_setter(swtch, "on_change", &Switch::on_change);
    swtch.def("set_checked",
              [](Switch& s, bool v, bool fire) -> Switch& {
                  return s.set_checked(v, fire);
              },
              nb::arg("value"), nb::arg("fire_callback") = false,
              nb::rv_policy::reference)
           .def_prop_ro("checked", &Switch::checked);

    auto slider = nb::class_<Slider, Widget>(m, "Slider");
    def_setter(slider, "on_change", &Slider::on_change);
    slider.def("set_range",
               [](Slider& s, float min, float max) -> Slider& {
                   return s.set_range(min, max);
               },
               nb::arg("min"), nb::arg("max"), nb::rv_policy::reference)
           .def("set_value",
                [](Slider& s, float v, bool fire) -> Slider& {
                    return s.set_value(v, fire);
                },
                nb::arg("value"), nb::arg("fire_callback") = true,
                nb::rv_policy::reference);

    auto progress = nb::class_<ProgressBar, Widget>(m, "ProgressBar");
    def_setter(progress, "set_value", &ProgressBar::set_value);

    auto spinner = nb::class_<Spinner, Widget>(m, "Spinner");
    def_setter(spinner, "set_speed", &Spinner::set_speed);

    auto textfield = nb::class_<TextField, Widget>(m, "TextField");
    def_setter(textfield, "set_text", &TextField::set_text);
    textfield.def_prop_rw("text",
                          [](TextField& t) { return t.text(); },
                          [](TextField& t, std::string s) { t.set_text(std::move(s)); });
    def_setter(textfield, "set_placeholder", &TextField::set_placeholder);
    def_setter(textfield, "set_password", &TextField::set_password);
    def_setter(textfield, "on_change", &TextField::on_change);
    def_setter(textfield, "on_submit", &TextField::on_submit);

    auto textarea = nb::class_<TextArea, Widget>(m, "TextArea");
    def_setter(textarea, "set_text", &TextArea::set_text);
    def_setter(textarea, "set_placeholder", &TextArea::set_placeholder);
    def_setter(textarea, "set_rows", &TextArea::set_rows);
    def_setter(textarea, "on_change", &TextArea::on_change);

    auto scroll = nb::class_<ScrollArea, Widget>(m, "ScrollArea");
    scroll.def_rw("hint_w", &ScrollArea::hint_w_).def_rw("hint_h", &ScrollArea::hint_h_);
    bind_add(scroll); // routes into the scrolling content column
    scroll.def_prop_ro("content",
                       [](ScrollArea& s) -> Column& { return s.content(); },
                       nb::rv_policy::reference_internal);

    auto listview = nb::class_<ListView, Widget>(m, "ListView");
    def_setter(listview, "set_items", &ListView::set_items);
    def_setter(listview, "set_selected", &ListView::set_selected);
    def_setter(listview, "on_select", &ListView::on_select);
    listview.def_prop_ro("selected", &ListView::selected);

    auto tabs = nb::class_<Tabs, Widget>(m, "Tabs");
    def_setter(tabs, "set_tabs", &Tabs::set_tabs);
    def_setter(tabs, "set_selected", &Tabs::set_selected);
    def_setter(tabs, "on_change", &Tabs::on_change);

    auto dropdown = nb::class_<Dropdown, Widget>(m, "Dropdown");
    def_setter(dropdown, "set_items", &Dropdown::set_items);
    def_setter(dropdown, "set_placeholder", &Dropdown::set_placeholder);
    def_setter(dropdown, "set_selected", &Dropdown::set_selected);
    def_setter(dropdown, "on_select", &Dropdown::on_select);
    dropdown.def_prop_ro("selected", &Dropdown::selected);

    auto expander = nb::class_<Expander, Container>(m, "Expander");
    def_setter(expander, "set_open", &Expander::set_open);
    def_setter(expander, "set_title", &Expander::set_title);
    def_setter(expander, "on_toggle", &Expander::on_toggle);
    expander.def_prop_ro("open", &Expander::open);

    auto accordion = nb::class_<Accordion, Column>(m, "Accordion");
    def_setter(accordion, "set_exclusive", &Accordion::set_exclusive);
    accordion.def_prop_ro("exclusive", &Accordion::exclusive)
             .def("add_section",
                  [](Accordion& a, std::string title, bool open) -> Expander& {
                      return a.add_section(std::move(title), open);
                  },
                  nb::arg("title"), nb::arg("open") = false,
                  nb::rv_policy::reference_internal);

    nb::enum_<Image::Fit>(m, "ImageFit")
        .value("Stretch", Image::Fit::Stretch)
        .value("Contain", Image::Fit::Contain)
        .value("Cover", Image::Fit::Cover);

    auto image = nb::class_<Image, Widget>(m, "Image");
    def_setter(image, "set_fit", &Image::set_fit);
    image.def("set_pixels",
              [](Image& img, nb::ndarray<nb::numpy, const std::uint8_t, nb::ndim<3>> arr) -> Image& {
                  if (arr.shape(2) != 4)
                      throw nb::value_error("set_pixels expects (H, W, 4) uint8 RGBA");
                  return img.set_pixels(static_cast<int>(arr.shape(1)),
                                        static_cast<int>(arr.shape(0)), arr.data());
              },
              nb::arg("pixels"), nb::rv_policy::reference)
         .def("set_file", &Image::set_file, nb::arg("path"))
         .def("set_memory",
              [](Image& img, nb::bytes blob) -> bool {
                  return img.set_memory(
                      reinterpret_cast<const std::uint8_t*>(blob.data()), blob.size());
              },
              nb::arg("data"))
         .def("set_size",
              [](Image& img, float w, float h) -> Image& { return img.set_size(w, h); },
              nb::arg("w"), nb::arg("h"), nb::rv_policy::reference)
         .def_prop_ro("width", &Image::width)
         .def_prop_ro("height", &Image::height);

    // ------------------------------------------------------------------ //
    // Theme
    // ------------------------------------------------------------------ //

    nb::class_<Theme>(m, "Theme")
        .def(nb::init<>())
        .def_rw("scale", &Theme::scale)
        .def_rw("background", &Theme::background)
        .def_rw("surface", &Theme::surface)
        .def_rw("surface_alt", &Theme::surface_alt)
        .def_rw("border", &Theme::border)
        .def_rw("text", &Theme::text)
        .def_rw("text_dim", &Theme::text_dim)
        .def_rw("accent", &Theme::accent)
        .def_rw("accent_text", &Theme::accent_text)
        .def_rw("danger", &Theme::danger)
        .def_rw("success", &Theme::success)
        .def_rw("warning", &Theme::warning)
        .def_rw("radius", &Theme::radius)
        .def_rw("border_width", &Theme::border_width)
        .def_rw("gap", &Theme::gap)
        .def_rw("base_font_px", &Theme::base_font_px)
        .def_rw("small_font_px", &Theme::small_font_px)
        .def_rw("large_font_px", &Theme::large_font_px)
        .def("hover", &Theme::hover, nb::arg("base"))
        .def("press", &Theme::press, nb::arg("base"))
        .def("dimmed", &Theme::dimmed, nb::arg("base"))
        .def_static("dark", &Theme::dark)
        .def_static("light", &Theme::light)
        .def_static("nord", &Theme::nord)
        .def_static("dracula", &Theme::dracula)
        .def_static("gruvbox", &Theme::gruvbox)
        .def_static("mocha", &Theme::mocha)
        .def_static("solarized", &Theme::solarized)
        .def_static("phosphor", &Theme::phosphor);

    nb::class_<ThemePreset>(m, "ThemePreset")
        .def_ro("name", &ThemePreset::name)
        .def("make", [](const ThemePreset& p) { return p.make(); });

    m.def("use_theme", &use_theme, nb::arg("into"), nb::arg("preset"));
    m.def("apply_preset", &apply_preset, nb::arg("theme"), nb::arg("index"));
    m.def("theme_presets", [] { return theme_presets(); },
          "Built-in presets in display order.");

    // ------------------------------------------------------------------ //
    // Module functions
    // ------------------------------------------------------------------ //

    // ------------------------------------------------------------------ //
    // Module functions
    // ------------------------------------------------------------------ //

    m.def("find_system_font", &find_system_font,
          "First system TTF that opens (best-effort; '' if none).");
}
