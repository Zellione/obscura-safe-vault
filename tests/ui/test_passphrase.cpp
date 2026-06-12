#include "test_framework.h"

#include <set>
#include <string>
#include <string_view>

#include "ui/passphrase.h"
#include "ui/secure_text_field.h"

// Phase 7: passphrase strength estimation + diceware-style generation.

static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

TEST(passphrase_entropy_of_empty_is_zero)
{
    CHECK_EQ(ui::estimate_entropy_bits({}), 0.0);
    CHECK_EQ(ui::classify_strength({}), ui::Strength::Weak);
}

TEST(passphrase_short_lowercase_is_weak)
{
    // 8 lowercase chars ~ 8 * log2(26) = 37.6 bits.
    CHECK_EQ(ui::classify_strength(bytes("abcdefgh")), ui::Strength::Weak);
    // Mixed-class but still short: 8 * log2(62) = 47.6 bits.
    CHECK_EQ(ui::classify_strength(bytes("Abcdef12")), ui::Strength::Weak);
}

TEST(passphrase_medium_and_strong_classification)
{
    // 13 chars upper+lower+digit ~ 13 * log2(62) = 77.4 bits -> Medium.
    CHECK_EQ(ui::classify_strength(bytes("Abcdefgh12345")), ui::Strength::Medium);
    // Adding symbols + length pushes past 80 bits -> Strong.
    CHECK_EQ(ui::classify_strength(bytes("Abcdefgh12345!?#")), ui::Strength::Strong);
    // Long all-lowercase diceware-style phrase is Strong by sheer length.
    CHECK_EQ(ui::classify_strength(bytes("correct horse battery staple")),
             ui::Strength::Strong);
}

TEST(passphrase_entropy_grows_with_length_and_classes)
{
    const double lower    = ui::estimate_entropy_bits(bytes("abcdefgh"));
    const double longer   = ui::estimate_entropy_bits(bytes("abcdefghijkl"));
    const double mixed    = ui::estimate_entropy_bits(bytes("abcdefG8"));
    CHECK_TRUE(lower > 0.0);
    CHECK_TRUE(longer > lower);
    CHECK_TRUE(mixed > lower);
}

TEST(passphrase_strength_labels_are_distinct)
{
    const std::string_view w = ui::strength_label(ui::Strength::Weak);
    const std::string_view m = ui::strength_label(ui::Strength::Medium);
    const std::string_view s = ui::strength_label(ui::Strength::Strong);
    CHECK_TRUE(!w.empty() && !m.empty() && !s.empty());
    CHECK_TRUE(w != m && m != s && w != s);
}

TEST(passphrase_wordlist_is_well_formed)
{
    std::set<std::string_view> seen;
    for (size_t i = 0; i < ui::PASSPHRASE_WORDLIST_SIZE; ++i) {
        const std::string_view w = ui::passphrase_word(i);
        REQUIRE(!w.empty());
        CHECK_TRUE(w.size() >= 3 && w.size() <= 8);
        for (char c : w) CHECK_TRUE(c >= 'a' && c <= 'z');
        seen.insert(w);
    }
    // Duplicates would silently reduce the entropy per word.
    CHECK_EQ(seen.size(), ui::PASSPHRASE_WORDLIST_SIZE);
}

TEST(passphrase_generation_produces_valid_strong_phrase)
{
    ui::SecureTextField out;
    out.push_utf8("stale");  // must be cleared by generate
    REQUIRE(ui::generate_passphrase(out));

    const auto b = out.bytes();
    REQUIRE(!b.empty());
    const std::string_view phrase{reinterpret_cast<const char*>(b.data()), b.size()};

    // Split on single spaces; every token must be a wordlist word.
    int    words = 0;
    size_t pos   = 0;
    while (pos <= phrase.size()) {
        const size_t sp  = phrase.find(' ', pos);
        const auto   tok = phrase.substr(pos, sp == std::string_view::npos ? sp : sp - pos);
        REQUIRE(!tok.empty());
        bool in_list = false;
        for (size_t i = 0; i < ui::PASSPHRASE_WORDLIST_SIZE && !in_list; ++i)
            in_list = (tok == ui::passphrase_word(i));
        CHECK_TRUE(in_list);
        ++words;
        if (sp == std::string_view::npos) break;
        pos = sp + 1;
    }
    CHECK_EQ(words, ui::PASSPHRASE_DEFAULT_WORDS);
    CHECK_EQ(ui::classify_strength(b), ui::Strength::Strong);
}

TEST(passphrase_generation_is_random)
{
    ui::SecureTextField a;
    ui::SecureTextField b;
    REQUIRE(ui::generate_passphrase(a));
    REQUIRE(ui::generate_passphrase(b));
    // 8 words of 8 bits each: a collision is a 2^-64 event.
    CHECK_FALSE(testing::bytes_equal(a.bytes(), b.bytes()));
}

TEST(passphrase_generation_rejects_bad_word_count)
{
    ui::SecureTextField out;
    CHECK_FALSE(ui::generate_passphrase(out, 0));
    CHECK_FALSE(ui::generate_passphrase(out, -3));
}
