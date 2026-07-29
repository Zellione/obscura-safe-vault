#include "test_framework.h"
#include "ui/zip_test_helpers.h"   // ziptest::make_vault, fresh_dir, cleanup_dir, kTestKdf
#include "image/fixtures.h"        // fixtures::solid_jpeg
#include "vault/vault.h"

#ifdef __linux__
#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

using ziptest::cleanup_dir;
using ziptest::fresh_dir;
using ziptest::kTestKdf;

#ifdef __linux__

// RAII helper to exhaust and restore file descriptors on Linux.
// Holds a vector of hoarded fds and closes them on destruction.
struct FdExhauster {
    std::vector<int> hoarded;

    // Exhaust all available fds by repeatedly opening /dev/null.
    // Returns the number of fds successfully opened before EMFILE.
    [[nodiscard]] int exhaust()
    {
        int opened = 0;
        while (true) {
            int fd = ::open("/dev/null", O_RDONLY);
            if (fd < 0) {
                // EMFILE: too many open files — exhaustion successful.
                break;
            }
            hoarded.push_back(fd);
            ++opened;
        }
        return opened;
    }

    // Close exactly `count` fds from the hoarded pile.
    // Precondition: hoarded.size() >= count (caller must verify).
    void free_slots(int count)
    {
        if (static_cast<int>(hoarded.size()) < count) {
            // Should not happen if caller counted correctly; abort here.
            std::abort();
        }
        for (int i = 0; i < count; ++i) {
            int fd = hoarded.back();
            hoarded.pop_back();
            ::close(fd);
        }
    }

    // Destructor: close all remaining hoarded fds to avoid leaking them.
    ~FdExhauster()
    {
        for (int fd : hoarded) {
            ::close(fd);
        }
    }
};

// Test: create() fails with IoError when thumb_fp_ fopen exhausts fds.
// Scenario: exhaust all fds, then free exactly the slots for fp_+read_fp_ (2 fds),
// so the third fopen (thumb_fp_) fails.
TEST(create_thumb_fp_fopen_exhausts_fds)
{
    const auto dir = fresh_dir("fd_exhaust_create");

    FdExhauster exhauster;
    [[maybe_unused]] int exhausted_count = exhauster.exhaust();
    // Free exactly 2 fd slots: one for fp_ write, one for read_fp_ read.
    // After this, the third fopen (thumb_fp_) will fail with EMFILE.
    exhauster.free_slots(2);

    vault::Vault v;
    const auto vault_path = (dir / "test.osv").string();
    const std::vector<uint8_t> pw{'p', 'w'};
    // create() will:
    //   1. fopen(path, "w+b") for fp_ — uses freed slot 1, succeeds
    //   2. commit_index() — writes to fp_
    //   3. fopen(path, "rb") for read_fp_ — uses freed slot 2, succeeds
    //   4. fopen(path, "rb") for thumb_fp_ — EMFILE, returns IoError
    auto result = vault::Vault::create(vault_path, pw, {}, kTestKdf, v);
    REQUIRE(result == vault::VaultResult::IoError);
    // Vault should be in a locked/unusable state (failed during setup).
    REQUIRE(!v.is_unlocked());
}


#endif  // __linux__
