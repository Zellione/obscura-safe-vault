#include "ui/import_tag_review.h"

#include <algorithm>
#include "ui/tag_inherit.h"
#include "ui/tag_list_parse.h"
#include "ui/zip_encoding.h"
#include "vault/index.h"

namespace ui {
namespace {
std::string trim(std::string_view s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    return std::string(s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1));
}

bool consume_delimiter(char c, size_t& angles, size_t& parens)
{
    switch (c) {
    case '<': ++angles; return true;
    case '(': ++parens; return true;
    case '>': angles -= std::min(angles, size_t{1}); return true;
    case ')': parens -= std::min(parens, size_t{1}); return true;
    default: return false;
    }
}

}

std::string strip_tag_annotations(std::string_view tag)
{
    std::string out;
    size_t angles = 0;
    size_t parens = 0;
    bool space = false;
    for (const char c : tag) {
        if (consume_delimiter(c, angles, parens)) { space = true; continue; }
        if (angles != 0 || parens != 0) continue;
        if (std::string_view(" \t\r\n").find(c) != std::string_view::npos) { space = true; continue; }
        if (space && !out.empty()) out += ' ';
        out += c;
        space = false;
    }
    return out;
}

ImportTagReview::ImportTagReview(std::vector<std::string> tags, std::vector<std::string> existing)
    : existing_(std::move(existing))
{
    for (auto& tag : tags) rows_.push_back({std::move(tag), true, std::nullopt, false});
}

std::string ImportTagReview::target(size_t row) const
{
    if (row >= rows_.size()) return {};
    const auto& r = rows_[row];
    if (r.override_tag) return *r.override_tag;
    if (!cleanup_) return r.source;
    auto cleaned = strip_tag_annotations(r.source);
    const auto colon = cleaned.find(':');
    if (colon != std::string::npos) {
        const auto name = trim(std::string_view(cleaned).substr(colon + 1));
        if (!tag_has_renderable_text(name)) return {};
        cleaned = trim(std::string_view(cleaned).substr(0, colon)) + ":" + name;
    }
    return cleaned;
}

bool ImportTagReview::rename(size_t row, std::string_view name)
{
    const auto value = trim(name);
    if (row >= rows_.size() || value.empty() || value.size() > TAG_MAX_BYTES ||
        !is_valid_utf8(value) || !tag_has_renderable_text(value)) return false;
    const auto colon = value.find(':');
    if (colon != std::string::npos && (colon == 0 || colon > vault::INDEX_MAX_CATEGORY_BYTES ||
        trim(std::string_view(value).substr(colon + 1)).empty())) return false;
    if (std::ranges::any_of(value, [](unsigned char c) { return c < 32 || c == 127; })) return false;
    rows_[row].override_tag = value;
    rows_[row].replacement = false;
    return true;
}

bool ImportTagReview::replace(size_t row, std::string_view existing)
{
    const auto it = std::ranges::find_if(existing_, [&](const auto& t) { return tag_ci_equal(t, existing); });
    if (row >= rows_.size() || it == existing_.end()) return false;
    rows_[row].override_tag = *it;
    rows_[row].replacement = true;
    return true;
}

std::vector<std::string> ImportTagReview::selected_tags() const
{
    std::vector<std::string> out;
    for (size_t i = 0; i < rows_.size(); ++i) {
        auto t = target(i);
        if (!rows_[i].selected || !tag_has_renderable_text(t)) continue;
        if (std::ranges::none_of(out, [&](const auto& old) { return tag_ci_equal(old, t); }))
            out.push_back(std::move(t));
    }
    return out;
}
} // namespace ui
