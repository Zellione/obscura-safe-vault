#include "ui/export.h"

#include <algorithm>
#include <cstdio>
#include <ranges>
#include <unordered_set>
#include <vector>
#include "platform/safe_print.h"

#include "platform/path_utf8.h"
#include "vault/safe_name.h"

namespace ui {

namespace fs = std::filesystem;

bool export_path_within(const fs::path& dest_dir, const fs::path& candidate)
{
    std::error_code ec;
    const fs::path base = fs::weakly_canonical(dest_dir, ec);
    if (ec) return false;
    const fs::path target = fs::weakly_canonical(candidate, ec);
    if (ec) return false;

    const fs::path rel = target.lexically_relative(base);
    if (rel.empty() || rel == ".") return false;
    return !std::ranges::contains(rel, "..");
}

// Images and videos carry originals; gallery directories are handled by the
// selection traversal below.
//
// Phase 98 (OSV-AUD-005): `out` is an ALREADY-OPEN handle produced by
// platform::create_new_file_within (atomic exclusive create, symlink-safe,
// contained). We write to the handle only — never reopen out.display_path.
vault::VaultResult export_one_media(const vault::Vault&         vault,
                                    const vault::IndexNode&     node,
                                    platform::NewOutputFile     out,
                                    crypto::SecureBytes&        scratch)
{
    const bool image = node.is_image();
    if (const bool video = node.is_video(); !image && !video) {
        scratch.wipe();
        return vault::VaultResult::InvalidArg;
    }

    // Decrypt the original stored bytes into mlock'd memory (invariant #1 holds
    // right up to the write below). A video's bytes live across several chunks;
    // read_video concatenates them into the same mlock'd buffer.
    if (const auto rc = image ? vault.read_image(node, scratch) : vault.read_video(node, scratch);
        rc != vault::VaultResult::Ok) {
        scratch.wipe();
        return rc;
    }

    // Deliberate, gated deviation from invariant #1: write the plaintext to disk.
    bool ok = false;
    if (out.fp) {
        const size_t n = scratch.size();
        ok = (n == 0) || (std::fwrite(scratch.data(), 1, n, out.fp) == n);
        ok = (std::fflush(out.fp) == 0) && ok;
        ok = (std::fclose(out.fp) == 0) && ok;
        out.fp = nullptr;   // detach: the call above already closed the stream
    }

    // Wipe the decrypted bytes immediately, whether or not the write succeeded.
    scratch.wipe();

    if (!ok) {
        platform::safe_println(stderr, "[Export] failed to write {}",
                               platform::path_to_utf8(out.display_path));
        return vault::VaultResult::IoError;
    }
    return vault::VaultResult::Ok;
}

namespace {

struct ExportEntry {
    const vault::IndexNode* node = nullptr;
    size_t depth = 0;
};

// Collection results can select both a gallery and any of its descendants.
// Determine covered nodes first so selection order cannot duplicate a subtree.
std::unordered_set<const vault::IndexNode*>
covered_nodes(std::span<const vault::IndexNode* const> selected, const vault::OpProgress* progress)
{
    std::unordered_set<const vault::IndexNode*> covered;
    std::unordered_set<const vault::IndexNode*> seen;
    std::vector<const vault::IndexNode*> pending(selected.begin(), selected.end());
    while (!pending.empty()) {
        if (progress && progress->cancel.load()) return {};
        const auto* node = pending.back();
        pending.pop_back();
        if (!node || !seen.insert(node).second || !node->is_gallery()) continue;
        for (const auto& child : node->children) {
            covered.insert(&child);
            pending.push_back(&child);
        }
    }

    return covered;
}

std::vector<ExportEntry> export_plan(std::span<const vault::IndexNode* const> selected,
                                     const vault::OpProgress* progress)
{
    const auto covered = covered_nodes(selected, progress);
    std::unordered_set<const vault::IndexNode*> seen;
    std::vector<ExportEntry> stack;
    for (const auto* node : selected) {
        if (!covered.contains(node) && seen.insert(node).second) stack.push_back({node, 0});
    }
    std::ranges::reverse(stack);
    std::vector<ExportEntry> plan;
    while (!stack.empty()) {
        if (progress && progress->cancel.load()) return {};
        const auto entry = stack.back();
        stack.pop_back();
        plan.push_back(entry);
        if (!entry.node || !entry.node->is_gallery()) continue;
        for (const auto& child : std::views::reverse(entry.node->children))
            stack.push_back({&child, entry.depth + 1});
    }
    return plan;
}

bool export_entry(const vault::Vault& vault, const vault::IndexNode* node,
                  std::vector<platform::OutputDirectory>& directories, crypto::SecureBytes& scratch)
{
    if (!node) return false;
    const auto name = vault::sanitize_node_name(node->name.view());
    if (node->is_gallery()) {
        auto child = directories.back().create_directory(name);
        if (!child) return false;
        directories.push_back(std::move(*child));
        return true;
    }
    if (!node->is_image() && !node->is_video()) return false;
    auto out = directories.back().create_file(name);
    return out &&
           export_one_media(vault, *node, std::move(*out), scratch) == vault::VaultResult::Ok;
}

// All descendants of a failed directory fail too; never flatten them into its
// parent. This also makes the completed progress denominator honest on errors.
size_t subtree_end(const std::vector<ExportEntry>& plan, size_t begin)
{
    size_t end = begin + 1;
    while (end < plan.size() && plan[end].depth > plan[begin].depth)
        ++end;
    return end;
}

ExportSummary run_export_plan(const vault::Vault& vault, const std::vector<ExportEntry>& plan,
                              platform::OutputDirectory root, vault::OpProgress* progress)
{
    ExportSummary sum;
    // One descriptor per active ancestor, bounded by depth rather than total
    // gallery count. No path below the chosen root is ever reopened.
    std::vector<platform::OutputDirectory> directories;
    directories.push_back(std::move(root));
    crypto::SecureBytes scratch;
    for (size_t i = 0; i < plan.size();) {
        if (progress && progress->cancel.load()) break;
        const auto [node, depth] = plan[i];
        directories.erase(directories.begin() + static_cast<std::ptrdiff_t>(depth + 1),
                          directories.end());
        const bool ok = export_entry(vault, node, directories, scratch);
        const size_t next = ok ? i + 1 : subtree_end(plan, i);
        if (ok)
            ++sum.written;
        else
            sum.failed += static_cast<int>(next - i);
        if (progress) progress->done.fetch_add(static_cast<int>(next - i));
        i = next;
    }
    return sum;
}

}  // namespace

ExportSummary export_images(const vault::Vault&                      vault,
                            std::span<const vault::IndexNode* const> images,
                            const fs::path&                          dest_dir,
                            ExportConsent                            consent,
                            vault::OpProgress*                       progress)
{
    ExportSummary sum;
    if (consent != ExportConsent::Confirm) return sum;

    const auto plan = export_plan(images, progress);
    if (progress) {
        progress->total.store(static_cast<int>(plan.size()));
        progress->done.store(0);
    }
    if (plan.empty() || (progress && progress->cancel.load())) return sum;
    auto root = platform::OutputDirectory::open(dest_dir);
    if (!root) {
        sum.failed = static_cast<int>(plan.size());
        if (progress) progress->done.store(sum.failed);
        return sum;
    }

    return run_export_plan(vault, plan, std::move(*root), progress);
}

} // namespace ui
