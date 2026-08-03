#pragma once

// Phase 62: sampled-frame video signature for the perceptual duplicate pass.
// Decodes one frame at each DUP_VID_FRAME_POSITIONS fraction of the timeline
// through a caller-supplied byte reader (the scan worker backs it with the
// thread-safe vault::read_thumb_span — never the main-thread VideoSource) and
// dHashes each. FFmpeg-only; non-AV builds compile a stub returning false.

#include <cstdint>
#include <functional>
#include <span>

#include "ui/dup_model.h"

namespace ui {

// Thread-safe byte reader over the video's plaintext stream: copy up to
// dst.size() bytes from `offset`, return bytes read (0 = EOF, -1 = error).
using DupStreamRead = std::function<int64_t(uint64_t offset, std::span<uint8_t> dst)>;

// Fill sig.frame_hash/frame_valid from decoded frames. Returns false when the
// container cannot be opened at all; a failed seek/decode merely leaves that
// position's valid bit unset. Software decode only (no hwaccel).
[[nodiscard]] bool compute_video_frame_sig(const DupStreamRead& read, uint64_t total_size,
                                           VideoSig& sig);

} // namespace ui
