#include "test_framework.h"

#include <string>
#include <string_view>

#include "vault/safe_name.h"

using vault::is_safe_node_name;
using vault::sanitize_node_name;
using vault::MAX_NODE_NAME_BYTES;

// The attack vectors named in SonarSource's cpp:S2083 rule text, plus the
// Windows-specific ones an exported name would hit on the user's filesystem.
// A node name is a single path COMPONENT: it must never be able to steer the
// export path out of the destination folder.
static const std::string kHostile[] = {
    "../../etc/passwd",
    "..\\..\\etc\\passwd",
    "/etc/passwd",
    "uploads/../../../etc/passwd",
    "..",
    ".",
    "",
    std::string("file\0.txt", 9),   // embedded NUL: truncates the C string fopen() gets
    std::string(300, 'a'),          // over the 255-byte component limit
    "CON",
    "com1.jpg",
    "nul",
    "LPT9.png",
    "name.",                        // trailing dot   — silently stripped by Windows
    "name ",                        // trailing space — silently stripped by Windows
    "a<b>c:d\"e|f?g*h.jpg",         // Windows-reserved characters
    "bell\x07.jpg",                 // control character
};

// Names that are perfectly ordinary and must keep working untouched.
static const std::string kBenign[] = {
    "photo.jpg",
    "01.png",
    "cat (1).jpg",
    "my photo - final.jpeg",
    "\xe5\x86\x99\xe7\x9c\x9f.png",   // 写真.png — CJK names are normal in comic archives
    "a.b.c.tar.gz",
    "_leading_underscore",
    "..hidden.jpg",                   // leading dots are fine; only "." / ".." are not
};

// --- is_safe_node_name -----------------------------------------------------

TEST(safe_name_rejects_hostile_names)
{
    for (const std::string& n : kHostile)
        CHECK_FALSE(is_safe_node_name(n));
}

TEST(safe_name_accepts_benign_names)
{
    for (const std::string& n : kBenign)
        CHECK_TRUE(is_safe_node_name(n));
}

TEST(safe_name_length_boundary)
{
    CHECK_TRUE(is_safe_node_name(std::string(MAX_NODE_NAME_BYTES, 'a')));
    CHECK_FALSE(is_safe_node_name(std::string(MAX_NODE_NAME_BYTES + 1, 'a')));
}

TEST(safe_name_reserved_device_names_are_case_insensitive)
{
    CHECK_FALSE(is_safe_node_name("con"));
    CHECK_FALSE(is_safe_node_name("CoN"));
    CHECK_FALSE(is_safe_node_name("PRN.txt"));
    CHECK_FALSE(is_safe_node_name("aux"));
    CHECK_FALSE(is_safe_node_name("COM3.jpeg"));
    // Not reserved: the device name must be the whole stem.
    CHECK_TRUE(is_safe_node_name("console.jpg"));
    CHECK_TRUE(is_safe_node_name("com10.jpg"));
    CHECK_TRUE(is_safe_node_name("my_con.png"));
}

// --- sanitize_node_name ----------------------------------------------------

// The load-bearing property: whatever goes in, what comes out is always a name
// is_safe_node_name() accepts. Every importer relies on this, because the vault
// ingress (Vault::add_image) hard-rejects anything else.
TEST(safe_name_sanitize_always_yields_a_safe_name)
{
    for (const std::string& n : kHostile)
        CHECK_TRUE(is_safe_node_name(sanitize_node_name(n)));
    for (const std::string& n : kBenign)
        CHECK_TRUE(is_safe_node_name(sanitize_node_name(n)));
}

TEST(safe_name_sanitize_leaves_benign_names_untouched)
{
    for (const std::string& n : kBenign)
        CHECK_EQ(sanitize_node_name(n), n);
}

TEST(safe_name_sanitize_neutralises_separators_and_traversal)
{
    CHECK_EQ(sanitize_node_name("../../etc/passwd"), std::string(".._.._etc_passwd"));
    CHECK_EQ(sanitize_node_name("/etc/passwd"), std::string("_etc_passwd"));
    CHECK_EQ(sanitize_node_name("..\\..\\evil.jpg"), std::string(".._.._evil.jpg"));
}

TEST(safe_name_sanitize_replaces_nul_and_control_bytes)
{
    CHECK_EQ(sanitize_node_name(std::string("file\0.txt", 9)), std::string("file_.txt"));
    CHECK_EQ(sanitize_node_name("bell\x07.jpg"), std::string("bell_.jpg"));
}

TEST(safe_name_sanitize_falls_back_when_nothing_survives)
{
    // "." and ".." reduce to nothing once trailing dots are stripped.
    CHECK_EQ(sanitize_node_name("."), std::string("unnamed"));
    CHECK_EQ(sanitize_node_name(".."), std::string("unnamed"));
    CHECK_EQ(sanitize_node_name(""), std::string("unnamed"));
    CHECK_EQ(sanitize_node_name("   "), std::string("unnamed"));
}

TEST(safe_name_sanitize_escapes_reserved_device_names)
{
    CHECK_TRUE(is_safe_node_name(sanitize_node_name("CON")));
    CHECK_TRUE(is_safe_node_name(sanitize_node_name("com1.jpg")));
    CHECK_FALSE(sanitize_node_name("CON").empty());
}

TEST(safe_name_sanitize_truncates_over_long_names)
{
    const std::string out = sanitize_node_name(std::string(300, 'a'));
    CHECK_TRUE(out.size() <= MAX_NODE_NAME_BYTES);
    CHECK_TRUE(is_safe_node_name(out));
}

// Truncation must not slice a multi-byte UTF-8 codepoint in half — a name is
// stored as opaque UTF-8 (see src/vault/index.cpp) and a torn sequence would
// render as a replacement glyph forever after.
TEST(safe_name_sanitize_truncates_on_a_utf8_boundary)
{
    // 2-byte codepoints, so the 255-byte cut lands mid-sequence and the back-off
    // is genuinely exercised (a 3-byte codepoint would divide 255 exactly).
    std::string wide;
    for (int i = 0; i < 200; ++i) wide += "\xc3\xa9";   // é, 2 bytes each
    const std::string out = sanitize_node_name(wide);

    CHECK_TRUE(out.size() <= MAX_NODE_NAME_BYTES);
    CHECK_EQ(out.size() % 2, 0u);           // whole codepoints only
    CHECK_EQ(out.size(), size_t{254});      // 255 would tear the 128th 'é' in half
    CHECK_TRUE(is_safe_node_name(out));
}
