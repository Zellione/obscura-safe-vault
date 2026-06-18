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

#include "vault/video_format.h"

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

bool holds_media(const IndexNode& g)
{
    return std::ranges::any_of(g.children, [](const auto& c) { return c.is_media(); });
}

bool holds_galleries(const IndexNode& g)
{
    return std::ranges::any_of(g.children, [](const auto& c) { return c.is_gallery(); });
}

// Visit every media node (image or video) in the tree rooted at `n`.
// Templated so it serves both const and non-const callers.
template <typename NodeT, typename Fn>
void for_each_media(NodeT& n, Fn&& fn)
{
    if (n.is_media()) {
        fn(n);
        return;
    }
    for (auto& c : n.children) for_each_media(c, fn);
}

// Copy a media node's live chunk(s) from `src` to `dst` verbatim (ciphertext —
// no decrypt/re-encrypt, invariant #1), rewriting each offset to its new
// location. Sets `err` to IoError on the first failed read/append.
void relocate_node_chunks(const ChunkStore& src, ChunkStore& dst, IndexNode& node, VaultResult& err)
{
    auto copy_span = [&err, &src, &dst](uint64_t& off, uint64_t len) {
        if (err != VaultResult::Ok || len == 0) return;
        std::vector<uint8_t> blob;
        uint64_t new_off = 0;
        if (!src.read_raw(off, len, blob) || !dst.append_raw(blob, new_off)) {
            err = VaultResult::IoError;
            return;
        }
        off = new_off;
    };
    if (node.is_image()) {
        copy_span(node.meta.data_offset, node.meta.data_length);
        copy_span(node.meta.thumb_offset, node.meta.thumb_length);
    } else if (node.is_video()) {
        for (auto& chunk : node.vmeta.chunks) copy_span(chunk.offset, chunk.length);
        copy_span(node.vmeta.poster_offset, node.vmeta.poster_length);
    }
}

// Trim ASCII whitespace from start and end of a string_view.
std::string_view trim_ws(std::string_view s)
{
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' ||
                                 s[start] == '\n' || s[start] == '\r')) ++start;
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' ||
                            s[end - 1] == '\n' || s[end - 1] == '\r')) --end;
    return s.substr(start, end - start);
}

// Case-insensitive comparison of strings.
bool ci_equal(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        auto to_lower = [](char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; };
        if (to_lower(a[i]) != to_lower(b[i])) return false;
    }
    return true;
}

// Case-insensitive substring check.
bool ci_contains(std::string_view haystack, std::string_view needle)
{
    if (needle.empty()) return true;
    if (needle.size() > haystack.size()) return false;
    auto to_lower = [](char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; };
    for (size_t i = 0; i <= haystack.size() - needle.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            if (to_lower(haystack[i + j]) != to_lower(needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

// Normalise a single tag: trim whitespace, return empty_view if empty after trim.
std::string_view normalise_tag(std::string_view tag)
{
    return trim_ws(tag);
}

// Normalise and deduplicate a list of tags (case-insensitively, keeping first
// occurrence's casing). Empties and whitespace-only tags are dropped.
std::vector<std::string> normalise_tags(const std::vector<std::string>& input)
{
    std::vector<std::string> out;
    for (const auto& tag : input) {
        auto trimmed = normalise_tag(tag);
        if (trimmed.empty()) continue;

        // Check for case-insensitive duplicate.
        bool found = false;
        for (const auto& existing : out) {
            if (ci_equal(existing, trimmed)) {
                found = true;
                break;
            }
        }
        if (!found) {
            out.emplace_back(trimmed);
        }
    }
    return out;
}

// Build effective_tags: union of node's own tags and inherited tags, case-insensitively
// de-duplicated, preserving the node's own tags' casing first.
std::vector<std::string> compute_effective_tags(const std::vector<std::string>& node_tags,
                                                const std::vector<std::string>& inherited_tags)
{
    std::vector<std::string> out = node_tags;
    for (const auto& inh : inherited_tags) {
        bool found = false;
        for (const auto& own : node_tags) {
            if (ci_equal(own, inh)) {
                found = true;
                break;
            }
        }
        if (!found) {
            out.push_back(inh);
        }
    }
    return out;
}

// --- search helpers (Phase 12) --------------------------------------------

bool node_in_scope(const IndexNode& n, SearchScope scope)
{
    using enum SearchScope;
    return n.is_gallery() ? (scope == Galleries || scope == Both)
                          : (scope == Images || scope == Both);
}

// True if `query` is a case-insensitive substring of the name or any effective tag.
bool node_matches(std::string_view name, std::string_view query,
                  const std::vector<std::string>& effective)
{
    return ci_contains(name, query) ||
           std::ranges::any_of(effective, [&](const auto& t) { return ci_contains(t, query); });
}

std::string join_child_path(std::string_view prefix, std::string_view name)
{
    if (prefix.empty()) return std::string(name);
    return std::string(prefix) + "/" + std::string(name);
}

// Walk the tree, accumulating ancestor-gallery tags as `inherited`, collecting
// in-scope nodes that match `query`. Gallery tags cascade to descendants here.
void search_dfs(const IndexNode& node, std::string_view prefix,
                const std::vector<std::string>& inherited,
                std::string_view query, SearchScope scope,
                std::vector<SearchHit>& out)
{
    for (const auto& child : node.children) {
        auto              effective = compute_effective_tags(child.tags, inherited);
        const std::string full_path = join_child_path(prefix, child.name);

        if (node_in_scope(child, scope) && node_matches(child.name, query, effective)) {
            out.push_back(SearchHit{
                .path           = full_path,
                .is_gallery     = child.is_gallery(),
                .name           = child.name,
                .effective_tags = effective,
                .node           = &child,
            });
        }

        if (child.is_gallery())
            search_dfs(child, full_path, effective, query, scope, out);
    }
}

// Walk the tree collecting every favorited node of the requested kind (galleries
// when `want_galleries`, otherwise media: images or videos) into `out`, flat, with full paths.
// effective_tags is intentionally left empty — favorites lists don't cascade tags.
void collect_favorites(const IndexNode& node, std::string_view prefix,
                       bool want_galleries, std::vector<SearchHit>& out)
{
    for (const auto& child : node.children) {
        const std::string full_path = join_child_path(prefix, child.name);

        if (const bool matches = want_galleries ? child.is_gallery() : child.is_media();
            child.favorite && matches) {
            out.push_back(SearchHit{
                .path           = full_path,
                .is_gallery     = child.is_gallery(),
                .name           = child.name,
                .effective_tags = {},
                .node           = &child,
            });
        }

        if (child.is_gallery())
            collect_favorites(child, full_path, want_galleries, out);
    }
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

VaultResult Vault::change_password(std::span<const uint8_t> old_password,
                                   std::span<const uint8_t> old_keyfile,
                                   std::span<const uint8_t> new_password,
                                   std::span<const uint8_t> new_keyfile)
{
    using enum VaultResult;
    if (fp_ == nullptr) return IoError;

    // Verify the old credentials by unwrapping the master key from the header
    // into a scratch buffer (the vault's own key state is untouched until the
    // new wrap is safely on disk).
    crypto::SecureBuffer<crypto::KEY_SIZE> kek;
    if (!crypto::derive_key(old_password, old_keyfile, header_.salt, header_.kdf, kek)) {
        return CryptoError;
    }
    std::array<uint8_t, crypto::KEY_SIZE + crypto::TAG_SIZE> sealed{};
    std::memcpy(sealed.data(), header_.wrapped_master_key.data(), crypto::KEY_SIZE);
    std::memcpy(sealed.data() + crypto::KEY_SIZE, header_.mk_tag.data(), crypto::TAG_SIZE);
    crypto::SecureBuffer<crypto::KEY_SIZE> master;
    if (!crypto::open_to(kek.as_span(), header_.mk_nonce, sealed, master.span())) {
        return AuthFailed;  // wrong old password / keyfile
    }

    // Re-wrap under the new KEK with a fresh salt and nonce (never reuse
    // either — a reused salt would let one cracked password open both wraps).
    Header h = header_;
    if (!crypto::fill_random(h.salt) || !crypto::fill_random(h.mk_nonce)) {
        return CryptoError;
    }
    if (!crypto::derive_key(new_password, new_keyfile, h.salt, h.kdf, kek)) {
        return CryptoError;
    }
    std::vector<uint8_t> wrapped;
    crypto::seal(kek.as_span(), h.mk_nonce, master.as_span(), wrapped);
    std::memcpy(h.wrapped_master_key.data(), wrapped.data(), crypto::KEY_SIZE);
    std::memcpy(h.mk_tag.data(), wrapped.data() + crypto::KEY_SIZE, crypto::TAG_SIZE);
    h.keyfile_required = new_keyfile.empty() ? 0 : 1;

    header_ = h;
    if (!write_header()) return IoError;
    return Ok;
}

// --- structure ------------------------------------------------------------

IndexNode*       Vault::find_gallery(std::string_view p)       { return resolve_gallery(&root_, p); }
const IndexNode* Vault::find_gallery(std::string_view p) const { return resolve_gallery(&root_, p); }

// Resolve a path to any node (gallery or image). The final segment may be either.
// Intermediate segments must be galleries.
template <typename NodeT>
NodeT* resolve_node_impl(NodeT* root, std::string_view path)
{
    auto segments = split_path(path);
    if (segments.empty()) return root;  // Empty path -> root.

    NodeT* cur = root;
    // All but the last segment must be galleries.
    for (size_t i = 0; i < segments.size() - 1; ++i) {
        NodeT* next = nullptr;
        for (auto& child : cur->children) {
            if (child.is_gallery() && child.name == segments[i]) {
                next = &child;
                break;
            }
        }
        if (!next) return nullptr;
        cur = next;
    }

    // The last segment can be any node.
    for (auto& child : cur->children) {
        if (child.name == segments.back()) return &child;
    }
    return nullptr;
}

IndexNode*       Vault::resolve_node(std::string_view path)       { return resolve_node_impl(&root_, path); }
const IndexNode* Vault::resolve_node(std::string_view path) const { return resolve_node_impl(&root_, path); }

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
            // A gallery holding media cannot also hold sub-galleries.
            if (holds_media(*cur)) return InvalidArg;
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
    if (!node.is_media())        return InvalidArg;

    // Determine thumbnail location: video uses poster, image uses meta.
    const uint64_t thumb_len = node.is_video() ? node.vmeta.poster_length : node.meta.thumb_length;
    const uint64_t thumb_off = node.is_video() ? node.vmeta.poster_offset : node.meta.thumb_offset;
    if (thumb_len == 0) return NotFound;

    if (ChunkStore store(fp_, master_key_.as_span());
        !store.read_chunk({thumb_off, thumb_len}, out)) {
        return AuthFailed;
    }
    return Ok;
}

VaultResult Vault::add_video(std::string_view         gallery_path,
                             std::span<const uint8_t> file_data,
                             std::string_view         filename,
                             uint32_t                 chunk_size)
{
    using enum VaultResult;
    if (!unlocked_)             return Locked;
    if (filename.empty())       return InvalidArg;
    if (chunk_size == 0)        return InvalidArg;

    const VideoContainer container = detect_video_container(file_data);
    if (container == VideoContainer::Unknown) return InvalidArg;   // not a video we accept

    IndexNode* g = find_gallery(gallery_path);
    if (!g) return NotFound;
    if (holds_galleries(*g))    return InvalidArg;   // not a leaf gallery
    if (child_named(g, filename)) return AlreadyExists;

    ChunkStore store(fp_, master_key_.as_span());
    std::vector<VideoChunk> chunks;
    for (size_t off = 0; off < file_data.size(); off += chunk_size) {
        const size_t len = std::min<size_t>(chunk_size, file_data.size() - off);
        ChunkSpan span;
        if (!store.append_chunk(file_data.subspan(off, len), span)) return IoError;
        chunks.push_back({span.offset, span.length});
    }
    // An empty file would store zero chunks; treat as invalid (no video stream).
    if (chunks.empty()) return InvalidArg;
    if (!store.sync())  return IoError;

    IndexNode vid = IndexNode::video(std::string(filename));
    vid.vmeta.container  = container;
    vid.vmeta.codec      = VideoCodec::Unknown;   // PR4 fills these
    vid.vmeta.width      = 0;
    vid.vmeta.height     = 0;
    vid.vmeta.duration_us= 0;
    vid.vmeta.orig_size  = file_data.size();
    vid.vmeta.created_ts = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    vid.vmeta.chunk_size = chunk_size;
    vid.vmeta.chunks     = std::move(chunks);
    vid.vmeta.poster_offset = 0;
    vid.vmeta.poster_length = 0;
    g->children.push_back(std::move(vid));

    return commit_index();
}

VaultResult Vault::read_video(const IndexNode& node, crypto::SecureBytes& out) const
{
    using enum VaultResult;
    if (!unlocked_)       return Locked;
    if (!node.is_video()) return InvalidArg;

    if (!out.resize(node.vmeta.orig_size)) return IoError;
    ChunkStore store(fp_, master_key_.as_span());
    size_t pos = 0;
    for (const VideoChunk& c : node.vmeta.chunks) {
        crypto::SecureBytes piece;
        if (!store.read_chunk({c.offset, c.length}, piece)) { (void)out.resize(0); return AuthFailed; }
        if (pos + piece.size() > out.size())                { (void)out.resize(0); return AuthFailed; }
        std::copy(piece.data(), piece.data() + piece.size(), out.data() + pos);
        pos += piece.size();
    }
    if (pos != out.size()) { (void)out.resize(0); return AuthFailed; }
    return Ok;
}

VaultResult Vault::remove_image(std::string_view gallery_path, std::string_view filename)
{
    using enum VaultResult;
    if (!unlocked_) return Locked;

    IndexNode* g = find_gallery(gallery_path);
    if (!g) return NotFound;

    for (auto it = g->children.begin(); it != g->children.end(); ++it) {
        if (it->is_media() && it->name == filename) {  // remove image or video
            g->children.erase(it);  // chunk(s) are orphaned until compaction
            if (const VaultResult r = commit_index(); r != Ok) return r;

            // Best-effort space reclamation: the remove itself already
            // succeeded, so a failed compaction only leaves waste behind.
            uint64_t size = 0;
            if (const uint64_t waste = wasted_bytes();
                waste >= AUTO_COMPACT_MIN_WASTE &&
                fileutil::file_size(fp_, size) &&
                waste * AUTO_COMPACT_WASTE_RATIO >= size) {
                (void)compact();
            }
            return Ok;
        }
    }
    return NotFound;
}

VaultResult Vault::remove_gallery(std::string_view gallery_path)
{
    using enum VaultResult;
    if (!unlocked_) return Locked;

    const auto segments = split_path(gallery_path);
    if (segments.empty()) return InvalidArg;   // the root cannot be removed

    // Walk to the parent of the target (all segments but the last).
    IndexNode* parent = &root_;
    for (size_t i = 0; i + 1 < segments.size(); ++i) {
        parent = child_named(parent, segments[i]);
        if (!parent || !parent->is_gallery()) return NotFound;
    }

    const std::string_view name = segments.back();
    for (auto it = parent->children.begin(); it != parent->children.end(); ++it) {
        if (it->is_gallery() && it->name == name) {
            parent->children.erase(it);  // whole subtree's chunks orphaned until compaction
            if (const VaultResult r = commit_index(); r != Ok) return r;

            // Best-effort reclamation (same gate as remove_image).
            uint64_t size = 0;
            if (const uint64_t waste = wasted_bytes();
                waste >= AUTO_COMPACT_MIN_WASTE &&
                fileutil::file_size(fp_, size) &&
                waste * AUTO_COMPACT_WASTE_RATIO >= size) {
                (void)compact();
            }
            return Ok;
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

VaultResult Vault::set_tags(std::string_view node_path, const std::vector<std::string>& tags)
{
    using enum VaultResult;
    if (!unlocked_) return Locked;

    IndexNode* node = resolve_node(node_path);
    if (!node) return NotFound;

    auto normalised = normalise_tags(tags);

    // Only commit if the tags changed.
    if (node->tags == normalised) return Ok;

    node->tags = std::move(normalised);
    return commit_index();
}

VaultResult Vault::add_tag(std::string_view node_path, std::string_view tag)
{
    using enum VaultResult;
    if (!unlocked_) return Locked;

    auto trimmed = normalise_tag(tag);
    if (trimmed.empty()) return InvalidArg;

    IndexNode* node = resolve_node(node_path);
    if (!node) return NotFound;

    // Check for case-insensitive duplicate.
    for (const auto& existing : node->tags) {
        if (ci_equal(existing, trimmed)) return Ok;
    }

    // Not found, add it.
    node->tags.emplace_back(trimmed);
    return commit_index();
}

VaultResult Vault::remove_tag(std::string_view node_path, std::string_view tag)
{
    using enum VaultResult;
    if (!unlocked_) return Locked;

    auto trimmed = normalise_tag(tag);
    if (trimmed.empty()) return Ok;  // Idempotent: removing nonexistent empty tag is Ok.

    IndexNode* node = resolve_node(node_path);
    if (!node) return NotFound;

    // Find and remove the tag case-insensitively.
    for (auto it = node->tags.begin(); it != node->tags.end(); ++it) {
        if (ci_equal(*it, trimmed)) {
            node->tags.erase(it);
            return commit_index();
        }
    }

    return Ok;  // Idempotent: tag not found.
}

std::vector<SearchHit> Vault::search(std::string_view query, SearchScope scope) const
{
    std::vector<SearchHit> out;
    if (!unlocked_) return out;

    // Seed inherited tags with the root's own tags so they cascade globally; the
    // unnamed root itself is never a hit.
    search_dfs(root_, "", root_.tags, query, scope, out);
    return out;
}

VaultResult Vault::toggle_favorite(std::string_view node_path)
{
    using enum VaultResult;
    if (!unlocked_) return Locked;

    IndexNode* node = resolve_node(node_path);
    if (!node) return NotFound;

    node->favorite = !node->favorite;
    return commit_index();
}

std::vector<SearchHit> Vault::list_favorite_images() const
{
    std::vector<SearchHit> out;
    if (!unlocked_) return out;
    collect_favorites(root_, "", /*want_galleries=*/false, out);
    return out;
}

std::vector<SearchHit> Vault::list_favorite_galleries() const
{
    std::vector<SearchHit> out;
    if (!unlocked_) return out;
    collect_favorites(root_, "", /*want_galleries=*/true, out);
    return out;
}

// --- compaction -------------------------------------------------------------

uint64_t Vault::wasted_bytes() const
{
    if (!unlocked_ || !fp_) return 0;

    uint64_t size = 0;
    if (!fileutil::file_size(fp_, size)) return 0;

    uint64_t live = HEADER_SIZE + header_.slot[header_.active_slot].length;
    for_each_media(root_, [&live](const IndexNode& node) {
        if (node.is_image()) {
            live += node.meta.data_length + node.meta.thumb_length;
        } else if (node.is_video()) {
            // Sum all video chunks plus optional poster.
            for (const auto& chunk : node.vmeta.chunks) {
                live += chunk.length;
            }
            live += node.vmeta.poster_length;
        }
    });
    return size > live ? size - live : 0;
}

VaultResult Vault::compact()
{
    using enum VaultResult;
    if (!unlocked_) return Locked;

    const std::string tmp_path = path_ + ".compact";
    std::FILE* tmp = std::fopen(tmp_path.c_str(), "w+b");
    if (!tmp) return IoError;

    // Any failure below abandons the temp file; the original vault is untouched.
    auto fail = [&](VaultResult r) {
        std::fclose(tmp);
        std::remove(tmp_path.c_str());
        return r;
    };

    std::array<uint8_t, HEADER_SIZE> raw{};
    if (std::fwrite(raw.data(), 1, raw.size(), tmp) != raw.size()) return fail(IoError);

    // Copy each live chunk verbatim. A byte-identical `nonce|ciphertext|tag`
    // copy under the same key reveals nothing new (it IS the old ciphertext),
    // keeps invariant #1 (plaintext never touches an unlocked buffer here),
    // and skips a pointless decrypt/re-encrypt pass.
    IndexNode  new_root = root_;
    ChunkStore src(fp_, master_key_.as_span());
    ChunkStore dst(tmp, master_key_.as_span());

    VaultResult copy_err = Ok;
    for_each_media(new_root, [&src, &dst, &copy_err](IndexNode& node) {
        relocate_node_chunks(src, dst, node, copy_err);
    });
    if (copy_err != Ok) return fail(copy_err);

    // Fresh sealed index into slot A; slot B starts empty in the new file.
    std::vector<uint8_t> blob;
    serialize_index(new_root, blob);
    std::array<uint8_t, crypto::NONCE_SIZE> nonce{};
    if (!crypto::fill_random(nonce)) return fail(CryptoError);
    std::vector<uint8_t> sealed;
    crypto::seal(master_key_.as_span(), nonce, blob, sealed);

    uint64_t idx_off = 0;
    if (!dst.append_raw(sealed, idx_off) || !dst.sync()) return fail(IoError);

    Header h      = header_;  // KDF params, salt, and master-key wrap carry over
    h.slot[0]     = IndexSlot{.offset = idx_off, .length = sealed.size(), .nonce = nonce};
    h.slot[1]     = IndexSlot{};
    h.active_slot = 0;
    h.serialize(raw);
    if (!fileutil::seek_to(tmp, 0) ||
        std::fwrite(raw.data(), 1, raw.size(), tmp) != raw.size() ||
        !fileutil::sync(tmp)) {
        return fail(IoError);
    }
    std::fclose(tmp);

    // Atomic commit point: rename the fully-synced new vault over the original.
    // Close our handle first — Windows refuses to replace a file that is open
    // (POSIX would happily swap the inode under us).
    std::fclose(fp_);
    fp_ = nullptr;
    if (!fileutil::rename_file(tmp_path, path_)) {
        std::remove(tmp_path.c_str());
        // The original is untouched; reacquire our handle to it.
        fp_ = std::fopen(path_.c_str(), "r+b");
        if (!fp_) reset();  // intact on disk; force a clean re-open
        return IoError;
    }
    fileutil::sync_dir_of(path_);

    fp_ = std::fopen(path_.c_str(), "r+b");
    if (!fp_) {
        // The compacted vault is intact on disk but we lost our handle to it;
        // wipe keys and force a clean re-open rather than limp along.
        reset();
        return IoError;
    }
    header_ = h;
    root_   = std::move(new_root);
    return Ok;
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
