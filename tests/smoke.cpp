// tests/smoke.cpp — compile-and-link every public header in one TU.
#include "silk/context.hpp"
#include "silk/backends/software/software.hpp"
#include "silk/widgets/containers.hpp"
#include "silk/widgets/text.hpp"

int main() {
    silk::software::Target target(320, 240);
    silk::software::Sink sink(target);
    silk::Context ctx(sink);
    ctx.set_frame_size({320, 240});

    auto& root = ctx.set_root<silk::Column>();
    root.set_padding(8).set_gap(4);
    root.add<silk::Label>("Hello, silk!");

    ctx.tick(silk::Time{0.5, 0.016});
    ctx.render();

    silk::Event e{};
    e.type = silk::Event::Type::MouseMove;
    e.pos = {10, 10};
    ctx.handle(e);
    return 0;
}
