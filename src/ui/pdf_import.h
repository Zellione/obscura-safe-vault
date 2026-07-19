#pragma once

#ifdef OSV_VENDORED_PDFIUM

#include "ui/zip_plan.h"  // Reuse ZipImportOutcome, ZipPlacement types
#include "vault/vault.h"
#include "ui/zip_import.h"  // For ImportProgress

#include <filesystem>
#include <string_view>
#include <vector>

namespace ui {

// Pure plan builder: generates a gallery structure and page placements
// for a PDF with `page_count` pages. Gallery name is sanitized from `pdf_filename`.
struct ImportPlan {
    std::vector<std::string> galleries;      // Leaf galleries to create
    std::vector<ZipPlacement> placements;    // One placement per page
};

// Build import plan for a PDF document
[[nodiscard]] ImportPlan build_pdf_plan(int page_count,
                                        std::string_view base_gallery,
                                        std::string_view pdf_filename);

// Import a PDF file as a gallery of page images
// - Reads file into mlock'd buffer (never to disk)
// - Renders each page to RGBA bitmap (in-memory, wiped on completion)
// - Adds each page as an image via vault::add_image
// - Returns outcome with ok/error/skipped counts
[[nodiscard]] ZipImportOutcome import_pdf(vault::Vault& v,
                                          const std::filesystem::path& pdf_path,
                                          std::string_view base_gallery,
                                          std::string_view gallery_name,
                                          ImportProgress* progress = nullptr);

} // namespace ui

#endif // OSV_VENDORED_PDFIUM
