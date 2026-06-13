#include "image/decoder.h"

namespace image {

void DecoderRegistry::add(std::unique_ptr<Decoder> decoder)
{
    if (decoder) decoders_.push_back(std::move(decoder));
}

std::optional<ImageData> DecoderRegistry::decode(std::span<const uint8_t> data) const
{
    for (const auto& d : decoders_) {
        if (d->can_decode(data)) return d->decode(data);
    }
    return std::nullopt;
}

DecoderRegistry default_registry()
{
    DecoderRegistry reg;
    // Specific containers first; stb is the catch-all and must be last because
    // it claims every buffer (last-resort path for headerless TGA).
    reg.add(make_webp_decoder());
    reg.add(make_heif_decoder());
    reg.add(make_stb_decoder());
    return reg;
}

} // namespace image
