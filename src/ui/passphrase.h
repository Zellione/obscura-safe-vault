#pragma once

// Passphrase strength estimation + diceware-style generation (Phase 7).
//
// The password is the vault's real security boundary (CLAUDE.md), so the
// create-vault flow shows a strength estimate and offers a randomly generated
// passphrase. Estimation uses the standard character-class model: pool size
// from the classes present, entropy = length * log2(pool). That overestimates
// human-pattern passwords, so the thresholds are deliberately conservative.
//
// Generation samples words from an embedded 256-word list with one CSPRNG byte
// per word (256 entries make byte-sampling exactly uniform): 8 bits/word, 64
// bits for the default 8 words — on top of Argon2id's work factor. Generated
// bytes land directly in a SecureTextField (mlock'd, wiped), never std::string.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "ui/secure_text_field.h"

namespace ui {

enum class Strength : uint8_t { Weak, Medium, Strong };

// Char-class entropy estimate in bits (0 for an empty password).
[[nodiscard]] double estimate_entropy_bits(std::span<const uint8_t> password) noexcept;

// Weak < 50 bits <= Medium < 80 bits <= Strong.
[[nodiscard]] Strength classify_strength(std::span<const uint8_t> password) noexcept;

// Short user-facing label ("weak" / "medium" / "strong").
[[nodiscard]] std::string_view strength_label(Strength s) noexcept;

inline constexpr size_t PASSPHRASE_WORDLIST_SIZE = 256;
inline constexpr int    PASSPHRASE_DEFAULT_WORDS = 8;   // 8 bits/word -> 64 bits
inline constexpr int    PASSPHRASE_MAX_WORDS     = 32;

// The i-th wordlist entry (i < PASSPHRASE_WORDLIST_SIZE). Exposed for tests.
[[nodiscard]] std::string_view passphrase_word(size_t i) noexcept;

// Replace `out` with `words` random space-separated wordlist words. Returns
// false (leaving `out` cleared) on a bad word count or CSPRNG failure.
[[nodiscard]] bool generate_passphrase(SecureTextField& out,
                                       int words = PASSPHRASE_DEFAULT_WORDS);

} // namespace ui
