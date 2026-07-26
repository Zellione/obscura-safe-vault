#pragma once

// Storage-agnostic conformance suite for ui::ITextInput.
//
// TextInputModel and SecureTextInput share every caret/selection/UTF-8 rule via
// TextInputBase, so the rules are asserted ONCE here and run against both
// backends (tests/ui/test_text_input_model.cpp and test_secure_text_input.cpp).
// Duplicating the suite per backend is how the two would silently drift after
// someone adds a case to only one of them.
//
// `make` produces a fresh, empty field with the requested byte capacity, so a
// backend that allocates its storage up front (SecureTextInput) can be sized.

#include "test_framework.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "ui/text_input_model.h"

namespace tests {

using InputFactory = std::function<std::unique_ptr<ui::ITextInput>(size_t cap)>;

inline std::string text_of(const ui::ITextInput& m)
{
    const auto b = m.bytes();
    return {reinterpret_cast<const char*>(b.data()), b.size()};
}

// "héllo" — 'é' (U+00E9) is two bytes: 6 bytes, 5 characters.
inline constexpr std::string_view CONF_HELLO = "h\xC3\xA9llo";
inline constexpr std::string_view CONF_EURO  = "\xE2\x82\xAC";        // 3 bytes
inline constexpr std::string_view CONF_EMOJI = "\xF0\x9F\x98\x80";    // 4 bytes

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
inline void check_text_input_conformance(const InputFactory& make, int& _failures)
{
    // --- Backspace / delete across multi-byte characters --------------------
    {
        auto m = make(4096);
        m->insert(CONF_HELLO);
        CHECK_EQ(m->size(), size_t{6});
        m->move_left(false, false);
        m->move_left(false, false);
        m->move_left(false, false);   // caret at offset 3, right after 'é'
        CHECK_EQ(m->caret(), size_t{3});
        m->backspace();
        // Both bytes of 'é' go, not just one: the buffer stays valid UTF-8.
        CHECK_EQ(text_of(*m), std::string("hllo"));
        CHECK_EQ(m->caret(), size_t{1});
    }
    {
        auto m = make(4096);
        m->insert(CONF_HELLO);
        m->move_home(false);
        m->move_right(false, false);  // caret at offset 1, just before 'é'
        m->del();
        CHECK_EQ(text_of(*m), std::string("hllo"));
        CHECK_EQ(m->caret(), size_t{1});
    }
    {
        auto m = make(4096);
        m->insert(CONF_EURO);
        m->insert(CONF_EMOJI);
        CHECK_EQ(m->size(), size_t{7});
        m->backspace();
        CHECK_EQ(text_of(*m), std::string(CONF_EURO));
        m->backspace();
        CHECK(m->empty());
        m->backspace();               // no-op on an empty buffer
        CHECK_EQ(m->caret(), size_t{0});
    }
    {   // Backspace at the start and Delete at the end are no-ops.
        auto m = make(4096);
        m->insert("ab");
        m->move_home(false);
        m->backspace();
        CHECK_EQ(text_of(*m), std::string("ab"));
        m->move_end(false);
        m->del();
        CHECK_EQ(text_of(*m), std::string("ab"));
    }

    // --- Caret movement ------------------------------------------------------
    {
        auto m = make(4096);
        m->insert("abcdef");
        CHECK_EQ(m->caret(), size_t{6});
        m->move_home(false);
        CHECK_EQ(m->caret(), size_t{0});
        m->move_end(false);
        CHECK_EQ(m->caret(), size_t{6});
    }
    {   // Word jumps land on word boundaries, in both directions.
        auto m = make(4096);
        m->insert("alpha beta  gamma");
        m->move_home(false);
        m->move_right(true, false);
        CHECK_EQ(m->caret(), size_t{5});    // end of "alpha"
        m->move_right(true, false);
        CHECK_EQ(m->caret(), size_t{10});   // end of "beta"
        m->move_right(true, false);
        CHECK_EQ(m->caret(), size_t{17});   // end of "gamma"
        m->move_right(true, false);
        CHECK_EQ(m->caret(), size_t{17});   // clamps at the end
        m->move_left(true, false);
        CHECK_EQ(m->caret(), size_t{12});   // start of "gamma"
        m->move_left(true, false);
        CHECK_EQ(m->caret(), size_t{6});    // start of "beta"
        m->move_left(true, false);
        CHECK_EQ(m->caret(), size_t{0});
    }
    {   // From mid-word: out to the current word's edges.
        auto m = make(4096);
        m->insert("alpha beta");
        m->move_home(false);
        m->move_right(false, false);
        m->move_right(false, false);
        m->move_right(true, false);
        CHECK_EQ(m->caret(), size_t{5});
        m->move_left(true, false);
        CHECK_EQ(m->caret(), size_t{0});
    }
    {   // Punctuation is its own class, so a word jump stops at it.
        auto m = make(4096);
        m->insert("a.b");
        m->move_home(false);
        m->move_right(true, false);
        CHECK_EQ(m->caret(), size_t{1});
        m->move_right(true, false);
        CHECK_EQ(m->caret(), size_t{2});
        m->move_right(true, false);
        CHECK_EQ(m->caret(), size_t{3});
    }

    // --- Selection -----------------------------------------------------------
    {
        auto m = make(4096);
        m->insert("abcdef");
        m->move_left(false, true);
        m->move_left(false, true);
        CHECK(m->has_selection());
        CHECK_EQ(m->sel_begin(), size_t{4});
        CHECK_EQ(m->sel_end(), size_t{6});
    }
    {   // An unextended move collapses the selection to its near edge without stepping.
        auto m = make(4096);
        m->insert("abcdef");
        m->move_left(false, true);
        m->move_left(false, true);
        m->move_left(false, false);
        CHECK(!m->has_selection());
        CHECK_EQ(m->caret(), size_t{4});

        m->move_end(false);
        m->move_left(false, true);
        m->move_left(false, true);
        m->move_right(false, false);
        CHECK(!m->has_selection());
        CHECK_EQ(m->caret(), size_t{6});
    }
    {
        auto m = make(4096);
        m->insert(CONF_HELLO);
        m->select_all();
        CHECK_EQ(m->sel_begin(), size_t{0});
        CHECK_EQ(m->sel_end(), size_t{6});
        m->delete_selection();
        CHECK(m->empty());
        CHECK_EQ(m->caret(), size_t{0});
        CHECK(!m->has_selection());
    }
    {   // Insert replaces the selection and leaves the caret after the new text.
        auto m = make(4096);
        m->insert("hello world");
        m->move_home(false);
        m->move_right(true, true);
        m->insert("bye");
        CHECK_EQ(text_of(*m), std::string("bye world"));
        CHECK_EQ(m->caret(), size_t{3});
        CHECK(!m->has_selection());
    }
    {   // Backspace and Delete with a selection remove exactly the selection.
        auto m = make(4096);
        m->insert("abcdef");
        m->move_home(false);
        m->move_right(false, true);
        m->move_right(false, true);
        m->backspace();
        CHECK_EQ(text_of(*m), std::string("cdef"));
        CHECK_EQ(m->caret(), size_t{0});
        m->select_all();
        m->del();
        CHECK(m->empty());
    }
    {
        auto m = make(4096);
        m->select_all();              // nothing to select
        CHECK(!m->has_selection());
    }

    // --- Capacity ------------------------------------------------------------
    {
        auto m = make(4);
        m->insert("abcdef");
        CHECK_EQ(text_of(*m), std::string("abcd"));
        CHECK_EQ(m->caret(), size_t{4});
        m->insert("z");               // full: nothing more fits
        CHECK_EQ(text_of(*m), std::string("abcd"));
    }
    {   // The cap never chops a character in half.
        auto m = make(4);
        m->insert("ab");
        m->insert(CONF_EURO);         // 3 bytes, only 2 free
        CHECK_EQ(text_of(*m), std::string("ab"));
        CHECK_EQ(m->size(), size_t{2});
    }
    {   // A replaced selection frees room the same insert can then use.
        auto m = make(4);
        m->insert("abcd");
        m->select_all();
        m->insert(CONF_EURO);
        CHECK_EQ(text_of(*m), std::string(CONF_EURO));
    }

    // --- Input sanitising ----------------------------------------------------
    {   // Fields are single-line: a pasted newline or tab must not enter.
        auto m = make(4096);
        m->insert("one\ntwo\tthree");
        CHECK_EQ(text_of(*m), std::string("onetwothree"));
    }
    {
        auto m = make(4096);
        m->insert(std::string_view("a\xC3\x28" "b", 4));    // 0xC3 0x28 is malformed
        CHECK_EQ(text_of(*m), std::string("a(b"));
    }
    {
        auto m = make(4096);
        m->insert(std::string_view("\x80\x80" "ok", 4));    // stray continuation bytes
        CHECK_EQ(text_of(*m), std::string("ok"));
    }
    {
        auto m = make(4096);
        m->insert(CONF_EMOJI);
        m->insert(CONF_EURO);
        CHECK_EQ(text_of(*m), std::string(CONF_EMOJI) + std::string(CONF_EURO));
    }

    // --- set_text / clear ----------------------------------------------------
    {
        auto m = make(4096);
        m->insert("abcdef");
        m->select_all();
        m->set_text("xy");
        CHECK_EQ(text_of(*m), std::string("xy"));
        CHECK_EQ(m->caret(), size_t{2});
        CHECK(!m->has_selection());
    }
    {
        auto m = make(4096);
        m->insert("abcdef");
        m->select_all();
        m->clear();
        CHECK(m->empty());
        CHECK_EQ(m->caret(), size_t{0});
        CHECK(!m->has_selection());
        m->insert("x");               // usable after clear
        CHECK_EQ(text_of(*m), std::string("x"));
    }
    {
        auto m = make(4);
        m->set_text("ab\ncdef");
        CHECK_EQ(text_of(*m), std::string("abcd"));
        CHECK_EQ(m->caret(), size_t{4});
    }
}
// NOLINTEND(bugprone-easily-swappable-parameters)

} // namespace tests
