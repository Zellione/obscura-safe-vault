#include "ui/text_input_model.h"

#include <algorithm>
#include <cstddef>

namespace ui {

namespace {

// A UTF-8 continuation byte (10xxxxxx) never starts a character. The mask runs
// in the std::byte domain: masking the underlying integer trips S6022
// (byte-oriented data wants std::byte) and casting back for it trips S7035.
bool is_continuation(uint8_t b) noexcept
{
    return (std::byte{b} & std::byte{0xC0}) == std::byte{0x80};
}

// Expected total length of the sequence a lead byte opens, or 0 if `b` cannot
// lead one (a continuation byte, or one of the never-valid 0xC0/0xC1/0xF5..0xFF).
size_t sequence_length(uint8_t b) noexcept
{
    if (b < 0x80)                return 1;
    if (b >= 0xC2 && b <= 0xDF)  return 2;
    if (b >= 0xE0 && b <= 0xEF)  return 3;
    if (b >= 0xF0 && b <= 0xF4)  return 4;
    return 0;
}

enum class CharClass : uint8_t { Space, Word, Punct };

// Classify by the character's LEAD byte. Anything non-ASCII counts as a word
// character: the app's fields hold names, tags and queries, where a word jump
// should run through accented letters rather than stop at each one.
CharClass classify(uint8_t lead) noexcept
{
    using enum CharClass;
    if (lead >= 0x80) return Word;
    if (lead == ' ' || lead == '\t' || lead == '\n' || lead == '\r' || lead == '\v' || lead == '\f')
        return Space;
    const bool alnum = (lead >= '0' && lead <= '9') || (lead >= 'A' && lead <= 'Z') ||
                       (lead >= 'a' && lead <= 'z') || lead == '_';
    return alnum ? Word : Punct;
}

// Class of the character starting at `pos` / ending at `pos`.
CharClass class_at(std::span<const uint8_t> buf, size_t pos) noexcept
{
    return classify(buf[pos]);
}

CharClass class_before(std::span<const uint8_t> buf, size_t pos) noexcept
{
    return classify(buf[utf8_prev_boundary(buf, pos)]);
}

// Is the sequence of `len` bytes starting at `p` well-formed UTF-8?
// Rejects overlong forms and surrogates via the standard second-byte ranges.
bool sequence_is_valid(const unsigned char* p, size_t len, size_t avail) noexcept
{
    if (len == 0 || len > avail) return false;
    if (len == 1) return true;
    for (size_t i = 1; i < len; ++i)
        if (!is_continuation(p[i])) return false;

    const unsigned char b0 = p[0];
    const unsigned char b1 = p[1];
    if (b0 == 0xE0 && b1 < 0xA0) return false;   // overlong 3-byte
    if (b0 == 0xED && b1 >= 0xA0) return false;  // UTF-16 surrogate half
    if (b0 == 0xF0 && b1 < 0x90) return false;   // overlong 4-byte
    if (b0 == 0xF4 && b1 >= 0x90) return false;  // beyond U+10FFFF
    return true;
}

// Acceptable field input: a well-formed sequence that is not a C0 control or DEL.
size_t acceptable_at(std::string_view text, size_t pos) noexcept
{
    const auto* p   = reinterpret_cast<const unsigned char*>(text.data()) + pos;
    const size_t len = sequence_length(*p);
    if (!sequence_is_valid(p, len, text.size() - pos)) return 0;
    if (len == 1 && (*p < 0x20 || *p == 0x7F)) return 0;   // control / DEL
    return len;
}

} // namespace

// --- helpers ---------------------------------------------------------------

size_t utf8_prev_boundary(std::span<const uint8_t> buf, size_t pos) noexcept
{
    if (pos == 0) return 0;
    size_t i = std::min(pos, buf.size());
    do { --i; } while (i > 0 && is_continuation(buf[i]));
    return i;
}

size_t utf8_next_boundary(std::span<const uint8_t> buf, size_t pos) noexcept
{
    if (pos >= buf.size()) return buf.size();
    size_t i = pos + 1;
    while (i < buf.size() && is_continuation(buf[i])) ++i;
    return i;
}

size_t utf8_char_count(std::span<const uint8_t> buf) noexcept
{
    size_t n = 0;
    for (uint8_t b : buf)
        if (!is_continuation(b)) ++n;
    return n;
}

size_t word_boundary_left(std::span<const uint8_t> buf, size_t pos) noexcept
{
    size_t i = std::min(pos, buf.size());
    while (i > 0 && class_before(buf, i) == CharClass::Space) i = utf8_prev_boundary(buf, i);
    if (i == 0) return 0;
    const CharClass cls = class_before(buf, i);
    while (i > 0 && class_before(buf, i) == cls) i = utf8_prev_boundary(buf, i);
    return i;
}

size_t word_boundary_right(std::span<const uint8_t> buf, size_t pos) noexcept
{
    size_t i = std::min(pos, buf.size());
    while (i < buf.size() && class_at(buf, i) == CharClass::Space) i = utf8_next_boundary(buf, i);
    if (i >= buf.size()) return buf.size();
    const CharClass cls = class_at(buf, i);
    while (i < buf.size() && class_at(buf, i) == cls) i = utf8_next_boundary(buf, i);
    return i;
}

size_t acceptable_input_run(std::string_view text) noexcept
{
    size_t i = 0;
    while (i < text.size()) {
        const size_t len = acceptable_at(text, i);
        if (len == 0) break;
        i += len;
    }
    return i;
}

size_t skip_unacceptable(std::string_view text, size_t from) noexcept
{
    size_t i = from;
    while (i < text.size() && acceptable_at(text, i) == 0) ++i;
    return i;
}

// --- TextInputBase ---------------------------------------------------------

size_t TextInputBase::sel_begin() const noexcept { return std::min(caret_, anchor_); }
size_t TextInputBase::sel_end()   const noexcept { return std::max(caret_, anchor_); }

void TextInputBase::apply_splice(size_t start, size_t end, std::string_view ins)
{
    splice(start, end, ins);
    ++revision_;
}

void TextInputBase::place_caret(size_t pos, bool extend) noexcept
{
    caret_ = std::min(pos, size());
    if (!extend) anchor_ = caret_;
}

void TextInputBase::insert(std::string_view text)
{
    // Walk the input as alternating acceptable / rejected runs so a paste with
    // embedded newlines or invalid bytes keeps everything around them. Each run
    // is a view into the caller's buffer, so a secure field never materialises
    // an intermediate std::string (AGENTS.md invariant #1/#2).
    size_t i = skip_unacceptable(text, 0);
    while (i < text.size()) {
        const size_t run = acceptable_input_run(text.substr(i));
        if (run == 0) break;                        // defensive; skip_unacceptable guarantees > 0
        insert_run(text.substr(i, run));
        i = skip_unacceptable(text, i + run);
    }
}

void TextInputBase::insert_run(std::string_view run)
{
    const size_t start = sel_begin();
    const size_t end   = sel_end();
    // Room left once the selection (which the insert replaces) is freed.
    const size_t room = byte_cap() - std::min(byte_cap(), size() - (end - start));

    size_t take = std::min(run.size(), room);
    // Never let the cap chop a character in half.
    if (take < run.size()) {
        while (take > 0 && is_continuation(static_cast<uint8_t>(run[take]))) --take;
    }
    if (take == 0 && start == end) return;

    apply_splice(start, end, run.substr(0, take));
    caret_  = start + take;
    anchor_ = caret_;
}

void TextInputBase::backspace()
{
    if (has_selection()) { delete_selection(); return; }
    if (caret_ == 0) return;
    const size_t from = utf8_prev_boundary(bytes(), caret_);
    apply_splice(from, caret_, {});
    caret_  = from;
    anchor_ = from;
}

void TextInputBase::del()
{
    if (has_selection()) { delete_selection(); return; }
    if (caret_ >= size()) return;
    apply_splice(caret_, utf8_next_boundary(bytes(), caret_), {});
    anchor_ = caret_;
}

void TextInputBase::delete_selection()
{
    if (!has_selection()) return;
    const size_t start = sel_begin();
    apply_splice(start, sel_end(), {});
    caret_  = start;
    anchor_ = start;
}

void TextInputBase::move_left(bool by_word, bool extend)
{
    // An unextended move over a selection collapses to its near edge without
    // also stepping a character — the behaviour every text field has.
    if (!extend && has_selection() && !by_word) { place_caret(sel_begin(), false); return; }
    place_caret(by_word ? word_boundary_left(bytes(), caret_)
                        : utf8_prev_boundary(bytes(), caret_), extend);
}

void TextInputBase::move_right(bool by_word, bool extend)
{
    if (!extend && has_selection() && !by_word) { place_caret(sel_end(), false); return; }
    place_caret(by_word ? word_boundary_right(bytes(), caret_)
                        : utf8_next_boundary(bytes(), caret_), extend);
}

void TextInputBase::move_home(bool extend) { place_caret(0, extend); }
void TextInputBase::move_end(bool extend)  { place_caret(size(), extend); }

void TextInputBase::select_all()
{
    anchor_ = 0;
    caret_  = size();
}

void TextInputBase::set_text(std::string_view text)
{
    clear();
    insert(text);
}

// --- TextInputModel --------------------------------------------------------

std::span<const uint8_t> TextInputModel::bytes() const noexcept
{
    return {reinterpret_cast<const uint8_t*>(buf_.data()), buf_.size()};
}

std::string TextInputModel::selection_text() const
{
    return buf_.substr(sel_begin(), sel_end() - sel_begin());
}

void TextInputModel::clear()
{
    buf_.clear();
    reset_caret();
}

void TextInputModel::splice(size_t start, size_t end, std::string_view ins)
{
    buf_.replace(start, end - start, ins);
}

} // namespace ui
