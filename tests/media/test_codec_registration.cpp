#include "test_framework.h"

#ifdef OSV_VENDORED_AV

#include <array>
#include <print>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
}

namespace {

// Phase 52 decoders (legacy + existing). FFmpeg's registered decoder name can
// differ from the configure --enable-decoder name, so the lookups below use the
// REGISTERED names (e.g. 'libaom-av1' for configure 'libaom_av1', 'msmpeg4' for
// the MS-MPEG4 v3 decoder). Stored as const char* so the C API takes them
// directly (a null-terminated literal), no string_view::data() round-trip.
constexpr std::array kRequiredDecoders{
    // Existing decoders
    "h264", "hevc", "prores", "dnxhd", "mjpeg", "vp8", "vp9", "libaom-av1",
    "qtrle", "cinepak", "gif", "aac", "opus", "mp3", "vorbis", "flac", "ac3",
    // Phase 52 additions
    "mpeg1video", "mpeg2video", "mpeg4", "msmpeg4v1", "msmpeg4v2", "msmpeg4",
    "wmv1", "wmv2", "wmv3", "vc1", "h263", "flv", "vp6", "vp6a", "vp6f",
    "svq1", "svq3", "dvvideo", "msvideo1", "rpza", "huffyuv", "ffv1", "theora",
    "rv10", "rv20", "rv30", "rv40", "mp2", "wmav1", "wmav2", "cook",
    "real_144", "real_288",  // configure ra_144/ra_288 register under these names
    "pcm_s16le", "pcm_u8", "adpcm_ms", "adpcm_ima_wav",
};

// Phase 52 demuxers (legacy additions). Registered names: configure 'mpegps'
// registers as 'mpeg'.
constexpr std::array kRequiredDemuxers{
    "avi", "mpeg", "mpegts", "asf", "flv", "ogg", "rm",
};

}  // namespace

TEST(codec_registration_decoders)
{
    for (const char* decoder_name : kRequiredDecoders) {
        const AVCodec* codec = avcodec_find_decoder_by_name(decoder_name);
        if (codec == nullptr) {
            std::println("  codec lookup failed: decoder '{}'", decoder_name);
        }
        CHECK(codec != nullptr);
    }
}

TEST(codec_registration_demuxers)
{
    for (const char* demuxer_name : kRequiredDemuxers) {
        const AVInputFormat* fmt = av_find_input_format(demuxer_name);
        if (fmt == nullptr) {
            std::println("  codec lookup failed: demuxer '{}'", demuxer_name);
        }
        CHECK(fmt != nullptr);
    }
}

TEST(codec_registration_has_yadif_filter)
{
    CHECK(avfilter_get_by_name("yadif") != nullptr);
}

#endif  // OSV_VENDORED_AV
