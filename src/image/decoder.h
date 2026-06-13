#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "image/image.h"

namespace image {

// A decoder for one or more image containers. Decoders sniff their own magic
// bytes (`can_decode`) and produce 3-channel RGB pixels (`decode`), so adding a
// new format is a matter of registering a new Decoder — no central switch to
// edit. Implementations live in the per-codec translation units (decode.cpp for
// stb, decode_webp.cpp, decode_heif.cpp) and are created via the make_*
// factories below, which keeps each codec's heavy headers out of this one.
class Decoder {
public:
    virtual ~Decoder() = default;

    // Cheap magic-byte sniff: does this decoder recognise the buffer? Must not
    // read past the span and must not allocate or throw.
    [[nodiscard]] virtual bool can_decode(std::span<const uint8_t> data) const noexcept = 0;

    // Decode the buffer to 3-channel RGB, tagging ImageData::format. Returns
    // nullopt on corrupt or unsupported data.
    [[nodiscard]] virtual std::optional<ImageData> decode(std::span<const uint8_t> data) const = 0;
};

// An ordered collection of decoders. `decode` dispatches to the first decoder
// whose `can_decode` claims the buffer, so registration order is precedence:
// register specific (magic-bearing) decoders before any last-resort catch-all.
class DecoderRegistry {
public:
    DecoderRegistry()                                  = default;
    DecoderRegistry(DecoderRegistry&&) noexcept        = default;
    DecoderRegistry& operator=(DecoderRegistry&&) noexcept = default;
    DecoderRegistry(const DecoderRegistry&)            = delete;
    DecoderRegistry& operator=(const DecoderRegistry&) = delete;

    // Append a decoder. Earlier-registered decoders take precedence on dispatch.
    void add(std::unique_ptr<Decoder> decoder);

    // Decode via the first decoder that claims the buffer. nullopt if no decoder
    // claims it (or the claiming decoder fails).
    [[nodiscard]] std::optional<ImageData> decode(std::span<const uint8_t> data) const;

private:
    std::vector<std::unique_ptr<Decoder>> decoders_;
};

// Factories for the built-in codecs (defined in their respective TUs).
[[nodiscard]] std::unique_ptr<Decoder> make_stb_decoder();
[[nodiscard]] std::unique_ptr<Decoder> make_webp_decoder();
[[nodiscard]] std::unique_ptr<Decoder> make_heif_decoder();

// The standard registry: WebP and HEIC/AVIF first (they own specific magic),
// then stb as the last-resort catch-all (it also handles headerless TGA).
[[nodiscard]] DecoderRegistry default_registry();

} // namespace image
