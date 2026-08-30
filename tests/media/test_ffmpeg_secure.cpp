#include "test_framework.h"

#ifdef OSV_VENDORED_AV

#include <cstring>

#include "crypto/secure_mem.h"
#include "media/ffmpeg_secure.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/buffer.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

TEST(ffmpeg_secure_packet_wipes_only_after_final_shared_reference)
{
    AVPacket* packet = av_packet_alloc();
    REQUIRE(packet != nullptr);
    REQUIRE(av_new_packet(packet, 256) == 0);
    std::memset(packet->data, 0x6D, 256);
    REQUIRE(media::secure_packet_storage(packet));

    AVPacket* clone = av_packet_clone(packet);
    REQUIRE(clone != nullptr);
    crypto::detail::reset_wipe_observations_for_tests();
    av_packet_free(&packet);
    CHECK(crypto::detail::wiping_deallocation_count() == 0);
    av_packet_free(&clone);
    CHECK(crypto::detail::wiping_deallocation_count() > 0);
    CHECK(crypto::detail::all_wipe_observations_zero_for_tests());
}

TEST(ffmpeg_secure_frame_wrapper_locks_and_wipes_software_frame)
{
    AVFrame* frame = av_frame_alloc();
    REQUIRE(frame != nullptr);
    frame->width = 64;
    frame->height = 64;
    frame->format = AV_PIX_FMT_YUV420P;
    REQUIRE(av_frame_get_buffer(frame, 0) == 0);
    REQUIRE(media::secure_frame_storage(frame));
    REQUIRE(frame->data[0] != nullptr);
    CHECK(crypto::detail::locked_page_refcount_for_tests(frame->data[0]) > 0);
    std::memset(frame->data[0], 0xC3, static_cast<size_t>(frame->linesize[0]) * frame->height);

    AVFrame* clone = av_frame_clone(frame);
    REQUIRE(clone != nullptr);
    crypto::detail::reset_wipe_observations_for_tests();
    av_frame_free(&frame);
    CHECK(crypto::detail::wiping_deallocation_count() == 0);
    av_frame_free(&clone);
    CHECK(crypto::detail::wiping_deallocation_count() > 0);
    CHECK(crypto::detail::all_wipe_observations_zero_for_tests());
}

TEST(ffmpeg_opaque_storage_marks_secure_memory_status_degraded)
{
    crypto::clear_opaque_plaintext_seen_for_tests();
    CHECK_FALSE(crypto::opaque_plaintext_seen());
    media::mark_ffmpeg_opaque_storage();
    CHECK_TRUE(crypto::opaque_plaintext_seen());
    CHECK_TRUE(crypto::secure_memory_degraded());
    crypto::clear_opaque_plaintext_seen_for_tests();
}

#endif  // OSV_VENDORED_AV
