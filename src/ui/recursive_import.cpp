#include "ui/recursive_import.h"

#include "ui/archive_kind.h"
#include "ui/meta_json.h"
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
    int n = 2;
    while (is_taken(candidate)) {
        candidate = std::format("{}_{}", base, n);
        ++n;
    }
    return candidate;
}

RecursionBudget::RecursionBudget(const RecursionLimits& limits, uint64_t root_archive_bytes)
    : limits_(limits), root_bytes_(root_archive_bytes)
{
}

RecursionVerdict RecursionBudget::may_descend(int depth, uint64_t nested_bytes) const
{
    using enum RecursionVerdict;

    if (depth >= limits_.max_depth) {
        return DepthExceeded;
    }
    if (nested_seen_ >= limits_.max_nested_archives) {
        return CountExceeded;
    }
    if (total_expanded_ > limits_.max_total_expanded) {
        return TotalBytesExceeded;
    }
    if (live_bytes_ + nested_bytes > limits_.max_live_bytes) {
        return LiveBytesExceeded;
    }
    // Ratio is meaningless for a zero-byte root, and dividing by it would be a
    // crash on a degenerate input. Compared by multiplication rather than
    // division so a huge expansion cannot overflow the quotient.
    if (root_bytes_ > 0 && total_expanded_ > limits_.max_expansion_ratio * root_bytes_) {
        return RatioExceeded;
    }
    return Allow;
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
namespace {

// One frame of the depth-first walk. Depth-first is what bounds memory: only
// the archives along the current root->leaf path are alive at once, which is
// what RecursionBudget's live-bytes limit polices.
struct Frame {
    crypto::SecureBytes      owned;    // this archive's bytes (empty for the root)
    std::span<const uint8_t> bytes;    // view of owned, or of the caller's root buffer
    ArchiveKind              kind;
    std::string              gallery;  // vault gallery receiving this archive's contents
    int                      depth;
};

void walk_one(const Frame& f, const RecursiveHooks& hooks, RecursionBudget& budget,
              RecursiveTally& tally);

// Each archive's own meta.json tags the gallery it produced — including a
// nested one, so a sub-gallery carries the metadata of the archive it came from
// rather than inheriting its parent's. Best-effort: bad metadata never fails an
// import (Phase 27's rule).
void apply_own_meta(const Frame& f, const RecursiveHooks& hooks,
                    const std::vector<ZipEntry>& entries)
{
    const std::optional<size_t> meta_idx = find_meta_entry(entries);
    if (!meta_idx.has_value()) {
        return;
    }
    crypto::SecureBytes meta_bytes;
    if (!hooks.extract_entry(f.bytes, f.kind, *meta_idx, meta_bytes)) {
        return;
    }
    for (const std::string& tag : meta_gallery_tags(parse_meta_json(meta_bytes.as_span()))) {
        (void)hooks.tag_gallery(f.gallery, tag);
    }
}

// Returns false if the walk was cancelled partway.
[[nodiscard]] bool place_media(const Frame& f, const ZipPlan& plan, const RecursiveHooks& hooks,
                               RecursionBudget& budget, RecursiveTally& tally)
{
    for (const ZipPlacement& p : plan.placements) {
        if (hooks.cancelled()) {
            return false;
        }
        crypto::SecureBytes payload;
        if (!hooks.extract_entry(f.bytes, f.kind, p.entry_index, payload)) {
            ++tally.skipped_unsupported;
            continue;
        }
        budget.note_expanded(payload.size());
        if (hooks.place_media(p.gallery_path, p.filename, payload.as_span())) {
            ++tally.media_placed;
        } else {
            ++tally.skipped_unsupported;
        }
    }
    return true;
}

// Extract a nested candidate and confirm it really is an archive the budget
// allows entering. Returns None when it must be skipped; the tally records why.
[[nodiscard]] ArchiveKind vet_nested(const Frame& f, const ZipPlacement& p,
                                     const RecursiveHooks& hooks, RecursionBudget& budget,
                                     RecursiveTally& tally, crypto::SecureBytes& child)
{
    if (!hooks.extract_entry(f.bytes, f.kind, p.entry_index, child)) {
        ++tally.unreadable;
        return ArchiveKind::None;
    }
    budget.note_expanded(child.size());

    // The name claimed an archive; the magic bytes get the final say.
    const ArchiveKind kind = detect_archive_kind(p.filename, child.as_span());
    if (kind == ArchiveKind::None) {
        ++tally.not_an_archive;
        return ArchiveKind::None;
    }

    switch (budget.may_descend(f.depth, child.size())) {
        case RecursionVerdict::Allow: return kind;
        case RecursionVerdict::DepthExceeded:
            ++tally.depth_capped;
            return ArchiveKind::None;
        default:
            ++tally.budget_stopped;
            return ArchiveKind::None;
    }
}

// Descend into each nested archive in turn, depth-first.
void descend(const Frame& f, const ZipPlan& plan, const RecursiveHooks& hooks,
             RecursionBudget& budget, RecursiveTally& tally)
{
    // Sub-gallery names are unique per parent gallery, so two sibling folders
    // each holding "bonus.zip" do not collapse onto one gallery.
    std::vector<std::string> taken;

    for (const ZipPlacement& p : plan.nested) {
        if (hooks.cancelled()) {
            return;
        }

        crypto::SecureBytes child;
        const ArchiveKind   kind = vet_nested(f, p, hooks, budget, tally, child);
        if (kind == ArchiveKind::None) {
            continue;
        }

        std::string base = nested_gallery_name(p.filename);
        if (base.empty()) {
            base = "archive";
        }
        const std::string name = unique_gallery_name(base, taken);
        taken.push_back(name);

        const std::string sub = p.gallery_path.empty() ? name : p.gallery_path + "/" + name;
        if (!hooks.create_gallery(sub)) {
            continue;
        }

        Frame child_frame{.owned   = std::move(child),
                          .bytes   = {},
                          .kind    = kind,
                          .gallery = sub,
                          .depth   = f.depth + 1};
        child_frame.bytes = child_frame.owned.as_span();

        budget.enter(child_frame.owned.size());
        ++tally.nested_archives;
        walk_one(child_frame, hooks, budget, tally);
        budget.leave(child_frame.owned.size());
    }
}

// Import every media entry, then descend into each nested archive in turn.
void walk_one(const Frame& f, const RecursiveHooks& hooks, RecursionBudget& budget,
              RecursiveTally& tally)
{
    std::vector<ZipEntry> entries;
    if (!hooks.list_entries(f.bytes, f.kind, entries)) {
        ++tally.unreadable;
        return;
    }

    // A CBZ is a flat run of pages, so an archive inside one is not a chapter:
    // plan it as pages and never descend.
    const ZipPlan plan = (f.kind == ArchiveKind::Cbz)
                             ? build_cbz_plan(entries, f.gallery, "")
                             : build_zip_plan(entries, f.gallery, "");

    for (const std::string& g : plan.galleries) {
        if (!hooks.create_gallery(g)) {
            return;
        }
    }

    apply_own_meta(f, hooks, entries);

    tally.skipped_unsupported += plan.skipped_unsupported;

    // Grow the caller's running total as the tree is explored. Optional, so an
    // unset hook is not the fatal-missing-hook case handled in walk_archive.
    if (hooks.note_planned) {
        hooks.note_planned(static_cast<int>(plan.placements.size()));
    }

    if (!place_media(f, plan, hooks, budget, tally)) {
        return;   // cancelled
    }
    descend(f, plan, hooks, budget, tally);
}

} // namespace

RecursiveTally walk_archive(std::span<const uint8_t> root_bytes,
                            ArchiveKind              root_kind,
                            std::string_view         dest_gallery,
                            const RecursiveHooks&    hooks,
                            const RecursionLimits&   limits)
{
    RecursiveTally tally;

    // A default-constructed std::function THROWS when called, and this project
    // is exception-free — an unset hook would terminate the process rather than
    // fail gracefully. Refuse the walk instead.
    if (!hooks.list_entries || !hooks.extract_entry || !hooks.create_gallery ||
        !hooks.place_media || !hooks.tag_gallery || !hooks.cancelled) {
        return tally;
    }

    RecursionBudget budget(limits, root_bytes.size());
    Frame           root{.owned   = {},
                         .bytes   = root_bytes,
                         .kind    = root_kind,
                         .gallery = std::string(dest_gallery),
                         .depth   = 0};
    walk_one(root, hooks, budget, tally);
    return tally;
}

} // namespace ui
