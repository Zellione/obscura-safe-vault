#include "ui/recursive_import.h"

#include "ui/archive_kind.h"
#include "vault/safe_name.h"

#include <algorithm>
#include <format>

namespace ui {

std::string nested_gallery_name(std::string_view archive_filename)
{
    // Not an archive name at all — including a bare ".zip", which is a dotfile
    // with no stem, not an archive. The planner would never route these, so
    // refusing here keeps the contract narrow instead of inventing a gallery
    // named after a dotfile.
    const std::string_view ext = archive_extension_of(archive_filename);
    if (ext.empty()) {
        return {};
    }

    const std::string_view stem = archive_filename.substr(0, archive_filename.size() - ext.size());
    // sanitize_node_name substitutes a fallback for an empty result, so an
    // empty stem has to be rejected here — a gallery named after the fallback
    // would be worse than letting the caller choose one.
    if (stem.empty()) {
        return {};
    }
    return vault::sanitize_node_name(stem);
}

std::string unique_gallery_name(std::string_view base, const std::vector<std::string>& taken)
{
    const auto is_taken = [&taken](const std::string& candidate) {
        return std::ranges::find(taken, candidate) != taken.end();
    };

    std::string candidate(base);
    // Starts at 2 so the first collision reads "bonus_2" — "bonus_1" would
    // imply a "bonus_0" somewhere.
    for (int n = 2; is_taken(candidate); ++n) {
        candidate = std::format("{}_{}", base, n);
    }
    return candidate;
}

} // namespace ui
