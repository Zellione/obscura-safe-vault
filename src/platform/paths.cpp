#include "platform/paths.h"

#include <SDL3/SDL.h>

#include <monocypher.h>

#include <array>
#include <cstdio>

#if defined(_WIN32)
#  include <io.h>
#endif

#include "crypto/random.h"
#include "platform/safe_print.h"

namespace platform {

// 64-bit-safe seek-to-end + tell, mirroring vault::fileutil (src/vault/file_util.h):
// plain fseek/ftell use `long`, which is 32-bit on Windows (LLP64) even in
// 64-bit builds, silently capping readable files at ~2 GiB there. platform/
// doesn't depend on vault/, so the same fix is duplicated locally.
namespace {

[[nodiscard]] bool file_size64(std::FILE* fp, long long& out_size) noexcept
{
#if defined(_WIN32)
    if (_fseeki64(fp, 0, SEEK_END) != 0) return false;
    const long long pos = _ftelli64(fp);
#else
    if (fseeko(fp, 0, SEEK_END) != 0) return false;
    const off_t pos = ftello(fp);
#endif
    if (pos < 0) return false;
    out_size = pos;
    return true;
}

[[nodiscard]] bool seek_to64(std::FILE* fp, long long off) noexcept
{
#if defined(_WIN32)
    return _fseeki64(fp, off, SEEK_SET) == 0;
#else
    return fseeko(fp, static_cast<off_t>(off), SEEK_SET) == 0;
#endif
}

} // namespace

std::filesystem::path config_dir()
{
    char* pref = SDL_GetPrefPath("ObscuraSafeVault", "ObscuraSafeVault");
    if (!pref) return {};
    // SDL_GetPrefPath returns a path with a trailing separator ('\' on
    // Windows); remove it for clean comparison.
    std::string s{pref};
    SDL_free(pref);
    if (!s.empty() && (s.back() == '/' || s.back() == '\\')) s.pop_back();
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
    long long size = -1;
    bool ok = file_size64(f, size) && seek_to64(f, 0);

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
    if (std::error_code ec; std::filesystem::exists(path, ec)) {
        platform::safe_println(stderr, "[Platform] refusing to overwrite existing keyfile {}",
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
