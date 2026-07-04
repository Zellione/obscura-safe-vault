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

TEST(format_duration_us_to_clock)
{
    CHECK_EQ(ui::format_duration(0), std::string("0:00"));
    CHECK_EQ(ui::format_duration(1'500'000), std::string("0:01"));        // 1.5 s
    CHECK_EQ(ui::format_duration(65'000'000), std::string("1:05"));       // 65 s
    CHECK_EQ(ui::format_duration(3'661'000'000ULL), std::string("1:01:01"));
}

TEST(video_codec_and_type_labels)
{
    using enum vault::VideoCodec;
    CHECK_EQ(ui::video_codec_name(H264), std::string_view("H.264"));
    CHECK_EQ(ui::video_codec_name(HEVC), std::string_view("H.265"));
    CHECK_EQ(ui::video_codec_name(ProRes), std::string_view("ProRes"));   // Phase 28
    CHECK_EQ(ui::video_codec_name(DNxHD), std::string_view("DNxHD"));
    CHECK_EQ(ui::video_codec_name(MJPEG), std::string_view("MJPEG"));
    CHECK_EQ(ui::video_codec_name(Unknown), std::string_view("Video"));
    CHECK_EQ(ui::video_type_label(H264), std::string("Video (H.264)"));
    CHECK_EQ(ui::video_type_label(Unknown), std::string("Video"));
}

TEST(is_video_filename_by_extension)
{
    CHECK(ui::is_video_filename("clip.mp4"));
    CHECK(ui::is_video_filename("CLIP.MP4"));        // case-insensitive
    CHECK(ui::is_video_filename("movie.mkv"));
    CHECK(ui::is_video_filename("a.b.webm"));        // last extension wins
    CHECK(ui::is_video_filename("phone.mov"));
    CHECK(ui::is_video_filename("old.m4v"));
    CHECK_FALSE(ui::is_video_filename("photo.jpg"));
    CHECK_FALSE(ui::is_video_filename("noext"));
    CHECK_FALSE(ui::is_video_filename(""));
    CHECK_FALSE(ui::is_video_filename("trailingdot."));
}
