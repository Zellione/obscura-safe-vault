#include "test_framework.h"

// Phase 15 PR1: prove the decode-only FFmpeg archives link and run. Compiled
// out where OSV_VENDORED_AV is undefined (e.g. the Windows leg until its FFmpeg
// build lands) so the test binary still builds everywhere.
#ifdef OSV_VENDORED_AV

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}
#pragma GCC diagnostic pop

TEST(av_libraries_link_and_report_version)
{
    // Version macros are compile-time; the *_version() calls force the linker to
    // pull in each archive at run time.
    CHECK(avcodec_version()  == LIBAVCODEC_VERSION_INT);
    CHECK(avformat_version() == LIBAVFORMAT_VERSION_INT);
    CHECK(swscale_version()  == LIBSWSCALE_VERSION_INT);

    // The H.264 decoder we enabled must be present in this decode-only build.
    CHECK(avcodec_find_decoder(AV_CODEC_ID_H264) != nullptr);
    CHECK(avcodec_find_decoder(AV_CODEC_ID_HEVC) != nullptr);
    // Encoders were disabled — confirm the build really is decode-only.
    CHECK(avcodec_find_encoder(AV_CODEC_ID_H264) == nullptr);
}

#endif // OSV_VENDORED_AV
