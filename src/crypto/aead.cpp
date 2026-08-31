#include "aead.h"

#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

#include <monocypher.h>

#include "random.h"
#include "platform/safe_print.h"

namespace crypto {

std::array<uint8_t, AD_SIZE> build_chunk_ad(const ChunkTag& t) noexcept
{
    std::array<uint8_t, AD_SIZE> ad{};
    ad[0] = std::to_underlying(t.domain);
    ad[1] = CHUNK_AD_VERSION;
    for (size_t i = 0; i < NODE_ID_SIZE; ++i) {
        ad[2 + i]         = t.owner[i];
        ad[2 + NODE_ID_SIZE + i] = t.record[i];
    }
    for (size_t i = 0; i < 4; ++i) {
        ad[2 + 2 * NODE_ID_SIZE + i] = static_cast<uint8_t>(t.sequence >> (8 * i));
    }
    return ad;
}

bool encrypt_chunk(std::span<const uint8_t, KEY_SIZE> key,
                   std::span<const uint8_t>           plaintext,
                   std::vector<uint8_t>&              out,
                   std::span<const uint8_t>           ad) noexcept
{
    out.clear();
    if (plaintext.size() > out.max_size() - NONCE_SIZE - TAG_SIZE) return false;
    try {
        out.assign(NONCE_SIZE + plaintext.size() + TAG_SIZE, 0);
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }

    uint8_t* nonce  = out.data();
    uint8_t* cipher = out.data() + NONCE_SIZE;
    uint8_t* tag    = out.data() + NONCE_SIZE + plaintext.size();

    if (!fill_random(std::span<uint8_t>(nonce, NONCE_SIZE))) {
        platform::safe_println(stderr, "[crypto::aead] nonce generation failed");
        out.clear();
        return false;
    }

    crypto_aead_lock(cipher, tag, key.data(), nonce,
                     ad.data(), ad.size(),
                     plaintext.data(), plaintext.size());
    return true;
}

bool decrypt_chunk_to(std::span<const uint8_t, KEY_SIZE> key,
                      std::span<const uint8_t>           chunk,
                      std::span<uint8_t>                 out,
                      std::span<const uint8_t>           ad) noexcept
{
    if (chunk.size() < NONCE_SIZE + TAG_SIZE) {
        return false;  // too small to even hold nonce + tag
    }

    const size_t   cipher_len = chunk.size() - NONCE_SIZE - TAG_SIZE;
    if (out.size() != cipher_len) {
        return false;  // caller mis-sized the output buffer
    }
    const uint8_t* nonce  = chunk.data();
    const uint8_t* cipher = chunk.data() + NONCE_SIZE;

    // crypto_aead_unlock verifies the Poly1305 tag before writing plaintext and
    // returns -1 on mismatch (tamper / wrong key). On failure we wipe the output.
    if (const uint8_t* tag = chunk.data() + NONCE_SIZE + cipher_len;
        crypto_aead_unlock(out.data(), tag, key.data(), nonce,
                           ad.data(), ad.size(),
                           cipher, cipher_len) != 0) {
        if (!out.empty()) crypto_wipe(out.data(), out.size());
        return false;
    }
    return true;
}

bool decrypt_chunk(std::span<const uint8_t, KEY_SIZE> key,
                   std::span<const uint8_t>           chunk,
                   std::vector<uint8_t>&              out_plaintext,
                   std::span<const uint8_t>           ad) noexcept
{
    out_plaintext.clear();
    try {
        out_plaintext.assign(chunk_plaintext_len(chunk.size()), 0);
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
    if (!decrypt_chunk_to(key, chunk, std::span<uint8_t>(out_plaintext), ad)) {
        out_plaintext.clear();
        return false;
    }
    return true;
}

bool seal(std::span<const uint8_t, KEY_SIZE>   key,
          std::span<const uint8_t, NONCE_SIZE> nonce,
          std::span<const uint8_t>             plaintext,
          std::vector<uint8_t>&                out,
          std::span<const uint8_t>             ad) noexcept
{
    out.clear();
    if (plaintext.size() > out.max_size() - TAG_SIZE) return false;
    try {
        out.assign(plaintext.size() + TAG_SIZE, 0);
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }

    uint8_t* cipher = out.data();
    uint8_t* tag    = out.data() + plaintext.size();

    crypto_aead_lock(cipher, tag, key.data(), nonce.data(),
                     ad.data(), ad.size(),
                     plaintext.data(), plaintext.size());
    return true;
}

bool open_to(std::span<const uint8_t, KEY_SIZE>   key,
             std::span<const uint8_t, NONCE_SIZE> nonce,
             std::span<const uint8_t>             sealed,
             std::span<uint8_t>                   out,
             std::span<const uint8_t>             ad) noexcept
{
    if (sealed.size() < TAG_SIZE) {
        return false;  // too small to even hold the tag
    }
    const size_t cipher_len = sealed.size() - TAG_SIZE;
    if (out.size() != cipher_len) {
        return false;  // caller mis-sized the output buffer
    }
    const uint8_t* cipher = sealed.data();

    if (const uint8_t* tag = sealed.data() + cipher_len;
        crypto_aead_unlock(out.data(), tag, key.data(), nonce.data(),
                           ad.data(), ad.size(),
                           cipher, cipher_len) != 0) {
        if (!out.empty()) crypto_wipe(out.data(), out.size());
        return false;
    }
    return true;
}

bool open(std::span<const uint8_t, KEY_SIZE>   key,
          std::span<const uint8_t, NONCE_SIZE> nonce,
          std::span<const uint8_t>             sealed,
          std::vector<uint8_t>&                out_plaintext,
          std::span<const uint8_t>             ad) noexcept
{
    out_plaintext.clear();
    try {
        out_plaintext.assign(sealed.size() < TAG_SIZE ? 0 : sealed.size() - TAG_SIZE, 0);
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
    if (!open_to(key, nonce, sealed, std::span<uint8_t>(out_plaintext), ad)) {
        out_plaintext.clear();
        return false;
    }
    return true;
}

} // namespace crypto
