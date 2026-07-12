#include "platform/vault_registry.h"

#include <fstream>
#include <string>

#include "platform/paths.h"
#include "platform/safe_print.h"

namespace platform {

VaultRegistry::VaultRegistry(std::filesystem::path file) : file_(std::move(file)) {}

VaultRegistry VaultRegistry::default_location()
{
    auto dir = config_dir();
    if (dir.empty()) return VaultRegistry{};            // no config dir → inert registry
    return VaultRegistry{dir / "vaults.list"};
}

std::vector<std::filesystem::path> VaultRegistry::list() const
{
    std::vector<std::filesystem::path> out;
    if (file_.empty()) return out;

    std::ifstream in(file_, std::ios::binary);
    if (!in) return out;                                // missing file → empty list

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();  // tolerate CRLF
        if (line.empty()) continue;
        std::filesystem::path p{line};
        // De-duplicate, keeping the first (most-recent) occurrence.
        bool dup = false;
        for (const auto& e : out) if (e == p) { dup = true; break; }
        if (!dup) out.push_back(std::move(p));
    }
    return out;
}

bool VaultRegistry::add(const std::filesystem::path& vault) const
{
    if (file_.empty() || vault.empty()) return false;
    auto entries = list();
    std::erase_if(entries, [&vault](const std::filesystem::path& e) { return e == vault; });
    entries.insert(entries.begin(), vault);             // move-to-front
    return write(entries);
}

bool VaultRegistry::remove(const std::filesystem::path& vault) const
{
    if (file_.empty()) return false;
    auto entries = list();
    std::erase_if(entries, [&vault](const std::filesystem::path& e) { return e == vault; });
    return write(entries);
}

void VaultRegistry::seed_if_empty(const std::filesystem::path& candidate) const
{
    if (file_.empty()) return;
    std::error_code ec;
    if (list().empty() && std::filesystem::exists(candidate, ec))
        (void)add(candidate);
}

bool VaultRegistry::write(const std::vector<std::filesystem::path>& entries) const
{
    if (file_.empty()) return false;

    // Atomic replace: write a sibling temp file, then rename over the target so a
    // crash mid-write never leaves a torn list.
    std::filesystem::path tmp = file_;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            platform::safe_println(stderr, "[VaultRegistry] cannot write {}", tmp.string());
            return false;
        }
        for (const auto& e : entries) out << e.string() << '\n';
        out.flush();
        if (!out) {
            platform::safe_println(stderr, "[VaultRegistry] write error on {}", tmp.string());
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file_, ec);
    if (ec) {
        platform::safe_println(stderr, "[VaultRegistry] rename failed: {}", ec.message());
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

} // namespace platform
