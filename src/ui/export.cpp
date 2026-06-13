#include "ui/export.h"

#include <cstdio>
#include <print>
#include <string>

namespace ui {

namespace fs = std::filesystem;

std::filesystem::path unique_export_path(
    const fs::path& dir, std::string_view filename,
    const std::function<bool(const fs::path&)>& exists)
{
    fs::path candidate = dir / filename;
    if (!exists(candidate)) return candidate;

    // Split "name.ext" so the counter goes before the extension: "name (1).ext".
    const fs::path base(filename);
    const std::string stem = base.stem().string();
    const std::string ext  = base.extension().string();
    for (int n = 1;; ++n) {
        candidate = dir / (stem + " (" + std::to_string(n) + ")" + ext);
        if (!exists(candidate)) return candidate;
    }
}

vault::VaultResult export_one_image(const vault::Vault&          vault,
                                    const vault::IndexNode&      node,
                                    const fs::path&              out_path,
                                    crypto::SecureBytes&         scratch)
{
    if (!node.is_image()) return vault::VaultResult::InvalidArg;

    // Decrypt the original stored bytes into mlock'd memory (invariant #1 holds
    // right up to the write below).
    if (auto rc = vault.read_image(node, scratch); rc != vault::VaultResult::Ok) {
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
                            ExportConsent                            consent)
{
    ExportSummary sum;
    if (consent != ExportConsent::Confirm) return sum;  // decline writes nothing

    auto exists = [](const fs::path& p) {
        std::error_code ec;
        return fs::exists(p, ec);
    };

    crypto::SecureBytes scratch;
    for (const vault::IndexNode* node : images) {
        if (node == nullptr || !node->is_image()) {
            ++sum.failed;
            continue;
        }
        const fs::path out = unique_export_path(dest_dir, node->name, exists);
        if (export_one_image(vault, *node, out, scratch) == vault::VaultResult::Ok)
            ++sum.written;
        else
            ++sum.failed;
    }
    return sum;
}

} // namespace ui
