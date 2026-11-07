// tests/test_layout.cpp — constraints and flex distribution.
#include "test.hpp"

#include "slikui/layout/layout.hpp"

TEST(constraints_clamp) {
    slikui::Constraints c = slikui::Constraints::loose({100, 100});
    slikui::Size s = c.clamp({150, 50});
    CHECK(s.w == 100 && s.h == 50);

    slikui::Constraints t = slikui::Constraints::tight({30, 40});
    slikui::Size r = t.clamp({999, 0});
    CHECK(r.w == 30 && r.h == 40);
}

TEST(constraints_tighten_padding) {
    slikui::Constraints c = slikui::Constraints::loose({100, 100});
    slikui::Constraints t = c.tighten(slikui::Padding::all(10));
    CHECK(t.max.x == 80 && t.max.y == 80);
}

TEST(distribute_no_expanders) {
    auto spans = slikui::distribute(300, {100, 100}, {false, false}, 10);
    CHECK(spans.size() == 2);
    CHECK(spans[0].first == 0 && spans[0].second == 100);
    CHECK(spans[1].first == 110 && spans[1].second == 100);
}

TEST(distribute_expands_fill) {
    auto spans = slikui::distribute(300, {100, 100}, {false, true}, 10);
    CHECK(spans[1].second == 190); // leftover 90 goes to the expander
    CHECK(spans[0].first == 0);
    CHECK(spans[1].first == 110);
}

TEST(distribute_two_expanders_split_evenly) {
    auto spans = slikui::distribute(320, {0, 0}, {true, true}, 0);
    CHECK(spans[0].first == 0 && spans[0].second == 160);
    CHECK(spans[1].first == 160 && spans[1].second == 160);
}
