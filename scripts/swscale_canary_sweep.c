// Phase 80 canary sweep — measures whether the vendored libswscale writes past
// the end of a destination buffer (the overrun class behind the Windows
// 0xc0000374 vault-upgrade crash; see docs/roadmap/phase-80-poster-heap-overrun.md).
//
// Not part of the test suite: ASAN cannot see stores made by vendored SIMD asm,
// so this standalone harness plants canary bytes after the destination and
// checks them. Run it after every FFmpeg bump (see docs/VENDORED_DEPS.md):
//
//   gcc scripts/swscale_canary_sweep.c -I vendor/codecs-prefix/include \
//       -Wl,--start-group vendor/codecs-prefix/lib/libswscale.a \
//       vendor/codecs-prefix/lib/libavutil.a -Wl,--end-group \
//       -lm -lva -lva-drm -o /tmp/sws_sweep
//   /tmp/sws_sweep tight    # old pattern: EXPECT overshoots (documents the class)
//   /tmp/sws_sweep padded   # the codebase's pattern: MUST report zero
//
// "padded" mirrors the contract used at every sws_scale site in src/media/:
// FFALIGN(w*bpp, 64) linesize + 128-byte tail, row-copied to tight buffers.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include <libavutil/macros.h>

#define TAIL_PAD 128
#define CANARY_LEN 512

// NOSONAR: the adjacent int parameters mirror sws_getContext's own signature.
static int test_one(enum AVPixelFormat srcfmt, enum AVPixelFormat dstfmt, int bpp,
                    int w, int h, int flags, int padded, int* first)
{
    uint8_t* src_data[4];
    int src_linesize[4];
    if (av_image_alloc(src_data, src_linesize, w, h, srcfmt, 64) < 0) {
        return -1;
    }
    for (int p = 0; p < 4 && src_data[p]; ++p) {
        size_t plane_h = (p == 1 || p == 2) ? (size_t)(h + 1) / 2 : (size_t)h;
        if (srcfmt == AV_PIX_FMT_YUV444P || srcfmt == AV_PIX_FMT_GBRP) {
            plane_h = h;
        }
        memset(src_data[p], 0x77, (size_t)src_linesize[p] * plane_h);
    }
    struct SwsContext* c = sws_getContext(w, h, srcfmt, w, h, dstfmt,
                                          flags, NULL, NULL, NULL);
    if (!c) {
        av_freep((void*)&src_data[0]);
        return -1;
    }

    const int linesize = padded ? FFALIGN(w * bpp, 64) : w * bpp;
    const size_t alloc = ((size_t)linesize * h) + (size_t)(padded ? TAIL_PAD : 0);
    uint8_t* buf = malloc(alloc + CANARY_LEN);
    if (!buf) {
        sws_freeContext(c);
        av_freep((void*)&src_data[0]);
        return -1;
    }
    memset(buf + alloc, 0xAB, CANARY_LEN);
    uint8_t* dst_data[4] = {buf, NULL, NULL, NULL};
    int dst_linesize[4] = {linesize, 0, 0, 0};
    sws_scale(c, (const uint8_t* const*)src_data, src_linesize, 0, h,
              dst_data, dst_linesize);

    int count = 0;
    *first = -1;
    for (int i = 0; i < CANARY_LEN; ++i) {
        if (buf[alloc + i] != 0xAB) {
            if (*first < 0) {
                *first = i;
            }
            ++count;
        }
    }
    free(buf);
    sws_freeContext(c);
    av_freep((void*)&src_data[0]);
    return count;
}

int main(int argc, char** argv)
{
    const int padded = argc > 1 && strcmp(argv[1], "padded") == 0;
    if (argc <= 1 || (!padded && strcmp(argv[1], "tight") != 0)) {
        fprintf(stderr, "usage: %s tight|padded\n", argv[0]);
        return 2;
    }
    enum AVPixelFormat fmts[] = {AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUVJ420P,
                                 AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P10LE,
                                 AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P,
                                 AV_PIX_FMT_GBRP, AV_PIX_FMT_PAL8};
    int flagsets[] = {SWS_BILINEAR, SWS_BILINEAR | SWS_FULL_CHR_H_INT,
                      SWS_FAST_BILINEAR, SWS_POINT | SWS_ACCURATE_RND};
    struct { enum AVPixelFormat fmt; int bpp; } dsts[] = {
        {AV_PIX_FMT_RGB24, 3},   // decode_poster_rgb
        {AV_PIX_FMT_RGBA, 4},    // gif_decoder
    };
    int cases = 0, worst = 0;
    for (unsigned di = 0; di < sizeof(dsts) / sizeof(*dsts); ++di)
        for (unsigned fi = 0; fi < sizeof(fmts) / sizeof(*fmts); ++fi)
            for (unsigned gi = 0; gi < sizeof(flagsets) / sizeof(*flagsets); ++gi)
                for (int w = 2; w <= 512; w += 2) {
                    int first = -1;
                    int n = test_one(fmts[fi], dsts[di].fmt, dsts[di].bpp,
                                     w, 64, flagsets[gi], padded, &first);
                    if (n > 0) {
                        ++cases;
                        if (n > worst) worst = n;
                        printf("OVERSHOOT dst=%s src=%s flags=%#x w=%d: %d bytes (first at end+%d)\n",
                               av_get_pix_fmt_name(dsts[di].fmt),
                               av_get_pix_fmt_name(fmts[fi]), flagsets[gi], w, n, first);
                    }
                }
    printf("%s pattern: %d overshooting case(s), worst %d bytes\n",
           padded ? "padded" : "tight", cases, worst);
    // tight mode is informational (documents the class); padded must be clean.
    return padded && cases ? 1 : 0;
}
