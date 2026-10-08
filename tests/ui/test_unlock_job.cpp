#include "test_framework.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <thread>

#include "gfx/text.h"
#include "gfx/window.h"
#include "platform/file_dialog.h"
#include "ui/unlock_screen.h"

#include "crypto/secure_mem.h"
#include "ui/unlock_job.h"
#include "vault/vault.h"

namespace fs = std::filesystem;

static const crypto::KdfParams kKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

// Internal linkage: several test files each define their own `TempVault` with
// a DIFFERENT layout; namespace scope would be an ODR violation (see
// tests/vault/test_combine.cpp).
namespace {

struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_unlockjob_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec; fs::remove(path, ec);
    }
    ~TempVault() { std::error_code ec; fs::remove_all(path, ec); }
    std::string str() const { return path.string(); }
};

}  // namespace

// Poll until the worker hands back its outcome. The KDF with the tiny test
// params takes milliseconds; the loop bound guards a hung worker.
static std::optional<vault::VaultResult> wait_outcome(ui::UnlockJob& job)
{
    using namespace std::chrono_literals;
    for (int i = 0; i < 10000; ++i) {
        if (auto oc = job.take_outcome()) return oc;
        std::this_thread::sleep_for(1ms);
    }
    return std::nullopt;
}

TEST(unlock_job_opens_and_unlocks_existing_vault)
{
    using enum vault::VaultResult;
    TempVault tv("ok");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == Ok);
    }  // close again — the job reopens the file itself

    vault::Vault target;
    ui::UnlockJob job;
    REQUIRE(job.start_unlock(target, tv.str(), bytes("pw"), {}));
    CHECK_TRUE(job.active());

    auto oc = wait_outcome(job);
    REQUIRE(oc.has_value());
    CHECK_TRUE(*oc == Ok);
    CHECK_TRUE(target.is_unlocked());
    CHECK_FALSE(job.active());
}

TEST(unlock_job_wrong_password_reports_auth_failed)
{
    using enum vault::VaultResult;
    TempVault tv("wrongpw");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == Ok);
    }

    vault::Vault target;
    ui::UnlockJob job;
    REQUIRE(job.start_unlock(target, tv.str(), bytes("nope"), {}));

    auto oc = wait_outcome(job);
    REQUIRE(oc.has_value());
    CHECK_TRUE(*oc == AuthFailed);
    CHECK_FALSE(target.is_unlocked());
}

TEST(unlock_job_create_mode_creates_and_unlocks)
{
    using enum vault::VaultResult;
    TempVault tv("create");

    vault::Vault target;
    ui::UnlockJob job;
    REQUIRE(job.start_create(target, tv.str(), bytes("pw"), {}, kKdf));

    auto oc = wait_outcome(job);
    REQUIRE(oc.has_value());
    CHECK_TRUE(*oc == Ok);
    CHECK_TRUE(target.is_unlocked());
    CHECK_TRUE(fs::is_directory(tv.path));
    CHECK_TRUE(vault::vault_uses_directory_storage(target));
}

TEST(unlock_job_refuses_overlapping_start)
{
    using enum vault::VaultResult;
    TempVault tv("overlap");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == Ok);
    }

    vault::Vault a;
    vault::Vault b;
    ui::UnlockJob job;
    REQUIRE(job.start_unlock(a, tv.str(), bytes("pw"), {}));
    // active() stays true until take_outcome() collects, even if the worker
    // already finished — so a second start must always be refused here.
    CHECK_FALSE(job.start_unlock(b, tv.str(), bytes("pw"), {}));

    auto oc = wait_outcome(job);
    REQUIRE(oc.has_value());
    CHECK_TRUE(*oc == Ok);
}

// --- OSV-AUD-006: failed launches must not leave secrets resident ----------

TEST(unlock_job_keyfile_copy_failure_wipes_password)
{
    using enum vault::VaultResult;
    TempVault tv("keyfail");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == Ok);
    }

    vault::Vault target;
    ui::UnlockJob job;
    crypto::detail::reset_wipe_observations_for_tests();
    const uint64_t before = crypto::detail::wiping_deallocation_count();

    // copy_secret(pw_) succeeds first; the keyfile copy then fails (value=1
    // leaves the 2nd SecureBytes allocation as the failure). A partially copied
    // secret must not survive the failed launch.
    crypto::detail::inject_secure_allocation_failure(1);
    CHECK_FALSE(job.start_unlock(target, tv.str(), bytes("pw"), bytes("kf")));
    crypto::detail::clear_secure_allocation_failure();

    CHECK_FALSE(job.active());
    CHECK(crypto::detail::wiping_deallocation_count() > before);
    CHECK_TRUE(crypto::detail::all_wipe_observations_zero_for_tests());
}

TEST(unlock_job_thread_launch_failure_wipes_secrets)
{
    using enum vault::VaultResult;
    TempVault tv("thrdfail");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == Ok);
    }

    vault::Vault target;
    ui::UnlockJob job;
    crypto::detail::reset_wipe_observations_for_tests();
    const uint64_t before = crypto::detail::wiping_deallocation_count();

    ui::test_only_force_unlock_thread_failure(true);
    CHECK_FALSE(job.start_unlock(target, tv.str(), bytes("pw"), bytes("kf")));
    ui::test_only_force_unlock_thread_failure(false);

    CHECK_FALSE(job.active());
    // Both copies made it into mlock'd storage; the failed launch must release
    // and wipe them rather than leave them resident for a worker that never ran.
    CHECK_FALSE(job.take_outcome().has_value());
    CHECK(crypto::detail::wiping_deallocation_count() > before);
    CHECK_TRUE(crypto::detail::all_wipe_observations_zero_for_tests());
}

// Exercise the actual screen without creating a display or renderer.

TEST(unlock_screen_legacy_browse_does_not_require_conversion)
{
    TempVault tv("readonly_screen");
    {
        vault::Vault source;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kKdf, source) ==
                vault::VaultResult::Ok);
    }
    const auto size_before = fs::file_size(tv.path);
    const auto modified_before = fs::last_write_time(tv.path);
    gfx::Window window;
    gfx::FontAtlas font;
    platform::FileDialog dialog;
    vault::Vault target;
    ui::UnlockScreen screen(window, font, target, dialog, tv.path);
    SDL_Event event{};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.text = "pw";
    screen.handle_event(event);
    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_RETURN;
    screen.handle_event(event);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (screen.animating() && std::chrono::steady_clock::now() < deadline) {
        screen.update(0.01);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    screen.update(0.01);
    REQUIRE(target.is_unlocked());
    REQUIRE(vault::vault_is_read_only(target));
    CHECK(screen.take_nav().kind == ui::NavKind::None);
    // Conversion is explicit, and Escape returns to the browsing choice.
    event.key.key = SDLK_C;
    screen.handle_event(event);
    CHECK(screen.help_groups().front().title == "Legacy conversion");
    event.key.key = SDLK_ESCAPE;
    screen.handle_event(event);
    CHECK(screen.take_nav().kind == ui::NavKind::None);
    CHECK(screen.help_groups().front().title == "Legacy vault");
    event.key.key = SDLK_RETURN;
    // Enter defaults to browsing the original file, without destination credentials.
    screen.handle_event(event);
    const auto nav = screen.take_nav();
    CHECK(nav.kind == ui::NavKind::ToGallery);
    CHECK(nav.path.empty());
    event = {};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = SDL_BUTTON_RIGHT;
    event.button.x = 100;
    event.button.y = 260;
    screen.handle_event(event);
    CHECK(screen.take_nav().kind == ui::NavKind::None);
    event.button.button = SDL_BUTTON_LEFT;
    screen.handle_event(event);
    CHECK(screen.take_nav().kind == ui::NavKind::ToGallery);
    CHECK_EQ(fs::file_size(tv.path), size_before);
    CHECK(fs::last_write_time(tv.path) == modified_before);
}
