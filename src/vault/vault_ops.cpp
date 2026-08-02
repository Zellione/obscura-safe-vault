#include "vault_ops.h"

#include <algorithm>
#include <new>
#include <ranges>
#include <vector>

namespace vault::vault_ops {

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

IndexNode* resolve_gallery(IndexNode* root, std::string_view path)
{
    IndexNode* cur = root;
    for (std::string_view seg : split_path(path)) {
        IndexNode* next = nullptr;
        for (auto& child : cur->children) {
            if (child.is_gallery() && child.name == seg) { next = &child; break; }
        }
        if (!next) return nullptr;
        cur = next;
    }
    return cur;
}

const IndexNode* resolve_gallery(const IndexNode* root, std::string_view path)
{
    const IndexNode* cur = root;
    for (std::string_view seg : split_path(path)) {
        const IndexNode* next = nullptr;
        for (const auto& child : cur->children) {
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


bool push_child(std::vector<IndexNode>& children, IndexNode node) noexcept
{
    try {
        if (int& c = push_child_fail_after(); c >= 0) {
            if (c == 0) {
                c = -1;
                throw std::bad_alloc();   // deterministic injected failure
            }
            --c;
        }
        children.push_back(std::move(node));
    } catch (...) {
        return false;
    }
    return true;
}

} // namespace vault::vault_ops
