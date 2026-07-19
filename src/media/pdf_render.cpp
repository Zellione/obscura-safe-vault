#include "media/pdf_render.h"

#ifdef OSV_VENDORED_PDFIUM

#include <fpdfview.h>
#include <print>
#include <cstring>

namespace media {

// Static initialization guard for PDFium library (once per process)
namespace {
bool& pdfium_initialized()
{
    static bool initialized = false;
    return initialized;
}

void ensure_pdfium_init()
{
    if (!pdfium_initialized()) {
        FPDF_InitLibrary();
        pdfium_initialized() = true;
    }
}
} // namespace

PdfDocument::~PdfDocument() noexcept
{
    if (doc_) {
        FPDF_CloseDocument(static_cast<FPDF_DOCUMENT>(doc_));
        doc_ = nullptr;
    }
}

bool PdfDocument::open(std::span<const uint8_t> data) noexcept
{
    ensure_pdfium_init();

    if (data.empty()) {
        std::println(stderr, "[PdfRender] Cannot open empty PDF buffer");
        return false;
    }

    // PDFium requires a custom file access structure for in-memory PDFs
    // Use FPDF_LoadMemDocument which reads directly from memory
    FPDF_DOCUMENT doc = FPDF_LoadMemDocument(data.data(), static_cast<int>(data.size()), "");
    if (!doc) {
        std::println(stderr, "[PdfRender] PDFium failed to load PDF from memory");
        return false;
    }

    doc_ = doc;
    return true;
}

int PdfDocument::page_count() const noexcept
{
    if (!doc_) return 0;
    return FPDF_GetPageCount(static_cast<FPDF_DOCUMENT>(doc_));
}

bool PdfDocument::render_page(int page_num, int dpi, crypto::SecureBytes& out_rgba,
                              int& out_width, int& out_height) noexcept
{
    if (!doc_) {
        std::println(stderr, "[PdfRender] Cannot render: no document loaded");
        return false;
    }

    if (page_num < 0 || page_num >= page_count()) {
        std::println(stderr, "[PdfRender] Page number {} out of range [0, {})", page_num, page_count());
        return false;
    }

    // Load the page
    FPDF_PAGE page = FPDF_LoadPage(static_cast<FPDF_DOCUMENT>(doc_), page_num);
    if (!page) {
        std::println(stderr, "[PdfRender] Failed to load page {}", page_num);
        return false;
    }

    // Get page dimensions in points (1/72 inch)
    double width_points = FPDF_GetPageWidth(page);
    double height_points = FPDF_GetPageHeight(page);

    // Convert to pixels at requested DPI (DPI / 72 = scale factor)
    const double scale = dpi / 72.0;
    int width_pixels = static_cast<int>(width_points * scale + 0.5);
    int height_pixels = static_cast<int>(height_points * scale + 0.5);

    // Sanity check: reasonable page sizes
    if (width_pixels <= 0 || height_pixels <= 0 || width_pixels > 16384 || height_pixels > 16384) {
        std::println(stderr, "[PdfRender] Invalid page dimensions: {}x{} pixels", width_pixels, height_pixels);
        FPDF_ClosePage(page);
        return false;
    }

    // Allocate RGBA buffer (4 bytes per pixel)
    size_t buffer_size = static_cast<size_t>(width_pixels) * height_pixels * 4;
    if (!out_rgba.resize(buffer_size)) {
        std::println(stderr, "[PdfRender] Failed to allocate {} bytes for page bitmap", buffer_size);
        FPDF_ClosePage(page);
        return false;
    }

    // Create bitmap for rendering (RGBA format)
    FPDF_BITMAP bitmap = FPDFBitmap_Create(width_pixels, height_pixels, 1);  // 1 = RGBA
    if (!bitmap) {
        std::println(stderr, "[PdfRender] Failed to create bitmap");
        FPDF_ClosePage(page);
        return false;
    }

    // Clear to white background
    FPDFBitmap_FillRect(bitmap, 0, 0, width_pixels, height_pixels, 0xFFFFFFFF);

    // Render page to bitmap
    FPDF_RenderPageBitmap(bitmap, page, 0, 0, width_pixels, height_pixels, 0, 0);

    // Copy bitmap buffer to our RGBA output
    const uint8_t* bitmap_buffer = static_cast<const uint8_t*>(FPDFBitmap_GetBuffer(bitmap));
    std::memcpy(out_rgba.data(), bitmap_buffer, buffer_size);

    // Set output dimensions
    out_width = width_pixels;
    out_height = height_pixels;

    // Cleanup
    FPDFBitmap_Destroy(bitmap);
    FPDF_ClosePage(page);

    return true;
}

} // namespace media

#endif // OSV_VENDORED_PDFIUM
