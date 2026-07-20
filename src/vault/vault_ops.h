#pragma once

// Vault tree operations: path resolution, tree navigation, and structural
// helpers for mutations. Internal component of Vault. (Phase 25)

#include <string_view>
#include <vector>

#include "chunk_store.h"
#include "index.h"
#include "vault.h"  // VaultResult

// VaultOps: owns the logic for tree traversal, path resolution, and structural
// validation of the in-memory index tree.
namespace vault::vault_ops {

// Split a slash-separated path into non-empty segments. Leading/trailing/
// repeated slashes are ignored, so "", "/", "a/", "/a/b" all normalise cleanly.
[[nodiscard]] std::vector<std::string_view> split_path(std::string_view p);

// Walk the gallery tree to the node named by `path`. Returns nullptr if any
// segment is missing, is an image rather than a gallery, or path doesn't
// resolve. Empty path resolves to the root.
[[nodiscard]] IndexNode*       resolve_gallery(IndexNode* root, std::string_view path);
[[nodiscard]] const IndexNode* resolve_gallery(const IndexNode* root, std::string_view path);

// Resolve a path to any node (gallery or image). The final segment may be either.
// Intermediate segments must be galleries. Empty path resolves to the root.
// Returns nullptr if any segment is missing.
template <typename NodeT>
[[nodiscard]] NodeT* resolve_node_impl(NodeT* root, std::string_view path);

// Find an immediate child of a node by name. Returns nullptr if not found.
[[nodiscard]] IndexNode* child_named(IndexNode* node, std::string_view name);

// Predicates: does a gallery node hold any media children (images or videos)?
// Does it hold any sub-galleries? Informational since Phase 46 (galleries may
// hold media and sub-galleries at once) — no longer gates insertion.
[[nodiscard]] bool holds_media(const IndexNode& g);
[[nodiscard]] bool holds_galleries(const IndexNode& g);

// Visit every media node (image or video) in the tree rooted at `n`.
// Templated for const and non-const traversal.
template <typename NodeT, typename Fn>
void for_each_media(NodeT& n, Fn&& fn);

// Copy a media node's live chunk(s) from `src` to `dst` verbatim (ciphertext —
// no decrypt/re-encrypt), rewriting each offset to its new location.
// Sets `err` to IoError on the first failed read/append. Used by compaction.
void relocate_node_chunks(const ChunkStore& src, ChunkStore& dst, IndexNode& node, VaultResult& err);

// Append `node` to `children`, catching an allocation-failure exception
// (bad_alloc/length_error) from vector growth instead of letting it escape
// uncaught — an uncaught exception here would call std::terminate() and kill
// the whole process (the same bug class as chunk_codec::resize_buf; see its
// comment). Returns false on failure (real or injected); `node` is left
// unspecified-but-valid in that case and should be discarded by the caller.
[[nodiscard]] bool push_child(std::vector<IndexNode>& children, IndexNode node) noexcept;

// --- fault injection (allocation-failure tests) -----------------------------
// Mirrors chunk_codec::resize_fail_after: arms the Nth upcoming push_child
// call (0 = the very next one) to fail deterministically, since there is no
// portable way to make a real allocation fail on demand.
inline int& push_child_fail_after() noexcept
{
    static int n = -1;
    return n;
}

inline void inject_push_child_failure(int after_calls) noexcept { push_child_fail_after() = after_calls; }
inline void clear_push_child_failure() noexcept                 { push_child_fail_after() = -1; }

// Template implementations (must be in header for link-time visibility).

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

template <typename NodeT, typename Fn>
void for_each_media(NodeT& n, Fn&& fn)
{
    if (n.is_media()) {
        fn(n);
        return;
    }
    for (auto& c : n.children) for_each_media(c, fn);
}

} // namespace vault::vault_ops
