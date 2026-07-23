#include "ui/gallery_sort.h"

#include <algorithm>
#include <functional>

#include "ui/natural_sort.h"

namespace ui {

namespace {

uint64_t created_ts_of(const vault::IndexNode& n)
{
    if (n.is_image()) return n.meta.created_ts;
    if (n.is_video()) return n.vmeta.created_ts;
    return 0;   // Gallery children: no created_ts field, sorts stably
}

uint64_t orig_size_of(const vault::IndexNode& n)
{
    if (n.is_image()) return n.meta.orig_size;
    if (n.is_video()) return n.vmeta.orig_size;
    return 0;
}

} // namespace

std::vector<const vault::IndexNode*> sort_children(std::span<const vault::IndexNode*> nodes,
                                                    vault::SortKey key)
{
    std::vector<const vault::IndexNode*> out(nodes.begin(), nodes.end());

    // Folders-first grouping, regardless of key.
    std::ranges::stable_partition(out, [](const vault::IndexNode* n) { return n->is_gallery(); });

    // Default never reaches here (callers resolve it via effective_sort_key),
    // and Insertion is a genuine no-op: keep the folders-first partition only.
    if (key == vault::SortKey::Default || key == vault::SortKey::Insertion) {
        return out;
    }

    using Node = const vault::IndexNode*;
    std::function<bool(Node, Node)> less;
    switch (key) {
    case vault::SortKey::NameAsc:
        less = [](Node a, Node b) { return natural_less(a->name, b->name); };
        break;
    case vault::SortKey::NameDesc:
        less = [](Node a, Node b) { return natural_less(b->name, a->name); };
        break;
    case vault::SortKey::DateAsc:
        less = [](Node a, Node b) { return created_ts_of(*a) < created_ts_of(*b); };
        break;
    case vault::SortKey::DateDesc:
        less = [](Node a, Node b) { return created_ts_of(*a) > created_ts_of(*b); };
        break;
    case vault::SortKey::SizeAsc:
        less = [](Node a, Node b) { return orig_size_of(*a) < orig_size_of(*b); };
        break;
    case vault::SortKey::SizeDesc:
        less = [](Node a, Node b) { return orig_size_of(*a) > orig_size_of(*b); };
        break;
    case vault::SortKey::Default:
    case vault::SortKey::Insertion:
        return out;   // unreachable (handled above); kept for an exhaustive switch
    }

    const auto galleries_end = std::ranges::partition_point(
        out, [](Node n) { return n->is_gallery(); });
    std::stable_sort(out.begin(), galleries_end, less);
    std::stable_sort(galleries_end, out.end(), less);
    return out;
}

vault::SortKey effective_sort_key(vault::SortKey gallery_key,
                                  vault::SortKey vault_default) noexcept
{
    using enum vault::SortKey;
    if (gallery_key != Default) {
        return gallery_key;
    }
    return vault_default == Default ? Insertion : vault_default;
}

vault::SortKey next_sort_key(vault::SortKey current) noexcept
{
    using enum vault::SortKey;
    switch (current) {
    case Default:   return NameAsc;
    case NameAsc:   return NameDesc;
    case NameDesc:  return DateAsc;
    case DateAsc:   return DateDesc;
    case DateDesc:  return SizeAsc;
    case SizeAsc:   return SizeDesc;
    case SizeDesc:  return Insertion;
    case Insertion: return Default;
    }
    return Default;   // unreachable for a valid enum value; safe fallback
}

vault::SortKey prev_sort_key(vault::SortKey current) noexcept
{
    using enum vault::SortKey;
    switch (current) {
    case NameAsc:   return Default;
    case NameDesc:  return NameAsc;
    case DateAsc:   return NameDesc;
    case DateDesc:  return DateAsc;
    case SizeAsc:   return DateDesc;
    case SizeDesc:  return SizeAsc;
    case Insertion: return SizeDesc;
    case Default:   return Insertion;
    }
    return Default;   // unreachable for a valid enum value; safe fallback
}

std::string sort_key_label(vault::SortKey key, vault::SortKey vault_default)
{
    using enum vault::SortKey;
    if (key == Default && vault_default == Insertion) {
        return {};
    }

    switch (effective_sort_key(key, vault_default)) {
    case NameAsc:   return "Name ↑";
    case NameDesc:  return "Name ↓";
    case DateAsc:   return "Date ↑";
    case DateDesc:  return "Date ↓";
    case SizeAsc:   return "Size ↑";
    case SizeDesc:  return "Size ↓";
    case Insertion: return "Insertion";
    case Default:   return {};   // unreachable: effective_sort_key never returns Default
    }
    return {};
}

} // namespace ui
