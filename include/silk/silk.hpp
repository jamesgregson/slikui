// silk/silk.hpp — umbrella header: everything the library offers.
//
//   #include <silk/silk.hpp>
//
// Header-only: no linking, no build step. C++20 required.
#pragma once

#include "core/color.hpp"
#include "core/input.hpp"
#include "core/math.hpp"
#include "core/time.hpp"
#include "core/utf8.hpp"
#include "layout/layout.hpp"
#include "render/draw_list.hpp"
#include "render/sink.hpp"
#include "style/anim.hpp"
#include "style/theme.hpp"
#include "text/font.hpp"
#include "widgets/all.hpp"
#include "context.hpp"

namespace silk {
inline constexpr const char* version = "0.1.0";
} // namespace silk
