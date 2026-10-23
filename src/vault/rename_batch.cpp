#include "vault/rename_batch.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <new>
#include <pcre2.h>
#include <set>
#include "vault/safe_name.h"
#include "vault/staging.h"

namespace vault {
namespace {
constexpr size_t ALLOCATION_LIMIT = 8 * 1024 * 1024;
struct alignas(std::max_align_t) Allocation {
    size_t size;
};
struct AllocationDeleter {
    void operator()(Allocation* storage) const noexcept
    {
        crypto::SecureAllocator<Allocation>{}.deallocate(storage, 0);
    }
};
using AllocationOwner = std::unique_ptr<Allocation, AllocationDeleter>;
struct Budget {
    size_t allocated = 0;
    size_t calls = 0;
    OpProgress* progress = nullptr;
    std::chrono::steady_clock::time_point deadline;
};
void* regex_alloc(size_t size, void* context)
{
    auto& b = *static_cast<Budget*>(context);
    if (size > ALLOCATION_LIMIT - b.allocated) return nullptr;
    try {
        const size_t units = 1 + (size + sizeof(Allocation) - 1) / sizeof(Allocation);
        AllocationOwner storage{crypto::SecureAllocator<Allocation>{}.allocate(units)};
        auto* bytes = static_cast<std::byte*>(static_cast<void*>(storage.get()));
        std::fill_n(bytes, units * sizeof(Allocation), std::byte{});
        std::construct_at(storage.get(), size);
        b.allocated += size;
        return storage.release() + 1;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void regex_free(std::byte* ptr, Budget& budget)
{
    if (!ptr) return;
    auto* header = static_cast<Allocation*>(static_cast<void*>(ptr - sizeof(Allocation)));
    const AllocationOwner storage{header};
    budget.allocated -= header->size;
    std::destroy_at(header);
}
bool cancelled(const OpProgress* p)
{
    return p && p->cancel.load();
}
int callout(pcre2_callout_block*, void* context)
{
    if (auto& b = *static_cast<Budget*>(context); ++b.calls > 100000 || cancelled(b.progress) ||
                                                  std::chrono::steady_clock::now() > b.deadline)
        return PCRE2_ERROR_CALLOUT;
    return 0;
}
struct Entry {
    IndexNode* node;
    RenameId parent;
    bool selected;
};
using Tree = std::map<RenameId, Entry>;
bool scan(Vault& v, std::span<const RenameId> ids, bool recursive, Tree& tree, const OpProgress* p)
{
    auto* root = v.resolve_node("");
    if (!root) return false;
    std::set<RenameId> wanted(ids.begin(), ids.end());
    std::vector<Entry> pending{{root, {}, false}};
    while (!pending.empty()) {
        if (cancelled(p)) return false;
        auto e = pending.back();
        pending.pop_back();
        const bool chosen = wanted.contains(e.node->node_id) || (recursive && e.selected);
        if (e.node != root && !tree.try_emplace(e.node->node_id, e.node, e.parent, chosen).second)
            return false;
        for (auto& child : e.node->children)
            pending.emplace_back(&child, e.node->node_id, chosen);
    }
    return std::ranges::all_of(wanted, [&](const auto& id) { return tree.contains(id); });
}
bool collisions(const Tree& tree, RenamePreview& preview)
{
    std::map<RenameId, size_t> rows;
    for (size_t i = 0; i < preview.rows.size(); ++i)
        rows.try_emplace(preview.rows[i].id, i);
    std::map<std::pair<RenameId, std::string_view>, RenameId> names;
    bool ok = true;
    for (const auto& [id, e] : tree) {
        const auto row = rows.find(id);
        const auto name =
            row == rows.end() ? e.node->name.view() : preview.rows[row->second].after.view();
        const auto [at, inserted] = names.try_emplace(std::make_pair(e.parent, name), id);
        if (inserted) continue;
        // Existing duplicate names outside the selection do not invalidate it.
        if (row == rows.end() && !rows.contains(at->second)) continue;
        ok = false;
        if (row != rows.end()) preview.rows[row->second].state = RenameState::Collision;
        if (const auto other = rows.find(at->second); other != rows.end())
            preview.rows[other->second].state = RenameState::Collision;
    }
    return ok;
}
class Regex {
    Budget budget_;
    using General = std::unique_ptr<pcre2_general_context, decltype(&pcre2_general_context_free)>;
    using Compile = std::unique_ptr<pcre2_compile_context, decltype(&pcre2_compile_context_free)>;
    using Code = std::unique_ptr<pcre2_code, decltype(&pcre2_code_free)>;
    using Match = std::unique_ptr<pcre2_match_context, decltype(&pcre2_match_context_free)>;
    using Data = std::unique_ptr<pcre2_match_data, decltype(&pcre2_match_data_free)>;
    General general_{nullptr, pcre2_general_context_free};
    Compile compile_{nullptr, pcre2_compile_context_free};
    Code code_{nullptr, pcre2_code_free};
    Match match_{nullptr, pcre2_match_context_free};
    Data data_{nullptr, pcre2_match_data_free};

public:
    bool init(const RenameOptions& o, OpProgress* progress)
    {
        budget_.progress = progress;
        if (o.pattern.empty() || o.pattern.size() > 4096 || o.replacement.size() > 4096)
            return false;
        general_.reset(pcre2_general_context_create(
            regex_alloc,
            [](auto* ptr, auto* context) {
                regex_free(static_cast<std::byte*>(ptr), *static_cast<Budget*>(context));
            },
            &budget_));
        if (!general_) return false;
        compile_.reset(pcre2_compile_context_create(general_.get()));
        match_.reset(pcre2_match_context_create(general_.get()));
        if (!compile_ || !match_) return false;
        pcre2_set_max_pattern_length(compile_.get(), 4096);
        pcre2_set_max_pattern_compiled_length(compile_.get(), 1024 * 1024);
        pcre2_set_parens_nest_limit(compile_.get(), 64);
        int error = 0;
        size_t offset = 0;
        uint32_t flags = PCRE2_UTF | PCRE2_UCP | PCRE2_AUTO_CALLOUT | PCRE2_NEVER_BACKSLASH_C;
        if (!o.case_sensitive) flags |= PCRE2_CASELESS;
        code_.reset(pcre2_compile(reinterpret_cast<PCRE2_SPTR>(o.pattern.view().data()),
                                  o.pattern.size(), flags, &error, &offset, compile_.get()));
        if (!code_) return false;
        data_.reset(pcre2_match_data_create_from_pattern(code_.get(), general_.get()));
        pcre2_set_match_limit(match_.get(), 100000);
        pcre2_set_depth_limit(match_.get(), 256);
        pcre2_set_heap_limit(match_.get(), 1024);
        pcre2_set_callout(match_.get(), callout, &budget_);
        return bool(data_);
    }
    void replace(RenameRow& row, bool media, const RenameOptions& o)
    {
        using enum RenameState;
        if (row.before.size() > MAX_NODE_NAME_BYTES) {
            row.state = Invalid;
            return;
        }
        auto subject = row.before.view();
        std::string_view extension;
        if (media && o.preserve_extensions) {
            const auto dot = subject.rfind('.');
            if (dot != std::string_view::npos && dot != 0) {
                extension = subject.substr(dot);
                subject = subject.substr(0, dot);
            }
        }
        crypto::SecureBytes output(MAX_NODE_NAME_BYTES + 1);
        if (output.size() != MAX_NODE_NAME_BYTES + 1) {
            row.state = Invalid;
            return;
        }
        size_t length = output.size();
        budget_.calls = 0;
        budget_.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{50};
        const int rc = pcre2_substitute(
            code_.get(), reinterpret_cast<PCRE2_SPTR>(subject.data()), subject.size(), 0,
            o.all_matches ? PCRE2_SUBSTITUTE_GLOBAL : 0, data_.get(), match_.get(),
            reinterpret_cast<PCRE2_SPTR>(o.replacement.view().data()), o.replacement.size(),
            output.data(), &length);
        if (rc < 0 || length + extension.size() > MAX_NODE_NAME_BYTES) {
            row.state = Invalid;
            return;
        }
        std::ranges::copy(extension, output.data() + length);
        row.after = std::string_view(reinterpret_cast<const char*>(output.data()),
                                     length + extension.size());
        if (!is_safe_node_name(row.after.view()))
            row.state = Invalid;
        else if (rc == 0)
            row.state = Unmatched;
        else
            row.state = row.before == row.after ? Unchanged : Changed;
    }
};
}  // namespace
bool RenamePreview::valid() const
{
    return error.empty() && !rows.empty() && std::ranges::none_of(rows, [](const auto& r) {
               return r.state == RenameState::Invalid || r.state == RenameState::Collision;
           });
}
RenamePreview preview_rename(Vault& v, std::span<const RenameId> selected, const RenameOptions& o,
                             OpProgress* progress)
{
    RenamePreview result;
    Regex regex;
    Tree tree;
    if (!v.is_unlocked() || vault_is_read_only(v)) {
        result.error = "Vault is not writable.";
        return result;
    }
    if (!regex.init(o, progress)) {
        result.error = "Invalid expression or regex compile limit exceeded.";
        return result;
    }
    if (!scan(v, selected, o.recursive, tree, progress)) {
        result.error = "Selection is stale or cancelled.";
        return result;
    }
    if (progress) {
        progress->done = 0;
        progress->total = static_cast<int>(tree.size());
    }
    for (const auto& [id, e] : tree) {
        if (cancelled(progress)) {
            result.error = "Cancelled.";
            return result;
        }
        if (e.selected) {
            RenameRow row{id, e.parent, e.node->name, e.node->name};
            regex.replace(row, !e.node->is_gallery(), o);
            result.rows.push_back(std::move(row));
        }
        if (progress) ++progress->done;
    }
    (void)collisions(tree, result);
    return result;
}
VaultResult apply_rename(Vault& v, const RenamePreview& preview, OpProgress* progress)
{
    if (!v.is_unlocked()) return VaultResult::Locked;
    if (vault_is_read_only(v) || !preview.valid()) return VaultResult::InvalidArg;
    Tree tree;
    if (!scan(v, {}, false, tree, progress)) return VaultResult::InvalidArg;
    // Validate every row before changing even one name, including unchanged rows.
    std::set<RenameId> seen;
    for (const auto& row : preview.rows) {
        const auto it = tree.find(row.id);
        if (it == tree.end() || !seen.insert(row.id).second || it->second.parent != row.parent ||
            it->second.node->name != row.before || !is_safe_node_name(row.after.view()))
            return VaultResult::InvalidArg;
    }
    if (auto check = preview; !collisions(tree, check) || cancelled(progress))
        return VaultResult::InvalidArg;
    // Allocate rollback/new strings before mutation; swaps and cycles need no
    // temporary names because the complete final namespace commits once.
    std::vector<std::pair<IndexNode*, crypto::SecureString>> changes;
    for (const auto& row : preview.rows)
        if (row.before != row.after) changes.emplace_back(tree.at(row.id).node, row.after);
    if (cancelled(progress)) return VaultResult::InvalidArg;
    if (changes.empty()) return VaultResult::Ok;
    struct Rollback {
        explicit Rollback(decltype(changes)& pending) noexcept : values(pending) {}
        Rollback(const Rollback&) = delete;
        Rollback& operator=(const Rollback&) = delete;
        Rollback(Rollback&&) = delete;
        Rollback& operator=(Rollback&&) = delete;
        decltype(changes)& values;
        bool committed = false;
        ~Rollback()
        {
            if (!committed)
                for (auto& [node, name] : values)
                    std::swap(node->name, name);
        }
    };
    for (auto& [node, name] : changes)
        std::swap(node->name, name);
    Rollback rollback{changes};
    // Cancellation is deliberately ignored once the durable commit starts.
    if (progress) progress->total = -1;
    const auto result = commit_staged(v);
    rollback.committed = result == VaultResult::Ok;
    return result;
}
}  // namespace vault
