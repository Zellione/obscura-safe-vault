#include "ui/export.h"

#include <algorithm>
#include <cstdio>
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

// Phase 53: videos became selectable, so a selection can legitimately contain
// one. A gallery still has no stored bytes of its own and is rejected — calling
// that an "export" would be a lie.
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

ExportSummary export_images(const vault::Vault&                      vault,
                            std::span<const vault::IndexNode* const> images,
                            const fs::path&                          dest_dir,
                            ExportConsent                            consent,
                            vault::OpProgress*                       progress)
{
    ExportSummary sum;
    if (consent != ExportConsent::Confirm) return sum;  // decline writes nothing

    if (progress) progress->total.store(static_cast<int>(images.size()));

    crypto::SecureBytes scratch;
    for (const vault::IndexNode* node : images) {
        if (progress && progress->cancel.load()) break;   // stop between files; written so far kept
        if (node == nullptr || !(node->is_image() || node->is_video())) {
            ++sum.failed;
        } else {
            // Phase 98 (OSV-AUD-005): no check-then-open. The vault's index is
            // untrusted input, so the name is first defanged
            // (vault::sanitize_node_name), then ATOMICALLY claimed inside
            // dest_dir by the platform helper — the exclusive create doubles as
            // the collision test and the containment enforcement, and it never
            // follows a symlink or truncates an existing entry. export_one_media
            // then writes only to the returned handle, never reopening a path.
            auto out = platform::create_new_file_within(
                dest_dir, vault::sanitize_node_name(node->name.view()));

            if (!out) {
                platform::safe_println(stderr,
                             "[Export] could not create a new file in {}",
                             platform::path_to_utf8(dest_dir));
                ++sum.failed;
            } else if (!export_path_within(dest_dir, out->display_path)) {
                // Invariant assertion (defense in depth): the atomic helper must
                // never hand back an escaping name. A single hostile vault name
                // already passed sanitize_node_name; if containment still fails,
                // something is very wrong — refuse rather than write.
                platform::safe_println(stderr,
                             "[Export] refusing a resolved path outside the chosen folder: {}",
                             platform::path_to_utf8(out->display_path));
                ++sum.failed;
            } else if (export_one_media(vault, *node, std::move(*out), scratch)
                       == vault::VaultResult::Ok) {
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
