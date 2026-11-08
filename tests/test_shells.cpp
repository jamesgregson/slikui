// tests/test_shells.cpp — Phase D: dialogs (floating layer, modal trap,
// drag, close), MessageBox, GroupBox, SearchBox, StatusBar, Toast, LogView,
// FileBrowser, ImageView, CompareView, cursor-shape requests.
#include "test.hpp"

#include "slikui/backends/software/software.hpp"
#include "slikui/context.hpp"
#include "slikui/text/default_font.hpp"
#include "slikui/widgets/all.hpp"

#include <memory>

namespace {

struct Fixture {
    slikui::software::Target target{320, 240};
    slikui::software::Sink sink{target};
    slikui::Context ctx{sink};
    bool has_font = false;
    std::vector<slikui::Cursor> cursors;

    Fixture() {
        ctx.set_frame_size({320, 240});
        has_font = ctx.load_font_memory(slikui::text::default_font_data(),
                                        slikui::text::default_font_size());
        ctx.set_cursor_host([&](slikui::Cursor c) { cursors.push_back(c); });
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
    void click(float x, float y, int button = slikui::MouseLeft) {
        ctx.handle(mouse(slikui::Event::Type::MousePress, x, y, button));
        ctx.handle(mouse(slikui::Event::Type::MouseRelease, x, y, button));
    }
    void move(float x, float y) {
        ctx.handle(mouse(slikui::Event::Type::MouseMove, x, y));
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
    std::uint32_t pixel(float x, float y) const {
        // Software target renders at 1x here (no sink scale).
        int px = int(x), py = int(y);
        return target.data()[std::size_t(py) * target.width() + std::size_t(px)];
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Dialog layer: open/close, above popups, drag, modal trap
// ---------------------------------------------------------------------------
TEST(dialog_open_close_and_layering) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    root.add<slikui::Button>("app");
    f.frame();
    REQUIRE(f.has_font);

    auto dlg = std::make_unique<slikui::Dialog>("Tools");
    dlg->content().add<slikui::Label>("Body");
    auto& ref = *dlg;
    f.ctx.open_dialog(std::move(dlg));
    f.frame();
    CHECK(f.ctx.dialog_count() == 1);
    CHECK(ref.bounds.w > 60 && ref.bounds.h > 40);
    // Default position is centered.
    CHECK(std::fabs(ref.bounds.center().x - 160.f) < 2.f);

    // The dialog paints ABOVE the app: its body pixel differs from bare bg.
    f.frame();
    CHECK(f.pixel(ref.bounds.x + 6, ref.bounds.y + ref.bounds.h - 6) !=
          f.pixel(5, 235));

    // Escape closes it (dialogs unwind first).
    f.ctx.handle(f.key(slikui::Key::Escape));
    CHECK(f.ctx.dialog_count() == 0);

    // The close ✕ button closes too.
    auto dlg2 = std::make_unique<slikui::Dialog>("Second");
    auto& ref2 = f.ctx.open_dialog(std::move(dlg2));
    f.frame();
    auto& close_btn = *ref2.children[0]->children[2];
    f.click(close_btn.bounds.center().x, close_btn.bounds.center().y);
    CHECK(f.ctx.dialog_count() == 0);
}

TEST(dialog_drag_moves) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto dlg = std::make_unique<slikui::Dialog>("Tools");
    dlg->content().add<slikui::Label>("Body");
    auto& ref = f.ctx.open_dialog(std::move(dlg));
    f.frame();
    REQUIRE(f.has_font);

    float x0 = ref.bounds.x, y0 = ref.bounds.y;
    auto& bar = *ref.children[0];
    // Press on the title bar (not on the close button at its right end).
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, bar.bounds.x + 10,
                         bar.bounds.center().y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseMove, bar.bounds.x + 50,
                         bar.bounds.center().y + 20));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseRelease, bar.bounds.x + 50,
                         bar.bounds.center().y + 20));
    f.frame();
    CHECK(ref.bounds.x == x0 + 40);
    CHECK(ref.bounds.y == y0 + 20);
}

TEST(modal_dialog_traps_presses_and_dims) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    int clicks = 0;
    root.add<slikui::Button>("app").on_click([&] { clicks++; });
    f.frame();
    REQUIRE(f.has_font);

    auto dlg = std::make_unique<slikui::MessageBox>("Confirm", "Delete item?");
    dlg->set_modal(true);
    int picked = 0;
    dlg->add_button("Delete", [&] { picked = 1; });
    dlg->add_button("Cancel", [&] { picked = 2; });
    auto& ref = f.ctx.open_dialog(std::move(dlg));
    f.frame();

    // Clicking the app button does nothing (trapped by the modal).
    f.click(60, 40);
    CHECK(clicks == 0);

    // Pressing a message-box button runs the action and closes.
    auto& del = *ref.children[1]->children[1]->children[0]; // content>buttons>Delete
    f.click(del.bounds.center().x, del.bounds.center().y);
    CHECK(picked == 1);
    CHECK(clicks == 0);
    CHECK(f.ctx.dialog_count() == 0);
}

// ---------------------------------------------------------------------------
// Toasts
// ---------------------------------------------------------------------------
TEST(toast_shows_and_expires) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    root.add<slikui::Label>("app");
    f.frame();
    REQUIRE(f.has_font);

    auto toast = std::make_unique<slikui::Toast>("Screenshot saved");
    toast->set_icon(slikui::Icon::Camera);
    f.ctx.push_toast(std::move(toast), 0.2); // expiry measured from the last tick
    f.frame();
    CHECK(f.ctx.popup_count() == 1);
    // Toast sits in the bottom-right corner.
    auto& t = *f.ctx.popup(0);
    CHECK(t.bounds.right() > 300.f && t.bounds.bottom() > 230.f);

    // After the expiry time it is gone (tick at now=1.0 > 0.5+0.2).
    f.ctx.tick(slikui::Time{1.0, 0.016});
    CHECK(f.ctx.popup_count() == 0);
}

// ---------------------------------------------------------------------------
// StatusBar / LogView / SearchBox / GroupBox
// ---------------------------------------------------------------------------
TEST(statusbar_transient_message) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& bar = root.add<slikui::StatusBar>();
    bar.set_text("Ready");
    bar.add<slikui::ProgressBar>().set_value(.5f);
    f.frame();
    REQUIRE(f.has_font);
    CHECK(bar.bounds.h >= 20.f);

    bar.show_message("Rendering…", 1.0);
    f.frame();
    CHECK(bar.text() == "Ready"); // getter holds the base text

    // After the transient window passes, tick restores it (via the label).
    f.ctx.tick(slikui::Time{2.0, 0.016});
    f.frame();
    // Indirect check: show + expire + show again works without crash.
    bar.show_message("Done", 0.1);
    f.ctx.tick(slikui::Time{3.0, 0.016});
    f.ctx.tick(slikui::Time{3.2, 0.016});
    CHECK(bar.text() == "Ready");
}

TEST(logview_levels_and_tail) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& log = root.add<slikui::LogView>();
    for (int i = 0; i < 40; ++i)
        log.log(i % 3 == 0 ? slikui::LogView::Level::Warning
                           : slikui::LogView::Level::Info,
                "line " + std::to_string(i));
    f.frame();
    REQUIRE(f.has_font);
    CHECK(log.line_count() == 40);
    CHECK(log.bounds.h >= 100.f);

    // The tail is visible: bright text ink exists somewhere in the view
    // (info lines render in the near-white text color over the dark bg).
    int bright = 0;
    for (int py = int(log.bounds.y); py < int(log.bounds.bottom()); ++py)
        for (int px2 = int(log.bounds.x); px2 < int(log.bounds.right()); ++px2)
            if ((f.pixel(float(px2), float(py)) & 0xFF) > 100) ++bright;
    CHECK(bright > 10);

    log.clear();
    CHECK(log.line_count() == 0);
}

TEST(searchbox_typing_and_clear) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    std::vector<std::string> changes;
    auto& box = root.add<slikui::SearchBox>();
    box.set_placeholder("Filter…");
    box.on_change([&](std::string_view t) { changes.push_back(std::string(t)); });
    f.frame();
    REQUIRE(f.has_font);

    auto& field = box.field();
    f.click(field.bounds.center().x, field.bounds.center().y); // focus the field
    for (char ch : std::string("cube")) f.ctx.handle(f.text(unsigned(ch)));
    CHECK(box.text() == "cube");
    CHECK(changes.size() == 4);
    CHECK(field.visible); // sanity
    // The clear button is visible now and resets the text. Lay out first so
    // the just-revealed button has bounds to click.
    f.frame();
    auto& clear_btn = *box.children[2];
    CHECK(clear_btn.visible);
    f.click(clear_btn.bounds.center().x, clear_btn.bounds.center().y);
    CHECK(box.text() == "");
    CHECK(changes.size() == 5);
}

TEST(groupbox_title_and_children) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& box = root.add<slikui::GroupBox>("Light");
    box.add<slikui::Checkbox>("Enabled", true);
    box.add<slikui::Slider>(0.f, 1.f);
    f.frame();
    REQUIRE(f.has_font);
    CHECK(box.bounds.h > 50.f);
    // Title label sits above the children.
    CHECK(box.children[0]->bounds.bottom() <= box.children[1]->bounds.y + 1.f);
}

// ---------------------------------------------------------------------------
// ImageView / CompareView
// ---------------------------------------------------------------------------
TEST(imageview_pan_zoom) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& view = root.add<slikui::ImageView>();
    std::vector<std::uint8_t> px(32 * 32 * 4, 200);
    view.set_pixels(32, 32, px.data());
    f.frame();
    REQUIRE(f.has_font);
    // Fit: 32px image in a >=150px viewport → initial zoom > 1.
    CHECK(view.zoom() > 1.f);

    float z0 = view.zoom();
    // Wheel up zooms in.
    slikui::Event e{};
    e.type = slikui::Event::Type::Scroll;
    e.pos = view.bounds.center();
    e.scroll = {0, 2};
    f.ctx.handle(e);
    CHECK(view.zoom() > z0);

    // Drag pans.
    float px0 = view.pan().x;
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress,
                         view.bounds.center().x, view.bounds.center().y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseMove,
                         view.bounds.center().x + 25, view.bounds.center().y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseRelease,
                         view.bounds.center().x + 25, view.bounds.center().y));
    CHECK(view.pan().x == px0 + 25.f);
}

TEST(compareview_divider_drag) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& view = root.add<slikui::CompareView>();
    std::vector<std::uint8_t> a(16 * 16 * 4, 0);
    std::vector<std::uint8_t> b(16 * 16 * 4, 0);
    for (size_t i = 0; i < a.size(); i += 4) {
        a[i] = 255; // red A
        a[i + 3] = 255;
        b[i + 2] = 255; // blue B
        b[i + 3] = 255;
    }
    view.set_pixels_a(16, 16, a.data());
    view.set_pixels_b(16, 16, b.data());
    f.frame();
    REQUIRE(f.has_font);
    CHECK(view.fraction() == .5f);

    // Drag the divider right.
    float mid = view.bounds.center().x;
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, mid, view.bounds.center().y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseMove, mid + 40, view.bounds.center().y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseRelease, mid + 40, view.bounds.center().y));
    CHECK(view.fraction() > .6f);

    // A-side pixel is red, B-side is blue at 1x framebuffer... sample at 2x.
    std::uint32_t left = f.pixel(view.bounds.x + 10, view.bounds.center().y);
    std::uint32_t right = f.pixel(view.bounds.right() - 10, view.bounds.center().y);
    CHECK((left & 0xFF) > 100 && ((left >> 16) & 0xFF) < 100);  // red-ish left
    CHECK(((right >> 16) & 0xFF) > 100 && (right & 0xFF) < 100); // blue-ish right
}

// ---------------------------------------------------------------------------
// Cursor requests
// ---------------------------------------------------------------------------
TEST(cursor_shape_requests) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    root.set_padding(6); // a dead margin so a corner hit resolves to the root
    auto& field = root.add<slikui::TextField>();
    auto& view = root.add<slikui::ImageView>();
    view.set_pixels(8, 8, std::vector<std::uint8_t>(8 * 8 * 4, 128).data());
    f.frame();
    REQUIRE(f.has_font);

    f.cursors.clear();
    f.move(field.bounds.center().x, field.bounds.center().y);
    CHECK(!f.cursors.empty() && f.cursors.back() == slikui::Cursor::IBeam);

    f.cursors.clear();
    f.move(view.bounds.center().x, view.bounds.center().y);
    CHECK(!f.cursors.empty() && f.cursors.back() == slikui::Cursor::Grab);

    f.cursors.clear();
    f.move(2, 237); // dead margin → back to arrow
    CHECK(!f.cursors.empty() && f.cursors.back() == slikui::Cursor::Arrow);
}

// ---------------------------------------------------------------------------
// FileBrowser (real temp directory)
// ---------------------------------------------------------------------------
TEST(filebrowser_lists_navigates_picks) {
    Fixture f;
    // Temp dir with two files and one subdir.
    std::string dir = "/tmp/slikui_fbtest";
    std::filesystem::create_directories(dir + "/sub");
    { std::FILE* fp = std::fopen((dir + "/scene.png").c_str(), "wb"); if (fp) std::fclose(fp); }
    { std::FILE* fp = std::fopen((dir + "/mat.png").c_str(), "wb"); if (fp) std::fclose(fp); }

    auto& root = f.ctx.set_root<slikui::Column>();
    auto fb = std::make_unique<slikui::FileBrowser>(slikui::FileBrowser::Mode::Open);
    fb->set_path(dir);
    std::string picked;
    fb->on_pick([&](std::string p) { picked = p; });
    auto& ref = f.ctx.open_dialog(std::move(fb));
    f.frame();
    REQUIRE(f.has_font);
    CHECK(f.ctx.dialog_count() == 1);

    // Directory listing: ".." + subdir + 2 files (dirs first).
    auto& list = *dynamic_cast<slikui::ListView*>(ref.children[1]->children[1].get());
    CHECK(list.bounds.h > 0);

    // Click the subdir entry (row 1) → navigates into it.
    float row_y = list.bounds.y + 1.5f * 24.f;
    f.click(list.bounds.x + 30, row_y);
    f.frame();
    CHECK(f.ctx.dialog_count() == 1); // still open

    // Navigate back up via ".." (row 0), then pick a file: click row 1
    // ("mat.png" or "scene.png"), then Open.
    f.click(list.bounds.x + 30, list.bounds.y + 12.f); // ".."
    f.frame();
    // entries: "..", "sub/", then sorted files — first file is row 2.
    f.click(list.bounds.x + 30, list.bounds.y + 2 * 24.f + 12.f);
    f.frame();
    // Open = the primary button in the button row (content last Row, 3rd child).
    auto& content = *ref.children[1];
    auto& buttons = *content.children[content.children.size() - 1];
    auto& open_btn = *buttons.children[2];
    f.click(open_btn.bounds.center().x, open_btn.bounds.center().y);
    CHECK(!picked.empty());
    CHECK(picked.find("slikui_fbtest") != std::string::npos);
    CHECK(picked.find(".png") != std::string::npos);
    CHECK(f.ctx.dialog_count() == 0); // commit closes the dialog

    std::filesystem::remove_all(dir);
}
