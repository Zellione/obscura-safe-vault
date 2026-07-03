#include "ui/search_model.h"

#include <algorithm>
#include <cctype>

namespace ui {

// Helper: convert a character to lowercase (ASCII only).
static char ascii_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// Helper: case-insensitive substring search.
static bool contains_ci(std::string_view haystack, std::string_view needle)
{
    if (needle.empty()) return true;
    if (haystack.size() < needle.size()) return false;

    for (std::size_t i = 0; i <= haystack.size() - needle.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (ascii_lower(haystack[i + j]) != ascii_lower(needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

std::vector<std::string> tokenize(std::string_view query)
{
    std::vector<std::string> tokens;

    std::size_t i = 0;
    while (i < query.size()) {
        // Skip whitespace
        while (i < query.size() && std::isspace(static_cast<unsigned char>(query[i]))) {
            ++i;
        }
        if (i >= query.size()) break;

        // Extract token until next whitespace
        std::size_t start = i;
        while (i < query.size() && !std::isspace(static_cast<unsigned char>(query[i]))) {
            ++i;
        }

        // Lowercase and store
        std::string token;
        for (std::size_t j = start; j < i; ++j) {
            token += ascii_lower(query[j]);
        }
        tokens.push_back(std::move(token));
    }

    return tokens;
}

// True if `token` is a case-insensitive substring of any tag.
static bool any_tag_contains(const std::vector<std::string>& tags, std::string_view token)
{
    return std::ranges::any_of(tags, [&](const auto& tag) { return contains_ci(tag, token); });
}

bool matches(const std::vector<std::string>& tokens, std::string_view name,
             const std::vector<std::string>& tags)
{
    // Every token must appear in the name or at least one tag (AND semantics).
    // An empty token list matches everything.
    return std::ranges::all_of(tokens, [&](const auto& token) {
        return contains_ci(name, token) || any_tag_contains(tags, token);
    });
}

int score(const std::vector<std::string>& tokens, std::string_view name,
          const std::vector<std::string>& tags)
{
    // Return 0 if not all tokens match (consistency with matches()).
    if (!matches(tokens, name, tags)) return 0;

    constexpr int name_weight = 2;
    constexpr int tag_weight  = 1;

    // Name matches outrank tag-only matches; a token can score in both fields.
    int total_score = 0;
    for (const auto& token : tokens) {
        if (contains_ci(name, token))        total_score += name_weight;
        if (any_tag_contains(tags, token))   total_score += tag_weight;
    }
    return total_score;
}

} // namespace ui
