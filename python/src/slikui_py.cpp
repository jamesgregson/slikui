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
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/vector.h>

#include <slikui/slikui.hpp>
#include <slikui/backends/software/software.hpp>
#include <slikui/text/default_font.hpp>
#include <slikui/text/system_fonts.hpp>
#include <slikui/version.hpp>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>

namespace nb = nanobind;
using namespace slikui;

namespace {

// Shortest round-trippable-enough float text for __repr__ ("12", "0.25").
std::string fmt_float(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", double(v));
    return buf;
}

// Bind one C++ setter `W& W::f(V)` under `name`, returning the same wrapper.
template <typename W, typename... Bs, typename V>
void def_setter(nb::class_<W, Bs...>& cls, const char* name, W& (W::*f)(V)) {
    cls.def(name, [f](W& w, V v) -> W& { return (w.*f)(std::move(v)); },
            nb::rv_policy::reference, nb::arg("value"));
}

// Vec2Edit/Vec3Edit/Vec4Edit share one API; bind it once per instantiation.
template <size_t N>
void bind_vec_edit(nb::class_<slikui::VecNEdit<N>, slikui::Widget>& cls) {
    using Vec = slikui::VecNEdit<N>;
    def_setter(cls, "set_labels", &Vec::set_labels);
    def_setter(cls, "on_change", &Vec::on_change);
    cls.def("set_range",
            [](Vec& v, float min, float max) -> Vec& { return v.set_range(min, max); },
            nb::arg("min"), nb::arg("max"), nb::rv_policy::reference)
        .def("set_axis_range",
             [](Vec& v, size_t axis, float min, float max) -> Vec& {
                 return v.set_axis_range(axis, min, max);
             },
             nb::arg("axis"), nb::arg("min"), nb::arg("max"), nb::rv_policy::reference)
        .def("set_int_mode", [](Vec& v, bool on) -> Vec& { return v.set_int_mode(on); },
             nb::arg("on"), nb::rv_policy::reference)
        .def("set_step", [](Vec& v, float step) -> Vec& { return v.set_step(step); },
             nb::arg("step"), nb::rv_policy::reference);
}

// Swatch — a small clickable color chip that highlights when its
// `is_selected` callback says so. Kept as the C++ reference for the Python
// custom-widget trampoline: python/tests/test_custom_widgets.py renders a
// pure-Python Swatch (subclassing slikui.Widget) and asserts the frames are
// pixel-identical. New custom widgets should be written in Python — see
// docs/python-custom-widgets.md.
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
        .def("add_dragvalue",
             [](Host& h, float value) -> DragValue& {
                 return h.template add<DragValue>(value);
             },
             nb::arg("value") = 0.f, nb::rv_policy::reference_internal)
        .def("add_vec2edit",
             [](Host& h, nb::ndarray<nb::numpy, float, nb::ndim<1>> values) -> Vec2Edit& {
                 if (values.shape(0) != 2)
                     throw nb::value_error("Vec2Edit needs a float32 array of length 2");
                 return h.template add<Vec2Edit>(values.data());
             },
             nb::arg("values"), nb::rv_policy::reference_internal)
        .def("add_vec3edit",
             [](Host& h, nb::ndarray<nb::numpy, float, nb::ndim<1>> values) -> Vec3Edit& {
                 if (values.shape(0) != 3)
                     throw nb::value_error("Vec3Edit needs a float32 array of length 3");
                 return h.template add<Vec3Edit>(values.data());
             },
             nb::arg("values"), nb::rv_policy::reference_internal)
        .def("add_vec4edit",
             [](Host& h, nb::ndarray<nb::numpy, float, nb::ndim<1>> values) -> Vec4Edit& {
                 if (values.shape(0) != 4)
                     throw nb::value_error("Vec4Edit needs a float32 array of length 4");
                 return h.template add<Vec4Edit>(values.data());
             },
             nb::arg("values"), nb::rv_policy::reference_internal)
        .def("add_plot", [](Host& h) -> Plot& { return h.template add<Plot>(); },
             nb::rv_policy::reference_internal)
        .def("add_vec2pad", [](Host& h) -> Vec2Pad& { return h.template add<Vec2Pad>(); },
             nb::rv_policy::reference_internal)
        .def("add_treeview", [](Host& h) -> TreeView& { return h.template add<TreeView>(); },
             nb::rv_policy::reference_internal)
        .def("add_table", [](Host& h) -> Table& { return h.template add<Table>(); },
             nb::rv_policy::reference_internal)
        .def("add_propertygrid",
             [](Host& h) -> PropertyGrid& { return h.template add<PropertyGrid>(); },
             nb::rv_policy::reference_internal)
        .def("add_iconbutton",
             [](Host& h, Icon icon) -> IconButton& {
                 return h.template add<IconButton>(icon);
             },
             nb::arg("icon"), nb::rv_policy::reference_internal)
        .def("add_toolbar", [](Host& h) -> Toolbar& { return h.template add<Toolbar>(); },
             nb::rv_policy::reference_internal)
        .def("add_menubar", [](Host& h) -> MenuBar& { return h.template add<MenuBar>(); },
             nb::rv_policy::reference_internal)
        .def("add_coloredit",
             [](Host& h, Color color) -> ColorEdit& {
                 return h.template add<ColorEdit>(color);
             },
             nb::arg("color") = Color::white(), nb::rv_policy::reference_internal)
        .def("add_segmented", [](Host& h) -> Segmented& { return h.template add<Segmented>(); },
             nb::rv_policy::reference_internal)
        .def("add_groupbox",
             [](Host& h, std::string title) -> GroupBox& {
                 return h.template add<GroupBox>(std::move(title));
             },
             nb::arg("title"), nb::rv_policy::reference_internal)
        .def("add_searchbox", [](Host& h) -> SearchBox& { return h.template add<SearchBox>(); },
             nb::rv_policy::reference_internal)
        .def("add_statusbar", [](Host& h) -> StatusBar& { return h.template add<StatusBar>(); },
             nb::rv_policy::reference_internal)
        .def("add_logview", [](Host& h) -> LogView& { return h.template add<LogView>(); },
             nb::rv_policy::reference_internal)
        .def("add_imageview", [](Host& h) -> ImageView& { return h.template add<ImageView>(); },
             nb::rv_policy::reference_internal)
        .def("add_compareview",
             [](Host& h) -> CompareView& { return h.template add<CompareView>(); },
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
             nb::arg("name"), nb::rv_policy::reference_internal)
        .def("add_splitter",
             [](Host& h, bool vertical) -> Splitter& {
                 return h.template add<Splitter>(vertical);
             },
             nb::arg("vertical") = false, nb::rv_policy::reference_internal);
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

// PyWidget — trampoline so Python classes can implement the Widget contract
// (measure/paint/on_event/...). See docs/python-custom-widgets.md.
//
// Lifetime model (spike-verified): Python-created widgets live in
// nanobind-colocated storage, so the C++ tree must never `delete` them. The
// tree destroys every widget through Widget::Deleter -> virtual release();
// PyWidget::release() drops the anchor — a reference to the instance's own
// wrapper — and nanobind then runs the destructor and frees the storage.
// While the tree holds the pointer, the anchor keeps the wrapper alive.
struct PyWidget : slikui::Widget {
    NB_TRAMPOLINE(slikui::Widget);

    Size measure(const Constraints& c) override { NB_OVERRIDE(measure, c); }

    void arrange(const Rect& area) override { NB_OVERRIDE(arrange, area); }

    void paint(const PaintEnv& env) const override { NB_OVERRIDE(paint, env); }

    void paint_over(const PaintEnv& env) const override { NB_OVERRIDE(paint_over, env); }

    // Protected in C++; the bound names below forward to these.
    std::optional<Rect> clip_rect() const override { NB_OVERRIDE(clip_rect); }
    // Manual dispatch: NB_OVERRIDE would try to COPY the (non-copyable)
    // Context into the call; pass it by reference instead.
    void attached(Context& c) override {
        constexpr std::uint64_t nb_hash = nb::detail::str_hash("attached");
        nb::detail::ticket t(nb_trampoline, "attached", nb_hash, false);
        if (t.key.is_valid()) {
            nb_trampoline.base().attr(t.key)(nb::cast(&c, nb::rv_policy::reference));
            return;
        }
        slikui::Widget::attached(c);
    }
    std::optional<Rect> base_clip_rect() const { return slikui::Widget::clip_rect(); }
    void base_attached(Context& c) { slikui::Widget::attached(c); }

    bool on_event(const Event& e) override { NB_OVERRIDE(on_event, e); }

    void tick(Time t) override { NB_OVERRIDE(tick, t); }

    bool stretch_cross() const override { NB_OVERRIDE(stretch_cross); }

    bool expands_main() const override { NB_OVERRIDE(expands_main); }

    bool focusable() const override { NB_OVERRIDE(focusable); }

    // widget_type() returns const char* — NB_OVERRIDE would hand back a
    // dangling pointer (nanobind materializes a temporary for the str ->
    // const char* cast). Dispatch manually; copy into a cache.
    const char* widget_type() const override {
        constexpr std::uint64_t nb_hash = nb::detail::str_hash("widget_type");
        nb::detail::ticket t(nb_trampoline, "widget_type", nb_hash, false);
        if (t.key.is_valid()) {
            type_cache_ = nb::borrow<nb::str>(nb_trampoline.base().attr(t.key)()).c_str();
            return type_cache_.c_str();
        }
        return slikui::Widget::widget_type();
    }

    // NOT dispatched to Python: this is the destruction protocol. Dropping
    // the anchor may deallocate `this` re-entrantly — touch nothing after.
    //
    // The subtree is destroyed eagerly, before the anchor drops. Dropping the
    // anchor alone does not guarantee ~PyWidget runs: Python may still hold
    // the wrapper — and the subtree itself can be what holds it. Callbacks
    // inside the subtree capture child wrappers, whose reference_internal
    // keep_alive edges climb back up to this wrapper: a cycle through C++
    // ownership the GC cannot see, so the whole subtree would leak (found
    // via examples/showcase.py: a Fill container hosting every page). The
    // tree is tearing this widget down, so its children go now, whatever
    // Python does with the detached wrapper afterwards. The anchor still
    // pins `this` while they die; moving them out first keeps `children`
    // consistent if their teardown re-enters Python.
    void release() noexcept override {
        nb::gil_scoped_acquire gil;
        {
            auto doomed = std::move(children);
            children.clear();
        } // children destroyed here (nested PyWidgets release recursively)
        anchor.reset();
    }

    // Called by add_widget()/set_root_widget() when the tree takes over.
    void anchor_self() { anchor = nb::borrow(nb::handle(nb_trampoline.self)); }

    nb::object anchor;          // set once the tree owns the pointer
    mutable std::string type_cache_;
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
             nb::arg("other"), nb::is_operator())
        .def("__repr__", [](const Vec2& v) {
            return "Vec2(" + fmt_float(v.x) + ", " + fmt_float(v.y) + ")";
        });

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
        .def("to_hsv",
             [](const Color& c) {
                 auto hsv = c.to_hsv();
                 return nb::make_tuple(hsv[0], hsv[1], hsv[2]);
             },
             "(h, s, v) in [0,1]; h=0 for achromatic colors.")
        .def_static("from_hsv",
                    [](float h, float s, float v, std::uint8_t alpha) {
                        return Color::from_hsv(h, s, v, alpha);
                    },
                    nb::arg("h"), nb::arg("s"), nb::arg("v"), nb::arg("alpha") = 255,
                    "h wraps; s/v clamp to [0,1].")
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

    nb::enum_<Cursor>(m, "Cursor")
        .value("Arrow", Cursor::Arrow)
        .value("IBeam", Cursor::IBeam)
        .value("Hand", Cursor::Hand)
        .value("Grab", Cursor::Grab)
        .value("Crosshair", Cursor::Crosshair)
        .value("ResizeH", Cursor::ResizeH)
        .value("ResizeV", Cursor::ResizeV);

    nb::enum_<PopupDir>(m, "PopupDir")
        .value("Below", PopupDir::Below)
        .value("Above", PopupDir::Above)
        .value("Right", PopupDir::Right)
        .value("Left", PopupDir::Left);

    nb::enum_<Icon>(m, "Icon")
        .value("Play", Icon::Play)
        .value("Pause", Icon::Pause)
        .value("Stop", Icon::Stop)
        .value("Record", Icon::Record)
        .value("StepForward", Icon::StepForward)
        .value("StepBack", Icon::StepBack)
        .value("Grid", Icon::Grid)
        .value("Camera", Icon::Camera)
        .value("Save", Icon::Save)
        .value("Folder", Icon::Folder)
        .value("Refresh", Icon::Refresh)
        .value("Plus", Icon::Plus)
        .value("Minus", Icon::Minus)
        .value("Close", Icon::Close)
        .value("Check", Icon::Check)
        .value("ChevronLeft", Icon::ChevronLeft)
        .value("ChevronRight", Icon::ChevronRight)
        .value("ChevronUp", Icon::ChevronUp)
        .value("ChevronDown", Icon::ChevronDown)
        .value("Eye", Icon::Eye)
        .value("Gear", Icon::Gear)
        .value("Search", Icon::Search)
        .value("Trash", Icon::Trash)
        .value("Copy", Icon::Copy)
        .value("Lock", Icon::Lock)
        .value("Sun", Icon::Sun);

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
        .def("inflate", &Rect::inflate, nb::arg("dx"), nb::arg("dy"))
        .def("right", &Rect::right)
        .def("bottom", &Rect::bottom)
        .def("center", &Rect::center)
        .def("__eq__", [](const Rect& a, const Rect& b) { return a == b; },
             nb::arg("other"), nb::is_operator())
        .def("__repr__", [](const Rect& r) {
            return "Rect(" + fmt_float(r.x) + ", " + fmt_float(r.y) + ", " +
                   fmt_float(r.w) + ", " + fmt_float(r.h) + ")";
        });

    nb::class_<Size>(m, "Size")
        .def(nb::init<float, float>(), nb::arg("w") = 0.f, nb::arg("h") = 0.f)
        .def_rw("w", &Size::w)
        .def_rw("h", &Size::h)
        .def("__eq__", [](const Size& a, const Size& b) { return a == b; },
             nb::arg("other"), nb::is_operator())
        .def("__repr__", [](const Size& z) {
            return "Size(" + fmt_float(z.w) + ", " + fmt_float(z.h) + ")";
        });

    nb::class_<Constraints>(m, "Constraints")
        .def(nb::init<>())
        .def_rw("min", &Constraints::min)
        .def_rw("max", &Constraints::max)
        .def_static("loose",
                    [](Vec2 maximum) { return Constraints::loose(maximum); },
                    nb::arg("maximum") = Vec2(kInfinite, kInfinite))
        .def_static("tight", [](Size s) { return Constraints::tight(s); },
                    nb::arg("size"))
        .def("clamp", &Constraints::clamp, nb::arg("size"))
        .def_prop_ro("is_bounded_x", &Constraints::is_bounded_x)
        .def_prop_ro("is_bounded_y", &Constraints::is_bounded_y);

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
        .value("F12", Key::F12)
        .value("A", Key::A)
        .value("B", Key::B)
        .value("C", Key::C)
        .value("D", Key::D)
        .value("E", Key::E)
        .value("F", Key::F)
        .value("G", Key::G)
        .value("H", Key::H)
        .value("I", Key::I)
        .value("J", Key::J)
        .value("K", Key::K)
        .value("L", Key::L)
        .value("M", Key::M)
        .value("N", Key::N)
        .value("O", Key::O)
        .value("P", Key::P)
        .value("Q", Key::Q)
        .value("R", Key::R)
        .value("S", Key::S)
        .value("T", Key::T)
        .value("U", Key::U)
        .value("V", Key::V)
        .value("W", Key::W)
        .value("X", Key::X)
        .value("Y", Key::Y)
        .value("Z", Key::Z)
        .value("Num0", Key::Num0)
        .value("Num1", Key::Num1)
        .value("Num2", Key::Num2)
        .value("Num3", Key::Num3)
        .value("Num4", Key::Num4)
        .value("Num5", Key::Num5)
        .value("Num6", Key::Num6)
        .value("Num7", Key::Num7)
        .value("Num8", Key::Num8)
        .value("Num9", Key::Num9);

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

    nb::enum_<Event::Type>(m, "EventType", nb::is_arithmetic(),
                           "Event kinds. Context-synthesized (never sent by hosts): "
                           "Click, DoubleClick, MouseEnter, MouseLeave, FocusGained; "
                           "FocusLost is both host→Context (app lost focus) and "
                           "Context→widget (this widget lost focus).")
        .value("MouseMove", Event::Type::MouseMove)
        .value("MousePress", Event::Type::MousePress)
        .value("MouseRelease", Event::Type::MouseRelease)
        .value("Scroll", Event::Type::Scroll)
        .value("KeyPress", Event::Type::KeyPress)
        .value("KeyRelease", Event::Type::KeyRelease)
        .value("TextInput", Event::Type::TextInput)
        .value("FocusLost", Event::Type::FocusLost)
        .value("FrameResize", Event::Type::FrameResize)
        .value("Click", Event::Type::Click)
        .value("DoubleClick", Event::Type::DoubleClick)
        .value("MouseEnter", Event::Type::MouseEnter)
        .value("MouseLeave", Event::Type::MouseLeave)
        .value("FocusGained", Event::Type::FocusGained);

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
        .def_rw("clicks", &Event::clicks,
                "Consecutive-click count on MousePress/MouseRelease/Click "
                "(1, 2, 3...); 0 on other events.")
        .def_rw("repeat", &Event::repeat, "KeyPress generated by key auto-repeat.")
        .def_prop_ro("shift", &Event::shift)
        .def_prop_ro("ctrl", &Event::ctrl)
        .def_prop_ro("alt", &Event::alt)
        .def_prop_ro("super", &Event::super)
        .def_prop_ro("shortcut_mod", &Event::shortcut_mod,
                     "Ctrl, or Cmd on macOS: the copy/paste chord modifier.")
        .def("__repr__", [](const Event& e) {
            using T = Event::Type;
            auto enum_name = [](auto v) {
                return std::string(nb::str(nb::cast(v).attr("name")).c_str());
            };
            std::string out = "Event(" + enum_name(e.type);
            auto pos = [&] { out += " pos=(" + fmt_float(e.pos.x) + ", " + fmt_float(e.pos.y) + ")"; };
            switch (e.type) {
            case T::MouseMove:
                pos();
                out += " delta=(" + fmt_float(e.delta.x) + ", " + fmt_float(e.delta.y) + ")";
                break;
            case T::MousePress: case T::MouseRelease: case T::Click: case T::DoubleClick:
                pos();
                out += " button=" + std::to_string(e.button) + " clicks=" + std::to_string(e.clicks);
                break;
            case T::Scroll:
                pos();
                out += " scroll=(" + fmt_float(e.scroll.x) + ", " + fmt_float(e.scroll.y) + ")";
                break;
            case T::KeyPress: case T::KeyRelease:
                out += " key=" + enum_name(e.key);
                if (e.repeat) out += " repeat";
                break;
            case T::TextInput:
                out += " codepoint=" + std::to_string(e.codepoint);
                break;
            case T::MouseEnter: case T::MouseLeave:
                pos();
                break;
            default:
                break;
            }
            if (e.mods) out += " mods=" + std::to_string(e.mods);
            return out + ")";
        });
    // Multi-click thresholds (Event.clicks): seconds on the tick clock, px.
    m.attr("Event").attr("MULTI_CLICK_TIME") = Event::kMultiClickTime;
    m.attr("Event").attr("MULTI_CLICK_SLOP") = Event::kMultiClickSlop;

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
        // Mutators — what a Python paint() override emits.
        .def("rect", &DrawList::rect, nb::arg("rect"), nb::arg("color"),
             nb::arg("radius") = 0.f)
        .def("hollow_rect", &DrawList::hollow_rect, nb::arg("rect"),
             nb::arg("color"), nb::arg("width"), nb::arg("radius") = 0.f)
        .def("line", &DrawList::line, nb::arg("a"), nb::arg("b"),
             nb::arg("color"), nb::arg("thickness") = 1.f)
        .def("circle", &DrawList::circle, nb::arg("center"), nb::arg("radius"),
             nb::arg("color"))
        .def("image", &DrawList::image, nb::arg("dst"), nb::arg("tex_id"),
             nb::arg("uv"))
        .def("glyph", &DrawList::glyph, nb::arg("dst"), nb::arg("tex_id"),
             nb::arg("uv"), nb::arg("color"))
        .def("push_clip", &DrawList::push_clip, nb::arg("rect"))
        .def("pop_clip", &DrawList::pop_clip)
        .def("clear", &DrawList::clear)
        .def_prop_ro("commands", [](const DrawList& l) { return l.commands(); })
        .def_prop_ro("size", [](const DrawList& l) { return l.size(); })
        .def("__len__", [](const DrawList& l) { return l.size(); })
        .def("__getitem__",
             [](const DrawList& l, std::size_t i) { return l.commands().at(i); })
        .def("__iter__",
             [](const DrawList& l) { return nb::cast(l.commands()).attr("__iter__")(); });

    // PaintEnv — the environment a paint() override receives: the frame's
    // draw list and the active theme. A view over context state, valid only
    // during the call (no font in v1: text needs Font/TextLayout bindings).
    nb::class_<PaintEnv>(m, "PaintEnv")
        .def_prop_ro("list",
                     [](PaintEnv& e) -> DrawList& { return e.list; },
                     nb::rv_policy::reference_internal)
        .def_prop_ro("theme",
                     [](PaintEnv& e) -> Theme& {
                         // PaintEnv carries the theme as const&, but it is
                         // context-owned mutable state; expose it as such.
                         return const_cast<Theme&>(e.theme);
                     },
                     nb::rv_policy::reference_internal);

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
        .def("load_default_font",
             [](Context& c) {
                 return c.load_font_memory(slikui::text::default_font_data(),
                                           slikui::text::default_font_size());
             },
             "Load the bundled DejaVu Sans subset (always available).")
        .def("set_root_column", [](Context& c) -> Column& { return c.set_root<Column>(); },
             nb::rv_policy::reference_internal)
        .def("set_root_row", [](Context& c) -> Row& { return c.set_root<Row>(); },
             nb::rv_policy::reference_internal)
        .def("set_root_widget",
             [](Context& c, Widget& w) -> Widget& {
                 if (auto* pw = dynamic_cast<PyWidget*>(&w)) {
                     if (pw->anchor)
                         throw nb::type_error(
                             "set_root_widget(): widget is already in a widget tree");
                     pw->anchor_self();
                 } else {
                     throw nb::type_error(
                         "set_root_widget(): custom widgets must subclass slikui.Widget");
                 }
                 c.set_root(std::unique_ptr<Widget>(&w));
                 return w;
             },
             nb::arg("widget"), nb::rv_policy::reference)
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
        .def("set_focus", [](Context& c, Widget* w) { c.set_focus(w); },
             nb::arg("widget").none(),
             "Move keyboard focus (None clears it); notifies both widgets.")
        .def_prop_ro("focused", [](Context& c) { return c.focused(); },
                     nb::rv_policy::reference)
        .def_prop_ro("hovered", [](Context& c) { return c.hovered(); },
                     nb::rv_policy::reference)
        .def_prop_ro("root", [](Context& c) { return c.root(); },
                     nb::rv_policy::reference, "The root widget (None before set_root).")
        .def("capture_mouse", [](Context& c, Widget& w) { c.capture_mouse(&w); },
             nb::arg("widget"),
             "Route every mouse event to `widget` until release_mouse — call "
             "on MousePress so a drag survives the pointer leaving it.")
        .def("release_mouse", [](Context& c, Widget& w) { c.release_mouse(&w); },
             nb::arg("widget"))
        .def("add_shortcut",
             [](Context& c, Key key, unsigned mods, std::function<void()> action) -> Context& {
                 return c.add_shortcut(key, mods, std::move(action));
             },
             nb::arg("key"), nb::arg("mods"), nb::arg("action"), nb::rv_policy::reference,
             "Global accelerator: fires on key+mods (needs a modifier) before "
             "widget dispatch.")
        .def("clear_shortcuts", &Context::clear_shortcuts)
        .def("push_popup",
             [](Context& c, Widget& w, Vec2 pos) -> Widget& {
                 return c.push_popup(w, pos);
             },
             nb::arg("widget"), nb::arg("pos"), nb::rv_policy::reference_internal,
             "Borrowed popup at a screen position; keep the widget alive.")
        .def("push_popup",
             [](Context& c, Widget& w, Widget& anchor, PopupDir dir) -> Widget& {
                 return c.push_popup(w, anchor, dir);
             },
             nb::arg("widget"), nb::arg("anchor"), nb::arg("dir"),
             nb::rv_policy::reference_internal,
             "Borrowed popup anchored beside a widget (flips + clamps).")
        .def("open_context_menu",
             [](Context& c, Menu& m, Vec2 pos) -> Widget& {
                 return c.open_context_menu(m, pos);
             },
             nb::arg("menu"), nb::arg("pos"), nb::rv_policy::reference_internal,
             "Open a Menu as a floating popup at pos (the right-click path "
             "does this for widgets with set_context_menu).")
        .def("set_clipboard_source",
             [](Context& c, std::function<std::string()> f) -> Context& {
                 return c.set_clipboard_source(std::move(f));
             },
             nb::arg("source"), nb::rv_policy::reference,
             "Host clipboard read callback; absent = clipboard ops are no-ops.")
        .def("set_clipboard_sink",
             [](Context& c, std::function<void(std::string)> f) -> Context& {
                 return c.set_clipboard_sink(std::move(f));
             },
             nb::arg("sink"), nb::rv_policy::reference)
        .def("close_popups", &Context::close_popups)
        .def("clear_tree", &Context::clear_tree)
        .def_prop_ro("has_popups", &Context::has_popups)
        .def_prop_ro("popup_count", &Context::popup_count)
        .def("popup",
             [](Context& c, std::size_t i) -> Widget* {
                 return i < c.popup_count() ? c.popup(i) : nullptr;
             },
             nb::arg("index"), nb::rv_policy::reference)
        .def("set_cursor_host",
             [](Context& c, nb::object fn) -> Context& {
                 c.set_cursor_host([fn](Cursor cur) {
                     nb::gil_scoped_acquire gil; // GLFW pumps events w/o GIL
                     fn(cur);
                 });
                 return c;
             },
             nb::arg("host"), nb::rv_policy::reference,
             "Host callback receiving a Cursor whenever the hovered widget "
             "requests a shape (GLFW attach sets the real cursor in C++).")
        .def("open_dialog",
             [](Context& c, Dialog& d) -> Dialog& {
                 return static_cast<Dialog&>(c.open_dialog(d));
             },
             nb::arg("dialog"), nb::rv_policy::reference_internal,
             "Put a Dialog on the floating shell layer (borrowed: keep the "
             "Python object alive). Modal dialogs dim the app and trap "
             "outside presses. Default position is centered.")
        .def("close_dialog",
             [](Context& c, Dialog& d) { c.close_dialog(d); }, nb::arg("dialog"))
        .def_prop_ro("dialog_count", &Context::dialog_count)
        .def("dialog",
             [](Context& c, std::size_t i) -> Widget* {
                 return i < c.dialog_count() ? c.dialog(i) : nullptr;
             },
             nb::arg("index"), nb::rv_policy::reference)
        .def("push_toast",
             [](Context& c, std::string text, double seconds,
                std::optional<Icon> icon) -> Toast& {
                 auto t = std::make_unique<Toast>(std::move(text));
                 Toast* raw = t.get();
                 if (icon) raw->set_icon(*icon);
                 c.push_toast(std::move(t), seconds);
                 return *raw;
             },
             nb::arg("text"), nb::arg("seconds") = 3.0,
             nb::arg("icon") = nb::none(), nb::rv_policy::reference,
             "Transient corner notice, owned by the context; auto-dismisses "
             "after `seconds` (measured from the last tick's time).");

    // ------------------------------------------------------------------ //
    // Widgets
    // ------------------------------------------------------------------ //

    auto widget_cls = nb::class_<Widget, PyWidget>(m, "Widget")
        .def(nb::init<>()) // super().__init__() for Python subclasses
        // The virtual protocol is bound by name so the trampoline can resolve
        // overrides on Python subclasses (NB_OVERRIDE looks the name up in
        // the MRO; unbound names would raise "lookup failed" on dispatch).
        .def("measure",
             [](Widget& w, const Constraints& c) { return w.measure(c); },
             nb::arg("constraints"),
             "Override: intrinsic size under `constraints`; return "
             "constraints.clamp(Size(w, h)). Layout runs in Context.tick(), "
             "not render() — bounds are zero until the first tick.")
        .def("arrange",
             [](Widget& w, const Rect& area) { w.arrange(area); },
             nb::arg("area"),
             "Runs the C++ arrange (custom containers call this on their "
             "child after assigning their own bounds).")
        .def("paint",
             [](Widget& w, const PaintEnv& e) { w.paint(e); },
             nb::arg("env"),
             "Override: append draw commands to env.list (rect, hollow_rect, "
             "line, circle, image, push_clip/pop_clip) using env.theme colors, "
             "in absolute coordinates (self.bounds). Painted BEFORE children; "
             "see paint_over for drawing on top of them.")
        .def("paint_over",
             [](Widget& w, const PaintEnv& e) { w.paint_over(e); },
             nb::arg("env"),
             "Override: drawn AFTER (on top of) the children, outside this "
             "widget's clip_rect — decorations over wrapped widgets.")
        .def("clip_rect",
             [](Widget& w) {
                 auto* pw = dynamic_cast<PyWidget*>(&w);
                 return pw ? pw->base_clip_rect() : std::optional<Rect>{};
             },
             "Override: Rect to clip the children to while painting, or None.")
        .def("attached",
             [](Widget& w, Context& c) {
                 if (auto* pw = dynamic_cast<PyWidget*>(&w)) pw->base_attached(c);
             },
             nb::arg("context"),
             "Override: called once when the widget joins a Context (on the "
             "first tick after it was added).")
        .def("on_event",
             [](Widget& w, const Event& e) { return w.on_event(e); },
             nb::arg("event"),
             "Override: return True to consume. Mouse events (MouseMove, "
             "MousePress, MouseRelease, Click, DoubleClick, Scroll) reach the "
             "widget under the pointer — or the mouse-capture widget — and "
             "bubble to ancestors until consumed; every button is delivered "
             "(check event.button). Key events go to the focused widget "
             "(falling back to the hovered one) and bubble; KeyRelease only "
             "follows a KeyPress the widget tree received. TextInput needs "
             "focus (focusable() -> True). MouseEnter/MouseLeave and "
             "FocusGained/FocusLost go to the affected widget only. Use "
             "context.capture_mouse(self) on press so a drag keeps receiving "
             "moves and the release outside your bounds.")
        .def("tick", [](Widget& w, Time t) { w.tick(t); }, nb::arg("time"),
             "Override: per-frame update; time.now / time.dt in seconds. "
             "Use this clock, not wall time, for animation.")
        .def("stretch_cross", [](Widget& w) { return w.stretch_cross(); })
        .def("expands_main", [](Widget& w) { return w.expands_main(); })
        .def("focusable", [](Widget& w) { return w.focusable(); })
        .def("widget_type",
             [](Widget& w) { return std::string(w.widget_type()); })
        .def_rw("id", &Widget::id)
        .def_rw("visible", &Widget::visible)
        .def_rw("enabled", &Widget::enabled)
        .def_prop_ro("focused", [](Widget& w) { return w.focused; },
                     "True while this widget holds keyboard focus.")
        .def_prop_ro("context", [](Widget& w) { return w.context(); },
                     nb::rv_policy::reference,
                     "The Context this widget is attached to (None until the "
                     "first tick after it joined a tree).")
        .def("request_focus",
             [](Widget& w) {
                 if (!w.context())
                     throw nb::value_error(
                         "request_focus(): widget is not attached to a Context "
                         "yet (call after the first tick)");
                 w.context()->set_focus(&w);
             },
             "Give this widget keyboard focus (it receives FocusGained).")
        .def("set_cursor",
             [](Widget& w, Cursor c) -> Widget& { return w.set_cursor(c); },
             nb::arg("cursor"), nb::rv_policy::reference,
             "Cursor shape requested while hovered (needs a cursor host).")
        .def_prop_ro("type", [](Widget& w) { return std::string(w.widget_type()); })
        .def_prop_ro("tooltip", [](Widget& w) { return w.tooltip(); })
        .def_prop_rw("bounds",
                     [](Widget& w) { return w.bounds; },
                     [](Widget& w, Rect area) { w.bounds = area; },
                     "Readable always; writable for custom widgets that "
                     "implement arrange() (the C++ side assigns bounds via "
                     "Widget::arrange, which Python overrides).")
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
        .def("set_context_menu",
             [](Widget& w, Menu& m) -> Widget& { return w.set_context_menu(&m); },
             nb::arg("menu"), nb::rv_policy::reference,
             "Right-click target for this widget and its subtree. Borrowed: "
             "keep the Menu alive on the Python side.")
        .def("set_tooltip",
             [](Widget& w, std::string t) -> Widget& { return w.set_tooltip(std::move(t)); },
             nb::arg("text"), nb::rv_policy::reference)
        .def("add_widget",
             [](Widget& host, Widget& child) -> Widget& {
                 if (auto* pw = dynamic_cast<PyWidget*>(&child)) {
                     if (pw->anchor) // nanobind owns the storage once anchored
                         throw nb::type_error(
                             "add_widget(): widget is already in a widget tree");
                     pw->anchor_self();
                 } else {
                     throw nb::type_error(
                         "add_widget(): custom widgets must subclass slikui.Widget — "
                         "Python subclasses of bound widget types (Column, Row, ...) "
                         "cannot dispatch overrides");
                 }
                 host.add(std::unique_ptr<Widget>(&child));
                 return child;
             },
             nb::arg("widget"), nb::rv_policy::reference);
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
    panel_cls.def(nb::init<>()); // standalone popups (Menus, anchored panels)
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
    label.def_prop_ro("text", [](Label& l) { return l.text(); });

    auto button = nb::class_<Button, Widget>(m, "Button");
    def_setter(button, "set_text", &Button::set_text);
    def_setter(button, "set_variant", &Button::set_variant);
    button.def_prop_ro("text", [](Button& b) { return b.text(); });
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
    def_setter(slider, "set_label", &Slider::set_label);
    def_setter(slider, "set_format", &Slider::set_format);
    def_setter(slider, "set_log_scale", &Slider::set_log_scale);
    def_setter(slider, "set_step", &Slider::set_step);
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
                nb::rv_policy::reference)
           .def_prop_ro("value", &Slider::value);

    auto progress = nb::class_<ProgressBar, Widget>(m, "ProgressBar");
    def_setter(progress, "set_value", &ProgressBar::set_value);

    auto dragvalue = nb::class_<DragValue, Widget>(m, "DragValue");
    def_setter(dragvalue, "on_change", &DragValue::on_change);
    def_setter(dragvalue, "set_speed", &DragValue::set_speed);
    def_setter(dragvalue, "set_step", &DragValue::set_step);
    def_setter(dragvalue, "set_int_mode", &DragValue::set_int_mode);
    def_setter(dragvalue, "set_prefix", &DragValue::set_prefix);
    def_setter(dragvalue, "set_suffix", &DragValue::set_suffix);
    def_setter(dragvalue, "set_format", &DragValue::set_format);
    dragvalue.def("set_value",
                  [](DragValue& d, float v, bool fire) -> DragValue& {
                      return d.set_value(v, fire);
                  },
                  nb::arg("value"), nb::arg("fire_callback") = true,
                  nb::rv_policy::reference)
             .def("set_range",
                  [](DragValue& d, float min, float max) -> DragValue& {
                      return d.set_range(min, max);
                  },
                  nb::arg("min"), nb::arg("max"), nb::rv_policy::reference)
             .def_prop_ro("value", &DragValue::value);

    // VecNEdit borrows a float32 array: edits write into it, the fields
    // mirror it every tick. Keep the numpy array alive as long as the widget.
    auto vec2 = nb::class_<Vec2Edit, Widget>(m, "Vec2Edit");
    vec2.def("__init__",
             [](Vec2Edit* w, nb::ndarray<nb::numpy, float, nb::ndim<1>> values,
                std::string labels) {
                 if (values.shape(0) != 2)
                     throw nb::value_error("Vec2Edit needs a float32 array of length 2");
                 new (w) Vec2Edit(values.data(), std::move(labels));
             },
             nb::arg("values"), nb::arg("labels") = "XY");
    bind_vec_edit(vec2);

    auto vec3 = nb::class_<Vec3Edit, Widget>(m, "Vec3Edit");
    vec3.def("__init__",
             [](Vec3Edit* w, nb::ndarray<nb::numpy, float, nb::ndim<1>> values,
                std::string labels) {
                 if (values.shape(0) != 3)
                     throw nb::value_error("Vec3Edit needs a float32 array of length 3");
                 new (w) Vec3Edit(values.data(), std::move(labels));
             },
             nb::arg("values"), nb::arg("labels") = "XYZ");
    bind_vec_edit(vec3);

    auto vec4 = nb::class_<Vec4Edit, Widget>(m, "Vec4Edit");
    vec4.def("__init__",
             [](Vec4Edit* w, nb::ndarray<nb::numpy, float, nb::ndim<1>> values,
                std::string labels) {
                 if (values.shape(0) != 4)
                     throw nb::value_error("Vec4Edit needs a float32 array of length 4");
                 new (w) Vec4Edit(values.data(), std::move(labels));
             },
             nb::arg("values"), nb::arg("labels") = "XYZW");
    bind_vec_edit(vec4);

    auto coloredit = nb::class_<ColorEdit, Widget>(m, "ColorEdit");
    def_setter(coloredit, "on_change", &ColorEdit::on_change);
    coloredit.def("set_color",
                  [](ColorEdit& c, Color color, bool fire) -> ColorEdit& {
                      return c.set_color(color, fire);
                  },
                  nb::arg("color"), nb::arg("fire_callback") = true,
                  nb::rv_policy::reference)
               .def_prop_ro("color", &ColorEdit::color);

    auto segmented = nb::class_<Segmented, Widget>(m, "Segmented");
    def_setter(segmented, "set_items", &Segmented::set_items);
    def_setter(segmented, "on_select", &Segmented::on_select);
    segmented.def("set_selected",
                  [](Segmented& s, long index, bool fire) -> Segmented& {
                      return s.set_selected(index, fire);
                  },
                  nb::arg("index"), nb::arg("fire_callback") = true,
                  nb::rv_policy::reference)
               .def_prop_ro("selected", &Segmented::selected);

    // --- menus & toolbars (Phase B) -----------------------------------
    auto menu = nb::class_<Menu, Panel>(m, "Menu");
    menu.def(nb::init<std::string, std::string>(), nb::arg("title") = "",
             nb::arg("id") = "")
        .def("add_item",
             [](Menu& m, std::string label, std::function<void()> action,
                std::string hint, bool enabled) -> Menu& {
                 return m.add_item(std::move(label), std::move(action),
                                   std::move(hint), enabled);
             },
             nb::arg("label"), nb::arg("action"), nb::arg("hint") = "",
             nb::arg("enabled") = true, nb::rv_policy::reference_internal)
        .def("add_checkable",
             [](Menu& m, std::string label, std::function<bool()> is_checked,
                std::function<void()> toggle, std::string hint, bool enabled) -> Menu& {
                 return m.add_checkable(std::move(label), std::move(is_checked),
                                        std::move(toggle), std::move(hint), enabled);
             },
             nb::arg("label"), nb::arg("is_checked"), nb::arg("toggle"),
             nb::arg("hint") = "", nb::arg("enabled") = true,
             nb::rv_policy::reference_internal)
        .def("add_submenu",
             [](Menu& m, std::string label, Menu& sub, std::string hint,
                bool enabled) -> Menu& {
                 return m.add_submenu(std::move(label), sub, std::move(hint),
                                      enabled);
             },
             nb::arg("label"), nb::arg("submenu"), nb::arg("hint") = "",
             nb::arg("enabled") = true, nb::rv_policy::reference_internal,
             "Borrowed submenu: keep the Menu object alive on the Python side.")
        .def("add_separator", [](Menu& m) -> Menu& { return m.add_separator(); },
             nb::rv_policy::reference_internal)
        .def("set_min_width", [](Menu& m, float w) -> Menu& { return m.set_min_width(w); },
             nb::arg("w"), nb::rv_policy::reference)
        .def("on_open", [](Menu& m, std::function<void()> f) -> Menu& { return m.on_open(std::move(f)); },
             nb::arg("f"), nb::rv_policy::reference)
        .def("open_for",
             [](Menu& m, Widget& anchor, PopupDir dir) {
                 m.open_for(anchor, dir);
             },
             nb::arg("anchor"), nb::arg("dir") = PopupDir::Below,
             "Push this menu onto the popup stack beside anchor.");

    auto menubar = nb::class_<MenuBar, Row>(m, "MenuBar");
    menubar.def(nb::init<std::string>(), nb::arg("id") = "")
        .def("add_menu",
             [](MenuBar& b, std::string title) -> Menu& {
                 return b.add_menu(std::move(title));
             },
             nb::arg("title"), nb::rv_policy::reference_internal);

    auto iconbutton = nb::class_<IconButton, Widget>(m, "IconButton");
    def_setter(iconbutton, "on_click", &IconButton::on_click);
    iconbutton.def(nb::init<Icon, std::string>(), nb::arg("icon"), nb::arg("id") = "")
        .def("set_icon",
             [](IconButton& b, Icon i) -> IconButton& { return b.set_icon(i); },
             nb::arg("icon"), nb::rv_policy::reference)
        .def("set_variant",
             [](IconButton& b, Button::Variant v) -> IconButton& {
                 return b.set_variant(v);
             },
             nb::arg("variant"), nb::rv_policy::reference)
        .def("set_icon_px",
             [](IconButton& b, float px) -> IconButton& { return b.set_icon_px(px); },
             nb::arg("px"), nb::rv_policy::reference)
        .def_prop_ro("icon", &IconButton::icon)
        .def_prop_ro("variant", &IconButton::variant);

    // --- data display (Phase C) ----------------------------------------
    auto plot = nb::class_<Plot, Widget>(m, "Plot");
    def_setter(plot, "set_bars", &Plot::set_bars);
    def_setter(plot, "set_fill", &Plot::set_fill);
    def_setter(plot, "set_markers", &Plot::set_markers);
    plot.def(nb::init<std::string>(), nb::arg("id") = "")
        .def("set_series",
             [](Plot& p, nb::ndarray<nb::numpy, float, nb::ndim<1>> ys, Color color) -> Plot& {
                 std::vector<Vec2> pts;
                 pts.reserve(ys.shape(0));
                 for (size_t i = 0; i < ys.shape(0); ++i)
                     pts.push_back({float(i), ys.data()[i]});
                 return p.clear_series().add_series(std::move(pts), color);
             },
             nb::arg("ys"), nb::arg("color"), nb::rv_policy::reference,
             "Single y-series with x = 0..n-1 (numpy float32, copied).")
        .def("add_series",
             [](Plot& p, nb::ndarray<nb::numpy, float, nb::ndim<1>> xs,
                nb::ndarray<nb::numpy, float, nb::ndim<1>> ys, Color color) -> Plot& {
                 if (xs.shape(0) != ys.shape(0))
                     throw nb::value_error("xs and ys must have the same length");
                 std::vector<Vec2> pts;
                 pts.reserve(ys.shape(0));
                 for (size_t i = 0; i < ys.shape(0); ++i)
                     pts.push_back({xs.data()[i], ys.data()[i]});
                 return p.add_series(std::move(pts), color);
             },
             nb::arg("xs"), nb::arg("ys"), nb::arg("color"), nb::rv_policy::reference)
        .def("clear_series", [](Plot& p) -> Plot& { return p.clear_series(); },
             nb::rv_policy::reference)
        .def("set_x_range",
             [](Plot& p, float a, float b) -> Plot& { return p.set_x_range(a, b); },
             nb::arg("min"), nb::arg("max"), nb::rv_policy::reference)
        .def("set_y_range",
             [](Plot& p, float a, float b) -> Plot& { return p.set_y_range(a, b); },
             nb::arg("min"), nb::arg("max"), nb::rv_policy::reference);

    auto vec2pad = nb::class_<Vec2Pad, Widget>(m, "Vec2Pad");
    def_setter(vec2pad, "on_change", &Vec2Pad::on_change);
    vec2pad.def(nb::init<std::string>(), nb::arg("id") = "")
        .def("set_value",
             [](Vec2Pad& p, Vec2 v, bool fire) -> Vec2Pad& {
                 return p.set_value(v, fire);
             },
             nb::arg("value"), nb::arg("fire_callback") = true,
             nb::rv_policy::reference)
        .def("set_range",
             [](Vec2Pad& p, Vec2 min, Vec2 max) -> Vec2Pad& {
                 return p.set_range(min, max);
             },
             nb::arg("min"), nb::arg("max"), nb::rv_policy::reference,
             "Corner vectors: min corner, then max corner.")
        .def_prop_ro("value", &Vec2Pad::value);

    auto treeview = nb::class_<TreeView, Widget>(m, "TreeView");
    nb::class_<TreeView::Row>(treeview, "Row")
        .def(nb::init<std::string, int, bool>(), nb::arg("label"),
             nb::arg("depth") = 0, nb::arg("expandable") = false)
        .def_rw("label", &TreeView::Row::label)
        .def_rw("depth", &TreeView::Row::depth)
        .def_rw("expandable", &TreeView::Row::expandable);
    def_setter(treeview, "on_select", &TreeView::on_select);
    treeview.def(nb::init<std::string>(), nb::arg("id") = "")
        .def("set_rows",
             [](TreeView& t, std::vector<TreeView::Row> rows) -> TreeView& {
                 return t.set_rows(std::move(rows));
             },
             nb::arg("rows"), nb::rv_policy::reference)
        .def("set_expanded",
             [](TreeView& t, size_t index, bool open) -> TreeView& {
                 return t.set_expanded(index, open);
             },
             nb::arg("index"), nb::arg("open"), nb::rv_policy::reference)
        .def("expanded", &TreeView::expanded, nb::arg("index"))
        .def("set_selected",
             [](TreeView& t, long index) -> TreeView& {
                 return t.set_selected(index);
             },
             nb::arg("index"), nb::rv_policy::reference)
        .def_prop_ro("selected", &TreeView::selected)
        .def_prop_ro("visible_count", &TreeView::visible_count);

    auto table = nb::class_<Table, Widget>(m, "Table");
    nb::class_<Table::ColumnSpec>(table, "ColumnSpec")
        .def(nb::init<std::string, float, bool>(), nb::arg("title"),
             nb::arg("width") = 0.f, nb::arg("right_align") = false)
        .def_rw("title", &Table::ColumnSpec::title)
        .def_rw("width", &Table::ColumnSpec::width)
        .def_rw("right_align", &Table::ColumnSpec::right_align);
    def_setter(table, "on_select", &Table::on_select);
    table.def(nb::init<std::string>(), nb::arg("id") = "")
        .def("set_columns",
             [](Table& t, std::vector<Table::ColumnSpec> cols) -> Table& {
                 return t.set_columns(std::move(cols));
             },
             nb::arg("columns"), nb::rv_policy::reference)
        .def("set_rows",
             [](Table& t, std::vector<std::vector<std::string>> rows) -> Table& {
                 return t.set_rows(std::move(rows));
             },
             nb::arg("rows"), nb::rv_policy::reference)
        .def("set_selected",
             [](Table& t, long index) -> Table& { return t.set_selected(index); },
             nb::arg("index"), nb::rv_policy::reference)
        .def_prop_ro("selected", &Table::selected);

    auto propertygrid = nb::class_<PropertyGrid, Column>(m, "PropertyGrid");
    bind_add(propertygrid);
    propertygrid.def(nb::init<std::string>(), nb::arg("id") = "")
        .def("add_property",
             [](PropertyGrid& g, std::string label, float label_w) -> PropertyRow& {
                 return g.add_property(std::move(label), label_w);
             },
             nb::arg("label"), nb::arg("label_width") = 110.f,
             nb::rv_policy::reference_internal,
             "Returns the row — add the editor widget into it.")
        .def("add_section",
             [](PropertyGrid& g, std::string title, bool open) -> PropertyGrid& {
                 return g.add_section(std::move(title), open);
             },
             nb::arg("title"), nb::arg("open") = false,
             nb::rv_policy::reference_internal);

    auto propertyrow = nb::class_<PropertyRow, Widget>(m, "PropertyRow");
    bind_add(propertyrow); // so `row.add(slikui.Slider, ...)` builds editors
    propertyrow.def("label", [](PropertyRow& r) -> Label& { return r.label(); },
                    nb::rv_policy::reference_internal)
                 .def("editor", [](PropertyRow& r) -> Widget& { return r.editor(); },
                      nb::rv_policy::reference);

    auto toolbar = nb::class_<Toolbar, Row>(m, "Toolbar");
    toolbar.def(nb::init<std::string>(), nb::arg("id") = "")
        .def("add_button",
             [](Toolbar& t, Icon icon, std::function<void()> on_click,
                std::string tip) -> IconButton& {
                 return t.add_button(icon, std::move(on_click), std::move(tip));
             },
             nb::arg("icon"), nb::arg("on_click"), nb::arg("tip") = "",
             nb::rv_policy::reference_internal)
        .def("add_labeled_button",
             [](Toolbar& t, Icon icon, std::string label,
                std::function<void()> on_click, std::string tip) -> IconButton& {
                 return t.add_button(icon, std::move(label), std::move(on_click),
                                     std::move(tip));
             },
             nb::arg("icon"), nb::arg("label"), nb::arg("on_click"),
             nb::arg("tip") = "", nb::rv_policy::reference_internal)
        .def("add_separator", [](Toolbar& t) { t.add_separator(); })
        .def("set_icon_px",
             [](Toolbar& t, float px) -> Toolbar& { return t.set_icon_px(px); },
             nb::arg("px"), nb::rv_policy::reference)
        .def("set_background",
             [](Toolbar& t, Color c) -> Toolbar& { return t.set_background(c); },
             nb::arg("color"), nb::rv_policy::reference);

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

    auto splitter = nb::class_<Splitter, Widget>(m, "Splitter");
    splitter.def(nb::init<bool, std::string>(), nb::arg("vertical") = false,
                 nb::arg("id") = "")
        .def("set_fraction",
             [](Splitter& s, float f) -> Splitter& { return s.set_fraction(f); },
             nb::arg("fraction"), nb::rv_policy::reference)
        .def("on_change",
             [](Splitter& s, std::function<void(float)> f) -> Splitter& {
                 return s.on_change(std::move(f));
             },
             nb::arg("f"), nb::rv_policy::reference)
        .def_prop_ro("fraction", &Splitter::fraction);
    bind_add(splitter); // splitters host exactly two children

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
    // Shells (Phase D): dialogs, feedback, browsers, image views
    // ------------------------------------------------------------------ //

    // Shared (H, W, 4) uint8 RGBA argument unpacking for ImageView/CompareView.
    auto px_arg = [](nb::ndarray<nb::numpy, const std::uint8_t, nb::ndim<3>> arr,
                     int& w, int& h) -> const std::uint8_t* {
        if (arr.shape(2) != 4)
            throw nb::value_error("set_pixels expects (H, W, 4) uint8 RGBA");
        w = static_cast<int>(arr.shape(1));
        h = static_cast<int>(arr.shape(0));
        return arr.data();
    };

    auto dialog = nb::class_<Dialog, Panel>(m, "Dialog");
    dialog.def(nb::init<std::string, std::string>(), nb::arg("title"),
               nb::arg("id") = "")
        .def("content", [](Dialog& d) -> Column& { return d.content(); },
             nb::rv_policy::reference_internal,
             "The body column — add widgets here.")
        .def("set_title",
             [](Dialog& d, std::string t) -> Dialog& {
                 return d.set_title(std::move(t));
             },
             nb::arg("title"), nb::rv_policy::reference)
        .def_prop_ro("title",
                     [](Dialog& d) -> const std::string& { return d.title(); })
        .def("set_modal",
             [](Dialog& d, bool mod) -> Dialog& { return d.set_modal(mod); },
             nb::arg("modal"), nb::rv_policy::reference)
        .def_prop_ro("modal", &Dialog::modal)
        .def("set_position",
             [](Dialog& d, Vec2 p) -> Dialog& { return d.set_position(p); },
             nb::arg("pos"), nb::rv_policy::reference)
        .def("request_close", &Dialog::request_close)
        .def("on_close",
             [](Dialog& d, std::function<void()> f) -> Dialog& {
                 return d.on_close(std::move(f));
             },
             nb::arg("callback"), nb::rv_policy::reference)
        .def_prop_ro("close_button",
                     [](Dialog& d) -> IconButton& { return d.close_button(); },
                     nb::rv_policy::reference_internal);

    auto messagebox = nb::class_<MessageBox, Dialog>(m, "MessageBox");
    messagebox.def(nb::init<std::string, std::string, std::string>(),
                   nb::arg("title"), nb::arg("message"), nb::arg("id") = "")
        .def("add_button",
             [](MessageBox& mb, std::string label, std::function<void()> action) -> Button& {
                 return mb.add_button(std::move(label), std::move(action));
             },
             nb::arg("label"), nb::arg("action"), nb::rv_policy::reference_internal,
             "Right-aligned action button; the callback runs, then the "
             "message box closes.");

    auto groupbox = nb::class_<GroupBox, Panel>(m, "GroupBox");
    bind_add(groupbox);
    groupbox.def(nb::init<std::string, std::string>(), nb::arg("title"),
                 nb::arg("id") = "")
        .def("set_title",
             [](GroupBox& g, std::string t) -> GroupBox& {
                 return g.set_title(std::move(t));
             },
             nb::arg("title"), nb::rv_policy::reference)
        .def_prop_ro("title",
                     [](GroupBox& g) -> const std::string& { return g.title(); });

    auto searchbox = nb::class_<SearchBox, Widget>(m, "SearchBox");
    def_setter(searchbox, "on_change", &SearchBox::on_change);
    def_setter(searchbox, "on_submit", &SearchBox::on_submit);
    searchbox.def(nb::init<std::string>(), nb::arg("id") = "")
        .def("set_text",
             [](SearchBox& b, std::string t) -> SearchBox& {
                 return b.set_text(std::move(t));
             },
             nb::arg("text"), nb::rv_policy::reference)
        .def_prop_ro("text",
                     [](SearchBox& b) -> const std::string& { return b.text(); })
        .def("set_placeholder",
             [](SearchBox& b, std::string p) -> SearchBox& {
                 return b.set_placeholder(std::move(p));
             },
             nb::arg("placeholder"), nb::rv_policy::reference)
        .def("field", [](SearchBox& b) -> TextField& { return b.field(); },
             nb::rv_policy::reference_internal)
        .def_prop_ro("clear_button",
                     [](SearchBox& b) -> IconButton& { return b.clear_button(); },
                     nb::rv_policy::reference_internal);

    auto statusbar = nb::class_<StatusBar, Row>(m, "StatusBar");
    bind_add(statusbar); // right-side widgets: bar.add(slikui.ProgressBar, ...)
    statusbar.def(nb::init<std::string>(), nb::arg("id") = "")
        .def("set_text",
             [](StatusBar& s, std::string t) -> StatusBar& {
                 return s.set_text(std::move(t));
             },
             nb::arg("text"), nb::rv_policy::reference)
        .def_prop_ro("text",
                     [](StatusBar& s) -> const std::string& { return s.text(); })
        .def("show_message",
             [](StatusBar& s, std::string msg, double seconds) -> StatusBar& {
                 return s.show_message(std::move(msg), seconds);
             },
             nb::arg("message"), nb::arg("seconds") = 3.0,
             nb::rv_policy::reference,
             "Transient message; the base text returns once it expires.");

    auto logview = nb::class_<LogView, Widget>(m, "LogView");
    nb::enum_<LogView::Level>(logview, "Level")
        .value("Info", LogView::Level::Info)
        .value("Success", LogView::Level::Success)
        .value("Warning", LogView::Level::Warning)
        .value("Error", LogView::Level::Error);
    logview.def(nb::init<std::string>(), nb::arg("id") = "")
        .def("log",
             [](LogView& l, LogView::Level level, std::string text) -> LogView& {
                 return l.log(level, std::move(text));
             },
             nb::arg("level"), nb::arg("text"), nb::rv_policy::reference)
        .def("clear",
             [](LogView& l) -> LogView& { return l.clear(); },
             nb::rv_policy::reference)
        .def_prop_ro("line_count", &LogView::line_count);

    nb::class_<Toast, Widget>(m, "Toast")
        .def(nb::init<std::string, std::string>(), nb::arg("text"),
             nb::arg("id") = "")
        .def("set_icon",
             [](Toast& t, Icon i) -> Toast& { return t.set_icon(i); },
             nb::arg("icon"), nb::rv_policy::reference,
             "Usually pushed with Context.push_toast, which owns the toast.");

    auto filebrowser = nb::class_<FileBrowser, Dialog>(m, "FileBrowser");
    nb::enum_<FileBrowser::Mode>(filebrowser, "Mode")
        .value("Open", FileBrowser::Mode::Open)
        .value("Save", FileBrowser::Mode::Save);
    def_setter(filebrowser, "on_pick", &FileBrowser::on_pick);
    filebrowser.def(nb::init<FileBrowser::Mode, std::string, std::string>(),
                    nb::arg("mode"), nb::arg("title") = "", nb::arg("id") = "")
        .def("set_path",
             [](FileBrowser& fb, std::string dir) -> FileBrowser& {
                 return fb.set_path(std::move(dir));
             },
             nb::arg("dir"), nb::rv_policy::reference)
        .def_prop_ro("path", [](FileBrowser& fb) { return fb.path(); })
        .def("selection",
             [](FileBrowser& fb) { return fb.selection(); },
             "path()/selection(): current directory and the picked file ('' "
             "when nothing is chosen).")
        .def_prop_ro("list", [](FileBrowser& fb) -> ListView& { return fb.list(); },
                     nb::rv_policy::reference_internal)
        .def_prop_ro("name_field",
                     [](FileBrowser& fb) -> TextField& { return fb.name_field(); },
                     nb::rv_policy::reference_internal)
        .def_prop_ro("open_button",
                     [](FileBrowser& fb) -> Button& { return fb.open_button(); },
                     nb::rv_policy::reference_internal);

    auto imageview = nb::class_<ImageView, Widget>(m, "ImageView");
    imageview.def(nb::init<std::string>(), nb::arg("id") = "")
        .def("set_pixels",
             [px_arg](ImageView& v,
                      nb::ndarray<nb::numpy, const std::uint8_t, nb::ndim<3>> arr) -> ImageView& {
                 int w = 0, h = 0;
                 const std::uint8_t* px = px_arg(arr, w, h);
                 return v.set_pixels(w, h, px);
             },
             nb::arg("pixels"), nb::rv_policy::reference)
        .def("set_file", &ImageView::set_file, nb::arg("path"))
        .def_prop_ro("zoom", &ImageView::zoom)
        .def_prop_ro("pan", &ImageView::pan)
        .def_prop_ro("width", &ImageView::width)
        .def_prop_ro("height", &ImageView::height);

    auto compareview = nb::class_<CompareView, Widget>(m, "CompareView");
    compareview.def(nb::init<std::string>(), nb::arg("id") = "")
        .def("set_pixels_a",
             [px_arg](CompareView& v,
                      nb::ndarray<nb::numpy, const std::uint8_t, nb::ndim<3>> arr) -> CompareView& {
                 int w = 0, h = 0;
                 const std::uint8_t* px = px_arg(arr, w, h);
                 return v.set_pixels_a(w, h, px);
             },
             nb::arg("pixels"), nb::rv_policy::reference,
             "Both images stretch over the full view; A fills [0, fraction].")
        .def("set_pixels_b",
             [px_arg](CompareView& v,
                      nb::ndarray<nb::numpy, const std::uint8_t, nb::ndim<3>> arr) -> CompareView& {
                 int w = 0, h = 0;
                 const std::uint8_t* px = px_arg(arr, w, h);
                 return v.set_pixels_b(w, h, px);
             },
             nb::arg("pixels"), nb::rv_policy::reference)
        .def("set_fraction",
             [](CompareView& v, float f) -> CompareView& {
                 return v.set_fraction(f);
             },
             nb::arg("fraction"), nb::rv_policy::reference)
        .def_prop_ro("fraction", &CompareView::fraction);

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

    m.def("shortcut_label",
          [](Key key, unsigned mods) { return shortcut_label(key, mods); },
          nb::arg("key"), nb::arg("mods"),
          "\"Ctrl+Shift+S\" (\"\u2318\u21e7S\" on Apple) — for menu hints.");
    m.def("find_system_font", &find_system_font,
          "First system TTF that opens (best-effort; '' if none).");
}
