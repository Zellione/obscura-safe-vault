#include "ui/zip_import.h"

#include "crypto/secure_mem.h"
#include "image/format_registry.h"
#include "vault/vault.h"

#include "miniz.h"

#include <cstdio>
#include <cstring>

namespace ui {

ZipImportOutcome import_zip(vault::Vault&                v,
                            const std::filesystem::path& zip_path,
                            ZipDest                      dest,
                            std::string_view             base_gallery,
                            std::string_view             new_gallery_name,
                            ZipConflictPolicy            policy)
{
    ZipImportOutcome out;

    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, zip_path.string().c_str(), 0)) {
        // On init failure miniz has already released any partial state — there is
        // nothing to mz_zip_reader_end() here.
        out.error = "Could not open archive";
        std::fprintf(stderr, "[ZipImport] open failed: %s\n", zip_path.string().c_str());
        return out;
    }

    // Build the entry list from the central directory.
    std::vector<ZipEntry> entries;
    const mz_uint n = mz_zip_reader_get_num_files(&zip);
    entries.reserve(n);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        entries.push_back({std::string(st.m_filename),
                           mz_zip_reader_is_file_a_directory(&zip, i) != MZ_FALSE});
    }

    ZipPlan plan = build_zip_plan(entries, dest, base_gallery, new_gallery_name, policy);
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
        const vault::VaultResult r = v.create_gallery(g);
        if (r != vault::VaultResult::Ok && r != vault::VaultResult::AlreadyExists) {
            out.error = "Could not create gallery: " + g;
            mz_zip_reader_end(&zip);
            return out;
        }
    }

    using enum vault::VaultResult;
    for (const auto& pl : plan.placements) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, static_cast<mz_uint>(pl.entry_index), &st)) {
            ++out.skipped;
            continue;
        }

        crypto::SecureBytes bytes(static_cast<size_t>(st.m_uncomp_size));  // mlock'd; wiped on scope exit
        if (!mz_zip_reader_extract_to_mem(&zip, static_cast<mz_uint>(pl.entry_index),
                                          bytes.data(), bytes.size(), 0)) {
            ++out.skipped;
            continue;
        }
        std::span<const uint8_t> span{bytes.data(), bytes.size()};

        vault::VaultResult r;
        if (image::detect_format(span) != image::ImageFormat::Unknown)
            r = v.add_image(pl.gallery_path, span, pl.filename);
        else
            r = v.add_video(pl.gallery_path, span, pl.filename);

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
    return out;
}

}  // namespace ui
