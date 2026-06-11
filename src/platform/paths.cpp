#include "platform/paths.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstdio>

namespace platform {

std::filesystem::path config_dir()
{
    char* pref = SDL_GetPrefPath("ObscuraSafeVault", "ObscuraSafeVault");
    if (!pref) return {};
    // SDL_GetPrefPath returns a path with trailing slash; remove it for clean comparison.
    std::string s{pref};
    SDL_free(pref);
    if (!s.empty() && s.back() == '/') s.pop_back();
    return std::filesystem::path{s};
}

std::filesystem::path default_vault_path()
{
    auto dir = config_dir();
    return dir.empty() ? std::filesystem::path{"vault.osv"} : dir / "vault.osv";
}

std::optional<std::vector<uint8_t>> read_file(const std::filesystem::path& path)
{
    std::FILE* f = std::fopen(path.string().c_str(), "rb");
    if (!f) return std::nullopt;

    std::vector<uint8_t> buf;
    std::array<uint8_t, 64 * 1024> chunk;
    size_t n;
    while ((n = std::fread(chunk.data(), 1, chunk.size(), f)) > 0)
        buf.insert(buf.end(), chunk.data(), chunk.data() + n);

    const bool ok = std::ferror(f) == 0;
    std::fclose(f);
    if (!ok) return std::nullopt;
    return buf;
}

} // namespace platform
