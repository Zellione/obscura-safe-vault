#include "ui/tag_json_parse.h"

#include <nlohmann/json.hpp>

#include <memory>

#include "ui/tag_inherit.h"        // tag_ci_equal — the UI's one tag-identity rule
#include "ui/text_input_model.h"   // utf8_prev_boundary

#include "vault/index.h"

namespace ui {

namespace {

using nlohmann::json;

bool is_ws(unsigned char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Trim surrounding whitespace, matching parse_tag_list's convention.
std::string trimmed(std::string_view s)
{
    size_t b = 0;
    size_t e = s.size();
    while (b < e && is_ws(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && is_ws(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

// obj[key] as a trimmed string. A missing key or a non-string value reads as
// absent — a number where a description belongs loses that description, but
// does not cost the caller an otherwise usable entry.
std::string str_field(const json& obj, const char* key)
{
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) return {};
    return trimmed(it->get_ref<const std::string&>());
}

// uint8_t, not unsigned char: this file's byte type is the span element type the
// UTF-8 helpers take, matching text_input_model.cpp's identical predicate.
bool is_continuation(uint8_t b) noexcept
{
    return (b & 0xC0) == 0x80;
}

// Shorten `s` to at most `cap` bytes, cutting on a UTF-8 character boundary so a
// multi-byte sequence is never left half-written into the vault. Returns whether
// anything was actually cut.
bool truncate_utf8(std::string& s, size_t cap)
{
    if (s.size() <= cap) return false;
    const std::span<const uint8_t> buf{reinterpret_cast<const uint8_t*>(s.data()), s.size()};
    // A cut landing ON a continuation byte splits a character; step back to the
    // start of the character that byte belongs to. A cut landing on a lead byte
    // is already a boundary.
    const size_t cut = is_continuation(buf[cap])
                           ? utf8_prev_boundary(buf, cap)
                           : cap;
    s.resize(cut);
    return true;
}

// One array element → an entry, or nothing plus a reason. Returns false when the
// element is malformed (the caller counts it and moves on to the next).
bool read_entry(const json& e, TagDictEntry& out, size_t& truncated)
{
    if (!e.is_object()) return false;

    out.name = str_field(e, "name");
    // A colon in the name would be read back as a category separator by
    // resolve_tag, splitting the tag at the wrong place on every chip that
    // renders it. Rejected rather than silently rewritten.
    if (out.name.empty() || out.name.contains(':')) return false;

    out.category    = str_field(e, "category");
    out.description = str_field(e, "description");
    if (truncate_utf8(out.category, vault::INDEX_MAX_CATEGORY_BYTES)) ++truncated;
    if (truncate_utf8(out.description, vault::INDEX_MAX_TAG_DESC_BYTES)) ++truncated;
    return true;
}

// The array of entries, whichever of the two accepted document shapes carries
// it. Null when the document is neither.
const json* entry_array(const json& doc)
{
    if (doc.is_array()) return &doc;
    if (!doc.is_object()) return nullptr;
    const auto it = doc.find("tags");
    return (it != doc.end() && it->is_array()) ? std::to_address(it) : nullptr;
}

}  // namespace

std::string TagDictEntry::key() const
{
    return category.empty() ? name : category + ":" + name;
}

TagDictParseResult parse_tag_dict_json(std::span<const uint8_t> bytes)
{
    TagDictParseResult out;

    // allow_exceptions=false: malformed input (including invalid UTF-8 inside a
    // string) yields a discarded value instead of a throw — the project is
    // exception-free.
    const json doc = json::parse(bytes.begin(), bytes.end(), nullptr, false);
    if (doc.is_discarded()) return out;

    const json* arr = entry_array(doc);
    if (arr == nullptr) return out;

    for (const json& e : *arr) {
        if (out.entries.size() >= vault::INDEX_MAX_TAG_DESCRIPTIONS) {
            ++out.over_cap_skipped;
            continue;
        }

        TagDictEntry entry;
        if (!read_entry(e, entry, out.fields_truncated)) {
            ++out.malformed_skipped;
            continue;
        }

        // Case-insensitive de-dupe within the file: first casing and first
        // description win, matching parse_tag_list and Vault::add_tag.
        const std::string k = entry.key();
        bool dup = false;
        for (const auto& kept : out.entries) {
            if (tag_ci_equal(kept.key(), k)) { dup = true; break; }
        }
        if (dup) {
            ++out.malformed_skipped;
            continue;
        }

        out.entries.push_back(std::move(entry));
    }

    return out;
}

}  // namespace ui
