#include "vault/tag_bundles.h"
#include <algorithm>
#include <memory>
#include "crypto/random.h"
#include "vault/vault.h"

namespace vault {
void initialize_tag_provenance(IndexNode& node)
{
    if (node.tag_provenance && !node.tag_bundles.empty()) return;
    node.explicit_tags = node.tags;
    node.tag_provenance = true;
}

bool rebuild_bundle_tags(IndexNode& node, const VaultSettings& settings)
{
    if (!node.tag_provenance) return true;
    auto tags = node.explicit_tags;
    for (const auto& id : node.tag_bundles) {
        const auto bundle = std::ranges::find(settings.tag_bundles, id, &TagBundle::id);
        if (bundle == settings.tag_bundles.end()) return false;
        for (const auto& tag : bundle->tags) {
            if (std::ranges::none_of(
                    tags, [&](const auto& t) { return tag_ci_equal(t.view(), tag.view()); }))
                tags.push_back(tag);
        }
    }
    if (tags.size() > INDEX_MAX_TAGS) return false;
    node.tags = std::move(tags);
    return true;
}

void visit_bundle_tag_references(IndexNode& root, VaultSettings& settings,
                                 const std::function<void(crypto::SecureString&)>& visit)
{
    const auto walk = [&](auto&& self, IndexNode& node) -> void {
        for (auto& tag : node.explicit_tags)
            visit(tag);
        for (auto& child : node.children)
            self(self, child);
    };
    walk(walk, root);
    for (auto& bundle : settings.tag_bundles)
        for (auto& tag : bundle.tags)
            visit(tag);
}

// Owns the transaction's live rollback, without replacing tree nodes/pointers.
struct BundleAccess {
    struct Prior {
        IndexNode* node;
        bool initialized;
        std::vector<crypto::SecureString> tags, explicit_tags;
        std::vector<std::array<uint8_t, crypto::NODE_ID_SIZE>> bundles;
    };
    Vault& v;
    VaultSettings settings;
    std::vector<Prior> prior;
    bool committed = false;
    explicit BundleAccess(Vault& vault) : v(vault), settings(vault.settings_) {}
    BundleAccess(const BundleAccess&) = delete;
    BundleAccess& operator=(const BundleAccess&) = delete;
    BundleAccess(BundleAccess&&) = delete;
    BundleAccess& operator=(BundleAccess&&) = delete;
    ~BundleAccess()
    {
        if (committed) return;
        v.settings_ = std::move(settings);
        for (auto it = prior.rbegin(); it != prior.rend(); ++it) {
            auto& p = *it;
            p.node->tags = std::move(p.tags);
            p.node->explicit_tags = std::move(p.explicit_tags);
            p.node->tag_bundles = std::move(p.bundles);
            p.node->tag_provenance = p.initialized;
        }
    }
    void capture(IndexNode& node)
    {
        prior.push_back(
            {&node, node.tag_provenance, node.tags, node.explicit_tags, node.tag_bundles});
    }
    VaultResult finish()
    {
        const auto result = v.commit_index();
        committed = result == VaultResult::Ok;
        return result;
    }
    static VaultResult check(Vault& v)
    {
        if (!v.unlocked_) return VaultResult::Locked;
        return v.v3_ ? VaultResult::Ok : VaultResult::InvalidArg;
    }
    static auto& settings_of(Vault& v)
    {
        return v.settings_;
    }
    auto& bundles()
    {
        return v.settings_.tag_bundles;
    }
    bool refresh(IndexNode& node, const std::array<uint8_t, crypto::NODE_ID_SIZE>* removed)
    {
        capture(node);
        if (removed) std::erase(node.tag_bundles, *removed);
        if (!rebuild_bundle_tags(node, v.settings_)) return false;
        for (auto& child : node.children)
            if (!refresh(child, removed)) return false;
        return true;
    }
};

VaultResult save_tag_bundle(Vault& v, std::string_view name, const std::vector<std::string>& tags,
                            std::string_view original_name)
{
    using enum VaultResult;
    if (const auto r = BundleAccess::check(v); r != Ok) return r;
    if (name.empty() || name.size() > 255 || name.contains('\0') || tags.size() > INDEX_MAX_TAGS)
        return InvalidArg;
    TagBundle replacement;
    replacement.name = name;
    for (const auto& tag : tags) {
        if (tag.empty() || tag.size() > 65535 || tag.contains('\0')) return InvalidArg;
        if (std::ranges::none_of(replacement.tags,
                                 [&](const auto& t) { return tag_ci_equal(t.view(), tag); }))
            replacement.tags.emplace_back(tag);
    }
    BundleAccess change(v);
    auto& bundles = change.bundles();
    auto found = std::ranges::find_if(bundles, [&](const auto& b) {
        return tag_ci_equal(b.name.view(), original_name.empty() ? name : original_name);
    });
    if (!original_name.empty() && found == bundles.end()) return NotFound;
    if (std::ranges::any_of(bundles, [&](const auto& b) {
            return &b != (found == bundles.end() ? nullptr : std::to_address(found)) &&
                   tag_ci_equal(b.name.view(), name);
        }))
        return AlreadyExists;
    if (found == bundles.end()) {
        if (bundles.size() >= INDEX_MAX_TAGS || !crypto::fill_random(replacement.id))
            return InvalidArg;
        bundles.push_back(std::move(replacement));
    } else {
        replacement.id = found->id;
        *found = std::move(replacement);
    }
    if (!change.refresh(*v.resolve_node(""), nullptr)) return InvalidArg;
    return change.finish();
}

VaultResult delete_tag_bundle(Vault& v, std::string_view name)
{
    using enum VaultResult;
    if (const auto r = BundleAccess::check(v); r != Ok) return r;
    BundleAccess change(v);
    auto& bundles = change.bundles();
    const auto found = std::ranges::find_if(
        bundles, [&](const auto& b) { return tag_ci_equal(b.name.view(), name); });
    if (found == bundles.end()) return NotFound;
    const auto id = found->id;
    bundles.erase(found);
    if (!change.refresh(*v.resolve_node(""), &id)) return InvalidArg;
    return change.finish();
}

VaultResult apply_tag_bundle(Vault& v, std::span<const std::string> paths, std::string_view name,
                             bool remove, OpProgress* progress)
{
    using enum VaultResult;
    if (const auto r = BundleAccess::check(v); r != Ok) return r;
    BundleAccess change(v);
    const auto& bundles = change.bundles();
    const auto found = std::ranges::find_if(
        bundles, [&](const auto& b) { return tag_ci_equal(b.name.view(), name); });
    if (found == bundles.end()) return NotFound;
    if (progress) {
        progress->total.store(static_cast<int>(paths.size()));
        progress->done.store(0);
    }
    for (auto* node : resolve_node_selection(v, paths)) {
        if (progress && progress->cancel.load()) return Ok;
        if (node && (std::ranges::find(node->tag_bundles, found->id) != node->tag_bundles.end()) ==
                        remove) {
            change.capture(*node);
            initialize_tag_provenance(*node);
            if (remove)
                std::erase(node->tag_bundles, found->id);
            else
                node->tag_bundles.push_back(found->id);
            if (!rebuild_bundle_tags(*node, vault_settings(v))) return InvalidArg;
        }
        if (progress) progress->done.fetch_add(1);
    }
    if (progress && progress->cancel.load()) return Ok;
    if (change.prior.empty()) return Ok;
    return change.finish();
}
VaultResult copy_tag_provenance(const IndexNode& source, const VaultSettings& source_settings,
                                IndexNode& destination, Vault& destination_vault)
{
    using enum VaultResult;
    if (source.tag_bundles.empty()) {
        initialize_tag_provenance(destination);
        return Ok;
    }
    if (const auto r = BundleAccess::check(destination_vault); r != Ok) return r;
    auto settings = BundleAccess::settings_of(destination_vault);
    IndexNode candidate;
    candidate.tags = destination.tags;
    candidate.tag_provenance = destination.tag_provenance;
    candidate.explicit_tags = destination.explicit_tags;
    candidate.tag_bundles = destination.tag_bundles;
    initialize_tag_provenance(candidate);
    // Transfer already materialized inherited tags. Only source bundle-only
    // contributions lose their otherwise spurious explicit ownership.
    for (const auto& tag : source.tags) {
        const bool manual =
            !source.tag_provenance || std::ranges::any_of(source.explicit_tags, [&](const auto& t) {
                return tag_ci_equal(t.view(), tag.view());
            });
        if (!manual)
            std::erase_if(candidate.explicit_tags,
                          [&](const auto& t) { return tag_ci_equal(t.view(), tag.view()); });
    }
    for (const auto& id : source.tag_bundles) {
        const auto found = std::ranges::find(source_settings.tag_bundles, id, &TagBundle::id);
        if (found == source_settings.tag_bundles.end()) return InvalidArg;
        auto definition = *found;
        const auto same_members = [&](const TagBundle& bundle) {
            return bundle.tags.size() == definition.tags.size() &&
                   std::ranges::equal(bundle.tags, definition.tags,
                                      [](const auto& a, const auto& b) {
                                          return tag_ci_equal(a.view(), b.view());
                                      });
        };
        auto match = std::ranges::find_if(settings.tag_bundles, [&](const auto& b) {
            return (b.id == id || tag_ci_equal(b.name.view(), definition.name.view())) &&
                   same_members(b);
        });
        if (match == settings.tag_bundles.end()) {
            const auto name = std::string(definition.name.view());
            unsigned suffix = 2;
            while (std::ranges::any_of(settings.tag_bundles, [&](const auto& b) {
                return tag_ci_equal(b.name.view(), definition.name.view());
            })) {
                definition.name =
                    name.substr(0, 220) + " (import " + std::to_string(suffix++) + ")";
                match = std::ranges::find_if(settings.tag_bundles, [&](const auto& b) {
                    return tag_ci_equal(b.name.view(), definition.name.view()) && same_members(b);
                });
                if (match != settings.tag_bundles.end()) break;
            }
            if (match == settings.tag_bundles.end()) {
                if (settings.tag_bundles.size() >= INDEX_MAX_TAGS) return InvalidArg;
                if (std::ranges::any_of(settings.tag_bundles,
                                        [&](const auto& b) { return b.id == id; }) &&
                    !crypto::fill_random(definition.id))
                    return IoError;
                settings.tag_bundles.push_back(std::move(definition));
                match = std::prev(settings.tag_bundles.end());
            }
        }
        if (std::ranges::find(candidate.tag_bundles, match->id) == candidate.tag_bundles.end())
            candidate.tag_bundles.push_back(match->id);
    }
    if (!rebuild_bundle_tags(candidate, settings)) return InvalidArg;
    BundleAccess::settings_of(destination_vault) = std::move(settings);
    destination.tags = std::move(candidate.tags);
    destination.explicit_tags = std::move(candidate.explicit_tags);
    destination.tag_bundles = std::move(candidate.tag_bundles);
    destination.tag_provenance = true;
    return Ok;
}

VaultResult merge_tag_provenance(const IndexNode& source, const VaultSettings& source_settings,
                                 Vault& v, std::string_view path,
                                 std::span<const std::string> inherited, bool commit)
{
    using enum VaultResult;
    if (!v.is_unlocked()) return Locked;
    if (vault_is_read_only(v)) return InvalidArg;
    auto* target = v.resolve_node(path);
    if (!target) return NotFound;
    const auto add = [](auto& tags, std::string_view tag) {
        if (std::ranges::none_of(tags, [&](const auto& t) { return tag_ci_equal(t.view(), tag); }))
            tags.emplace_back(tag);
    };
    // This union is intentional manual ownership, separate from contributions.
    auto manual = target->tag_provenance ? target->explicit_tags : target->tags;
    for (const auto& tag : source.tag_provenance ? source.explicit_tags : source.tags)
        add(manual, tag.view());
    for (const auto& tag : inherited)
        add(manual, tag);
    if (manual.size() > INDEX_MAX_TAGS) return InvalidArg;
    if (!vault_uses_directory_storage(v)) {
        if (!source.tag_bundles.empty()) return InvalidArg;
        std::vector<std::string> tags;
        for (const auto& tag : manual)
            tags.emplace_back(tag.view());
        if (commit) return v.set_tags(path, tags);
        target->tags = std::move(manual);
        target->explicit_tags = target->tags;
        target->tag_provenance = true;
        return Ok;
    }
    BundleAccess change(v);
    change.capture(*target);
    for (const auto& tag : source.tags)
        add(target->tags, tag.view());
    if (const auto r = copy_tag_provenance(source, source_settings, *target, v); r != Ok) return r;
    target->explicit_tags = std::move(manual);
    target->tag_provenance = true;
    if (!rebuild_bundle_tags(*target, vault_settings(v))) return InvalidArg;
    if (commit) return change.finish();
    change.committed = true;
    return Ok;
}

}  // namespace vault
