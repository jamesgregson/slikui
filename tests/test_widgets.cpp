// tests/test_widgets.cpp - behavior tests: callbacks, focus, input routing.
#include "test.hpp"

#include "slikui/backends/software/software.hpp"
#include "slikui/context.hpp"
#include "slikui/text/default_font.hpp"
#include "slikui/widgets/all.hpp"

namespace {

struct Fixture {
    slikui::software::Target target{320, 240};
    slikui::software::Sink sink{target};
    slikui::Context ctx{sink};
    bool has_font = false;

    Fixture() {
        ctx.set_frame_size({320, 240});
        // Bundled font: identical text on every platform, even fontless
        // containers (manylinux), so text checks never skip.
        has_font = ctx.load_font_memory(slikui::text::default_font_data(),
                                        slikui::text::default_font_size());
    }

    void resize(float w, float h) {
        slikui::Event e{};
        e.type = slikui::Event::Type::FrameResize;
        e.pos = {w, h}; // scale 1 in tests: px == logical
        ctx.handle(e);
    }

    void frame() {
        ctx.tick(slikui::Time{0.5, 0.016});
        ctx.render();
    }

    slikui::Event mouse(slikui::Event::Type t, float x, float y, int button = slikui::MouseLeft) {
        slikui::Event e{};
        e.type = t;
        e.pos = {x, y};
        e.button = button;
        return e;
    }

    slikui::Event key(slikui::Key k, std::uint32_t mods = 0) {
        slikui::Event e{};
        e.type = slikui::Event::Type::KeyPress;
        e.key = k;
        e.mods = mods;
        return e;
    }
};

} // namespace

TEST(root_fills_frame) {
    Fixture f;
    f.ctx.set_root<slikui::Column>();
    f.frame();
    CHECK(f.ctx.root()->bounds.w == 320);
    CHECK(f.ctx.root()->bounds.h == 240);
}

TEST(button_click_callback) {
    Fixture f;
    int clicks = 0;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& btn = root.add<slikui::Button>("OK").on_click([&] { clicks++; });
    f.frame();

    slikui::Vec2 c = btn.bounds.center();
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, c.x, c.y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseRelease, c.x, c.y));
    CHECK(clicks == 1);

    // Release outside the button: no click.
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, c.x, c.y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseRelease, c.x, c.y + 200));
    CHECK(clicks == 1);
}

TEST(button_keyboard_activation) {
    Fixture f;
    int clicks = 0;
    auto& root = f.ctx.set_root<slikui::Column>();
    root.add<slikui::Button>("OK").on_click([&] { clicks++; });
    f.frame();

    f.ctx.handle(f.key(slikui::Key::Tab));   // focus first focusable
    f.ctx.handle(f.key(slikui::Key::Enter)); // activate
    CHECK(clicks == 1);
}

TEST(checkbox_toggle_and_callback) {
    Fixture f;
    bool last = false;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& box = root.add<slikui::Checkbox>("opt", false).on_change([&](bool v) { last = v; });
    f.frame();

    slikui::Vec2 c = box.bounds.center();
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, c.x, c.y));
    CHECK(box.checked());
    CHECK(last);

    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, c.x, c.y));
    CHECK(!box.checked());
    CHECK(!last);
}

TEST(slider_drag_updates_value) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& slider = root.add<slikui::Slider>(0.f, 100.f);
    f.frame();

    slikui::Vec2 c = slider.bounds.center();
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, c.x, c.y)); // press in middle
    float mid = slider.value();
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseMove, slider.bounds.right() - 10, c.y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseRelease, slider.bounds.right() - 10, c.y));

    CHECK(mid > 40 && mid < 60);     // middle press ~ 50
    CHECK(slider.value() > 90);      // dragged to the right end
}

TEST(tab_cycles_focus) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    root.add<slikui::Button>("a");
    root.add<slikui::Button>("b");
    root.add<slikui::TextField>();
    f.frame();

    f.ctx.handle(f.key(slikui::Key::Tab));
    CHECK(f.ctx.focused() != nullptr);
    slikui::Widget* first = f.ctx.focused();
    f.ctx.handle(f.key(slikui::Key::Tab));
    slikui::Widget* second = f.ctx.focused();
    CHECK(first != second);
    f.ctx.handle(f.key(slikui::Key::Tab)); // -> third
    f.ctx.handle(f.key(slikui::Key::Tab)); // wraps -> first
    CHECK(f.ctx.focused() == first);
}

TEST(textfield_typing_and_editing) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& field = root.add<slikui::TextField>();
    f.frame();

    // Click to focus.
    slikui::Vec2 c = field.bounds.center();
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, c.x, c.y));
    CHECK(f.ctx.focused() == static_cast<slikui::Widget*>(&field));

    auto text_ev = [&](std::uint32_t cp) {
        slikui::Event e{};
        e.type = slikui::Event::Type::TextInput;
        e.codepoint = cp;
        f.ctx.handle(e);
    };
    text_ev('h');
    text_ev('i');
    CHECK(field.text() == "hi");
    f.ctx.handle(f.key(slikui::Key::Backspace));
    CHECK(field.text() == "h");
}

TEST(dropdown_opens_and_dismisses) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& dd = root.add<slikui::Dropdown>()
                   .set_items({"Apple", "Banana", "Cherry"})
                   .set_placeholder("pick");
    f.frame();

    slikui::Vec2 c = dd.bounds.center();
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, c.x, c.y));
    CHECK(f.ctx.has_popups());

    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, 5.f, 5.f)); // outside
    CHECK(!f.ctx.has_popups());
}

TEST(dropdown_selection) {
    Fixture f;
    size_t picked = size_t(-1);
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& dd = root.add<slikui::Dropdown>()
                   .set_items({"Apple", "Banana", "Cherry"})
                   .on_select([&](size_t i) { picked = i; });
    f.frame();

    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, dd.bounds.center().x, dd.bounds.center().y));
    REQUIRE(f.ctx.has_popups());
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress,
                         dd.bounds.x + 20.f, dd.bounds.bottom() + 20.f)); // first row
    CHECK(!f.ctx.has_popups());
    CHECK(picked == 0);
    CHECK(dd.selected() == 0);
}

TEST(scrollarea_wheel_scroll) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& scroll = root.add<slikui::ScrollArea>();
    scroll.hint_w_ = 100;
    scroll.hint_h_ = 50;
    for (int i = 0; i < 10; ++i) scroll.add<slikui::Label>("row");
    f.frame();

    slikui::Event wheel{};
    wheel.type = slikui::Event::Type::Scroll;
    wheel.pos = {50, 25};
    wheel.scroll = {0, -3}; // scroll down
    f.ctx.handle(wheel);
    f.frame();

    // Content moved up: first label is now above the viewport top.
    auto& content = scroll.content();
    CHECK(content.children[0]->bounds.y < scroll.bounds.y);
}


TEST(scrollarea_clips_picking_to_viewport) {
    // Tabs header above a scrolled page: content scrolled up behind the
    // header is clipped from view and must not receive the header's clicks.
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    size_t tab_changed = 99;
    auto& tabs = root.add<slikui::Tabs>()
                     .set_tabs({"a", "second tab label"}) // tab 1 starts ~25px in
                     .on_change([&](size_t i) { tab_changed = i; });
    auto& scroll = root.add<slikui::ScrollArea>();
    scroll.hint_w_ = 300;
    scroll.hint_h_ = 60;
    int clicks = 0;
    for (int i = 0; i < 20; ++i)
        scroll.add<slikui::Button>("row").on_click([&] { ++clicks; });
    f.frame();

    slikui::Event wheel{};
    wheel.type = slikui::Event::Type::Scroll;
    wheel.pos = scroll.bounds.center();
    wheel.scroll = {0, -3};
    f.ctx.handle(wheel);
    f.frame();

    // Probe a point where a scrolled-away button overlaps the tab header.
    slikui::Widget* hidden = nullptr;
    for (auto& c : scroll.content().children) {
        float top = std::fmax(c->bounds.y, tabs.bounds.y);
        float bot = std::fmin(c->bounds.bottom(), tabs.bounds.bottom());
        if (bot - top > 4.f && c->bounds.bottom() <= scroll.bounds.y) hidden = c.get();
    }
    REQUIRE(hidden != nullptr);
    float top = std::fmax(hidden->bounds.y, tabs.bounds.y);
    float bot = std::fmin(hidden->bounds.bottom(), tabs.bounds.bottom());
    slikui::Vec2 p{hidden->bounds.center().x, (top + bot) * .5f};
    REQUIRE(!scroll.bounds.contains(p));

    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, p.x, p.y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseRelease, p.x, p.y));
    CHECK(clicks == 0);
    CHECK(tab_changed == 1);
}

TEST(scrollarea_reveals_keyboard_focus) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& scroll = root.add<slikui::ScrollArea>();
    scroll.hint_w_ = 300;
    scroll.hint_h_ = 60;
    std::vector<slikui::Button*> rows;
    for (int i = 0; i < 20; ++i) rows.push_back(&scroll.add<slikui::Button>("row"));
    f.frame();

    auto in_view = [&](const slikui::Widget& w) {
        return w.bounds.y >= scroll.bounds.y - .5f &&
               w.bounds.bottom() <= scroll.bounds.bottom() + .5f;
    };
    REQUIRE(!in_view(*rows[10]));
    for (int i = 0; i <= 10; ++i) { // tab down to rows[10]
        f.ctx.handle(f.key(slikui::Key::Tab));
        f.frame();
    }
    REQUIRE(rows[10]->focused);
    CHECK(in_view(*rows[10]));
    CHECK(!in_view(*rows[0]));

    // Wrap around backwards to the last row, then forwards to the first.
    for (int i = 0; i <= 10; ++i) {
        f.ctx.handle(f.key(slikui::Key::Tab, slikui::ModShift));
        f.frame();
    }
    REQUIRE(rows[19]->focused);
    CHECK(in_view(*rows[19]));
    f.ctx.handle(f.key(slikui::Key::Tab));
    f.frame();
    REQUIRE(rows[0]->focused);
    CHECK(in_view(*rows[0]));
}

TEST(splitter_pane_overflow_does_not_cover_divider) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& split = root.add<slikui::Splitter>(/*vertical=*/true).set_fraction(0.f);
    auto& top = split.add<slikui::Column>(); // overflows its squeezed pane
    int clicks = 0;
    for (int i = 0; i < 6; ++i) top.add<slikui::Button>("row").on_click([&] { ++clicks; });
    auto& bottom = split.add<slikui::Column>();
    for (int i = 0; i < 6; ++i) bottom.add<slikui::Label>("pane two");
    f.frame();

    // Divider centre, and a point inside pane two, both under overflow.
    slikui::Vec2 d{top.children[0]->bounds.center().x, top.bounds.bottom() + 3.f};
    slikui::Vec2 p2{d.x, bottom.bounds.y + 10.f};
    bool covered_d = false, covered_p2 = false;
    for (auto& c : top.children) {
        covered_d |= c->bounds.contains(d);
        covered_p2 |= c->bounds.contains(p2);
    }
    REQUIRE(covered_d && covered_p2);

    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, p2.x, p2.y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseRelease, p2.x, p2.y));
    CHECK(clicks == 0);

    float before = split.fraction();
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, d.x, d.y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseMove, d.x, d.y + 40.f));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseRelease, d.x, d.y + 40.f));
    CHECK(clicks == 0);
    CHECK(split.fraction() > before); // the divider was grabbed and dragged
}

TEST(window_resize_reflows_layout) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    root.add<slikui::Button>("wide");
    f.frame();
    CHECK(f.ctx.root()->bounds.w == 320);
    CHECK(f.ctx.root()->bounds.h == 240);

    f.resize(480, 300);
    f.frame();
    CHECK(f.ctx.root()->bounds.w == 480);
    CHECK(f.ctx.root()->bounds.h == 300);
}

TEST(label_rewraps_on_resize) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& lbl = root.add<slikui::Label>("aaaa bbbb cccc dddd eeee ffff gggg hhhh");
    f.frame();
    float h_wide = lbl.bounds.h;

    f.resize(120, 240); // much narrower -> label must wrap taller
    f.frame();
    CHECK(lbl.bounds.h > h_wide);
    CHECK(lbl.bounds.w <= 120.f);
}

TEST(scrollarea_clamps_scroll_on_resize) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& scroll = root.add<slikui::ScrollArea>();
    scroll.hint_w_ = 100;
    scroll.hint_h_ = 50;
    for (int i = 0; i < 10; ++i) scroll.add<slikui::Label>("row");
    f.frame();

    slikui::Event wheel{};
    wheel.type = slikui::Event::Type::Scroll;
    wheel.pos = {50, 25};
    wheel.scroll = {0, -100};
    f.ctx.handle(wheel);
    f.frame();

    f.resize(320, 100); // window shrink: scroll area keeps its hint height
    f.frame();
    // scroll position remains clamped to the content
    CHECK(scroll.content().children[0]->bounds.y <= scroll.bounds.y + 0.5f);
}

TEST(popup_reclamps_on_resize) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& dd = root.add<slikui::Dropdown>().set_items({"one", "two", "three"});
    f.frame();

    slikui::Vec2 c = dd.bounds.center();
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, c.x, c.y));
    REQUIRE(f.ctx.has_popups());
    f.ctx.close_popups();

    // Re-open after shrinking the window: popup must stay inside the frame.
    f.resize(160, 100);
    f.frame();
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, dd.bounds.center().x,
                         dd.bounds.center().y));
    REQUIRE(f.ctx.has_popups());
    slikui::Widget* p = f.ctx.popup(0);
    CHECK(p->bounds.right() <= f.ctx.frame_size().x + 0.5f);
    CHECK(p->bounds.bottom() <= f.ctx.frame_size().y + 0.5f);
}

// ---------------------------------------------------------------------------
// New widget set: Radio, Expander, Splitter, Image, TextArea, tooltips.
// ---------------------------------------------------------------------------

TEST(radio_selects_once) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& r = root.add<slikui::Radio>("mode", false);
    int fired = 0;
    bool last = false;
    r.on_change([&](bool v) { fired++; last = v; });
    f.frame();

    slikui::Vec2 c = r.bounds.center();
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, c.x, c.y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseRelease, c.x, c.y));
    CHECK(r.checked());
    CHECK(fired == 1);
    CHECK(last == true);

    // Clicking again does not fire (a radio never unselects itself).
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, c.x, c.y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseRelease, c.x, c.y));
    CHECK(fired == 1);
    CHECK(r.checked());
}

TEST(radio_group_exclusive) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& row = root.add<slikui::Row>();
    auto& a = row.add<slikui::Radio>("a", true);
    auto& b = row.add<slikui::Radio>("b", false);
    a.on_change([&](bool on) { if (on) b.set_checked(false); });
    b.on_change([&](bool on) { if (on) a.set_checked(false); });
    f.frame();

    slikui::Vec2 cb = b.bounds.center();
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, cb.x, cb.y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseRelease, cb.x, cb.y));
    CHECK(b.checked());
    CHECK(!a.checked());
}

TEST(expander_collapses_content) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& ex = root.add<slikui::Expander>("section");
    ex.add<slikui::Label>("inside");
    ex.set_open(true);
    f.frame();
    float open_h = ex.bounds.h;
    CHECK(open_h > 24.f);

    ex.set_open(false);
    f.frame();
    CHECK(ex.bounds.h < open_h);
    // Hidden content: zero frame and invisible.
    CHECK(!ex.children[0]->visible);
    CHECK(ex.children[0]->bounds.w == 0);

    // Clicking the header opens it again.
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, ex.bounds.center().x, ex.bounds.y + 10.f));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseRelease, ex.bounds.center().x, ex.bounds.y + 10.f));
    f.frame();
    CHECK(ex.open());
    CHECK(ex.children[0]->visible);
    CHECK(ex.children[0]->bounds.w > 0);
}

TEST(splitter_drag_resizes_panes) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& split = root.add<slikui::Splitter>();
    auto& left = split.add<slikui::Label>("left");
    auto& right = split.add<slikui::Label>("right");
    float reported = -1;
    split.on_change([&](float v) { reported = v; });
    f.frame();

    float w0 = left.bounds.w;
    CHECK(left.bounds.right() <= right.bounds.x); // divider between panes

    // Drag the divider (grab area sits at the first pane's right edge).
    float dx = split.bounds.x + left.bounds.w + 3.f;
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, dx, split.bounds.center().y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseMove, dx + 60.f, split.bounds.center().y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseRelease, dx + 60.f, split.bounds.center().y));
    f.frame();

    CHECK(split.fraction() > 0.5f); // dragged right
    CHECK(left.bounds.w > w0);
    CHECK(reported > 0.5f);
    CHECK(left.bounds.right() <= right.bounds.x);
}

TEST(image_measure_and_upload) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& img = root.add<slikui::Image>();
    const int w = 8, h = 6;
    std::vector<std::uint8_t> px(size_t(w) * h * 4, 200);
    img.set_pixels(w, h, px.data());
    f.frame();

    CHECK(img.bounds.w == 8.f); // natural pixel size at 1x
    CHECK(img.bounds.h == 6.f);

    // One Rgba8 upload on the next render, then no more (dirty flag cleared).
    f.frame();
    // (Sink has no query API; the observable contract is exercised in the
    // golden capture - here we check measure/fit behaviour instead.)
    img.set_size(24, 24).set_fit(slikui::Image::Fit::Contain);
    f.frame();
    // Contain fit into a 24x24 frame of an 8x6 image centers it at 24x18.
    // (Paint-side: the destination rect is derived in paint(); bounds stay
    // the laid-out frame.)
    CHECK(img.bounds.w == 24.f);
    CHECK(img.bounds.h == 24.f);
}

TEST(textarea_multiline_editing) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& ta = root.add<slikui::TextArea>();
    ta.set_rows(3);
    f.frame();

    slikui::Vec2 c = ta.bounds.center();
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, c.x, c.y));
    CHECK(f.ctx.focused() == static_cast<slikui::Widget*>(&ta));

    auto text_ev = [&](std::uint32_t cp) {
        slikui::Event e{};
        e.type = slikui::Event::Type::TextInput;
        e.codepoint = cp;
        f.ctx.handle(e);
    };
    text_ev('a');
    text_ev('b');
    f.ctx.handle(f.key(slikui::Key::Enter));
    text_ev('c');
    CHECK(ta.text() == "ab\nc");

    // Up moves to line 0 keeping the column (col 1 -> between 'a' and 'b').
    f.ctx.handle(f.key(slikui::Key::Left)); // col 0 of line 1
    f.ctx.handle(f.key(slikui::Key::Up));   // line 0, col 0
    f.ctx.handle(f.key(slikui::Key::Down)); // back to line 1, col 0
    f.ctx.handle(f.key(slikui::Key::Right));
    f.ctx.handle(f.key(slikui::Key::Backspace));
    CHECK(ta.text() == "ab\n"); // deleted 'c'

    // Backspace at a line start joins lines.
    f.ctx.handle(f.key(slikui::Key::Backspace));
    CHECK(ta.text() == "ab");
}

TEST(textarea_click_places_caret) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& ta = root.add<slikui::TextArea>();
    ta.set_text("first\nsecond");
    f.frame();

    // Click into the second line.
    float y = ta.bounds.y + 8.f + 1.5f * 20.f; // ~1.5 lines down
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, ta.bounds.x + 30.f, y));
    CHECK(f.ctx.focused() == static_cast<slikui::Widget*>(&ta));

    // Type: must land inside line 1 (not appended to line 0).
    slikui::Event in{};
    in.type = slikui::Event::Type::TextInput;
    in.codepoint = 'X';
    f.ctx.handle(in);
    CHECK(ta.text().find("X") != std::string::npos);
    CHECK(ta.text().substr(0, 5) == "first");
}

TEST(tooltip_shows_after_hover_delay) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& btn = root.add<slikui::Button>("hover me").set_tooltip("I am a tooltip");
    f.frame();

    slikui::Vec2 c = btn.bounds.center();
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseMove, c.x, c.y));

    double now = 0;
    for (int i = 0; i < 20; ++i) { // ~0.32s: below the 0.6s delay
        now += 0.016;
        f.ctx.tick(slikui::Time{now, 0.016});
    }
    CHECK(!f.ctx.has_popups());

    for (int i = 0; i < 25; ++i) { // +0.4s: past the delay
        now += 0.016;
        f.ctx.tick(slikui::Time{now, 0.016});
    }
    CHECK(f.ctx.has_popups());

    // Moving the pointer away dismisses it (bottom-left: outside the button).
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseMove, 5.f, 200.f));
    CHECK(!f.ctx.has_popups());
}

TEST(single_paint_per_widget) {
    // Regression: containers used to paint their subtree in paint() AND
    // Widget::paint_tree recursed - every child was emitted twice (doubling
    // alpha coverage on text).
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    root.add<slikui::Label>("hello");
    f.frame();

    slikui::DrawList list;
    const slikui::PaintEnv env{list, f.ctx.theme(), f.ctx.font()};
    list.rect({0, 0, 320, 240}, f.ctx.theme().background);
    f.ctx.root()->paint_tree(env);

    float px = f.ctx.theme().s(f.ctx.theme().base_font_px);
    size_t glyphs = f.ctx.font().layout("hello", px).glyphs.size();
    CHECK(list.size() == 1 + glyphs); // background + glyphs, exactly once
}

TEST(text_block_tight_height) {
    // The first line's baseline sits on its ascent, not a full leading down:
    // single-line blocks are tighter than the old 1.25x line-height box.
    Fixture f;
    float px = f.ctx.theme().s(f.ctx.theme().base_font_px);
    auto lay = f.ctx.font().layout("Hx", px);
    CHECK(lay.size.h <= lay.line_height + 2.f);
    // Multi-line grows by line_height per extra line.
    auto two = f.ctx.font().layout("H\nx", px);
    CHECK_NEAR(two.size.h - lay.size.h, lay.line_height, 0.5f);
}

// ---------------------------------------------------------------------------
// Widget::remove / clear_children
// ---------------------------------------------------------------------------
namespace {
// Counts live instances so tests can see exactly when destruction happens.
struct Tracked : slikui::Widget {
    static inline int alive = 0;
    Tracked() { ++alive; }
    ~Tracked() override { --alive; }
    slikui::Size measure(const slikui::Constraints& c) override { return c.clamp({40, 20}); }
    bool focusable() const override { return true; }
    bool on_event(const slikui::Event& e) override {
        if (e.type == slikui::Event::Type::FocusLost) ++focus_lost;
        return false;
    }
    int focus_lost = 0;
};
// Removes a sibling from inside tick().
struct SiblingKiller : slikui::Widget {
    slikui::Widget* victim = nullptr;
    void tick(slikui::Time) override {
        if (victim && parent) parent->remove(*victim);
        victim = nullptr;
    }
};
} // namespace

TEST(remove_child_outside_a_pass_destroys_now) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& a = root.add<Tracked>();
    root.add<slikui::Label>("keep");
    f.frame();
    CHECK(Tracked::alive == 1);
    CHECK(root.remove(a));
    CHECK(Tracked::alive == 0);
    CHECK(root.children.size() == 1);
    slikui::Label stranger("not a child");
    CHECK(!root.remove(stranger));
    f.frame();
}

TEST(remove_self_from_click_handler) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& b = root.add<slikui::Button>("delete me");
    int clicks = 0;
    b.on_click([&] {
        ++clicks;
        root.remove(b); // the handler's own widget: destroyed after handle()
    });
    f.frame();
    slikui::Vec2 c = b.bounds.center();
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, c.x, c.y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseRelease, c.x, c.y));
    CHECK(clicks == 1);
    CHECK(root.children.empty());
    f.frame(); // nothing dangles (ASan)
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseMove, c.x, c.y));
}

TEST(remove_focused_and_hovered_subtree) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& box = root.add<slikui::Column>();
    auto& t = box.add<Tracked>();
    f.frame();
    slikui::Vec2 c = t.bounds.center();
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseMove, c.x, c.y));
    f.ctx.set_focus(&t);
    CHECK(f.ctx.focused() == &t);
    CHECK(f.ctx.hovered() == &t);
    root.remove(box); // grandparent of the focused widget (FocusLost first)
    CHECK(Tracked::alive == 0);
    CHECK(f.ctx.focused() == nullptr);
    CHECK(f.ctx.hovered() == nullptr);
    f.frame();
    f.ctx.handle(f.key(slikui::Key::A));
}

TEST(remove_popup_anchor_keeps_popup) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& anchor = root.add<slikui::Button>("anchor");
    f.frame();
    auto& pop = f.ctx.push_popup(std::make_unique<slikui::Label>("popup"), anchor,
                                 slikui::PopupDir::Below);
    f.frame();
    slikui::Rect before = pop.bounds;
    root.remove(anchor);
    f.frame(); // layout_popup must not read the destroyed anchor (ASan)
    CHECK(f.ctx.popup_count() == 1);
    CHECK(pop.bounds.x == before.x && pop.bounds.y == before.y);
}

TEST(remove_sibling_during_tick) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& killer = root.add<SiblingKiller>();
    auto& victim = root.add<Tracked>();
    root.add<slikui::Label>("after");
    killer.victim = &victim;
    f.frame(); // tick removes `victim` mid-tick_tree; freed after the pass
    CHECK(Tracked::alive == 0);
    CHECK(root.children.size() == 2);
    f.frame();
}

TEST(clear_children_and_scrollarea_forwarding) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& scroll = root.add<slikui::ScrollArea>();
    for (int i = 0; i < 5; ++i) scroll.add<Tracked>();
    f.frame();
    CHECK(Tracked::alive == 5);
    slikui::Widget& as_widget = scroll;
    as_widget.clear_children(); // virtual: empties the content, keeps the column
    CHECK(Tracked::alive == 0);
    CHECK(scroll.children.size() == 1);
    CHECK(scroll.content().children.empty());
    scroll.add<Tracked>();
    f.frame();
    CHECK(Tracked::alive == 1);
    root.clear_children();
    CHECK(Tracked::alive == 0);
    f.frame();
}

// ---------------------------------------------------------------------------
// set_expand / ScrollArea scroll API
// ---------------------------------------------------------------------------
TEST(set_expand_fills_column) {
    Fixture f; // 320 x 240
    auto& root = f.ctx.set_root<slikui::Column>();
    root.add<slikui::Label>("header");
    auto& scroll = root.add<slikui::ScrollArea>();
    scroll.set_expand();
    root.add<slikui::Label>("footer");
    f.frame();
    // Without expand it keeps its 150 px hint; with it, the rest of the frame.
    CHECK(scroll.bounds.h > 150.f);
    CHECK_NEAR(root.children[2]->bounds.bottom(), 240.f, 0.5f);
}

TEST(scrollarea_scroll_api) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& scroll = root.add<slikui::ScrollArea>();
    std::vector<slikui::Label*> rows;
    for (int i = 0; i < 40; ++i) rows.push_back(&scroll.add<slikui::Label>("row"));
    f.frame();
    CHECK(scroll.scroll_y() == 0.f);
    CHECK(scroll.max_scroll() > 0.f);

    scroll.scroll_to_bottom();
    f.frame();
    CHECK_NEAR(scroll.scroll_y(), scroll.max_scroll(), 0.01f);

    scroll.set_scroll_y(-50.f); // clamped
    f.frame();
    CHECK(scroll.scroll_y() == 0.f);

    // Reveal a row below the fold: it ends up inside the viewport.
    scroll.scroll_to(*rows[30]);
    f.frame();
    CHECK(rows[30]->bounds.y >= scroll.bounds.y);
    CHECK(rows[30]->bounds.bottom() <= scroll.bounds.bottom());
}

TEST(treeview_row_rect) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& tree = root.add<slikui::TreeView>();
    using Row = slikui::TreeView::Row;
    tree.set_rows({Row{"a", 0, true}, Row{"a1", 1}, Row{"b", 0}});
    f.frame();
    slikui::Rect r = tree.row_rect(2);
    CHECK_NEAR(r.y, tree.bounds.y + 2 * r.h, 0.01f);
    tree.set_expanded(0, false);
    CHECK(tree.row_rect(1).empty()); // hidden under a collapsed parent
    CHECK_NEAR(tree.row_rect(2).y, tree.bounds.y + r.h, 0.01f);
    CHECK(tree.row_rect(99).empty());
}

// ---------------------------------------------------------------------------
// PopupClosed
// ---------------------------------------------------------------------------
namespace {
struct PopupProbe : slikui::Widget {
    int* closed;
    std::function<void()> on_closed;
    explicit PopupProbe(int* counter) : closed(counter) {}
    slikui::Size measure(const slikui::Constraints& c) override { return c.clamp({60, 30}); }
    bool on_event(const slikui::Event& e) override {
        if (e.type != slikui::Event::Type::PopupClosed) return false;
        ++*closed;
        if (on_closed) on_closed();
        return true;
    }
};
} // namespace

TEST(popup_closed_on_every_path_once) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& anchor = root.add<slikui::Button>("anchor");
    f.frame();
    int closed = 0;
    auto open = [&] {
        f.ctx.push_popup(std::make_unique<PopupProbe>(&closed), anchor, slikui::PopupDir::Below);
        f.frame();
    };
    using T = slikui::Event::Type;

    open(); // click outside every popup
    f.ctx.handle(f.mouse(T::MousePress, 300, 230));
    CHECK(closed == 1);
    CHECK(f.ctx.popup_count() == 0);

    open(); // Escape
    f.ctx.handle(f.key(slikui::Key::Escape));
    CHECK(closed == 2);

    open(); // app focus lost
    slikui::Event lost{};
    lost.type = T::FocusLost;
    f.ctx.handle(lost);
    CHECK(closed == 3);

    open(); // programmatic, outside any pass
    f.ctx.close_popups();
    CHECK(closed == 4);
    f.frame(); // owned probe destroyed after its notification (ASan)
    f.ctx.close_popups(); // nothing open: no notification
    CHECK(closed == 4);
}

TEST(popup_closed_handler_may_reopen) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& anchor = root.add<slikui::Button>("anchor");
    f.frame();
    int closed = 0;
    auto& first = static_cast<PopupProbe&>(f.ctx.push_popup(
        std::make_unique<PopupProbe>(&closed), anchor, slikui::PopupDir::Below));
    first.on_closed = [&] { // runs after the Escape pass, stack unlocked
        f.ctx.push_popup(std::make_unique<PopupProbe>(&closed), anchor, slikui::PopupDir::Above);
    };
    f.frame();
    f.ctx.handle(f.key(slikui::Key::Escape));
    CHECK(closed == 1);
    CHECK(f.ctx.popup_count() == 1); // the replacement is open
    f.frame();
    f.ctx.handle(f.key(slikui::Key::Escape));
    CHECK(closed == 2);
    CHECK(f.ctx.popup_count() == 0);
    f.frame();
}

TEST(popup_closed_skipped_when_reopened_same_pass) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& anchor = root.add<slikui::Button>("anchor");
    int closed = 0;
    PopupProbe borrowed(&closed);
    // Close + re-push inside one handle() pass - what Menu::open_for does.
    f.ctx.add_shortcut(slikui::Key::R, slikui::ModCtrl, [&] {
        f.ctx.close_popups();
        f.ctx.push_popup(borrowed, anchor, slikui::PopupDir::Below);
    });
    f.frame();
    f.ctx.push_popup(borrowed, anchor, slikui::PopupDir::Below);
    f.frame();
    f.ctx.handle(f.key(slikui::Key::R, slikui::ModCtrl));
    CHECK(closed == 0); // it never really closed
    CHECK(f.ctx.popup_count() == 1);
    f.ctx.close_popups();
    CHECK(closed == 1);
}
