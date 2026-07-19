#ifdef OSV_VENDORED_PDFIUM

#include "test_framework.h"

#include "ui/pdf_import.h"

namespace {

// Test build_pdf_plan pure function - no vault needed
TEST(pdf_plan_creates_correct_structure)
{
    auto plan = ui::build_pdf_plan(3, "/", "test.pdf");
    CHECK_EQ(plan.galleries.size(), std::size_t{1});
    CHECK(plan.galleries[0].find("test") != std::string::npos);
    CHECK_EQ(plan.placements.size(), std::size_t{3});

    // Pages should be in order with correct entry indices
    for (size_t i = 0; i < plan.placements.size(); ++i) {
        CHECK_EQ(plan.placements[i].entry_index, i);
        CHECK(plan.placements[i].filename.find("page_") != std::string::npos);
    }
}

TEST(pdf_plan_single_page)
{
    auto plan = ui::build_pdf_plan(1, "/", "single.pdf");
    CHECK_EQ(plan.galleries.size(), std::size_t{1});
    CHECK_EQ(plan.placements.size(), std::size_t{1});
    CHECK_EQ(plan.placements[0].entry_index, 0);
}

TEST(pdf_plan_nested_gallery)
{
    auto plan = ui::build_pdf_plan(2, "/documents", "report.pdf");
    CHECK_EQ(plan.galleries.size(), std::size_t{1});
    // Gallery path should contain the base path
    CHECK(plan.galleries[0].find("documents") != std::string::npos);
    CHECK(plan.galleries[0].find("report") != std::string::npos);
}

TEST(pdf_plan_sanitizes_filename_with_extension)
{
    auto plan = ui::build_pdf_plan(1, "/", "my-document_2026.pdf");
    CHECK(!plan.galleries[0].empty());
    CHECK(plan.galleries[0].find("my") != std::string::npos);
}

} // namespace

#endif // OSV_VENDORED_PDFIUM
