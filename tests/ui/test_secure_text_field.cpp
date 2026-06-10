#include "test_framework.h"

#include <string_view>

#include "ui/secure_text_field.h"

static std::span<const uint8_t> bytes_of(std::string_view s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

TEST(stf_push_and_bytes)
{
    ui::SecureTextField f;
    f.push_utf8("abc");
    CHECK_EQ(f.length(), size_t{3});
    CHECK_BYTES_EQ(f.bytes(), bytes_of("abc"));
}

TEST(stf_backspace_and_clear)
{
    ui::SecureTextField f;
    f.push_utf8("hello");
    f.backspace();
    CHECK_EQ(f.length(), size_t{4});
    CHECK_BYTES_EQ(f.bytes(), bytes_of("hell"));
    f.clear();
    CHECK_EQ(f.length(), size_t{0});
    CHECK(f.empty());
    f.push_utf8("x");          // usable after clear
    CHECK_BYTES_EQ(f.bytes(), bytes_of("x"));
}

TEST(stf_capacity_clamp)
{
    ui::SecureTextField f(4);
    f.push_utf8("abcdef");
    CHECK_EQ(f.length(), size_t{4});
    CHECK_BYTES_EQ(f.bytes(), bytes_of("abcd"));
}
