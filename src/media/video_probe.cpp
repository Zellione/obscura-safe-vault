#include "media/video_probe.h"

#ifdef OSV_VENDORED_AV

#include "media/mem_avio.h"
#include "media/video_decoder.h"
#include "image/thumbnail.h"

#endif // OSV_VENDORED_AV

#include "vault/video_format.h"

namespace media {

bool probe_video(std::span<const uint8_t> data, VideoProbeResult& out)
{
#ifdef OSV_VENDORED_AV

    // Detect the container first.
    out.container = vault::detect_video_container(data);

    // Try to open with FFmpeg for detailed metadata and poster.
    MemAvio avio(data);
    if (!avio.valid()) {
        // If MemAvio allocation failed but we detected a container, still return true
        // (best effort — no poster, but basic metadata from detect_video_container).
        return out.container != vault::VideoContainer::Unknown;
    }

    VideoDecoder decoder;
    if (!decoder.open(avio.ctx())) {
        // FFmpeg couldn't open the stream; use what magic-byte detection gave us.
        return out.container != vault::VideoContainer::Unknown;
    }

    // FFmpeg opened successfully; fill in the metadata.
    out.codec       = decoder.codec();
    out.width       = decoder.width();
    out.height      = decoder.height();
    out.duration_us = decoder.duration_us();

    // Generate the poster (best effort).
    if (auto rgb = decoder.decode_poster_rgb()) {
        if (auto poster = image::make_thumbnail(*rgb, 256, 85)) {
            out.poster_jpeg = *poster;
        }
    }

    return true;

#else  // !OSV_VENDORED_AV

    // No FFmpeg: fall back to magic-byte detection only.
    out.container = vault::detect_video_container(data);
    if (out.container == vault::VideoContainer::Unknown) {
        return false;
    }

    // Leave codec, dims, duration, and poster at their defaults (Unknown/0/empty).
    return true;

#endif // OSV_VENDORED_AV
}

} // namespace media
