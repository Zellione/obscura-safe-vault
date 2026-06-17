#include "vault/transfer.h"

#include "crypto/secure_mem.h"

namespace vault {

namespace {

// Append `gallery`'s slash-path child name; "" stays "".
std::string child_path(std::string_view gallery, std::string_view name)
{
    std::string p(gallery);
    if (!p.empty()) p += '/';
    p.append(name);
    return p;
}

// Does this gallery hold any sub-gallery? (If so it cannot accept images.)
bool holds_subgalleries(const Vault& v, std::string_view gallery)
{
    for (const auto* c : v.list(gallery))
        if (c->is_gallery()) return true;
    return false;
}

void collect_targets(const Vault& v, std::string_view gallery,
                     std::vector<std::string>& out)
{
    if (!holds_subgalleries(v, gallery)) {
        out.emplace_back(gallery);          // a leaf (incl. empty) can accept images
        return;
    }
    for (const auto* c : v.list(gallery))   // recurse into sub-galleries
        if (c->is_gallery())
            collect_targets(v, child_path(gallery, c->name), out);
}

} // namespace

VaultResult move_image(Vault& src, std::string_view src_gallery,
                       std::string_view filename,
                       Vault& dst, std::string_view dst_gallery)
{
    using enum VaultResult;

    // Locate the source image node (intermediate path segments must be galleries).
    const IndexNode* node = nullptr;
    for (const auto* c : src.list(src_gallery))
        if (c->is_image() && c->name == filename) { node = c; break; }
    if (!node) return NotFound;

    // Decrypt into mlock'd memory, then re-encrypt into the destination.
    crypto::SecureBytes plain;
    if (VaultResult r = src.read_image(*node, plain); r != Ok) return r;

    // dst commits first (crash-safe: a crash before the source remove leaves a
    // recoverable duplicate, never a loss). A failed add leaves the source intact.
    if (VaultResult r = dst.add_image(dst_gallery, plain.as_span(), filename); r != Ok)
        return r;

    return src.remove_image(src_gallery, filename);
}

std::vector<std::string> image_target_galleries(const Vault& v)
{
    std::vector<std::string> out;
    if (!v.is_unlocked()) return out;
    collect_targets(v, "", out);
    return out;
}

} // namespace vault
