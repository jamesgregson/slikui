// slikui/text/system_fonts.hpp - locate a system TTF for demos/tests.
//
// Not part of the rendering contract: production apps ship or load their own
// fonts. This just makes examples work out of the box.
#pragma once

#include "slikui/text/font.hpp"

#include <cstdio>
#include <string>
#include <string_view>
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

// A font file plus the face to use inside it (-1: pick by style bits, for
// .ttc collections - Context::add_font resolves it).
struct SystemFont {
    std::string path;
    int face = 0;
    explicit operator bool() const { return !path.empty(); }
};

// The first installed font for a role and style: role "sans" (or "body",
// "heading") or "mono". Empty when nothing matches - callers then keep the
// family's other faces and synthesize the style.
//
//   if (auto f = find_system_font_face("mono"))
//       ctx.add_font("mono", f.path, text::FontStyle::Regular, f.face);
inline SystemFont find_system_font_face(std::string_view role = "sans",
                                        text::FontStyle style = text::FontStyle::Regular) {
    struct Cand {
        const char* path;
        int face;
    };
    static const std::vector<Cand> sans[4] = {
        // Regular: same order as font_candidates()
        {{"/System/Library/Fonts/Supplemental/Arial.ttf", 0},
         {"/Library/Fonts/Arial.ttf", 0},
         {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 0},
         {"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", 0},
         {"/usr/share/fonts/TTF/DejaVuSans.ttf", 0},
         {"C:/Windows/Fonts/arial.ttf", 0},
         {"C:/Windows/Fonts/segoeui.ttf", 0}},
        // Bold
        {{"/System/Library/Fonts/Supplemental/Arial Bold.ttf", 0},
         {"/Library/Fonts/Arial Bold.ttf", 0},
         {"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 0},
         {"/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf", 0},
         {"/usr/share/fonts/TTF/DejaVuSans-Bold.ttf", 0},
         {"C:/Windows/Fonts/arialbd.ttf", 0},
         {"C:/Windows/Fonts/segoeuib.ttf", 0}},
        // Italic
        {{"/System/Library/Fonts/Supplemental/Arial Italic.ttf", 0},
         {"/Library/Fonts/Arial Italic.ttf", 0},
         {"/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf", 0},
         {"/usr/share/fonts/truetype/liberation/LiberationSans-Italic.ttf", 0},
         {"/usr/share/fonts/TTF/DejaVuSans-Oblique.ttf", 0},
         {"C:/Windows/Fonts/ariali.ttf", 0},
         {"C:/Windows/Fonts/segoeuii.ttf", 0}},
        // BoldItalic
        {{"/System/Library/Fonts/Supplemental/Arial Bold Italic.ttf", 0},
         {"/Library/Fonts/Arial Bold Italic.ttf", 0},
         {"/usr/share/fonts/truetype/dejavu/DejaVuSans-BoldOblique.ttf", 0},
         {"/usr/share/fonts/truetype/liberation/LiberationSans-BoldItalic.ttf", 0},
         {"/usr/share/fonts/TTF/DejaVuSans-BoldOblique.ttf", 0},
         {"C:/Windows/Fonts/arialbi.ttf", 0},
         {"C:/Windows/Fonts/segoeuiz.ttf", 0}},
    };
    static const std::vector<Cand> mono[4] = {
        {{"/System/Library/Fonts/Menlo.ttc", -1},
         {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 0},
         {"/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf", 0},
         {"/usr/share/fonts/TTF/DejaVuSansMono.ttf", 0},
         {"C:/Windows/Fonts/consola.ttf", 0},
         {"C:/Windows/Fonts/cour.ttf", 0}},
        {{"/System/Library/Fonts/Menlo.ttc", -1},
         {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 0},
         {"/usr/share/fonts/truetype/liberation/LiberationMono-Bold.ttf", 0},
         {"/usr/share/fonts/TTF/DejaVuSansMono-Bold.ttf", 0},
         {"C:/Windows/Fonts/consolab.ttf", 0},
         {"C:/Windows/Fonts/courbd.ttf", 0}},
        {{"/System/Library/Fonts/Menlo.ttc", -1},
         {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Oblique.ttf", 0},
         {"/usr/share/fonts/truetype/liberation/LiberationMono-Italic.ttf", 0},
         {"/usr/share/fonts/TTF/DejaVuSansMono-Oblique.ttf", 0},
         {"C:/Windows/Fonts/consolai.ttf", 0},
         {"C:/Windows/Fonts/couri.ttf", 0}},
        {{"/System/Library/Fonts/Menlo.ttc", -1},
         {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono-BoldOblique.ttf", 0},
         {"/usr/share/fonts/truetype/liberation/LiberationMono-BoldItalic.ttf", 0},
         {"/usr/share/fonts/TTF/DejaVuSansMono-BoldOblique.ttf", 0},
         {"C:/Windows/Fonts/consolaz.ttf", 0},
         {"C:/Windows/Fonts/courbi.ttf", 0}},
    };
    const auto& table = role == "mono" ? mono : sans;
    for (const Cand& c : table[int(style) & 3]) {
        if (std::FILE* f = std::fopen(c.path, "rb")) {
            std::fclose(f);
            return {c.path, c.face};
        }
    }
    return {};
}

} // namespace slikui
