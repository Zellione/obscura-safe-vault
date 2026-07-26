#pragma once

// Phase 55: the JSON tag-dictionary parser.
//
// Turns a picked .json file's raw bytes into tag-dictionary entries — a tag
// *vocabulary* (categories + per-tag descriptions), not tags on any content.
// Pure: no vault, no SDL, no disk, and exception-free (nlohmann is parsed with
// allow_exceptions=false, exactly as ui/meta_json.cpp does it).
//
// The file is untrusted input. Everything it can influence is bounded here:
// field sizes are capped, the entry count is capped, and a malformed entry is
// skipped rather than aborting the file.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ui {

// One dictionary entry. `category` and `description` may be empty; `name` never
// is (an entry without a usable name never reaches a TagDictEntry).
struct TagDictEntry {
    std::string category;
    std::string name;
    std::string description;

    // The tag key this entry describes: "category:name", or a bare "name" when
    // there is no category — the same shape ui::resolve_tag splits back apart.
    [[nodiscard]] std::string key() const;

    friend bool operator==(const TagDictEntry&, const TagDictEntry&) = default;
};

struct TagDictParseResult {
    std::vector<TagDictEntry> entries;

    // Entries DROPPED: not an object, no usable name, a colon inside the name,
    // or a case-insensitive duplicate of an earlier entry in the same file.
    size_t malformed_skipped = 0;

    // Fields SHORTENED (the entry was still kept): one per over-long category
    // or description. Counted separately from malformed_skipped so the import
    // summary never reports an imported entry as skipped.
    size_t fields_truncated = 0;

    // Entries dropped because the file holds more than INDEX_MAX_TAG_DESCRIPTIONS
    // of them — a bound on a hostile file, not a judgement about the entry.
    size_t over_cap_skipped = 0;
};

// Parse `bytes` as either a bare JSON array of entry objects, or an object with
// a "tags" array of them. A document that is neither (or is not JSON at all)
// yields an empty result rather than an error.
//
// Per-entry rules, each separately tested in tests/ui/test_tag_json_parse.cpp:
//   * every field is trimmed of surrounding whitespace;
//   * `name` is required and must not contain ':' (ui::resolve_tag splits on the
//     first colon, so a colon in the name would display split at the wrong
//     place — rejected loudly rather than silently normalised);
//   * `category` is truncated to INDEX_MAX_CATEGORY_BYTES and `description` to
//     INDEX_MAX_TAG_DESC_BYTES, always on a UTF-8 character boundary;
//   * duplicates of an earlier key (case-insensitive) are dropped, first casing
//     and first description win.
//
// Note the two vault-wide COUNT caps split across the two halves of the import:
// the entry cap is enforced here (it bounds this file), while the category cap
// (INDEX_MAX_TAG_CATEGORIES) is enforced by apply_tag_dict, which alone knows
// how many categories the vault already holds.
[[nodiscard]] TagDictParseResult parse_tag_dict_json(std::span<const uint8_t> bytes);

}  // namespace ui
