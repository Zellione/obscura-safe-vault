#include "image/gif_info.h"

namespace image {
namespace {

// Cursor over the GIF byte stream. Every read is bounds-checked; once a read
// runs past the end the cursor latches into a failed state and stays there.
class Cursor {
public:
    explicit Cursor(std::span<const uint8_t> d) noexcept : d_(d) {}

    [[nodiscard]] bool ok() const noexcept { return ok_; }

    [[nodiscard]] uint8_t u8() noexcept
    {
        if (!ok_ || pos_ >= d_.size()) { ok_ = false; return 0; }
        return d_[pos_++];
    }

    void skip(size_t n) noexcept
    {
        if (!ok_ || n > d_.size() - pos_) { ok_ = false; return; }
        pos_ += n;
    }

private:
    std::span<const uint8_t> d_;
    size_t                   pos_ = 0;
    bool                     ok_  = true;
};

// GIF block introducers.
constexpr uint8_t kExtension      = 0x21;
constexpr uint8_t kImageSeparator = 0x2C;
constexpr uint8_t kTrailer        = 0x3B;

// Skip a chain of length-prefixed sub-blocks, terminated by a zero-length block.
void skip_sub_blocks(Cursor& c) noexcept
{
    while (c.ok()) {
        const uint8_t len = c.u8();
        if (!c.ok() || len == 0) {
            return;
        }
        c.skip(len);
    }
}

// Skip the colour table implied by a packed field: bit 7 = table present,
// bits 0-2 = size exponent (2^(n+1) entries, 3 bytes each).
void skip_colour_table(Cursor& c, uint8_t packed) noexcept
{
    if ((packed & 0x80) == 0) {
        return;
    }
    const size_t entries = static_cast<size_t>(1) << ((packed & 0x07) + 1);
    c.skip(entries * 3);
}

} // namespace

bool gif_is_animated(std::span<const uint8_t> data) noexcept
{
    Cursor c(data);

    // Header: "GIF87a" or "GIF89a".
    const uint8_t g = c.u8();
    const uint8_t i = c.u8();
    const uint8_t f = c.u8();
    if (!c.ok() || g != 'G' || i != 'I' || f != 'F') {
        return false;
    }
    c.skip(3);                      // version
    if (!c.ok()) {
        return false;
    }

    // Logical screen descriptor.
    c.skip(4);                      // width + height
    const uint8_t packed = c.u8();
    c.skip(2);                      // background colour index + aspect ratio
    if (!c.ok()) {
        return false;
    }
    skip_colour_table(c, packed);
    if (!c.ok()) {
        return false;
    }

    int frames = 0;
    while (c.ok()) {
        const uint8_t block = c.u8();
        if (!c.ok() || block == kTrailer) {
            break;
        }

        if (block == kExtension) {
            c.skip(1);              // extension label
            skip_sub_blocks(c);
        } else if (block == kImageSeparator) {
            if (++frames >= 2) {
                return true;
            }
            c.skip(8);              // left, top, width, height
            const uint8_t img_packed = c.u8();
            if (!c.ok()) {
                return false;
            }
            skip_colour_table(c, img_packed);
            c.skip(1);              // LZW minimum code size
            skip_sub_blocks(c);
        } else {
            return false;           // unknown block introducer
        }
    }

    return false;
}

} // namespace image
