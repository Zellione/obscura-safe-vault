#pragma once

// Pure, SDL-free advanced-search query model (Phase 18).
//
// A richer relative of `search_model`: instead of one flat token list it carries
// weighted include tags, exclude tags (a hard negative filter), named AND/OR tag
// groups joined by a top-level AND/OR, a gallery-name substring, and a scope.
// `evaluate()` scores a single candidate (its display name + effective tags); the
// query also serialises to an opaque blob so the vault can persist saved searches.
//
// Layering: this unit depends on nothing from the vault or SDL, so the vault may
// include it to evaluate saved searches without creating a real dependency cycle
// (the include is one-directional — this header pulls in only the std library).

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ui {

// How a group's tags combine, and how the groups combine at the top level.
enum class Combinator : uint8_t { And = 0, Or = 1 };

// What kinds of node a search returns. Applied by the vault, not by evaluate().
enum class SearchScope : uint8_t { Images = 0, Galleries = 1, Both = 2 };

// An include tag with a relevance weight (default 1). Present-tag weights sum
// into the candidate's score, so heavier tags rank their matches higher.
struct WeightedTag {
    std::string tag;
    int32_t     weight = 1;
};

// A named bag of tags combined by `combinator`. An empty tag list is neutral
// (matches anything). The name is for display only; it never affects matching.
struct TagGroup {
    std::string              name;
    Combinator               combinator = Combinator::Or;
    std::vector<std::string> tags;
};

// A full advanced query. Every clause is optional; an all-empty query matches
// every candidate with score 0 (mirrors a blank search box).
struct AdvancedQuery {
    std::vector<WeightedTag> include;                       // weighted OR gate + scorers
    std::vector<std::string> exclude;                       // hard filter: any match rejects
    std::vector<TagGroup>    groups;                         // each AND/OR over its own tags
    Combinator               group_join = Combinator::And;  // how the groups combine
    std::string              name_query;                    // case-insensitive substring on the name
    SearchScope              scope = SearchScope::Both;
};

// The outcome of evaluating one candidate against a query.
struct EvalResult {
    bool matched = false;
    int  score   = 0;   // 0 when not matched
};

// Evaluate a candidate described by its display `name` and `effective_tags`
// (own tags ∪ inherited gallery tags) against `query`. Tag clauses (include /
// exclude / group) match a tag case-insensitively and exactly; `name_query`
// matches as a case-insensitive substring. A candidate matches iff: no exclude
// tag is present AND (include empty OR ≥1 include tag present) AND (name_query
// empty OR it is a substring) AND (groups empty OR their top-level join holds).
[[nodiscard]] EvalResult evaluate(const AdvancedQuery& query,
                                  std::string_view name,
                                  const std::vector<std::string>& effective_tags);

// Serialise the query into a self-describing versioned blob (used as the opaque
// payload of a saved search). Always non-empty.
[[nodiscard]] std::vector<uint8_t> serialize_query(const AdvancedQuery& query);

// Parse a blob produced by serialize_query. Returns false (leaving `out` in an
// unspecified-but-valid state) on any malformed/truncated/over-large input.
[[nodiscard]] bool deserialize_query(std::span<const uint8_t> in, AdvancedQuery& out);

// Case-insensitive, ranked tag autocomplete. Returns distinct vocabulary entries
// (de-duplicated case-insensitively, first casing kept) that contain `prefix`,
// with prefix matches ranked ahead of mid-string matches and ties broken
// alphabetically. An empty prefix yields no suggestions.
[[nodiscard]] std::vector<std::string> tag_suggestions(
    std::string_view prefix, const std::vector<std::string>& vocabulary);

// Move a tag-selection cursor within a field's committed-tag list. `cur` is the
// current index (-1 = nothing selected / editing the buffer); `dir` is -1 (up) or
// +1 (down); `count` is the number of committed tags. Returns the new index,
// clamped to [-1, count-1]:
//   down (+1): -1 -> 0 -> ... -> count-1 (stays at the last row)
//   up   (-1): k  -> k-1, with 0 -> -1 (deselect); -1 stays -1
// A count <= 0 always yields -1. An out-of-range `cur` is clamped first.
[[nodiscard]] int move_tag_cursor(int cur, int dir, int count);

} // namespace ui
