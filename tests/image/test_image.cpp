#include "test_framework.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include <monocypher.h>

#include "image/decode.h"
#include "image/format_registry.h"
#include "image/thumbnail.h"
#include "vault/vault.h"
#include "fixtures.h"

namespace fs = std::filesystem;

static const crypto::KdfParams kFastKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

// ---------------------------------------------------------------------------
// RAII vault in /tmp
// ---------------------------------------------------------------------------

struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_imgtest_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempVault() { std::error_code ec; fs::remove(path, ec); }
    std::string str() const { return path.string(); }
};

// ---------------------------------------------------------------------------
// decode tests
// ---------------------------------------------------------------------------

TEST(decode_empty_buffer_returns_nullopt)
{
    CHECK_FALSE(image::decode_from_memory({}).has_value());
}

TEST(decode_malformed_jpeg_returns_nullopt)
{
    const auto buf = fixtures::malformed_jpeg();
    CHECK_FALSE(image::decode_from_memory(buf).has_value());
}

TEST(decode_jpeg_format_and_dims)
{
    const auto buf = fixtures::solid_jpeg(16, 8, 200, 100, 50);
    REQUIRE(!buf.empty());
    const auto img = image::decode_from_memory(buf);
    REQUIRE(img.has_value());
    CHECK_EQ(img->format, image::ImageFormat::JPEG);
    CHECK_EQ(img->width,  16);
    CHECK_EQ(img->height, 8);
    CHECK_EQ(static_cast<int>(img->pixels.size()), 16 * 8 * 3);
}

TEST(decode_png_format_and_dims)
{
    const auto buf = fixtures::solid_png(7, 13, 0, 255, 0);
    REQUIRE(!buf.empty());
    const auto img = image::decode_from_memory(buf);
    REQUIRE(img.has_value());
    CHECK_EQ(img->format, image::ImageFormat::PNG);
    CHECK_EQ(img->width,  7);
    CHECK_EQ(img->height, 13);
    CHECK_EQ(static_cast<int>(img->pixels.size()), 7 * 13 * 3);
}

TEST(decode_bmp_format_and_dims)
{
    const auto buf = fixtures::solid_bmp(5, 5, 0, 0, 255);
    REQUIRE(!buf.empty());
    const auto img = image::decode_from_memory(buf);
    REQUIRE(img.has_value());
    CHECK_EQ(img->format, image::ImageFormat::BMP);
    CHECK_EQ(img->width,  5);
    CHECK_EQ(img->height, 5);
}

TEST(decode_gif_format_and_dims)
{
    const auto buf = fixtures::gif_1x1_red();
    const auto img = image::decode_from_memory(buf);
    REQUIRE(img.has_value());
    CHECK_EQ(img->format, image::ImageFormat::GIF);
    CHECK_EQ(img->width,  1);
    CHECK_EQ(img->height, 1);
}

TEST(decode_tga_format_and_dims)
{
    const auto buf = fixtures::solid_tga(4, 4, 128, 64, 32);
    REQUIRE(!buf.empty());
    const auto img = image::decode_from_memory(buf);
    REQUIRE(img.has_value());
    // TGA has no magic bytes; detect_format returns Unknown and the stb path
    // tags the decoded image as TGA (its last-resort decoder).
    CHECK_EQ(img->format, image::ImageFormat::TGA);
    CHECK_EQ(img->width,  4);
    CHECK_EQ(img->height, 4);
}

// ---------------------------------------------------------------------------
// format_registry magic-byte detection
// ---------------------------------------------------------------------------

TEST(detect_format_identifies_containers)
{
    using image::ImageFormat;
    using image::detect_format;

    CHECK_EQ(detect_format(std::span<const uint8_t>{}), ImageFormat::Unknown);
    CHECK_EQ(detect_format(fixtures::solid_png(2, 2, 1, 2, 3)),  ImageFormat::PNG);
    CHECK_EQ(detect_format(fixtures::solid_jpeg(2, 2, 1, 2, 3)), ImageFormat::JPEG);

    // Hand-built container headers (just the magic bytes the registry inspects).
    const std::vector<uint8_t> webp{'R','I','F','F', 0,0,0,0, 'W','E','B','P'};
    CHECK_EQ(detect_format(webp), ImageFormat::WebP);

    const std::vector<uint8_t> avif{0,0,0,0x18, 'f','t','y','p', 'a','v','i','f'};
    CHECK_EQ(detect_format(avif), ImageFormat::AVIF);

    const std::vector<uint8_t> heic{0,0,0,0x18, 'f','t','y','p', 'h','e','i','c'};
    CHECK_EQ(detect_format(heic), ImageFormat::HEIC);

    const std::vector<uint8_t> mif1{0,0,0,0x18, 'f','t','y','p', 'm','i','f','1'};
    CHECK_EQ(detect_format(mif1), ImageFormat::HEIC);
}

// ---------------------------------------------------------------------------
// WebP decode (libwebp) — fixture-backed
// ---------------------------------------------------------------------------

TEST(decode_webp_format_and_dims)
{
    const auto buf = fixtures::load_webp();
    REQUIRE(!buf.empty());  // fixture present
    const auto img = image::decode_from_memory(buf);
    REQUIRE(img.has_value());
    CHECK_EQ(img->format, image::ImageFormat::WebP);
    CHECK_EQ(img->width,  8);
    CHECK_EQ(img->height, 8);
    CHECK_EQ(img->pixels.size(), static_cast<size_t>(8 * 8 * 3));
}

TEST(decode_malformed_webp_returns_nullopt)
{
    // Valid RIFF/WEBP magic but a bogus, truncated payload.
    const std::vector<uint8_t> bad{'R','I','F','F', 4,0,0,0, 'W','E','B','P', 'X'};
    CHECK_FALSE(image::decode_from_memory(bad).has_value());
}

// ---------------------------------------------------------------------------
// HEIC / AVIF decode (libheif: libde265 + libaom) — fixture-backed
// ---------------------------------------------------------------------------

TEST(decode_heic_format_and_dims)
{
    const auto buf = fixtures::load_heic();
    REQUIRE(!buf.empty());  // fixture present
    const auto img = image::decode_from_memory(buf);
    REQUIRE(img.has_value());
    CHECK_EQ(img->format, image::ImageFormat::HEIC);
    CHECK_EQ(img->width,  8);
    CHECK_EQ(img->height, 8);
    CHECK_EQ(img->pixels.size(), static_cast<size_t>(8 * 8 * 3));
}

TEST(decode_avif_format_and_dims)
{
    const auto buf = fixtures::load_avif();
    REQUIRE(!buf.empty());  // fixture present
    const auto img = image::decode_from_memory(buf);
    REQUIRE(img.has_value());
    CHECK_EQ(img->format, image::ImageFormat::AVIF);
    CHECK_EQ(img->width,  8);
    CHECK_EQ(img->height, 8);
    CHECK_EQ(img->pixels.size(), static_cast<size_t>(8 * 8 * 3));
}

TEST(decode_malformed_heif_returns_nullopt)
{
    // A truncated ISO-BMFF ftyp box with an HEIC brand but no image payload.
    const std::vector<uint8_t> bad{0,0,0,0x18, 'f','t','y','p', 'h','e','i','c'};
    CHECK_FALSE(image::decode_from_memory(bad).has_value());
}

// ---------------------------------------------------------------------------
// thumbnail tests
// ---------------------------------------------------------------------------

TEST(thumbnail_empty_src_returns_nullopt)
{
    image::ImageData empty;
    CHECK_FALSE(image::make_thumbnail(empty, 256).has_value());
}

TEST(thumbnail_zero_max_side_returns_nullopt)
{
    const auto buf = fixtures::solid_jpeg(10, 10, 100, 100, 100);
    const auto img = image::decode_from_memory(buf);
    REQUIRE(img.has_value());
    CHECK_FALSE(image::make_thumbnail(*img, 0).has_value());
}

TEST(thumbnail_small_image_not_upscaled)
{
    // Source fits within max_side already — no upscaling.
    const auto buf = fixtures::solid_png(10, 10, 255, 0, 0);
    const auto img = image::decode_from_memory(buf);
    REQUIRE(img.has_value());

    const auto thumb = image::make_thumbnail(*img, 256);
    REQUIRE(thumb.has_value());

    const auto decoded = image::decode_from_memory(*thumb);
    REQUIRE(decoded.has_value());
    CHECK_EQ(decoded->width,  10);
    CHECK_EQ(decoded->height, 10);
}

TEST(thumbnail_wide_image_scaled_down)
{
    // 400x200, max_side=100 → 100x50
    const auto buf = fixtures::solid_jpeg(400, 200, 200, 150, 100);
    const auto img = image::decode_from_memory(buf);
    REQUIRE(img.has_value());

    const auto thumb = image::make_thumbnail(*img, 100);
    REQUIRE(thumb.has_value());

    const auto decoded = image::decode_from_memory(*thumb);
    REQUIRE(decoded.has_value());
    CHECK(decoded->width  <= 100);
    CHECK(decoded->height <= 100);
    // Aspect ratio preserved: width should equal max_side.
    CHECK_EQ(decoded->width, 100);
}

TEST(thumbnail_tall_image_scaled_down)
{
    // 100x400, max_side=50 → 12x50 (approx, preserving aspect ratio)
    const auto buf = fixtures::solid_png(100, 400, 100, 200, 100);
    const auto img = image::decode_from_memory(buf);
    REQUIRE(img.has_value());

    const auto thumb = image::make_thumbnail(*img, 50);
    REQUIRE(thumb.has_value());

    const auto decoded = image::decode_from_memory(*thumb);
    REQUIRE(decoded.has_value());
    CHECK(decoded->width  <= 50);
    CHECK(decoded->height <= 50);
    CHECK_EQ(decoded->height, 50);
}

TEST(thumbnail_produces_jpeg_magic_bytes)
{
    const auto buf = fixtures::solid_bmp(20, 20, 64, 128, 200);
    const auto img = image::decode_from_memory(buf);
    REQUIRE(img.has_value());

    const auto thumb = image::make_thumbnail(*img, 256);
    REQUIRE(thumb.has_value());
    REQUIRE(thumb->size() >= 2);
    // JPEG always starts with 0xFF 0xD8.
    CHECK_EQ((*thumb)[0], uint8_t{0xFF});
    CHECK_EQ((*thumb)[1], uint8_t{0xD8});
}

// ---------------------------------------------------------------------------
// Vault integration tests
// ---------------------------------------------------------------------------

TEST(vault_add_jpeg_sets_format_and_dims)
{
    TempVault tv("fmt");

    // 320x160 JPEG → after add_image: format=JPEG, width=320, height=160.
    const auto jpeg_buf = fixtures::solid_jpeg(320, 160, 100, 150, 200);
    REQUIRE(!jpeg_buf.empty());

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), {reinterpret_cast<const uint8_t*>("pw"), 2},
                                 {}, kFastKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", jpeg_buf, "photo.jpg") == vault::VaultResult::Ok);
    v.lock();

    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock({reinterpret_cast<const uint8_t*>("pw"), 2}, {}) == vault::VaultResult::Ok);

    const auto kids = v2.list("");
    REQUIRE(kids.size() == 1);
    const auto& meta = kids[0]->meta;
    CHECK_EQ(meta.format, vault::ImageFormat::JPEG);
    CHECK_EQ(meta.width,  320u);
    CHECK_EQ(meta.height, 160u);
}

TEST(vault_add_webp_round_trip_imports_and_reads_back)
{
    // End-to-end import (decode -> thumbnail -> encrypt -> store) of a non-stb
    // format, then re-open and read the original bytes back. Covers the Phase 9
    // acceptance criterion ("WebP/HEIC/AVIF can be imported and displayed").
    TempVault tv("webp");

    const auto webp_buf = fixtures::load_webp();
    REQUIRE(!webp_buf.empty());

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), {reinterpret_cast<const uint8_t*>("pw"), 2},
                                 {}, kFastKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", webp_buf, "photo.webp") == vault::VaultResult::Ok);
    v.lock();

    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock({reinterpret_cast<const uint8_t*>("pw"), 2}, {}) == vault::VaultResult::Ok);

    const auto kids = v2.list("");
    REQUIRE(kids.size() == 1);
    const auto& meta = kids[0]->meta;
    CHECK_EQ(meta.format, vault::ImageFormat::WebP);
    CHECK_EQ(meta.width,  8u);
    CHECK_EQ(meta.height, 8u);
    CHECK(meta.thumb_length > 0);  // thumbnail generated from the decoded WebP

    // Original bytes survive the round-trip unchanged.
    crypto::SecureBytes out;
    REQUIRE(v2.read_image(*kids[0], out) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(webp_buf));
}

TEST(vault_add_jpeg_generates_thumbnail)
{
    TempVault tv("thumb");

    const auto jpeg_buf = fixtures::solid_jpeg(400, 200, 200, 100, 50);
    REQUIRE(!jpeg_buf.empty());

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), {reinterpret_cast<const uint8_t*>("pw"), 2},
                                 {}, kFastKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", jpeg_buf, "img.jpg") == vault::VaultResult::Ok);

    const auto kids = v.list("");
    REQUIRE(kids.size() == 1);
    // Thumbnail chunk must have been written.
    CHECK(kids[0]->meta.thumb_length > 0);
}

TEST(vault_read_thumbnail_round_trip)
{
    TempVault tv("rt");

    // 512x256 JPEG → thumbnail ≤ 256px on each side.
    const auto jpeg_buf = fixtures::solid_jpeg(512, 256, 80, 120, 200);
    REQUIRE(!jpeg_buf.empty());

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), {reinterpret_cast<const uint8_t*>("pw"), 2},
                                 {}, kFastKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", jpeg_buf, "big.jpg") == vault::VaultResult::Ok);

    const auto kids = v.list("");
    REQUIRE(kids.size() == 1);
    REQUIRE(kids[0]->meta.thumb_length > 0);

    crypto::SecureBytes thumb_bytes;
    REQUIRE(v.read_thumbnail(*kids[0], thumb_bytes) == vault::VaultResult::Ok);
    REQUIRE(!thumb_bytes.empty());

    // Thumbnail bytes are a JPEG blob; decode to verify dimensions.
    const auto decoded = image::decode_from_memory(thumb_bytes.as_span());
    REQUIRE(decoded.has_value());
    CHECK(decoded->width  <= 256);
    CHECK(decoded->height <= 256);
}

TEST(vault_add_malformed_image_is_soft_failure)
{
    TempVault tv("soft");

    // Garbage bytes — decode will fail, stored with Unknown format and no thumbnail.
    const std::vector<uint8_t> garbage(256, 0xAB);

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), {reinterpret_cast<const uint8_t*>("pw"), 2},
                                 {}, kFastKdf, v) == vault::VaultResult::Ok);
    // add_image must NOT fail even when the image is unrecognised.
    REQUIRE(v.add_image("", garbage, "bad.bin") == vault::VaultResult::Ok);

    const auto kids = v.list("");
    REQUIRE(kids.size() == 1);
    CHECK_EQ(kids[0]->meta.format,       vault::ImageFormat::Unknown);
    CHECK_EQ(kids[0]->meta.thumb_length, uint64_t{0});

    // read_thumbnail on a node with no thumbnail returns NotFound.
    crypto::SecureBytes out;
    CHECK_EQ(v.read_thumbnail(*kids[0], out), vault::VaultResult::NotFound);
}

TEST(vault_read_thumbnail_no_thumb_returns_not_found)
{
    TempVault tv("notfound");

    // Pattern bytes with no recognisable image format.
    const std::vector<uint8_t> data(512, 0x00);

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), {reinterpret_cast<const uint8_t*>("pw"), 2},
                                 {}, kFastKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", data, "empty.bin") == vault::VaultResult::Ok);

    const auto kids = v.list("");
    REQUIRE(kids.size() == 1);

    crypto::SecureBytes out;
    CHECK_EQ(v.read_thumbnail(*kids[0], out), vault::VaultResult::NotFound);
    CHECK(out.empty());
}
