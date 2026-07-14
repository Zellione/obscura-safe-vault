#include "safe_name.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace vault {

namespace {

constexpr std::string_view FALLBACK_NAME = "unnamed";

// Characters that can never appear in a node name. '/' and '\\' are rejected on
// every platform, not just the host's: a vault authored on Linux is exported on
// Windows, where a name holding '\\' would split into directories.
[[nodiscard]] bool is_forbidden_byte(unsigned char c) noexcept
{
    if (c < 0x20 || c == 0x7F) return true;   // NUL, control bytes, DEL
    switch (c) {
    case '/': case '\\':
    case '<': case '>': case ':': case '"': case '|': case '?': case '*':
        return true;
    default:
        return false;
    }
}

// The DOS device names. Windows resolves these as devices no matter which
// directory they sit in and no matter what extension they carry, so "COM1.jpg"
// is as unusable as "COM1".
[[nodiscard]] bool is_reserved_device(std::string_view name) noexcept
{
    // The device name must be the whole stem: "console.jpg" is fine, "con.jpg" is not.
    const size_t dot  = name.find('.');
    std::string_view stem = (dot == std::string_view::npos) ? name : name.substr(0, dot);
    if (stem.size() < 3 || stem.size() > 4) return false;

    std::array<char, 4> lower{};
    for (size_t i = 0; i < stem.size(); ++i)
        lower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(stem[i])));
    const std::string_view s{lower.data(), stem.size()};

    if (s == "con" || s == "prn" || s == "aux" || s == "nul") return true;
    if (s.size() == 4 && (s.starts_with("com") || s.starts_with("lpt")))
        return s[3] >= '1' && s[3] <= '9';
    return false;
}

// Back off `len` to the start of the UTF-8 codepoint it lands in the middle of,
// so truncation never tears a multi-byte sequence in half.
[[nodiscard]] size_t utf8_floor(std::string_view s, size_t len) noexcept
{
    while (len > 0 && (static_cast<unsigned char>(s[len]) & 0xC0) == 0x80) --len;
    return len;
}

// Windows silently strips trailing dots and spaces from a filename, so a name
// ending in one would pass a check here and become a *different* file on disk.
void strip_trailing_dots_and_spaces(std::string& s)
{
    while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
}

} // namespace

bool is_safe_node_name(std::string_view name) noexcept
{
    if (name.empty() || name.size() > MAX_NODE_NAME_BYTES) return false;
    if (name == "." || name == "..") return false;
    if (name.back() == '.' || name.back() == ' ') return false;
    if (std::ranges::any_of(name, [](char c) {
            return is_forbidden_byte(static_cast<unsigned char>(c));
        }))
        return false;
    return !is_reserved_device(name);
}

std::string sanitize_node_name(std::string_view name)
{
    std::string out(name);

    // 1. Neutralise every forbidden byte. This is what disarms traversal: the
    //    separators in "../../etc/passwd" become underscores, so the result can
    //    only ever name a file directly inside the destination directory.
    for (char& c : out)
        if (is_forbidden_byte(static_cast<unsigned char>(c))) c = '_';

    // 2. Trim what Windows would trim anyway. This also dissolves "." and "..",
    //    which are nothing but trailing dots.
    strip_trailing_dots_and_spaces(out);
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());

    // 3. Escape a reserved device name before truncation (the prefix costs a byte).
    if (is_reserved_device(out)) out.insert(out.begin(), '_');

    // 4. Fit the component limit, then re-trim: truncation can expose a new
    //    trailing dot or space.
    if (out.size() > MAX_NODE_NAME_BYTES) {
        out.resize(utf8_floor(out, MAX_NODE_NAME_BYTES));
        strip_trailing_dots_and_spaces(out);
    }

    if (out.empty()) out = FALLBACK_NAME;
    return out;
}

} // namespace vault
