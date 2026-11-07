// tests/test_core.cpp — math, color, utf8.
#include "test.hpp"

#include "silk/core/color.hpp"
#include "silk/core/math.hpp"
#include "silk/core/utf8.hpp"

TEST(rect_contains_and_intersect) {
    silk::Rect r{10, 10, 50, 40};
    CHECK(r.contains({10, 10}));
    CHECK(r.contains({59, 49}));
    CHECK(!r.contains({60, 49}));
    CHECK(!r.contains({59.9f, 50}));

    silk::Rect a{0, 0, 10, 10};
    silk::Rect b{5, 5, 10, 10};
    silk::Rect i = a.intersect(b);
    CHECK(i.x == 5 && i.y == 5 && i.w == 5 && i.h == 5);

    silk::Rect far_away{100, 100, 10, 10};
    CHECK(a.intersect(far_away).empty());
}

TEST(padding_shrinks) {
    silk::Padding p = silk::Padding::all(4);
    silk::Rect r{0, 0, 100, 50};
    silk::Rect inner = p.apply(r);
    CHECK(inner.x == 4 && inner.y == 4 && inner.w == 92 && inner.h == 42);
}

TEST(color_blend) {
    silk::Color bg(0, 0, 0);
    silk::Color half_white(255, 255, 255, 128);
    silk::Color out = silk::blend(bg, half_white);
    CHECK(out.r == 128 && out.g == 128 && out.b == 128); // 255*128/255 ≈ 127.5→128

    silk::Color full(10, 20, 30, 255);
    CHECK(silk::blend(bg, full) == full); // opaque overwrites

    CHECK(silk::Color::from_hex(0xFF0000).r == 255);
    CHECK(silk::Color::from_hex(0xFF0000).g == 0);
}

TEST(color_abgr_roundtrip) {
    silk::Color c(1, 2, 3, 4);
    CHECK(silk::Color::from_abgr(c.to_abgr()) == c);
}

TEST(utf8_decode) {
    std::string s = "aé€𝄞"; // 1, 2, 3, 4 bytes
    size_t i = 0;
    CHECK(silk::utf8_next(s, i) == 'a');
    CHECK(silk::utf8_next(s, i) == 0xE9);
    CHECK(silk::utf8_next(s, i) == 0x20AC);
    CHECK(silk::utf8_next(s, i) == 0x1D11E);
    CHECK(i == s.size());
    CHECK(silk::utf8_next(s, i) == 0); // past end
}
