// tests/test_textures.cpp - texture lifetime through IRenderSink:
// upload / release / invalidate, dialogs, and re-attachment.
#include "test.hpp"

#include "slikui/context.hpp"
#include "slikui/widgets/all.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace slikui;

namespace {

struct RecordingSink : IRenderSink {
    std::vector<std::uint32_t> uploads, released;
    std::vector<std::uint32_t> drawn; // Image tex ids in the last frame
    void upload(const TexUpload& up) override { uploads.push_back(up.id); }
    void release(std::uint32_t id) override { released.push_back(id); }
    void render(const DrawList& l, Vec2) override {
        drawn.clear();
        for (const auto& c : l.commands())
            if (c.type == DrawCmd::Type::Image) drawn.push_back(c.tex_id);
    }
};

const std::vector<std::uint8_t>& pixels4x4() {
    static const std::vector<std::uint8_t> px(4 * 4 * 4, 200);
    return px;
}

void frame(Context& ctx) {
    ctx.tick(Time{0.5, 0.016});
    ctx.render();
}

bool contains(const std::vector<std::uint32_t>& v, std::uint32_t id) {
    return std::find(v.begin(), v.end(), id) != v.end();
}

} // namespace

TEST(texture_release_on_destroy) {
    RecordingSink sink;
    Context ctx(sink);
    ctx.set_frame_size({200, 150});
    auto& root = ctx.set_root<Column>();
    root.add<Image>().set_pixels(4, 4, pixels4x4().data());
    auto& cv = root.add<CompareView>();
    cv.set_pixels_a(4, 4, pixels4x4().data());
    cv.set_pixels_b(4, 4, pixels4x4().data());
    frame(ctx);
    CHECK(sink.drawn.size() == 3);
    CHECK(sink.released.empty());
    std::vector<std::uint32_t> drawn = sink.drawn;
    ctx.clear_tree();
    CHECK(sink.released.size() == 3);
    for (auto id : drawn) CHECK(contains(sink.released, id));
}

TEST(texture_no_release_from_context_destructor) {
    RecordingSink sink;
    {
        Context ctx(sink);
        ctx.set_frame_size({200, 150});
        ctx.set_root<Column>().add<Image>().set_pixels(4, 4, pixels4x4().data());
        frame(ctx);
    }
    CHECK(sink.released.empty());
}

TEST(texture_invalidate_reuploads_once) {
    RecordingSink sink;
    Context ctx(sink);
    ctx.set_frame_size({200, 150});
    ctx.set_root<Column>().add<Image>().set_pixels(4, 4, pixels4x4().data());
    frame(ctx);
    CHECK(sink.uploads.size() == 1); // no font loaded: the image only
    sink.uploads.clear();
    frame(ctx);
    CHECK(sink.uploads.empty());
    ctx.invalidate_textures();
    frame(ctx);
    CHECK(sink.uploads.size() == 1);
    sink.uploads.clear();
    frame(ctx);
    CHECK(sink.uploads.empty());
}

TEST(texture_image_in_dialog_uploads) {
    RecordingSink sink;
    Context ctx(sink);
    ctx.set_frame_size({300, 200});
    ctx.set_root<Column>();
    Dialog dlg("Pic");
    dlg.content().add<Image>().set_pixels(4, 4, pixels4x4().data());
    ctx.open_dialog(dlg);
    frame(ctx);
    CHECK(sink.drawn.size() == 1);
    CHECK(sink.drawn.size() == 1 && contains(sink.uploads, sink.drawn[0]));
    ctx.close_dialog(dlg);
}

TEST(texture_reattach_to_new_context_uploads) {
    RecordingSink sink_a, sink_b;
    Context a(sink_a), b(sink_b);
    a.set_frame_size({300, 200});
    b.set_frame_size({300, 200});
    a.set_root<Column>();
    b.set_root<Column>();
    Dialog dlg("Pic");
    dlg.content().add<Image>().set_pixels(4, 4, pixels4x4().data());
    a.open_dialog(dlg);
    frame(a);
    CHECK(sink_a.uploads.size() == 1);
    a.close_dialog(dlg);
    b.open_dialog(dlg);
    frame(b);
    CHECK(sink_b.drawn.size() == 1);
    CHECK(sink_b.drawn.size() == 1 && contains(sink_b.uploads, sink_b.drawn[0]));
    b.close_dialog(dlg);
}
