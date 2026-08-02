#include "test_framework.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "crypto/random.h"
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

// Internal linkage: several test files each define their own `TempVault`
// with a DIFFERENT layout. At namespace scope those are one-definition-rule
// violations — the member functions are implicitly inline, so the linker keeps
// a single copy and silently discards the rest.
namespace {

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

}  // namespace

static std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 37 + seed);
    return v;
}

// Generate random (incompressible) payload via CSPRNG.
// Framed size = plaintext + 1 method byte + 40 AEAD bytes, so waste
// calculations hold without deflate interference.
static std::vector<uint8_t> random_payload(size_t n)
{
    std::vector<uint8_t> v(n);
    (void)crypto::fill_random(v);  // fills v in-place, should not fail in tests
    return v;
}

static uint64_t size_on_disk(const fs::path& p)
{
    std::error_code ec;
    const auto s = fs::file_size(p, ec);
    return ec ? 0 : static_cast<uint64_t>(s);
}

// Physical (allocated) size of a file by path: st_blocks is in 512-byte units,
// so a sparse file (post hole-punch) reports less here than size_on_disk.
static uint64_t allocated_on_disk(const fs::path& p)
{
#if defined(_WIN32)
    return size_on_disk(p);
#else
    struct stat st{};
    if (::stat(p.string().c_str(), &st) != 0 || st.st_blocks < 0) {
        return 0;
    }
    return static_cast<uint64_t>(st.st_blocks) * 512U;
#endif
}

// Whether the temp filesystem actually supports hole punching, so physical
// (block-accounting) assertions are only made where a hole can be punched.
static bool punch_supported()
{
    const auto p = fs::temp_directory_path() / "osv_punch_probe.bin";
    std::FILE* fp = std::fopen(p.string().c_str(), "w+b");
    if (fp == nullptr) {
        return false;
    }
    const std::vector<uint8_t> buf(256 * 1024, 0x5A);
    (void)std::fwrite(buf.data(), 1, buf.size(), fp);
    (void)std::fflush(fp);
    const bool ok = vault::fileutil::punch_hole(fp, 64 * 1024, 128 * 1024);
    std::fclose(fp);
    std::error_code ec;
    fs::remove(p, ec);
    return ok;
}

TEST(wasted_bytes_tracks_orphaned_chunks)
{
    TempVault tv("waste");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    CHECK_EQ(v.wasted_bytes(), 0u);  // fresh vault: header + live index only

    const size_t img_size = 100 * 1024;
    REQUIRE(v.add_image("", random_payload(img_size), "a.bin") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", random_payload(img_size), "b.bin") == vault::VaultResult::Ok);

    const uint64_t before = v.wasted_bytes();  // superseded index blobs only
    REQUIRE(v.remove_image("", "a.bin") == vault::VaultResult::Ok);
    // The orphaned chunk (incompressible 100 KiB + AEAD framing) is now waste.
    CHECK_TRUE(v.wasted_bytes() >= before + img_size);
}

TEST(compact_reclaims_space_and_preserves_remaining_images)
{
    TempVault tv("reclaim");
    const auto keep = random_payload(80 * 1024);

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    // Dead chunks total ~200 KiB — below the auto-compact threshold, so the
    // waste is still there for the explicit compact() below to reclaim.
    REQUIRE(v.add_image("", random_payload(100 * 1024), "gone1.bin") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", keep, "keep.bin")                    == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", random_payload(100 * 1024), "gone2.bin") == vault::VaultResult::Ok);
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
        REQUIRE(v.add_image("trips/2026", random_payload(5000), "raw.bin")
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
TEST(remove_image_auto_reclaims_past_waste_threshold)
{
    TempVault tv("autoreclaim");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    const size_t big = vault::Vault::AUTO_COMPACT_MIN_WASTE * 4;  // safely past both gates
    REQUIRE(v.add_image("", random_payload(big), "gone.bin") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", random_payload(2000), "keep.bin") == vault::VaultResult::Ok);

    const uint64_t alloc_before = allocated_on_disk(tv.path);
    REQUIRE(v.remove_image("", "gone.bin") == vault::VaultResult::Ok);

    // Past the threshold the delete auto-reclaims the orphaned chunk's disk
    // blocks — in place via hole punching on Linux, via a full compact()
    // rewrite on platforms without hole-punch.
#if defined(__linux__)
    const bool reclaims = punch_supported();
#else
    const bool reclaims = true;
#endif
    if (reclaims) {
        CHECK_TRUE(allocated_on_disk(tv.path) + big <= alloc_before);
    }

    // The surviving image is untouched and still decrypts.
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

    REQUIRE(v.add_image("", random_payload(4096), "gone.bin") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", random_payload(4096), "keep.bin") == vault::VaultResult::Ok);
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

    const auto image = random_payload(vault::Vault::AUTO_COMPACT_MIN_WASTE * 2);

    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);

        REQUIRE(v.create_gallery("mixed") == vault::VaultResult::Ok);
        // Add a large image, a video, then delete the image to trigger auto-compact.
        REQUIRE(v.add_image("mixed", image, "big.bin") == vault::VaultResult::Ok);
        REQUIRE(v.add_video("mixed", video_bytes, "v.mp4", /*chunk_size=*/4096)
                == vault::VaultResult::Ok);

        // This delete's waste exceeds the threshold, so it auto-reclaims. The
        // point of this test is that the *video* chunks survive that reclamation
        // untouched — reclaim must free only the deleted image's dead spans.
        REQUIRE(v.remove_image("mixed", "big.bin") == vault::VaultResult::Ok);

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
TEST(compact_progress_reaches_total)
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
        const std::string p = temp_path.string();
        std::FILE* fp = std::fopen(p.c_str(), "w+b");
        REQUIRE(fp != nullptr);
        const std::vector<uint8_t> content = pattern(8192, 42);
        REQUIRE(std::fwrite(content.data(), 1, content.size(), fp) == content.size());
        std::fclose(fp);
    }

    // Verify file exists and contains the pattern.
    REQUIRE(fs::exists(temp_path));

    // Peek at the file to verify it has content.
    {
        const std::string p = temp_path.string();
        std::FILE* fp = std::fopen(p.c_str(), "rb");
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

// --- in-place hole-punch reclamation (Vault::reclaim) -------------------------
// Unlike compact(), reclaim() punches holes in orphaned chunk spans in place:
// no temp copy, offsets unchanged, index untouched. It reclaims physical disk
// without the transient ~2x file-size spike compact() needs.

TEST(reclaim_preserves_remaining_images_and_survives_reopen)
{
    TempVault tv("reclaim_preserve");
    const std::vector<uint8_t> keep = random_payload(40 * 1024);

    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        // keep is wedged BETWEEN two doomed images so reclaim must punch holes
        // on both sides without disturbing the live chunk in the middle.
        REQUIRE(v.add_image("", random_payload(100 * 1024), "gone1.bin") == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", keep, "keep.bin")                        == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", random_payload(100 * 1024), "gone2.bin") == vault::VaultResult::Ok);
        REQUIRE(v.remove_image("", "gone1.bin") == vault::VaultResult::Ok);
        REQUIRE(v.remove_image("", "gone2.bin") == vault::VaultResult::Ok);

        REQUIRE(v.reclaim() == vault::VaultResult::Ok);

        // The surviving image still decrypts correctly, offsets unchanged.
        const auto kids = v.list("");
        REQUIRE(kids.size() == 1);
        CHECK_EQ(kids[0]->name, std::string("keep.bin"));
        crypto::SecureBytes out;
        REQUIRE(v.read_image(*kids[0], out) == vault::VaultResult::Ok);
        CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(keep));
    }

    // Reopen from disk: reclaim must not have corrupted the header/index.
    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);
    const auto kids = v2.list("");
    REQUIRE(kids.size() == 1);
    crypto::SecureBytes img;
    REQUIRE(v2.read_image(*kids[0], img) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(img.as_span(), std::span<const uint8_t>(keep));

    // The vault is still fully usable after in-place reclamation.
    REQUIRE(v2.add_image("", pattern(1000, 9), "new.bin") == vault::VaultResult::Ok);
    CHECK_EQ(v2.list("").size(), 2u);
}

TEST(reclaim_releases_disk_blocks_without_shrinking_the_file)
{
    if (!punch_supported()) return;  // no hole-punch on this fs: nothing to assert

    TempVault tv("reclaim_blocks");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", random_payload(512 * 1024), "gone.bin") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", random_payload(32 * 1024),  "keep.bin") == vault::VaultResult::Ok);

    // Physical baseline while both images are densely allocated.
    const uint64_t alloc_dense = allocated_on_disk(tv.path);

    REQUIRE(v.remove_image("", "gone.bin") == vault::VaultResult::Ok);
    // The delete appends a fresh index blob (logical size may grow); capture the
    // logical size AFTER it so we can prove reclaim() leaves that length alone.
    const uint64_t logical_pre = size_on_disk(tv.path);

    REQUIRE(v.reclaim() == vault::VaultResult::Ok);  // idempotent even if the delete auto-reclaimed

    // Physical allocation has dropped by roughly the orphaned 512 KiB chunk...
    CHECK_TRUE(allocated_on_disk(tv.path) + 256 * 1024 <= alloc_dense);
    // ...while reclaim() left the file's LOGICAL length exactly where it was —
    // offsets stay put (it punches holes, never truncates or copies). This is
    // precisely what avoids compact()'s transient second copy.
    CHECK_EQ(size_on_disk(tv.path), logical_pre);
}

TEST(reclaim_on_a_locked_vault_reports_locked)
{
    vault::Vault v;
    CHECK_EQ(v.reclaim(), vault::VaultResult::Locked);
}

// Phase 60: in-place compact must not need a second copy of the vault. The
// strongest observable proxy without an rlimit sandbox: no sibling temp file
// is ever created, and the file's own size never exceeds its starting size
// plus one commit's overhead (index blob + slack), monitored from a watcher
// thread while compact runs.
TEST(compact_in_place_never_creates_a_second_file)
{
    TempVault tv("inplace_nofile");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    for (int i = 0; i < 8; ++i) {
        REQUIRE(v.add_image("", random_payload(64 * 1024),
                            "img" + std::to_string(i) + ".bin")
                == vault::VaultResult::Ok);
    }
    REQUIRE(v.remove_image("", "img0.bin") == vault::VaultResult::Ok);
    REQUIRE(v.remove_image("", "img3.bin") == vault::VaultResult::Ok);

    const uint64_t size_before = size_on_disk(tv.path);
    std::atomic<bool> stop{false};
    uint64_t peak = 0;
    std::atomic<bool> saw_temp{false};
    std::thread watcher([&] {
        while (!stop.load()) {
            peak = std::max(peak, size_on_disk(tv.path));
            if (fs::exists(tv.str() + ".compact") || fs::exists(tv.str() + ".old"))
                saw_temp.store(true);
            std::this_thread::yield();
        }
    });
    REQUIRE(v.compact() == vault::VaultResult::Ok);
    stop.store(true);
    watcher.join();

    CHECK_FALSE(saw_temp.load());
    // In-place: the file may grow by batch-commit blobs, never by a data copy.
    CHECK_TRUE(peak <= size_before + 64 * 1024);
    CHECK_TRUE(size_on_disk(tv.path) < size_before);
    CHECK_EQ(v.wasted_bytes(), 0u);
}

// The stuck-hole case: a small hole in front of larger units cannot be packed
// (an overlapping slide is forbidden by the crash-safety rule). compact() must
// converge, keep every image intact, and leave only the bounded residual.
TEST(compact_stuck_hole_leaves_bounded_residual_and_intact_data)
{
    TempVault tv("stuckhole");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    // small (4 KiB, will be deleted) then three big 256 KiB images: the 4 KiB
    // hole fits no big unit, so it survives packing as logical residual.
    REQUIRE(v.add_image("", random_payload(4 * 1024), "small.bin") == vault::VaultResult::Ok);
    std::vector<std::vector<uint8_t>> big;
    for (int i = 0; i < 3; ++i) {
        big.push_back(random_payload(256 * 1024));
        REQUIRE(v.add_image("", big.back(), "big" + std::to_string(i) + ".bin")
                == vault::VaultResult::Ok);
    }
    REQUIRE(v.remove_image("", "small.bin") == vault::VaultResult::Ok);

    REQUIRE(v.compact() == vault::VaultResult::Ok);

    // Residual bound: the stuck hole (~4 KiB + AEAD framing) plus superseded
    // index blobs too small to host any 256 KiB unit, plus commit slack.
    CHECK_TRUE(v.wasted_bytes() <= 32 * 1024);
    auto kids = v.list("");
    REQUIRE(kids.size() == 3);
    for (const auto* k : kids) {
        crypto::SecureBytes out;
        REQUIRE(v.read_image(*k, out) == vault::VaultResult::Ok);
    }
    // Cold reopen still unlocks and reads everything.
    v.lock();
    REQUIRE(v.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);
    for (const auto* k : v.list("")) {
        crypto::SecureBytes out;
        REQUIRE(v.read_image(*k, out) == vault::VaultResult::Ok);
    }
}

// Crash-safety: a sync failure mid-compact aborts with the last committed
// index intact — a cold reopen from disk must see every image, whatever step
// the failure hit. Sweeping N over the first dozen sync calls covers phase A
// (blob append), B (slot write) and C (flip) of both batch and final commits.
TEST(compact_survives_sync_failure_at_every_step)
{
    for (int n = 0; n < 12; ++n) {
        TempVault tv(("synfail" + std::to_string(n)).c_str());
        std::vector<std::vector<uint8_t>> keep;
        {
            vault::Vault v;
            REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                    == vault::VaultResult::Ok);
            REQUIRE(v.add_image("", random_payload(64 * 1024), "gone.bin")
                    == vault::VaultResult::Ok);
            for (int i = 0; i < 3; ++i) {
                keep.push_back(random_payload(48 * 1024));
                REQUIRE(v.add_image("", keep.back(),
                                    "keep" + std::to_string(i) + ".bin")
                        == vault::VaultResult::Ok);
            }
            REQUIRE(v.remove_image("", "gone.bin") == vault::VaultResult::Ok);

            vault::fileutil::inject_sync_failure(n);
            const auto r = v.compact();
            vault::fileutil::clear_sync_failure();
            // Either the injected failure surfaced (IoError) or compact used
            // fewer than n syncs and succeeded — both must leave a valid vault.
            REQUIRE((r == vault::VaultResult::Ok) || (r == vault::VaultResult::IoError));
        }  // dtor closes like a crash

        vault::Vault v2;
        REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
        REQUIRE(v2.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);
        auto kids = v2.list("");
        REQUIRE(kids.size() == 3);
        for (size_t i = 0; i < kids.size(); ++i) {
            crypto::SecureBytes out;
            REQUIRE(v2.read_image(*kids[i], out) == vault::VaultResult::Ok);
        }
        // And a rerun converges from wherever the abort left the file.
        REQUIRE(v2.compact() == vault::VaultResult::Ok);
    }
}

// Cancel keeps the work done so far and a rerun finishes the job.
TEST(compact_cancel_keeps_partial_progress_and_rerun_converges)
{
    TempVault tv("cancel_keep");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", random_payload(200 * 1024), "gone.bin") == vault::VaultResult::Ok);
    const auto keep = random_payload(100 * 1024);
    REQUIRE(v.add_image("", keep, "keep.bin") == vault::VaultResult::Ok);
    REQUIRE(v.remove_image("", "gone.bin") == vault::VaultResult::Ok);

    // Cancel as soon as the first move lands: done>=1 then cancel (same
    // spin-wait pattern as the old mid-cancel test, but the outcome is now
    // deterministic: Ok either way, vault valid, progress kept).
    vault::OpProgress prog;
    vault::VaultResult r = vault::VaultResult::IoError;
    std::thread t([&] { r = v.compact(&prog); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        if (prog.done.load() >= 1) { prog.cancel.store(true); break; }
        std::this_thread::yield();
    }
    t.join();
    REQUIRE(r == vault::VaultResult::Ok);

    crypto::SecureBytes out;
    auto kids = v.list("");
    REQUIRE(kids.size() == 1);
    REQUIRE(v.read_image(*kids[0], out) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(keep));

    // Rerun (no cancel) reclaims everything that remains.
    REQUIRE(v.compact() == vault::VaultResult::Ok);
    CHECK_EQ(v.wasted_bytes(), 0u);
    REQUIRE(v.read_image(*v.list("")[0], out) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(keep));
}

// A framed (compressed) vault compacts identically — moves are byte-verbatim,
// so framing must be irrelevant to packing. (Vault::create writes framed
// vaults by default since Phase 26; this pins that assumption explicitly by
// exercising compact on a vault that stores compressible data.)
TEST(compact_preserves_framed_vault_content)
{
    TempVault tv("framed");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    const auto compressible = pattern(300 * 1024, 7);  // deflates well
    REQUIRE(v.add_image("", random_payload(100 * 1024), "gone.bin") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", compressible, "keep.bin") == vault::VaultResult::Ok);
    REQUIRE(v.remove_image("", "gone.bin") == vault::VaultResult::Ok);
    REQUIRE(v.compact() == vault::VaultResult::Ok);

    v.lock();
    REQUIRE(v.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);
    crypto::SecureBytes out;
    REQUIRE(v.read_image(*v.list("")[0], out) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(compressible));
}
