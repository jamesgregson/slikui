// tests/smoke.cpp — compile-and-link every public header in one TU.
#include "slikui/context.hpp"
#include "slikui/backends/software/software.hpp"
#include "slikui/widgets/containers.hpp"
#include "slikui/widgets/text.hpp"

int main() {
    slikui::software::Target target(320, 240);
    slikui::software::Sink sink(target);
    slikui::Context ctx(sink);
    ctx.set_frame_size({320, 240});

    auto& root = ctx.set_root<slikui::Column>();
    root.set_padding(8).set_gap(4);
    root.add<slikui::Label>("Hello, slikui!");

    ctx.tick(slikui::Time{0.5, 0.016});
    ctx.render();

    slikui::Event e{};
    e.type = slikui::Event::Type::MouseMove;
    e.pos = {10, 10};
    ctx.handle(e);
    return 0;
}
