#ifdef OSV_VENDORED_AV

#include "test_framework.h"

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

#include <array>

#include "media/video_decoder.h"

TEST(codec_map_mpeg2)
{
    using enum vault::VideoCodec;
    CHECK_EQ(media::map_codec_id(AV_CODEC_ID_MPEG2VIDEO), MPEG2);
}

TEST(codec_map_msmpeg4v3)
{
    using enum vault::VideoCodec;
    CHECK_EQ(media::map_codec_id(AV_CODEC_ID_MSMPEG4V3), MSMPEG4V3);
}

TEST(codec_map_wmv3)
{
    using enum vault::VideoCodec;
    CHECK_EQ(media::map_codec_id(AV_CODEC_ID_WMV3), WMV3);
}

TEST(codec_map_vc1)
{
    using enum vault::VideoCodec;
    CHECK_EQ(media::map_codec_id(AV_CODEC_ID_VC1), VC1);
}

TEST(codec_map_flv1)
{
    using enum vault::VideoCodec;
    CHECK_EQ(media::map_codec_id(AV_CODEC_ID_FLV1), FLV1);
}

TEST(codec_map_dvvideo)
{
    using enum vault::VideoCodec;
    CHECK_EQ(media::map_codec_id(AV_CODEC_ID_DVVIDEO), DV);
}

TEST(codec_map_rv40)
{
    using enum vault::VideoCodec;
    CHECK_EQ(media::map_codec_id(AV_CODEC_ID_RV40), RV40);
}

TEST(codec_map_h264_preexisting)
{
    using enum vault::VideoCodec;
    CHECK_EQ(media::map_codec_id(AV_CODEC_ID_H264), H264);
}

TEST(codec_map_unknown_nullopt)
{
    CHECK(!media::map_codec_id(AV_CODEC_ID_NONE).has_value());
}

// ---------------------------------------------------------------------------
// Tier-2 codecs (Phase 52): the vendored build contains the DECODER, but system
// ffmpeg has NO ENCODER for them, so no Tier-1 fixture can be generated and their
// DECODE path is UNVERIFIED by the automated suite. What we CAN assert — and do
// here — is that the build actually contains the decoder (avcodec_find_decoder_by_name
// resolves) AND that the app will accept it (map_codec_id maps the id). Real-file
// decode verification for these is a MANUAL release-check item, called out in the
// phase doc. This is the phase's honest weak point: wmv3/vc1 is the dominant .wmv
// codec and rv30/rv40 the dominant RealVideo codecs, so the two headline container
// wins (.wmv, RealMedia) rest partly on these untested decode paths.
namespace {
struct Tier2Codec {
    int          codec_id;              // AV_CODEC_ID_*
    const char*  registered_name;       // avcodec_find_decoder_by_name lookup
    vault::VideoCodec expected;         // map_codec_id result
};
constexpr std::array kTier2{
    Tier2Codec{.codec_id = AV_CODEC_ID_MSMPEG4V1, .registered_name = "msmpeg4v1", .expected = vault::VideoCodec::MSMPEG4V1},
    Tier2Codec{.codec_id = AV_CODEC_ID_WMV3,      .registered_name = "wmv3",      .expected = vault::VideoCodec::WMV3},
    Tier2Codec{.codec_id = AV_CODEC_ID_VC1,       .registered_name = "vc1",       .expected = vault::VideoCodec::VC1},
    Tier2Codec{.codec_id = AV_CODEC_ID_SVQ3,      .registered_name = "svq3",      .expected = vault::VideoCodec::SVQ3},
    Tier2Codec{.codec_id = AV_CODEC_ID_VP6,       .registered_name = "vp6",       .expected = vault::VideoCodec::VP6},
    Tier2Codec{.codec_id = AV_CODEC_ID_VP6A,      .registered_name = "vp6a",      .expected = vault::VideoCodec::VP6A},
    Tier2Codec{.codec_id = AV_CODEC_ID_VP6F,      .registered_name = "vp6f",      .expected = vault::VideoCodec::VP6F},
    Tier2Codec{.codec_id = AV_CODEC_ID_RV10,      .registered_name = "rv10",      .expected = vault::VideoCodec::RV10},
    Tier2Codec{.codec_id = AV_CODEC_ID_RV30,      .registered_name = "rv30",      .expected = vault::VideoCodec::RV30},
    Tier2Codec{.codec_id = AV_CODEC_ID_RV40,      .registered_name = "rv40",      .expected = vault::VideoCodec::RV40},
};
}  // namespace

TEST(codec_map_tier2_decoders_present_and_mapped)
{
    for (const auto& c : kTier2) {
        const AVCodec* dec = avcodec_find_decoder_by_name(c.registered_name);
        CHECK(dec != nullptr);                                   // build contains the decoder
        CHECK_EQ(media::map_codec_id(c.codec_id), c.expected);   // app accepts the id
    }
}

// Tier-2 audio (cook, real_144, real_288) has no video-codec mapping — audio has
// no allowlist; its coverage is that the decoder is present in the build, asserted
// in test_codec_registration.cpp (Task 1). Named here so the Tier-2 story is complete.
TEST(codec_map_tier2_audio_decoders_present)
{
    for (const char* name : {"cook", "real_144", "real_288"}) {
        CHECK(avcodec_find_decoder_by_name(name) != nullptr);
    }
}

#endif // OSV_VENDORED_AV
