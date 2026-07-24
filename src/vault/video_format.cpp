#include "vault/video_format.h"

namespace vault {

VideoContainer detect_video_container(std::span<const uint8_t> d) noexcept
{
    using enum VideoContainer;

    // Matroska / WebM: EBML magic 0x1A45DFA3 at offset 0.
    if (d.size() >= 4 && d[0] == 0x1A && d[1] == 0x45 && d[2] == 0xDF && d[3] == 0xA3) {
        return MKV;
    }

    // ISO-BMFF (MP4/MOV/M4V): "ftyp" box type at bytes 4..7. (The leading 4 bytes
    // are the box size; we don't validate it — magic-byte detection only.)
    if (d.size() >= 8 && d[4] == 'f' && d[5] == 't' && d[6] == 'y' && d[7] == 'p') {
        return MP4;
    }

    // AVI: "RIFF" at offset 0 AND "AVI " at offset 8 (Phase 52). Bounds-check both.
    if (d.size() >= 12 && d[0] == 'R' && d[1] == 'I' && d[2] == 'F' && d[3] == 'F' &&
        d[8] == 'A' && d[9] == 'V' && d[10] == 'I' && d[11] == ' ') {
        return AVI;
    }

    // MPEG-PS: 0x00 0x00 0x01 0xBA at offset 0 (Phase 52).
    if (d.size() >= 4 && d[0] == 0x00 && d[1] == 0x00 && d[2] == 0x01 && d[3] == 0xBA) {
        return MPEGPS;
    }

    // ASF/WMV: 16-byte header GUID at offset 0 (Phase 52).
    // 30 26 B2 75 8E 66 CF 11 A6 D9 00 AA 00 62 CE 6C
    if (d.size() >= 16 && d[0] == 0x30 && d[1] == 0x26 && d[2] == 0xB2 && d[3] == 0x75 &&
        d[4] == 0x8E && d[5] == 0x66 && d[6] == 0xCF && d[7] == 0x11 &&
        d[8] == 0xA6 && d[9] == 0xD9 && d[10] == 0x00 && d[11] == 0xAA &&
        d[12] == 0x00 && d[13] == 0x62 && d[14] == 0xCE && d[15] == 0x6C) {
        return ASF;
    }

    // FLV: "FLV" at offset 0 (Phase 52).
    if (d.size() >= 3 && d[0] == 'F' && d[1] == 'L' && d[2] == 'V') {
        return FLV;
    }

    // Ogg: "OggS" at offset 0 (Phase 52).
    if (d.size() >= 4 && d[0] == 'O' && d[1] == 'g' && d[2] == 'g' && d[3] == 'S') {
        return OGG;
    }

    // RealMedia: ".RMF" = 0x2E 0x52 0x4D 0x46 at offset 0 (Phase 52).
    if (d.size() >= 4 && d[0] == 0x2E && d[1] == 0x52 && d[2] == 0x4D && d[3] == 0x46) {
        return RM;
    }

    // MPEG-TS: 0x47 at offsets 0, 188, and 376. Checked LAST because a single 0x47
    // is a weak signature and could appear in other formats. Bounds-check all three
    // offsets before dereferencing (Phase 52).
    if (d.size() >= 377 && d[0] == 0x47 && d[188] == 0x47 && d[376] == 0x47) {
        return MPEGTS;
    }

    return Unknown;
}

} // namespace vault
