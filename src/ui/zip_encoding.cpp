#include "ui/zip_encoding.h"

#include <array>

namespace ui {
namespace {

// CP437 (IBM PC OEM code page) code points for bytes 0x80-0xFF. Bytes
// 0x00-0x7F are identical to ASCII/Unicode and need no table entry.
constexpr std::array<char16_t, 128> CP437_HIGH = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, // 0x80-87
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5, // 0x88-8F
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, // 0x90-97
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192, // 0x98-9F
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, // 0xA0-A7
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB, // 0xA8-AF
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, // 0xB0-B7
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510, // 0xB8-BF
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, // 0xC0-C7
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567, // 0xC8-CF
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, // 0xD0-D7
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580, // 0xD8-DF
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, // 0xE0-E7
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229, // 0xE8-EF
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, // 0xF0-F7
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0, // 0xF8-FF
};

void append_utf8(std::string& out, char32_t cp)
{
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0U | (cp >> 6)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    } else {
        out.push_back(static_cast<char>(0xE0U | (cp >> 12)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    }
}

std::string decode_cp437(std::string_view raw)
{
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        const auto b = static_cast<unsigned char>(c);
        const char32_t cp = (b < 0x80) ? b : CP437_HIGH[static_cast<size_t>(b - 0x80)];
        append_utf8(out, cp);
    }
    return out;
}

// Conservative UTF-8 validator: also rejects overlong encodings, surrogate
// halves, and codepoints past U+10FFFF (not just structurally-invalid byte
// sequences), so a CP437 byte string that happens to parse as a "plausible"
// but wrong UTF-8 codepoint isn't mistaken for real UTF-8. Used only to
// decide the CP437-vs-pass-through heuristic; never fails, only classifies.
bool is_valid_utf8(std::string_view s)
{
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        const auto b0 = static_cast<unsigned char>(s[i]);
        size_t extra;
        char32_t cp;
        char32_t min_cp;
        if (b0 <= 0x7F) {
            ++i;
            continue;
        }
        if ((b0 & 0xE0) == 0xC0)      { extra = 1; cp = b0 & 0x1FU; min_cp = 0x80; }
        else if ((b0 & 0xF0) == 0xE0) { extra = 2; cp = b0 & 0x0FU; min_cp = 0x800; }
        else if ((b0 & 0xF8) == 0xF0) { extra = 3; cp = b0 & 0x07U; min_cp = 0x10000; }
        else return false;

        if (i + extra >= n) return false;
        for (size_t k = 1; k <= extra; ++k) {
            const auto bk = static_cast<unsigned char>(s[i + k]);
            if ((bk & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (bk & 0x3FU);
        }
        if (cp < min_cp) return false;                  // overlong encoding
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;  // surrogate half
        if (cp > 0x10FFFF) return false;
        i += extra + 1;
    }
    return true;
}

} // namespace

std::string decode_zip_entry_name(std::string_view raw, bool utf8_flag)
{
    if (utf8_flag || is_valid_utf8(raw)) return std::string(raw);
    return decode_cp437(raw);
}

} // namespace ui
