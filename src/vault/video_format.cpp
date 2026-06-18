#include "vault/video_format.h"

namespace vault {

VideoContainer detect_video_container(std::span<const uint8_t> d) noexcept
{
    using enum VideoContainer;

    // Matroska / WebM: EBML magic 0x1A45DFA3 at offset 0.
    if (d.size() >= 4 && d[0] == 0x1A && d[1] == 0x45 && d[2] == 0xDF && d[3] == 0xA3)
        return MKV;

    // ISO-BMFF (MP4/MOV/M4V): "ftyp" box type at bytes 4..7. (The leading 4 bytes
    // are the box size; we don't validate it — magic-byte detection only.)
    if (d.size() >= 8 && d[4] == 'f' && d[5] == 't' && d[6] == 'y' && d[7] == 'p')
        return MP4;

    return Unknown;
}

} // namespace vault
