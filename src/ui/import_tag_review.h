#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ui {

[[nodiscard]] std::string strip_tag_annotations(std::string_view tag);

struct ImportTagRow {
    std::string source;
    bool selected = true;
    std::optional<std::string> override_tag;
    bool replacement = false;
};

// A detached working copy: no vault mutations until its owner confirms it.
class ImportTagReview {
public:
    ImportTagReview() = default;
    ImportTagReview(std::vector<std::string> tags, std::vector<std::string> existing);
    void set_cleanup(bool enabled) { cleanup_ = enabled; }
    [[nodiscard]] bool cleanup() const { return cleanup_; }
    [[nodiscard]] std::vector<ImportTagRow>& rows() { return rows_; }
    [[nodiscard]] const std::vector<ImportTagRow>& rows() const { return rows_; }
    [[nodiscard]] const std::vector<std::string>& existing() const { return existing_; }
    [[nodiscard]] std::string target(size_t row) const;
    [[nodiscard]] bool rename(size_t row, std::string_view name);
    [[nodiscard]] bool replace(size_t row, std::string_view existing);
    [[nodiscard]] std::vector<std::string> selected_tags() const;
private:
    std::vector<ImportTagRow> rows_;
    std::vector<std::string> existing_;
    bool cleanup_ = false;
};

} // namespace ui
