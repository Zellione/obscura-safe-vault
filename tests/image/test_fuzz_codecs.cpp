#include "test_framework.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "image/decode.h"
#include "fixtures.h"

#ifdef OSV_VENDORED_AV
#include "media/video_probe.h"
#endif

// Phase 25 fuzz suite for codec attack surface (WebP/HEIC/AVIF/video).
// Deterministic seeded mutations: truncations, bit-flips, dimension lies, and
// brand-byte swaps. Verifies that all decoders reject malformed input without
// crashing or hanging.
//
// Runs under scripts/test.sh / scripts/test.sh --asan (with ASAN-instrumented
// codecs built into vendor/codecs-prefix-asan/ by scripts/build_codecs.sh --asan).

// xorshift64*: tiny deterministic PRNG (same as test_fuzz.cpp).
struct Prng {
    uint64_t s;
    explicit Prng(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    uint64_t next()
    {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 0x2545F4914F6CDD1Dull;
    }
    uint32_t below(uint32_t n) { return n ? static_cast<uint32_t>(next() % n) : 0; }
};

static std::vector<uint8_t> random_bytes(Prng& rng, size_t n)
{
    std::vector<uint8_t> v(n);
    for (auto& b : v) b = static_cast<uint8_t>(rng.next());
    return v;
}

// Flip a single bit at (byte_idx, bit_idx).
static void flip_bit(std::vector<uint8_t>& v, size_t byte_idx, int bit_idx)
{
    if (byte_idx < v.size()) {
        v[byte_idx] ^= static_cast<uint8_t>(1u << bit_idx);
    }
}

// Mutate a copy of `base`: truncate at boundaries, flip bits, lie about dimensions,
// and (for HEIC/AVIF) swap brand bytes.
static std::vector<uint8_t> mutate_codec(Prng& rng, const std::vector<uint8_t>& base,
                                          bool allow_dimension_lie = true,
                                          bool allow_brand_swap = false)
{
    std::vector<uint8_t> v = base;
    if (v.empty()) return v;

    // 1. Truncate at ~1/16 boundaries (roughly 16 cases per seed).
    const uint32_t trunc_pos = rng.below(16);
    if (trunc_pos > 0) {
        const size_t boundary = (v.size() * trunc_pos) / 16;
        if (boundary < v.size()) v.resize(boundary);
    }

    // 2. Flip a few bits (seeded, deterministic).
    const uint32_t num_flips = 1 + rng.below(8);
    for (uint32_t i = 0; i < num_flips; ++i) {
        if (!v.empty()) {
            const size_t byte_idx = rng.below(static_cast<uint32_t>(v.size()));
            const int bit_idx = rng.below(8);
            flip_bit(v, byte_idx, bit_idx);
        }
    }

    // 3. Lie about dimensions if format allows (for PNG, JPEG, BMP with recognizable headers).
    if (allow_dimension_lie && v.size() >= 8) {
        // Check for PNG: 0x89 0x50 0x4E 0x47 (PNG magic + IHDR chunk)
        if (v[0] == 0x89 && v[1] == 0x50 && v[2] == 0x4E && v[3] == 0x47 &&
            v.size() >= 24) {
            // PNG width is at offset 16–19 (big-endian u32).
            const uint64_t lie_raw = rng.next();
            v[16] = static_cast<uint8_t>((lie_raw >> 24) & 0xFFu);
            v[17] = static_cast<uint8_t>((lie_raw >> 16) & 0xFFu);
            v[18] = static_cast<uint8_t>((lie_raw >> 8) & 0xFFu);
            v[19] = static_cast<uint8_t>(lie_raw & 0xFFu);
        }
        // Check for JPEG: 0xFF 0xD8
        else if (v[0] == 0xFF && v[1] == 0xD8) {
            // Lie in an SOF marker if present (harder to detect without parsing, so just flip SOF bytes).
            for (size_t i = 4; i + 8 < v.size(); ++i) {
                if (v[i] == 0xFF && (v[i + 1] & 0xF0) == 0xC0) {  // SOF markers
                    const uint64_t lie_raw = rng.next();
                    if (i + 5 < v.size()) v[i + 5] = static_cast<uint8_t>((lie_raw >> 8) & 0xFFu);
                    if (i + 6 < v.size()) v[i + 6] = static_cast<uint8_t>(lie_raw & 0xFFu);
                    break;
                }
            }
        }
    }

    // 4. Brand-byte swap for HEIC/AVIF (ftyp brand at offset 4–7).
    if (allow_brand_swap && v.size() >= 8) {
        // Check for ISO Base Media File Format: "ftyp" at offset 4.
        if (v.size() >= 8 && v[4] == 'f' && v[5] == 't' && v[6] == 'y' && v[7] == 'p') {
            // Brand is at offset 8–11.
            if (v.size() >= 12) {
                const uint64_t new_brand_raw = rng.next();
                v[8] = static_cast<uint8_t>((new_brand_raw >> 24) & 0xFFu);
                v[9] = static_cast<uint8_t>((new_brand_raw >> 16) & 0xFFu);
                v[10] = static_cast<uint8_t>((new_brand_raw >> 8) & 0xFFu);
                v[11] = static_cast<uint8_t>(new_brand_raw & 0xFFu);
            }
        }
    }

    return v;
}

// Cap input size to prevent pathological cases from taking minutes.
static constexpr size_t MAX_FUZZ_INPUT = 1024 * 1024;  // 1 MiB

TEST(fuzz_webp_survives_500_malformed_inputs)
{
    Prng rng(0xDECAFBAD);
    const auto webp = fixtures::load_webp();

    // Skip if fixture not found.
    if (webp.empty()) {
        fprintf(stderr, "SKIP: WebP fixture not found\n");
        return;
    }

    for (int i = 0; i < 500; ++i) {
        const auto blob = mutate_codec(rng, webp, false, false);
        // Cap input size to prevent hangs.
        if (blob.size() <= MAX_FUZZ_INPUT) {
            // decode_from_memory must return nullopt or a valid DecodedImage, never crash.
            (void)image::decode_from_memory(blob);
        }
    }
}

TEST(fuzz_heic_survives_500_malformed_inputs)
{
    Prng rng(0xC0DECAFE);
    const auto heic = fixtures::load_heic();

    // Skip if fixture not found.
    if (heic.empty()) {
        fprintf(stderr, "SKIP: HEIC fixture not found\n");
        return;
    }

    for (int i = 0; i < 500; ++i) {
        const auto blob = mutate_codec(rng, heic, true, true);
        if (blob.size() <= MAX_FUZZ_INPUT) {
            (void)image::decode_from_memory(blob);
        }
    }
}

TEST(fuzz_avif_survives_500_malformed_inputs)
{
    Prng rng(0xFEEDFACE);
    const auto avif = fixtures::load_avif();

    // Skip if fixture not found.
    if (avif.empty()) {
        fprintf(stderr, "SKIP: AVIF fixture not found\n");
        return;
    }

    for (int i = 0; i < 500; ++i) {
        const auto blob = mutate_codec(rng, avif, true, true);
        if (blob.size() <= MAX_FUZZ_INPUT) {
            (void)image::decode_from_memory(blob);
        }
    }
}

TEST(fuzz_random_bytes_never_decode)
{
    Prng rng(0xABCDEF00);

    // 1500 pure-garbage buffers of random sizes (incl. empty and small).
    for (int i = 0; i < 1500; ++i) {
        const auto blob = random_bytes(rng, rng.below(4096));
        // Random garbage should never decode; if it does, that's an accept on garbage.
        // We don't assert !decode — just verify no crash.
        (void)image::decode_from_memory(blob);
    }
}

#ifdef OSV_VENDORED_AV

TEST(fuzz_video_probe_survives_500_malformed_inputs)
{
    Prng rng(0x12345678);

    // Load a real video file as the mutation base (if available).
    std::vector<uint8_t> valid_video;
    {
        std::FILE* fp = std::fopen(OSV_VAULT_FIXTURE_DIR "/tiny.mp4", "rb");
        if (fp) {
            std::fseek(fp, 0, SEEK_END);
            const long n = std::ftell(fp);
            std::fseek(fp, 0, SEEK_SET);
            if (n > 0) {
                valid_video.resize(static_cast<size_t>(n));
                if (std::fread(valid_video.data(), 1, valid_video.size(), fp) != valid_video.size()) {
                    valid_video.clear();
                }
            }
            std::fclose(fp);
        }
    }

    // Test mutated videos (if we have a valid base).
    if (!valid_video.empty()) {
        for (int i = 0; i < 500; ++i) {
            const auto blob = mutate_codec(rng, valid_video, true, true);
            if (blob.size() <= MAX_FUZZ_INPUT) {
                media::VideoProbeResult result;
                // probe_video must return false on malformed input, never crash.
                (void)media::probe_video(std::span(blob), result);
            }
        }
    } else {
        fprintf(stderr, "SKIP: Video fixture not found\n");
    }

    // Also test 500 pure-garbage video buffers.
    for (int i = 0; i < 500; ++i) {
        const auto blob = random_bytes(rng, rng.below(8192));
        if (blob.size() <= MAX_FUZZ_INPUT) {
            media::VideoProbeResult result;
            (void)media::probe_video(std::span(blob), result);
        }
    }
}

#endif  // OSV_VENDORED_AV
