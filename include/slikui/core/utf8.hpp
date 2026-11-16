// slikui/core/utf8.hpp - minimal UTF-8 decoding/encoding for text widgets.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace slikui {

// Decodes the codepoint at s[i] and advances i past it. Malformed input -
// an invalid lead byte, a missing or bad continuation byte, an overlong
// form, a surrogate, or a value above U+10FFFF - yields U+FFFD and advances
// past the bytes consumed so far (never past a byte that could start the
// next sequence). Returns 0 at the end of the string.
inline std::uint32_t utf8_next(std::string_view s, size_t& i) {
    if (i >= s.size()) return 0;
    auto lead = std::uint8_t(s[i++]);
    if (lead < 0x80) return lead;
    std::uint32_t cp = 0, min = 0;
    int extra = 0;
    if ((lead & 0xE0) == 0xC0) { cp = lead & 0x1F; extra = 1; min = 0x80; }
    else if ((lead & 0xF0) == 0xE0) { cp = lead & 0x0F; extra = 2; min = 0x800; }
    else if ((lead & 0xF8) == 0xF0) { cp = lead & 0x07; extra = 3; min = 0x10000; }
    else { return 0xFFFD; } // invalid lead byte (stray continuation, 0xF8+)
    for (int k = 0; k < extra; ++k) {
        if (i >= s.size()) return 0xFFFD; // truncated at the end of the string
        auto cont = std::uint8_t(s[i]);
        if ((cont & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (cont & 0x3F);
        ++i;
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0xFFFD;
    return cp;
}

// Appends the UTF-8 encoding of `cp` (U+FFFD for surrogates / out of range).
inline void utf8_append(std::string& out, std::uint32_t cp) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
    if (cp < 0x80) {
        out += char(cp);
    } else if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xF0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

inline std::string utf8_encode(std::uint32_t cp) {
    std::string out;
    utf8_append(out, cp);
    return out;
}

} // namespace slikui
