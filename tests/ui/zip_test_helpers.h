#pragma once

// Shared helpers for the ZIP/CBZ planner + import tests. Kept in one header so
// the round-trip fixtures (archive writer, throwaway vault, temp dir) exist in a
// single place rather than copy-pasted per test TU.

#include "crypto/kdf.h"
#include "ui/zip_plan.h"
#include "vault/vault.h"

#include "miniz.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace ziptest {

namespace fs = std::filesystem;

// Build an entry list from archive paths (a trailing '/' marks a directory).
inline std::vector<ui::ZipEntry> entries(std::initializer_list<const char*> names)
{
    std::vector<ui::ZipEntry> v;
    for (const char* n : names) {
        std::string s = n;
        v.push_back({s, !s.empty() && s.back() == '/'});
    }
    return v;
}

// Synthetic "image": valid JPEG SOI/APP0 magic (so image::detect_format routes
// to Vault::add_image) followed by a seed-derived tail, so each blob is distinct
// and byte-checkable. The thumbnail decode fails fast on the garbage tail and
// add_image stores the original anyway (thumb_length == 0).
inline std::vector<uint8_t> fake_jpeg(uint8_t seed)
{
    std::vector<uint8_t> v{0xFF, 0xD8, 0xFF, 0xE0};
    for (int i = 0; i < 200; ++i)
        v.push_back(static_cast<uint8_t>(seed + i));
    return v;
}

// Write a zip archive (the comic-vs-zip distinction is purely the output path's
// extension) from (archive_path -> bytes) entries; return its path. `level_and_flags`
// defaults to always setting the UTF-8 general-purpose bit (miniz's own default);
// pass MZ_BEST_SPEED | MZ_ZIP_FLAG_ASCII_FILENAME to write raw non-UTF-8 bytes
// (e.g. CP437) without that flag, for legacy-encoding tests (Phase 36 part 2).
inline fs::path make_archive(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& items,
                             const fs::path& out,
                             mz_uint level_and_flags = MZ_BEST_SPEED)
{
    mz_zip_archive z;
    std::memset(&z, 0, sizeof(z));
    mz_zip_writer_init_heap(&z, 0, 0);
    for (const auto& [name, b] : items)
        mz_zip_writer_add_mem(&z, name.c_str(), b.data(), b.size(), level_and_flags);
    void* buf = nullptr;
    size_t sz = 0;
    mz_zip_writer_finalize_heap_archive(&z, &buf, &sz);
    std::ofstream(out, std::ios::binary).write(static_cast<const char*>(buf), static_cast<std::streamsize>(sz));
    mz_free(buf);
    mz_zip_writer_end(&z);
    return out;
}

// Fast Argon2 params for tests (mirrors tests/vault/*). Production cost is far higher.
inline const crypto::KdfParams kTestKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

// Fill `v` in place with a fresh password-only vault at `p`.
inline void make_vault(vault::Vault& v, const fs::path& p)
{
    const std::vector<uint8_t> pw{'p', 'w'};
    (void)vault::Vault::create(p.string(), pw, {}, kTestKdf, v);
}

// Fresh empty temp dir. Non-throwing: a still-open vault file from a crashed
// prior run can't be deleted on Windows, and the throwing overload would abort.
inline fs::path fresh_dir(const char* name)
{
    fs::path dir = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

// Remove the temp dir. Non-throwing (the Vault must already be destroyed so its
// file handle is closed — on Windows an open file can't be deleted).
inline void cleanup_dir(const fs::path& dir)
{
    std::error_code ec;
    fs::remove_all(dir, ec);
}

} // namespace ziptest
