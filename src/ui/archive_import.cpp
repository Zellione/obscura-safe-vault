#include "ui/archive_import.h"

#ifdef OSV_VENDORED_ARCHIVE

#include "crypto/secure_mem.h"
#include "image/format_registry.h"
#include "ui/archive_reader.h"
#include "vault/vault.h"

#include <fstream>
#include <print>
#include <vector>

namespace ui {
namespace {

// Read the whole archive file into memory (the archive is the user's own
// on-disk file; only the *decompressed* entry bytes are sensitive — those go
// to mlock'd SecureBytes). Returns false if the file is missing or empty.
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

// Read `path` into memory and open an ArchiveReader over it. On failure sets
// out.error and returns false — mirrors zip_import.cpp's open_archive.
bool open_archive(const std::filesystem::path& path, const char* tag,
                  ArchiveReader& reader, ZipImportOutcome& out)
{
    std::vector<uint8_t> bytes;
    if (read_archive(path, bytes) && reader.open(bytes)) return true;
    out.error = "Could not open archive";
    std::println(stderr, "[{}] open failed: {}", tag, path.string());
    return false;
}

// Extract one planned entry into mlock'd memory and store it in the vault.
// Tallies the result into `out`. Decompressed bytes never touch disk.
void import_one(vault::Vault& v, const ArchiveReader& reader, const ZipPlacement& pl,
                ZipImportOutcome& out)
{
    using enum vault::VaultResult;

    crypto::SecureBytes bytes;  // mlock'd; wiped on scope exit
    if (!reader.extract(pl.entry_index, bytes)) {
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

// Create plan.galleries (parents already ordered first by the planner). On a
// hard failure sets out.error and returns false.
bool create_galleries(vault::Vault& v, const ZipPlan& plan, ZipImportOutcome& out)
{
    for (const auto& g : plan.galleries) {
        const vault::VaultResult r = v.create_gallery(g);
        if (r != vault::VaultResult::Ok && r != vault::VaultResult::AlreadyExists) {
            out.error = "Could not create gallery: " + g;
            return false;
        }
    }
    return true;
}

// Store every planned placement and mark the outcome ok. When `progress` is
// set, publishes the page count up front and bumps `done` per page so a
// background poller can draw a bar; `cancel` cooperatively stops between
// pages (already-stored pages remain — the vault is append-only).
void run_placements(vault::Vault& v, const ArchiveReader& reader, const ZipPlan& plan,
                    ZipImportOutcome& out, ImportProgress* progress)
{
    if (progress) progress->total.store(static_cast<int>(plan.placements.size()));
    for (const auto& pl : plan.placements) {
        if (progress && progress->cancel.load()) {
            out.cancelled = true;  // user pressed Esc during import
            break;
        }
        import_one(v, reader, pl, out);
        if (progress) progress->done.fetch_add(1);
    }
    out.ok = true;
}

} // namespace

ZipImportOutcome import_archive(vault::Vault&                v,
                                const std::filesystem::path& archive_path,
                                ZipDest                      dest,
                                std::string_view             base_gallery,
                                std::string_view             new_gallery_name,
                                ZipConflictPolicy            policy,
                                ImportProgress*              progress)
{
    ZipImportOutcome out;
    ArchiveReader reader;
    if (!open_archive(archive_path, "ArchiveImport", reader, out)) return out;

    ZipPlan plan = build_zip_plan(reader.entries(), dest, base_gallery, new_gallery_name, policy);
    if (plan.needs_resolution) {
        out.ok = true;
        out.needs_resolution = true;
        out.mixed_dirs = std::move(plan.mixed_dirs);
        return out;
    }
    out.skipped = plan.skipped_unsupported;

    if (!create_galleries(v, plan, out)) return out;
    run_placements(v, reader, plan, out, progress);
    return out;
}

ZipImportOutcome import_archive_cbz(vault::Vault&                v,
                                    const std::filesystem::path& archive_path,
                                    std::string_view             base_gallery,
                                    std::string_view             gallery_name,
                                    ImportProgress*              progress)
{
    ZipImportOutcome out;
    ArchiveReader reader;
    if (!open_archive(archive_path, "ArchiveCbzImport", reader, out)) return out;

    const ZipPlan plan = build_cbz_plan(reader.entries(), base_gallery, gallery_name);
    out.skipped = plan.skipped_unsupported;

    if (!create_galleries(v, plan, out)) return out;
    run_placements(v, reader, plan, out, progress);
    return out;
}

} // namespace ui

#else  // !OSV_VENDORED_ARCHIVE — 7z/RAR/TAR support unavailable in this build.

namespace ui {

namespace {
ZipImportOutcome unsupported()
{
    ZipImportOutcome out;
    out.error = "Archive support (7z/RAR/TAR) is not available in this build.";
    return out;
}
} // namespace

ZipImportOutcome import_archive(vault::Vault&, const std::filesystem::path&, ZipDest,
                                std::string_view, std::string_view, ZipConflictPolicy,
                                ImportProgress*)
{
    return unsupported();
}

ZipImportOutcome import_archive_cbz(vault::Vault&, const std::filesystem::path&, std::string_view,
                                    std::string_view, ImportProgress*)
{
    return unsupported();
}

} // namespace ui

#endif // OSV_VENDORED_ARCHIVE
