// tests/test_input.cpp — the 0.3 input model: every button reaches widgets,
// Click/DoubleClick/clicks synthesis, Enter/Leave, focus notifications,
// KeyRelease routing, destruction safety and paint_over.
#include "test.hpp"

#include "slikui/backends/software/software.hpp"
#include "slikui/context.hpp"
#include "slikui/text/default_font.hpp"
#include "slikui/widgets/all.hpp"

#include <memory>
#include <string>
#include <vector>

namespace {

using T = slikui::Event::Type;

// Records every event it receives; consumes mouse/key events by default.
class Probe : public slikui::Widget {
public:
    explicit Probe(float h = 40, bool focusable = false) : h_(h), focusable_(focusable) {}

    slikui::Size measure(const slikui::Constraints& c) override { return c.clamp({0, h_}); }
    bool focusable() const override { return focusable_; }

    bool on_event(const slikui::Event& e) override {
        log.push_back(e);
        if (capture_on_press && e.type == T::MousePress) ctx->capture_mouse(this);
        if (e.type == T::MouseRelease) ctx->release_mouse(this);
        return consume;
    }

    int count(T t) const {
        int n = 0;
        for (auto& e : log)
            if (e.type == t) ++n;
        return n;
    }
    const slikui::Event* last(T t) const {
        for (auto it = log.rbegin(); it != log.rend(); ++it)
            if (it->type == t) return &*it;
        return nullptr;
    }

    std::vector<slikui::Event> log;
    bool consume = true;
    bool capture_on_press = false;

private:
    float h_;
    bool focusable_;
};

struct Fixture {
    slikui::software::Target target{320, 240};
    slikui::software::Sink sink{target};
    slikui::Context ctx{sink};
    double now = 0.5;

    Fixture() {
        ctx.set_frame_size({320, 240});
        ctx.load_font_memory(slikui::text::default_font_data(),
                             slikui::text::default_font_size());
    }

    void frame(double dt = 0.016) {
        now += dt;
        ctx.tick(slikui::Time{now, dt});
    }

    slikui::Event mouse(T t, float x, float y, int button = slikui::MouseLeft) {
        slikui::Event e{};
        e.type = t;
        e.pos = {x, y};
        e.button = button;
        return e;
    }
    void move(float x, float y) { ctx.handle(mouse(T::MouseMove, x, y)); }
    void press(float x, float y, int b = slikui::MouseLeft) { ctx.handle(mouse(T::MousePress, x, y, b)); }
    void release(float x, float y, int b = slikui::MouseLeft) { ctx.handle(mouse(T::MouseRelease, x, y, b)); }
    void click(float x, float y, int b = slikui::MouseLeft) {
        press(x, y, b);
        release(x, y, b);
    }
    void key(T t, slikui::Key k, std::uint32_t mods = 0) {
        slikui::Event e{};
        e.type = t;
        e.key = k;
        e.mods = mods;
        ctx.handle(e);
    }
};

// Column (padding 0, gap 10) with two 40px probes: a at y 0..40, b at 50..90.
struct TwoProbes : Fixture {
    Probe* a;
    Probe* b;
    TwoProbes(bool focusable = false) {
        auto& root = ctx.set_root<slikui::Column>();
        root.set_padding(0).set_gap(10);
        a = &root.add<Probe>(40.f, focusable);
        b = &root.add<Probe>(40.f, focusable);
        frame();
    }
};

} // namespace

// --- every button reaches widgets ------------------------------------------

TEST(input_right_and_middle_press_reach_widgets) {
    TwoProbes f;
    f.press(20, 20, slikui::MouseRight);
    f.release(20, 20, slikui::MouseRight);
    f.press(20, 20, slikui::MouseMiddle);
    CHECK(f.a->count(T::MousePress) == 2);
    CHECK(f.a->count(T::MouseRelease) == 1);
    CHECK(f.a->last(T::MousePress)->button == slikui::MouseMiddle);
}

TEST(input_right_click_does_not_activate_button) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    root.set_padding(0);
    int clicks = 0;
    root.add<slikui::Button>("Go").on_click([&] { ++clicks; });
    f.frame();
    f.click(10, 10, slikui::MouseRight);
    f.click(10, 10, slikui::MouseMiddle);
    CHECK(clicks == 0);
    f.click(10, 10);
    CHECK(clicks == 1);
}

TEST(input_context_menu_swallows_press_and_release) {
    TwoProbes f;
    slikui::Menu menu("ctx");
    menu.add_item("Item", [] {});
    f.a->set_context_menu(&menu);
    f.press(20, 20, slikui::MouseRight);
    CHECK(f.ctx.popup_count() == 1);
    f.release(20, 20, slikui::MouseRight);
    CHECK(f.a->count(T::MousePress) == 0);
    CHECK(f.a->count(T::MouseRelease) == 0); // no orphan release
    CHECK(f.a->count(T::Click) == 0);
    f.ctx.close_popups();
}

// --- Click / DoubleClick / clicks ------------------------------------------

TEST(input_click_on_same_widget) {
    TwoProbes f;
    f.click(20, 20);
    CHECK(f.a->count(T::Click) == 1);
    CHECK(f.a->last(T::Click)->clicks == 1);
    CHECK(f.a->last(T::Click)->button == slikui::MouseLeft);
    // order: press, release, click
    size_t n = f.a->log.size();
    CHECK(n >= 3 && f.a->log[n - 3].type == T::MousePress &&
          f.a->log[n - 2].type == T::MouseRelease && f.a->log[n - 1].type == T::Click);
}

TEST(input_no_click_when_release_lands_elsewhere) {
    TwoProbes f;
    f.a->capture_on_press = true; // the release still reaches a (capture)
    f.press(20, 20);
    f.release(20, 70); // over b
    CHECK(f.a->count(T::MouseRelease) == 1);
    CHECK(f.a->count(T::Click) == 0);
    CHECK(f.b->count(T::Click) == 0);
}

TEST(input_click_on_any_button) {
    TwoProbes f;
    f.click(20, 20, slikui::MouseRight);
    CHECK(f.a->count(T::Click) == 1);
    CHECK(f.a->last(T::Click)->button == slikui::MouseRight);
}

TEST(input_double_and_triple_click) {
    TwoProbes f;
    f.click(20, 20);
    f.frame(0.1);
    f.click(21, 20); // within slop and time
    CHECK(f.a->last(T::Click)->clicks == 2);
    CHECK(f.a->last(T::MousePress)->clicks == 2);
    CHECK(f.a->count(T::DoubleClick) == 1);
    CHECK(f.a->log.back().type == T::DoubleClick); // after the Click
    f.frame(0.1);
    f.click(20, 21);
    CHECK(f.a->last(T::Click)->clicks == 3);
    CHECK(f.a->count(T::DoubleClick) == 1); // only on clicks == 2
}

TEST(input_click_count_resets_on_time_distance_or_button) {
    TwoProbes f;
    f.click(20, 20);
    f.frame(0.5); // > kMultiClickTime
    f.click(20, 20);
    CHECK(f.a->last(T::Click)->clicks == 1);
    f.frame(0.1);
    f.click(40, 20); // > kMultiClickSlop away
    CHECK(f.a->last(T::Click)->clicks == 1);
    f.frame(0.1);
    f.click(40, 20, slikui::MouseRight); // different button
    CHECK(f.a->last(T::Click)->clicks == 1);
    CHECK(f.a->count(T::DoubleClick) == 0);
}

TEST(input_button_fires_on_click_not_release_elsewhere) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    root.set_padding(0).set_gap(10);
    int clicks = 0;
    root.add<slikui::Button>("Go").on_click([&] { ++clicks; });
    root.add<Probe>(40.f);
    f.frame();
    float by = root.children[0]->bounds.center().y;
    float py = root.children[1]->bounds.center().y;
    f.press(10, by);
    f.release(10, py); // dragged off before releasing
    CHECK(clicks == 0);
}

// --- Enter / Leave -----------------------------------------------------------

TEST(input_enter_leave_follow_the_hover_chain) {
    Fixture g;
    auto& groot = g.ctx.set_root<slikui::Column>();
    groot.set_padding(0);
    auto& box = groot.add<Probe>(100.f); // container probe, y 0..100
    box.consume = false;
    auto& child = box.add<Probe>(20.f);  // laid out by hand below
    g.frame();
    child.arrange({10, 10, 50, 20});      // Probe::arrange doesn't place children

    g.move(200, 50); // inside box, outside child
    CHECK(box.count(T::MouseEnter) == 1);
    CHECK(child.count(T::MouseEnter) == 0);
    g.move(20, 15); // onto child: box must NOT leave
    CHECK(child.count(T::MouseEnter) == 1);
    CHECK(box.count(T::MouseLeave) == 0);
    g.move(25, 16); // within child: nothing new
    CHECK(child.count(T::MouseEnter) == 1);
    g.move(200, 50); // back to box only
    CHECK(child.count(T::MouseLeave) == 1);
    CHECK(box.count(T::MouseLeave) == 0);
    g.move(200, 150); // out of everything
    CHECK(box.count(T::MouseLeave) == 1);
    // Enter/Leave are delivered before the move that caused them
    const auto& L = child.log;
    CHECK(L.size() >= 2 && L[0].type == T::MouseEnter && L[1].type == T::MouseMove);
}

TEST(input_enter_leave_do_not_bubble) {
    TwoProbes f;
    f.move(20, 20);
    f.move(20, 70);
    CHECK(f.a->count(T::MouseEnter) == 1);
    CHECK(f.a->count(T::MouseLeave) == 1);
    CHECK(f.b->count(T::MouseEnter) == 1);
    CHECK(f.b->count(T::MouseLeave) == 0);
}

// --- focus notifications -----------------------------------------------------

TEST(input_focus_gained_and_lost) {
    TwoProbes f(true);
    f.click(20, 20);
    CHECK(f.a->focused);
    CHECK(f.a->count(T::FocusGained) == 1);
    f.click(20, 70);
    CHECK(f.a->count(T::FocusLost) == 1);
    CHECK(f.b->count(T::FocusGained) == 1);
    f.key(T::KeyPress, slikui::Key::Escape);
    CHECK(f.b->count(T::FocusLost) == 1);
    CHECK(f.ctx.focused() == nullptr);
    // FocusGained arrives after the press that caused it
    const auto& L = f.a->log;
    CHECK(L[0].type == T::MousePress && L[1].type == T::FocusGained);
}

TEST(input_set_focus_from_code_notifies) {
    TwoProbes f(true);
    f.ctx.set_focus(f.b);
    CHECK(f.b->count(T::FocusGained) == 1);
    f.ctx.set_focus(nullptr);
    CHECK(f.b->count(T::FocusLost) == 1);
}

TEST(input_app_focus_lost_reaches_captured_and_focused) {
    TwoProbes f(true);
    f.a->capture_on_press = true;
    f.press(20, 20); // a focused + capturing
    slikui::Event lost{};
    lost.type = T::FocusLost;
    f.ctx.handle(lost);
    CHECK(f.a->count(T::FocusLost) == 1); // once, not twice
    // capture is gone: a move over b now reaches b, not a
    f.move(20, 70);
    CHECK(f.b->count(T::MouseMove) == 1);
    CHECK(f.a->count(T::MouseMove) == 0);
}

// --- keys ----------------------------------------------------------------------

TEST(input_key_release_routes_to_focus) {
    TwoProbes f(true);
    f.click(20, 20);
    f.key(T::KeyPress, slikui::Key::Z);
    f.key(T::KeyRelease, slikui::Key::Z);
    CHECK(f.a->count(T::KeyPress) == 1);
    CHECK(f.a->count(T::KeyRelease) == 1);
    // Tab is consumed by the Context: its release is not delivered either
    f.key(T::KeyPress, slikui::Key::Tab);
    f.key(T::KeyRelease, slikui::Key::Tab);
    CHECK(f.a->count(T::KeyRelease) == 1);
    CHECK(f.b->count(T::KeyRelease) == 0);
}

TEST(input_key_without_focus_goes_to_hover) {
    TwoProbes f;
    f.move(20, 70); // hover b
    f.key(T::KeyPress, slikui::Key::Num1);
    CHECK(f.b->count(T::KeyPress) == 1);
    CHECK(f.b->last(T::KeyPress)->key == slikui::Key::Num1);
    CHECK(f.a->count(T::KeyPress) == 0);
}

TEST(input_digit_keys_are_appended) {
    // Existing enum values must not move (Python and hosts store them).
    CHECK(int(slikui::Key::Z) + 1 == int(slikui::Key::Num0));
    CHECK(int(slikui::Event::Type::FrameResize) + 1 == int(slikui::Event::Type::Click));
}

// --- destruction safety --------------------------------------------------------

TEST(input_destroyed_widget_is_forgotten) {
    TwoProbes f(true);
    f.a->capture_on_press = true;
    f.move(20, 20);
    f.press(20, 20); // a: hovered, focused, captured, press target
    CHECK(f.ctx.focused() == f.a);
    CHECK(f.ctx.hovered() == f.a);
    f.ctx.root()->children.erase(f.ctx.root()->children.begin()); // destroy a
    CHECK(f.ctx.focused() == nullptr);
    CHECK(f.ctx.hovered() == nullptr);
    // No dispatch into the freed widget: release/move/keys route normally.
    f.release(20, 20);
    f.frame();
    f.move(20, 20);
    f.key(T::KeyPress, slikui::Key::A);
    CHECK(f.b->count(T::MouseMove) >= 1);
}

TEST(input_borrowed_widget_may_outlive_context) {
    auto menu = std::make_unique<slikui::Menu>("m");
    menu->add_item("x", [] {});
    {
        Fixture f;
        f.ctx.set_root<slikui::Column>();
        f.frame();
        f.ctx.open_context_menu(*menu, {10, 10});
        f.frame();
        f.move(15, 15); // hover inside the menu
    } // Context destroyed while the menu is still alive
    menu.reset(); // must not call back into the dead Context
    CHECK(true);
}

// --- paint_over ---------------------------------------------------------------

namespace {
class Decorated : public slikui::Widget {
public:
    slikui::Size measure(const slikui::Constraints& c) override { return c.clamp({0, 40}); }
    void arrange(const slikui::Rect& area) override {
        bounds = area;
        for (auto& c : children) c->arrange(area);
    }
    void paint(const slikui::PaintEnv& env) const override {
        env.list.rect(bounds, slikui::Color{1, 0, 0, 255});
    }
    void paint_over(const slikui::PaintEnv& env) const override {
        env.list.rect(bounds, slikui::Color{3, 0, 0, 255});
    }
};
class Leaf : public slikui::Widget {
public:
    void paint(const slikui::PaintEnv& env) const override {
        env.list.rect(bounds, slikui::Color{2, 0, 0, 255});
    }
};
} // namespace

TEST(input_paint_over_paints_after_children) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& d = root.add<Decorated>();
    d.add<Leaf>();
    f.frame();
    slikui::DrawList list;
    slikui::PaintEnv env{list, f.ctx.theme(), f.ctx.font()};
    d.paint_tree(env);
    std::vector<int> order;
    for (auto& c : list.commands())
        if (c.type == slikui::DrawCmd::Type::Rect) order.push_back(c.color.r);
    CHECK(order == (std::vector<int>{1, 2, 3}));
}

// --- TextField multi-click ------------------------------------------------------

TEST(input_textfield_double_click_selects_word_triple_selects_all) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& tf = root.add<slikui::TextField>();
    tf.set_text("hello world");
    f.frame();
    slikui::Rect b = tf.bounds;
    float x_end = b.right() - 4; // past the text: caret at the end ("world")
    f.click(x_end, b.center().y);
    f.click(x_end, b.center().y); // double-click
    slikui::Event t{};
    t.type = T::TextInput;
    t.codepoint = 'X';
    f.ctx.handle(t); // replaces the selected word
    CHECK(tf.text() == "hello X");

    f.frame(0.5);
    for (int i = 0; i < 3; ++i) f.click(b.x + 8, b.center().y);
    f.ctx.handle(t);
    CHECK(tf.text() == "X");
}

TEST(input_shift_tab_walks_backwards) {
    TwoProbes f(true);
    auto tab = [&](bool shift) {
        slikui::Event e{};
        e.type = T::KeyPress;
        e.key = slikui::Key::Tab;
        e.mods = shift ? slikui::ModShift : 0;
        f.ctx.handle(e);
    };
    tab(false);
    CHECK(f.ctx.focused() == f.a);
    tab(false);
    CHECK(f.ctx.focused() == f.b);
    tab(true);
    CHECK(f.ctx.focused() == f.a); // was: stayed on b
    tab(true);
    CHECK(f.ctx.focused() == f.b); // wraps
    f.ctx.set_focus(nullptr);
    tab(true);
    CHECK(f.ctx.focused() == f.b); // from nothing: the last one
}
