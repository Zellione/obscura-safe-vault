#include "test_framework.h"

#ifdef OSV_VENDORED_AV

#include <cstring>
#include <vector>

#include "media/frame_convert.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace {

// Owns an AVFrame for the duration of a test.
class FramePtr {
public:
    FramePtr() = default;
    explicit FramePtr(AVFrame* p) noexcept : f_(p) {}
    FramePtr(FramePtr&& o) noexcept : f_(o.f_) { o.f_ = nullptr; }
    FramePtr(const FramePtr&)            = delete;
    FramePtr& operator=(const FramePtr&) = delete;
    FramePtr& operator=(FramePtr&&)      = delete;
    ~FramePtr() { if (f_) av_frame_free(&f_); }

    [[nodiscard]] AVFrame* get() const noexcept { return f_; }
    AVFrame* operator->() const noexcept { return f_; }
    explicit operator bool() const noexcept { return f_ != nullptr; }

private:
    AVFrame* f_ = nullptr;
};

// A YUV420P frame filled with a horizontal-stripe pattern (alternating bright
// and dark lines) — exactly the comb artefact yadif exists to remove — and
// flagged interlaced. 64x64 keeps it above any filter's minimum geometry.
// Returns an empty FramePtr if allocation fails; callers REQUIRE on it.
FramePtr make_interlaced_frame(int w, int h, int64_t pts)
{
    FramePtr fp{av_frame_alloc()};
    if (!fp) return {};
    fp->format = AV_PIX_FMT_YUV420P;
    fp->width  = w;
    fp->height = h;
    if (av_frame_get_buffer(fp.get(), 0) != 0) return {};

    for (int y = 0; y < h; ++y) {
        std::memset(fp->data[0] + static_cast<ptrdiff_t>(y) * fp->linesize[0],
                    (y % 2 != 0) ? 0x20 : 0xE0, static_cast<size_t>(w));
    }
    for (int y = 0; y < h / 2; ++y) {
        std::memset(fp->data[1] + static_cast<ptrdiff_t>(y) * fp->linesize[1], 0x80,
                    static_cast<size_t>(w / 2));
        std::memset(fp->data[2] + static_cast<ptrdiff_t>(y) * fp->linesize[2], 0x80,
                    static_cast<size_t>(w / 2));
    }

    fp->flags |= AV_FRAME_FLAG_INTERLACED;
    fp->pts = pts;
    return fp;
}

}  // namespace

TEST(should_deinterlace_true_when_interlaced_flag_set)
{
    // AV_FRAME_FLAG_INTERLACED is (1 << 3) = 8
    CHECK(media::should_deinterlace(AV_FRAME_FLAG_INTERLACED) == true);
}

TEST(should_deinterlace_false_when_no_flags)
{
    CHECK(media::should_deinterlace(0) == false);
}

TEST(should_deinterlace_false_when_other_flags_only)
{
    // Test with a different flag value (not the interlaced flag)
    CHECK(media::should_deinterlace(4) == false);
    CHECK(media::should_deinterlace(1) == false);
}

TEST(should_deinterlace_true_when_interlaced_with_other_flags)
{
    // Test interlaced flag set along with other flags
    CHECK(media::should_deinterlace(AV_FRAME_FLAG_INTERLACED | 1) == true);
    CHECK(media::should_deinterlace(AV_FRAME_FLAG_INTERLACED | 4) == true);
}

TEST(deinterlace_null_source_returns_null)
{
    media::FrameConverter conv;
    CHECK(conv.deinterlace(nullptr) == nullptr);
}

TEST(deinterlace_first_frame_returns_null_and_leaves_source_untouched)
{
    // yadif (mode=0) buffers one frame for temporal lookahead, so the first
    // call is EAGAIN. The caller (video_decode_worker) then falls through and
    // displays `src` itself — which only works if deinterlace() left src's
    // buffers intact. av_buffersrc_add_frame would have stolen src's
    // references and blanked it; av_buffersrc_write_frame must not.
    media::FrameConverter conv;
    FramePtr src = make_interlaced_frame(64, 64, 0);
    REQUIRE(src);

    const uint8_t* y_before        = src->data[0];
    const int      linesize_before = src->linesize[0];
    const uint8_t  first_px        = src->data[0][0];

    CHECK(conv.deinterlace(src.get()) == nullptr);

    CHECK(src->data[0] == y_before);
    CHECK(src->linesize[0] == linesize_before);
    CHECK(src->width == 64);
    CHECK(src->height == 64);
    CHECK(src->format == AV_PIX_FMT_YUV420P);
    REQUIRE(src->data[0] != nullptr);
    CHECK(src->data[0][0] == first_px);
}

TEST(deinterlace_emits_frame_once_yadif_has_lookahead)
{
    media::FrameConverter conv;
    FramePtr first = make_interlaced_frame(64, 64, 0);
    FramePtr second = make_interlaced_frame(64, 64, 1);
    REQUIRE(first);
    REQUIRE(second);

    CHECK(conv.deinterlace(first.get()) == nullptr);  // EAGAIN: needs lookahead

    const AVFrame* out = conv.deinterlace(second.get());
    REQUIRE(out != nullptr);
    CHECK(out->width == 64);
    CHECK(out->height == 64);
    CHECK(out->format == AV_PIX_FMT_YUV420P);
    REQUIRE(out->data[0] != nullptr);

    // yadif blends the comb stripes away, so at least one luma row must differ
    // from the hard 0x20/0xE0 pattern that went in.
    bool any_row_changed = false;
    for (int y = 0; y < 64 && !any_row_changed; ++y) {
        const uint8_t expected_in = (y % 2 != 0) ? 0x20 : 0xE0;
        if (out->data[0][static_cast<ptrdiff_t>(y) * out->linesize[0]] != expected_in)
            any_row_changed = true;
    }
    CHECK(any_row_changed);
}

TEST(deinterlace_reuses_graph_across_many_frames_without_leaking)
{
    // Each av_buffersink_get_frame move-refs into the converter's output frame
    // without unreferencing it first, so a missing av_frame_unref leaks one
    // full frame buffer per call. Run enough frames that the ASAN/valgrind
    // pass has something unmistakable to report if that unref goes away.
    media::FrameConverter conv;
    int emitted = 0;
    for (int64_t pts = 0; pts < 12; ++pts) {
        FramePtr f = make_interlaced_frame(64, 64, pts);
        REQUIRE(f);
        if (conv.deinterlace(f.get()) != nullptr) ++emitted;
    }
    // One frame is held back as lookahead; the rest come out.
    CHECK(emitted == 11);
}

TEST(deinterlace_rebuilds_graph_when_geometry_changes)
{
    // A cached graph is bound to one width/height/pix_fmt. Feeding a differently
    // sized frame must rebuild rather than push mismatched data through it.
    media::FrameConverter conv;
    FramePtr a1 = make_interlaced_frame(64, 64, 0);
    FramePtr a2 = make_interlaced_frame(64, 64, 1);
    REQUIRE(a1);
    REQUIRE(a2);
    CHECK(conv.deinterlace(a1.get()) == nullptr);
    REQUIRE(conv.deinterlace(a2.get()) != nullptr);

    FramePtr b1 = make_interlaced_frame(32, 48, 2);
    FramePtr b2 = make_interlaced_frame(32, 48, 3);
    REQUIRE(b1);
    REQUIRE(b2);
    // Graph rebuild resets yadif's lookahead, so the first new-size frame is
    // EAGAIN again, then output arrives at the new geometry.
    CHECK(conv.deinterlace(b1.get()) == nullptr);
    const AVFrame* out = conv.deinterlace(b2.get());
    REQUIRE(out != nullptr);
    CHECK(out->width == 32);
    CHECK(out->height == 48);
}

TEST(deinterlace_works_again_after_reset)
{
    // reset() frees the filter graph; the next deinterlace() must lazily
    // rebuild it rather than dereference the freed one.
    media::FrameConverter conv;
    FramePtr a1 = make_interlaced_frame(64, 64, 0);
    FramePtr a2 = make_interlaced_frame(64, 64, 1);
    REQUIRE(a1);
    REQUIRE(a2);
    CHECK(conv.deinterlace(a1.get()) == nullptr);
    REQUIRE(conv.deinterlace(a2.get()) != nullptr);

    conv.reset();

    FramePtr b1 = make_interlaced_frame(64, 64, 2);
    FramePtr b2 = make_interlaced_frame(64, 64, 3);
    REQUIRE(b1);
    REQUIRE(b2);
    CHECK(conv.deinterlace(b1.get()) == nullptr);  // lookahead starts over
    CHECK(conv.deinterlace(b2.get()) != nullptr);
}

TEST(copy_owned_frame_i420_preserves_pixel_data_and_survives_source_mutation)
{
    // Build a tiny synthetic I420 frame: 4x2 luma, 2x1 chroma planes.
    std::vector<uint8_t> y_plane{1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<uint8_t> u_plane{9, 10};
    std::vector<uint8_t> v_plane{11, 12};

    media::DecodedFrame src{};
    src.width       = 4;
    src.height      = 2;
    src.pix_fmt      = media::FramePixelFormat::I420;
    src.pts_seconds = 1.5;
    src.planes[0]    = y_plane.data();
    src.planes[1]    = u_plane.data();
    src.planes[2]    = v_plane.data();
    src.linesizes[0] = 4;
    src.linesizes[1] = 2;
    src.linesizes[2] = 2;

    std::vector<uint8_t> storage;
    media::DecodedFrame owned = media::copy_owned_frame(src, storage);

    CHECK(owned.width == 4);
    CHECK(owned.height == 2);
    CHECK(owned.pix_fmt == media::FramePixelFormat::I420);
    CHECK(owned.pts_seconds == 1.5);
    REQUIRE(owned.planes[0] != nullptr);
    REQUIRE(owned.planes[1] != nullptr);
    REQUIRE(owned.planes[2] != nullptr);
    CHECK(std::memcmp(owned.planes[0], y_plane.data(), y_plane.size()) == 0);
    CHECK(std::memcmp(owned.planes[1], u_plane.data(), u_plane.size()) == 0);
    CHECK(std::memcmp(owned.planes[2], v_plane.data(), v_plane.size()) == 0);

    // Mutate the source; the owned copy must be unaffected (proves it's a
    // real copy, not an alias — the property the cross-thread handoff needs).
    y_plane[0] = 0xFF;
    CHECK(std::memcmp(owned.planes[0], "\x01\x02\x03\x04", 4) == 0);
}

TEST(copy_owned_frame_nv12_has_two_planes_third_null)
{
    std::vector<uint8_t> y_plane{1, 2, 3, 4};
    std::vector<uint8_t> uv_plane{5, 6};

    media::DecodedFrame src{};
    src.width        = 2;
    src.height       = 2;
    src.pix_fmt       = media::FramePixelFormat::NV12;
    src.pts_seconds  = 0.0;
    src.planes[0]     = y_plane.data();
    src.planes[1]     = uv_plane.data();
    src.planes[2]     = nullptr;
    src.linesizes[0]  = 2;
    src.linesizes[1]  = 2;
    src.linesizes[2]  = 0;

    std::vector<uint8_t> storage;
    media::DecodedFrame owned = media::copy_owned_frame(src, storage);

    CHECK(owned.pix_fmt == media::FramePixelFormat::NV12);
    REQUIRE(owned.planes[0] != nullptr);
    REQUIRE(owned.planes[1] != nullptr);
    CHECK(owned.planes[2] == nullptr);
    CHECK(std::memcmp(owned.planes[0], y_plane.data(), 4) == 0);
    CHECK(std::memcmp(owned.planes[1], uv_plane.data(), 2) == 0);
}

#endif // OSV_VENDORED_AV
