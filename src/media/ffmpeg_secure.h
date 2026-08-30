#pragma once

#ifdef OSV_VENDORED_AV

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace media {

// Attach a reference-counted secure-lifetime sidecar to a demuxed packet.
// Clones share the sidecar; the bytes are wiped only after its final release.
[[nodiscard]] bool secure_packet_storage(AVPacket* packet) noexcept;

// Attach the frame sidecar to already-allocated software planes (filter and
// hardware-transfer outputs that bypass AVCodecContext::get_buffer2).
[[nodiscard]] bool secure_frame_storage(AVFrame* frame) noexcept;

// AVCodecContext::get_buffer2 callback. Software frame planes are page-locked
// and receive the same final-reference wipe sidecar as packets. Hardware frames
// stay driver-owned and mark the session's documented opaque boundary.
int secure_get_buffer2(AVCodecContext* ctx, AVFrame* frame, int flags) noexcept;

// Record use of codec/filter/driver scratch whose allocator FFmpeg does not
// expose. F1 folds this into the secure-memory degraded status.
void mark_ffmpeg_opaque_storage() noexcept;

}  // namespace media

#endif  // OSV_VENDORED_AV
