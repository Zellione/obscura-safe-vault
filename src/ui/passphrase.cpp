#include "ui/passphrase.h"

#include <array>
#include <cmath>

#include <monocypher.h>

#include "crypto/random.h"

namespace ui {

namespace {

// 256 distinct, easy-to-type lowercase words (3-8 letters). Exactly 256 so a
// single CSPRNG byte selects a word with zero modulo bias: 8 bits per word.
constexpr std::array<std::string_view, PASSPHRASE_WORDLIST_SIZE> WORDLIST = {
    "acorn",   "actor",   "alarm",   "amber",   "anchor",  "animal",  "apple",   "april",
    "arrow",   "atlas",   "autumn",  "badge",   "baker",   "bamboo",  "banana",  "barrel",
    "basket",  "beach",   "berry",   "birch",   "bison",   "blade",   "blanket", "blossom",
    "bottle",  "branch",  "brave",   "bread",   "breeze",  "brick",   "bridge",  "bronze",
    "brush",   "bucket",  "butter",  "button",  "cabin",   "cable",   "camera",  "candle",
    "canoe",   "canyon",  "carbon",  "carpet",  "carrot",  "castle",  "cedar",   "cellar",
    "chalk",   "cherry",  "chess",   "chimney", "circle",  "citrus",  "clover",  "cobalt",
    "coffee",  "comet",   "copper",  "coral",   "cotton",  "cradle",  "crane",   "crater",
    "cricket", "crystal", "daisy",   "dancer",  "delta",   "desert",  "diamond", "dinner",
    "dolphin", "donkey",  "dragon",  "drum",    "eagle",   "early",   "earth",   "echo",
    "elbow",   "ember",   "emerald", "engine",  "fabric",  "falcon",  "feather", "fence",
    "fiddle",  "field",   "finch",   "flame",   "flock",   "flower",  "forest",  "fossil",
    "fountain","frost",   "garden",  "garlic",  "giant",   "ginger",  "glacier", "globe",
    "glove",   "golden",  "goose",   "grape",   "gravel",  "green",   "grove",   "guitar",
    "hammer",  "harbor",  "harvest", "hazel",   "heron",   "hidden",  "hollow",  "honey",
    "horizon", "horse",   "hotel",   "humble",  "hunter",  "igloo",   "insect",  "indigo",
    "iron",    "island",  "ivory",   "jacket",  "jaguar",  "jelly",   "jewel",   "journey",
    "jungle",  "kayak",   "kettle",  "kitten",  "ladder",  "lagoon",  "lantern", "laurel",
    "lemon",   "lentil",  "letter",  "lily",    "linen",   "lizard",  "lobster", "locket",
    "lotus",   "lumber",  "magnet",  "mango",   "maple",   "marble",  "meadow",  "melon",
    "mirror",  "mitten",  "monkey",  "morning", "mosaic",  "mountain","muffin",  "mushroom",
    "music",   "napkin",  "nectar",  "needle",  "night",   "noble",   "north",   "nutmeg",
    "ocean",   "olive",   "onion",   "opera",   "orange",  "orbit",   "orchard", "otter",
    "oyster",  "paddle",  "palace",  "panda",   "pantry",  "paper",   "parade",  "parrot",
    "pearl",   "pebble",  "pencil",  "pepper",  "piano",   "pillow",  "pilot",   "planet",
    "pocket",  "pollen",  "pony",    "poppy",   "portal",  "prairie", "pretzel", "pumpkin",
    "puzzle",  "quartz",  "quiet",   "rabbit",  "raccoon", "radio",   "rainbow", "raisin",
    "raven",   "reef",    "ribbon",  "river",   "rocket",  "rooster", "rustic",  "saddle",
    "salmon",  "sandal",  "sapphire","scarf",   "shadow",  "shell",   "silver",  "sleigh",
    "socket",  "spider",  "spiral",  "spring",  "squash",  "stone",   "storm",   "sugar",
    "summer",  "sunset",  "table",   "tailor",  "temple",  "thunder", "tiger",   "timber",
    "toast",   "tomato",  "torch",   "tulip",   "turtle",  "velvet",  "violet",  "walnut",
};

constexpr double WEAK_BELOW_BITS   = 50.0;
constexpr double STRONG_FROM_BITS  = 80.0;

} // namespace

double estimate_entropy_bits(std::span<const uint8_t> password) noexcept
{
    if (password.empty()) return 0.0;

    bool lower = false;
    bool upper = false;
    bool digit = false;
    bool other = false;
    for (const uint8_t b : password) {
        if (b >= 'a' && b <= 'z')      lower = true;
        else if (b >= 'A' && b <= 'Z') upper = true;
        else if (b >= '0' && b <= '9') digit = true;
        else                           other = true;
    }

    int pool = 0;
    if (lower) pool += 26;
    if (upper) pool += 26;
    if (digit) pool += 10;
    if (other) pool += 33;  // printable ASCII symbols incl. space
    return static_cast<double>(password.size()) * std::log2(static_cast<double>(pool));
}

Strength classify_strength(std::span<const uint8_t> password) noexcept
{
    using enum Strength;
    const double bits = estimate_entropy_bits(password);
    if (bits < WEAK_BELOW_BITS)  return Weak;
    if (bits < STRONG_FROM_BITS) return Medium;
    return Strong;
}

std::string_view strength_label(Strength s) noexcept
{
    using enum Strength;
    switch (s) {
        case Weak:   return "weak";
        case Medium: return "medium";
        case Strong: return "strong";
    }
    return "weak";
}

std::string_view passphrase_word(size_t i) noexcept
{
    return i < WORDLIST.size() ? WORDLIST[i] : std::string_view{};
}

bool generate_passphrase(SecureTextField& out, int words)
{
    out.clear();
    if (words < 1 || words > PASSPHRASE_MAX_WORDS) return false;

    // The selector bytes determine the passphrase, so they are wiped like a key.
    std::array<uint8_t, PASSPHRASE_MAX_WORDS> pick{};
    if (const std::span<uint8_t> used(pick.data(), static_cast<size_t>(words));
        !crypto::fill_random(used)) {
        return false;
    }

    for (int i = 0; i < words; ++i) {
        if (i > 0) out.push_utf8(" ");
        out.push_utf8(WORDLIST[pick[static_cast<size_t>(i)]]);
    }
    crypto_wipe(pick.data(), pick.size());
    return true;
}

} // namespace ui
