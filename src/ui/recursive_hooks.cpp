#include "ui/recursive_hooks.h"

#include "image/format_registry.h"
#include "ui/archive_reader.h"
#include "ui/media_sink.h"
#include "ui/zip_encoding.h"

#include "miniz.h"

#include <cstring>
#include <string>

namespace ui {
namespace {

// Bit 11 of a zip entry's flags ("language encoding flag"): set => the name is
// UTF-8. miniz keeps the constant private, so it is restated here exactly as
// zip_import.cpp does.
constexpr mz_uint16 kZipUtf8BitFlag = 1U << 11;

[[nodiscard]] bool kind_is_zip(ArchiveKind k)
{
    return k == ArchiveKind::Zip || k == ArchiveKind::Cbz;
}

// --- miniz path (zip / cbz) -------------------------------------------------

bool zip_list(std::span<const uint8_t> bytes, std::vector<ZipEntry>& out)
{
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0) == MZ_FALSE) {
        return false;
    }
    const mz_uint n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (mz_zip_reader_file_stat(&zip, i, &st) == MZ_FALSE) {
            continue;
        }
        const bool utf8 = (st.m_bit_flag & kZipUtf8BitFlag) != 0;
        out.emplace_back(decode_zip_entry_name(st.m_filename, utf8),
                         mz_zip_reader_is_file_a_directory(&zip, i) != MZ_FALSE);
    }
    mz_zip_reader_end(&zip);
    return true;
}

bool zip_extract(std::span<const uint8_t> bytes, std::size_t index, crypto::SecureBytes& out)
{
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0) == MZ_FALSE) {
        return false;
    }
    bool ok = false;
    if (mz_zip_archive_file_stat st;
        mz_zip_reader_file_stat(&zip, static_cast<mz_uint>(index), &st) != MZ_FALSE &&
        out.resize(static_cast<size_t>(st.m_uncomp_size))) {
        ok = out.empty() || mz_zip_reader_extract_to_mem(&zip, static_cast<mz_uint>(index),
                                                         out.data(), out.size(), 0) != MZ_FALSE;
    }
    mz_zip_reader_end(&zip);
    return ok;
}

// --- libarchive path (7z / rar / tar) ---------------------------------------
//
// These hooks are stateless by design (span in, bytes out), so each call
// builds a fresh reader. That forfeits ArchiveReader's forward stream cursor
// (which only pays off across extracts on ONE reader) — acceptable here
// because nested-archive candidates are rare, and the recursive walk extracts
// the nested archive once and then imports from its in-memory bytes.

bool arc_list(std::span<const uint8_t> bytes, std::string_view password,
              std::vector<ZipEntry>& out)
{
    ArchiveReader reader;
    if (!reader.open(bytes, password)) {
        return false;
    }
    out = reader.entries();
    return true;
}

bool arc_extract(std::span<const uint8_t> bytes, std::string_view password, std::size_t index,
                 crypto::SecureBytes& out)
{
    ArchiveReader reader;
    if (!reader.open(bytes, password)) {
        return false;
    }
    return reader.extract(index, out);
}

// walk_archive works in ABSOLUTE vault paths — a nested sub-gallery's path is
// only known mid-walk — while MediaSink takes paths relative to the import
// root. Strip the root prefix, exactly as the flat importer does inline.
[[nodiscard]] std::string_view relative_to(std::string_view absolute, std::string_view root)
{
    if (root.empty() || absolute == root) {
        return absolute == root ? std::string_view{} : absolute;
    }
    if (absolute.size() > root.size() && absolute.starts_with(root) &&
        absolute[root.size()] == '/') {
        return absolute.substr(root.size() + 1);
    }
    return absolute;
}

// A read-only view over a SecurePassword's bytes for the arc backends, which
// take a string_view. Empty (or a resize failure) yields an empty view.
std::string_view pw_view(const SecurePassword& pw)
{
    if (!pw || pw->size() == 0) {
        return {};
    }
    return {reinterpret_cast<const char*>(pw->data()), pw->size()};
}

} // namespace

SecurePassword make_secure_password(std::string_view password)
{
    auto pw = std::make_shared<crypto::SecureBytes>();
    if (!password.empty() && pw->resize(password.size())) {
        std::memcpy(pw->data(), password.data(), password.size());
    }
    return pw;
}

RecursiveHooks make_recursive_hooks(MediaSink& sink, std::string_view root_gallery,
                                    std::string_view password)
{
    const std::string root(root_gallery);
    // The password lives in a shared, wiping SecureBytes rather than a plain
    // std::string: the hook closures outlive this call and are the only owners,
    // so the plaintext is crypto_wipe'd (never left in freed heap) once the last
    // closure is destroyed — invariant #2. Shared so both closures hold one copy.
    const SecurePassword pw = make_secure_password(password);

    RecursiveHooks h;

    h.list_entries = [pw](std::span<const uint8_t> bytes, ArchiveKind kind,
                          std::vector<ZipEntry>& out) {
        return kind_is_zip(kind) ? zip_list(bytes, out) : arc_list(bytes, pw_view(pw), out);
    };

    h.extract_entry = [pw](std::span<const uint8_t> bytes, ArchiveKind kind, std::size_t index,
                           crypto::SecureBytes& out) {
        return kind_is_zip(kind) ? zip_extract(bytes, index, out)
                                 : arc_extract(bytes, pw_view(pw), index, out);
    };

    h.create_gallery = [&sink, root](std::string_view gallery) {
        // Idempotent: the destination gallery already exists, and a plan lists
        // every ancestor of every placement, so AlreadyExists is the NORMAL
        // outcome — not an error. Treating it as failure aborts the whole walk
        // on the very first call.
        const vault::VaultResult r = sink.ensure_gallery(relative_to(gallery, root));
        return r == vault::VaultResult::Ok || r == vault::VaultResult::AlreadyExists;
    };

    h.place_media = [&sink, root](std::string_view gallery, std::string_view filename,
                                  std::span<const uint8_t> data) {
        // Same discrimination the flat importer uses: anything the image
        // decoders can identify is an image, everything else goes to video.
        const std::string_view   rel = relative_to(gallery, root);
        const vault::VaultResult r   = image::detect_format(data) != image::ImageFormat::Unknown
                                           ? sink.place_image(rel, data, filename)
                                           : sink.place_video(rel, data, filename);
        return r == vault::VaultResult::Ok;
    };

    h.tag_gallery = [&sink, root](std::string_view gallery, std::string_view tag) {
        return sink.tag_gallery(relative_to(gallery, root), tag) == vault::VaultResult::Ok;
    };

    h.cancelled = [&sink] { return sink.cancelled(); };

    return h;
}

} // namespace ui
