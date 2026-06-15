#pragma once

// Pure, SDL-free search query tokenisation, matching, and ranking (Phase 12).
//
// Provides case-insensitive substring matching across a name and a set of tags,
// with AND semantics across query tokens and simple relevance scoring that
// weights name matches higher than tag matches.

#include <string>
#include <string_view>
#include <vector>

namespace ui {

// Lowercase the query and split on ASCII whitespace into tokens; empty tokens
// (from repeated whitespace) are discarded. Returns an empty vector if the
// query is empty or contains only whitespace.
[[nodiscard]] std::vector<std::string> tokenize(std::string_view query);

// Checks if a name and tags match a set of query tokens. Returns true if every
// token appears as a case-insensitive substring in either the name or at least
// one of the tags. An empty token list matches everything (blank query shows
// all results).
[[nodiscard]] bool matches(const std::vector<std::string>& tokens,
                           std::string_view name,
                           const std::vector<std::string>& tags);

// A simple relevance score for ordering results (higher = better). Name matches
// are weighted more heavily than tag matches; a token contributes to the score
// once per source it appears in. Returns 0 when `matches()` would be false
// (i.e., when not all tokens match), ensuring consistency.
[[nodiscard]] int score(const std::vector<std::string>& tokens,
                        std::string_view name,
                        const std::vector<std::string>& tags);

} // namespace ui
