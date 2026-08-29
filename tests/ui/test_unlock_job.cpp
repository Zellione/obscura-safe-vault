#include "test_framework.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <thread>

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
    ~TempVault() { std::error_code ec; fs::remove(path, ec); }
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
    CHECK_TRUE(fs::exists(tv.path));
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
