#include "test_framework.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ui/clipboard.h"
#include "ui/clipboard_gate.h"
#include "ui/secure_text_input.h"
#include "ui/text_input_model.h"

namespace {

// Mock backend: hands out a heap copy of `contents` (mirroring SDL, which also
// returns a buffer the caller must release) and keeps the released pointer so a
// test can assert the shim wiped it before letting go.
class MockClipboard final : public ui::ClipboardBackend {
public:
    explicit MockClipboard(std::string contents = {}) : contents_(std::move(contents)) {}

    ~MockClipboard() override
    {
        ui::set_clipboard_backend(nullptr);
        for (char* p : owned_) std::free(p);   // NOLINT(cppcoreguidelines-no-malloc)
    }

    MockClipboard(const MockClipboard&)            = delete;
    MockClipboard& operator=(const MockClipboard&) = delete;
    MockClipboard(MockClipboard&&)                 = delete;
    MockClipboard& operator=(MockClipboard&&)      = delete;

    char* get_text() override
    {
        if (!has_text_) return nullptr;
        auto* p = static_cast<char*>(std::malloc(contents_.size() + 1));  // NOLINT(cppcoreguidelines-no-malloc)
        if (p == nullptr) return nullptr;
        std::memcpy(p, contents_.c_str(), contents_.size() + 1);
        owned_.push_back(p);
        handed_out_ = p;
        handed_len_ = contents_.size();
        return p;
    }

    void release_text(char* p) override { released_ = p; }

    bool set_text(std::string_view s) override
    {
        contents_ = std::string(s);
        set_calls_++;
        return true;
    }

    void set_empty() { has_text_ = false; }

    [[nodiscard]] const std::string& contents() const { return contents_; }
    [[nodiscard]] int set_calls() const { return set_calls_; }
    [[nodiscard]] bool released_the_buffer() const { return released_ == handed_out_ && released_ != nullptr; }

    // True if every byte of the buffer we handed out reads back as zero.
    [[nodiscard]] bool handed_buffer_is_wiped() const
    {
        if (handed_out_ == nullptr) return false;
        for (size_t i = 0; i < handed_len_; ++i)
            if (handed_out_[i] != 0) return false;
        return true;
    }

private:
    std::string        contents_;
    bool               has_text_ = true;
    std::vector<char*> owned_;
    char*              handed_out_ = nullptr;
    size_t             handed_len_ = 0;
    char*              released_   = nullptr;
    int                set_calls_  = 0;
};

// RAII installer so a failed CHECK cannot leave the mock installed.
struct InstalledMock {
    explicit InstalledMock(MockClipboard& m) { ui::set_clipboard_backend(&m); }
    ~InstalledMock() { ui::set_clipboard_backend(nullptr); }
    InstalledMock(const InstalledMock&)            = delete;
    InstalledMock& operator=(const InstalledMock&) = delete;
    InstalledMock(InstalledMock&&)                 = delete;
    InstalledMock& operator=(InstalledMock&&)      = delete;
};

} // namespace

TEST(clip_paste_inserts_at_the_caret_of_an_ordinary_field)
{
    MockClipboard mock("world");
    const InstalledMock guard(mock);

    ui::TextInputModel f;
    f.set_text("hello ");
    CHECK(ui::paste_from_clipboard(f));
    CHECK_EQ(f.str(), std::string("hello world"));
    CHECK_EQ(f.caret(), size_t{11});
}

TEST(clip_paste_replaces_the_selection)
{
    MockClipboard mock("bye");
    const InstalledMock guard(mock);

    ui::TextInputModel f;
    f.set_text("hello world");
    f.move_home(false);
    f.move_right(true, true);           // select "hello"
    CHECK(ui::paste_from_clipboard(f));
    CHECK_EQ(f.str(), std::string("bye world"));
}

TEST(clip_paste_of_a_multiline_block_strips_the_newlines)
{
    MockClipboard mock("one\ntwo\n");
    const InstalledMock guard(mock);

    ui::TextInputModel f;
    CHECK(ui::paste_from_clipboard(f));
    CHECK_EQ(f.str(), std::string("onetwo"));
}

TEST(clip_paste_from_an_empty_clipboard_changes_nothing)
{
    MockClipboard mock("");
    const InstalledMock guard(mock);

    ui::TextInputModel f;
    f.set_text("keep");
    CHECK(!ui::paste_from_clipboard(f));
    CHECK_EQ(f.str(), std::string("keep"));

    mock.set_empty();                   // backend returns nullptr entirely
    CHECK(!ui::paste_from_clipboard(f));
    CHECK_EQ(f.str(), std::string("keep"));
}

TEST(clip_paste_always_releases_the_backend_buffer)
{
    MockClipboard mock("abc");
    const InstalledMock guard(mock);

    ui::TextInputModel f;
    CHECK(ui::paste_from_clipboard(f));
    CHECK(mock.released_the_buffer());
}

TEST(clip_paste_into_a_secure_field_is_allowed)
{
    MockClipboard mock("correct horse");
    const InstalledMock guard(mock);

    ui::SecureTextInput pw;
    CHECK(ui::paste_from_clipboard(pw));
    CHECK(pw.text_view() == std::string_view("correct horse"));
}

TEST(clip_copy_puts_the_selection_on_the_clipboard)
{
    MockClipboard mock;
    const InstalledMock guard(mock);

    ui::TextInputModel f;
    f.set_text("hello world");
    f.move_home(false);
    f.move_right(true, true);
    CHECK(ui::copy_selection_to_clipboard(f));
    CHECK_EQ(mock.contents(), std::string("hello"));
}

TEST(clip_copy_with_no_selection_does_nothing)
{
    MockClipboard mock("untouched");
    const InstalledMock guard(mock);

    ui::TextInputModel f;
    f.set_text("hello");
    CHECK(!ui::copy_selection_to_clipboard(f));
    CHECK_EQ(mock.set_calls(), 0);
    CHECK_EQ(mock.contents(), std::string("untouched"));
}

TEST(clip_cut_copies_then_removes_the_selection)
{
    MockClipboard mock;
    const InstalledMock guard(mock);

    ui::TextInputModel f;
    f.set_text("hello world");
    f.move_home(false);
    f.move_right(true, true);
    CHECK(ui::cut_selection_to_clipboard(f));
    CHECK_EQ(mock.contents(), std::string("hello"));
    CHECK_EQ(f.str(), std::string(" world"));
}

// --- Secure fields never surrender plaintext -------------------------------

TEST(clip_copy_from_a_secure_field_is_refused)
{
    MockClipboard mock("untouched");
    const InstalledMock guard(mock);

    ui::SecureTextInput pw;
    pw.insert("hunter2");
    pw.select_all();
    CHECK(!ui::copy_selection_to_clipboard(pw));
    CHECK_EQ(mock.set_calls(), 0);
    CHECK_EQ(mock.contents(), std::string("untouched"));
}

TEST(clip_cut_from_a_secure_field_neither_copies_nor_deletes)
{
    MockClipboard mock("untouched");
    const InstalledMock guard(mock);

    ui::SecureTextInput pw;
    pw.insert("hunter2");
    pw.select_all();
    CHECK(!ui::cut_selection_to_clipboard(pw));
    CHECK_EQ(mock.set_calls(), 0);
    // Cut must not destroy what it was not allowed to copy.
    CHECK_EQ(pw.size(), size_t{7});
}

// The shim hands the backend's buffer back through release_text(); the SDL
// backend wipes it there. Verify the shim does not keep a second live copy and
// that a mock which wipes on release sees a buffer it can wipe.
TEST(clip_the_backend_buffer_is_handed_back_for_wiping_after_a_secure_paste)
{
    MockClipboard mock("s3cret-passphrase");
    const InstalledMock guard(mock);

    ui::SecureTextInput pw;
    CHECK(ui::paste_from_clipboard(pw));
    CHECK(mock.released_the_buffer());
    CHECK(pw.text_view() == std::string_view("s3cret-passphrase"));
}

// --- The clipboard gate (Phase 92) ------------------------------------------
// copy/cut consult the process-global gate. These tests reset it first so a
// parked pending request from an earlier test cannot leak across test order.

TEST(clip_copy_is_refused_by_a_disable_gate)
{
    ui::reset_clipboard_gate();
    ui::set_clipboard_gate(platform::ClipboardMode::Disable);
    MockClipboard mock("untouched");
    const InstalledMock guard(mock);

    ui::TextInputModel f;
    f.set_text("hello world");
    f.move_home(false);
    f.move_right(true, true);               // select "hello"
    CHECK(!ui::copy_selection_to_clipboard(f));
    CHECK_EQ(mock.set_calls(), 0);
    CHECK_EQ(mock.contents(), std::string("untouched"));
    CHECK_FALSE(ui::clipboard_confirm_pending());
}

TEST(clip_cut_is_refused_by_a_disable_gate_and_keeps_the_text)
{
    ui::reset_clipboard_gate();
    ui::set_clipboard_gate(platform::ClipboardMode::Disable);
    MockClipboard mock("untouched");
    const InstalledMock guard(mock);

    ui::TextInputModel f;
    f.set_text("hello world");
    f.move_home(false);
    f.move_right(true, true);
    CHECK(!ui::cut_selection_to_clipboard(f));
    CHECK_EQ(mock.set_calls(), 0);
    // Cut must not destroy what the gate refused to copy.
    CHECK_EQ(f.str(), std::string("hello world"));
}

TEST(clip_copy_under_a_warn_gate_parks_a_confirm_and_writes_nothing)
{
    ui::reset_clipboard_gate();
    ui::set_clipboard_gate(platform::ClipboardMode::Warn);
    MockClipboard mock("untouched");
    const InstalledMock guard(mock);

    ui::TextInputModel f;
    f.set_text("hello world");
    f.move_home(false);
    f.move_right(true, true);
    // Under Warn the copy is deferred, so nothing was written yet — the
    // function reports false, and the pending request holds the payload.
    CHECK(!ui::copy_selection_to_clipboard(f));
    CHECK(ui::clipboard_confirm_pending());
    CHECK_EQ(std::string(ui::clipboard_confirm_text()), std::string("hello"));
    CHECK_FALSE(ui::clipboard_confirm_sensitive());
    CHECK_EQ(mock.set_calls(), 0);
    CHECK_EQ(mock.contents(), std::string("untouched"));
    ui::cancel_clipboard_copy();
}

TEST(clip_copy_under_a_warn_gate_writes_on_confirm)
{
    ui::reset_clipboard_gate();
    ui::set_clipboard_gate(platform::ClipboardMode::Warn);
    MockClipboard mock;
    const InstalledMock guard(mock);

    ui::TextInputModel f;
    f.set_text("hello world");
    f.move_home(false);
    f.move_right(true, true);
    CHECK(!ui::copy_selection_to_clipboard(f));
    CHECK(ui::clipboard_confirm_pending());
    CHECK(ui::confirm_clipboard_copy());
    CHECK_EQ(mock.set_calls(), 1);
    CHECK_EQ(mock.contents(), std::string("hello"));
    CHECK_FALSE(ui::clipboard_confirm_pending());
}

TEST(clip_cut_under_a_warn_gate_parks_without_deleting_the_selection)
{
    ui::reset_clipboard_gate();
    ui::set_clipboard_gate(platform::ClipboardMode::Warn);
    MockClipboard mock("untouched");
    const InstalledMock guard(mock);

    ui::TextInputModel f;
    f.set_text("hello world");
    f.move_home(false);
    f.move_right(true, true);
    CHECK(!ui::cut_selection_to_clipboard(f));
    CHECK(ui::clipboard_confirm_pending());
    CHECK_EQ(mock.set_calls(), 0);
    // The delete is deferred until the copy is actually confirmed.
    CHECK_EQ(f.str(), std::string("hello world"));
    ui::cancel_clipboard_copy();
}

TEST(clip_copy_under_an_allow_gate_still_writes_immediately)
{
    ui::reset_clipboard_gate();
    ui::set_clipboard_gate(platform::ClipboardMode::Allow);
    MockClipboard mock;
    const InstalledMock guard(mock);

    ui::TextInputModel f;
    f.set_text("hello world");
    f.move_home(false);
    f.move_right(true, true);
    CHECK(ui::copy_selection_to_clipboard(f));
    CHECK_EQ(mock.set_calls(), 1);
    CHECK_EQ(mock.contents(), std::string("hello"));
    CHECK_FALSE(ui::clipboard_confirm_pending());
}
