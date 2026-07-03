#include "vault.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstring>
#include <utility>

#include "crypto/aead.h"
#include "crypto/kdf.h"
#include "crypto/random.h"

#include "chunk_codec.h"
#include "chunk_store.h"
#include "file_util.h"

#include "image/decode.h"
#include "image/thumbnail.h"

#include "vault/video_format.h"
#include "vault/vault_search.h"
#include "vault/index_io.h"
#include "vault/vault_ops.h"
#include "media/video_probe.h"
#include "ui/advanced_search_model.h"  // AdvancedQuery + evaluate (pure, SDL/vault-free)

namespace vault {

namespace {

// Wrappers around vault_ops functions for backward compatibility in this TU,
// avoiding the need to update every call site.
using vault_ops::split_path;
using vault_ops::resolve_gallery;
using vault_ops::child_named;
using vault_ops::holds_media;
using vault_ops::holds_galleries;
using vault_ops::for_each_media;
using vault_ops::relocate_node_chunks;

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

// Walk the whole tree, accumulating distinct tags (case-insensitive, first-seen
// casing kept) into `out` (Phase 18 — feeds tag autocomplete).
void collect_tags(const IndexNode& node, std::vector<std::string>& out)
{
    for (const auto& t : node.tags) {
        if (!std::ranges::any_of(out, [&](const auto& x) { return ci_equal(x, t); }))
            out.push_back(t);
    }
    for (const auto& c : node.children) collect_tags(c, out);
}

// Count, per distinct tag, the galleries and leaf media that DIRECTLY carry it
// (Phase 22 — no cascade, so a gallery tag never inflates its descendants).
// `tallies` is pre-seeded with one zeroed entry per distinct tag (canonical
// casing from collect_tags); this just bumps the matching entry for each node's
// own tags. Non-gallery nodes (images + videos) count toward image_count.
// Bump the tally for `tag` (matched case-insensitively against the pre-seeded
// vocabulary) by one gallery or one image. Kept separate from the recursive walk
// to keep that walk's nesting shallow (cpp:S134).
void bump_tag_tally(std::vector<ui::TagTally>& tallies, std::string_view tag, bool is_gallery)
{
    auto it = std::ranges::find_if(tallies, [&](const ui::TagTally& tt) {
        return ci_equal(tt.tag, tag);
    });
    if (it == tallies.end()) return;
    if (is_gallery) ++it->gallery_count;
    else            ++it->image_count;
}

void count_direct_tags(const IndexNode& node, std::vector<ui::TagTally>& tallies)
{
    for (const auto& child : node.children) {
        for (const auto& t : child.tags)
            bump_tag_tally(tallies, t, child.is_gallery());
        if (child.is_gallery()) count_direct_tags(child, tallies);
    }
}

// Collect every gallery that DIRECTLY carries `tag` (case-insensitive exact
// match — not substring, not cascade), flat with full paths (Phase 22).
// effective_tags is left empty (this lookup never computes the cascade).
void collect_galleries_with_tag(const IndexNode& node, std::string_view prefix,
                                std::string_view tag, std::vector<SearchHit>& out)
{
    for (const auto& child : node.children) {
        if (!child.is_gallery()) continue;
        const std::string full_path = join_child_path(prefix, child.name);
        if (std::ranges::any_of(child.tags, [&](const auto& t) { return ci_equal(t, tag); }))
            out.push_back(SearchHit{
                .path           = full_path,
                .is_gallery     = true,
                .name           = child.name,
                .effective_tags = {},
                .node           = &child,
            });
        collect_galleries_with_tag(child, full_path, tag, out);
    }
}

// Collect every leaf media node (image or video) that DIRECTLY carries `tag`
// (case-insensitive exact match — not substring, not cascade), flat with full
// paths. effective_tags is left empty (this lookup never computes the cascade).
// Mirrors collect_galleries_with_tag but for !is_gallery() nodes (Phase 22 f/u).
void collect_images_with_tag(const IndexNode& node, std::string_view prefix,
                             std::string_view tag, std::vector<SearchHit>& out)
{
    for (const auto& child : node.children) {
        const std::string full_path = join_child_path(prefix, child.name);
        if (child.is_gallery()) {
            collect_images_with_tag(child, full_path, tag, out);
            continue;
        }
        if (std::ranges::any_of(child.tags, [&](const auto& t) { return ci_equal(t, tag); }))
            out.push_back(SearchHit{
                .path           = full_path,
                .is_gallery     = false,
                .name           = child.name,
                .effective_tags = {},
                .node           = &child,
            });
    }
}

// Advanced-search DFS (Phase 18): evaluate `query` against every in-scope node,
// cascading gallery tags, collecting each match with its relevance score.
void adv_search_dfs(const IndexNode& node, std::string_view prefix,
                    const std::vector<std::string>& inherited,
                    const ui::AdvancedQuery& query, ui::SearchScope scope,
                    std::vector<std::pair<int, SearchHit>>& out)
{
    using enum ui::SearchScope;
    for (const auto& child : node.children) {
        auto              effective = compute_effective_tags(child.tags, inherited);
        const std::string full_path = join_child_path(prefix, child.name);

        if (const bool in_scope = child.is_gallery() ? (scope == Galleries || scope == Both)
                                                     : (scope == Images || scope == Both);
            in_scope) {
            if (const ui::EvalResult r = ui::evaluate(query, child.name, effective); r.matched) {
                out.emplace_back(r.score, SearchHit{
                    .path           = full_path,
                    .is_gallery     = child.is_gallery(),
                    .name           = child.name,
                    .effective_tags = effective,
                    .node           = &child,
                });
            }
        }

        if (child.is_gallery())
            adv_search_dfs(child, full_path, effective, query, scope, out);
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
      root_(std::move(o.root_)),
      saved_searches_(std::move(o.saved_searches_))
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
        saved_searches_ = std::move(o.saved_searches_);
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
    saved_searches_.clear();
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
    h.flags |= FLAG_FRAMED_CHUNKS;  // Phase 26: new vaults frame chunk + index plaintext

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
        if (ChunkStore store(fp_, master_key_.as_span(), framed_chunks(header_)); !store.read_raw(s.offset, s.length, on_disk)) return false;
        std::vector<uint8_t> blob;
        if (!crypto::open(master_key_.as_span(), s.nonce, on_disk, blob)) return false;
        if (framed_chunks(header_)) {
            std::vector<uint8_t> plain;
            if (!chunk_codec::decode_frame(blob, plain)) return false;
            blob = std::move(plain);
        }
        IndexNode tmp;
        std::vector<SavedSearch> tmp_searches;
        if (!deserialize_index(blob, tmp, tmp_searches)) return false;
        root_           = std::move(tmp);
        saved_searches_ = std::move(tmp_searches);
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

IndexNode*       Vault::find_gallery(std::string_view p)       { return vault_ops::resolve_gallery(&root_, p); }
const IndexNode* Vault::find_gallery(std::string_view p) const { return vault_ops::resolve_gallery(&root_, p); }

IndexNode*       Vault::resolve_node(std::string_view path)       { return vault_ops::resolve_node_impl(&root_, path); }
const IndexNode* Vault::resolve_node(std::string_view path) const { return vault_ops::resolve_node_impl(&root_, path); }

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

    ChunkStore store(fp_, master_key_.as_span(), framed_chunks(header_));
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

    if (ChunkStore store(fp_, master_key_.as_span(), framed_chunks(header_));
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

    if (ChunkStore store(fp_, master_key_.as_span(), framed_chunks(header_));
        !store.read_chunk({thumb_off, thumb_len}, out)) {
        return AuthFailed;
    }
    return Ok;
}

VaultResult read_thumb_span(const Vault& v, uint64_t offset, uint64_t length,
                            crypto::SecureBytes& out)
{
    using enum VaultResult;
    if (!v.unlocked_)  return Locked;
    if (length == 0)   return InvalidArg;

    if (ChunkStore store(v.fp_, v.master_key_.as_span(), framed_chunks(v.header_));
        !store.read_chunk({offset, length}, out)) {
        return AuthFailed;
    }
    return Ok;
}

uint64_t vault_file_bytes(const Vault& v) noexcept
{
    if (!v.unlocked_ || !v.fp_) return 0;
    uint64_t size = 0;
    if (!fileutil::file_size(v.fp_, size)) return 0;
    return size;
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

    // Probe the video file first (before storing chunks) to detect metadata and generate poster.
    // This ensures we don't create orphan chunks if the video is invalid.
    media::VideoProbeResult probe;
    if (!media::probe_video(file_data, probe)) return InvalidArg;

    IndexNode* g = find_gallery(gallery_path);
    if (!g) return NotFound;
    if (holds_galleries(*g))    return InvalidArg;   // not a leaf gallery
    if (child_named(g, filename)) return AlreadyExists;

    ChunkStore store(fp_, master_key_.as_span(), framed_chunks(header_));
    std::vector<VideoChunk> chunks;
    for (size_t off = 0; off < file_data.size(); off += chunk_size) {
        const size_t len = std::min<size_t>(chunk_size, file_data.size() - off);
        ChunkSpan span;
        if (!store.append_chunk(file_data.subspan(off, len), span)) return IoError;
        chunks.push_back({span.offset, span.length});
    }
    // An empty file would store zero chunks; treat as invalid (no video stream).
    if (chunks.empty()) return InvalidArg;

    // Store the poster if it was generated.
    uint64_t poster_offset = 0;
    uint64_t poster_length = 0;
    if (!probe.poster_jpeg.empty()) {
        ChunkSpan poster_span;
        if (!store.append_chunk(probe.poster_jpeg, poster_span)) return IoError;
        poster_offset = poster_span.offset;
        poster_length = poster_span.length;
    }

    if (!store.sync())  return IoError;

    IndexNode vid = IndexNode::video(std::string(filename));
    vid.vmeta.container  = probe.container;
    vid.vmeta.codec      = probe.codec;
    vid.vmeta.width      = probe.width;
    vid.vmeta.height     = probe.height;
    vid.vmeta.duration_us= probe.duration_us;
    vid.vmeta.orig_size  = file_data.size();
    vid.vmeta.created_ts = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    vid.vmeta.chunk_size = chunk_size;
    vid.vmeta.chunks     = std::move(chunks);
    vid.vmeta.poster_offset = poster_offset;
    vid.vmeta.poster_length = poster_length;
    g->children.push_back(std::move(vid));

    return commit_index();
}

VaultResult Vault::read_video(const IndexNode& node, crypto::SecureBytes& out) const
{
    using enum VaultResult;
    if (!unlocked_)       return Locked;
    if (!node.is_video()) return InvalidArg;

    if (!out.resize(node.vmeta.orig_size)) return IoError;
    ChunkStore store(fp_, master_key_.as_span(), framed_chunks(header_));
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

// --- VaultSearch facade (Phase 18) -----------------------------------------
// Defined here (rather than a separate TU) so it can reuse the file-local DFS /
// tag-collection helpers; it reaches into the borrowed Vault as a friend.

std::vector<std::string> VaultSearch::all_tags() const
{
    std::vector<std::string> out;
    if (!v_.unlocked_) return out;
    collect_tags(v_.root_, out);
    // Sort case-insensitively for a stable autocomplete vocabulary.
    std::ranges::sort(out, [](std::string_view a, std::string_view b) {
        return std::ranges::lexicographical_compare(a, b, [](char x, char y) {
            return std::tolower(static_cast<unsigned char>(x)) <
                   std::tolower(static_cast<unsigned char>(y));
        });
    });
    return out;
}

std::vector<SearchHit> VaultSearch::run_search(const ui::AdvancedQuery& query) const
{
    std::vector<SearchHit> out;
    if (!v_.unlocked_) return out;

    std::vector<std::pair<int, SearchHit>> scored;
    adv_search_dfs(v_.root_, "", v_.root_.tags, query, query.scope, scored);

    // Rank by descending score, breaking ties by ascending path for stability.
    std::ranges::sort(scored, [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second.path < b.second.path;
    });

    out.reserve(scored.size());
    for (auto& [score, hit] : scored) out.push_back(std::move(hit));
    return out;
}

std::vector<ui::TagTally> VaultSearch::tag_overview() const
{
    if (!v_.unlocked_) return {};

    // Seed one zeroed tally per distinct tag, reusing all_tags' vocabulary +
    // canonical first-seen casing, then count direct carriers in a single walk.
    std::vector<std::string> vocab;
    collect_tags(v_.root_, vocab);

    std::vector<ui::TagTally> out;
    out.reserve(vocab.size());
    for (auto& t : vocab) out.push_back(ui::TagTally{.tag = std::move(t)});

    count_direct_tags(v_.root_, out);
    return out;
}

std::vector<SearchHit> VaultSearch::galleries_with_tag(std::string_view tag) const
{
    std::vector<SearchHit> out;
    if (!v_.unlocked_ || tag.empty()) return out;
    collect_galleries_with_tag(v_.root_, "", tag, out);
    return out;
}

std::vector<SearchHit> VaultSearch::images_with_tag(std::string_view tag) const
{
    std::vector<SearchHit> out;
    if (!v_.unlocked_ || tag.empty()) return out;
    collect_images_with_tag(v_.root_, "", tag, out);
    return out;
}

std::vector<SavedSearch> VaultSearch::list_saved_searches() const
{
    if (!v_.unlocked_) return {};
    return v_.saved_searches_;
}

VaultResult VaultSearch::save_search(std::string_view name, const ui::AdvancedQuery& query)
{
    using enum VaultResult;
    if (!v_.unlocked_)  return Locked;
    if (name.empty())   return InvalidArg;

    std::vector<uint8_t> blob = ui::serialize_query(query);

    // Upsert: replace an existing same-name entry, else append (bounded).
    for (auto& s : v_.saved_searches_) {
        if (s.name == name) { s.query = std::move(blob); return v_.commit_index(); }
    }
    if (v_.saved_searches_.size() >= INDEX_MAX_SAVED_SEARCHES) return InvalidArg;
    v_.saved_searches_.emplace_back(std::string(name), std::move(blob));
    return v_.commit_index();
}

VaultResult VaultSearch::delete_saved_search(std::string_view name)
{
    using enum VaultResult;
    if (!v_.unlocked_) return Locked;

    const auto it = std::ranges::find_if(v_.saved_searches_,
                                         [&](const SavedSearch& s) { return s.name == name; });
    if (it == v_.saved_searches_.end()) return NotFound;
    v_.saved_searches_.erase(it);
    return v_.commit_index();
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

// --- compaction helpers ---

namespace {

// Count total chunks to copy for progress reporting.
void count_compact_chunks(const IndexNode& root, int& total_chunks)
{
    for_each_media(root, [&total_chunks](const IndexNode& node) {
        if (node.is_image()) {
            ++total_chunks;  // data + thumb each are separate for counting
        } else if (node.is_video()) {
            total_chunks += static_cast<int>(node.vmeta.chunks.size()) + 1;  // +1 for poster
        }
    });
}

// Copy all chunks with progress tracking and cancellation support.
VaultResult copy_compact_chunks(IndexNode& root, const ChunkStore& src, ChunkStore& dst,
                                 OpProgress* progress)
{
    using enum VaultResult;
    VaultResult copy_err = Ok;
    int chunks_done = 0;
    for_each_media(root, [progress, &copy_err, &src, &dst, &chunks_done](IndexNode& node) {
        // Check for cancellation before processing each node.
        if (progress && progress->cancel.load()) {
            copy_err = VaultResult::Ok;  // signal to abort, but it's not an error
            return;         // Early return from lambda
        }
        relocate_node_chunks(src, dst, node, copy_err);
        if (progress) {
            if (node.is_image()) {
                ++chunks_done;
            } else if (node.is_video()) {
                chunks_done += static_cast<int>(node.vmeta.chunks.size()) + 1;
            }
            progress->done.store(chunks_done);
        }
    });
    return copy_err;
}

}  // namespace

// --- compaction --------- --------------------------------------------------

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

VaultResult Vault::compact(OpProgress* progress)
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
    ChunkStore src(fp_, master_key_.as_span(), framed_chunks(header_));
    ChunkStore dst(tmp, master_key_.as_span(), framed_chunks(header_));

    // Count total chunks to copy for progress reporting.
    int total_chunks = 0;
    if (progress) {
        count_compact_chunks(new_root, total_chunks);
        progress->total.store(total_chunks);
        progress->done.store(0);
    }

    VaultResult copy_err = copy_compact_chunks(new_root, src, dst, progress);
    // If cancelled, abort before the atomic rename (original is untouched).
    if (progress && progress->cancel.load()) return fail(Ok);
    if (copy_err != Ok) return fail(copy_err);

    // Fresh sealed index into slot A; slot B starts empty in the new file.
    // Saved searches carry over unchanged (vault-global metadata).
    std::vector<uint8_t> blob;
    serialize_index(new_root, saved_searches_, blob);
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

    // Atomic commit point: crash-safe 3-step rename sequence to enable secure
    // wipe of the original file (Task 7). At every instant, either the original
    // or .old file exists complete under a discoverable name.
    // Close our handle first — Windows refuses to replace a file that is open
    // (POSIX would happily swap the inode under us).
    std::fclose(fp_);
    fp_ = nullptr;
    const std::string old_path = path_ + ".old";

    // Step 1: Rename original aside (vault.osv -> vault.osv.old, fsync dir).
    // If this fails, the temp file is abandoned and the original remains in place.
    if (!fileutil::rename_file(path_, old_path)) {
        std::remove(tmp_path.c_str());
        // The original is untouched; reacquire our handle to it.
        fp_ = std::fopen(path_.c_str(), "r+b");
        if (!fp_) reset();  // intact on disk; force a clean re-open
        return IoError;
    }
    fileutil::sync_dir_of(old_path);

    // Step 2: Rename temp into place (vault.osv.compact -> vault.osv, fsync dir).
    // If this fails, vault.osv.old still exists with the original intact;
    // reacquire the original handle.
    if (!fileutil::rename_file(tmp_path, path_)) {
        // vault.osv.old has the original; vault.osv.compact is abandoned.
        std::remove(tmp_path.c_str());
        // Restore the original from .old (reverse step 1, best-effort).
        if (!fileutil::rename_file(old_path, path_))
            std::fprintf(stderr, "[Vault] compact recovery: could not restore %s from %s — vault remains at the .old path\n",
                         path_.c_str(), old_path.c_str());
        fp_ = std::fopen(path_.c_str(), "r+b");
        if (!fp_) reset();  // intact on disk; force a clean re-open
        return IoError;
    }
    fileutil::sync_dir_of(path_);

    // Step 3: Zero-overwrite and remove the old file (best-effort, non-fatal).
    // If the wipe fails, the old file is still removed; if the remove fails,
    // the old file stays on disk but is harmless (the vault has moved on).
    // NOTE: best-effort wipe. CoW filesystems (btrfs, APFS), SSD wear-leveling,
    // and snapshots may retain old blocks regardless.
    fileutil::wipe_and_remove(old_path);

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
    return index_io::write_header(fp_, header_);
}

VaultResult Vault::commit_index()
{
    IndexIoContext ctx{
        .fp_           = fp_,
        .header_       = header_,
        .master_key_   = master_key_,
        .root_         = root_,
        .saved_searches_ = saved_searches_,
    };
    return index_io::commit_index(ctx);
}

} // namespace vault
