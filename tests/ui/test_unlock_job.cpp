#include "test_framework.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <thread>

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
