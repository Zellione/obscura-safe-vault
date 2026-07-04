#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ui {

// Parsed archive `meta.json` (Phase 27). Every field is optional in the JSON;
// absent/malformed fields stay empty and unknown keys are ignored — a bad
// meta.json must never block an import.
struct ArchiveMeta {
    std::string              title_english;
    std::string              title_japanese;
    std::vector<std::string> tags;   // "type:name" (bare name when type is absent)
};

// Tolerant parse of a meta.json blob. Malformed JSON, a non-object root, and
// wrong field types all degrade to empty fields. Pure: no SDL, no vault, no I/O.
[[nodiscard]] ArchiveMeta parse_meta_json(std::span<const uint8_t> bytes);

// Imported-gallery name: english title → japanese title → fallback (the archive
// filename / user-entered name). '/' is sanitised to '_' so a title can never
// split the vault gallery path.
[[nodiscard]] std::string meta_gallery_name(const ArchiveMeta& m, std::string_view fallback);

// Tags to seed the imported gallery: the japanese title (so it stays
// searchable) plus every "type:name" tag. Trailing normalisation and
// case-insensitive de-dupe are Vault::add_tag's job.
[[nodiscard]] std::vector<std::string> meta_gallery_tags(const ArchiveMeta& m);

} // namespace ui
