// slikui/text/system_fonts.hpp — locate a system TTF for demos/tests.
//
// Not part of the rendering contract: production apps ship or load their own
// fonts. This just makes examples work out of the box.
#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace slikui {

inline const std::vector<const char*>& font_candidates() {
    static const std::vector<const char*> paths = {
        // macOS
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/Menlo.ttc",
        "/System/Library/Fonts/Helvetica.ttc",
        "/Library/Fonts/Arial.ttf",
        // Linux
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        // Windows
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
    };
    return paths;
}

// Returns the first candidate that opens, or "".
inline std::string find_system_font() {
    for (const char* p : font_candidates()) {
        if (std::FILE* f = std::fopen(p, "rb")) {
            std::fclose(f);
            return p;
        }
    }
    return "";
}

} // namespace slikui
