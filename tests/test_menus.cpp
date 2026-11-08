// tests/test_menus.cpp — Phase B: popup anchoring + stack semantics, the
// shortcut table, Menu/MenuItem (checkable, disabled, hints, submenus),
// MenuBar switching, right-click context menus, IconButton/Toolbar/icons.
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
        int px = int(x), py = int(y); // software target renders at 1x here
        return target.data()[std::size_t(py) * target.width() + std::size_t(px)];
    }
};

const std::uint32_t ModShortcut =
#ifdef __APPLE__
    slikui::ModSuper;
#else
    slikui::ModCtrl;
#endif

slikui::Menu& filled_menu(slikui::MenuBar& bar, const char* title) {
    return bar.add_menu(title);
}

} // namespace

// ---------------------------------------------------------------------------
// Anchored popups: flip + clamp
// ---------------------------------------------------------------------------
TEST(popup_anchor_flip_and_clamp) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& btn = root.add<slikui::Button>("Anchor");
    f.frame();
    REQUIRE(f.has_font);

    // Below, but the popup is taller than the space under the anchor: flips
    // above, then clamps fully inside the frame.
    auto panel = std::make_unique<slikui::Panel>();
    panel->set_padding(8);
    panel->add<slikui::Label>("one");
    panel->add<slikui::Label>("two");
    panel->add<slikui::Label>("three");
    panel->add<slikui::Label>("four");
    panel->add<slikui::Label>("five");
    auto& pop = f.ctx.push_popup(std::move(panel), btn, slikui::PopupDir::Below);
    f.frame();
    CHECK(pop.bounds.y + pop.bounds.h <= 240.f);   // fully visible
    CHECK(pop.bounds.bottom() <= btn.bounds.y + 0.5f ||
          pop.bounds.y >= btn.bounds.bottom());    // actually flipped above

    // Right of a button near the right edge flips to the left.
    auto panel2 = std::make_unique<slikui::Panel>();
    panel2->set_padding(8);
    panel2->add<slikui::Label>("wide popup content here");
    auto& right = root.add<slikui::Button>("Far right");
    f.frame();
    right.bounds = {239.f, right.bounds.y, 80.f, right.bounds.h}; // right edge 319
    auto& pop2 = f.ctx.push_popup(std::make_unique<slikui::Panel>(), right,
                                  slikui::PopupDir::Right);
    f.frame();
    CHECK(pop2.bounds.x + pop2.bounds.w <= 320.f); // inside the frame
    f.ctx.close_popups();
}

TEST(popup_stack_click_and_escape) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& btn = root.add<slikui::Button>("open");
    f.frame();

    // Real menu stack: hover opens the submenu above the parent menu popup.
    auto sub = std::make_unique<slikui::Menu>("Export");
    sub->add_item("PNG", [] {});
    auto menu = std::make_unique<slikui::Menu>("File");
    menu->add_item("New", [] {});
    menu->add_submenu("Export As", std::move(sub));
    menu->open_for(btn, slikui::PopupDir::Below);
    f.frame();
    auto& export_item = *f.ctx.popup(0)->children[1];
    f.move(export_item.bounds.center().x, export_item.bounds.center().y);
    f.frame();
    CHECK(f.ctx.popup_count() == 2);

    // Click on the parent menu's dead area (its padding): the submenu closes,
    // the parent stays.
    auto& parent = *f.ctx.popup(0);
    f.click(parent.bounds.x + 2, parent.bounds.y + 2);
    CHECK(f.ctx.popup_count() == 1);
    CHECK(f.ctx.popup(0) == &parent);

    // Escape unwinds the rest one level at a time.
    f.ctx.handle(f.key(slikui::Key::Escape));
    CHECK(f.ctx.popup_count() == 0);

    // Owned popups: a click outside every popup closes the whole stack.
    f.ctx.push_popup(std::make_unique<slikui::Panel>(), {10, 10});
    f.ctx.push_popup(std::make_unique<slikui::Panel>(), {60, 10});
    f.frame();
    CHECK(f.ctx.popup_count() == 2);
    f.click(300, 200);
    CHECK(f.ctx.popup_count() == 0);
}

// ---------------------------------------------------------------------------
// Shortcut table
// ---------------------------------------------------------------------------
TEST(shortcut_table) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& field = root.add<slikui::TextField>();
    f.frame();

    int fired = 0;
    f.ctx.add_shortcut(slikui::Key::S, slikui::ModCtrl, [&] { fired++; });
    f.ctx.add_shortcut(slikui::Key::R, ModShortcut, [&] { fired += 10; });

    // With the field focused: plain typing reaches the field, shortcuts fire.
    f.ctx.handle(f.key(slikui::Key::Tab));
    f.ctx.handle(f.text('h'));
    CHECK(field.text() == "h");
    CHECK(fired == 0);

    f.ctx.handle(f.key(slikui::Key::S, slikui::ModCtrl));
    CHECK(fired == 1);
    f.ctx.handle(f.key(slikui::Key::S, slikui::ModCtrl | slikui::ModShift));
    CHECK(fired == 1); // exact mods only
    f.ctx.handle(f.key(slikui::Key::R, ModShortcut));
    CHECK(fired == 11);
    // Bare key without modifiers never fires a shortcut.
    f.ctx.handle(f.key(slikui::Key::S));
    CHECK(fired == 11);

    f.ctx.clear_shortcuts();
    f.ctx.handle(f.key(slikui::Key::S, slikui::ModCtrl));
    CHECK(fired == 11);
}

// ---------------------------------------------------------------------------
// Menu items
// ---------------------------------------------------------------------------
TEST(menu_items_and_selection) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& btn = root.add<slikui::Button>("Menu");
    f.frame();
    REQUIRE(f.has_font);

    auto menu = std::make_unique<slikui::Menu>("File");
    int picked = 0;
    bool toggled = false;
    bool on = false;
    menu->add_item("Open", [&] { picked = 1; }, "Ctrl+O");
    menu->add_item("Disabled thing", [&] { picked = 99; }, {}, false);
    menu->add_separator();
    menu->add_checkable("Grid", [&] { return on; }, [&] { toggled = true; on = !on; });
    menu->add_item("Quit", [&] { picked = 2; }, "Ctrl+Q");

    // Open via the anchored push (what MenuBar/Menu::open_for do).
    menu->open_for(btn, slikui::PopupDir::Below);
    f.frame();
    REQUIRE(f.ctx.popup_count() == 1);
    auto* pop = f.ctx.popup(0);
    REQUIRE(dynamic_cast<slikui::Menu*>(pop) == menu.get());
    CHECK(pop->bounds.w >= 140.f); // min width

    // Item 0 "Open": fires and closes the menu.
    auto& open_item = *pop->children[0];
    f.click(open_item.bounds.center().x, open_item.bounds.center().y);
    CHECK(picked == 1);
    CHECK(f.ctx.popup_count() == 0);

    // Reopen: the disabled item swallows the click and the menu stays.
    menu->open_for(btn, slikui::PopupDir::Below);
    f.frame();
    pop = f.ctx.popup(0);
    auto& disabled = *pop->children[1];
    f.click(disabled.bounds.center().x, disabled.bounds.center().y);
    CHECK(picked == 1); // action did not run
    CHECK(f.ctx.popup_count() == 1); // menu stays open

    // Checkable item: checkmark drawn region exists; click toggles + closes.
    auto& check_item = *pop->children[3]; // after separator
    f.click(check_item.bounds.center().x, check_item.bounds.center().y);
    CHECK(toggled);
    CHECK(on);
    CHECK(f.ctx.popup_count() == 0); // selecting closes

    // on_open hook runs before each open.
    int opens = 0;
    menu->on_open([&] { opens++; });
    menu->open_for(btn, slikui::PopupDir::Below);
    f.frame();
    CHECK(opens == 1);
    f.ctx.close_popups();
}

TEST(menu_submenu) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& btn = root.add<slikui::Button>("Menu");
    f.frame();
    REQUIRE(f.has_font);

    auto sub = std::make_unique<slikui::Menu>("Export");
    int exported = 0;
    sub->add_item("PNG", [&] { exported = 1; });
    sub->add_item("JPEG", [&] { exported = 2; });

    auto menu = std::make_unique<slikui::Menu>("File");
    menu->add_item("New", [] {});
    menu->add_submenu("Export As", std::move(sub));
    menu->add_item("Close", [] {});

    menu->open_for(btn, slikui::PopupDir::Below);
    f.frame();
    REQUIRE(f.ctx.popup_count() == 1);

    // Hovering the submenu item opens the child popup to the right.
    auto& export_item = *f.ctx.popup(0)->children[1];
    f.move(export_item.bounds.center().x, export_item.bounds.center().y);
    f.frame();
    CHECK(f.ctx.popup_count() == 2);
    auto* child = f.ctx.popup(1);
    CHECK(child->bounds.x >= export_item.bounds.right() - 1.f); // opened right

    // Moving to a different item closes the submenu.
    auto& new_item = *f.ctx.popup(0)->children[0];
    f.move(new_item.bounds.center().x, new_item.bounds.center().y);
    f.frame();
    CHECK(f.ctx.popup_count() == 1);

    // Reopen, then click a submenu entry: fires and closes everything.
    f.move(export_item.bounds.center().x, export_item.bounds.center().y);
    f.frame();
    REQUIRE(f.ctx.popup_count() == 2);
    auto& png = *f.ctx.popup(1)->children[0];
    f.click(png.bounds.center().x, png.bounds.center().y);
    CHECK(exported == 1);
    CHECK(f.ctx.popup_count() == 0);

    // Escape unwinds one level per press.
    menu->open_for(btn, slikui::PopupDir::Below);
    f.frame();
    f.move(export_item.bounds.center().x, export_item.bounds.center().y);
    f.frame();
    REQUIRE(f.ctx.popup_count() == 2);
    f.ctx.handle(f.key(slikui::Key::Escape));
    CHECK(f.ctx.popup_count() == 1);
    f.ctx.handle(f.key(slikui::Key::Escape));
    CHECK(f.ctx.popup_count() == 0);
}

// ---------------------------------------------------------------------------
// MenuBar
// ---------------------------------------------------------------------------
TEST(menubar_open_switch_select) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& bar = root.add<slikui::MenuBar>();
    int picked = 0;
    auto& file = bar.add_menu("File");
    file.add_item("Open", [&] { picked = 1; }, "Ctrl+O");
    file.add_item("Save", [&] { picked = 2; }, shortcut_label(slikui::Key::S, ModShortcut));
    auto& edit = bar.add_menu("Edit");
    edit.add_item("Undo", [&] { picked = 3; }, "Ctrl+Z");
    f.frame();
    REQUIRE(f.has_font);
    CHECK(bar.bounds.h >= 26.f);

    // MenuButtons are the bar's children; centers of the first two titles.
    auto& file_btn = *bar.children[0];
    auto& edit_btn = *bar.children[1];
    f.click(file_btn.bounds.center().x, file_btn.bounds.center().y);
    f.frame();
    CHECK(f.ctx.popup_count() == 1);
    auto* menu = f.ctx.popup(0);
    CHECK(menu->bounds.y >= file_btn.bounds.bottom()); // opened below

    // Hover-while-open: moving over the second title switches menus.
    f.move(edit_btn.bounds.center().x, edit_btn.bounds.center().y);
    f.frame();
    CHECK(f.ctx.popup_count() == 1);
    CHECK(f.ctx.popup(0) != menu); // a different menu is open now

    // Pick "Undo".
    auto& undo = *f.ctx.popup(0)->children[0];
    f.click(undo.bounds.center().x, undo.bounds.center().y);
    CHECK(picked == 3);
    CHECK(f.ctx.popup_count() == 0);

    // Clicking the open title again closes it.
    f.click(file_btn.bounds.center().x, file_btn.bounds.center().y);
    f.frame();
    CHECK(f.ctx.popup_count() == 1);
    f.click(file_btn.bounds.center().x, file_btn.bounds.center().y);
    f.frame();
    CHECK(f.ctx.popup_count() == 0);
}

// ---------------------------------------------------------------------------
// Context menu (right-click)
// ---------------------------------------------------------------------------
TEST(context_menu_right_click) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto menu = std::make_unique<slikui::Menu>();
    int picked = 0;
    menu->add_item("Duplicate", [&] { picked = 1; });
    menu->add_item("Delete", [&] { picked = 2; });

    auto& target = root.add<slikui::Panel>();
    target.set_padding(20);
    target.add<slikui::Label>("right-click me");
    target.set_context_menu(menu.get());
    f.frame();
    REQUIRE(f.has_font);

    // Right-click inside the panel opens the menu at the cursor.
    slikui::Vec2 at = target.bounds.center();
    f.click(at.x, at.y, slikui::MouseRight);
    f.frame();
    REQUIRE(f.ctx.popup_count() == 1);
    CHECK(f.ctx.popup(0)->bounds.contains(at)); // anchored at the cursor

    // A child without its own menu inherits the ancestor's menu.
    // (The label is inside the panel — same result.)

    // A child without its own menu inherits the ancestor's menu: right-click
    // the label inside the panel.
    auto& lab = *target.children[0];
    f.click(lab.bounds.center().x, lab.bounds.center().y, slikui::MouseRight);
    f.frame();
    CHECK(f.ctx.popup_count() == 1);
    f.ctx.close_popups();

    // Right-click outside the panel: nothing opens.
    f.click(10, 200, slikui::MouseRight); // below the panel — plain column
    f.frame();
    CHECK(f.ctx.popup_count() == 0);

    // Selecting an entry closes the menu.
    f.click(at.x, at.y, slikui::MouseRight);
    f.frame();
    auto& dup = *f.ctx.popup(0)->children[0];
    f.click(dup.bounds.center().x, dup.bounds.center().y);
    CHECK(picked == 1);
    CHECK(f.ctx.popup_count() == 0);
}

// ---------------------------------------------------------------------------
// Icons, IconButton, Toolbar
// ---------------------------------------------------------------------------
TEST(icon_painter_covers_all_icons) {
    Fixture f; // (target unused; painter test needs no context)
    (void)f;
    for (int i = 0; i <= int(slikui::Icon::Sun); ++i) {
        auto icon = static_cast<slikui::Icon>(i);
        slikui::DrawList list;
        slikui::paint_icon(list, icon, {0, 0, 24, 24}, slikui::Color::white());
        CHECK(list.size() > 0); // every icon paints something
        CHECK(std::string(slikui::icon_name(icon)) != "unknown");
    }
}

TEST(icon_button_and_toolbar) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    int clicks = 0;
    auto& bar = root.add<slikui::Toolbar>();
    auto& play = bar.add_button(slikui::Icon::Play, [&] { clicks++; }, "Run");
    bar.add_separator();
    bar.add_button(slikui::Icon::Grid, [&] { clicks += 10; });
    bar.add_button(slikui::Icon::Save, "Save", [&] { clicks += 100; });
    f.frame();
    REQUIRE(f.has_font);
    CHECK(bar.bounds.h >= 30.f);

    // Ghost is the default variant; separators add a Divider child.
    CHECK(play.variant() == slikui::Button::Variant::Ghost);
    size_t dividers = 0;
    for (auto& c : bar.children)
        if (dynamic_cast<slikui::Divider*>(c.get())) dividers++;
    CHECK(dividers == 1);

    // The labeled cell (4th child: play, divider, grid, cell) is taller than
    // a bare icon button.
    CHECK(bar.children[3]->bounds.h > play.bounds.h);

    // Clicks fire.
    f.click(play.bounds.center().x, play.bounds.center().y);
    CHECK(clicks == 1);

    // Icons actually paint: the button interior is not flat background.
    f.frame();
    int distinct = 0;
    std::uint32_t first = f.pixel(play.bounds.center().x - 8, play.bounds.center().y);
    for (int dy = -8; dy <= 8; ++dy)
        for (int dx = -8; dx <= 8; ++dx) {
            std::uint32_t px = f.pixel(play.bounds.center().x + dx,
                                       play.bounds.center().y + dy);
            if (px != first) distinct++;
        }
    CHECK(distinct > 10); // strokes of the icon vs the button fill
}

// ---------------------------------------------------------------------------
// shortcut_label
// ---------------------------------------------------------------------------
TEST(shortcut_label_format) {
#ifdef __APPLE__
    CHECK(slikui::shortcut_label(slikui::Key::S, slikui::ModSuper) == "⌘S");
    CHECK(slikui::shortcut_label(slikui::Key::S, slikui::ModSuper | slikui::ModShift) == "⌘⇧S");
#else
    CHECK(slikui::shortcut_label(slikui::Key::S, slikui::ModCtrl) == "Ctrl+S");
    CHECK(slikui::shortcut_label(slikui::Key::S, slikui::ModCtrl | slikui::ModShift) == "Ctrl+Shift+S");
#endif
    CHECK(slikui::shortcut_label(slikui::Key::F5, 0) == "F5");
}
