// tests/test_batcher.cpp - GpuBatcher: DrawList -> instances + batches.
// Pure CPU: no GL needed, runs everywhere.
#include "test.hpp"

#include "slikui/render/batcher.hpp"

using namespace slikui;

namespace {

const Color kRed{255, 0, 0, 255};

int kind(const GpuInstance& in) { return int(in.shape[0] + .5f); }

} // namespace

TEST(batcher_empty_list) {
    GpuBatcher b;
    DrawList l;
    b.build(l, {100, 50}, 1.f);
    CHECK(b.instances().empty());
    CHECK(b.batches().empty());
    CHECK(b.framebuffer_width() == 100 && b.framebuffer_height() == 50);
}

TEST(batcher_one_instance_per_drawable) {
    GpuBatcher b;
    DrawList l;
    l.rect({1, 2, 10, 5}, kRed, 2);
    l.hollow_rect({1, 2, 10, 5}, kRed, 1.5f, 0);
    l.line({0, 0}, {20, 10}, kRed, 2);
    l.glyph({5, 5, 4, 6}, 7, {0, 0, 4, 6}, kRed);
    l.image({0, 0, 8, 8}, 9, {0, 0, 8, 8});
    b.build(l, {100, 50}, 1.f);
    CHECK(b.instances().size() == 5);
    const auto& in = b.instances();
    CHECK(kind(in[0]) == int(GpuKind::Rect) && in[0].shape[1] == 2 && in[0].shape[2] == 0);
    CHECK(kind(in[1]) == int(GpuKind::Rect) && in[1].shape[2] == 1.5f);
    CHECK(kind(in[2]) == int(GpuKind::Line) && in[2].shape[3] == 1.f);
    CHECK(in[2].line[2] == 20 && in[2].line[3] == 10);
    // capsule bbox includes the half thickness
    CHECK(in[2].rect[0] == -1 && in[2].rect[1] == -1 && in[2].rect[2] == 22 && in[2].rect[3] == 12);
    CHECK(kind(in[3]) == int(GpuKind::Glyph) && in[3].uv[2] == 4 && in[3].uv[3] == 6);
    CHECK(kind(in[4]) == int(GpuKind::Image) && in[4].color[3] == 1.f);
    CHECK(in[0].color[0] == 1.f && in[0].color[1] == 0.f);
    // glyph tex 7, image tex 9: two batches
    CHECK(b.batches().size() == 2);
    CHECK(b.batches()[0].tex_id == 7 && b.batches()[0].count == 4);
    CHECK(b.batches()[1].tex_id == 9 && b.batches()[1].first == 4 && b.batches()[1].count == 1);
}

TEST(batcher_shapes_do_not_split_texture_runs) {
    GpuBatcher b;
    DrawList l;
    l.glyph({0, 0, 4, 4}, 3, {0, 0, 4, 4}, kRed);
    l.rect({0, 0, 4, 4}, kRed);
    l.glyph({5, 0, 4, 4}, 3, {0, 0, 4, 4}, kRed);
    l.line({0, 0}, {5, 5}, kRed);
    l.glyph({9, 0, 4, 4}, 3, {0, 0, 4, 4}, kRed);
    b.build(l, {100, 50}, 1.f);
    CHECK(b.batches().size() == 1);
    CHECK(b.batches()[0].count == 5 && b.batches()[0].tex_id == 3);
}

TEST(batcher_clip_batches_and_scissor) {
    GpuBatcher b;
    DrawList l;
    l.rect({0, 0, 10, 10}, kRed);
    l.push_clip({10, 5, 20, 10});
    l.rect({12, 6, 4, 4}, kRed);
    l.push_clip({0, 0, 15, 100}); // intersects to {10, 5, 5, 10}
    l.rect({11, 6, 2, 2}, kRed);
    l.pop_clip();
    l.pop_clip();
    l.rect({40, 20, 5, 5}, kRed);
    b.build(l, {100, 50}, 1.f);
    const auto& bt = b.batches();
    CHECK(bt.size() == 4);
    // root clip: whole framebuffer
    CHECK(bt[0].scissor[0] == 0 && bt[0].scissor[1] == 0 && bt[0].scissor[2] == 100 &&
          bt[0].scissor[3] == 50);
    // {10,5,20,10}, y flipped: 50 - 15 = 35
    CHECK(bt[1].scissor[0] == 10 && bt[1].scissor[1] == 35 && bt[1].scissor[2] == 20 &&
          bt[1].scissor[3] == 10);
    CHECK(bt[2].scissor[0] == 10 && bt[2].scissor[1] == 35 && bt[2].scissor[2] == 5 &&
          bt[2].scissor[3] == 10);
    CHECK(bt[3].scissor[2] == 100 && bt[3].first == 3);
}

TEST(batcher_scissor_scale_and_rounding) {
    GpuBatcher b;
    DrawList l;
    l.push_clip({10.25f, 5.5f, 20, 10});
    l.rect({12, 6, 4, 4}, kRed);
    l.pop_clip();
    b.build(l, {100, 50}, 2.f);
    CHECK(b.framebuffer_width() == 200 && b.framebuffer_height() == 100);
    const auto& s = b.batches()[0].scissor;
    // px: x 20.5..60.5 -> pixels 21..60; y 11..31 -> rows 11..30
    CHECK(s[0] == 21 && s[2] == 40);
    CHECK(s[1] == 100 - 31 && s[3] == 20);
    // instances stay logical
    CHECK(b.instances()[0].rect[0] == 12 && b.instances()[0].rect[2] == 4);
}

TEST(batcher_culls_outside_clip) {
    GpuBatcher b;
    DrawList l;
    l.push_clip({0, 0, 10, 10});
    l.rect({50, 50, 5, 5}, kRed);                       // outside
    l.glyph({-20, 0, 4, 4}, 3, {0, 0, 4, 4}, kRed);     // outside
    l.rect({9.5f, 9.5f, 5, 5}, kRed);                    // overlaps the corner
    l.pop_clip();
    l.push_clip({5, 5, 0, 10});                          // empty clip
    l.rect({5, 5, 5, 5}, kRed);
    l.pop_clip();
    b.build(l, {100, 50}, 1.f);
    CHECK(b.instances().size() == 1);
    CHECK(b.batches().size() == 1);
}

TEST(batcher_skips_invisible) {
    GpuBatcher b;
    DrawList l;
    l.rect({0, 0, 10, 10}, Color{1, 2, 3, 0});           // transparent
    l.hollow_rect({0, 0, 10, 10}, kRed, 0.f);            // zero border
    l.line({0, 0}, {5, 5}, kRed, 0.f);                   // zero thickness
    l.glyph({0, 0, 4, 4}, 0, {0, 0, 4, 4}, kRed);        // no texture
    l.glyph({0, 0, 4, 4}, 3, {0, 0, 0, 4}, kRed);        // empty uv
    l.line({3, 3}, {3, 3}, kRed, 2.f);                   // dot: drawn
    b.build(l, {100, 50}, 1.f);
    CHECK(b.instances().size() == 1);
    CHECK(kind(b.instances()[0]) == int(GpuKind::Line));
}

TEST(batcher_unbalanced_pop_is_safe) {
    GpuBatcher b;
    DrawList l;
    l.pop_clip();
    l.rect({0, 0, 10, 10}, kRed);
    b.build(l, {100, 50}, 1.f);
    CHECK(b.instances().size() == 1);
    CHECK(b.batches()[0].scissor[2] == 100);
}

TEST(batcher_reuse_clears_previous_frame) {
    GpuBatcher b;
    DrawList l;
    for (int i = 0; i < 50; ++i) l.rect({float(i), 0, 1, 1}, kRed);
    b.build(l, {100, 50}, 1.f);
    CHECK(b.instances().size() == 50);
    DrawList l2;
    l2.rect({0, 0, 1, 1}, kRed);
    b.build(l2, {100, 50}, 1.f);
    CHECK(b.instances().size() == 1 && b.batches().size() == 1 && b.batches()[0].count == 1);
}
