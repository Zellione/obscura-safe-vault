#include "test_framework.h"

#include <memory>
#include <string>
#include <string_view>

#include "text_input_conformance.h"
#include "ui/text_input_model.h"

namespace {

std::span<const uint8_t> bytes_of(std::string_view s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

constexpr std::string_view HELLO = "h\xC3\xA9llo";       // 6 bytes, 5 characters
constexpr std::string_view EMOJI = "\xF0\x9F\x98\x80";   // 4 bytes, 1 character

} // namespace

// --- The shared suite, run against std::string storage ---------------------

TEST(tim_conforms_to_the_shared_input_semantics)
{
    tests::check_text_input_conformance(
        [](size_t cap) -> std::unique_ptr<ui::ITextInput> {
            return std::make_unique<ui::TextInputModel>(cap);
        },
        _failures);
}

// --- UTF-8 helpers ---------------------------------------------------------

TEST(tim_utf8_boundaries_walk_whole_characters)
{
    const auto b = bytes_of(HELLO);
    // Forward: 0 -> 1 (h) -> 3 (é) -> 4 -> 5 -> 6.
    CHECK_EQ(ui::utf8_next_boundary(b, 0), size_t{1});
    CHECK_EQ(ui::utf8_next_boundary(b, 1), size_t{3});
    CHECK_EQ(ui::utf8_next_boundary(b, 3), size_t{4});
    CHECK_EQ(ui::utf8_next_boundary(b, 6), size_t{6});   // clamps at the end
    // Backward mirrors it.
    CHECK_EQ(ui::utf8_prev_boundary(b, 6), size_t{5});
    CHECK_EQ(ui::utf8_prev_boundary(b, 4), size_t{3});
    CHECK_EQ(ui::utf8_prev_boundary(b, 3), size_t{1});
    CHECK_EQ(ui::utf8_prev_boundary(b, 0), size_t{0});   // clamps at the start
}

TEST(tim_utf8_char_count)
{
    CHECK_EQ(ui::utf8_char_count(bytes_of("")), size_t{0});
    CHECK_EQ(ui::utf8_char_count(bytes_of("abc")), size_t{3});
    CHECK_EQ(ui::utf8_char_count(bytes_of(HELLO)), size_t{5});
    CHECK_EQ(ui::utf8_char_count(bytes_of(EMOJI)), size_t{1});
}

TEST(tim_acceptable_input_run_stops_at_the_first_rejected_byte)
{
    CHECK_EQ(ui::acceptable_input_run("abc"), size_t{3});
    CHECK_EQ(ui::acceptable_input_run("ab\ncd"), size_t{2});
    CHECK_EQ(ui::acceptable_input_run("\nabc"), size_t{0});
    CHECK_EQ(ui::acceptable_input_run(EMOJI), size_t{4});
    // A truncated sequence at the very end is not acceptable either.
    CHECK_EQ(ui::acceptable_input_run(std::string_view("a\xE2\x82", 3)), size_t{1});
}

// --- Ordinary-field specifics ----------------------------------------------

TEST(tim_selection_text_returns_the_selected_bytes)
{
    ui::TextInputModel m;
    m.insert("hello world");
    m.move_home(false);
    m.move_right(true, true);
    CHECK_EQ(m.selection_text(), std::string("hello"));
    m.select_all();
    CHECK_EQ(m.selection_text(), std::string("hello world"));
    m.clear();
    CHECK_EQ(m.selection_text(), std::string(""));
}

TEST(tim_str_and_view_expose_the_buffer_for_vault_calls)
{
    ui::TextInputModel m;
    m.set_text(HELLO);
    CHECK_EQ(m.str(), std::string(HELLO));
    CHECK(m.view() == HELLO);
}

TEST(tim_an_ordinary_field_is_not_secure)
{
    const ui::TextInputModel m;
    CHECK(!m.secure());
    CHECK_EQ(m.byte_cap(), ui::TEXT_INPUT_DEFAULT_CAP);
}
