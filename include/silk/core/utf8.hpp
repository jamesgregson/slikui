// silk/core/utf8.hpp — minimal UTF-8 decoding for text widgets.
#pragma once

#include <cstdint>
#include <string_view>

namespace silk {

inline std::uint32_t utf8_next(std::string_view s, size_t& i) {
    if (i >= s.size()) return 0;
    auto lead = std::uint8_t(s[i++]);
    if (lead < 0x80) return lead;
    std::uint32_t cp = 0;
    int extra = 0;
    if ((lead & 0xE0) == 0xC0) { cp = lead & 0x1F; extra = 1; }
    else if ((lead & 0xF0) == 0xE0) { cp = lead & 0x0F; extra = 2; }
    else if ((lead & 0xF8) == 0xF0) { cp = lead & 0x07; extra = 3; }
    else { return 0xFFFD; } // invalid lead byte
    for (int k = 0; k < extra && i < s.size(); ++k) {
        auto cont = std::uint8_t(s[i]);
        if ((cont & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (cont & 0x3F);
        ++i;
    }
    return cp ? cp : 0xFFFD;
}

} // namespace silk
