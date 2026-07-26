#include "test_framework.h"

#include <memory>
#include <string>
#include <string_view>

#include "text_input_conformance.h"
#include "ui/secure_text_input.h"

namespace {

std::span<const uint8_t> bytes_of(std::string_view s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

// True if every byte of `raw[from, to)` reads back as zero.
bool all_zero(const uint8_t* raw, size_t from, size_t to)
{
    for (size_t i = from; i < to; ++i)
        if (raw[i] != 0) return false;
    return true;
}

} // namespace

// --- The shared suite, run against mlock'd storage -------------------------

TEST(sti_conforms_to_the_shared_input_semantics)
{
    tests::check_text_input_conformance(
        [](size_t cap) -> std::unique_ptr<ui::ITextInput> {
            return std::make_unique<ui::SecureTextInput>(cap);
        },
        _failures);
}

// --- Coverage inherited from the retired SecureTextField -------------------

TEST(sti_push_and_bytes)
{
    ui::SecureTextInput f;
    f.insert("abc");
    CHECK_EQ(f.size(), size_t{3});
    CHECK_BYTES_EQ(f.bytes(), bytes_of("abc"));
}

TEST(sti_backspace_and_clear)
{
    ui::SecureTextInput f;
    f.insert("hello");
    f.backspace();
    CHECK_EQ(f.size(), size_t{4});
    CHECK_BYTES_EQ(f.bytes(), bytes_of("hell"));
    f.clear();
    CHECK_EQ(f.size(), size_t{0});
    CHECK(f.empty());
    f.insert("x");             // usable after clear
    CHECK_BYTES_EQ(f.bytes(), bytes_of("x"));
}

TEST(sti_capacity_clamp)
{
    ui::SecureTextInput f(4);
    f.insert("abcdef");
    CHECK_EQ(f.size(), size_t{4});
    CHECK_BYTES_EQ(f.bytes(), bytes_of("abcd"));
    CHECK_EQ(f.byte_cap(), size_t{4});
}

TEST(sti_text_view_aliases_the_locked_buffer)
{
    ui::SecureTextInput f;
    f.insert("secret");
    CHECK(f.text_view() == std::string_view("secret"));
    CHECK(reinterpret_cast<const uint8_t*>(f.text_view().data()) == f.bytes().data());
}

// --- Invariant #2: every mutation wipes the bytes it vacates ---------------

// clear() wipes in place (no reallocation), so a pointer taken before clear()
// still addresses the same buffer and must read back as zero.
TEST(sti_memory_wiped_after_clear)
{
    ui::SecureTextInput f(16);
    f.insert("secret");
    const uint8_t* raw = f.bytes().data();
    REQUIRE(raw != nullptr);
    REQUIRE(f.size() == size_t{6});
    f.clear();
    CHECK(all_zero(raw, 0, 6));
}

TEST(sti_backspace_wipes_the_vacated_byte)
{
    ui::SecureTextInput f(16);
    f.insert("secret");
    const uint8_t* raw = f.bytes().data();
    REQUIRE(raw != nullptr);
    f.backspace();
    CHECK_EQ(f.size(), size_t{5});
    CHECK(all_zero(raw, 5, 6));          // the dropped 't' is gone, not just unreferenced
}

TEST(sti_deleting_a_selection_wipes_the_whole_tail)
{
    ui::SecureTextInput f(64);
    f.insert("correct horse battery staple");
    const uint8_t* raw = f.bytes().data();
    const size_t   old = f.size();
    REQUIRE(raw != nullptr);
    f.move_home(false);
    f.move_right(true, true);            // select "correct"
    f.delete_selection();
    CHECK_EQ(f.size(), old - 7);
    CHECK(all_zero(raw, f.size(), old));
}

TEST(sti_select_all_then_delete_wipes_everything)
{
    ui::SecureTextInput f(32);
    f.insert("hunter2hunter2");
    const uint8_t* raw = f.bytes().data();
    const size_t   old = f.size();
    REQUIRE(raw != nullptr);
    f.select_all();
    f.del();
    CHECK(f.empty());
    CHECK(all_zero(raw, 0, old));
}

TEST(sti_replacing_a_selection_with_shorter_text_wipes_the_remainder)
{
    ui::SecureTextInput f(32);
    f.insert("passphrase");
    const uint8_t* raw = f.bytes().data();
    const size_t   old = f.size();
    REQUIRE(raw != nullptr);
    f.select_all();
    f.insert("pw");
    CHECK_EQ(f.size(), size_t{2});
    CHECK(all_zero(raw, 2, old));
}

TEST(sti_set_text_wipes_the_previous_secret)
{
    ui::SecureTextInput f(32);
    f.insert("old-secret-value");
    const uint8_t* raw = f.bytes().data();
    const size_t   old = f.size();
    REQUIRE(raw != nullptr);
    f.set_text("ab");
    CHECK_EQ(f.size(), size_t{2});
    CHECK(all_zero(raw, 2, old));
}

// --- Copy/cut are structurally unavailable ---------------------------------

TEST(sti_is_secure_and_never_surrenders_its_selection)
{
    ui::SecureTextInput f;
    f.insert("topsecret");
    f.select_all();
    CHECK(f.secure());
    CHECK(f.has_selection());
    // Even with a full selection, the plaintext never leaves the field: the
    // shared handler suppresses Ctrl+C/Ctrl+X, and this is the second line of
    // defence for a caller that asks anyway.
    CHECK_EQ(f.selection_text(), std::string(""));
}

TEST(sti_a_failed_allocation_degrades_to_an_inert_field)
{
    ui::SecureTextInput f(0);            // zero-capacity: nothing can be stored
    CHECK_EQ(f.byte_cap(), size_t{0});
    f.insert("anything");
    CHECK(f.empty());
    f.backspace();                       // must not touch the null buffer
    f.select_all();
    f.del();
    CHECK(f.empty());
    CHECK_EQ(f.caret(), size_t{0});
}
