#include "ui/zip_import.h"

#include "crypto/secure_mem.h"
#include "image/format_registry.h"
#include "vault/vault.h"

#include "miniz.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace ui {

ZipImportOutcome import_zip(vault::Vault&                v,
                            const std::filesystem::path& zip_path,
                            ZipDest                      dest,
                            std::string_view             base_gallery,
                            std::string_view             new_gallery_name,
                            ZipConflictPolicy            policy)
{
    ZipImportOutcome out;
    std::fprintf(stderr, "[ZIPDBG] enter dest=%d\n", static_cast<int>(dest)); std::fflush(stderr);  // TEMP-DBG

    // Read the whole archive into memory and drive miniz from the buffer
    // (mz_zip_reader_init_mem) rather than mz_zip_reader_init_file. The in-memory
    // reader is the portable, well-exercised path; the file-based path crashed
    // under MSVC. The archive is the user's own on-disk file — only the
    // *decompressed* entry bytes are sensitive, and those still go to SecureBytes.
    // `archive` must outlive every read: init_mem borrows it, it does not copy.
    std::vector<uint8_t> archive;
    {
        std::ifstream f(zip_path, std::ios::binary | std::ios::ate);
        if (f) {
            const std::streamoff sz = f.tellg();
            if (sz > 0) {
                archive.resize(static_cast<size_t>(sz));
                f.seekg(0);
                f.read(reinterpret_cast<char*>(archive.data()), sz);
            }
        }
    }

    std::fprintf(stderr, "[ZIPDBG] archive read bytes=%zu\n", archive.size()); std::fflush(stderr);  // TEMP-DBG

    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (archive.empty() || !mz_zip_reader_init_mem(&zip, archive.data(), archive.size(), 0)) {
        // On a read/init failure there is nothing to mz_zip_reader_end().
        out.error = "Could not open archive";
        std::fprintf(stderr, "[ZipImport] open failed: %s\n", zip_path.string().c_str());
        return out;
    }

    // Build the entry list from the central directory.
    std::vector<ZipEntry> entries;
    const mz_uint n = mz_zip_reader_get_num_files(&zip);
    std::fprintf(stderr, "[ZIPDBG] init_mem ok num_files=%u\n", n); std::fflush(stderr);  // TEMP-DBG
    entries.reserve(n);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        const bool is_dir = mz_zip_reader_is_file_a_directory(&zip, i) != MZ_FALSE;
        std::fprintf(stderr, "[ZIPDBG] entry[%u] dir=%d name='%s'\n", i, is_dir ? 1 : 0, st.m_filename); std::fflush(stderr);  // TEMP-DBG
        entries.push_back({std::string(st.m_filename), is_dir});
    }

    ZipPlan plan = build_zip_plan(entries, dest, base_gallery, new_gallery_name, policy);
    std::fprintf(stderr, "[ZIPDBG] plan galleries=%zu placements=%zu needs_res=%d skipped=%d\n",
                 plan.galleries.size(), plan.placements.size(), plan.needs_resolution ? 1 : 0,
                 plan.skipped_unsupported); std::fflush(stderr);  // TEMP-DBG
    if (plan.needs_resolution) {
        out.ok = true;
        out.needs_resolution = true;
        out.mixed_dirs = std::move(plan.mixed_dirs);
        mz_zip_reader_end(&zip);
        return out;
    }
    out.skipped = plan.skipped_unsupported;

    // Create galleries first (parents already ordered first by the planner).
    for (const auto& g : plan.galleries) {
        std::fprintf(stderr, "[ZIPDBG] create_gallery '%s'\n", g.c_str()); std::fflush(stderr);  // TEMP-DBG
        const vault::VaultResult r = v.create_gallery(g);
        if (r != vault::VaultResult::Ok && r != vault::VaultResult::AlreadyExists) {
            out.error = "Could not create gallery: " + g;
            mz_zip_reader_end(&zip);
            return out;
        }
    }

    using enum vault::VaultResult;
    for (const auto& pl : plan.placements) {
        std::fprintf(stderr, "[ZIPDBG] placement idx=%zu gallery='%s' file='%s'\n",
                     pl.entry_index, pl.gallery_path.c_str(), pl.filename.c_str()); std::fflush(stderr);  // TEMP-DBG
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, static_cast<mz_uint>(pl.entry_index), &st)) {
            ++out.skipped;
            continue;
        }
        std::fprintf(stderr, "[ZIPDBG]   uncomp_size=%llu\n", static_cast<unsigned long long>(st.m_uncomp_size)); std::fflush(stderr);  // TEMP-DBG

        crypto::SecureBytes bytes(static_cast<size_t>(st.m_uncomp_size));  // mlock'd; wiped on scope exit
        std::fprintf(stderr, "[ZIPDBG]   SecureBytes ok data=%p\n", static_cast<const void*>(bytes.data())); std::fflush(stderr);  // TEMP-DBG
        if (!mz_zip_reader_extract_to_mem(&zip, static_cast<mz_uint>(pl.entry_index),
                                          bytes.data(), bytes.size(), 0)) {
            ++out.skipped;
            continue;
        }
        std::span<const uint8_t> span{bytes.data(), bytes.size()};
        std::fprintf(stderr, "[ZIPDBG]   extracted; detecting format\n"); std::fflush(stderr);  // TEMP-DBG

        vault::VaultResult r;
        if (image::detect_format(span) != image::ImageFormat::Unknown) {
            std::fprintf(stderr, "[ZIPDBG]   -> add_image\n"); std::fflush(stderr);  // TEMP-DBG
            r = v.add_image(pl.gallery_path, span, pl.filename);
        } else {
            std::fprintf(stderr, "[ZIPDBG]   -> add_video\n"); std::fflush(stderr);  // TEMP-DBG
            r = v.add_video(pl.gallery_path, span, pl.filename);
        }
        std::fprintf(stderr, "[ZIPDBG]   add result=%d\n", static_cast<int>(r)); std::fflush(stderr);  // TEMP-DBG

        switch (r) {
            case Ok:
                ++out.imported;
                break;
            case AlreadyExists:
                ++out.skipped;
                break;
            case InvalidArg:
            case BadFormat:
                ++out.skipped;
                break;  // unsupported content
            default:
                if (out.error.empty()) out.error = "Import error on " + pl.filename;
                ++out.skipped;
                break;
        }
    }

    mz_zip_reader_end(&zip);
    out.ok = true;
    std::fprintf(stderr, "[ZIPDBG] done imported=%d skipped=%d\n", out.imported, out.skipped); std::fflush(stderr);  // TEMP-DBG
    return out;
}

}  // namespace ui
