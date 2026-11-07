// tests/test_layout.cpp — constraints and flex distribution.
#include "test.hpp"

#include "silk/layout/layout.hpp"

TEST(constraints_clamp) {
    silk::Constraints c = silk::Constraints::loose({100, 100});
    silk::Size s = c.clamp({150, 50});
    CHECK(s.w == 100 && s.h == 50);

    silk::Constraints t = silk::Constraints::tight({30, 40});
    silk::Size r = t.clamp({999, 0});
    CHECK(r.w == 30 && r.h == 40);
}

TEST(constraints_tighten_padding) {
    silk::Constraints c = silk::Constraints::loose({100, 100});
    silk::Constraints t = c.tighten(silk::Padding::all(10));
    CHECK(t.max.x == 80 && t.max.y == 80);
}

TEST(distribute_no_expanders) {
    auto spans = silk::distribute(300, {100, 100}, {false, false}, 10);
    CHECK(spans.size() == 2);
    CHECK(spans[0].first == 0 && spans[0].second == 100);
    CHECK(spans[1].first == 110 && spans[1].second == 100);
}

TEST(distribute_expands_fill) {
    auto spans = silk::distribute(300, {100, 100}, {false, true}, 10);
    CHECK(spans[1].second == 190); // leftover 90 goes to the expander
    CHECK(spans[0].first == 0);
    CHECK(spans[1].first == 110);
}

TEST(distribute_two_expanders_split_evenly) {
    auto spans = silk::distribute(320, {0, 0}, {true, true}, 0);
    CHECK(spans[0].first == 0 && spans[0].second == 160);
    CHECK(spans[1].first == 160 && spans[1].second == 160);
}
