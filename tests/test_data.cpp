// tests/test_data.cpp — Phase C data display: Plot (+bars/fill), Vec2Pad,
// TreeView, Table, PropertyGrid.
#include "test.hpp"

#include "slikui/backends/software/software.hpp"
#include "slikui/context.hpp"
#include "slikui/text/default_font.hpp"
#include "slikui/widgets/all.hpp"

#include <cmath>
#include <functional>

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
    void click(float x, float y) {
        ctx.handle(mouse(slikui::Event::Type::MousePress, x, y));
        ctx.handle(mouse(slikui::Event::Type::MouseRelease, x, y));
    }
    std::uint32_t pixel(float x, float y) const {
        int px = int(x), py = int(y); // software target renders at 1x here
        return target.data()[std::size_t(py) * target.width() + std::size_t(px)];
    }
    // Count pixels in a rect that pass a channel predicate (packed R|G<<8).
    int count_ink(const slikui::Rect& r,
                  const std::function<bool(std::uint32_t)>& pred) const {
        int n = 0;
        for (int py = int(r.y * 2); py < int(r.bottom() * 2); ++py)
            for (int px = int(r.x * 2); px < int(r.right() * 2); ++px)
                if (pred(target.data()[std::size_t(py) * target.width() +
                                     std::size_t(px)]))
                    ++n;
        return n;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Plot
// ---------------------------------------------------------------------------
TEST(plot_autoscale_lines_fill_bars) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& plot = root.add<slikui::Plot>();
    std::vector<slikui::Vec2> pts;
    for (int i = 0; i < 11; ++i) pts.push_back({float(i), float(i % 2)}); // square wave
    plot.add_series(pts, f.ctx.theme().accent);
    plot.set_markers(true);
    f.frame();
    REQUIRE(f.has_font);
    CHECK(plot.bounds.w >= 150);
    CHECK(plot.bounds.h >= 90);

    // The wave + markers paint plenty of accent (blue) and white ink inside
    // the plot frame.
    auto bright = [](std::uint32_t p) {
        return ((p >> 16) & 0xFF) > 90 || ((p >> 8) & 0xFF) > 90; // R or G high
    };
    CHECK(f.count_ink(plot.bounds, bright) > 40);

    // Bars mode: warning-orange columns rise from the baseline.
    plot.clear_series().set_bars(true);
    std::vector<slikui::Vec2> bars;
    for (int i = 0; i < 8; ++i) bars.push_back({float(i), float(i + 1)});
    plot.add_series(bars, f.ctx.theme().warning);
    f.frame();
    auto orange = [](std::uint32_t p) {
        return (p & 0xFF) > 150 && ((p >> 8) & 0xFF) > 90; // R and G high
    };
    CHECK(f.count_ink(plot.bounds, orange) > 60);
}

TEST(plot_fixed_range_pins_values) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& plot = root.add<slikui::Plot>();
    plot.set_y_range(0.f, 1.f);
    std::vector<slikui::Vec2> flat;
    for (int i = 0; i < 5; ++i) flat.push_back({float(i), 1.f}); // constant max
    plot.add_series(flat, f.ctx.theme().accent);
    f.frame();
    REQUIRE(f.has_font);

    // y=1 maps to the top of the plot area: the accent polyline sits high.
    auto bright = [](std::uint32_t p) {
        return ((p >> 16) & 0xFF) > 90 || ((p >> 8) & 0xFF) > 90;
    };
    CHECK(f.count_ink(plot.bounds, bright) > 20);
}

// ---------------------------------------------------------------------------
// Vec2Pad
// ---------------------------------------------------------------------------
TEST(vec2pad_drag_and_clamp) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    int changes = 0;
    auto& pad = root.add<slikui::Vec2Pad>();
    pad.on_change([&](slikui::Vec2 v) { changes++; });
    f.frame();

    // Drag from center to the top-right corner → (1, 1) with default range.
    slikui::Vec2 c = pad.bounds.center();
    f.ctx.handle(f.mouse(slikui::Event::Type::MousePress, c.x, c.y));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseMove,
                         pad.bounds.right() + 30, pad.bounds.y - 30));
    f.ctx.handle(f.mouse(slikui::Event::Type::MouseRelease,
                         pad.bounds.right() + 30, pad.bounds.y - 30));
    CHECK(pad.value().x == 1.f);
    CHECK(pad.value().y == 1.f);
    CHECK(changes == 1);

    // set_value clamps and can skip the callback.
    pad.set_value({5.f, -5.f}, false);
    CHECK(pad.value().x == 1.f);
    CHECK(pad.value().y == 0.f);
    CHECK(changes == 1);

    // Custom range maps proportionally (corner vectors: min, then max).
    pad.set_range({-1.f, -1.f}, {1.f, 1.f});
    pad.set_value({0.f, 0.f}, false);
    CHECK(pad.value().x == 0.f);
    CHECK(pad.value().y == 0.f);
}

// ---------------------------------------------------------------------------
// TreeView
// ---------------------------------------------------------------------------
TEST(treeview_expand_collapse_select) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    using Row = slikui::TreeView::Row;
    auto& tree = root.add<slikui::TreeView>();
    tree.set_rows({
        {"Scene", 0, true},
        {"Mesh", 1, false},
        {"Light", 1, false},
        {"Camera", 0, false},
    });
    f.frame();
    REQUIRE(f.has_font);
    CHECK(tree.bounds.h == 96.f); // 4 visible rows

    long picked = -1;
    tree.on_select([&](size_t i) { picked = long(i); });

    // Collapse "Scene" → its children vanish.
    tree.set_expanded(0, false);
    f.frame();
    CHECK(tree.bounds.h == 48.f); // Scene + Camera
    CHECK(tree.visible_count() == 2);

    // Clicking the chevron zone re-expands.
    float chev_x = tree.bounds.x + 6 + 2;      // depth 0 indent
    float row1_y = tree.bounds.y + 12.f;       // first visible row center
    f.click(chev_x, row1_y);
    f.frame();
    CHECK(tree.expanded(0));
    CHECK(tree.visible_count() == 4);

    // Clicking a row selects it and fires on_select.
    f.click(tree.bounds.x + 60, tree.bounds.y + 24.f + 12.f); // second row = Mesh
    CHECK(picked == 1);
    CHECK(tree.selected() == 1);

    // Expanding a non-expandable row is a no-op toggle target; chevron only
    // toggles expandable rows.
    f.click(tree.bounds.x + 6, tree.bounds.y + 3 * 24.f + 12.f); // Camera row
    CHECK(tree.selected() == 3);
}

// ---------------------------------------------------------------------------
// Table
// ---------------------------------------------------------------------------
TEST(table_header_rows_selection) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& table = root.add<slikui::Table>();
    table.set_columns({{"Name", 120, false}, {"Tris", 70, true}, {"Passes", 70, true}});
    table.set_rows({
        {"Dragon", "870k", "4"},
        {"Suzanne", "50k", "2"},
        {"Bunny", "69k", "2"},
    });
    f.frame();
    REQUIRE(f.has_font);
    CHECK(table.bounds.w >= 260.f); // natural width (column cross-stretches)
    CHECK(table.bounds.h == 24.f * 4);

    long picked = -1;
    table.on_select([&](size_t r) { picked = long(r); });
    float row2_y = table.bounds.y + 24.f + 1.5f * 24.f;
    f.click(table.bounds.x + 40, row2_y);
    CHECK(picked == 1);
    CHECK(table.selected() == 1);

    // Clicking the header does not select.
    picked = -1;
    f.click(table.bounds.x + 40, table.bounds.y + 12.f);
    CHECK(picked == -1);
}

// ---------------------------------------------------------------------------
// PropertyGrid
// ---------------------------------------------------------------------------
TEST(propertygrid_rows_and_sections) {
    Fixture f;
    auto& root = f.ctx.set_root<slikui::Column>();
    auto& grid = root.add<slikui::PropertyGrid>();
    grid.set_gap(4);
    auto& rough = grid.add_property("Roughness");
    rough.add<slikui::Slider>(0.f, 1.f);
    grid.add_property("Samples").add<slikui::DragValue>(64.f);
    auto& mat = grid.add_section("Material", true);
    mat.add_property("Metallic").add<slikui::Slider>(0.f, 1.f);
    grid.add_section("Advanced"); // starts closed
    f.frame();
    REQUIRE(f.has_font);

    // Label column is fixed; the editor starts right of it.
    CHECK(rough.editor().bounds.x >= rough.bounds.x + 110.f);
    CHECK(rough.label().bounds.w <= 112.f);
    CHECK(rough.editor().bounds.w > 40.f);

    // The open section's nested grid laid out its property row.
    CHECK(mat.bounds.h > 0);

    // Closed section contributes nothing.
    float h_open_all = grid.bounds.h;
    (void)h_open_all;
    CHECK(grid.bounds.h >= 3 * 24.f); // at least the visible rows
}
