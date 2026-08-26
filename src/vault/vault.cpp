#include "vault.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cstring>
#include <utility>

#include "crypto/aead.h"
#include "crypto/kdf.h"
#include "crypto/random.h"

#include "chunk_codec.h"
#include "chunk_store.h"
#include "compact_plan.h"
#include "file_util.h"
#include "safe_name.h"
#include "platform/path_utf8.h"
#include "platform/paths.h"

#include "image/anim_info.h"
#include "image/decode.h"
#include "image/thumbnail.h"

#include "media/video_probe.h"
#include "platform/error_log.h"
#include "ui/advanced_search_model.h"  // AdvancedQuery + evaluate (pure, SDL/vault-free)
#include "ui/gallery_sort.h"           // sort_children (pure, SDL/vault-free, Phase 37)
#include "vault/commit_lane.h"
#include "vault/index.h"
#include "vault/index_io.h"
#include "vault/staging.h"
#include "vault/vault_ops.h"
#include "vault/vault_search.h"
#include "vault/video_format.h"

namespace vault {

namespace {

// Wrappers around vault_ops functions for backward compatibility in this TU,
// avoiding the need to update every call site.
using vault_ops::child_named;
using vault_ops::for_each_media;
using vault_ops::resolve_gallery;
using vault_ops::split_path;

// Trim ASCII whitespace from start and end of a string_view.
std::string_view trim_ws(std::string_view s)
{
    size_t start = 0;
    while (start < s.size() &&
           (s[start] == ' ' || s[start] == '\t' || s[start] == '\n' || s[start] == '\r'))
        ++start;
    size_t end = s.size();
    while (end > start &&
           (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\n' || s[end - 1] == '\r'))
        --end;
    return s.substr(start, end - start);
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
            if (tag_ci_equal(existing, trimmed)) {
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
            if (tag_ci_equal(own, inh)) {
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

// Phase 73: tag → virtual "field:value" tags, built once per search from the
// vault-global settings. Match-only — never surfaced in effective_tags.
struct FieldTagMap {
    std::vector<std::pair<std::string, std::vector<std::string>>> by_tag;

    [[nodiscard]] bool empty() const noexcept { return by_tag.empty(); }

    static FieldTagMap build(const VaultSettings& s)
    {
        FieldTagMap m;
        for (const auto& fv : s.tag_field_values) {
            auto it = std::ranges::find_if(m.by_tag, [&fv](const auto& e) {
                return tag_ci_equal(e.first, fv.tag);
            });
            if (it == m.by_tag.end()) {
                m.by_tag.emplace_back(fv.tag, std::vector<std::string>{});
                it = std::prev(m.by_tag.end());
            }
            it->second.push_back(fv.field + ":" + fv.value);
        }
        return m;
    }

    // The tags a node should be MATCHED against: `tags` plus every virtual tag
    // of every carried tag. Returns `tags` untouched when nothing expands.
    [[nodiscard]] std::vector<std::string> expand(const std::vector<std::string>& tags) const
    {
        std::vector<std::string> out = tags;
        for (const auto& t : tags)
            for (const auto& [tag, values] : by_tag)
                if (tag_ci_equal(tag, t))
                    out.insert(out.end(), values.begin(), values.end());
        return out;
    }
};

// Walk the tree, accumulating ancestor-gallery tags as `inherited`, collecting
// in-scope nodes that match `query`. Gallery tags cascade to descendants here.
// Returns the union of tags from the subtree (including the node itself).
// For galleries, the match decision includes tags from descendants (Phase 51 roll-up).
std::vector<std::string> search_dfs(const IndexNode& node, std::string_view prefix,
                                    const std::vector<std::string>& inherited,
                                    std::string_view query, SearchScope scope,
                                    std::vector<SearchHit>& out,
                                    const FieldTagMap& fmap)
{
    std::vector<std::string> subtree_tags;

    for (const auto& child : node.children) {
        auto effective = compute_effective_tags(child.tags, inherited);
        const std::string full_path = join_child_path(prefix, child.name);

        // Recurse first to get the child's subtree tags.
        std::vector<std::string> child_subtree;
        if (child.is_gallery()) {
            child_subtree = search_dfs(child, full_path, effective, query, scope, out, fmap);
        }

        // For galleries, include descendant tags in the match decision (Phase 51).
        std::vector<std::string> match_tags = effective;
        if (child.is_gallery()) {
            match_tags = compute_effective_tags(child_subtree, effective);
        }

        if (node_in_scope(child, scope) &&
            node_matches(child.name, query,
                         fmap.empty() ? match_tags : fmap.expand(match_tags))) {
            out.push_back(SearchHit{
                .path = full_path,
                .is_gallery = child.is_gallery(),
                .name = child.name,
                .effective_tags = effective,
                .node = &child,
            });
        }

        // Accumulate this child's tags into the subtree union.
        // For galleries, include the subtree recursion result.
        std::vector<std::string> to_union = child.tags;
        if (child.is_gallery()) {
            to_union = compute_effective_tags(child_subtree, child.tags);
        }

        // Union into subtree_tags (case-insensitive).
        for (const auto& tag : to_union) {
            if (!std::ranges::any_of(subtree_tags,
                                     [&](const auto& t) { return tag_ci_equal(t, tag); })) {
                subtree_tags.push_back(tag);
            }
        }
    }

    return subtree_tags;
}

// Walk the tree collecting every favorited node of the requested kind (galleries
// when `want_galleries`, otherwise media: images or videos) into `out`, flat, with full paths.
// effective_tags is intentionally left empty — favorites lists don't cascade tags.
void collect_favorites(const IndexNode& node, std::string_view prefix, bool want_galleries,
                       std::vector<SearchHit>& out)
{
    for (const auto& child : node.children) {
        const std::string full_path = join_child_path(prefix, child.name);

        if (const bool matches = want_galleries ? child.is_gallery() : child.is_media();
            child.favorite && matches) {
            out.push_back(SearchHit{
                .path = full_path,
                .is_gallery = child.is_gallery(),
                .name = child.name,
                .effective_tags = {},
                .node = &child,
            });
        }

        if (child.is_gallery()) collect_favorites(child, full_path, want_galleries, out);
    }
}

// Walk the whole tree, accumulating distinct tags (case-insensitive, first-seen
// casing kept) into `out` (Phase 18 — feeds tag autocomplete).
void collect_tags(const IndexNode& node, std::vector<std::string>& out)
{
    for (const auto& t : node.tags) {
        if (!std::ranges::any_of(out, [&](const auto& x) { return tag_ci_equal(x, t); }))
            out.push_back(t);
    }
    for (const auto& c : node.children)
        collect_tags(c, out);
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
    auto it = std::ranges::find_if(tallies,
                                   [&](const ui::TagTally& tt) { return tag_ci_equal(tt.tag, tag); });
    if (it == tallies.end()) return;
    if (is_gallery)
        ++it->gallery_count;
    else
        ++it->image_count;
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
        if (std::ranges::any_of(child.tags, [&](const auto& t) { return tag_ci_equal(t, tag); }))
            out.push_back(SearchHit{
                .path = full_path,
                .is_gallery = true,
                .name = child.name,
                .effective_tags = {},
                .node = &child,
            });
        collect_galleries_with_tag(child, full_path, tag, out);
    }
}

// Collect every leaf media node (image or video) that DIRECTLY carries `tag`
// (case-insensitive exact match — not substring, not cascade), flat with full
// paths. effective_tags is left empty (this lookup never computes the cascade).
// Mirrors collect_galleries_with_tag but for !is_gallery() nodes (Phase 22 f/u).
void collect_images_with_tag(const IndexNode& node, std::string_view prefix, std::string_view tag,
                             std::vector<SearchHit>& out)
{
    for (const auto& child : node.children) {
        const std::string full_path = join_child_path(prefix, child.name);
        if (child.is_gallery()) {
            collect_images_with_tag(child, full_path, tag, out);
            continue;
        }
        if (std::ranges::any_of(child.tags, [&](const auto& t) { return tag_ci_equal(t, tag); }))
            out.push_back(SearchHit{
                .path = full_path,
                .is_gallery = false,
                .name = child.name,
                .effective_tags = {},
                .node = &child,
            });
    }
}

// Helper: accumulate a child's tags into the subtree union (case-insensitive).
void accumulate_subtree_union(std::vector<std::string>& subtree_tags,
                              const std::vector<std::string>& child_tags,
                              const std::vector<std::string>& child_subtree, bool is_child_gallery)
{
    // For galleries, include the subtree recursion result.
    const auto to_union = is_child_gallery ? compute_effective_tags(child_subtree, child_tags)
                                           : child_tags;

    for (const auto& tag : to_union) {
        if (!std::ranges::any_of(subtree_tags,
                                 [&](const auto& t) { return tag_ci_equal(t, tag); })) {
            subtree_tags.push_back(tag);
        }
    }
}

// Advanced-search DFS (Phase 18): evaluate `query` against every in-scope node,
// cascading gallery tags, collecting each match with its relevance score.
// Returns the union of tags from the subtree (including the node itself).
// For galleries, the match decision includes tags from descendants (Phase 51 roll-up).
std::vector<std::string> adv_search_dfs(const IndexNode& node, std::string_view prefix,
                                        const std::vector<std::string>& inherited,
                                        const ui::AdvancedQuery& query, ui::SearchScope scope,
                                        std::vector<std::pair<int, SearchHit>>& out,
                                        const FieldTagMap& fmap)
{
    using enum ui::SearchScope;
    std::vector<std::string> subtree_tags;

    for (const auto& child : node.children) {
        auto effective = compute_effective_tags(child.tags, inherited);
        const std::string full_path = join_child_path(prefix, child.name);

        // Recurse first to get the child's subtree tags.
        std::vector<std::string> child_subtree;
        if (child.is_gallery()) {
            child_subtree = adv_search_dfs(child, full_path, effective, query, scope, out, fmap);
        }

        if (const bool in_scope = child.is_gallery() ? (scope == Galleries || scope == Both)
                                                     : (scope == Images || scope == Both);
            in_scope) {
            // For galleries, include descendant tags in the match decision (Phase 51).
            std::vector<std::string> match_tags = effective;
            if (child.is_gallery()) {
                match_tags = compute_effective_tags(child_subtree, effective);
            }

            const ui::EvalResult r = ui::evaluate(
                query, child.name, fmap.empty() ? match_tags : fmap.expand(match_tags));
            if (r.matched) {
                out.emplace_back(r.score, SearchHit{
                                              .path = full_path,
                                              .is_gallery = child.is_gallery(),
                                              .name = child.name,
                                              .effective_tags = effective,
                                              .node = &child,
                                          });
            }
        }

        accumulate_subtree_union(subtree_tags, child.tags, child_subtree, child.is_gallery());
    }

    return subtree_tags;
}

}  // namespace

// --- lifecycle ------------------------------------------------------------

Vault::~Vault()
{
    reset();
}

Vault::Vault(Vault&& o) noexcept
    : path_(std::move(o.path_)), fp_(o.fp_), read_fp_(o.read_fp_), thumb_fp_(o.thumb_fp_),
      thumb_mutex_(std::move(o.thumb_mutex_)), write_mutex_(std::move(o.write_mutex_)),
      header_mutex_(std::move(o.header_mutex_)), header_(o.header_), unlocked_(o.unlocked_),
      master_key_(std::move(o.master_key_)), root_(std::move(o.root_)),
      saved_searches_(std::move(o.saved_searches_)), settings_(std::move(o.settings_))
{
    // Phase 50: A bound CommitLane holds a raw Vault* to &o. App holds the active
    // vault behind unique_ptr and never moves it while a session is live — this
    // assert turns a violation into a loud debug failure instead of a dangling pointer.
    assert(!o.commit_router_ && "cannot move a Vault with an active CommitLane bound");

    o.fp_ = nullptr;
    o.read_fp_ = nullptr;
    o.thumb_fp_ = nullptr;
    o.unlocked_ = false;
}

Vault& Vault::operator=(Vault&& o) noexcept
{
    if (this != &o) {
        reset();
        // Phase 50: A bound CommitLane holds a raw Vault* to &o. App holds the active
        // vault behind unique_ptr and never moves it while a session is live — this
        // assert turns a violation into a loud debug failure instead of a dangling pointer.
        assert(!o.commit_router_ && "cannot move a Vault with an active CommitLane bound");

        path_ = std::move(o.path_);
        fp_ = o.fp_;
        read_fp_ = o.read_fp_;
        thumb_fp_ = o.thumb_fp_;
        thumb_mutex_ = std::move(o.thumb_mutex_);
        write_mutex_ = std::move(o.write_mutex_);
        header_mutex_ = std::move(o.header_mutex_);
        header_ = o.header_;
        unlocked_ = o.unlocked_;
        master_key_ = std::move(o.master_key_);
        root_ = std::move(o.root_);
        saved_searches_ = std::move(o.saved_searches_);
        settings_ = std::move(o.settings_);
        o.fp_ = nullptr;
        o.read_fp_ = nullptr;
        o.thumb_fp_ = nullptr;
        o.unlocked_ = false;
    }
    return *this;
}

void Vault::lock() noexcept
{
    // Phase 50: If a CommitLane is bound, stop it (flush + join) before wiping the
    // master key. The lane seals with master_key_; stopping it here makes a
    // wipe-after-seal race impossible regardless of caller discipline.
    if (commit_router_) {
        commit_router_->stop();
        commit_router_ = nullptr;
    }

    master_key_.wipe();
    unlocked_ = false;
    root_ = IndexNode::gallery("");
    saved_searches_.clear();
}

void Vault::reset() noexcept
{
    // Phase 58: Close thumb_fp_ under the mutex BEFORE the master key wipe.
    // This ensures an in-flight thumbnail read either completes against valid state
    // or observes unlocked_ == false. The lock() call below wipes the master key.
    if (thumb_mutex_) {
        const std::lock_guard lk(*thumb_mutex_);
        unlocked_ = false;
        if (thumb_fp_ != nullptr) {
            std::fclose(thumb_fp_);
            thumb_fp_ = nullptr;
        }
    }

    lock();
    if (fp_) {
        std::fclose(fp_);
        fp_ = nullptr;
    }
    if (read_fp_) {
        std::fclose(read_fp_);
        read_fp_ = nullptr;
    }
    write_mutex_.reset();
    thumb_mutex_.reset();
    path_.clear();
    header_ = Header{};
    settings_ = VaultSettings{};
}

VaultResult Vault::create(const std::string& path, std::span<const uint8_t> password,
                          std::span<const uint8_t> keyfile, const crypto::KdfParams& params,
                          Vault& out)
{
    out.reset();

    // A vault is secret material like a keyfile: claim the file atomically
    // (never truncate an existing one) with owner-only permissions from the
    // first byte, instead of "w+b" landing 0644 under a default umask.
    std::FILE* fp = nullptr;
    const platform::OwnerOnlyCreate created =
        platform::create_owner_only_file(platform::utf8_to_path(path), fp);
    if (created == platform::OwnerOnlyCreate::AlreadyExists) return VaultResult::AlreadyExists;
    if (created != platform::OwnerOnlyCreate::Ok) return VaultResult::IoError;

    Header h;
    h.kdf = params;
    h.kdf_algo = 0;  // Argon2id
    h.keyfile_required = keyfile.empty() ? 0 : 1;
    h.flags |= FLAG_FRAMED_CHUNKS | FLAG_DOMAIN_SEPARATED_KDF;

    crypto::SecureBuffer<crypto::KEY_SIZE> master;
    crypto::SecureBuffer<crypto::KEY_SIZE> kek;
    if (!crypto::fill_random(h.salt) || !crypto::fill_random(master.span()) ||
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
    if (!crypto::seal(kek.as_span(), h.mk_nonce, master.as_span(), wrapped)) {
        std::fclose(fp);
        return VaultResult::CryptoError;
    }
    std::memcpy(h.wrapped_master_key.data(), wrapped.data(), crypto::KEY_SIZE);
    std::memcpy(h.mk_tag.data(), wrapped.data() + crypto::KEY_SIZE, crypto::TAG_SIZE);

    // Reserve the fixed header region so the data region begins at HEADER_SIZE.
    // The real header is written by commit_index() below.
    if (const std::array<uint8_t, HEADER_SIZE> placeholder{};
        std::fwrite(placeholder.data(), 1, placeholder.size(), fp) != placeholder.size()) {
        std::fclose(fp);
        return VaultResult::IoError;
    }

    out.path_ = path;
    out.fp_ = fp;
    out.header_ = h;
    out.master_key_ = std::move(master);
    out.root_ = IndexNode::gallery("");
    out.unlocked_ = true;
    out.settings_ = VaultSettings::seeded();
    // Stamp migration watermarks: fresh vaults already have 512px thumbs and current index format
    // (prevents false "upgrade available" offer at first unlock).
    out.settings_.migrated_index_version = MIGRATION_INDEX_VERSION;
    out.settings_.migrated_thumb_side = static_cast<uint16_t>(image::THUMB_MAX_SIDE);
    out.write_mutex_ = std::make_unique<std::mutex>();
    out.header_mutex_ = std::make_unique<std::mutex>();
    out.thumb_mutex_ = std::make_unique<std::mutex>();

    // Write the initial (empty) index + a valid header via the crash-safe path.
    if (const VaultResult r = out.commit_index(); r != VaultResult::Ok) {
        out.reset();
        return r;
    }

    // Open read_fp_ after the initial commit so it sees the complete file.
    out.read_fp_ = platform::open_existing_read(platform::utf8_to_path(path));
    if (!out.read_fp_) {
        out.reset();
        return VaultResult::IoError;
    }
    // Disable buffering on read_fp_ so it always reads fresh from disk,
    // avoiding buffering conflicts with fp_'s writes.
    std::setvbuf(out.read_fp_, nullptr, _IONBF, 0);

    // Open thumb_fp_ the same way for thread-safe background thumbnail reads.
    out.thumb_fp_ = platform::open_existing_read(platform::utf8_to_path(path));
    if (!out.thumb_fp_) {
        out.reset();
        return VaultResult::IoError;
    }
    std::setvbuf(out.thumb_fp_, nullptr, _IONBF, 0);
    return VaultResult::Ok;
}

VaultResult Vault::open(const std::string& path, Vault& out)
{
    out.reset();

    std::FILE* fp = platform::fopen_path(platform::utf8_to_path(path), "r+b");
    if (!fp) return VaultResult::IoError;

    std::array<uint8_t, HEADER_SIZE> raw{};
    if (!fileutil::seek_to(fp, 0) || std::fread(raw.data(), 1, raw.size(), fp) != raw.size()) {
        std::fclose(fp);
        return VaultResult::BadFormat;
    }

    Header h;
    if (!Header::parse(raw, h)) {
        std::fclose(fp);
        return VaultResult::BadFormat;
    }

    // Phase 88: tighten a pre-existing vault to owner-only (best-effort — a
    // vault on a share owned by someone else may not be tighten-able).
    platform::ensure_owner_only_file(platform::utf8_to_path(path));

    out.path_ = path;
    out.fp_ = fp;
    out.read_fp_ = platform::fopen_path(platform::utf8_to_path(path), "rb");
    if (!out.read_fp_) {
        std::fclose(fp);
        out.fp_ = nullptr;  // Null the pointer so reset() doesn't double-close
        return VaultResult::IoError;
    }
    // Disable buffering on read_fp_ so it always reads fresh from disk,
    // avoiding buffering conflicts with fp_'s writes.
    std::setvbuf(out.read_fp_, nullptr, _IONBF, 0);

    // Open thumb_fp_ the same way for thread-safe background thumbnail reads.
    out.thumb_fp_ = platform::fopen_path(platform::utf8_to_path(path), "rb");
    if (!out.thumb_fp_) {
        std::fclose(out.read_fp_);
        std::fclose(fp);
        // Null the pointers so reset() doesn't double-close
        out.read_fp_ = nullptr;
        out.fp_ = nullptr;
        return VaultResult::IoError;
    }
    std::setvbuf(out.thumb_fp_, nullptr, _IONBF, 0);

    out.header_ = h;
    out.unlocked_ = false;
    out.write_mutex_ = std::make_unique<std::mutex>();
    out.header_mutex_ = std::make_unique<std::mutex>();
    out.thumb_mutex_ = std::make_unique<std::mutex>();
    return VaultResult::Ok;
}

namespace {

// Attempt to load and deserialize the index from a vault slot.
[[nodiscard]] bool try_load_slot(std::FILE* fp, const Header& header,
                                 std::span<const uint8_t, crypto::KEY_SIZE> master_key,
                                 uint8_t slot_idx, IndexNode& root_out,
                                 std::vector<SavedSearch>& searches_out,
                                 VaultSettings& settings_out)
{
    const IndexSlot& s = header.slot[slot_idx];
    if (s.length == 0) {
        return false;
    }
    std::vector<uint8_t> on_disk;
    if (ChunkStore store(fp, master_key, framed_chunks(header));
        !store.read_raw(s.offset, s.length, on_disk)) {
        return false;
    }
    std::vector<uint8_t> blob;
    if (!crypto::open(master_key, s.nonce, on_disk, blob)) {
        return false;
    }
    if (framed_chunks(header)) {
        std::vector<uint8_t> plain;
        if (!chunk_codec::decode_frame(blob, plain)) {
            return false;
        }
        blob = std::move(plain);
    }
    IndexNode tmp;
    std::vector<SavedSearch> tmp_searches;
    VaultSettings tmp_settings;
    if (!deserialize_index(blob, tmp, tmp_searches, tmp_settings)) {
        return false;
    }
    root_out = std::move(tmp);
    searches_out = std::move(tmp_searches);
    settings_out = std::move(tmp_settings);
    return true;
}

}  // namespace

VaultResult Vault::unlock(std::span<const uint8_t> password, std::span<const uint8_t> keyfile)
{
    using enum VaultResult;
    if (fp_ == nullptr) {
        return IoError;
    }
    if (unlocked_) {
        return Ok;
    }

    crypto::SecureBuffer<crypto::KEY_SIZE> kek;
    if (const auto input_format = domain_separated_kdf(header_)
            ? crypto::KdfInputFormat::DomainSeparatedV2 : crypto::KdfInputFormat::LegacyConcat;
        !crypto::derive_key(password, keyfile, header_.salt, header_.kdf, kek, input_format)) {
        return CryptoError;
    }

    // Unwrap the master key straight into mlock'd memory.
    std::array<uint8_t, crypto::KEY_SIZE + crypto::TAG_SIZE> sealed{};
    std::memcpy(sealed.data(), header_.wrapped_master_key.data(), crypto::KEY_SIZE);
    std::memcpy(sealed.data() + crypto::KEY_SIZE, header_.mk_tag.data(), crypto::TAG_SIZE);
    if (!crypto::open_to(kek.as_span(), header_.mk_nonce, sealed, master_key_.span())) {
        master_key_.wipe();
        return AuthFailed;  // wrong password / keyfile / tampered wrap
    }

    // Load the index from the active slot, falling back to the other slot if the
    // active one is unreadable (crash during a swap left it truncated/corrupt).
    if (const uint8_t active = header_.active_slot == 0 ? 0 : 1;
        !try_load_slot(fp_, header_, master_key_.as_span(), active, root_, saved_searches_,
                       settings_)) {
        if (!try_load_slot(fp_, header_, master_key_.as_span(), active == 0 ? 1 : 0, root_,
                           saved_searches_, settings_)) {
            master_key_.wipe();
            return BadFormat;
        }
        // Never silent: the most recent commit was lost (interrupted or
        // corrupted), so recent changes may replay — leave a trace for
        // diagnosis (no key material, invariant #5).
        platform::log_error("Vault",
                            "active index slot unreadable — recovered from the previous slot");
    }

    unlocked_ = true;
    return Ok;
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
    if (const auto old_format = domain_separated_kdf(header_)
            ? crypto::KdfInputFormat::DomainSeparatedV2 : crypto::KdfInputFormat::LegacyConcat;
        !crypto::derive_key(old_password, old_keyfile, header_.salt, header_.kdf, kek,
                            old_format)) {
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
    h.flags |= FLAG_DOMAIN_SEPARATED_KDF;
    if (!crypto::fill_random(h.salt) || !crypto::fill_random(h.mk_nonce)) {
        return CryptoError;
    }
    if (!crypto::derive_key(new_password, new_keyfile, h.salt, h.kdf, kek)) {
        return CryptoError;
    }
    std::vector<uint8_t> wrapped;
    if (!crypto::seal(kek.as_span(), h.mk_nonce, master.as_span(), wrapped)) {
        return CryptoError;
    }
    std::memcpy(h.wrapped_master_key.data(), wrapped.data(), crypto::KEY_SIZE);
    std::memcpy(h.mk_tag.data(), wrapped.data() + crypto::KEY_SIZE, crypto::TAG_SIZE);
    h.keyfile_required = new_keyfile.empty() ? 0 : 1;

    header_ = h;
    if (!write_header()) return IoError;
    return Ok;
}

// --- structure ------------------------------------------------------------

IndexNode* Vault::find_gallery(std::string_view p)
{
    return vault_ops::resolve_gallery(&root_, p);
}
const IndexNode* Vault::find_gallery(std::string_view p) const
{
    return vault_ops::resolve_gallery(&root_, p);
}

IndexNode* Vault::resolve_node(std::string_view path)
{
    return vault_ops::resolve_node_impl(&root_, path);
}
const IndexNode* Vault::resolve_node(std::string_view path) const
{
    return vault_ops::resolve_node_impl(&root_, path);
}

VaultResult Vault::create_gallery(std::string_view gallery_path)
{
    using enum VaultResult;
    if (!unlocked_) return Locked;

    const auto segments = split_path(gallery_path);
    if (segments.empty()) return AlreadyExists;  // root always exists

    // '/' legitimately separates segments here, but each SEGMENT is still a node
    // name and must be safe on its own — "a/../b" or "a/..\\b" must not resolve.
    for (std::string_view seg : segments)
        if (!is_safe_node_name(seg)) return InvalidArg;

    IndexNode* cur = &root_;
    bool created = false;
    for (std::string_view seg : segments) {
        IndexNode* child = child_named(cur, seg);
        if (child) {
            if (!child->is_gallery()) return InvalidArg;  // name is an image
            cur = child;
        } else {
            cur->children.push_back(IndexNode::gallery(std::string(seg)));
            cur = &cur->children.back();
            created = true;
        }
    }

    if (!created) return AlreadyExists;
    return commit_index();
}

VaultResult Vault::add_image(std::string_view gallery_path, std::span<const uint8_t> file_data,
                             std::string_view filename)
{
    using enum VaultResult;
    if (!unlocked_) return Locked;
    if (!is_safe_node_name(filename)) return InvalidArg;  // no traversal into the index

    // Fail-fast pre-checks: do these before staging to avoid orphaning chunks
    // on the synchronous path.
    IndexNode* g = find_gallery(gallery_path);
    if (!g) return NotFound;
    if (child_named(g, filename)) return AlreadyExists;

    // Stage the image (encode, encrypt, append chunks).
    StagedNode staged = stage_image(*this, file_data, filename);
    if (staged.status != Ok) return staged.status;

    // Attach the staged node to the tree (no commit yet).
    if (const VaultResult r = attach_staged(*this, gallery_path, std::move(staged.node)); r != Ok)
        return r;

    // Synchronize the staged chunks to stable storage (fsync).
    if (ChunkStore store(fp_, master_key_.as_span(), framed_chunks(header_)); !store.sync())
        return IoError;

    return commit_index();
}

VaultResult Vault::read_image(const IndexNode& node, crypto::SecureBytes& out) const
{
    using enum VaultResult;
    if (!unlocked_) return Locked;
    if (!node.is_image()) return InvalidArg;

    if (ChunkStore store(read_fp_, master_key_.as_span(), framed_chunks(header_));
        !store.read_chunk({node.meta.data_offset, node.meta.data_length}, out)) {
        return AuthFailed;  // corrupt / tampered / unreadable chunk
    }
    return Ok;
}

VaultResult Vault::read_thumbnail(const IndexNode& node, crypto::SecureBytes& out) const
{
    using enum VaultResult;
    if (!node.is_media()) return InvalidArg;

    // Determine thumbnail location: video uses poster, image uses meta.
    const uint64_t thumb_len = node.is_video() ? node.vmeta.poster_length : node.meta.thumb_length;
    const uint64_t thumb_off = node.is_video() ? node.vmeta.poster_offset : node.meta.thumb_offset;
    if (thumb_len == 0) return NotFound;

    // Phase 58: Use dedicated thumb_fp_ + mutex for thread-safe background reads.
    if (!thumb_mutex_) return Locked;
    const std::lock_guard lk(*thumb_mutex_);
    if (!unlocked_) return Locked;
    if (ChunkStore store(thumb_fp_, master_key_.as_span(), framed_chunks(header_));
        !store.read_chunk({thumb_off, thumb_len}, out)) {
        return AuthFailed;
    }
    return Ok;
}

VaultResult read_thumb_span(const Vault& v, uint64_t offset, uint64_t length,
                            crypto::SecureBytes& out)
{
    using enum VaultResult;
    if (length == 0) return InvalidArg;

    // Phase 58: Use dedicated thumb_fp_ + mutex for thread-safe background reads.
    if (!v.thumb_mutex_) return Locked;
    const std::lock_guard lk(*v.thumb_mutex_);
    if (!v.unlocked_) return Locked;
    if (ChunkStore store(v.thumb_fp_, v.master_key_.as_span(), framed_chunks(v.header_));
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

VaultResult Vault::add_video(std::string_view gallery_path, std::span<const uint8_t> file_data,
                             std::string_view filename, uint32_t chunk_size)
{
    using enum VaultResult;
    if (!unlocked_) return Locked;
    if (!is_safe_node_name(filename)) return InvalidArg;  // no traversal into the index
    if (chunk_size == 0) return InvalidArg;

    // Fail-fast pre-checks: do these before staging to avoid orphaning chunks
    // on the synchronous path. Probe the video file first to detect metadata and
    // generate poster — ensures we don't create orphan chunks if the video is invalid.
    if (media::VideoProbeResult probe; !media::probe_video(file_data, probe)) return InvalidArg;

    IndexNode* g = find_gallery(gallery_path);
    if (!g) return NotFound;
    if (child_named(g, filename)) return AlreadyExists;

    // Stage the video (split into chunks, encrypt, append).
    StagedNode staged = stage_video(*this, file_data, filename, chunk_size);
    if (staged.status != Ok) return staged.status;

    // Attach the staged node to the tree (no commit yet).
    if (const VaultResult r = attach_staged(*this, gallery_path, std::move(staged.node)); r != Ok)
        return r;

    // Synchronize the staged chunks to stable storage (fsync).
    if (ChunkStore store(fp_, master_key_.as_span(), framed_chunks(header_)); !store.sync())
        return IoError;

    return commit_index();
}

VaultResult Vault::read_video(const IndexNode& node, crypto::SecureBytes& out) const
{
    using enum VaultResult;
    if (!unlocked_) return Locked;
    if (!node.is_video()) return InvalidArg;

    if (!out.resize(node.vmeta.orig_size)) return IoError;
    ChunkStore store(read_fp_, master_key_.as_span(), framed_chunks(header_));
    size_t pos = 0;
    for (const VideoChunk& c : node.vmeta.chunks) {
        crypto::SecureBytes piece;
        if (!store.read_chunk({c.offset, c.length}, piece)) {
            (void)out.resize(0);
            return AuthFailed;
        }
        if (pos + piece.size() > out.size()) {
            (void)out.resize(0);
            return AuthFailed;
        }
        std::copy(piece.data(), piece.data() + piece.size(), out.data() + pos);
        pos += piece.size();
    }
    if (pos != out.size()) {
        (void)out.resize(0);
        return AuthFailed;
    }
    return Ok;
}

VaultResult apply_video_probe(Vault& v, std::string_view node_path,
                              const VideoProbeApply& probe, bool sync)
{
    using enum VaultResult;
    if (!v.unlocked_) return Locked;

    IndexNode* n = v.resolve_node(node_path);
    if (!n || !n->is_video()) return NotFound;
    if (n->vmeta.codec != VideoCodec::Unknown) return Ok;   // already real metadata
    if (probe.codec == VideoCodec::Unknown) return Ok;      // nothing learned

    n->vmeta.codec       = probe.codec;
    n->vmeta.width       = probe.width;
    n->vmeta.height      = probe.height;
    n->vmeta.duration_us = probe.duration_us;

    if (n->vmeta.poster_length == 0 && !probe.poster_jpeg.empty()) {
        // Phase 50 protocol: every append to fp_ holds write_mutex_.
        std::lock_guard lk(*v.write_mutex_);
        ChunkStore store(v.fp_, v.master_key_.as_span(), framed_chunks(v.header_));
        ChunkSpan poster_span;
        if (!store.append_chunk(probe.poster_jpeg, poster_span)) return IoError;
        if (sync && !store.sync()) return IoError;
        n->vmeta.poster_offset = poster_span.offset;
        n->vmeta.poster_length = poster_span.length;
    }
    return Ok;
}

VaultResult apply_image_thumb(Vault& v, std::string_view node_path,
                              std::span<const uint8_t> thumb_jpeg, bool sync)
{
    using enum VaultResult;
    if (!v.unlocked_) return Locked;
    if (thumb_jpeg.empty()) return InvalidArg;
    IndexNode* n = v.resolve_node(node_path);
    if (!n || !n->is_image()) return NotFound;

    std::lock_guard lk(*v.write_mutex_);
    ChunkStore store(v.fp_, v.master_key_.as_span(), framed_chunks(v.header_));
    ChunkSpan span;
    if (!store.append_chunk(thumb_jpeg, span)) return IoError;
    if (sync && !store.sync()) return IoError;
    n->meta.thumb_offset = span.offset;
    n->meta.thumb_length = span.length;
    return Ok;
}

VaultResult apply_video_poster(Vault& v, std::string_view node_path,
                               std::span<const uint8_t> poster_jpeg, bool sync)
{
    using enum VaultResult;
    if (!v.unlocked_) return Locked;
    if (poster_jpeg.empty()) return InvalidArg;
    IndexNode* n = v.resolve_node(node_path);
    if (!n || !n->is_video()) return NotFound;

    std::lock_guard lk(*v.write_mutex_);
    ChunkStore store(v.fp_, v.master_key_.as_span(), framed_chunks(v.header_));
    ChunkSpan span;
    if (!store.append_chunk(poster_jpeg, span)) return IoError;
    if (sync && !store.sync()) return IoError;
    n->vmeta.poster_offset = span.offset;
    n->vmeta.poster_length = span.length;
    return Ok;
}

VaultResult apply_image_animated(Vault& v, std::string_view node_path, bool animated)
{
    using enum VaultResult;
    if (!v.unlocked_) return Locked;

    IndexNode* n = v.resolve_node(node_path);
    if (!n || !n->is_image()) return NotFound;
    if (!format_can_animate(n->meta.format)) return Ok;
    if (n->meta.animated == animated) return Ok;

    n->meta.animated = animated;
    return Ok;
}

VaultResult commit_migration(Vault& v, VaultSettings settings)
{
    using enum VaultResult;
    if (!v.unlocked_) return Locked;
    v.settings_ = std::move(settings);
    return v.commit_index();
}

VaultResult Vault::remove_image(std::string_view gallery_path, std::string_view filename)
{
    using enum VaultResult;
    if (!unlocked_) return Locked;

    IndexNode* g = find_gallery(gallery_path);
    if (!g) return NotFound;

    for (auto it = g->children.begin(); it != g->children.end(); ++it) {
        if (it->is_media() && it->name == filename) {  // remove image or video
            g->children.erase(it);                     // chunk(s) are orphaned until reclamation
            if (const VaultResult r = commit_index(); r != Ok) return r;

            // Best-effort space reclamation: the remove itself already
            // succeeded, so a failed reclaim only leaves waste behind.
            auto_reclaim_space();
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
    if (segments.empty()) return InvalidArg;  // the root cannot be removed

    // Walk to the parent of the target (all segments but the last).
    IndexNode* parent = &root_;
    for (size_t i = 0; i + 1 < segments.size(); ++i) {
        parent = child_named(parent, segments[i]);
        if (!parent || !parent->is_gallery()) return NotFound;
    }

    const std::string_view name = segments.back();
    for (auto it = parent->children.begin(); it != parent->children.end(); ++it) {
        if (it->is_gallery() && it->name == name) {
            parent->children.erase(it);  // whole subtree's chunks orphaned until reclamation
            if (const VaultResult r = commit_index(); r != Ok) return r;

            auto_reclaim_space();  // best-effort (same gate as remove_image)
            return Ok;
        }
    }
    return NotFound;
}

// Erase the media child named `leaf` from `g` (nullptr-safe). Returns whether
// a matching child was found; its chunks stay orphaned until reclamation.
static bool erase_media_child(IndexNode* g, std::string_view leaf)
{
    if (!g) return false;
    for (auto it = g->children.begin(); it != g->children.end(); ++it) {
        if (it->is_media() && it->name == leaf) {
            g->children.erase(it);
            return true;
        }
    }
    return false;
}

VaultResult remove_media_batch(Vault& v, std::span<const std::string> node_paths,
                               RemoveBatchStats* stats)
{
    using enum VaultResult;
    if (stats) *stats = {};
    if (!v.unlocked_) return Locked;

    std::size_t removed = 0;
    std::size_t missing = 0;
    for (const std::string& path : node_paths) {
        // Split "gal/sub/name" into parent gallery + leaf name.
        const auto   slash  = path.find_last_of('/');
        const auto   parent = (slash == std::string::npos)
                                  ? std::string_view{}
                                  : std::string_view(path).substr(0, slash);
        const auto   leaf   = (slash == std::string::npos)
                                  ? std::string_view(path)
                                  : std::string_view(path).substr(slash + 1);
        const bool hit = erase_media_child(v.find_gallery(parent), leaf);
        hit ? ++removed : ++missing;
    }

    if (removed > 0) {
        if (const VaultResult r = v.commit_index(); r != Ok) return r;
        v.auto_reclaim_space();  // best-effort, same gate as remove_image
    }
    if (stats) *stats = {.removed = removed, .missing = missing};
    return Ok;
}

// Erase the child named `leaf` from `g` regardless of kind (nullptr-safe).
// A gallery child takes its whole subtree with it; all covered chunks stay
// orphaned until reclamation.
static bool erase_any_child(IndexNode* g, std::string_view leaf)
{
    if (!g) return false;
    for (auto it = g->children.begin(); it != g->children.end(); ++it) {
        if (it->name == leaf) {
            g->children.erase(it);
            return true;
        }
    }
    return false;
}

VaultResult remove_nodes_batch(Vault& v, std::span<const std::string> node_paths,
                               RemoveBatchStats* stats)
{
    using enum VaultResult;
    if (stats) *stats = {};
    if (!v.unlocked_) return Locked;

    std::size_t removed = 0;
    std::size_t missing = 0;
    for (const std::string& path : node_paths) {
        if (path.empty()) { ++missing; continue; }   // the root is not deletable
        const auto   slash  = path.find_last_of('/');
        const auto   parent = (slash == std::string::npos)
                                  ? std::string_view{}
                                  : std::string_view(path).substr(0, slash);
        const auto   leaf   = (slash == std::string::npos)
                                  ? std::string_view(path)
                                  : std::string_view(path).substr(slash + 1);
        const bool hit = erase_any_child(v.find_gallery(parent), leaf);
        hit ? ++removed : ++missing;
    }

    if (removed > 0) {
        if (const VaultResult r = v.commit_index(); r != Ok) return r;
        v.auto_reclaim_space();  // best-effort, same gate as remove_image
    }
    if (stats) *stats = {.removed = removed, .missing = missing};
    return Ok;
}

VaultResult set_favorites_batch(Vault& v, std::span<const std::string> node_paths, bool value)
{
    using enum VaultResult;
    if (!v.unlocked_) return Locked;

    bool changed = false;
    for (const std::string& path : node_paths) {
        IndexNode* node = v.resolve_node(path);
        if (!node || node->favorite == value) continue;   // missing: skipped, not an error
        node->favorite = value;
        changed = true;
    }

    // One crash-safe index swap for the whole batch; none for a no-op batch.
    return changed ? v.commit_index() : Ok;
}

VaultResult add_tag_batch(Vault& v, std::span<const std::string> node_paths,
                          std::string_view tag)
{
    using enum VaultResult;
    if (!v.unlocked_) return Locked;
    const auto trimmed = normalise_tag(tag);
    if (trimmed.empty()) return InvalidArg;

    bool changed = false;
    for (const std::string& path : node_paths) {
        if (IndexNode* node = v.resolve_node(path); node) {
            const bool dup = std::ranges::any_of(node->tags,
                [&trimmed](const std::string& e) { return tag_ci_equal(e, trimmed); });
            if (!dup) {
                node->tags.emplace_back(trimmed);
                changed = true;
            }
        }
        // missing: skipped, not an error
    }
    // One crash-safe index swap for the whole batch; none for a no-op batch.
    return changed ? v.commit_index() : Ok;
}

VaultResult remove_tag_batch(Vault& v, std::span<const std::string> node_paths,
                             std::string_view tag)
{
    using enum VaultResult;
    if (!v.unlocked_) return Locked;
    const auto trimmed = normalise_tag(tag);
    if (trimmed.empty()) return Ok;   // idempotent, like remove_tag

    bool changed = false;
    for (const std::string& path : node_paths) {
        IndexNode* node = v.resolve_node(path);
        if (!node) continue;
        const auto removed = std::erase_if(node->tags,
            [&trimmed](const std::string& e) { return tag_ci_equal(e, trimmed); });
        changed = changed || removed > 0;
    }
    return changed ? v.commit_index() : Ok;
}

std::vector<const IndexNode*> Vault::list(std::string_view gallery_path) const
{
    std::vector<const IndexNode*> out;
    const IndexNode* g = find_gallery(gallery_path);
    if (!g) return out;
    out.reserve(g->children.size());
    for (const auto& c : g->children)
        out.push_back(&c);
    return ui::sort_children(out, ui::effective_sort_key(g->sort_key, settings_.default_sort));
}

SortKey gallery_sort_key(const Vault& v, std::string_view gallery_path)
{
    const IndexNode* g = v.find_gallery(gallery_path);
    return g ? g->sort_key : SortKey::Default;
}

// Phase 78: check if a gallery path exists (for DualGalleryScreen walk-up on vault changes).
bool gallery_exists(const Vault& v, std::string_view gallery_path)
{
    if (!v.is_unlocked()) return false;
    return v.find_gallery(gallery_path) != nullptr;
}

VaultResult set_gallery_sort(Vault& v, std::string_view gallery_path, SortKey key)
{
    using enum VaultResult;
    if (!v.unlocked_) return Locked;

    IndexNode* g = v.find_gallery(gallery_path);
    if (!g) return NotFound;

    if (g->sort_key == key) return Ok;

    g->sort_key = key;
    return v.commit_index();
}

const VaultSettings& vault_settings(const Vault& v) noexcept
{
    return v.settings_;
}

VaultResult set_vault_settings(Vault& v, VaultSettings s)
{
    using enum VaultResult;
    if (!v.unlocked_) return Locked;
    v.settings_ = std::move(s);
    return v.commit_index();
}

VaultResult rename_node(Vault& v, std::string_view gallery_path, std::string_view old_name,
                        std::string_view new_name)
{
    using enum VaultResult;
    if (!v.unlocked_) return Locked;
    if (!is_safe_node_name(new_name)) return InvalidArg;

    IndexNode* g = v.find_gallery(gallery_path);
    if (!g) return NotFound;

    IndexNode* node = child_named(g, old_name);
    if (!node) return NotFound;

    if (new_name == old_name) return Ok;  // no-op

    for (const auto& c : g->children)
        if (&c != node && c.name == new_name) return AlreadyExists;

    node->name = std::string(new_name);
    return v.commit_index();
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
        if (tag_ci_equal(existing, trimmed)) return Ok;
    }

    // Not found, add it.
    node->tags.emplace_back(trimmed);
    return commit_index();
}

namespace {

// Depth-first tag prune over the index tree; counts into `stats`.
void prune_node_tags(IndexNode& node, const std::function<bool(std::string_view)>& keep,
                     PruneTagsStats& stats)
{
    if (const auto removed = std::erase_if(node.tags,
                                           [&keep](const std::string& t) { return !keep(t); });
        removed > 0) {
        stats.tags_removed += removed;
        ++stats.nodes_touched;
    }
    for (IndexNode& c : node.children) prune_node_tags(c, keep, stats);
}

}  // namespace

VaultResult Vault::prune_tags(const std::function<bool(std::string_view)>& keep,
                              PruneTagsStats* stats)
{
    using enum VaultResult;
    if (stats) *stats = {};
    if (!unlocked_) return Locked;
    if (!keep) return InvalidArg;

    PruneTagsStats local;
    prune_node_tags(root_, keep, local);
    if (stats) *stats = local;

    if (local.tags_removed == 0) return Ok;   // nothing changed — no commit
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
        if (tag_ci_equal(*it, trimmed)) {
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
    // unnamed root itself is never a hit. The returned tag union bubbles up from
    // the root's children but is unused here (Phase 51 roll-up).
    const FieldTagMap fmap = FieldTagMap::build(settings_);
    [[maybe_unused]] auto _ = search_dfs(root_, "", root_.tags, query, scope, out, fmap);
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
    const FieldTagMap fmap = FieldTagMap::build(v_.settings_);
    [[maybe_unused]] auto _ =
        adv_search_dfs(v_.root_, "", v_.root_.tags, query, query.scope, scored, fmap);

    // Rank by descending score, breaking ties by ascending path for stability.
    std::ranges::sort(scored, [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second.path < b.second.path;
    });

    out.reserve(scored.size());
    for (auto& [score, hit] : scored)
        out.push_back(std::move(hit));
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
    for (auto& t : vocab)
        out.push_back(ui::TagTally{.tag = std::move(t)});

    count_direct_tags(v_.root_, out);

    // Phase 51: attach each tag's vault-global description (empty when unset).
    const VaultSettings& settings = vault_settings(v_);
    for (auto& row : out)
        row.description = std::string(find_tag_description(settings, row.tag));

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
    if (!v_.unlocked_) return Locked;
    if (name.empty()) return InvalidArg;

    std::vector<uint8_t> blob = ui::serialize_query(query);

    // Upsert: replace an existing same-name entry, else append (bounded).
    for (auto& s : v_.saved_searches_) {
        if (s.name == name) {
            s.query = std::move(blob);
            return v_.commit_index();
        }
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

// Free friend implementations
VaultResult toggle_favorite_node(Vault& v, std::string_view node_path)
{
    using enum VaultResult;
    if (!v.unlocked_) return Locked;

    IndexNode* node = v.resolve_node(node_path);
    if (!node) return NotFound;

    node->favorite = !node->favorite;
    return v.commit_index();
}

std::vector<SearchHit> list_favorite_images(const Vault& v)
{
    std::vector<SearchHit> out;
    if (!v.unlocked_) return out;
    collect_favorites(v.root_, "", /*want_galleries=*/false, out);
    return out;
}

std::vector<SearchHit> list_favorite_galleries(const Vault& v)
{
    std::vector<SearchHit> out;
    if (!v.unlocked_) return out;
    collect_favorites(v.root_, "", /*want_galleries=*/true, out);
    return out;
}

// --- compaction helpers ---

namespace {

// Append a media node's on-disk chunk spans (offset, length) to `live`: an
// image's data + optional thumb, or a video's chunks + optional poster. Used by
// reclaim() to build the set of spans that must NOT be punched.
void collect_media_spans(const IndexNode& n, std::vector<std::pair<uint64_t, uint64_t>>& live)
{
    if (n.is_image()) {
        if (n.meta.data_length > 0) {
            live.emplace_back(n.meta.data_offset, n.meta.data_length);
        }
        if (n.meta.thumb_length > 0) {
            live.emplace_back(n.meta.thumb_offset, n.meta.thumb_length);
        }
    } else if (n.is_video()) {
        for (const VideoChunk& c : n.vmeta.chunks) {
            if (c.length > 0) {
                live.emplace_back(c.offset, c.length);
            }
        }
        if (n.vmeta.poster_length > 0) {
            live.emplace_back(n.vmeta.poster_offset, n.vmeta.poster_length);
        }
    }
}

// Gather every movable on-disk span from `root` (image data + thumb, video
// chunks + poster) as planner units, with a parallel back-reference to the
// node field so an executed move can update the tree copy. unit.id is the
// index into both vectors.
void collect_units(IndexNode& root, std::vector<compact_plan::Unit>& units,
                   std::vector<uint64_t*>& offset_fields)
{
    for_each_media(root, [&units, &offset_fields](IndexNode& n) {
        auto add = [&units, &offset_fields](uint64_t& offset, uint64_t length) {
            if (length == 0) return;
            units.emplace_back(offset, length, static_cast<uint32_t>(units.size()));
            offset_fields.push_back(&offset);
        };
        if (n.is_image()) {
            add(n.meta.data_offset, n.meta.data_length);
            add(n.meta.thumb_offset, n.meta.thumb_length);
        } else if (n.is_video()) {
            for (VideoChunk& c : n.vmeta.chunks)
                add(c.offset, c.length);
            add(n.vmeta.poster_offset, n.vmeta.poster_length);
        }
    });
}

// Stream one unit's bytes to its destination through a bounded buffer. The
// destination is dead space (disjoint from every live span including the
// source), so slice order is irrelevant and no whole-unit RAM copy is needed.
// Ciphertext is moved verbatim — no decrypt, invariant #1 untouched.
bool stream_move(ChunkStore& store, uint64_t src, uint64_t dest, uint64_t length)
{
    constexpr uint64_t SLICE = 1u << 20;  // 1 MiB
    std::vector<uint8_t> buf;
    for (uint64_t i = 0; i < length; i += SLICE) {
        if (const uint64_t n = std::min(SLICE, length - i); !store.read_raw(src + i, n, buf))
            return false;
        if (!store.write_raw_at(dest + i, buf)) return false;
    }
    return true;
}

// State bundle for move execution: reduces parameter count for S107 compliance.
struct MoveExecState {
    std::vector<compact_plan::Unit>& units;
    std::vector<uint64_t*>& offset_fields;
    ChunkStore& store;
    IndexIoContext& ctx;
    OpProgress* progress;
    uint64_t planned_mib;  // const by value (S995: never written)
    uint64_t& moved_bytes;
    uint64_t& moved_since_commit;
    bool& cancelled;
};

// Execute all moves in a compaction pass: stream each unit to its destination,
// update in-memory offset references, track progress, and commit batches when
// BATCH_BYTES is reached. Sets cancelled if progress.cancel is signalled.
VaultResult execute_pass_moves(const std::vector<compact_plan::Move>& moves, MoveExecState& state,
                               uint64_t batch_bytes)
{
    using enum VaultResult;
    for (const compact_plan::Move& m : moves) {
        if (state.progress && state.progress->cancel.load()) {
            state.cancelled = true;
            break;
        }
        compact_plan::Unit& u = state.units[m.unit_id];
        if (!stream_move(state.store, u.offset, m.dest, u.length)) return IoError;
        u.offset = m.dest;
        *state.offset_fields[m.unit_id] = m.dest;
        state.moved_since_commit += u.length;
        state.moved_bytes += u.length;
        if (state.progress) {
            state.progress->done.store(static_cast<int>(
                std::min<uint64_t>(state.planned_mib, (state.moved_bytes >> 20) + 1)));
        }
        if (state.moved_since_commit >= batch_bytes) {
            if (index_io::commit_index(state.ctx) != Ok) return IoError;
            state.moved_since_commit = 0;
        }
    }
    return Ok;
}

}  // namespace

// --- compaction --------- --------------------------------------------------

uint64_t vault_wasted_bytes(const Vault& v)
{
    if (!v.unlocked_ || !v.fp_) return 0;

    uint64_t size = 0;
    if (!fileutil::file_size(v.fp_, size)) return 0;

    // Phase 50: guard access to header_.slot and header_.active_slot, which can
    // race with the commit lane thread's index commits.
    uint64_t live = 0;
    {
        std::lock_guard lk(*v.header_mutex_);
        live = HEADER_SIZE + v.header_.slot[v.header_.active_slot].length;
    }
    for_each_media(v.root_, [&live](const IndexNode& node) {
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
    if (!unlocked_ || !fp_) return Locked;
    // Early cancel is a true no-op: not even an index commit (a UI cancel that
    // races the job start must leave the file byte-identical).
    if (progress && progress->cancel.load()) return Ok;

    // Quiesce the commit lane (it appends blobs + flips slots from another
    // thread), then own the write path for the whole operation — mirrors
    // reclaim()'s contract.
    if (commit_router_ && commit_router_->running() && !commit_router_->flush()) {
        return IoError;
    }
    std::lock_guard wlk(*write_mutex_);

    // Work on a COPY of the tree (same pattern the old compact used): the live
    // root_ keeps pre-move offsets for any in-flight background thumb read;
    // moves only write into dead space, so those reads stay valid all the way
    // until the final publish below.
    IndexNode new_root = root_;
    std::vector<compact_plan::Unit> units;
    std::vector<uint64_t*> offset_fields;
    collect_units(new_root, units, offset_fields);

    ChunkStore store(fp_, master_key_.as_span(), framed_chunks(header_));
    IndexIoContext ctx{
        fp_, header_, master_key_, new_root, saved_searches_, settings_, header_mutex_.get()};

    constexpr uint64_t BATCH_BYTES = 256ULL << 20;  // commit cadence: ~256 MiB
    uint64_t moved_since_commit = 0;
    uint64_t planned_mib = 0;
    uint64_t moved_bytes = 0;
    bool cancelled = false;

    // Pack until a pass plans nothing. Each pass's plan is computed against
    // the layout the last-committed index describes (units + the active blob),
    // so every destination is crash-safe dead space; the commit between
    // passes is what legalises space vacated by the previous one.
    while (!cancelled) {
        compact_plan::Unit pinned_blob{};
        {
            std::lock_guard hlk(*header_mutex_);
            const IndexSlot& s = header_.slot[header_.active_slot];
            pinned_blob = {s.offset, s.length, 0};
        }
        const std::span<const compact_plan::Unit> pinned(&pinned_blob, pinned_blob.length ? 1 : 0);
        const auto moves = compact_plan::plan_pass(units, HEADER_SIZE, pinned);
        if (moves.empty()) break;

        for (const auto& m : moves)
            planned_mib += (units[m.unit_id].length >> 20) + 1;
        if (progress) progress->total.store(static_cast<int>(planned_mib));

        if (MoveExecState state{units, offset_fields, store, ctx, progress, planned_mib,
                                moved_bytes, moved_since_commit, cancelled};
            execute_pass_moves(moves, state, BATCH_BYTES) != Ok) {
            return IoError;
        }
        // Pass boundary commit: legalises this pass's vacated space for the
        // next pass's plan. Skipped only if the pass committed on its very
        // last move.
        if (moved_since_commit > 0) {
            if (index_io::commit_index(ctx) != Ok) return IoError;
            moved_since_commit = 0;
        }
    }

    // Idempotent short-circuit: nothing moved AND the file is already tight
    // (active blob sits directly at/after the packed data with nothing beyond
    // it) AND there's no old inactive blob in the way -> true no-op. Without
    // this, re-compacting a tight vault would place a fresh blob after the
    // active one and leave one blob of slack forever.
    if (moved_bytes == 0) {
        uint64_t fsize = 0;
        std::lock_guard hlk(*header_mutex_);
        const IndexSlot& active = header_.slot[header_.active_slot];
        const IndexSlot& inactive = header_.slot[1 - header_.active_slot];
        if (fileutil::file_size(fp_, fsize) && active.length > 0 &&
            active.offset >= compact_plan::live_end(units, HEADER_SIZE) &&
            fsize == active.offset + active.length &&
            (inactive.length == 0 || inactive.offset >= active.offset + active.length)) {
            if (progress) {
                progress->total.store(1);
                progress->done.store(1);
            }
            return Ok;
        }
    }

    // Final placed commit: the blob goes right after the packed data so it
    // does not pin the dead tail. If that spot would overlap the currently
    // active blob (an already-tight vault), fall back to just after it —
    // the cost is one blob of slack, reclaimed by the next compact.
    std::vector<uint8_t> plain;
    if (!index_io::serialize_plain_index(ctx, plain)) return CryptoError;
    const uint64_t sealed_len = plain.size() + crypto::TAG_SIZE;
    uint64_t dest = compact_plan::live_end(units, HEADER_SIZE);
    {
        std::lock_guard hlk(*header_mutex_);
        const IndexSlot& s = header_.slot[header_.active_slot];
        if (s.length > 0 && dest < s.offset + s.length && s.offset < dest + sealed_len) {
            dest = s.offset + s.length;
        }
    }
    if (index_io::commit_plain_blob_at(ctx, plain, dest) != Ok) return IoError;
    if (!fileutil::truncate_file(fp_, dest + sealed_len)) return IoError;

    // Residual holes cost no physical disk where hole-punch exists (Linux;
    // a silent no-op elsewhere). Punch the gaps between live spans, exactly
    // like reclaim() but against the packed layout.
    {
        std::vector<std::pair<uint64_t, uint64_t>> live;
        for (const auto& u : units)
            live.emplace_back(u.offset, u.length);
        live.emplace_back(dest, sealed_len);
        std::ranges::sort(live);
        uint64_t cursor = HEADER_SIZE;
        for (const auto& [off, len] : live) {
            if (off > cursor) (void)fileutil::punch_hole(fp_, cursor, off - cursor);
            cursor = std::max(cursor, off + len);
        }
    }

    if (progress) {
        progress->total.store(static_cast<int>(planned_mib));
        progress->done.store(static_cast<int>(planned_mib));
    }
    root_ = std::move(new_root);  // publish moved offsets (job-worker hand-off,
                                  // modal blocks tree readers — same as before)
    return Ok;
}

VaultResult vault_reclaim(Vault& v)
{
    using enum VaultResult;
    if (!v.unlocked_ || !v.fp_) return Locked;

    // Quiesce the commit lane first: it appends index blobs and flips
    // active_slot from another thread, and punching a region it is about to make
    // live would be catastrophic. flush() drains all pending + in-flight commits.
    if (v.commit_router_ && v.commit_router_->running() && !v.commit_router_->flush()) {
        return IoError;
    }

    // Hold write_mutex_ across the whole scan+punch so a worker append cannot
    // interleave; the lane's append path takes the same mutex.
    std::lock_guard wlk(*v.write_mutex_);

    uint64_t fsize = 0;
    if (!fileutil::file_size(v.fp_, fsize)) return IoError;

    // Collect every LIVE span in the data region: the active index blob plus each
    // media chunk. The header [0, HEADER_SIZE) is live by construction (the scan
    // starts at HEADER_SIZE); the INACTIVE index slot is dead and thus reclaimed.
    std::vector<std::pair<uint64_t, uint64_t>> live;  // (offset, on-disk length)
    {
        std::lock_guard hlk(*v.header_mutex_);
        if (const IndexSlot& s = v.header_.slot[v.header_.active_slot]; s.length > 0) {
            live.emplace_back(s.offset, s.length);
        }
    }
    for_each_media(v.root_, [&live](const IndexNode& n) { collect_media_spans(n, live); });
    std::ranges::sort(live);

    // Punch every gap between consecutive live spans. `cursor` is the first byte
    // not yet known to be live; a span starting past it exposes a dead gap. Spans
    // that overlap or nest are absorbed by the max() so no live byte is punched.
    uint64_t cursor = HEADER_SIZE;
    for (const auto& [off, len] : live) {
        if (off > cursor) {
            (void)fileutil::punch_hole(v.fp_, cursor, off - cursor);
        }
        cursor = std::max(cursor, off + len);
    }
    if (cursor < fsize) {
        (void)fileutil::punch_hole(v.fp_, cursor, fsize - cursor);
    }
    return Ok;
}

void Vault::auto_reclaim_space()
{
    // Gate: only worth reclaiming when the waste is both absolutely large and a
    // meaningful fraction of the file (rewriting to save a few KiB costs more I/O
    // than it saves). Same thresholds the two delete paths shared before.
    uint64_t size = 0;
    if (const uint64_t waste = vault_wasted_bytes(*this); waste < AUTO_COMPACT_MIN_WASTE ||
                                                        !fileutil::file_size(fp_, size) ||
                                                        waste * AUTO_COMPACT_WASTE_RATIO < size) {
        return;
    }
#if defined(__linux__)
    // In-place hole punching: reclaims the disk blocks without the transient
    // second copy compact() writes, so a delete never briefly doubles disk use.
    (void)vault_reclaim(*this);
#else
    // No portable hole-punch: fall back to in-place compact (no disk spike).
    (void)compact();
#endif
}

// --- persistence ----------------------------------------------------------

bool Vault::write_header()
{
    std::lock_guard lk(*write_mutex_);
    return index_io::write_header(fp_, header_);
}

VaultResult Vault::commit_index()
{
    // Phase 50: if a commit router (CommitLane) is active and running, route through
    // it instead of committing synchronously. The router runs serialize on this thread
    // and enqueues the blob for asynchronous durability under the write mutex.
    if (commit_router_ && commit_router_->running()) {
        return commit_router_->enqueue_snapshot() ? VaultResult::Ok : VaultResult::IoError;
    }

    // Synchronous commit path (default, pre-Phase-50 behavior).
    std::lock_guard lk(*write_mutex_);
    IndexIoContext ctx{
        .fp_ = fp_,
        .header_ = header_,
        .master_key_ = master_key_,
        .root_ = root_,
        .saved_searches_ = saved_searches_,
        .settings_ = settings_,
        .header_mutex_ = header_mutex_.get(),
    };
    return index_io::commit_index(ctx);
}

}  // namespace vault
