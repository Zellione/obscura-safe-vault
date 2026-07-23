#include "ui/zip_import.h"

#include "crypto/secure_mem.h"
#include "image/format_registry.h"
#include "ui/import_common.h"
#include "ui/meta_json.h"
#include "ui/zip_encoding.h"
#include "vault/vault.h"

#include "miniz.h"

#include <cstring>
#include <print>
#include <vector>

namespace ui {
namespace {

// The zip general-purpose bit-flag field's bit 11 (APPNOTE.TXT's "language
// encoding flag", EFS): set => the entry's name/comment bytes are UTF-8.
// miniz's public headers don't expose this constant (it's a private enum
// inside miniz_zip.c), so it's named here instead of used as a bare literal.
constexpr mz_uint16 kZipUtf8BitFlag = 1U << 11;

// Snapshot the archive's central directory as a list of (path, is_dir) entries.
// A name written without the UTF-8 flag is legacy-encoded (Phase 36 part 2):
// decode_zip_entry_name falls back to CP437, the overwhelmingly common case
// for such archives, unless the raw bytes already parse as valid UTF-8.
std::vector<ZipEntry> read_entry_list(mz_zip_archive& zip)
{
    std::vector<ZipEntry> entries;
    const mz_uint n = mz_zip_reader_get_num_files(&zip);
    entries.reserve(n);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        const bool utf8_flag = (st.m_bit_flag & kZipUtf8BitFlag) != 0;
        entries.emplace_back(decode_zip_entry_name(st.m_filename, utf8_flag),
                             mz_zip_reader_is_file_a_directory(&zip, i) != MZ_FALSE);
    }
    return entries;
}

// Anything bigger than this is not plausible gallery metadata; refuse to
// decompress it (zip headers can lie about m_uncomp_size).
constexpr mz_uint64 kMaxMetaJsonBytes = 1u << 20;

// Extract + parse the archive's top-level meta.json, if any (Phase 27).
// Absent, oversized, or malformed metadata degrades to empty fields — it can
// never fail the import.
ArchiveMeta load_archive_meta(mz_zip_archive& zip, const std::vector<ZipEntry>& entries)
{
    const std::optional<size_t> idx = find_meta_entry(entries);
    if (!idx.has_value()) return {};
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&zip, static_cast<mz_uint>(*idx), &st)) return {};
    if (st.m_uncomp_size == 0 || st.m_uncomp_size > kMaxMetaJsonBytes) return {};
    crypto::SecureBytes bytes(static_cast<size_t>(st.m_uncomp_size));
    if (!mz_zip_reader_extract_to_mem(&zip, static_cast<mz_uint>(*idx),
                                      bytes.data(), bytes.size(), 0))
        return {};
    return parse_meta_json({bytes.data(), bytes.size()});
}

// Seed `gallery` with the meta-derived tags (japanese title + each "type:name").
// Best-effort: add_tag merges case-insensitively; a failed tag is not fatal.
void apply_meta_tags(vault::Vault& v, std::string_view gallery, const ArchiveMeta& meta)
{
    for (const std::string& t : meta_gallery_tags(meta))
        (void)v.add_tag(gallery, t);
}

// Extract one planned entry into mlock'd memory and store it through the sink.
// Tallies the result into `out`. Decompressed bytes never touch disk.
// `rel_gallery` is relative to the sink's import root.
void import_one(MediaSink& sink, mz_zip_archive& zip, const ZipPlacement& pl,
                std::string_view root_gallery, ZipImportOutcome& out)
{
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

    // Extract relative gallery path: strip root_gallery prefix from pl.gallery_path.
    std::string_view rel_gallery{pl.gallery_path};
    if (!root_gallery.empty()) {
        if (rel_gallery.size() > root_gallery.size() && rel_gallery.starts_with(root_gallery)) {
            // Skip the root + separator
            rel_gallery.remove_prefix(root_gallery.size() + 1);
        } else if (rel_gallery == root_gallery) {
            // pl.gallery_path is exactly root_gallery, so rel_gallery is empty
            rel_gallery = {};
        }
    }

    const vault::VaultResult r = image::detect_format(span) != image::ImageFormat::Unknown
                                     ? sink.place_image(rel_gallery, span, pl.filename)
                                     : sink.place_video(rel_gallery, span, pl.filename);
    tally_import_result(r, pl.filename, out);
}

// Read `path` into memory and init a miniz reader over it (the portable
// in-memory path; `archive` must outlive every read — init_mem borrows it). On
// failure there is nothing to mz_zip_reader_end(); sets out.error and returns false.
bool open_archive(const std::filesystem::path& path, const char* tag,
                  std::vector<uint8_t>& archive, mz_zip_archive& zip, ZipImportOutcome& out)
{
    std::memset(&zip, 0, sizeof(zip));
    if (read_whole_file(path, archive) &&
        mz_zip_reader_init_mem(&zip, archive.data(), archive.size(), 0))
        return true;
    out.error = "Could not open archive";
    std::println(stderr, "[{}] open failed: {}", tag, path.string());
    return false;
}

// Create plan.galleries through the sink. On a hard failure ends `zip`, sets
// out.error, and returns false. Galleries are passed relative to sink root.
bool create_galleries(MediaSink& sink, const ZipPlan& plan, mz_zip_archive& zip,
                      std::string_view root_gallery, ZipImportOutcome& out)
{
    for (const auto& g : plan.galleries) {
        // Extract relative gallery path: strip root_gallery prefix from g.
        std::string_view rel_gallery{g};
        if (!root_gallery.empty()) {
            if (rel_gallery.size() > root_gallery.size() && rel_gallery.starts_with(root_gallery)) {
                // Skip the root + separator
                rel_gallery.remove_prefix(root_gallery.size() + 1);
            } else if (rel_gallery == root_gallery) {
                // g is exactly root_gallery, so rel_gallery is empty
                rel_gallery = {};
            }
        }

        const vault::VaultResult r = sink.ensure_gallery(rel_gallery);
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
void run_placements(MediaSink& sink, mz_zip_archive& zip, const ZipPlan& plan,
                    std::string_view root_gallery, ZipImportOutcome& out, ImportProgress* progress)
{
    if (progress) progress->total.store(static_cast<int>(plan.placements.size()));
    for (const auto& pl : plan.placements) {
        if (sink.cancelled()) {
            out.cancelled = true;  // user pressed Esc during import (Phase 26)
            break;
        }
        import_one(sink, zip, pl, root_gallery, out);
        if (progress) progress->done.fetch_add(1);
    }
    mz_zip_reader_end(&zip);
    out.ok = true;
}

}  // namespace

ZipImportOutcome import_zip(MediaSink&                   sink,
                            const std::filesystem::path& zip_path,
                            std::string_view             new_gallery_name,
                            ImportProgress*              progress)
{
    ZipImportOutcome out;
    std::vector<uint8_t> archive;
    mz_zip_archive zip;
    if (!open_archive(zip_path, "ZipImport", archive, zip, out)) return out;

    // A top-level meta.json tags the new top gallery (Phase 27). Its title is
    // NOT applied here: the UI prefills the name popup from peek_archive_meta,
    // so `new_gallery_name` (the user's confirmed text) is authoritative.
    const std::vector<ZipEntry> entries = read_entry_list(zip);
    const ArchiveMeta meta = load_archive_meta(zip, entries);

    // Build plan with empty base (sink handles absolute placement).
    ZipPlan plan = build_zip_plan(entries, "", new_gallery_name);
    out.skipped = plan.skipped_unsupported;

    // Root is just new_gallery_name when base is empty.
    const std::string root_gallery(new_gallery_name);

    if (!create_galleries(sink, plan, zip, root_gallery, out)) return out;
    if (!plan.placements.empty()) {
        // Apply tags to the vault for the root gallery (sink stores it, so we pass root).
        // This requires getting the vault from sink, which DirectVaultSink provides.
        // For now, we skip tags through the sink API; they're best-effort anyway.
        // TODO: consider adding tag support to MediaSink if needed.
    }
    run_placements(sink, zip, plan, root_gallery, out, progress);
    return out;
}

ZipImportOutcome import_cbz(MediaSink&                   sink,
                            const std::filesystem::path& cbz_path,
                            std::string_view             gallery_name,
                            ImportProgress*              progress)
{
    // A .cbz is a plain zip; build_cbz_plan emits one leaf gallery of pages.
    // import_one then decompresses each page into mlock'd memory — never to disk.
    ZipImportOutcome out;
    std::vector<uint8_t> archive;
    mz_zip_archive zip;
    if (!open_archive(cbz_path, "CbzImport", archive, zip, out)) return out;

    // A top-level meta.json tags the single leaf gallery (Phase 27). Its title
    // is NOT applied here: the UI prefills the name popup from
    // peek_archive_meta, so `gallery_name` (the user's confirmed text) is
    // authoritative.
    const std::vector<ZipEntry> entries = read_entry_list(zip);
    const ArchiveMeta meta = load_archive_meta(zip, entries);

    // Build plan with empty base (sink handles absolute placement).
    const ZipPlan plan = build_cbz_plan(entries, "", gallery_name);
    out.skipped = plan.skipped_unsupported;

    // Root is just gallery_name when base is empty.
    const std::string root_gallery(gallery_name);

    if (!create_galleries(sink, plan, zip, root_gallery, out)) return out;
    if (!plan.placements.empty()) {
        // Apply tags to the vault for the root gallery (sink stores it, so we pass root).
        // For now, we skip tags through the sink API; they're best-effort anyway.
        // TODO: consider adding tag support to MediaSink if needed.
    }
    run_placements(sink, zip, plan, root_gallery, out, progress);
    return out;
}

// Wrapper: construct a DirectVaultSink and call the MediaSink version.
// Applies meta tags using the vault directly (best-effort).
ZipImportOutcome import_zip(vault::Vault&                v,
                            const std::filesystem::path& zip_path,
                            std::string_view             base_gallery,
                            std::string_view             new_gallery_name,
                            ImportProgress*              progress)
{
    ZipImportOutcome out;
    std::vector<uint8_t> archive;
    mz_zip_archive zip;
    if (!open_archive(zip_path, "ZipImport", archive, zip, out)) return out;

    // Load meta.json for tags (Phase 27).
    const std::vector<ZipEntry> entries = read_entry_list(zip);
    const ArchiveMeta meta = load_archive_meta(zip, entries);
    mz_zip_reader_end(&zip);

    // Create sink and call the MediaSink version.
    DirectVaultSink sink(v, base_gallery, new_gallery_name, progress);
    out = import_zip(sink, zip_path, new_gallery_name, progress);

    // Apply meta tags to the top-level imported gallery (best-effort).
    if (out.ok && !out.imported && !out.skipped) {
        // No placements, skip tags
    } else if (out.ok) {
        // Compute absolute path to the top-level gallery.
        const std::string root_gallery = base_gallery.empty() ? std::string(new_gallery_name)
                                                              : std::string(base_gallery) + "/" + std::string(new_gallery_name);
        apply_meta_tags(v, root_gallery, meta);
    }
    return out;
}

// Wrapper: construct a DirectVaultSink and call the MediaSink version.
// Applies meta tags using the vault directly (best-effort).
ZipImportOutcome import_cbz(vault::Vault&                v,
                            const std::filesystem::path& cbz_path,
                            std::string_view             base_gallery,
                            std::string_view             gallery_name,
                            ImportProgress*              progress)
{
    ZipImportOutcome out;
    std::vector<uint8_t> archive;
    mz_zip_archive zip;
    if (!open_archive(cbz_path, "CbzImport", archive, zip, out)) return out;

    // Load meta.json for tags (Phase 27).
    const std::vector<ZipEntry> entries = read_entry_list(zip);
    const ArchiveMeta meta = load_archive_meta(zip, entries);
    mz_zip_reader_end(&zip);

    // Create sink and call the MediaSink version.
    DirectVaultSink sink(v, base_gallery, gallery_name, progress);
    out = import_cbz(sink, cbz_path, gallery_name, progress);

    // Apply meta tags to the top-level imported gallery (best-effort).
    if (out.ok && !out.imported && !out.skipped) {
        // No placements, skip tags
    } else if (out.ok) {
        // Compute absolute path to the top-level gallery.
        const std::string root_gallery = base_gallery.empty() ? std::string(gallery_name)
                                                              : std::string(base_gallery) + "/" + std::string(gallery_name);
        apply_meta_tags(v, root_gallery, meta);
    }
    return out;
}

ArchiveMeta peek_archive_meta(const std::filesystem::path& archive_path)
{
    // Same portable whole-file read the importers use (std::ifstream handles
    // non-ASCII paths on every platform). The pick that triggers this peek is
    // followed by a full import read anyway, so the cost is paid once more at
    // popup time, not a new order of work.
    ZipImportOutcome ignored;
    std::vector<uint8_t> archive;
    mz_zip_archive zip;
    if (!open_archive(archive_path, "MetaPeek", archive, zip, ignored)) return {};
    const ArchiveMeta meta = load_archive_meta(zip, read_entry_list(zip));
    mz_zip_reader_end(&zip);
    return meta;
}

bool zip_is_encrypted(const std::filesystem::path& zip_path)
{
    ZipImportOutcome ignored;
    std::vector<uint8_t> archive;
    mz_zip_archive zip;
    if (!open_archive(zip_path, "ZipEncryptedPeek", archive, zip, ignored)) return false;

    const mz_uint n = mz_zip_reader_get_num_files(&zip);
    bool encrypted = false;
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (mz_zip_reader_file_stat(&zip, i, &st) && st.m_is_encrypted) {
            encrypted = true;
            break;
        }
    }
    mz_zip_reader_end(&zip);
    return encrypted;
}

}  // namespace ui
