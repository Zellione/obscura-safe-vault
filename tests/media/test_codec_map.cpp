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

#endif // OSV_VENDORED_AV
