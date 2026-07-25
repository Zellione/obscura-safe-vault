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

RecursionBudget::RecursionBudget(RecursionLimits limits, uint64_t root_archive_bytes)
    : limits_(limits), root_bytes_(root_archive_bytes)
{
}

RecursionVerdict RecursionBudget::may_descend(int depth, uint64_t nested_bytes) const
{
    if (depth >= limits_.max_depth) {
        return RecursionVerdict::DepthExceeded;
    }
    if (nested_seen_ >= limits_.max_nested_archives) {
        return RecursionVerdict::CountExceeded;
    }
    if (total_expanded_ > limits_.max_total_expanded) {
        return RecursionVerdict::TotalBytesExceeded;
    }
    if (live_bytes_ + nested_bytes > limits_.max_live_bytes) {
        return RecursionVerdict::LiveBytesExceeded;
    }
    // Ratio is meaningless for a zero-byte root, and dividing by it would be a
    // crash on a degenerate input. Compared by multiplication rather than
    // division so a huge expansion cannot overflow the quotient.
    if (root_bytes_ > 0 && total_expanded_ > limits_.max_expansion_ratio * root_bytes_) {
        return RecursionVerdict::RatioExceeded;
    }
    return RecursionVerdict::Allow;
}

void RecursionBudget::enter(uint64_t nested_bytes)
{
    live_bytes_ += nested_bytes;
    ++nested_seen_;
}

void RecursionBudget::leave(uint64_t nested_bytes)
{
    // Depth-first means this is symmetric with enter(); guard anyway so a
    // mismatched pair cannot wrap the unsigned counter around to enormous.
    live_bytes_ -= std::min(live_bytes_, nested_bytes);
}

void RecursionBudget::note_expanded(uint64_t bytes)
{
    total_expanded_ += bytes;
}
} // namespace ui
