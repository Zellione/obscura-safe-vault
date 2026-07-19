#include "ui/pdf_import.h"

#ifdef OSV_VENDORED_PDFIUM

#include "media/pdf_render.h"
#include "crypto/secure_mem.h"
#include "vault/vault.h"
#include "vault/safe_name.h"
#include "image/format_registry.h"
#include "ui/import_common.h"

#include <print>
#include <filesystem>
#include <fstream>
#include <vector>

namespace ui {

// Read entire file into secure memory (mlock'd, wiped on destruction)
bool read_pdf_file(const std::filesystem::path& path, crypto::SecureBytes& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::println(stderr, "[PdfImport] Cannot open file: {}", path.string());
        return false;
    }
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (!out.resize(size)) {
        std::println(stderr, "[PdfImport] Failed to allocate {} bytes for PDF file", size);
        return false;
    }
    file.read(reinterpret_cast<char*>(out.data()), size);
    return file.good();
}

ImportPlan build_pdf_plan(int page_count,
                          std::string_view base_gallery,
                          std::string_view pdf_filename)
{
    ImportPlan plan;

    // Extract filename without extension
    std::string base = std::filesystem::path(pdf_filename).stem().string();
    // Sanitize for use as gallery name
    std::string safe_name = vault::sanitize_node_name(base);
    if (safe_name.empty()) safe_name = "imported_pdf";

    // Create gallery path
    std::string gallery_path = base_gallery.empty() ? safe_name : std::string(base_gallery) + "/" + safe_name;
    plan.galleries.push_back(gallery_path);

    // Create one placement per page
    for (int i = 0; i < page_count; ++i) {
        ZipPlacement pl{
            .gallery_path = gallery_path,
            .entry_index = static_cast<size_t>(i),
            .filename = "page_" + std::to_string(i + 1) + ".rgba",  // Temporary name for tracking
            .page_number = i,
        };
        plan.placements.push_back(pl);
    }

    return plan;
}

ZipImportOutcome import_pdf(vault::Vault& v,
                            const std::filesystem::path& pdf_path,
                            std::string_view base_gallery,
                            std::string_view gallery_name_hint,
                            ImportProgress* progress)
{
    ZipImportOutcome out;

    // Read PDF file into secure memory (mlock'd, auto-wiped on scope exit)
    crypto::SecureBytes pdf_bytes;
    if (!read_pdf_file(pdf_path, pdf_bytes)) {
        out.error = "Could not open PDF file";
        return out;
    }

    if (pdf_bytes.empty()) {
        out.error = "PDF file is empty";
        return out;
    }

    // Load PDF document
    media::PdfDocument doc;
    if (!doc.open(pdf_bytes)) {
        out.error = "Could not parse PDF file";
        std::println(stderr, "[PdfImport] PDF parsing failed: {}", pdf_path.string());
        return out;
    }

    int num_pages = doc.page_count();
    if (num_pages <= 0) {
        out.error = "PDF has no pages";
        return out;
    }

    // Build import plan
    std::string gallery_name = gallery_name_hint.empty()
        ? std::filesystem::path(pdf_path).stem().string()
        : std::string(gallery_name_hint);
    gallery_name = vault::sanitize_node_name(gallery_name);
    if (gallery_name.empty()) gallery_name = "imported_pdf";

    ImportPlan plan = build_pdf_plan(num_pages, base_gallery, pdf_path.filename().string());

    // Create gallery
    std::string gallery_path = plan.galleries[0];
    vault::VaultResult r = v.create_gallery(gallery_path);
    if (r != vault::VaultResult::Ok && r != vault::VaultResult::AlreadyExists) {
        out.error = "Could not create gallery: " + gallery_path;
        return out;
    }

    // Render and import each page
    if (progress) progress->total.store(num_pages);

    for (int i = 0; i < num_pages; ++i) {
        if (progress && progress->cancel.load()) {
            out.cancelled = true;
            break;
        }

        // Render page to RGBA
        crypto::SecureBytes rgba;
        if (!doc.render_page(i, 150, rgba)) {
            std::println(stderr, "[PdfImport] Failed to render page {}", i);
            ++out.skipped;
            if (progress) progress->done.fetch_add(1);
            continue;
        }

        // Add as image (RGBA format always detected as image)
        std::string page_name = "page_" + std::to_string(i + 1);
        vault::VaultResult add_r = v.add_image(gallery_path, std::span<const uint8_t>(rgba.data(), rgba.size()), page_name);
        if (add_r != vault::VaultResult::Ok) {
            std::println(stderr, "[PdfImport] Failed to add page {} to vault", i);
            ++out.skipped;
        }

        if (progress) progress->done.fetch_add(1);
    }

    out.ok = true;
    return out;
}

} // namespace ui

#endif // OSV_VENDORED_PDFIUM
