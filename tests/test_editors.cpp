// tests/test_editors.cpp — Phase A editors: DragValue, VecNEdit, ColorEdit,
// Segmented, Slider upgrades (label/format/log/step), TextField selection +
// clipboard, and the Context clipboard host callbacks.
#include "test.hpp"

#include "slikui/backends/software/software.hpp"
#include "slikui/context.hpp"
#include "slikui/text/default_font.hpp"
#include "slikui/widgets/all.hpp"

#include <cmath>

namespace {

struct Fixture {
    slikui::software::Target target{320, 240};
    slikui::software::Sink sink{target};
    slikui::Context ctx{sink};
    bool has_font = false;

    Fixture() {
        ctx.set_frame_size({320, 240});
        has_font = ctx.load_font_memory(slikui::text::default_font_data(),
                                        slikui::text::default_font_size());
    }

    void frame() { ctx.tick(slikui::Time{0.5, 0.016}); ctx.render(); }

    slikui::Event mouse(slikui::Event::Type t, float x, float y,
                        int button = slikui::MouseLeft) {
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
    slikui::Event text(unsigned cp) {
        slikui::Event e{};
        e.type = slikui::Event::Type::TextInput;
        e.codepoint = cp;
        return e;
    }
};

const std::uint32_t ModShortcut =
#ifdef __APPLE__
    slikui::ModSuper;
#else
    slikui::ModCtrl;
#endif

} // namespace

// ---------------------------------------------------------------------------
// HSV
// ---------------------------------------------------------------------------
TEST(hsv_roundtrip) {
    // Achromatic and pure hues round-trip exactly.
    for (std::uint32_t hex : {0x000000u, 0xFFFFFFu, 0x808080u, 0xFF0000u,
                              0x00FF00u, 0x0000FFu, 0xFFFF00u, 0x00FFFFu, 0xFF00FFu}) {
        auto c = slikui::Color::from_hex(hex);
        auto hsv = c.to_hsv();
        auto back = slikui::Color::from_hsv(hsv[0], hsv[1], hsv[2]);
        CHECK(back == c);
    }
    // Arbitrary colors within 1/255 per channel.
    for (std::uint32_t hex : {0xBF5C38u, 0x4E9E58u, 0x3F6FBFu, 0x29529Eu, 0xF08C47u}) {
        auto c = slikui::Color::from_hex(hex);
        auto hsv = c.to_hsv();
        auto back = slikui::Color::from_hsv(hsv[0], hsv[1], hsv[2]);
        CHECK(std::abs(int(back.r) - int(c.r)) <= 1);
        CHECK(std::abs(int(back.g) - int(c.g)) <= 1);
        CHECK(std::abs(int(back.b) - int(c.b)) <= 1);
    }
    // from_hsv wraps hue and clamps s/v.
    CHECK(slikui::Color::from_hsv(-0.25f, 1.f, 1.f) == slikui::Color::from_hsv(0.75f, 1.f, 1.f));
    CHECK(slikui::Color::from_hsv(0.3f, 2.f, -1.f) == slikui::Color::black());
}

// ---------------------------------------------------------------------------
// DragValue
// ---------------------------------------------------------------------------
TEST(dragvalue_scrub_and_type) {
    Fixture f;
    float last = 0;
    int changes = 0;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& dv = root.add<slikui::DragValue>(10.f).on_change([&](float v) {
        last = v;
        changes++;
    });
    f.frame();

    // Drag right by 50px with default speed 0.02/px → +1.
    slikui::Vec2 c = dv.bounds.center();
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, c.x, c.y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseMove, c.x + 50, c.y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseRelease, c.x + 50, c.y));
    f.frame();
    CHECK_NEAR(dv.value(), 11.f, 0.01f);
    CHECK(changes == 1);
    CHECK(last == 11.f);

    // A click (no drag) enters typed mode; typing replaces via buffer append.
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, c.x, c.y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseRelease, c.x, c.y));
    for (char ch : "42") f.ctx.handle(f.text(unsigned(ch)));
    f.ctx.handle(f.key(slikui::Key::Enter)); // commit
    f.frame();
    CHECK(dv.value() == 42.f);
    CHECK(changes == 2);
}

TEST(dragvalue_escape_and_focus_loss_cancel) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    root.add<slikui::Button>("other"); // somewhere to move focus
    auto& dv = root.add<slikui::DragValue>(1.5f);
    f.frame();

    slikui::Vec2 c = dv.bounds.center();
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, c.x, c.y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseRelease, c.x, c.y)); // click → typing
    for (char ch : "999") f.ctx.handle(f.text(unsigned(ch)));
    // Escape clears focus at the Context level; the field abandons the edit.
    f.ctx.handle(f.key(slikui::Key::Escape));
    f.frame();
    CHECK(!dv.focused);
    CHECK_NEAR(dv.value(), 1.5f, 1e-6f);
}

TEST(dragvalue_range_step_int) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& dv = root.add<slikui::DragValue>(0.f);
    dv.set_range(0.f, 10.f).set_step(2.f);
    f.frame();
    dv.set_value(7.f); // quantizes to 8 (multiples of 2 from min 0)
    CHECK(dv.value() == 8.f);
    dv.set_value(99.f); // clamps to 10
    CHECK(dv.value() == 10.f);

    dv.set_int_mode(true).set_step(0.f).set_range(0.f, 255.f);
    dv.set_value(0.f); // reset: the clamp above left it at 10
    f.ctx.handle(f.key(slikui::Key::Tab)); // focus
    f.ctx.handle(f.key(slikui::Key::Up));
    f.ctx.handle(f.key(slikui::Key::Up));
    CHECK(dv.value() == 2.f);
}

// ---------------------------------------------------------------------------
// VecNEdit
// ---------------------------------------------------------------------------
TEST(vec3edit_borrowed_buffer) {
    Fixture f;
    float v[3] = {1.f, 2.f, 3.f};
    int changes = 0;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& vec = root.add<slikui::Vec3Edit>(v).on_change([&] { changes++; });
    f.frame();
    CHECK(vec.bounds.w > 0 && vec.bounds.h == 24);

    // Editing the X field writes straight into the borrowed array.
    auto& xfield = static_cast<slikui::DragValue&>(*vec.children[0]->children[0]);
    slikui::Vec2 c = xfield.bounds.center();
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, c.x, c.y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseMove, c.x + 60, c.y)); // +1.2
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseRelease, c.x + 60, c.y));
    f.frame();
    CHECK_NEAR(v[0], 2.2f, 0.05f);
    CHECK_NEAR(v[1], 2.f, 1e-6f);
    CHECK(changes == 1);

    // External writes to the array are mirrored on the next tick.
    v[2] = 9.5f;
    f.frame();
    auto& zfield = static_cast<slikui::DragValue&>(*vec.children[0]->children[2]);
    CHECK_NEAR(zfield.value(), 9.5f, 1e-6f);
}

// ---------------------------------------------------------------------------
// ColorEdit + popup
// ---------------------------------------------------------------------------
TEST(coloredit_popup_and_sv_drag) {
    Fixture f;
    slikui::Color picked{};
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& ce = root.add<slikui::ColorEdit>(slikui::Color::from_hex(0xBF5C38)); // red hue
    ce.on_change([&](slikui::Color c) { picked = c; });
    f.frame();

    slikui::Vec2 c = ce.bounds.center();
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, c.x, c.y));
    CHECK(f.ctx.popup_count() == 1);

    // Drag inside the SV square to its top-right corner (s=1, v=1): full
    // saturation at the chip's hue.
    auto& popup = *f.ctx.popup(0);
    float sx = popup.bounds.x + slikui::ColorPopup::kPad;
    float sy = popup.bounds.y + slikui::ColorPopup::kPad;
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, sx + 150, sy + 2));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseMove, sx + 150, sy + 2));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseRelease, sx + 150, sy + 2));
    f.frame();
    CHECK(picked.r > 200); // s=1, v=1 at a red hue → strong red
    CHECK(picked.g < 80);
    CHECK(ce.color().r == picked.r);

    // Click outside closes the popup.
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, 5, 5));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseRelease, 5, 5));
    CHECK(f.ctx.popup_count() == 0);
}

// ---------------------------------------------------------------------------
// Segmented
// ---------------------------------------------------------------------------
TEST(segmented_select) {
    Fixture f;
    long picked = -1;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& seg = root.add<slikui::Segmented>();
    seg.set_items({"Cube", "Sphere", "Torus"}).on_select([&](size_t i) { picked = long(i); });
    f.frame();
    CHECK(seg.bounds.w > 0);

    // Click the second segment.
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, seg.bounds.x + seg.bounds.w * .5f, seg.bounds.center().y));
    CHECK(seg.selected() == 1);
    CHECK(picked == 1);

    // Programmatic set without firing.
    picked = -1;
    seg.set_selected(2, false);
    CHECK(seg.selected() == 2);
    CHECK(picked == -1);
}

// ---------------------------------------------------------------------------
// Slider upgrades
// ---------------------------------------------------------------------------
TEST(slider_label_format_log_step) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& plain = root.add<slikui::Slider>(0.f, 100.f);
    auto& labeled = root.add<slikui::Slider>(0.f, 1.f).set_label("Roughness").set_format("%.2f");
    auto& log = root.add<slikui::Slider>(1.f, 100.f).set_log_scale(true);
    auto& stepped = root.add<slikui::Slider>(0.f, 10.f).set_step(5.f);
    f.frame();

    CHECK(plain.bounds.h == 22);
    CHECK(labeled.bounds.h == 37); // track + label row

    // Log scale: dragging to the middle gives the geometric mean.
    slikui::Vec2 c = log.bounds.center();
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, c.x, c.y)); // middle
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseRelease, c.x, c.y));
    CHECK_NEAR(log.value(), 10.f, 0.5f);

    // Step quantization (release each press: capture switches widgets).
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, stepped.bounds.x + 3, stepped.bounds.center().y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseRelease, stepped.bounds.x + 3, stepped.bounds.center().y));
    CHECK(stepped.value() == 0.f);
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, stepped.bounds.x + 150, stepped.bounds.center().y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseRelease, stepped.bounds.x + 150, stepped.bounds.center().y));
    CHECK(stepped.value() == 5.f);
}

// ---------------------------------------------------------------------------
// Clipboard + TextField
// ---------------------------------------------------------------------------
TEST(clipboard_host_callbacks_and_textfield) {
    Fixture f;
    std::string stored;
    f.ctx.set_clipboard_sink([&](std::string s) { stored = s; });
    f.ctx.set_clipboard_source([&] { return stored; });

    auto& root = f.ctx.set_root<slikui::Column>();
    auto& tf = root.add<slikui::TextField>();
    tf.set_text("hello world");
    f.frame();
    f.ctx.handle(f.key(slikui::Key::Tab)); // focus the field

    // Select all + copy.
    f.ctx.handle(f.key(slikui::Key::A, ModShortcut));
    f.ctx.handle(f.key(slikui::Key::C, ModShortcut));
    CHECK(stored == "hello world");

    // Drag-select from the caret to the end: still the full text.
    f.ctx.handle(f.key(slikui::Key::Home));
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, tf.bounds.x + 8, tf.bounds.center().y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseMove, tf.bounds.x + 250, tf.bounds.center().y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseRelease, tf.bounds.x + 250, tf.bounds.center().y));
    f.ctx.handle(f.key(slikui::Key::C, ModShortcut));
    CHECK(stored == "hello world");

    // Shift+Right selects exactly one character.
    f.ctx.handle(f.key(slikui::Key::Home));
    f.ctx.handle(f.key(slikui::Key::Right, slikui::ModShift));
    f.ctx.handle(f.key(slikui::Key::X, ModShortcut));
    CHECK(tf.text() == "ello world");
    CHECK(stored == "h");

    // Paste twice at the caret (which the cut left at 0).
    f.ctx.handle(f.key(slikui::Key::V, ModShortcut));
    f.ctx.handle(f.key(slikui::Key::V, ModShortcut));
    CHECK(tf.text() == "hhello world");
}

TEST(clipboard_absent_is_silent) {
    Fixture f; // no clipboard callbacks installed
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& tf = root.add<slikui::TextField>();
    tf.set_text("data");
    f.frame();
    f.ctx.handle(f.key(slikui::Key::Tab));
    f.ctx.handle(f.key(slikui::Key::A, ModShortcut));
    f.ctx.handle(f.key(slikui::Key::X, ModShortcut)); // no-op, must not crash
    f.frame();
    CHECK(tf.text() == "");
}
