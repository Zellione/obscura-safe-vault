#include "ui/archive_import.h"

#ifdef OSV_VENDORED_ARCHIVE

#include "crypto/secure_mem.h"
#include "image/format_registry.h"
#include "ui/archive_reader.h"
#include "ui/import_common.h"

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

// Extract one planned entry into mlock'd memory and store it through the sink.
// Tallies the result into `out`. Decompressed bytes never touch disk.
// `rel_gallery` is relative to the sink's import root.
void import_one(MediaSink& sink, const ArchiveReader& reader, const ZipPlacement& pl,
                std::string_view root_gallery, ZipImportOutcome& out)
{
    crypto::SecureBytes bytes;  // mlock'd; wiped on scope exit
    if (!reader.extract(pl.entry_index, bytes)) {
        ++out.skipped;
        return;
    }
    const std::span<const uint8_t> span{bytes.data(), bytes.size()};

    // Extract relative gallery path: strip root_gallery prefix from pl.gallery_path.
    std::string_view rel_gallery = pl.gallery_path;
    if (!root_gallery.empty() && pl.gallery_path.size() > root_gallery.size()) {
        if (pl.gallery_path.substr(0, root_gallery.size()) == root_gallery) {
            // Skip the root + separator
            rel_gallery = pl.gallery_path.substr(root_gallery.size() + 1);
        }
    } else if (!root_gallery.empty()) {
        // pl.gallery_path is exactly root_gallery, so rel_gallery is empty
        rel_gallery = "";
    }

    const vault::VaultResult r = image::detect_format(span) != image::ImageFormat::Unknown
                                     ? sink.place_image(rel_gallery, span, pl.filename)
                                     : sink.place_video(rel_gallery, span, pl.filename);
    tally_import_result(r, pl.filename, out);
}

// Create plan.galleries through the sink. On a hard failure sets out.error
// and returns false. Galleries are passed relative to sink root.
bool create_galleries(MediaSink& sink, const ZipPlan& plan, std::string_view root_gallery,
                      ZipImportOutcome& out)
{
    for (const auto& g : plan.galleries) {
        // Extract relative gallery path: strip root_gallery prefix from g.
        std::string_view rel_gallery = g;
        if (!root_gallery.empty() && g.size() > root_gallery.size()) {
            if (g.substr(0, root_gallery.size()) == root_gallery) {
                // Skip the root + separator
                rel_gallery = g.substr(root_gallery.size() + 1);
            }
        } else if (!root_gallery.empty()) {
            // g is exactly root_gallery, so rel_gallery is empty
            rel_gallery = "";
        }

        const vault::VaultResult r = sink.ensure_gallery(rel_gallery);
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
void run_placements(MediaSink& sink, const ArchiveReader& reader, const ZipPlan& plan,
                    std::string_view root_gallery, ZipImportOutcome& out, ImportProgress* progress)
{
    if (progress) progress->total.store(static_cast<int>(plan.placements.size()));
    for (const auto& pl : plan.placements) {
        if (sink.cancelled()) {
            out.cancelled = true;  // user pressed Esc during import
            break;
        }
        import_one(sink, reader, pl, root_gallery, out);
        if (progress) progress->done.fetch_add(1);
    }
    out.ok = true;
}

}  // namespace

ZipImportOutcome import_archive(MediaSink&                   sink,
                                const std::filesystem::path& archive_path,
                                std::string_view             new_gallery_name,
                                ImportProgress*              progress,
                                ArchivePassword              pw)
{
    ZipImportOutcome out;
    ArchiveReader reader;
    if (!open_archive(archive_path, "ArchiveImport", reader, out, pw.password_protected, pw.password))
        return out;

    // Build plan with empty base (sink handles absolute placement).
    ZipPlan plan = build_zip_plan(reader.entries(), "", new_gallery_name);
    out.skipped = plan.skipped_unsupported;

    // Root is just new_gallery_name when base is empty.
    const std::string root_gallery(new_gallery_name);

    if (!create_galleries(sink, plan, root_gallery, out)) return out;
    run_placements(sink, reader, plan, root_gallery, out, progress);
    return out;
}

ZipImportOutcome import_archive_cbz(MediaSink&                   sink,
                                    const std::filesystem::path& archive_path,
                                    std::string_view             gallery_name,
                                    ImportProgress*              progress,
                                    ArchivePassword              pw)
{
    ZipImportOutcome out;
    ArchiveReader reader;
    if (!open_archive(archive_path, "ArchiveCbzImport", reader, out, pw.password_protected, pw.password))
        return out;

    // Build plan with empty base (sink handles absolute placement).
    const ZipPlan plan = build_cbz_plan(reader.entries(), "", gallery_name);
    out.skipped = plan.skipped_unsupported;

    // Root is just gallery_name when base is empty.
    const std::string root_gallery(gallery_name);

    if (!create_galleries(sink, plan, root_gallery, out)) return out;
    run_placements(sink, reader, plan, root_gallery, out, progress);
    return out;
}

}  // namespace ui

#else  // !OSV_VENDORED_ARCHIVE — 7z/RAR/TAR support unavailable in this build.

namespace ui {

namespace {
ZipImportOutcome unsupported()
{
    ZipImportOutcome out;
    out.error = "Archive support (7z/RAR/TAR) is not available in this build.";
    return out;
}
}  // namespace

ZipImportOutcome import_archive(MediaSink&, const std::filesystem::path&, std::string_view,
                                ImportProgress*, ArchivePassword)
{
    return unsupported();
}

ZipImportOutcome import_archive_cbz(MediaSink&, const std::filesystem::path&, std::string_view,
                                    ImportProgress*, ArchivePassword)
{
    return unsupported();
}

}  // namespace ui

#endif  // OSV_VENDORED_ARCHIVE

namespace ui {

// Thin wrappers: construct a DirectVaultSink and call the MediaSink versions.
// Preserved for tests and ZipImportJob compatibility.

ZipImportOutcome import_archive(vault::Vault&                v,
                                const std::filesystem::path& archive_path,
                                const ZipDestination&        dest,
                                ImportProgress*              progress,
                                ArchivePassword              pw)
{
    DirectVaultSink sink(v, dest.base_gallery, dest.new_gallery_name, progress);
    return import_archive(sink, archive_path, dest.new_gallery_name, progress, pw);
}

ZipImportOutcome import_archive_cbz(vault::Vault&                v,
                                    const std::filesystem::path& archive_path,
                                    std::string_view             base_gallery,
                                    std::string_view             gallery_name,
                                    ImportProgress*              progress,
                                    ArchivePassword              pw)
{
    DirectVaultSink sink(v, base_gallery, gallery_name, progress);
    return import_archive_cbz(sink, archive_path, gallery_name, progress, pw);
}

}  // namespace ui
