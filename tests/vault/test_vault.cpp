#include "test_framework.h"

#include <fstream>
#include <filesystem>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include "image/fixtures.h"
#include "vault/vault.h"
#include "vault/vault_ops.h"

namespace fs = std::filesystem;

// --- helpers --------------------------------------------------------------

// Cheap Argon2 params so the test suite stays fast (real vaults use the
// 64 MiB / 3-pass default). Security of the KDF is covered by the crypto tests.
static const crypto::KdfParams kTestKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

// RAII temp path: a unique .osv file removed when the helper goes out of scope.
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
               ("osv_test_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempVault()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }
    std::string str() const { return path.string(); }
};

}  // namespace

static std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 37 + seed);
    return v;
}

// Load a GIF fixture from tests/vault/fixtures/ (Phase 47).
// OSV_VAULT_FIXTURE_DIR is set by premake to the absolute path.
static std::vector<uint8_t> load_vault_gif_fixture(const char* name)
{
    #ifndef OSV_VAULT_FIXTURE_DIR
    #define OSV_VAULT_FIXTURE_DIR "tests/vault/fixtures"
    #endif
    const std::string path = std::string(OSV_VAULT_FIXTURE_DIR) + "/" + name;
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// --- tests ----------------------------------------------------------------

TEST(vault_create_leaves_vault_unlocked)
{
    TempVault tv("create");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("hunter2"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    CHECK_TRUE(v.is_unlocked());
}

TEST(vault_add_and_read_image_same_session)
{
    TempVault tv("addread");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    auto img = pattern(20000, 11);
    REQUIRE(v.add_image("", img, "cat.jpg") == vault::VaultResult::Ok);

    auto children = v.list("");
    REQUIRE(children.size() == 1);
    REQUIRE(children[0]->is_image());

    crypto::SecureBytes out;
    REQUIRE(v.read_image(*children[0], out) == vault::VaultResult::Ok);
    REQUIRE(out.size() == img.size());
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(img));
}

TEST(vault_roundtrip_across_lock_reopen_unlock)
{
    TempVault tv("reopen");
    auto img = pattern(12345, 22);
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("correct horse"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", img, "photo.png") == vault::VaultResult::Ok);
        v.lock();
        CHECK_FALSE(v.is_unlocked());
    }

    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    CHECK_FALSE(v2.is_unlocked());
    REQUIRE(v2.unlock(bytes("correct horse"), {}) == vault::VaultResult::Ok);
    CHECK_TRUE(v2.is_unlocked());

    auto children = v2.list("");
    REQUIRE(children.size() == 1);
    crypto::SecureBytes out;
    REQUIRE(v2.read_image(*children[0], out) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(img));
}

TEST(vault_unlock_with_wrong_password_fails)
{
    TempVault tv("wrongpw");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("rightpw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", pattern(100, 1), "a.jpg") == vault::VaultResult::Ok);
    }
    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    CHECK_EQ(v2.unlock(bytes("wrongpw"), {}), vault::VaultResult::AuthFailed);
    CHECK_FALSE(v2.is_unlocked());
}

TEST(vault_keyfile_required_to_unlock)
{
    TempVault tv("keyfile");
    auto keyfile = pattern(64, 99);
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), keyfile, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", pattern(50, 2), "x.jpg") == vault::VaultResult::Ok);
    }
    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    // Right password but missing keyfile -> auth fails.
    CHECK_EQ(v2.unlock(bytes("pw"), {}), vault::VaultResult::AuthFailed);
    // Right password + keyfile -> success.
    REQUIRE(v2.unlock(bytes("pw"), keyfile) == vault::VaultResult::Ok);
}

TEST(vault_nested_galleries_and_list)
{
    TempVault tv("nested");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    REQUIRE(v.create_gallery("vacation") == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("vacation/2024") == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("vacation/2025") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("vacation/2024", pattern(80, 4), "beach.jpg")
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("vacation/2024", pattern(90, 5), "sunset.jpg")
            == vault::VaultResult::Ok);

    auto root_children = v.list("");
    REQUIRE(root_children.size() == 1);
    CHECK_EQ(root_children[0]->name, std::string("vacation"));
    CHECK_TRUE(root_children[0]->is_gallery());

    auto vac = v.list("vacation");
    REQUIRE(vac.size() == 2);

    auto y2024 = v.list("vacation/2024");
    REQUIRE(y2024.size() == 2);
    CHECK_TRUE(y2024[0]->is_image());
    CHECK_TRUE(y2024[1]->is_image());
}

TEST(vault_allows_mixed_galleries)
{
    TempVault tv("mixed");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    // A gallery holding a sub-gallery may ALSO hold media (Phase 46).
    REQUIRE(v.create_gallery("mixed/sub") == vault::VaultResult::Ok);
    CHECK_EQ(v.add_image("mixed", pattern(10, 1), "ok.jpg"),
             vault::VaultResult::Ok);

    // A gallery holding media may ALSO gain a sub-gallery (Phase 46).
    REQUIRE(v.create_gallery("photos") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("photos", pattern(10, 1), "img.jpg") == vault::VaultResult::Ok);
    CHECK_EQ(v.create_gallery("photos/sub"), vault::VaultResult::Ok);

    // The mix survives a lock/unlock round-trip with both child kinds present.
    v.lock();
    REQUIRE(v.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);
    auto mixed_children = v.list("mixed");
    bool has_sub = false;
    bool has_img = false;
    for (const auto* c : mixed_children) {
        if (c->is_gallery() && c->name == "sub") {
            has_sub = true;
        }
        if (c->is_image() && c->name == "ok.jpg") {
            has_img = true;
        }
    }
    CHECK_TRUE(has_sub);
    CHECK_TRUE(has_img);
}

TEST(vault_remove_image)
{
    TempVault tv("remove");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(30, 1), "a.jpg") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(40, 2), "b.jpg") == vault::VaultResult::Ok);
    REQUIRE(v.list("").size() == 2);

    REQUIRE(v.remove_image("", "a.jpg") == vault::VaultResult::Ok);
    auto remaining = v.list("");
    REQUIRE(remaining.size() == 1);
    CHECK_EQ(remaining[0]->name, std::string("b.jpg"));

    CHECK_EQ(v.remove_image("", "ghost.jpg"), vault::VaultResult::NotFound);
}

// Gallery cover montages (Phase 19) read descendant thumbnails by raw span,
// not by node, so the grid can draw a sub-gallery's cover without holding the
// node. read_thumb_span must return the same bytes as read_thumbnail.
TEST(vault_read_thumb_span_matches_read_thumbnail)
{
    TempVault tv("thumbspan");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    const auto png = fixtures::solid_png(64, 48, 10, 200, 30);  // decodable -> thumbnail
    REQUIRE(v.add_image("", png, "pic.png") == vault::VaultResult::Ok);

    auto kids = v.list("");
    REQUIRE(kids.size() == 1);
    REQUIRE(kids[0]->meta.thumb_length > 0);

    crypto::SecureBytes via_node;
    REQUIRE(v.read_thumbnail(*kids[0], via_node) == vault::VaultResult::Ok);

    crypto::SecureBytes via_span;
    REQUIRE(vault::read_thumb_span(v, kids[0]->meta.thumb_offset,
                                   kids[0]->meta.thumb_length, via_span)
            == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(via_span.as_span(), via_node.as_span());
}

TEST(vault_read_thumb_span_rejects_empty_and_locked)
{
    TempVault tv("thumbspan_err");
    crypto::SecureBytes out;
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        // A zero-length span is never a valid thumbnail.
        CHECK_EQ(vault::read_thumb_span(v, 0, 0, out), vault::VaultResult::InvalidArg);
    }
    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    CHECK_EQ(vault::read_thumb_span(v2, 0, 16, out), vault::VaultResult::Locked);
}

TEST(vault_operations_require_unlock)
{
    TempVault tv("locked");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
    }
    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    // Still locked: mutating ops are rejected.
    CHECK_EQ(v2.add_image("", pattern(10, 1), "a.jpg"), vault::VaultResult::Locked);
    CHECK_EQ(v2.create_gallery("g"), vault::VaultResult::Locked);
}

TEST(vault_file_bytes_tracks_size_growth)
{
    TempVault tv("file_bytes");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    // Newly created vault should have some file size.
    const uint64_t initial_size = vault::vault_file_bytes(v);
    CHECK_TRUE(initial_size > 0);

    // Add an image and verify the file grows.
    auto img = pattern(50000, 99);
    REQUIRE(v.add_image("", img, "test.jpg") == vault::VaultResult::Ok);
    const uint64_t after_add_size = vault::vault_file_bytes(v);
    CHECK_TRUE(after_add_size > initial_size);

    // Locked vault returns 0 (no file handle).
    v.lock();
    CHECK_EQ(vault::vault_file_bytes(v), 0);
}

// Regression test for a real crash: appending the new IndexNode to a
// gallery's children (std::vector::push_back) can throw bad_alloc on
// allocation failure. Uncaught, that calls std::terminate() and kills the
// whole process instead of returning an error — the same bug class fixed for
// chunk_codec::resize_buf (PR #57), but at add_image's tree-mutation step.
TEST(add_image_vector_push_failure_returns_io_error_not_terminate)
{
    TempVault tv("add_image_push_fail");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    auto img = pattern(1000, 3);
    vault::vault_ops::inject_push_child_failure(0);   // fail the very next push_child
    CHECK_EQ(v.add_image("", img, "a.jpg"), vault::VaultResult::IoError);
    vault::vault_ops::clear_push_child_failure();

    // The vault must still work normally afterward (fault disarms after firing).
    REQUIRE(v.add_image("", img, "b.jpg") == vault::VaultResult::Ok);
}

// --- Node-name validation at the vault ingress (path-traversal defence) -----
//
// The vault API is the trust boundary: a name that could steer an export write
// out of the destination folder must never reach the index in the first place.
// The repair-instead-of-reject counterpart lives in the importers, which call
// vault::sanitize_node_name (see src/vault/safe_name.h).

TEST(add_image_rejects_unsafe_filenames)
{
    TempVault tv("unsafe_img");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    auto img = pattern(500, 1);
    using enum vault::VaultResult;
    CHECK_EQ(v.add_image("", img, "../escape.jpg"), InvalidArg);
    CHECK_EQ(v.add_image("", img, "/etc/passwd"), InvalidArg);
    CHECK_EQ(v.add_image("", img, ".."), InvalidArg);
    CHECK_EQ(v.add_image("", img, "sub/dir.jpg"), InvalidArg);
    CHECK_EQ(v.add_image("", img, "back\\slash.jpg"), InvalidArg);
    CHECK_EQ(v.add_image("", img, std::string("nul\0byte.jpg", 12)), InvalidArg);
    CHECK_EQ(v.add_image("", img, ""), InvalidArg);   // pre-existing empty check still holds

    // Nothing hostile made it into the index.
    CHECK_EQ(v.list("").size(), 0u);

    // A normal name still works.
    CHECK_EQ(v.add_image("", img, "fine.jpg"), Ok);
    CHECK_EQ(v.list("").size(), 1u);
}

TEST(add_video_rejects_unsafe_filenames)
{
    TempVault tv("unsafe_vid");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    auto data = pattern(500, 2);
    using enum vault::VaultResult;
    CHECK_EQ(v.add_video("", data, "../escape.mp4"), InvalidArg);
    CHECK_EQ(v.add_video("", data, "/tmp/escape.mp4"), InvalidArg);
    CHECK_EQ(v.list("").size(), 0u);
}

TEST(create_gallery_rejects_unsafe_path_segments)
{
    TempVault tv("unsafe_gal");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    using enum vault::VaultResult;
    CHECK_EQ(v.create_gallery(".."), InvalidArg);
    CHECK_EQ(v.create_gallery("ok/../escape"), InvalidArg);
    CHECK_EQ(v.create_gallery("back\\slash"), InvalidArg);
    CHECK_EQ(v.create_gallery("trailing."), InvalidArg);
    CHECK_EQ(v.list("").size(), 0u);

    // Legitimate nesting is unaffected — '/' still separates segments.
    CHECK_EQ(v.create_gallery("a/b/c"), Ok);
    CHECK_EQ(v.list("").size(), 1u);
}

TEST(vault_add_image_marks_animated_gif)
{
    TempVault tv("add_img_anim_gif");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("hunter2"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    // Fixtures (32x32): anim.gif has 4 frames; still.gif has 1. Verified with
    // `ffprobe -count_frames`; the animated flag under test derives from that difference.
    const std::vector<uint8_t> anim = load_vault_gif_fixture("anim.gif");
    const std::vector<uint8_t> still = load_vault_gif_fixture("still.gif");
    REQUIRE(!anim.empty());  // fixture must be present
    REQUIRE(!still.empty());

    REQUIRE(v.add_image("", anim, "anim.gif") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", still, "still.gif") == vault::VaultResult::Ok);

    auto children = v.list("");
    REQUIRE(children.size() == 2);

    // Find the animated and still images by name
    const vault::IndexNode* anim_node = nullptr;
    const vault::IndexNode* still_node = nullptr;
    for (const auto* child : children) {
        if (child->name == "anim.gif") {
            anim_node = child;
        } else if (child->name == "still.gif") {
            still_node = child;
        }
    }
    REQUIRE(anim_node != nullptr);
    REQUIRE(still_node != nullptr);

    CHECK(anim_node->meta.animated);
    CHECK(!still_node->meta.animated);
}
