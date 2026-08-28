#include "test_framework.h"

#include <cstring>
#include <string>
#include <string_view>

#include "ui/clipboard.h"
#include "ui/clipboard_gate.h"

namespace {

// Minimal backend recording the last set_text() so the gate's confirm path can
// be verified through the same seam the live SDL backend uses.
class RecordingClipboard final : public ui::ClipboardBackend {
public:
    RecordingClipboard() = default;

    ~RecordingClipboard() override
    {
        ui::set_clipboard_backend(nullptr);
    }

    RecordingClipboard(const RecordingClipboard&) = delete;
    RecordingClipboard& operator=(const RecordingClipboard&) = delete;
    RecordingClipboard(RecordingClipboard&&) = delete;
    RecordingClipboard& operator=(RecordingClipboard&&) = delete;

    char* get_text() override
    {
        return nullptr;
    }
    void release_text(char* p) override
    {
        std::free(p);
    }  // NOLINT
    bool set_text(std::string_view s) override
    {
        contents_ = std::string(s);
        set_calls_++;
        return true;
    }

    [[nodiscard]] const std::string& contents() const
    {
        return contents_;
    }
    [[nodiscard]] int set_calls() const
    {
        return set_calls_;
    }

private:
    std::string contents_;
    int set_calls_ = 0;
};

// RAII installer so a failed CHECK cannot leave the mock installed.
struct Installed {
    explicit Installed(RecordingClipboard& m)
    {
        ui::set_clipboard_backend(&m);
    }
    ~Installed()
    {
        ui::set_clipboard_backend(nullptr);
    }
    Installed(const Installed&) = delete;
    Installed& operator=(const Installed&) = delete;
    Installed(Installed&&) = delete;
    Installed& operator=(Installed&&) = delete;
};

}  // namespace

TEST(clipboard_gate_action_maps_the_three_modes)
{
    using enum ui::ClipboardGateAction;
    CHECK(ui::clipboard_gate_action(platform::ClipboardMode::Allow) == Copy);
    CHECK(ui::clipboard_gate_action(platform::ClipboardMode::Warn) == Confirm);
    CHECK(ui::clipboard_gate_action(platform::ClipboardMode::Disable) == Refuse);
}

TEST(clipboard_gate_mode_defaults_to_allow)
{
    ui::reset_clipboard_gate();
    CHECK(ui::clipboard_gate() == platform::ClipboardMode::Allow);
}

TEST(clipboard_gate_mode_set_and_get)
{
    ui::reset_clipboard_gate();
    ui::set_clipboard_gate(platform::ClipboardMode::Disable);
    CHECK(ui::clipboard_gate() == platform::ClipboardMode::Disable);
    ui::set_clipboard_gate(platform::ClipboardMode::Warn);
    CHECK(ui::clipboard_gate() == platform::ClipboardMode::Warn);
}

TEST(clipboard_gate_a_request_parks_a_pending_confirm)
{
    ui::reset_clipboard_gate();
    CHECK(ui::request_clipboard_confirm("vacation.jpg", /*sensitive=*/false));
    CHECK(ui::clipboard_confirm_pending());
    CHECK_EQ(std::string(ui::clipboard_confirm_text()), std::string("vacation.jpg"));
    CHECK_FALSE(ui::clipboard_confirm_sensitive());
}

TEST(clipboard_gate_a_sensitive_request_is_tagged)
{
    ui::reset_clipboard_gate();
    CHECK(ui::request_clipboard_confirm("s3cret-passphrase", /*sensitive=*/true));
    CHECK(ui::clipboard_confirm_sensitive());
}

TEST(clipboard_gate_an_empty_request_parks_nothing)
{
    ui::reset_clipboard_gate();
    CHECK(ui::request_clipboard_confirm("", /*sensitive=*/false));
    CHECK_FALSE(ui::clipboard_confirm_pending());
}

TEST(clipboard_gate_confirm_writes_the_clipboard_and_clears_pending)
{
    ui::reset_clipboard_gate();
    RecordingClipboard mock;
    const Installed guard(mock);

    CHECK(ui::request_clipboard_confirm("tag:artist", /*sensitive=*/false));
    CHECK(ui::confirm_clipboard_copy());
    CHECK_EQ(mock.set_calls(), 1);
    CHECK_EQ(mock.contents(), std::string("tag:artist"));
    CHECK_FALSE(ui::clipboard_confirm_pending());
    CHECK(ui::clipboard_confirm_text().empty());
}

TEST(clipboard_gate_confirm_retains_the_copy_for_auto_clear_arming)
{
    ui::reset_clipboard_gate();
    RecordingClipboard mock;
    const Installed guard(mock);

    CHECK(ui::request_clipboard_confirm("correct horse", /*sensitive=*/true));
    CHECK(ui::confirm_clipboard_copy());

    auto copy = ui::take_confirmed_copy();
    REQUIRE(copy.has_value());
    CHECK_EQ(copy->text, std::string("correct horse"));
    CHECK(copy->sensitive);

    // Consumed on read.
    CHECK_FALSE(ui::take_confirmed_copy().has_value());
}

TEST(clipboard_gate_confirm_requires_a_pending_request)
{
    ui::reset_clipboard_gate();
    RecordingClipboard mock;
    const Installed guard(mock);

    CHECK_FALSE(ui::confirm_clipboard_copy());
    CHECK_EQ(mock.set_calls(), 0);
}

TEST(clipboard_gate_cancel_drops_the_pending_without_writing)
{
    ui::reset_clipboard_gate();
    RecordingClipboard mock;
    const Installed guard(mock);

    CHECK(ui::request_clipboard_confirm("sensitive note", /*sensitive=*/true));
    ui::cancel_clipboard_copy();
    CHECK_FALSE(ui::clipboard_confirm_pending());
    CHECK_FALSE(ui::clipboard_confirm_sensitive());
    CHECK(ui::clipboard_confirm_text().empty());
    CHECK_EQ(mock.set_calls(), 0);
    CHECK_FALSE(ui::take_confirmed_copy().has_value());
}

TEST(clipboard_gate_a_new_request_replaces_an_unconfirmed_one)
{
    ui::reset_clipboard_gate();
    CHECK(ui::request_clipboard_confirm("first", /*sensitive=*/false));
    CHECK(ui::request_clipboard_confirm("second", /*sensitive=*/true));
    CHECK_EQ(std::string(ui::clipboard_confirm_text()), std::string("second"));
    CHECK(ui::clipboard_confirm_sensitive());
}

TEST(clipboard_gate_reset_clears_everything)
{
    ui::reset_clipboard_gate();
    RecordingClipboard mock;
    const Installed guard(mock);

    ui::set_clipboard_gate(platform::ClipboardMode::Disable);
    CHECK(ui::request_clipboard_confirm("stale", /*sensitive=*/true));
    ui::reset_clipboard_gate();
    CHECK(ui::clipboard_gate() == platform::ClipboardMode::Allow);
    CHECK_FALSE(ui::clipboard_confirm_pending());
    CHECK_FALSE(ui::take_confirmed_copy().has_value());
    (void)mock;
}