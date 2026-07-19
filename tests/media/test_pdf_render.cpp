#ifdef OSV_VENDORED_PDFIUM

#include "test_framework.h"

#include "media/pdf_render.h"
#include "crypto/secure_mem.h"

#include <fstream>
#include <vector>
#include <filesystem>

namespace {

// Helper to read a test fixture PDF into memory
std::vector<uint8_t> read_fixture(std::string_view name)
{
    std::string path = std::string("tests/media/fixtures/") + std::string(name);
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
}

} // namespace

TEST(pdf_loads_from_memory_buffer)
{
    auto bytes = read_fixture("simple.pdf");
    REQUIRE(!bytes.empty());
    media::PdfDocument doc;
    REQUIRE(doc.open(bytes));
    CHECK(doc.page_count() > 0);
}

TEST(pdf_multi_page_returns_correct_count)
{
    auto bytes = read_fixture("multi.pdf");
    REQUIRE(!bytes.empty());
    media::PdfDocument doc;
    REQUIRE(doc.open(bytes));
    CHECK_EQ(doc.page_count(), 3);
}

TEST(pdf_renders_page_to_rgba)
{
    auto bytes = read_fixture("simple.pdf");
    REQUIRE(!bytes.empty());
    media::PdfDocument doc;
    REQUIRE(doc.open(bytes));

    crypto::SecureBytes rgba;
    REQUIRE(doc.render_page(0, 150, rgba));

    // Should have rendered to RGBA (4 bytes per pixel)
    CHECK(rgba.size() > 0);
    CHECK_EQ(rgba.size() % 4, static_cast<size_t>(0));
}

TEST(pdf_rejects_invalid_page_number)
{
    auto bytes = read_fixture("simple.pdf");
    REQUIRE(!bytes.empty());
    media::PdfDocument doc;
    REQUIRE(doc.open(bytes));

    crypto::SecureBytes rgba;
    CHECK(!doc.render_page(999, 150, rgba));
}

TEST(pdf_rejects_malformed_pdf)
{
    auto bytes = read_fixture("corrupt.pdf");
    REQUIRE(!bytes.empty());
    media::PdfDocument doc;
    CHECK(!doc.open(bytes));
}

TEST(pdf_rejects_encrypted_pdf)
{
    auto bytes = read_fixture("encrypted.pdf");
    REQUIRE(!bytes.empty());
    media::PdfDocument doc;
    // Encrypted PDF should either fail to open or fail to render gracefully
    bool opened = doc.open(bytes);
    if (opened) {
        crypto::SecureBytes rgba;
        CHECK(!doc.render_page(0, 150, rgba));
    }
    // Pass: no crash
}

TEST(pdf_empty_buffer_rejected)
{
    std::vector<uint8_t> empty;
    media::PdfDocument doc;
    CHECK(!doc.open(empty));
}

TEST(pdf_sequential_page_renders)
{
    auto bytes = read_fixture("multi.pdf");
    REQUIRE(!bytes.empty());
    media::PdfDocument doc;
    REQUIRE(doc.open(bytes));
    REQUIRE(doc.page_count() == 3);

    // Render all pages in sequence
    for (int i = 0; i < doc.page_count(); ++i) {
        crypto::SecureBytes rgba;
        REQUIRE(doc.render_page(i, 150, rgba));
        CHECK(rgba.size() > 0);
    }
}

#endif // OSV_VENDORED_PDFIUM
