#pragma once

// Phase 55: applying a parsed tag dictionary to a vault's settings.
//
// Pure over vault::VaultSettings — it mutates a settings struct in memory and
// returns a summary. No Vault, no file handle, no SDL, so the whole import is
// unit-testable without opening a vault. The CALLER performs the single
// vault::set_vault_settings() commit afterwards; keeping that out of here is
// what keeps this function pure, and what makes one file one crash-safe index
// write rather than one write per entry.

#include <cstddef>
#include <string>
#include <vector>

#include "ui/tag_json_parse.h"   // TagDictEntry, TagDictParseResult

namespace vault { struct VaultSettings; }

namespace ui {

// What an import did. The three parse-side counters are carried through from
// TagDictParseResult so the summary modal has every number in one place.
struct TagDictImportSummary {
    size_t categories_added   = 0;   // categories new to this vault
    size_t descriptions_added = 0;   // tags that had no description before
    size_t descriptions_updated = 0; // tags whose description text CHANGED

    size_t entries_skipped_malformed = 0;   // from the parse
    size_t fields_truncated          = 0;   // from the parse; entry still imported

    // Entries whose description could not be stored: either the file held more
    // than INDEX_MAX_TAG_DESCRIPTIONS entries, or the vault's description list
    // is already full.
    size_t entries_skipped_over_cap = 0;

    // Categories that could not be registered because the vault already holds
    // INDEX_MAX_TAG_CATEGORIES of them. The entry's description still lands;
    // only its colour registration was refused, so the tag renders unswatched.
    size_t categories_skipped_over_cap = 0;
};

// Register `parsed.entries`' categories and upsert their descriptions into `s`.
//
//   * A category new to the vault is appended with the lowest palette index no
//     category is using yet, wrapping round-robin once all TAG_SWATCH_COUNT are
//     taken. An existing category NEVER gets recoloured — an import must not
//     undo colours the owner chose.
//   * An entry with an EMPTY description leaves any existing description intact
//     rather than deleting it. This deliberately diverges from Phase 51's
//     edit-time rule (where clearing the field removes the description): an
//     empty field in an imported file is ambiguous — missing data, or an
//     intentional blank? — so the safe reading is "says nothing about it".
//
// Matching is case-insensitive throughout, first-seen casing wins, mirroring
// how the vault de-duplicates tags everywhere else.
[[nodiscard]] TagDictImportSummary apply_tag_dict(vault::VaultSettings&     s,
                                                  const TagDictParseResult& parsed);

// The summary modal's body text, one line per outcome. The three "what changed"
// counts are always listed (an import that changed nothing must say so plainly);
// the skip and truncation lines appear only when they are non-zero, so a clean
// import stays short — but a cap or a skipped entry is NEVER left unmentioned.
// Pure and SDL-free, so the wording is unit-testable next to the counts.
[[nodiscard]] std::vector<std::string> tag_dict_summary_lines(const TagDictImportSummary& s);

}  // namespace ui
