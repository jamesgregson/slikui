// examples/05_showcase/main.cpp — every widget, live, in one window.
//
// The interactive sibling of 03_gallery: each component prints its state
// changes into the on-screen LogView as you use it, so the log doubles as a
// trace of what you touched. Layout:
//
//     MenuBar ─ Toolbar ─ [ log panel | tabbed pages ] ─ StatusBar
//
// The tabbed pages cover the whole component set (controls, inputs, data,
// containers, media, custom); Tabs only carry labels, so pages are a column
// of Panels with visibility toggled from the Tabs callback.
//
// Two custom widgets exercise the measure/paint/on_event/tick contract:
//   Fill       — expands on the main axis and delegates to its one child
//                (the trick that lets the splitter fill the window).
//   LevelMeter — LED bar meter: click/drag to scrub, peak-hold marker,
//                green→amber→red gradient, warns above 90%.
//
// Modes:
//   ./slikui_showcase          interactive (GLFW + OpenGL, like 03_gallery)
//   ./slikui_showcase --smoke [out.ppm]
//                              headless: a scripted pass over every component
//                              (real synthetic mouse/key/scroll events), then
//                              prints the log transcript and a screenshot.
//                              Convert the PPM: python3 tools/ppm2png.py.
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h> // GL first: silenced deprecations, no legacy gl.h

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <slikui/backends/glfw/glfw_window.hpp>
#include <slikui/backends/opengl/opengl.hpp>
#include <slikui/backends/software/software.hpp>
#include <slikui/slikui.hpp>
#include <slikui/text/system_fonts.hpp>

using namespace slikui;

// ---------------------------------------------------------------------------
// Custom widget 1: Fill — a plain Widget that expands on the main axis and
// hands its whole area to a single child. Column only gives leftover height
// to children whose expands_main() is true; this wrapper is the honest way
// to get a splitter (or any natural-height composite) to fill the window.
// ---------------------------------------------------------------------------
class Fill : public Widget {
public:
    bool expands_main() const override { return true; }

    Size measure(const Constraints& c) override {
        return children.empty() ? Size{0, 0} : children[0]->measure(c);
    }
    void arrange(const Rect& area) override {
        bounds = area;
        if (!children.empty()) children[0]->arrange(area);
    }
    // paint/on_event/tick inherited: paint_tree, hit-testing and tick_tree
    // all recurse through children, so the wrapped subtree stays fully live.
};

// ---------------------------------------------------------------------------
// Custom widget 2: LevelMeter — LED bar with click/drag scrubbing, a peak
// marker, and a gradient from success through warning to danger.
// ---------------------------------------------------------------------------
class LevelMeter : public Widget {
public:
    explicit LevelMeter(std::string widget_id = {}) : Widget(std::move(widget_id)) {}

    LevelMeter& on_change(std::function<void(float)> f) {
        on_change_ = std::move(f);
        return *this;
    }
    LevelMeter& set_value(float v) {
        value_ = std::clamp(v, 0.f, 1.f);
        peak_ = std::fmax(peak_, value_);
        return *this;
    }
    float value() const { return value_; }

    Size measure(const Constraints& c) override { return c.clamp({240.f, 26.f}); }
    bool stretch_cross() const override { return false; }

    void tick(Time t) override {
        // Peak holds while rising, decays ~0.25/s otherwise.
        peak_ = value_ >= peak_ ? value_ : std::fmax(value_, peak_ - 0.25f * t.dt);
    }

    void paint(const PaintEnv& env) const override {
        const Theme& th = env.theme;
        constexpr int kSeg = 16;
        constexpr float kGap = 2.f;
        float bw = (bounds.w - kGap * (kSeg - 1)) / float(kSeg);
        for (int i = 0; i < kSeg; ++i) {
            float t = float(i) / float(kSeg - 1);
            Rect b{bounds.x + i * (bw + kGap), bounds.y, bw, bounds.h};
            bool lit = value_ >= (float(i) + 0.5f) / float(kSeg);
            Color c = th.success.lerp(th.warning, std::clamp(t * 2.f, 0.f, 1.f))
                          .lerp(th.danger, std::clamp(t * 2.f - 1.f, 0.f, 1.f));
            env.list.rect(b, lit ? c : th.surface_alt, 2.f);
        }
        if (peak_ > 0.01f) { // peak-hold marker
            float px = bounds.x + std::clamp(peak_, 0.f, 1.f) * (bounds.w - 2.f);
            env.list.rect({px, bounds.y, 2.f, bounds.h}, th.text, 0.f);
        }
    }

    bool on_event(const Event& e) override {
        if (e.type == Event::Type::MousePress && e.button == int(MouseButton::MouseLeft)
            && bounds.contains(e.pos)) {
            dragging_ = true;
            context()->capture_mouse(this);
            apply(e.pos.x);
            return true;
        }
        if (e.type == Event::Type::MouseMove && dragging_) {
            apply(e.pos.x);
            return true;
        }
        if (e.type == Event::Type::MouseRelease && dragging_) {
            dragging_ = false;
            context()->release_mouse(this);
            if (on_change_) on_change_(value_);
            return true;
        }
        return false;
    }

private:
    void apply(float x) {
        value_ = std::clamp((x - bounds.x) / std::fmax(bounds.w, 1.f), 0.f, 1.f);
        peak_ = std::fmax(peak_, value_);
        if (on_change_) on_change_(value_);
    }

    std::function<void(float)> on_change_;
    float value_ = 0.4f, peak_ = 0.4f;
    bool dragging_ = false;
};

// ---------------------------------------------------------------------------
// Demo plumbing: every state change flows through say() — into the LogView
// and into a transcript the smoke mode prints.
// ---------------------------------------------------------------------------
struct Demo {
    LogView* log = nullptr;
    StatusBar* status = nullptr;
    Label* count = nullptr;
    std::vector<std::string> transcript;
    int events = 0;

    void say(std::string msg, LogView::Level level = LogView::Level::Info) {
        if (log) log->log(level, msg);
        const char* tag = level == LogView::Level::Success ? "ok  "
                        : level == LogView::Level::Warning ? "warn"
                        : level == LogView::Level::Error   ? "err "
                                                           : "info";
        transcript.push_back(std::string("[") + tag + "] " + msg);
        ++events;
        if (count) count->set_text(std::to_string(events) + " events");
    }
};

// Synthetic-input helpers for the smoke script (bounds are live per tick).
static Vec2 center(const Widget& w) {
    return {w.bounds.x + w.bounds.w / 2.f, w.bounds.y + w.bounds.h / 2.f};
}
static Event mouse(Event::Type t, Vec2 p, int button = int(MouseButton::MouseLeft)) {
    Event e{};
    e.type = t;
    e.pos = p;
    e.button = button;
    return e;
}
static void click(Context& ctx, const Widget& w) {
    Vec2 p = center(w);
    ctx.handle(mouse(Event::Type::MousePress, p));
    ctx.handle(mouse(Event::Type::MouseRelease, p));
}
static void click_at(Context& ctx, Vec2 p) {
    ctx.handle(mouse(Event::Type::MousePress, p));
    ctx.handle(mouse(Event::Type::MouseRelease, p));
}
static void drag(Context& ctx, Vec2 from, Vec2 to) {
    ctx.handle(mouse(Event::Type::MousePress, from));
    ctx.handle(mouse(Event::Type::MouseMove, to));
    ctx.handle(mouse(Event::Type::MouseRelease, to));
}
static void wheel(Context& ctx, Vec2 p, float dy) {
    Event e{};
    e.type = Event::Type::Scroll;
    e.pos = p;
    e.scroll = {0.f, dy};
    ctx.handle(e);
}
static void type(Context& ctx, const char* s) {
    for (const char* c = s; *c; ++c) {
        Event e{};
        e.type = Event::Type::TextInput;
        e.codepoint = std::uint32_t(*c);
        ctx.handle(e);
    }
}
static void key(Context& ctx, Key k, std::uint32_t mods = ModNone) {
    Event e{};
    e.type = Event::Type::KeyPress;
    e.key = k;
    e.mods = mods;
    ctx.handle(e);
}

// ---------------------------------------------------------------------------
// UI construction. Shared by interactive and smoke modes.
// ---------------------------------------------------------------------------
struct UI {
    Demo demo;

    Tabs* tabs = nullptr;
    std::vector<Panel*> pages;

    Button *btn_ping = nullptr, *btn_danger = nullptr;
    Checkbox* checkbox = nullptr;
    std::vector<Radio*> radios;
    Switch* sw = nullptr;
    Slider* slider = nullptr;
    Segmented* segmented = nullptr;
    ProgressBar* progress = nullptr;

    TextField* field = nullptr;
    SearchBox* search = nullptr;
    Dropdown* dropdown = nullptr;
    DragValue* dv = nullptr;
    VecNEdit<2>* vec2 = nullptr;
    float vec_vals[2] = {1.f, 2.f};
    ColorEdit* color = nullptr;

    ListView* list = nullptr;
    TreeView* tree = nullptr;
    Table* table = nullptr;
    Vec2Pad* pad = nullptr;
    Slider* prop_opacity = nullptr;

    Expander *acc_geo = nullptr, *acc_sh = nullptr;
    Expander* expander = nullptr;
    Splitter* vsplit = nullptr;
    ScrollArea* scroll = nullptr;

    ImageView* image_view = nullptr;
    CompareView* compare = nullptr;

    LevelMeter* meter = nullptr;
    std::vector<Button*> meter_set_buttons;
    FileBrowser* file_dialog = nullptr;

    bool playing = true;
    double plot_phase = 0.0, plot_next = 0.0;
    int plot_logged = 0;
    std::vector<float> samples;
    std::function<void()> open_about, open_file_dialog, save;
};

static void apply_page(UI& ui, size_t i) {
    for (size_t k = 0; k < ui.pages.size(); ++k) ui.pages[k]->visible = (k == i);
}

static const char* kTabNames[] = {"Controls", "Inputs", "Data", "Containers",
                                  "Media", "Custom"};

static void build_ui(Context& ctx, UI& ui) {
    Demo& demo = ui.demo;

    auto& root = ctx.set_root<Column>();
    root.set_padding(0).set_gap(0);

    // ----- menu bar -------------------------------------------------------
    auto& bar = root.add<MenuBar>();
    auto& mfile = bar.add_menu("File");
    mfile.add_item("New", [&] { demo.say("menu File → New"); }, "Ctrl+N");
    mfile.add_item("Open…", [&] { if (ui.open_file_dialog) ui.open_file_dialog(); },
                   "Ctrl+O");
    mfile.add_item("Save", [&] { if (ui.save) ui.save(); }, "Ctrl+S");
    mfile.add_separator();
    mfile.add_item("Exit", [&] { demo.say("menu File → Exit (close the window)"); });
    auto& mview = bar.add_menu("View");
    auto& appearance = mview.add_submenu("Appearance", std::make_unique<Menu>("Appearance"));
    for (size_t i = 0; i < theme_presets().size(); ++i) {
        std::string name = theme_presets()[i].name;
        appearance.add_item(name, [&, i, name] {
            apply_preset(ctx.theme(), i);
            demo.say("theme → '" + name + "'");
        });
    }
    mview.add_separator();
    mview.add_checkable("Play samples", [&] { return ui.playing; }, [&] {
        ui.playing = !ui.playing;
        demo.say(std::string("menu View → play samples ")
                 + (ui.playing ? "on" : "off"));
    });
    auto& mhelp = bar.add_menu("Help");
    mhelp.add_item("About", [&] { if (ui.open_about) ui.open_about(); });

    // ----- toolbar --------------------------------------------------------
    auto& tb = root.add<Toolbar>();
    tb.add_button(Icon::Play, [&] {
        ui.playing = true;
        demo.say("toolbar → play");
    }, "Play the live plot + progress");
    tb.add_button(Icon::Pause, [&] {
        ui.playing = false;
        demo.say("toolbar → pause");
    }, "Pause the live plot + progress");
    tb.add_button(Icon::Stop, [&] {
        ui.playing = false;
        ui.samples.assign(24, 0.35f);
        demo.say("toolbar → stop (plot reset)");
    }, "Stop and reset the plot");
    tb.add_separator();
    tb.add_button(Icon::Folder, [&] { if (ui.open_file_dialog) ui.open_file_dialog(); },
                  "Open a file (real std::filesystem browser)");
    tb.add_button(Icon::Save, [&] { if (ui.save) ui.save(); }, "Save (toast + status)");
    tb.add_button(Icon::Camera, [&] {
        auto t = std::make_unique<Toast>("Screenshot saved to /tmp");
        t->set_icon(Icon::Camera);
        ctx.push_toast(std::move(t), 3.0);
        demo.say("toast → 'Screenshot saved to /tmp'");
    }, "Push a toast");
    tb.add_button(Icon::Eye, [&] { if (ui.open_about) ui.open_about(); }, "About");
    tb.add_button(Icon::Trash, [&] {
        if (ui.demo.log) ui.demo.log->clear();
        demo.say("log cleared");
    }, "Clear the event log");
    tb.add<Spacer>();
    tb.add<Label>("everything you touch logs to the panel on the left")
        .set_color(Color::from_hex(0x8A94A0))
        .set_font_size(13);

    // ----- main area: log | tabbed pages ----------------------------------
    auto& split = root.add<Fill>().add<Splitter>();
    split.set_fraction(0.34f);

    auto& log_panel = split.add<Panel>();
    log_panel.set_padding(10).set_gap(6);
    auto& log_head = log_panel.add<Row>().set_gap(8);
    log_head.add<Label>("event log").set_font_size(15);
    log_head.add<Spacer>();
    log_head.add<Label>("live — every component reports here")
        .set_color(Color::from_hex(0x8A94A0))
        .set_font_size(12);
    auto& logview = log_panel.add<LogView>();
    demo.log = &logview;

    auto& right = split.add<Column>();
    right.set_padding(10).set_gap(10);
    auto& tabs = right.add<Tabs>();
    tabs.set_tabs({kTabNames[0], kTabNames[1], kTabNames[2], kTabNames[3],
                   kTabNames[4], kTabNames[5]});
    ui.tabs = &tabs;
    auto& page_host = right.add<Fill>().add<Column>();
    page_host.set_gap(10);

    // --- page 1: Controls -------------------------------------------------
    auto& p1 = page_host.add<Panel>();
    p1.set_padding(12).set_gap(10);
    auto& g_btn = p1.add<GroupBox>("buttons");
    auto& btn_row = g_btn.add<Row>().set_gap(8);
    ui.btn_ping = &btn_row.add<Button>("Ping").on_click([&] {
        demo.say("button 'Ping' clicked");
    });
    btn_row.add<Button>("Primary")
        .set_variant(Button::Variant::Primary)
        .on_click([&] { demo.say("button 'Primary' clicked"); });
    ui.btn_danger = &btn_row.add<Button>("Cause error").on_click([&] {
        demo.say("button 'Cause error' clicked — boom", LogView::Level::Error);
    });
    btn_row.add<Button>("Ghost")
        .set_variant(Button::Variant::Ghost)
        .on_click([&] { demo.say("button 'Ghost' clicked"); });
    auto& g_sel = p1.add<GroupBox>("toggles");
    auto& sel_row = g_sel.add<Row>().set_gap(16);
    ui.checkbox = &sel_row.add<Checkbox>("autosave", true).on_change([&](bool v) {
        demo.say(std::string("checkbox autosave → ") + (v ? "on" : "off"));
    });
    for (int i = 0; i < 3; ++i) {
        auto& r = sel_row.add<Radio>("pick " + std::to_string(i + 1), i == 0);
        ui.radios.push_back(&r);
        r.on_change([&ui, &demo, i](bool on) {
            if (!on) return;
            for (auto* r2 : ui.radios) r2->set_checked(r2 == ui.radios[i]);
            demo.say("radio → pick " + std::to_string(i + 1));
        });
    }
    ui.sw = &sel_row.add<Switch>();
    ui.sw->on_change([&demo](bool v) {
        demo.say(std::string("switch → ") + (v ? "on" : "off"));
    });
    auto& g_rng = p1.add<GroupBox>("ranges");
    auto& rng_row = g_rng.add<Row>().set_gap(16);
    ui.slider = &rng_row.add<Slider>(0.f, 1.f);
    ui.slider->set_label("gain").set_format("%.2f").set_value(0.35f);
    ui.slider->on_change([&demo](float v) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.2f", double(v));
        demo.say("slider gain → " + std::string(buf));
    });
    ui.progress = &rng_row.add<ProgressBar>();
    auto& rng_row2 = g_rng.add<Row>().set_gap(16);
    rng_row2.add<Spinner>().set_speed(2.5f);
    ui.segmented = &rng_row2.add<Segmented>();
    ui.segmented->set_items({"line", "tree", "grid"}).set_selected(0);
    ui.segmented->on_select([&demo](size_t i) {
        demo.say("segmented → " + std::vector<std::string>{"line", "tree", "grid"}[i]);
    });
    ui.pages.push_back(&p1);

    // --- page 2: Inputs ---------------------------------------------------
    auto& p2 = page_host.add<Panel>();
    p2.set_padding(12).set_gap(10);
    auto& g_text = p2.add<GroupBox>("text");
    ui.field = &g_text.add<TextField>().set_placeholder("type here, Enter submits");
    ui.field->on_change([&demo](std::string_view s) {
        demo.say("text → '" + std::string(s) + "'");
    });
    ui.field->on_submit([&demo](std::string_view s) {
        demo.say("text submit → '" + std::string(s) + "'", LogView::Level::Success);
    });
    g_text.add<TextArea>().set_placeholder("multiline…").set_rows(3).on_change(
        [&demo](std::string_view s) {
            demo.say("area → " + std::to_string(s.size()) + " chars");
        });
    ui.search = &g_text.add<SearchBox>().set_placeholder("search…");
    ui.search->on_change([&demo](std::string_view s) {
        demo.say("search → '" + std::string(s) + "'");
    });
    ui.search->on_submit([&demo](std::string_view s) {
        demo.say("search submit → '" + std::string(s) + "'");
    });
    auto& g_val = p2.add<GroupBox>("values");
    auto& val_row = g_val.add<Row>().set_gap(14);
    ui.dropdown = &val_row.add<Dropdown>();
    ui.dropdown->set_items({"Nearest", "Linear", "Cubic"}).set_placeholder("filter…");
    ui.dropdown->on_select([&demo](size_t i) {
        demo.say("dropdown → "
                 + std::vector<std::string>{"Nearest", "Linear", "Cubic"}[i]);
    });
    ui.dv = &val_row.add<DragValue>(1.f);
    ui.dv->set_range(0.f, 100.f).set_speed(0.6f).set_suffix(" fps");
    ui.dv->on_change([&demo](float v) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.1f", double(v));
        demo.say("dragvalue → " + std::string(buf));
    });
    ui.vec2 = &val_row.add<VecNEdit<2>>(ui.vec_vals, "xy");
    ui.vec2->set_range(-10.f, 10.f);
    ui.vec2->on_change([&ui, &demo] {
        char buf[64];
        std::snprintf(buf, sizeof buf, "(%.1f, %.1f)", double(ui.vec_vals[0]),
                      double(ui.vec_vals[1]));
        demo.say("vec2 → " + std::string(buf));
    });
    auto& g_col = p2.add<GroupBox>("color");
    auto& col_row = g_col.add<Row>().set_gap(14);
    ui.color = &col_row.add<ColorEdit>();
    ui.color->set_color(Color::from_hex(0x4C8DFF));
    ui.color->on_change([&demo](Color c) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "#%02X%02X%02X", c.r, c.g, c.b);
        demo.say("color → " + std::string(buf));
    });
    col_row.add<Label>("click the swatch for the picker popup")
        .set_color(Color::from_hex(0x8A94A0))
        .set_font_size(12);
    ui.pages.push_back(&p2);

    // --- page 3: Data -----------------------------------------------------
    auto& p3 = page_host.add<Panel>();
    p3.set_padding(12).set_gap(10);
    auto& plot = p3.add<Plot>();
    plot.set_fill(true);
    ui.samples.assign(24, 0.35f);
    plot.set_series(ui.samples, Color::from_hex(0x4C8DFF));
    plot.set_tooltip("live series — press play in the toolbar");
    auto& data_row = p3.add<Row>().set_gap(10);
    ui.tree = &data_row.add<TreeView>();
    ui.tree->set_rows({
        {"slikui/", 0, true},
        {"src/", 1, true},
        {"main.cpp", 2, false},
        {"widgets/", 1, true},
        {"controls.hpp", 2, false},
        {"shells.hpp", 2, false},
        {"docs/", 1, true},
        {"widgets-roadmap.md", 2, false},
    });
    ui.tree->set_expanded(0, true).set_expanded(1, true).set_expanded(3, true);
    ui.tree->on_select([&demo](size_t i) {
        demo.say("tree → " + std::vector<std::string>{
                                  "slikui/", "src/", "main.cpp", "widgets/",
                                  "controls.hpp", "shells.hpp", "docs/",
                                  "widgets-roadmap.md"}[i]);
    });
    ui.table = &data_row.add<Table>();
    ui.table->set_columns({{"file", 130.f}, {"checks", 70.f, true}, {"status", 80.f}});
    ui.table->set_rows({
        {"controls.hpp", "488", "ok"},
        {"shells.hpp", "73", "ok"},
        {"views.hpp", "31", "ok"},
        {"menus.hpp", "18", "warn"},
    });
    ui.table->on_select([&demo](size_t i) {
        demo.say("table → row " + std::to_string(i) + " ("
                 + std::vector<std::string>{"controls.hpp", "shells.hpp",
                                            "views.hpp", "menus.hpp"}[i]
                 + ")");
    });
    auto& data_row2 = p3.add<Row>().set_gap(10);
    ui.list = &data_row2.add<ListView>();
    ui.list->set_items({"alpha", "beta", "gamma", "delta"});
    ui.list->on_select([&demo](size_t i) {
        demo.say("list → "
                 + std::vector<std::string>{"alpha", "beta", "gamma", "delta"}[i]);
    });
    auto& grid = data_row2.add<PropertyGrid>();
    ui.prop_opacity = &grid.add_property("opacity").add<Slider>(0.f, 1.f);
    ui.prop_opacity->set_value(0.75f);
    ui.prop_opacity->on_change([&demo](float v) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.2f", double(v));
        demo.say("property opacity → " + std::string(buf));
    });
    auto& adv = grid.add_section("advanced", true);
    auto& dv2 = adv.add_property("threads").add<DragValue>(4.f);
    dv2.set_range(1.f, 64.f).set_int_mode(true);
    dv2.on_change([&demo](float v) {
        demo.say("property threads → " + std::to_string(int(v)));
    });
    auto& pad_row = p3.add<Row>().set_gap(14);
    ui.pad = &pad_row.add<Vec2Pad>();
    ui.pad->set_range(Vec2{0.f, 0.f}, Vec2{1.f, 1.f});
    ui.pad->set_value(Vec2{0.3f, 0.6f});
    ui.pad->on_change([&demo](Vec2 v) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "(%.2f, %.2f)", double(v.x), double(v.y));
        demo.say("pad → " + std::string(buf));
    });
    pad_row.add<Label>("Vec2Pad — drag the dot")
        .set_color(Color::from_hex(0x8A94A0))
        .set_font_size(12);
    ui.pages.push_back(&p3);

    // --- page 4: Containers ----------------------------------------------
    auto& p4 = page_host.add<Panel>();
    p4.set_padding(12).set_gap(10);
    auto& c_row = p4.add<Row>().set_gap(14);
    auto& c_left = c_row.add<Column>().set_gap(8);
    auto& accordion = c_left.add<Accordion>().set_exclusive(true);
    auto& sec_geo = accordion.add_section("geometry", true);
    sec_geo.add<Label>("vertex buffer contents…");
    auto& sec_sh = accordion.add_section("shaders");
    sec_sh.add<Label>("glsl sources…");
    ui.acc_geo = &sec_geo;
    ui.acc_sh = &sec_sh;
    sec_geo.on_toggle([&demo](bool opening) {
        demo.say(std::string("accordion → 'geometry' ")
                 + (opening ? "opened" : "closed"));
    });
    sec_sh.on_toggle([&demo](bool opening) {
        demo.say(std::string("accordion → 'shaders' ")
                 + (opening ? "opened" : "closed"));
    });
    ui.expander = &c_left.add<Expander>("more options");
    ui.expander->add<Checkbox>("nested checkbox", false);
    ui.expander->on_toggle([&demo](bool opening) {
        demo.say(std::string("expander → ") + (opening ? "opened" : "closed"));
    });
    auto& demo_panel = c_left.add<Panel>();
    demo_panel.set_background(Color::from_hex(0x223344)).set_radius(8);
    demo_panel.add<Label>("a Panel inside the flow");
    auto& c_right = c_row.add<Column>().set_gap(8);
    ui.vsplit = &c_right.add<Splitter>(true); // vertical: upper | lower panes
    ui.vsplit->set_fraction(0.5f);
    auto& upper = ui.vsplit->add<Column>().set_gap(2);
    upper.add<Label>("upper pane").set_color(Color::from_hex(0x77C7FF));
    upper.add<Label>("drag the divider").set_color(Color::from_hex(0x44515F));
    upper.add<Label>("…it logs here").set_color(Color::from_hex(0x44515F));
    auto& lower = ui.vsplit->add<Column>().set_gap(2);
    lower.add<Label>("lower pane").set_color(Color::from_hex(0xFFD377));
    lower.add<Label>("both panes stay live").set_color(Color::from_hex(0x44515F));
    lower.add<Label>("fraction → log").set_color(Color::from_hex(0x44515F));
    ui.vsplit->on_change([&demo](float f) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.2f", double(f));
        demo.say("splitter → " + std::string(buf));
    });
    ui.scroll = &c_right.add<ScrollArea>();
    ui.scroll->hint_w_ = 240;
    ui.scroll->hint_h_ = 110;
    for (int i = 0; i < 10; ++i)
        ui.scroll->add<Label>("scrollable row " + std::to_string(i));
    ui.pages.push_back(&p4);

    // --- page 5: Media ----------------------------------------------------
    auto& p5 = page_host.add<Panel>();
    p5.set_padding(12).set_gap(10);
    auto& m_row = p5.add<Row>().set_gap(14);
    auto& m_left = m_row.add<Column>().set_gap(6);
    m_left.add<Label>("ImageView — wheel zooms at the cursor, drag pans")
        .set_color(Color::from_hex(0x8A94A0))
        .set_font_size(12);
    ui.image_view = &m_left.add<ImageView>();
    {
        const int w = 96, h = 72;
        std::vector<std::uint8_t> px(size_t(w) * h * 4);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                bool chk = ((x / 8) + (y / 8)) % 2 == 0;
                auto* p = &px[(size_t(y) * w + x) * 4];
                p[0] = std::uint8_t(chk ? 60 + x : 90 + x);
                p[1] = std::uint8_t(chk ? 60 + y : 90 + y);
                p[2] = 140;
                p[3] = 255;
            }
        ui.image_view->set_pixels(w, h, px.data());
    }
    auto& m_right = m_row.add<Column>().set_gap(6);
    m_right.add<Label>("Image — plain, Contain fit")
        .set_color(Color::from_hex(0x8A94A0))
        .set_font_size(12);
    auto& img = m_right.add<Image>("gradient");
    {
        const int w = 64, h = 48;
        std::vector<std::uint8_t> px(size_t(w) * h * 4);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                auto* p = &px[(size_t(y) * w + x) * 4];
                p[0] = std::uint8_t(x * 255 / (w - 1));
                p[1] = std::uint8_t(y * 255 / (h - 1));
                p[2] = 128;
                p[3] = 255;
            }
        img.set_pixels(w, h, px.data()).set_size(96, 72).set_fit(Image::Fit::Contain);
    }
    m_right.add<Label>("CompareView — drag the A|B divider")
        .set_color(Color::from_hex(0x8A94A0))
        .set_font_size(12);
    ui.compare = &m_right.add<CompareView>();
    {
        const int w = 64, h = 48;
        std::vector<std::uint8_t> pa(size_t(w) * h * 4), pb(size_t(w) * h * 4);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                auto* a = &pa[(size_t(y) * w + x) * 4];
                auto* b = &pb[(size_t(y) * w + x) * 4];
                a[0] = std::uint8_t(40 + x), a[1] = 70, a[2] = 160, a[3] = 255;
                b[0] = 160, b[1] = std::uint8_t(120 - y), b[2] = 60, b[3] = 255;
            }
        ui.compare->set_pixels_a(w, h, pa.data());
        ui.compare->set_pixels_b(w, h, pb.data());
        ui.compare->set_fraction(0.5f);
    }
    ui.pages.push_back(&p5);

    // --- page 6: Custom ---------------------------------------------------
    auto& p6 = page_host.add<Panel>();
    p6.set_padding(12).set_gap(10);
    p6.add<Label>("LevelMeter — a custom widget (direct subclass of Widget)")
        .set_font_size(15);
    p6.add<Label>("click or drag to scrub · peak marker decays · >90% logs a warning")
        .set_color(Color::from_hex(0x8A94A0))
        .set_font_size(12);
    ui.meter = &p6.add<LevelMeter>();
    ui.meter->set_value(0.4f);
    ui.meter->on_change([&demo](float v) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.2f", double(v));
        if (v > 0.9f)
            demo.say("levelmeter → " + std::string(buf) + " (clip!)",
                     LogView::Level::Warning);
        else
            demo.say("levelmeter → " + std::string(buf));
    });
    auto& m_btns = p6.add<Row>().set_gap(8);
    for (float v : {0.25f, 0.5f, 0.75f, 1.0f}) {
        float val = v;
        std::string label = std::to_string(int(v * 100)) + "%";
        auto& b = m_btns.add<Button>(label).on_click([&ui, &demo, val, label] {
            ui.meter->set_value(val);
            if (val > 0.9f)
                demo.say("levelmeter set → " + label + " (clip!)",
                         LogView::Level::Warning);
            else
                demo.say("levelmeter set → " + label);
        });
        ui.meter_set_buttons.push_back(&b);
    }
    ui.pages.push_back(&p6);

    apply_page(ui, 0);
    tabs.on_change([&ui, &demo](size_t i) {
        apply_page(ui, i);
        demo.say(std::string("tabs → ") + kTabNames[i]);
    });

    // ----- status bar -----------------------------------------------------
    auto& status = root.add<StatusBar>();
    status.set_text("showcase ready — every component is live");
    ui.demo.status = &status;
    status.add<Spinner>().set_speed(1.5f);
    auto& count = status.add<Label>("0 events");
    count.set_color(Color::from_hex(0x8A94A0)).set_font_size(12);
    ui.demo.count = &count;

    // ----- shared actions -------------------------------------------------
    ui.open_about = [&ctx, &demo] {
        auto box = std::make_unique<MessageBox>(
            "About", "slikui showcase — every widget, live.\n"
                     "C++ build; python/examples/showcase.py is the twin.");
        box->add_button("Nice", [] {});
        ctx.open_dialog(std::move(box));
        demo.say("dialog 'About' opened");
    };
    ui.open_file_dialog = [&ui, &ctx, &demo] {
        auto browser = std::make_unique<FileBrowser>(FileBrowser::Mode::Open);
        FileBrowser& fb = *browser; // capture BEFORE releasing ownership
        ui.file_dialog = &fb;       // valid until closed (smoke clicks its ✕)
        fb.on_pick([&demo](std::string path) {
            demo.say("file → " + path, LogView::Level::Success);
        });
        fb.on_close([&demo] { demo.say("dialog 'Open' closed"); });
        ctx.open_dialog(std::move(browser));
        demo.say("dialog 'Open' opened (navigate, pick a file)");
    };
    ui.save = [&demo] {
        demo.say("menu File → Save (saved showcase.ui)", LogView::Level::Success);
        if (demo.status) demo.status->show_message("saved showcase.ui");
        demo.say("toast → 'Saved showcase.ui'");
    };

    ctx.add_shortcut(Key::O, ModCtrl, [&] {
        demo.say("shortcut Ctrl+O → open dialog");
        if (ui.open_file_dialog) ui.open_file_dialog();
    });
    ctx.add_shortcut(Key::S, ModCtrl, [&] {
        demo.say("shortcut Ctrl+S → save");
        if (ui.save) ui.save();
    });
}

// One live plot/progress update (shared by interactive loop and smoke).
static void pump_plot(UI& ui, double now, double dt) {
    if (!ui.playing) return;
    ui.plot_phase += dt;
    if (now >= ui.plot_next) {
        ui.plot_next = now + 0.25;
        ui.samples.push_back(0.35f + 0.3f * std::sin(float(ui.plot_phase) * 3.f));
        ui.samples.erase(ui.samples.begin());
        if (++ui.plot_logged % 8 == 1)
            ui.demo.say("plot → sample " + std::to_string(ui.samples.size()));
    }
    ui.progress->set_value(0.5f + 0.5f * std::sin(float(now)));
}

// ---------------------------------------------------------------------------
// Smoke mode: headless scripted pass over every component. Real synthetic
// mouse/key/scroll events wherever a widget's bounds make that practical;
// direct calls (through the same say() path) for popup-driven widgets.
// Prints the transcript and writes a screenshot PPM.
// ---------------------------------------------------------------------------
static int run_smoke(const char* ppm_path) {
    constexpr int W = 960, H = 640;
    std::vector<std::uint32_t> pixels(size_t(W) * H);
    slikui::software::Target target(pixels.data(), W, H);
    slikui::software::Sink sink(target);
    Context ctx(sink);
    ctx.load_font_file(find_system_font());
    ctx.set_frame_size({float(W), float(H)}); // smoke is 1x: px == logical

    UI ui;
    build_ui(ctx, ui);
    ui.demo.say("showcase started (smoke) — scripted pass over every component");

    double now = 0.0;
    auto frame = [&](double dt) {
        now += dt;
        pump_plot(ui, now, clamp_dt(dt));
        ctx.tick(Time{now, clamp_dt(dt)});
        ctx.render();
    };
    frame(0.016); // layout pass — bounds are live after this

    // Pages are visibility-toggled Panels, so the script switches tabs before
    // touching each page's widgets (hit-testing skips hidden subtrees).
    auto select_tab = [&](size_t i) {
        ui.tabs->set_selected(i);
        apply_page(ui, i);
        ui.demo.say(std::string("tabs → ") + kTabNames[i]);
        frame(0.016);
    };

    // --- Controls page: real clicks, real slider drag ---------------------
    click(ctx, *ui.btn_ping);
    click(ctx, *ui.btn_danger);
    click(ctx, *ui.checkbox);
    click(ctx, *ui.radios[1]);
    click(ctx, *ui.sw);
    drag(ctx, {ui.slider->bounds.x + ui.slider->bounds.w * 0.25f, center(*ui.slider).y},
         {ui.slider->bounds.x + ui.slider->bounds.w * 0.70f, center(*ui.slider).y});
    click(ctx, *ui.segmented);
    frame(0.016);

    // Dialogs via real plumbing while the Controls page is up: Ctrl+O opens
    // the file browser, its ✕ closes it, and the tree is still live after.
    key(ctx, Key::O, ModCtrl);
    frame(0.016);
    click(ctx, ui.file_dialog->close_button());
    frame(0.016);
    click(ctx, *ui.btn_ping);
    frame(0.016);
    ui.save(); // toast + status + success log
    frame(0.016);

    // --- Inputs page: real typing; direct paths for popup-driven widgets --
    select_tab(1);
    click(ctx, *ui.field);
    type(ctx, "hi");
    key(ctx, Key::Enter);
    click(ctx, ui.search->field());
    type(ctx, "q");
    ui.dropdown->set_selected(1); // set_selected doesn't announce; say directly
    ui.demo.say("dropdown → Linear");
    ui.dv->set_value(42.f); // set_value fires on_change itself
    ui.vec_vals[0] = 1.f;
    ui.vec_vals[1] = 2.f;
    ui.demo.say("vec2 → (1.0, 2.0)");
    ui.color->set_color(Color::from_hex(0x7FD1FF)); // fires on_change
    frame(0.016);

    // --- Data page: direct selections, real pad drag, playing plot --------
    select_tab(2);
    ui.list->set_selected(1);
    ui.demo.say("list → beta");
    ui.tree->set_selected(2);
    ui.demo.say("tree → main.cpp");
    ui.table->set_selected(2);
    ui.demo.say("table → row 2 (views.hpp)");
    ui.prop_opacity->set_value(0.5f); // fires on_change
    drag(ctx, center(*ui.pad),
         {ui.pad->bounds.x + ui.pad->bounds.w * 0.55f,
          ui.pad->bounds.y + ui.pad->bounds.h * 0.25f});
    for (int i = 0; i < 8; ++i) {
        ui.samples.push_back(0.35f + 0.3f * std::sin(float(ui.plot_phase) * 3.f));
        ui.samples.erase(ui.samples.begin());
        ++ui.plot_phase;
    }
    ui.plot_next = now + 0.25;
    ui.demo.say("plot → +8 samples (playing)");
    frame(0.016);

    // --- Containers page: real expander/accordion clicks, splitter drag ---
    select_tab(3);
    click(ctx, *ui.expander);
    click(ctx, *ui.acc_sh);
    {
        Vec2 mid{center(*ui.vsplit).x,
                 ui.vsplit->bounds.y + ui.vsplit->bounds.h * 0.5f};
        drag(ctx, mid, {mid.x, mid.y + 24.f});
    }
    wheel(ctx, center(*ui.scroll), 3.f);
    ui.demo.say("scrollarea → scrolled by 3 ticks");
    frame(0.016);

    // --- Media page: real wheel-zoom; compare fraction directly -----------
    select_tab(4);
    wheel(ctx, center(*ui.image_view), 1.f);
    ui.compare->set_fraction(0.35f);
    ui.demo.say("compare → 0.35");
    frame(0.016);

    // --- Custom page: real meter scrub at 60%, then the 100% set button ---
    select_tab(5);
    click_at(ctx, {ui.meter->bounds.x + ui.meter->bounds.w * 0.6f, center(*ui.meter).y});
    click(ctx, *ui.meter_set_buttons[3]); // 100% → warning level
    frame(0.016);

    frame(3.5); // toast expiry + status transient clear

    // Screenshot of whatever page is visible (Custom tab, last selected).
    std::FILE* out = std::fopen(ppm_path, "wb");
    if (!out) {
        std::fprintf(stderr, "smoke: cannot write %s\n", ppm_path);
        return 1;
    }
    std::fprintf(out, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            Color c = Color::from_abgr(target.at(x, y));
            std::uint8_t rgb[3] = {c.r, c.g, c.b};
            std::fwrite(rgb, 1, 3, out);
        }
    std::fclose(out);

    for (const auto& line : ui.demo.transcript) std::printf("%s\n", line.c_str());
    std::printf("smoke ok: %d events, %zu log lines live, screenshot %s\n",
                ui.demo.events, ui.demo.log->line_count(), ppm_path);
    return 0;
}

int main(int argc, char** argv) {
    bool smoke = argc > 1 && std::string(argv[1]) == "--smoke";
    const char* ppm_path = argc > 2 ? argv[2] : "showcase_smoke.ppm";

    if (smoke) return run_smoke(ppm_path);

    slikui::glfw::GlfwWindow win(960, 640, "slikui showcase — every widget, live");
    win.set_min_size(720, 480);
    if (!win.ok()) return 1;
    slikui::opengl::OpenglSink sink;
    if (!sink.init()) return 1;

    Context ctx(sink);
    ctx.load_font_file(find_system_font());

    UI ui;
    build_ui(ctx, ui);
    ui.demo.say("showcase started — click around, watch the log");

    auto last = std::chrono::steady_clock::now();
    double now = 0;
    while (!win.should_close()) {
        win.dispatch(ctx);
        ctx.set_frame_size(win.point_size()); // LOGICAL frame: layout in points
        ctx.theme().scale = win.ui_scale();
        sink.set_scale(win.ui_scale());

        auto pt = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(pt - last).count();
        last = pt;
        now += dt;
        pump_plot(ui, now, clamp_dt(dt));

        ctx.tick(Time{now, clamp_dt(dt)});
        ctx.render();
        win.swap();
    }
    return 0;
}
