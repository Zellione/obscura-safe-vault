#include "ui/tag_dict_import.h"

#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <string>

#include "ui/tag_inherit.h"   // tag_ci_equal — the UI's one tag-identity rule
#include "vault/index.h"

namespace ui {

namespace {

using vault::TagCategory;
using vault::VaultSettings;

const TagCategory* find_category(const VaultSettings& s, std::string_view name)
{
    const auto it = std::ranges::find_if(
        s.categories, [name](const TagCategory& c) { return tag_ci_equal(c.name, name); });
    return it == s.categories.end() ? nullptr : std::to_address(it);
}

// The palette index to give a category being registered now: the lowest one no
// existing category uses. Once all TAG_SWATCH_COUNT are taken there is no free
// colour left, so it wraps round-robin on the category count — a duplicate
// swatch beyond 16 categories is the recorded tradeoff, preferred over failing
// the import or stopping to ask for a colour.
uint8_t next_swatch(const VaultSettings& s)
{
    std::array<bool, vault::TAG_SWATCH_COUNT> used{};
    for (const auto& c : s.categories) {
        if (c.swatch < vault::TAG_SWATCH_COUNT) used[c.swatch] = true;
    }
    for (uint8_t i = 0; i < vault::TAG_SWATCH_COUNT; ++i) {
        if (!used[i]) return i;
    }
    return static_cast<uint8_t>(s.categories.size() % vault::TAG_SWATCH_COUNT);
}

void register_category(VaultSettings& s, const TagDictEntry& e, TagDictImportSummary& sum)
{
    if (e.category.empty()) return;             // a bare tag registers nothing
    if (find_category(s, e.category) != nullptr) return;   // existing: keep its colour

    if (s.categories.size() >= vault::INDEX_MAX_TAG_CATEGORIES) {
        ++sum.categories_skipped_over_cap;
        return;
    }
    s.categories.push_back({.name = e.category, .swatch = next_swatch(s), .fields = {}});
    ++sum.categories_added;
}

void upsert_description(VaultSettings& s, const TagDictEntry& e, TagDictImportSummary& sum)
{
    // An empty description says nothing about the tag — it must not delete a
    // description the owner already wrote (see the header's Phase 51 note).
    if (e.description.empty()) return;

    const std::string     key      = e.key();
    const std::string_view existing = vault::find_tag_description(s, key);

    if (existing.empty()) {
        // A new key needs a free slot; an existing key never does, so the cap is
        // only ever checked here.
        if (s.tag_descriptions.size() >= vault::INDEX_MAX_TAG_DESCRIPTIONS) {
            ++sum.entries_skipped_over_cap;
            return;
        }
        vault::set_tag_description(s, key, e.description);
        ++sum.descriptions_added;
        return;
    }

    if (existing == e.description) return;   // already says exactly this
    vault::set_tag_description(s, key, e.description);
    ++sum.descriptions_updated;
}

}  // namespace

TagDictImportSummary apply_tag_dict(VaultSettings& s, const TagDictParseResult& parsed)
{
    TagDictImportSummary sum{
        .entries_skipped_malformed = parsed.malformed_skipped,
        .fields_truncated          = parsed.fields_truncated,
        // Entries the FILE overflowed and entries the VAULT has no room for are
        // the same thing to the owner: skipped because a cap was reached.
        .entries_skipped_over_cap  = parsed.over_cap_skipped,
    };

    for (const TagDictEntry& e : parsed.entries) {
        register_category(s, e, sum);
        upsert_description(s, e, sum);
    }

    return sum;
}

std::vector<std::string> tag_dict_summary_lines(const TagDictImportSummary& s)
{
    std::vector<std::string> lines{
        std::format("Categories added:     {}", s.categories_added),
        std::format("Descriptions added:   {}", s.descriptions_added),
        std::format("Descriptions updated: {}", s.descriptions_updated),
    };

    if (s.entries_skipped_malformed > 0)
        lines.push_back(std::format("Entries skipped (malformed): {}", s.entries_skipped_malformed));
    if (s.entries_skipped_over_cap > 0)
        lines.push_back(std::format("Entries skipped (vault full): {}", s.entries_skipped_over_cap));
    if (s.categories_skipped_over_cap > 0)
        lines.push_back(std::format("Categories not registered (vault full): {}",
                                    s.categories_skipped_over_cap));
    if (s.fields_truncated > 0)
        lines.push_back(std::format("Fields shortened to fit: {}", s.fields_truncated));

    return lines;
}

}  // namespace ui
