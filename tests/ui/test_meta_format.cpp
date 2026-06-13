#include "test_framework.h"

#include <string>

#include "ui/meta_format.h"
#include "vault/index.h"

TEST(format_size_scales_units)
{
    CHECK_EQ(ui::format_size(0), std::string("0 B"));
    CHECK_EQ(ui::format_size(512), std::string("512 B"));
    CHECK_EQ(ui::format_size(1024), std::string("1.0 KB"));
    CHECK_EQ(ui::format_size(1536), std::string("1.5 KB"));
    CHECK_EQ(ui::format_size(1048576), std::string("1.0 MB"));
    CHECK_EQ(ui::format_size(1073741824), std::string("1.0 GB"));
}

TEST(format_dimensions_handles_unknown)
{
    CHECK_EQ(ui::format_dimensions(1920, 1080), std::string("1920x1080"));
    CHECK_EQ(ui::format_dimensions(0, 1080), std::string("-"));
    CHECK_EQ(ui::format_dimensions(800, 0), std::string("-"));
}

TEST(format_date_is_utc_iso)
{
    CHECK_EQ(ui::format_date(0), std::string("-"));
    CHECK_EQ(ui::format_date(1718236800), std::string("2024-06-13")); // 2024-06-13 00:00 UTC
}

TEST(image_format_name_maps_enum)
{
    using enum vault::ImageFormat;
    CHECK_EQ(ui::image_format_name(JPEG), std::string_view("JPEG"));
    CHECK_EQ(ui::image_format_name(PNG), std::string_view("PNG"));
    CHECK_EQ(ui::image_format_name(WebP), std::string_view("WebP"));
    CHECK_EQ(ui::image_format_name(AVIF), std::string_view("AVIF"));
    CHECK_EQ(ui::image_format_name(Unknown), std::string_view("-"));
}
