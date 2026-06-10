#include "vault.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <utility>

#include "crypto/aead.h"
#include "crypto/kdf.h"
#include "crypto/random.h"

#include "chunk_store.h"
#include "file_util.h"

#include "image/decode.h"
#include "image/thumbnail.h"

namespace vault {

namespace {

// Split a slash-separated path into non-empty segments. Leading/trailing/
// repeated slashes are ignored, so "", "/", "a/", "/a/b" all normalise cleanly.
std::vector<std::string_view> split_path(std::string_view p)
{
    std::vector<std::string_view> out;
    size_t i = 0;
    while (i < p.size()) {
        while (i < p.size() && p[i] == '/') ++i;
        const size_t start = i;
        while (i < p.size() && p[i] != '/') ++i;
        if (i > start) out.push_back(p.substr(start, i - start));
    }
    return out;
}

// Walk the gallery tree to the node named by `path` (templated so it serves both
// const and non-const callers). Returns nullptr if any segment is missing or is
// an image rather than a gallery.
template <typename NodeT>
NodeT* resolve_gallery(NodeT* root, std::string_view path)
{
    NodeT* cur = root;
    for (std::string_view seg : split_path(path)) {
        NodeT* next = nullptr;
        for (auto& child : cur->children) {
            if (child.is_gallery() && child.name == seg) { next = &child; break; }
        }
        if (!next) return nullptr;
        cur = next;
    }
    return cur;
}

IndexNode* child_named(IndexNode* node, std::string_view name)
{
    for (auto& c : node->children)
        if (c.name == name) return &c;
    return nullptr;
}

bool holds_images(const IndexNode& g)
{
    return std::ranges::any_of(g.children, [](const auto& c) { return c.is_image(); });
}

bool holds_galleries(const IndexNode& g)
{
    return std::ranges::any_of(g.children, [](const auto& c) { return c.is_gallery(); });
}

} // namespace

// --- lifecycle ------------------------------------------------------------

Vault::~Vault() { reset(); }

Vault::Vault(Vault&& o) noexcept
    : path_(std::move(o.path_)),
      fp_(o.fp_),
      header_(o.header_),
      unlocked_(o.unlocked_),
      master_key_(std::move(o.master_key_)),
      root_(std::move(o.root_))
{
    o.fp_       = nullptr;
    o.unlocked_ = false;
}

Vault& Vault::operator=(Vault&& o) noexcept
{
    if (this != &o) {
        reset();
        path_       = std::move(o.path_);
        fp_         = o.fp_;
        header_     = o.header_;
        unlocked_   = o.unlocked_;
        master_key_ = std::move(o.master_key_);
        root_       = std::move(o.root_);
        o.fp_       = nullptr;
        o.unlocked_ = false;
    }
    return *this;
}

void Vault::lock() noexcept
{
    master_key_.wipe();
    unlocked_ = false;
    root_     = IndexNode::gallery("");
}

void Vault::reset() noexcept
{
    lock();
    if (fp_) { std::fclose(fp_); fp_ = nullptr; }
    path_.clear();
    header_ = Header{};
}

VaultResult Vault::create(const std::string&       path,
                          std::span<const uint8_t>  password,
                          std::span<const uint8_t>  keyfile,
                          const crypto::KdfParams&  params,
                          Vault&                    out)
{
    out.reset();

    std::FILE* fp = std::fopen(path.c_str(), "w+b");
    if (!fp) return VaultResult::IoError;

    Header h;
    h.kdf              = params;
    h.kdf_algo         = 0;  // Argon2id
    h.keyfile_required = keyfile.empty() ? 0 : 1;

    crypto::SecureBuffer<crypto::KEY_SIZE> master;
    crypto::SecureBuffer<crypto::KEY_SIZE> kek;
    if (!crypto::fill_random(h.salt) ||
        !crypto::fill_random(master.span()) ||
        !crypto::fill_random(h.mk_nonce)) {
        std::fclose(fp);
        return VaultResult::CryptoError;
    }
    if (!crypto::derive_key(password, keyfile, h.salt, params, kek)) {
        std::fclose(fp);
        return VaultResult::CryptoError;
    }

    // Wrap the master key under the KEK (detached: cipher[32]||tag[16]).
    std::vector<uint8_t> wrapped;
    crypto::seal(kek.as_span(), h.mk_nonce, master.as_span(), wrapped);
    std::memcpy(h.wrapped_master_key.data(), wrapped.data(), crypto::KEY_SIZE);
    std::memcpy(h.mk_tag.data(), wrapped.data() + crypto::KEY_SIZE, crypto::TAG_SIZE);

    // Reserve the fixed header region so the data region begins at HEADER_SIZE.
    // The real header is written by commit_index() below.
    if (const std::array<uint8_t, HEADER_SIZE> placeholder{}; std::fwrite(placeholder.data(), 1, placeholder.size(), fp) != placeholder.size()) {
        std::fclose(fp);
        return VaultResult::IoError;
    }

    out.path_        = path;
    out.fp_          = fp;
    out.header_      = h;
    out.master_key_  = std::move(master);
    out.root_        = IndexNode::gallery("");
    out.unlocked_    = true;

    // Write the initial (empty) index + a valid header via the crash-safe path.
    if (const VaultResult r = out.commit_index(); r != VaultResult::Ok) { out.reset(); return r; }
    return VaultResult::Ok;
}

VaultResult Vault::open(const std::string& path, Vault& out)
{
    out.reset();

    std::FILE* fp = std::fopen(path.c_str(), "r+b");
    if (!fp) return VaultResult::IoError;

    std::array<uint8_t, HEADER_SIZE> raw{};
    if (!fileutil::seek_to(fp, 0) ||
        std::fread(raw.data(), 1, raw.size(), fp) != raw.size()) {
        std::fclose(fp);
        return VaultResult::BadFormat;
    }

    Header h;
    if (!Header::parse(raw, h)) {
        std::fclose(fp);
        return VaultResult::BadFormat;
    }

    out.path_     = path;
    out.fp_       = fp;
    out.header_   = h;
    out.unlocked_ = false;
    return VaultResult::Ok;
}

VaultResult Vault::unlock(std::span<const uint8_t> password,
                          std::span<const uint8_t> keyfile)
{
    if (fp_ == nullptr) return VaultResult::IoError;
    if (unlocked_)      return VaultResult::Ok;

    crypto::SecureBuffer<crypto::KEY_SIZE> kek;
    if (!crypto::derive_key(password, keyfile, header_.salt, header_.kdf, kek)) {
        return VaultResult::CryptoError;
    }

    // Unwrap the master key straight into mlock'd memory.
    std::array<uint8_t, crypto::KEY_SIZE + crypto::TAG_SIZE> sealed{};
    std::memcpy(sealed.data(), header_.wrapped_master_key.data(), crypto::KEY_SIZE);
    std::memcpy(sealed.data() + crypto::KEY_SIZE, header_.mk_tag.data(), crypto::TAG_SIZE);
    if (!crypto::open_to(kek.as_span(), header_.mk_nonce, sealed, master_key_.span())) {
        master_key_.wipe();
        return VaultResult::AuthFailed;  // wrong password / keyfile / tampered wrap
    }

    // Load the index from the active slot, falling back to the other slot if the
    // active one is unreadable (crash during a swap left it truncated/corrupt).
    auto load_slot = [&](uint8_t idx) {
        const IndexSlot& s = header_.slot[idx];
        if (s.length == 0) return false;
        std::vector<uint8_t> on_disk;
        if (ChunkStore store(fp_, master_key_.as_span()); !store.read_raw(s.offset, s.length, on_disk)) return false;
        std::vector<uint8_t> blob;
        if (!crypto::open(master_key_.as_span(), s.nonce, on_disk, blob)) return false;
        IndexNode tmp;
        if (!deserialize_index(blob, tmp)) return false;
        root_ = std::move(tmp);
        return true;
    };

    if (const uint8_t active = header_.active_slot == 0 ? 0 : 1; !load_slot(active) && !load_slot(active == 0 ? 1 : 0)) {
        master_key_.wipe();
        return VaultResult::BadFormat;
    }

    unlocked_ = true;
    return VaultResult::Ok;
}

// --- structure ------------------------------------------------------------

IndexNode*       Vault::find_gallery(std::string_view p)       { return resolve_gallery(&root_, p); }
const IndexNode* Vault::find_gallery(std::string_view p) const { return resolve_gallery(&root_, p); }

VaultResult Vault::create_gallery(std::string_view gallery_path)
{
    using enum VaultResult;
    if (!unlocked_) return Locked;

    const auto segments = split_path(gallery_path);
    if (segments.empty()) return AlreadyExists;  // root always exists

    IndexNode* cur     = &root_;
    bool       created = false;
    for (std::string_view seg : segments) {
        IndexNode* child = child_named(cur, seg);
        if (child) {
            if (!child->is_gallery()) return InvalidArg;  // name is an image
            cur = child;
        } else {
            // A gallery holding images cannot also hold sub-galleries.
            if (holds_images(*cur)) return InvalidArg;
            cur->children.push_back(IndexNode::gallery(std::string(seg)));
            cur     = &cur->children.back();
            created = true;
        }
    }

    if (!created) return AlreadyExists;
    return commit_index();
}

VaultResult Vault::add_image(std::string_view         gallery_path,
                             std::span<const uint8_t> file_data,
                             std::string_view         filename)
{
    using enum VaultResult;
    if (!unlocked_)        return Locked;
    if (filename.empty())  return InvalidArg;

    IndexNode* g = find_gallery(gallery_path);
    if (!g) return NotFound;
    if (holds_galleries(*g)) return InvalidArg;   // not a leaf gallery
    if (child_named(g, filename)) return AlreadyExists;

    ChunkStore store(fp_, master_key_.as_span());
    ChunkSpan  span;
    if (!store.append_chunk(file_data, span)) return IoError;

    // Phase 3: detect format/dims and generate an encrypted thumbnail chunk.
    // Soft failure: if decode or thumbnail generation fails, we still store the image
    // with format=Unknown and thumb_length=0 (same as Phase 2 behaviour).
    ImageFormat detected_fmt    = ImageFormat::Unknown;
    uint32_t    detected_width  = 0;
    uint32_t    detected_height = 0;
    ChunkSpan   thumb_span{};

    if (auto decoded = image::decode_from_memory(file_data)) {
        detected_fmt    = static_cast<ImageFormat>(decoded->format);
        detected_width  = static_cast<uint32_t>(decoded->width);
        detected_height = static_cast<uint32_t>(decoded->height);

        if (auto thumb_jpeg = image::make_thumbnail(*decoded, 256, 85))
            (void)store.append_chunk(*thumb_jpeg, thumb_span);  // best-effort
    }

    if (!store.sync()) return IoError;

    IndexNode img = IndexNode::image(std::string(filename));
    img.meta.format       = detected_fmt;
    img.meta.width        = detected_width;
    img.meta.height       = detected_height;
    img.meta.orig_size    = file_data.size();
    img.meta.created_ts   = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    img.meta.data_offset  = span.offset;
    img.meta.data_length  = span.length;
    img.meta.thumb_offset = thumb_span.offset;
    img.meta.thumb_length = thumb_span.length;
    g->children.push_back(std::move(img));

    return commit_index();
}

VaultResult Vault::read_image(const IndexNode& node, crypto::SecureBytes& out) const
{
    using enum VaultResult;
    if (!unlocked_)       return Locked;
    if (!node.is_image()) return InvalidArg;

    if (ChunkStore store(fp_, master_key_.as_span());
        !store.read_chunk({node.meta.data_offset, node.meta.data_length}, out)) {
        return AuthFailed;  // corrupt / tampered / unreadable chunk
    }
    return Ok;
}

VaultResult Vault::read_thumbnail(const IndexNode& node, crypto::SecureBytes& out) const
{
    using enum VaultResult;
    if (!unlocked_)              return Locked;
    if (!node.is_image())        return InvalidArg;
    if (node.meta.thumb_length == 0) return NotFound;

    if (ChunkStore store(fp_, master_key_.as_span());
        !store.read_chunk({node.meta.thumb_offset, node.meta.thumb_length}, out)) {
        return AuthFailed;
    }
    return Ok;
}

VaultResult Vault::remove_image(std::string_view gallery_path, std::string_view filename)
{
    using enum VaultResult;
    if (!unlocked_) return Locked;

    IndexNode* g = find_gallery(gallery_path);
    if (!g) return NotFound;

    for (auto it = g->children.begin(); it != g->children.end(); ++it) {
        if (it->is_image() && it->name == filename) {
            g->children.erase(it);  // chunk is orphaned; reclaimed by compaction
            return commit_index();
        }
    }
    return NotFound;
}

std::vector<const IndexNode*> Vault::list(std::string_view gallery_path) const
{
    std::vector<const IndexNode*> out;
    const IndexNode* g = find_gallery(gallery_path);
    if (!g) return out;
    out.reserve(g->children.size());
    for (const auto& c : g->children) out.push_back(&c);
    return out;
}

// --- persistence ----------------------------------------------------------

bool Vault::write_header()
{
    std::array<uint8_t, HEADER_SIZE> raw{};
    header_.serialize(raw);
    if (!fileutil::seek_to(fp_, 0)) return false;
    if (std::fwrite(raw.data(), 1, raw.size(), fp_) != raw.size()) return false;
    return fileutil::sync(fp_);
}

VaultResult Vault::commit_index()
{
    using enum VaultResult;
    // Serialise + seal the index with a fresh random nonce.
    std::vector<uint8_t> blob;
    serialize_index(root_, blob);

    std::array<uint8_t, crypto::NONCE_SIZE> nonce{};
    if (!crypto::fill_random(nonce)) return CryptoError;

    std::vector<uint8_t> sealed;
    crypto::seal(master_key_.as_span(), nonce, blob, sealed);

    // Step A: append the new index blob and make it durable.
    ChunkStore store(fp_, master_key_.as_span());
    uint64_t offset = 0;
    if (!store.append_raw(sealed, offset)) return IoError;
    if (!store.sync())                     return IoError;

    const uint8_t inactive = header_.active_slot == 0 ? 1 : 0;
    header_.slot[inactive] = IndexSlot{.offset = offset,
                                       .length = sealed.size(),
                                       .nonce  = nonce};

    // Step B: persist the new slot pointer with active_slot still pointing at the
    // old index — both slots are now valid on disk.
    if (!write_header()) return IoError;

    // Step C: flip active_slot. This is the atomic commit point; a crash before
    // it leaves the previous index in force, after it the new one.
    header_.active_slot = inactive;
    if (!write_header()) return IoError;

    return Ok;
}

} // namespace vault
