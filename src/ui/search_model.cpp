#include "ui/search_model.h"

#include <cctype>

namespace ui {

// Helper: convert a character to lowercase (ASCII only).
static char ascii_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c;
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

bool matches(const std::vector<std::string>& tokens, std::string_view name,
             const std::vector<std::string>& tags)
{
    // Empty token list matches everything.
    if (tokens.empty()) return true;

    // Every token must appear in name or at least one tag.
    for (const auto& token : tokens) {
        bool found = false;

        // Check name
        if (contains_ci(name, token)) {
            found = true;
        }

        // Check tags
        if (!found) {
            for (const auto& tag : tags) {
                if (contains_ci(tag, token)) {
                    found = true;
                    break;
                }
            }
        }

        if (!found) return false;  // Token not found anywhere
    }

    return true;
}

int score(const std::vector<std::string>& tokens, std::string_view name,
          const std::vector<std::string>& tags)
{
    // Return 0 if not all tokens match (consistency with matches()).
    if (!matches(tokens, name, tags)) {
        return 0;
    }

    int total_score = 0;
    const int name_weight = 2;
    const int tag_weight = 1;

    // For each token, accumulate score based on where it matches.
    for (const auto& token : tokens) {
        bool found_in_name = contains_ci(name, token);
        bool found_in_tags = false;

        for (const auto& tag : tags) {
            if (contains_ci(tag, token)) {
                found_in_tags = true;
                break;
            }
        }

        // Contribute to score: name match is worth more, and both can add.
        if (found_in_name) {
            total_score += name_weight;
        }
        if (found_in_tags) {
            total_score += tag_weight;
        }
    }

    return total_score;
}

} // namespace ui
