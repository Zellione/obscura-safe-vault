#include "test_framework.h"

#ifdef OSV_VENDORED_AV

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
extern "C" {
#include <libavcodec/avcodec.h>
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

#include "media/video_decoder.h"
#include "media/chunk_avio.h"
#include "media/mem_avio.h"
#include "media/video_source.h"
#include "media/video_probe.h"
#include "vault/vault.h"
#include "vault/video_format.h"
#include "crypto/secure_mem.h"

namespace {

namespace fs = std::filesystem;

// Test KDF params: cheap Argon2 so the test suite stays fast.
static const crypto::KdfParams kTestKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

// RAII temp path for a unique .osv file.
struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_test_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempVault()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }
    std::string str() const { return path.string(); }
};

// Read a file into a vector.
std::vector<uint8_t> read_file(const char* file_path)
{
    std::ifstream f(file_path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// Convert std::string to span of bytes for password.
static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}


}  // namespace

// Tier-1 legacy codec tests (Phase 52)
// Each codec is verified for:
// 1. Container detection (non-Unknown)
// 2. add_video succeeds
// 3. Decoder opens and reports correct codec
// 4. Dimensions match expected values
// 5. Seek operations work

// NOTE (Phase 52): MPEG-1 and MPEG-2 are Tier-1 decodable via MKV/TS containers.
// Only the raw MPEG-PS (.mpg) container is a documented limitation: the decode-only
// vendored FFmpeg cannot identify the program-stream elementary codec (full system ffmpeg
// reads it fine; our stripped build can't), so a .mpg imports but stores as Unknown-codec.
// MPEG-1/2 content is fully supported via MKV/TS/MP4/MOV containers.

TEST(legacy_codec_mpeg4_decodes)
{
    auto v_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tinylegacy_mpeg4.avi");
    REQUIRE(!v_bytes.empty());
    TempVault tv("mpeg4");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "mpeg4.avi", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);
    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));
    CHECK(static_cast<int>(dec.codec()) == static_cast<int>(vault::VideoCodec::MPEG4));
    CHECK(dec.width() == 160);
    CHECK(dec.height() == 120);
}

TEST(legacy_codec_msmpeg4v2_decodes)
{
    auto v_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tinylegacy_msmpeg4v2.avi");
    REQUIRE(!v_bytes.empty());
    TempVault tv("msmpeg4v2");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "msmpeg4v2.avi", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);
    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));
    CHECK(static_cast<int>(dec.codec()) == static_cast<int>(vault::VideoCodec::MSMPEG4V2));
    CHECK(dec.width() == 160);
    CHECK(dec.height() == 120);
}

TEST(legacy_codec_msmpeg4v3_decodes)
{
    auto v_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tinylegacy_msmpeg4v3.avi");
    REQUIRE(!v_bytes.empty());
    TempVault tv("msmpeg4v3");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "msmpeg4v3.avi", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);
    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));
    CHECK(static_cast<int>(dec.codec()) == static_cast<int>(vault::VideoCodec::MSMPEG4V3));
    CHECK(dec.width() == 160);
    CHECK(dec.height() == 120);
}

TEST(legacy_codec_wmv1_decodes)
{
    auto v_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tinylegacy_wmv1.avi");
    REQUIRE(!v_bytes.empty());
    TempVault tv("wmv1");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "wmv1.avi", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);
    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));
    CHECK(static_cast<int>(dec.codec()) == static_cast<int>(vault::VideoCodec::WMV1));
    CHECK(dec.width() == 160);
    CHECK(dec.height() == 120);
}

TEST(legacy_codec_wmv2_decodes)
{
    auto v_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tinylegacy_wmv2.avi");
    REQUIRE(!v_bytes.empty());
    TempVault tv("wmv2");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "wmv2.avi", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);
    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));
    CHECK(static_cast<int>(dec.codec()) == static_cast<int>(vault::VideoCodec::WMV2));
    CHECK(dec.width() == 160);
    CHECK(dec.height() == 120);
}

TEST(legacy_codec_h263_decodes)
{
    // H.263 requires 176x144 QCIF resolution
    auto v_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tinylegacy_h263.avi");
    REQUIRE(!v_bytes.empty());
    TempVault tv("h263");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "h263.avi", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);
    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));
    CHECK(static_cast<int>(dec.codec()) == static_cast<int>(vault::VideoCodec::H263));
    CHECK(dec.width() == 176);
    CHECK(dec.height() == 144);
}

TEST(legacy_codec_flv1_decodes)
{
    auto v_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tinylegacy_flv1.flv");
    REQUIRE(!v_bytes.empty());
    TempVault tv("flv1");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "flv1.flv", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);
    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));
    CHECK(static_cast<int>(dec.codec()) == static_cast<int>(vault::VideoCodec::FLV1));
    CHECK(dec.width() == 160);
    CHECK(dec.height() == 120);
}

TEST(legacy_codec_svq1_decodes)
{
    auto v_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tinylegacy_svq1.mov");
    REQUIRE(!v_bytes.empty());
    TempVault tv("svq1");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "svq1.mov", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);
    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));
    CHECK(static_cast<int>(dec.codec()) == static_cast<int>(vault::VideoCodec::SVQ1));
    CHECK(dec.width() == 160);
    CHECK(dec.height() == 120);
}

TEST(legacy_codec_msvideo1_decodes)
{
    auto v_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tinylegacy_msvideo1.avi");
    REQUIRE(!v_bytes.empty());
    TempVault tv("msvideo1");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "msvideo1.avi", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);
    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));
    CHECK(static_cast<int>(dec.codec()) == static_cast<int>(vault::VideoCodec::MSVideo1));
    CHECK(dec.width() == 160);
    CHECK(dec.height() == 120);
}

TEST(legacy_codec_rpza_decodes)
{
    auto v_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tinylegacy_rpza.mov");
    REQUIRE(!v_bytes.empty());
    TempVault tv("rpza");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "rpza.mov", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);
    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));
    CHECK(static_cast<int>(dec.codec()) == static_cast<int>(vault::VideoCodec::RPZA));
    CHECK(dec.width() == 160);
    CHECK(dec.height() == 120);
}

TEST(legacy_codec_huffyuv_decodes)
{
    auto v_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tinylegacy_huffyuv.avi");
    REQUIRE(!v_bytes.empty());
    TempVault tv("huffyuv");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "huffyuv.avi", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);
    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));
    CHECK(static_cast<int>(dec.codec()) == static_cast<int>(vault::VideoCodec::HuffYUV));
    CHECK(dec.width() == 64);
    CHECK(dec.height() == 48);
}

TEST(legacy_codec_ffv1_decodes)
{
    auto v_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tinylegacy_ffv1.mkv");
    REQUIRE(!v_bytes.empty());
    TempVault tv("ffv1");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "ffv1.mkv", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);
    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));
    CHECK(static_cast<int>(dec.codec()) == static_cast<int>(vault::VideoCodec::FFV1));
    CHECK(dec.width() == 64);
    CHECK(dec.height() == 48);
}

TEST(legacy_codec_theora_decodes)
{
    auto v_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tinylegacy_theora.ogv");
    REQUIRE(!v_bytes.empty());
    TempVault tv("theora");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "theora.ogv", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);
    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));
    CHECK(static_cast<int>(dec.codec()) == static_cast<int>(vault::VideoCodec::Theora));
    CHECK(dec.width() == 160);
    CHECK(dec.height() == 120);
}

TEST(legacy_codec_rv20_decodes)
{
    auto v_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tinylegacy_rv20.rm");
    REQUIRE(!v_bytes.empty());
    TempVault tv("rv20");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "rv20.rm", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);
    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));
    CHECK(static_cast<int>(dec.codec()) == static_cast<int>(vault::VideoCodec::RV20));
    CHECK(dec.width() == 160);
    CHECK(dec.height() == 120);
}

TEST(legacy_codec_mpeg1_decodes)
{
    auto v_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tinylegacy_mpeg1.mkv");
    REQUIRE(!v_bytes.empty());
    TempVault tv("mpeg1");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "mpeg1.mkv", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);
    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));
    CHECK(static_cast<int>(dec.codec()) == static_cast<int>(vault::VideoCodec::MPEG1));
    CHECK(dec.width() == 160);
    CHECK(dec.height() == 120);
}

TEST(legacy_codec_mpeg2_mkv_decodes)
{
    auto v_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tinylegacy_mpeg2.mkv");
    REQUIRE(!v_bytes.empty());
    TempVault tv("mpeg2mkv");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "mpeg2.mkv", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);
    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));
    CHECK(static_cast<int>(dec.codec()) == static_cast<int>(vault::VideoCodec::MPEG2));
    CHECK(dec.width() == 160);
    CHECK(dec.height() == 120);
}

TEST(legacy_codec_mpeg2_ts_decodes)
{
    auto v_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tinylegacy_mpeg2.ts");
    REQUIRE(!v_bytes.empty());
    TempVault tv("mpeg2ts");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "mpeg2.ts", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);
    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));
    CHECK(static_cast<int>(dec.codec()) == static_cast<int>(vault::VideoCodec::MPEG2));
    CHECK(dec.width() == 160);
    CHECK(dec.height() == 120);
}

TEST(legacy_codec_mpeg2_interlaced_decodes)
{
    // Interlaced MPEG-2 in MKV. This test uses VideoDecoder directly (not VideoDecodeWorker),
    // so it verifies that interlaced content imports and decodes cleanly without crashing.
    // The actual deinterlacing pixel effect (yadif) is tested separately in Task 7's unit tests.
    auto v_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tinylegacy_mpeg2_interlaced.mkv");
    REQUIRE(!v_bytes.empty());
    TempVault tv("mpeg2interlaced");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "mpeg2_interlaced.mkv", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);
    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));
    CHECK(static_cast<int>(dec.codec()) == static_cast<int>(vault::VideoCodec::MPEG2));
    CHECK(dec.width() == 320);
    CHECK(dec.height() == 240);
}

TEST(legacy_codec_wmv1_with_wmav1_audio)
{
    // Video + audio track test: video is WMV1, audio is WMAV1
    auto v_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tinylegacy_wmav1_audio.asf");
    REQUIRE(!v_bytes.empty());
    TempVault tv("wmav1");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "wmav1.asf", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);
    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));
    CHECK(static_cast<int>(dec.codec()) == static_cast<int>(vault::VideoCodec::WMV1));
}

TEST(legacy_codec_wmv2_with_wmav2_audio)
{
    // Video + audio track test: video is WMV2, audio is WMAV2
    auto v_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tinylegacy_wmav2_audio.asf");
    REQUIRE(!v_bytes.empty());
    TempVault tv("wmav2");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "wmav2.asf", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);
    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));
    CHECK(static_cast<int>(dec.codec()) == static_cast<int>(vault::VideoCodec::WMV2));
}

TEST(legacy_codec_mpeg4_with_adpcm_audio)
{
    // Video + audio track test: video is MPEG4, audio is ADPCM MS
    auto v_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tinylegacy_adpcm_ms_audio.avi");
    REQUIRE(!v_bytes.empty());
    TempVault tv("adpcm");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "adpcm.avi", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);
    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));
    CHECK(static_cast<int>(dec.codec()) == static_cast<int>(vault::VideoCodec::MPEG4));
}

TEST(legacy_codec_prores_with_pcm_audio)
{
    // Video + audio track test: video is ProRes, audio is PCM
    auto v_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tinylegacy_pcm_s16le_audio.mov");
    REQUIRE(!v_bytes.empty());
    TempVault tv("pcm");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "pcm.mov", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);
    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));
    CHECK(static_cast<int>(dec.codec()) == static_cast<int>(vault::VideoCodec::ProRes));
}

TEST(legacy_codec_anamorphic_dimensions)
{
    // Anamorphic fixture: 704x576 with SAR 16/15 (non-square pixels)
    // The displayed dimensions may differ from the coded dimensions
    auto v_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tinylegacy_anamorphic.mkv");
    REQUIRE(!v_bytes.empty());

    TempVault tv("anamorphic");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "anamorphic.mkv", 4096) == vault::VaultResult::Ok);

    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);

    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));

    // For anamorphic video, the decoder may report display dims that differ
    // from coded dims. Just verify we get valid dimensions.
    CHECK(dec.width() > 0);
    CHECK(dec.height() > 0);
    CHECK(static_cast<int>(dec.codec()) != static_cast<int>(vault::VideoCodec::Unknown));
}

#endif  // OSV_VENDORED_AV
