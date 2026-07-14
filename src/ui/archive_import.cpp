#include "ui/archive_import.h"

#ifdef OSV_VENDORED_ARCHIVE

#include "crypto/secure_mem.h"
#include "image/format_registry.h"
#include "ui/archive_reader.h"
#include "ui/import_common.h"
#include "vault/vault.h"

#include <print>
#include <vector>

namespace ui {
namespace {

// Read `path` into memory and open an ArchiveReader over it. On failure sets
// out.error and returns false — mirrors zip_import.cpp's open_archive.
bool open_archive(const std::filesystem::path& path, const char* tag,
                  ArchiveReader& reader, ZipImportOutcome& out,
                  bool password_protected, std::string_view password)
{
    if (std::vector<uint8_t> bytes;
        !read_whole_file(path, bytes) || !reader.open(bytes, password)) {
        out.error = "Could not open archive";
        std::println(stderr, "[{}] open failed: {}", tag, path.string());
        return false;
    }
    if (!password_protected) return true;

    // Verify the password against the first real entry before touching the
    // vault at all — never a silent partial/unencrypted import (Phase 35).
    const auto& entries = reader.entries();
    size_t first = entries.size();
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!entries[i].is_dir) { first = i; break; }
    }
    // A degenerate all-directories encrypted archive has nothing to verify
    // against — reader.open() with this password already succeeded (headers
    // aren't encrypted for ZipCrypto), so don't ask for a password again;
    // there's no way this could ever come back as "correct" via a probe that
    // doesn't exist (code-review fix — this would otherwise be an infinite
    // re-prompt loop for a password that's already known to be right).
    if (first >= entries.size()) return true;

    if (crypto::SecureBytes probe; reader.extract(first, probe)) return true;

    // Only ask for a (re)try when the failure was actually a password
    // problem (ArchiveReader::extract_failed_needs_password, backed by
    // archive_error_is_passphrase_issue in Task 3). Anything else — most
    // notably a WinZip-AES-encrypted entry, which this build's libarchive
    // can never decrypt regardless of passphrase since no crypto backend is
    // compiled in — must fall through to the ordinary generic-error path
    // instead, or the UI would loop forever re-prompting for a password that
    // could never work (code-review fix).
    if (!reader.extract_failed_needs_password()) {
        out.error = "Could not import archive";
        return false;
    }

    out.ok            = true;
    out.needs_password = true;
    return false;
}

// Extract one planned entry into mlock'd memory and store it in the vault.
// Tallies the result into `out`. Decompressed bytes never touch disk.
void import_one(vault::Vault& v, const ArchiveReader& reader, const ZipPlacement& pl,
                ZipImportOutcome& out)
{
    crypto::SecureBytes bytes;  // mlock'd; wiped on scope exit
    if (!reader.extract(pl.entry_index, bytes)) {
        ++out.skipped;
        return;
    }
    const std::span<const uint8_t> span{bytes.data(), bytes.size()};

    const vault::VaultResult r = image::detect_format(span) != image::ImageFormat::Unknown
                                     ? v.add_image(pl.gallery_path, span, pl.filename)
                                     : v.add_video(pl.gallery_path, span, pl.filename);
    tally_import_result(r, pl.filename, out);
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

// Shared tail of both import_archive and import_archive_cbz once a plan is
// ready: create the galleries it needs, then store every placement. Factored
// out because the two callers would otherwise duplicate this sequence.
ZipImportOutcome finish_import(vault::Vault& v, const ArchiveReader& reader, const ZipPlan& plan,
                               ImportProgress* progress)
{
    ZipImportOutcome out;
    out.skipped = plan.skipped_unsupported;
    if (!create_galleries(v, plan, out)) return out;
    run_placements(v, reader, plan, out, progress);
    return out;
}

} // namespace

ZipImportOutcome import_archive(vault::Vault&                v,
                                const std::filesystem::path& archive_path,
                                ZipDestination                dest,
                                ImportProgress*              progress,
                                ArchivePassword              pw)
{
    ZipImportOutcome out;
    ArchiveReader reader;
    if (!open_archive(archive_path, "ArchiveImport", reader, out, pw.password_protected, pw.password))
        return out;

    ZipPlan plan = build_zip_plan(reader.entries(), dest.dest, dest.base_gallery,
                                  dest.new_gallery_name, dest.policy);
    if (plan.needs_resolution) {
        out.ok = true;
        out.needs_resolution = true;
        out.mixed_dirs = std::move(plan.mixed_dirs);
        return out;
    }
    return finish_import(v, reader, plan, progress);
}

ZipImportOutcome import_archive_cbz(vault::Vault&                v,
                                    const std::filesystem::path& archive_path,
                                    std::string_view             base_gallery,
                                    std::string_view             gallery_name,
                                    ImportProgress*              progress,
                                    ArchivePassword              pw)
{
    ZipImportOutcome out;
    ArchiveReader reader;
    if (!open_archive(archive_path, "ArchiveCbzImport", reader, out, pw.password_protected, pw.password))
        return out;

    const ZipPlan plan = build_cbz_plan(reader.entries(), base_gallery, gallery_name);
    return finish_import(v, reader, plan, progress);
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

ZipImportOutcome import_archive(vault::Vault&, const std::filesystem::path&, ZipDestination,
                                ImportProgress*, ArchivePassword)
{
    return unsupported();
}

ZipImportOutcome import_archive_cbz(vault::Vault&, const std::filesystem::path&, std::string_view,
                                    std::string_view, ImportProgress*, ArchivePassword)
{
    return unsupported();
}

} // namespace ui

#endif // OSV_VENDORED_ARCHIVE
