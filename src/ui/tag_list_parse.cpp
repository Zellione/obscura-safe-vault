#include "ui/tag_list_parse.h"

#include "vault/index.h"

namespace ui {

namespace {

bool is_ws(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Trim surrounding whitespace (spaces/tabs/CR/LF) from a byte range.
std::string trim(const uint8_t* begin, const uint8_t* end)
{
    while (begin < end && is_ws(*begin)) ++begin;
    while (end > begin && is_ws(*(end - 1))) --end;
    return std::string(reinterpret_cast<const char*>(begin),
                       static_cast<std::size_t>(end - begin));
}

bool ci_equal(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; };
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower(static_cast<unsigned char>(a[i])) != lower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

}  // namespace

std::vector<std::string> parse_tag_list(std::span<const uint8_t> bytes)
{
    std::vector<std::string> out;
    const uint8_t* p   = bytes.data();
    const uint8_t* end = bytes.data() + bytes.size();

    while (p < end) {
        const uint8_t* nl = p;
        while (nl < end && *nl != '\n') ++nl;   // line is [p, nl)

        std::string tag = trim(p, nl);          // trims the CR of a CRLF too
        p = (nl < end) ? nl + 1 : end;

        if (tag.empty()) continue;
        if (tag.size() > TAG_MAX_BYTES) tag.resize(TAG_MAX_BYTES);

        bool dup = false;
        for (const auto& existing : out)
            if (ci_equal(existing, tag)) { dup = true; break; }
        if (dup) continue;

        out.push_back(std::move(tag));
        if (out.size() >= vault::INDEX_MAX_TAGS) break;
    }

    return out;
}

}  // namespace ui
