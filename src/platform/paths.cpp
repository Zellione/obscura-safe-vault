#include "platform/paths.h"

#include <SDL3/SDL.h>

#include <monocypher.h>

#include <array>
#include <cstdio>
#include <print>

#include "crypto/random.h"

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

    // Size first, then one allocation and one read. Keyfiles pass through
    // here: a chunk-growing vector would strew stale copies of key material
    // across freed heap blocks on every reallocation.
    bool ok = std::fseek(f, 0, SEEK_END) == 0;
    const long size = ok ? std::ftell(f) : -1;
    ok = ok && size >= 0 && std::fseek(f, 0, SEEK_SET) == 0;

    std::vector<uint8_t> buf;
    if (ok && size > 0) {
        buf.resize(static_cast<size_t>(size));
        ok = std::fread(buf.data(), 1, buf.size(), f) == buf.size();
    }
    std::fclose(f);
    if (!ok) return std::nullopt;
    return buf;
}

bool write_new_keyfile(const std::filesystem::path& path)
{
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        std::println(stderr, "[Platform] refusing to overwrite existing keyfile {}",
                     path.string());
        return false;
    }

    std::array<uint8_t, KEYFILE_SIZE> key{};
    if (!crypto::fill_random(key)) return false;

    std::FILE* f = std::fopen(path.string().c_str(), "wb");
    if (!f) {
        crypto_wipe(key.data(), key.size());
        return false;
    }
    const bool ok = std::fwrite(key.data(), 1, key.size(), f) == key.size() &&
                    std::fflush(f) == 0;
    std::fclose(f);
    crypto_wipe(key.data(), key.size());  // the keyfile IS key material
    if (!ok) {
        std::error_code rm_ec;
        std::filesystem::remove(path, rm_ec);  // don't leave a short keyfile behind
    }
    return ok;
}

} // namespace platform
