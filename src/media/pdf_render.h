#pragma once

#ifdef OSV_VENDORED_PDFIUM

#include "crypto/secure_mem.h"

#include <cstdint>
#include <span>

namespace media {

// RAII wrapper around PDFium FPDF_DOCUMENT for rendering PDF pages to RGBA bitmaps.
// All rendering happens in-memory (mlock'd SecureBytes); no temp files.
class PdfDocument {
public:
    PdfDocument() noexcept = default;
    ~PdfDocument() noexcept;

    // Deleted copy/move for safety (document holds native PDFium state)
    PdfDocument(const PdfDocument&) = delete;
    PdfDocument& operator=(const PdfDocument&) = delete;
    PdfDocument(PdfDocument&&) = delete;
    PdfDocument& operator=(PdfDocument&&) = delete;

    // Load a PDF document from memory buffer. Buffer is NOT copied — caller
    // must keep it alive until document is closed or destroyed.
    // Returns false if PDF is malformed, encrypted, or unreadable.
    [[nodiscard]] bool open(std::span<const uint8_t> data) noexcept;

    // Number of pages in the loaded document. Returns 0 if no document is open.
    [[nodiscard]] int page_count() const noexcept;

    // Render a single page to an RGBA bitmap at the given DPI.
    // Page numbers are 0-indexed.
    // Output buffer is replaced with mlock'd SecureBytes; auto-wiped on destruction.
    // Returns false if page_num is out of range or rendering fails.
    [[nodiscard]] bool render_page(int page_num, int dpi, crypto::SecureBytes& out_rgba) noexcept;

private:
    void* doc_ = nullptr;  // FPDF_DOCUMENT opaque handle
};

} // namespace media

#endif // OSV_VENDORED_PDFIUM
