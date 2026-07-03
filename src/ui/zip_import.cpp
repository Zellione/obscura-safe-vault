#include "ui/zip_import.h"

#include "crypto/secure_mem.h"
#include "image/format_registry.h"
#include "vault/vault.h"

#include "miniz.h"

#include <cstring>
#include <fstream>
#include <print>
#include <vector>

namespace ui {
namespace {

// Read the whole archive file into memory. The archive is the user's own
// on-disk file; only the *decompressed* entry bytes are sensitive (those go to
// mlock'd SecureBytes). Returns false if the file is missing or empty.
bool read_archive(const std::filesystem::path& path, std::vector<uint8_t>& out)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamoff sz = f.tellg();
    if (sz <= 0) return false;
    out.resize(static_cast<size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(out.data()), sz);
    return true;
}

// Snapshot the archive's central directory as a list of (path, is_dir) entries.
std::vector<ZipEntry> read_entry_list(mz_zip_archive& zip)
{
    std::vector<ZipEntry> entries;
    const mz_uint n = mz_zip_reader_get_num_files(&zip);
    entries.reserve(n);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        entries.emplace_back(std::string(st.m_filename),
                             mz_zip_reader_is_file_a_directory(&zip, i) != MZ_FALSE);
    }
    return entries;
}

// Extract one planned entry into mlock'd memory and store it in the vault.
// Tallies the result into `out`. Decompressed bytes never touch disk.
void import_one(vault::Vault& v, mz_zip_archive& zip, const ZipPlacement& pl, ZipImportOutcome& out)
{
    using enum vault::VaultResult;

    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&zip, static_cast<mz_uint>(pl.entry_index), &st)) {
        ++out.skipped;
        return;
    }

    crypto::SecureBytes bytes(static_cast<size_t>(st.m_uncomp_size));  // mlock'd; wiped on scope exit
    if (!mz_zip_reader_extract_to_mem(&zip, static_cast<mz_uint>(pl.entry_index),
                                      bytes.data(), bytes.size(), 0)) {
        ++out.skipped;
        return;
    }
    const std::span<const uint8_t> span{bytes.data(), bytes.size()};

    const vault::VaultResult r = image::detect_format(span) != image::ImageFormat::Unknown
                                     ? v.add_image(pl.gallery_path, span, pl.filename)
                                     : v.add_video(pl.gallery_path, span, pl.filename);
    switch (r) {
        case Ok:
            ++out.imported;
            break;
        case AlreadyExists:
        case InvalidArg:
        case BadFormat:  // unsupported / duplicate content
            ++out.skipped;
            break;
        default:
            if (out.error.empty()) out.error = "Import error on " + pl.filename;
            ++out.skipped;
            break;
    }
}

// Read `path` into memory and init a miniz reader over it (the portable
// in-memory path; `archive` must outlive every read — init_mem borrows it). On
// failure there is nothing to mz_zip_reader_end(); sets out.error and returns false.
bool open_archive(const std::filesystem::path& path, const char* tag,
                  std::vector<uint8_t>& archive, mz_zip_archive& zip, ZipImportOutcome& out)
{
    std::memset(&zip, 0, sizeof(zip));
    if (read_archive(path, archive) &&
        mz_zip_reader_init_mem(&zip, archive.data(), archive.size(), 0))
        return true;
    out.error = "Could not open archive";
    std::println(stderr, "[{}] open failed: {}", tag, path.string());
    return false;
}

// Create plan.galleries (parents already ordered first by the planner). On a
// hard failure ends `zip`, sets out.error, and returns false.
bool create_galleries(vault::Vault& v, const ZipPlan& plan, mz_zip_archive& zip, ZipImportOutcome& out)
{
    for (const auto& g : plan.galleries) {
        const vault::VaultResult r = v.create_gallery(g);
        if (r != vault::VaultResult::Ok && r != vault::VaultResult::AlreadyExists) {
            out.error = "Could not create gallery: " + g;
            mz_zip_reader_end(&zip);
            return false;
        }
    }
    return true;
}

// Store every planned placement, then end `zip` and mark the outcome ok. When
// `progress` is set, publishes the page count up front and bumps `done` per page
// so a background poller can draw a bar; `cancel` cooperatively stops between
// pages (already-stored pages remain — the vault is append-only).
void run_placements(vault::Vault& v, mz_zip_archive& zip, const ZipPlan& plan,
                    ZipImportOutcome& out, ImportProgress* progress)
{
    if (progress) progress->total.store(static_cast<int>(plan.placements.size()));
    for (const auto& pl : plan.placements) {
        if (progress && progress->cancel.load()) {
            out.cancelled = true;  // user pressed Esc during import (Phase 26)
            break;
        }
        import_one(v, zip, pl, out);
        if (progress) progress->done.fetch_add(1);
    }
    mz_zip_reader_end(&zip);
    out.ok = true;
}

} // namespace

ZipImportOutcome import_zip(vault::Vault&                v,
                            const std::filesystem::path& zip_path,
                            ZipDest                      dest,
                            std::string_view             base_gallery,
                            std::string_view             new_gallery_name,
                            ZipConflictPolicy            policy,
                            ImportProgress*              progress)
{
    ZipImportOutcome out;
    std::vector<uint8_t> archive;
    mz_zip_archive zip;
    if (!open_archive(zip_path, "ZipImport", archive, zip, out)) return out;

    ZipPlan plan = build_zip_plan(read_entry_list(zip), dest, base_gallery, new_gallery_name, policy);
    if (plan.needs_resolution) {
        out.ok = true;
        out.needs_resolution = true;
        out.mixed_dirs = std::move(plan.mixed_dirs);
        mz_zip_reader_end(&zip);
        return out;
    }
    out.skipped = plan.skipped_unsupported;

    if (!create_galleries(v, plan, zip, out)) return out;
    run_placements(v, zip, plan, out, progress);
    return out;
}

ZipImportOutcome import_cbz(vault::Vault&                v,
                            const std::filesystem::path& cbz_path,
                            std::string_view             base_gallery,
                            std::string_view             gallery_name,
                            ImportProgress*              progress)
{
    // A .cbz is a plain zip; build_cbz_plan emits one leaf gallery of pages.
    // import_one then decompresses each page into mlock'd memory — never to disk.
    ZipImportOutcome out;
    std::vector<uint8_t> archive;
    mz_zip_archive zip;
    if (!open_archive(cbz_path, "CbzImport", archive, zip, out)) return out;

    const ZipPlan plan = build_cbz_plan(read_entry_list(zip), base_gallery, gallery_name);
    out.skipped = plan.skipped_unsupported;

    if (!create_galleries(v, plan, zip, out)) return out;
    run_placements(v, zip, plan, out, progress);
    return out;
}

}  // namespace ui
