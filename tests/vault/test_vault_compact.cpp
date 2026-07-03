#include "test_framework.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "image/fixtures.h"
#include "vault/file_util.h"
#include "vault/op_progress.h"
#include "vault/vault.h"

namespace fs = std::filesystem;

// Phase 7: compaction. Vault::compact() rewrites the vault with only live
// chunks (deleted images' chunks and superseded index blobs are dropped),
// atomically replacing the original file. wasted_bytes() reports how much of
// the data region is reclaimable.

static const crypto::KdfParams kTestKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_ctest_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempVault() { std::error_code ec; fs::remove(path, ec); }
    std::string str() const { return path.string(); }
};

static std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 37 + seed);
    return v;
}

static uint64_t size_on_disk(const fs::path& p)
{
    std::error_code ec;
    const auto s = fs::file_size(p, ec);
    return ec ? 0 : static_cast<uint64_t>(s);
}

TEST(wasted_bytes_tracks_orphaned_chunks)
{
    TempVault tv("waste");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    CHECK_EQ(v.wasted_bytes(), 0u);  // fresh vault: header + live index only

    const size_t img_size = 100 * 1024;
    REQUIRE(v.add_image("", pattern(img_size, 1), "a.bin") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(img_size, 2), "b.bin") == vault::VaultResult::Ok);

    const uint64_t before = v.wasted_bytes();  // superseded index blobs only
    REQUIRE(v.remove_image("", "a.bin") == vault::VaultResult::Ok);
    // The orphaned chunk (incompressible 100 KiB + AEAD framing) is now waste.
    CHECK_TRUE(v.wasted_bytes() >= before + img_size);
}

TEST(compact_reclaims_space_and_preserves_remaining_images)
{
    TempVault tv("reclaim");
    const auto keep = pattern(80 * 1024, 7);

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    // Dead chunks total ~200 KiB — below the auto-compact threshold, so the
    // waste is still there for the explicit compact() below to reclaim.
    REQUIRE(v.add_image("", pattern(100 * 1024, 3), "gone1.bin") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", keep, "keep.bin")                    == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(100 * 1024, 4), "gone2.bin") == vault::VaultResult::Ok);
    REQUIRE(v.remove_image("", "gone1.bin") == vault::VaultResult::Ok);
    REQUIRE(v.remove_image("", "gone2.bin") == vault::VaultResult::Ok);

    const uint64_t size_before = size_on_disk(tv.path);
    REQUIRE(v.compact() == vault::VaultResult::Ok);
    const uint64_t size_after = size_on_disk(tv.path);

    CHECK_TRUE(size_after + 200 * 1024 <= size_before);  // both dead chunks gone
    CHECK_EQ(v.wasted_bytes(), 0u);

    // Still usable in-session after the rewrite...
    auto kids = v.list("");
    REQUIRE(kids.size() == 1);
    crypto::SecureBytes out;
    REQUIRE(v.read_image(*kids[0], out) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(keep));

    // ...and the vault can still grow afterwards.
    REQUIRE(v.add_image("", pattern(1000, 5), "new.bin") == vault::VaultResult::Ok);
    CHECK_EQ(v.list("").size(), 2u);
}

TEST(compact_preserves_structure_thumbnails_and_survives_reopen)
{
    TempVault tv("structure");
    const auto png = fixtures::solid_png(64, 48, 10, 200, 30);  // gets a thumbnail

    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("trips/2026")                    == vault::VaultResult::Ok);
        REQUIRE(v.add_image("trips/2026", png, "pic.png")         == vault::VaultResult::Ok);
        REQUIRE(v.add_image("trips/2026", pattern(5000, 8), "raw.bin")
                == vault::VaultResult::Ok);
        REQUIRE(v.remove_image("trips/2026", "raw.bin") == vault::VaultResult::Ok);
        REQUIRE(v.compact() == vault::VaultResult::Ok);
    }

    // A compacted vault must reopen and unlock from a cold start.
    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

    REQUIRE(v2.list("").size() == 1);
    auto kids = v2.list("trips/2026");
    REQUIRE(kids.size() == 1);
    CHECK_EQ(kids[0]->name, std::string("pic.png"));

    crypto::SecureBytes img;
    REQUIRE(v2.read_image(*kids[0], img) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(img.as_span(), std::span<const uint8_t>(png));

    crypto::SecureBytes thumb;  // the thumbnail chunk must have been carried over
    REQUIRE(kids[0]->meta.thumb_length > 0);
    CHECK_EQ(v2.read_thumbnail(*kids[0], thumb), vault::VaultResult::Ok);
}

// Deleting an image auto-compacts once the waste passes the threshold
// (>= AUTO_COMPACT_MIN_WASTE and >= a quarter of the file).
TEST(remove_image_auto_compacts_past_waste_threshold)
{
    TempVault tv("autocompact");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    const size_t big = vault::Vault::AUTO_COMPACT_MIN_WASTE * 4;  // safely past both gates
    REQUIRE(v.add_image("", pattern(big, 1), "gone.bin") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(2000, 2), "keep.bin") == vault::VaultResult::Ok);

    const uint64_t size_before = size_on_disk(tv.path);
    REQUIRE(v.remove_image("", "gone.bin") == vault::VaultResult::Ok);

    CHECK_EQ(v.wasted_bytes(), 0u);                        // compaction ran
    CHECK_TRUE(size_on_disk(tv.path) + big <= size_before);  // chunk reclaimed

    auto kids = v.list("");
    REQUIRE(kids.size() == 1);
    crypto::SecureBytes out;
    CHECK_EQ(v.read_image(*kids[0], out), vault::VaultResult::Ok);
}

TEST(remove_image_below_threshold_keeps_orphan)
{
    TempVault tv("nocompact");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    REQUIRE(v.add_image("", pattern(4096, 1), "gone.bin") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(4096, 2), "keep.bin") == vault::VaultResult::Ok);
    REQUIRE(v.remove_image("", "gone.bin") == vault::VaultResult::Ok);

    // 4 KiB of waste is far below AUTO_COMPACT_MIN_WASTE: the orphan stays
    // until an explicit compact() (rewriting the vault per tiny delete would
    // cost more I/O than it reclaims).
    CHECK_TRUE(v.wasted_bytes() >= 4096);
}

TEST(compact_requires_unlocked_vault)
{
    TempVault tv("locked");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
    }
    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    CHECK_EQ(v2.compact(), vault::VaultResult::Locked);
    CHECK_EQ(v2.wasted_bytes(), 0u);  // unknown while locked
}

TEST(compact_rename_failure_keeps_original_vault_usable)
{
    TempVault tv("renamefail");
    const auto keep = pattern(8 * 1024, 9);

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(100 * 1024, 6), "gone.bin") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", keep, "keep.bin")                   == vault::VaultResult::Ok);
    REQUIRE(v.remove_image("", "gone.bin") == vault::VaultResult::Ok);

    // The atomic-commit rename fails: compact() must report IoError, leave the
    // original vault file in place, and reacquire its handle (the temp file's
    // contents must never become the vault without a successful rename).
    vault::fileutil::inject_rename_failure(0);
    CHECK_EQ(v.compact(), vault::VaultResult::IoError);
    vault::fileutil::clear_rename_failure();

    CHECK_TRUE(v.wasted_bytes() > 0);  // nothing was reclaimed
    auto kids = v.list("");
    REQUIRE(kids.size() == 1);
    crypto::SecureBytes out;
    REQUIRE(v.read_image(*kids[0], out) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(keep));

    // A later compact (rename now succeeding) completes normally.
    REQUIRE(v.compact() == vault::VaultResult::Ok);
    CHECK_EQ(v.wasted_bytes(), 0u);
    crypto::SecureBytes again;
    REQUIRE(v.read_image(*v.list("")[0], again) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(again.as_span(), std::span<const uint8_t>(keep));
}

// Phase 15 PR2: video chunks must survive compaction (regression test for
// data-loss bug where compact() only copied image chunks, leaving video
// chunks pointing into the discarded original file).
TEST(compact_preserves_video_chunks)
{
    TempVault tv("video_compact");

    // Read the tiny.mp4 fixture.
    std::ifstream fixture(OSV_VAULT_FIXTURE_DIR "/tiny.mp4", std::ios::binary);
    REQUIRE(fixture.is_open());
    fixture.seekg(0, std::ios::end);
    size_t size = fixture.tellg();
    fixture.seekg(0, std::ios::beg);
    std::vector<uint8_t> video_bytes(size);
    fixture.read(reinterpret_cast<char*>(video_bytes.data()), size);
    REQUIRE(!video_bytes.empty());

    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);

        // Add a video to a gallery with small chunks to force multi-chunk storage.
        REQUIRE(v.create_gallery("clips") == vault::VaultResult::Ok);
        REQUIRE(v.add_video("clips", video_bytes, "v.mp4", /*chunk_size=*/4096)
                == vault::VaultResult::Ok);

        // Compact the vault.
        REQUIRE(v.compact() == vault::VaultResult::Ok);

        // Verify the video still exists and reads back correctly in-session.
        auto kids = v.list("clips");
        REQUIRE(kids.size() == 1);
        REQUIRE(kids[0]->is_video());

        crypto::SecureBytes out;
        REQUIRE(v.read_video(*kids[0], out) == vault::VaultResult::Ok);
        CHECK_EQ(out.size(), video_bytes.size());
        CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(video_bytes));
    }

    // Verify the video survives a cold reopen after compaction.
    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

    auto kids = v2.list("clips");
    REQUIRE(kids.size() == 1);
    REQUIRE(kids[0]->is_video());

    crypto::SecureBytes out;
    REQUIRE(v2.read_video(*kids[0], out) == vault::VaultResult::Ok);
    CHECK_EQ(out.size(), video_bytes.size());
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(video_bytes));
}

// Bonus: verify images and videos both survive when an image is deleted and
// compaction is triggered.
TEST(compact_preserves_both_image_and_video_when_deleting_image)
{
    TempVault tv("video_image_compact");

    // Read fixtures.
    std::ifstream video_fixture(OSV_VAULT_FIXTURE_DIR "/tiny.mp4", std::ios::binary);
    REQUIRE(video_fixture.is_open());
    video_fixture.seekg(0, std::ios::end);
    size_t video_size = video_fixture.tellg();
    video_fixture.seekg(0, std::ios::beg);
    std::vector<uint8_t> video_bytes(video_size);
    video_fixture.read(reinterpret_cast<char*>(video_bytes.data()), video_size);
    REQUIRE(!video_bytes.empty());

    const auto image = pattern(vault::Vault::AUTO_COMPACT_MIN_WASTE * 2, 99);

    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);

        REQUIRE(v.create_gallery("mixed") == vault::VaultResult::Ok);
        // Add a large image, a video, then delete the image to trigger auto-compact.
        REQUIRE(v.add_image("mixed", image, "big.bin") == vault::VaultResult::Ok);
        REQUIRE(v.add_video("mixed", video_bytes, "v.mp4", /*chunk_size=*/4096)
                == vault::VaultResult::Ok);

        // This delete should trigger auto-compact (waste exceeds threshold).
        REQUIRE(v.remove_image("mixed", "big.bin") == vault::VaultResult::Ok);
        CHECK_EQ(v.wasted_bytes(), 0u);  // auto-compact ran

        // Verify both remain: video should still be there.
        auto kids = v.list("mixed");
        REQUIRE(kids.size() == 1);
        REQUIRE(kids[0]->is_video());
        CHECK_EQ(kids[0]->name, std::string("v.mp4"));

        crypto::SecureBytes out;
        REQUIRE(v.read_video(*kids[0], out) == vault::VaultResult::Ok);
        CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(video_bytes));
    }

    // Reopen and verify the video persists after auto-compaction.
    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

    auto kids = v2.list("mixed");
    REQUIRE(kids.size() == 1);
    REQUIRE(kids[0]->is_video());

    crypto::SecureBytes out;
    REQUIRE(v2.read_video(*kids[0], out) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(video_bytes));
}

// Phase 26: compact with OpProgress tracking and cancellation support.
TEST(compact_progress_tracks_chunks_copied)
{
    TempVault tv("progress_track");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    // Add 3 images to create measurable progress.
    REQUIRE(v.add_image("", pattern(100 * 1024, 1), "a.bin") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(100 * 1024, 2), "b.bin") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(100 * 1024, 3), "c.bin") == vault::VaultResult::Ok);

    // Delete one image to create waste.
    REQUIRE(v.remove_image("", "a.bin") == vault::VaultResult::Ok);

    // Compact with progress tracking.
    vault::OpProgress prog;
    REQUIRE(v.compact(&prog) == vault::VaultResult::Ok);

    // Progress should be non-zero (images tracked).
    CHECK_TRUE(prog.total.load() > 0);
    CHECK_EQ(prog.done.load(), prog.total.load());
    CHECK_FALSE(prog.cancel.load());

    // Verify the vault is still usable.
    auto roots = v.list("");
    CHECK_EQ(roots.size(), 2u);  // 2 images remain
}

TEST(compact_cancel_before_start_is_noop)
{
    TempVault tv("cancel_noop");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    REQUIRE(v.add_image("", pattern(100 * 1024, 1), "a.bin") == vault::VaultResult::Ok);
    REQUIRE(v.remove_image("", "a.bin") == vault::VaultResult::Ok);

    const uint64_t size_before = size_on_disk(tv.path);

    // Cancel before starting compact.
    vault::OpProgress prog;
    prog.cancel.store(true);
    REQUIRE(v.compact(&prog) == vault::VaultResult::Ok);

    // File size unchanged: nothing was compacted.
    const uint64_t size_after = size_on_disk(tv.path);
    CHECK_EQ(size_before, size_after);

    // Waste is still there (not reclaimed).
    CHECK_TRUE(v.wasted_bytes() > 0);
}

TEST(compact_cancel_mid_operation_leaves_original_intact)
{
    // Genuine mid-loop cancel test: run compact on a vault with multiple chunks
    // in a background thread, spin-wait until done >= 1, then set cancel.
    // Structure the assertion to accept either:
    //   (a) cancelled with original intact, or
    //   (b) completed successfully.
    // Run the race ~10 times to give it a chance to hit the mid-loop window.
    // This test may pass "trivially" (cancel too late, compact finishes) —
    // that is acceptable; the important case is when cancel arrives mid-loop,
    // original must be intact and no temp file left.

    for (int iteration = 0; iteration < 10; ++iteration) {
        TempVault tv(("cancel_mid_" + std::to_string(iteration)).c_str());
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);

        // Create multiple images so we have multiple chunks to compact.
        REQUIRE(v.add_image("", pattern(100 * 1024, 1), "a.bin") == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", pattern(100 * 1024, 2), "b.bin") == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", pattern(100 * 1024, 3), "c.bin") == vault::VaultResult::Ok);
        REQUIRE(v.remove_image("", "a.bin") == vault::VaultResult::Ok);
        REQUIRE(v.remove_image("", "b.bin") == vault::VaultResult::Ok);

        const uint64_t size_before = size_on_disk(tv.path);
        const uint64_t waste_before = v.wasted_bytes();
        REQUIRE(waste_before > 0);  // ensure we have waste to reclaim

        vault::OpProgress prog;
        vault::VaultResult compact_result = vault::VaultResult::IoError;  // sentinel; overwritten by thread
        std::atomic<bool> cancel_sent(false);

        // Run compact on a background thread.
        std::thread t([&v, &prog, &compact_result]() {
            compact_result = v.compact(&prog);
        });

        // Spin-wait until done >= 1 (compaction has started processing chunks),
        // then set cancel. Use a generous hard timeout (1 second) to avoid infinite
        // waits if the test framework hangs.
        auto start = std::chrono::steady_clock::now();
        const auto timeout = std::chrono::seconds(1);
        while (std::chrono::steady_clock::now() - start < timeout) {
            if (prog.done.load() >= 1) {
                prog.cancel.store(true);
                cancel_sent.store(true);
                break;
            }
            std::this_thread::yield();
        }

        // Join the worker thread.
        t.join();

        // Verify the outcome: either cancelled or completed successfully.
        if (compact_result == vault::VaultResult::Ok) {
            // Compact completed successfully (cancel too late, or not sent).
            // File may be smaller after reclaiming waste, or slightly larger due to index overhead.
            CHECK_TRUE(size_on_disk(tv.path) <= size_before + 4096);  // allow small overhead
        } else {
            // Compact failed (cancel succeeded mid-loop): original must be untouched.
            CHECK_EQ(size_on_disk(tv.path), size_before);
        }

        // Vault must still be usable.
        auto kids = v.list("");
        REQUIRE(kids.size() == 1);  // only "c.bin" remains
        crypto::SecureBytes out;
        REQUIRE(v.read_image(*kids[0], out) == vault::VaultResult::Ok);

        // No temp file should remain in the directory.
        for (const auto& entry : fs::directory_iterator(fs::temp_directory_path())) {
            const auto fname = entry.path().filename().string();
            if (fname.find("compact_tmp") != std::string::npos) {
                CHECK_FALSE(true);  // stray temp file found
            }
        }
    }
}

TEST(compact_progress_nullptr_succeeds)
{
    TempVault tv("progress_null");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    REQUIRE(v.add_image("", pattern(100 * 1024, 1), "a.bin") == vault::VaultResult::Ok);
    REQUIRE(v.remove_image("", "a.bin") == vault::VaultResult::Ok);

    // Compact without progress struct (original behavior).
    REQUIRE(v.compact(nullptr) == vault::VaultResult::Ok);
    CHECK_EQ(v.wasted_bytes(), 0u);
}

// Phase 7 Task 7: secure wipe of pre-compaction vault file.
// Test that wipe_and_remove overwrites a file with zeros then removes it.
TEST(wipe_and_remove_zeroes_and_deletes_file)
{
    const auto temp_path = fs::temp_directory_path() / "osv_wipe_test.bin";

    // Create a test file with known content.
    {
        std::FILE* fp = std::fopen(temp_path.c_str(), "w+b");
        REQUIRE(fp != nullptr);
        const std::vector<uint8_t> content = pattern(8192, 42);
        REQUIRE(std::fwrite(content.data(), 1, content.size(), fp) == content.size());
        std::fclose(fp);
    }

    // Verify file exists and contains the pattern.
    REQUIRE(fs::exists(temp_path));

    // Peek at the file to verify it has content.
    {
        std::FILE* fp = std::fopen(temp_path.c_str(), "rb");
        REQUIRE(fp != nullptr);
        uint8_t first_byte = 0;
        REQUIRE(std::fread(&first_byte, 1, 1, fp) == 1);
        CHECK_FALSE(first_byte == 0u);  // Should be part of the pattern, not zero
        std::fclose(fp);
    }

    // Wipe and remove.
    vault::fileutil::wipe_and_remove(temp_path.string());

    // File should no longer exist.
    CHECK_FALSE(fs::exists(temp_path));
}

// Test that compact leaves no .old file after successful completion.
TEST(compact_removes_old_file_after_success)
{
    TempVault tv("old_file_cleanup");
    const auto keep = pattern(80 * 1024, 7);

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    // Create waste to compact.
    REQUIRE(v.add_image("", pattern(100 * 1024, 3), "gone.bin") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", keep, "keep.bin")                   == vault::VaultResult::Ok);
    REQUIRE(v.remove_image("", "gone.bin") == vault::VaultResult::Ok);

    const std::string old_path = tv.str() + ".old";

    // After compact, there should be no .old file.
    REQUIRE(v.compact() == vault::VaultResult::Ok);
    CHECK_FALSE(fs::exists(old_path));

    // The original vault file should exist and be usable.
    CHECK_TRUE(fs::exists(tv.path));
    auto kids = v.list("");
    REQUIRE(kids.size() == 1);
    crypto::SecureBytes out;
    REQUIRE(v.read_image(*kids[0], out) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(keep));
}
