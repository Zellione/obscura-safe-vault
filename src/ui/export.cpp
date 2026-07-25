#include "ui/export.h"

#include <algorithm>
#include <cstdio>
#include <print>

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

// Phase 53: videos became selectable, so a selection can legitimately contain
// one. A gallery still has no stored bytes of its own and is rejected — calling
// that an "export" would be a lie.
vault::VaultResult export_one_media(const vault::Vault&          vault,
                                    const vault::IndexNode&      node,
                                    const fs::path&              out_path,
                                    crypto::SecureBytes&         scratch)
{
    const bool image = node.is_image();
    const bool video = node.is_video();
    if (!image && !video) return vault::VaultResult::InvalidArg;

    // Decrypt the original stored bytes into mlock'd memory (invariant #1 holds
    // right up to the write below). A video's bytes live across several chunks;
    // read_video concatenates them into the same mlock'd buffer.
    const auto rc = image ? vault.read_image(node, scratch) : vault.read_video(node, scratch);
    if (rc != vault::VaultResult::Ok) {
        scratch.wipe();
        return rc;
    }

    // Deliberate, gated deviation from invariant #1: write the plaintext to disk.
    bool ok = false;
    if (std::FILE* fp = std::fopen(out_path.string().c_str(), "wb")) {
        const size_t n = scratch.size();
        ok = (n == 0) || (std::fwrite(scratch.data(), 1, n, fp) == n);
        ok = (std::fflush(fp) == 0) && ok;
        ok = (std::fclose(fp) == 0) && ok;
    }

    // Wipe the decrypted bytes immediately, whether or not the write succeeded.
    scratch.wipe();

    if (!ok) {
        std::println(stderr, "[Export] failed to write {}", out_path.string());
        return vault::VaultResult::IoError;
    }
    return vault::VaultResult::Ok;
}

ExportSummary export_images(const vault::Vault&                      vault,
                            std::span<const vault::IndexNode* const> images,
                            const fs::path&                          dest_dir,
                            ExportConsent                            consent,
                            vault::OpProgress*                       progress)
{
    ExportSummary sum;
    if (consent != ExportConsent::Confirm) return sum;  // decline writes nothing

    auto exists = [](const fs::path& p) {
        std::error_code ec;
        return fs::exists(p, ec);
    };

    if (progress) progress->total.store(static_cast<int>(images.size()));

    crypto::SecureBytes scratch;
    for (const vault::IndexNode* node : images) {
        if (progress && progress->cancel.load()) break;   // stop between files; written so far kept
        if (node == nullptr || !(node->is_image() || node->is_video())) {
            ++sum.failed;
        } else {
            // The vault's index is untrusted input: defang the name, then verify
            // the path it produced really is inside dest_dir before writing.
            const fs::path out =
                unique_export_path(dest_dir, vault::sanitize_node_name(node->name), exists);
            if (!export_path_within(dest_dir, out)) {
                std::println(stderr,
                             "[Export] refusing to write outside the chosen folder: {}",
                             out.string());
                ++sum.failed;
            } else if (export_one_media(vault, *node, out, scratch) == vault::VaultResult::Ok) {
                ++sum.written;
            } else {
                ++sum.failed;
            }
        }
        if (progress) progress->done.fetch_add(1);
    }
    return sum;
}

} // namespace ui
