#include "ui/tag_inherit.h"

#include <algorithm>
#include <span>

#include "ui/nav_model.h"
#include "vault/index.h"
#include "vault/vault.h"

namespace ui {

namespace {

char lower_ascii(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : static_cast<char>(c);
}

bool ci_equal(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (lower_ascii(static_cast<unsigned char>(a[i])) !=
            lower_ascii(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

bool ci_contains(const std::vector<std::string>& v, std::string_view s)
{
    return std::ranges::any_of(v, [&](const std::string& e) { return ci_equal(e, s); });
}

} // namespace

std::vector<std::string> inherited_tags(const vault::Vault& vault, std::string_view node_path)
{
    const auto segs = split_path(node_path);
    if (segs.size() < 2) return {};   // root nodes have no ancestors

    // The node's own tags: an inherited duplicate is hidden (own tags win).
    std::vector<std::string> own;
    const std::string parent = join_path(std::span(segs.data(), segs.size() - 1));
    for (const auto* child : vault.list(parent))
        if (child->name == segs.back()) { own = child->tags; break; }

    // Union of every ancestor gallery's tags, root-first, ci-de-duplicated —
    // the same cascade search matches the node by (compute_effective_tags).
    std::vector<std::string> out;
    for (size_t len = 1; len < segs.size(); ++len) {
        const std::string ancestor_parent = join_path(std::span(segs.data(), len - 1));
        for (const auto* child : vault.list(ancestor_parent)) {
            if (child->name != segs[len - 1]) continue;
            for (const std::string& t : child->tags)
                if (!ci_contains(out, t) && !ci_contains(own, t)) out.push_back(t);
            break;
        }
    }
    return out;
}

} // namespace ui
