// slikui/slikui.hpp — umbrella header: everything the library offers.
//
//   #include <slikui/slikui.hpp>
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
#include "version.hpp"

namespace slikui {
inline constexpr int version_major = SLIKUI_VERSION_MAJOR;
inline constexpr int version_minor = SLIKUI_VERSION_MINOR;
inline constexpr int version_patch = SLIKUI_VERSION_PATCH;
inline constexpr const char* version = SLIKUI_VERSION_STRING;
} // namespace slikui
