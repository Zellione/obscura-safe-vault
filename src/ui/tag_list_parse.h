#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ui {

// Largest tag length we persist. The index serialises each tag with a u16
// length prefix (clamped to 0xFFFF in index.cpp), so anything longer is
// truncated to this byte bound before it reaches the vault (Phase 21).
inline constexpr std::size_t TAG_MAX_BYTES = 0xFFFF;

// Parse the raw bytes of a plain-text tag list (one tag per line) into a
// normalised, deduplicated tag list:
//   - splits on LF; a trailing CR is trimmed as whitespace (handles CRLF);
//   - trims surrounding spaces/tabs/CR from each line;
//   - drops blank lines;
//   - de-duplicates case-insensitively, keeping the first occurrence's casing;
//   - truncates each tag to TAG_MAX_BYTES;
//   - caps the result at INDEX_MAX_TAGS entries.
// Bytes are treated opaquely (no UTF-8 validation), so non-UTF-8 input never
// crashes. Pure: no vault, no SDL, no disk (Phase 21).
[[nodiscard]] std::vector<std::string> parse_tag_list(std::span<const uint8_t> bytes);

}  // namespace ui
