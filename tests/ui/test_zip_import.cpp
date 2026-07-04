#include "test_framework.h"
#include "ui/zip_import.h"
#include "ui/zip_test_helpers.h"
#include "vault/vault.h"
#include "crypto/secure_mem.h"

#include <span>
#include <vector>

// These tests verify the import *plumbing* (planner routing, miniz extraction
// into mlock'd memory, add_image storage, byte-identical readback) without
// coupling to a real codec. Shared fixtures (fake_jpeg, make_archive, vault +
// temp-dir helpers) live in zip_test_helpers.h.
namespace fs = std::filesystem;
using ui::ZipDest;
using ui::ZipConflictPolicy;
using ziptest::cleanup_dir;
using ziptest::fake_jpeg;
using ziptest::fresh_dir;
using ziptest::make_archive;
using ziptest::make_vault;

TEST(zip_import_new_gallery_mirrors_tree)
{
    auto img = fake_jpeg(1);
    auto dir = fresh_dir("osv_zip_test_new");
    auto zip = make_archive({{"2020/winter/a.jpg", img}, {"2020/sub/b.jpg", img}, {"notes.txt", {1, 2, 3}}},
                        dir / "in.zip");

    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        auto out = ui::import_zip(v, zip, ZipDest::NewGallery, "", "Album", ZipConflictPolicy::AskUser);
        CHECK(out.ok);
        CHECK_FALSE(out.needs_resolution);
        CHECK_EQ(out.imported, 2);
        CHECK_EQ(out.skipped, 1);  // notes.txt
        CHECK_EQ(v.list("Album/2020/winter").size(), static_cast<size_t>(1));  // a.jpg
        CHECK_EQ(v.list("Album/2020/sub").size(), static_cast<size_t>(1));  // b.jpg

        // Byte-identical readback (spec: per-file checksum match). Find the imported
        // a.jpg node and compare its decrypted original bytes to the payload.
        const vault::IndexNode* node = nullptr;
        for (const auto* c : v.list("Album/2020/winter"))
            if (c->name == "a.jpg") node = c;
        REQUIRE(node != nullptr);
        crypto::SecureBytes orig;
        REQUIRE(v.read_image(*node, orig) == vault::VaultResult::Ok);
        CHECK_BYTES_EQ((std::span<const uint8_t>{orig.data(), orig.size()}),
                       (std::span<const uint8_t>{img.data(), img.size()}));
    }
    cleanup_dir(dir);
}

TEST(zip_import_append_flattens)
{
    auto img = fake_jpeg(2);
    auto dir = fresh_dir("osv_zip_test_app");
    auto zip = make_archive({{"x/a.jpg", img}, {"x/y/b.jpg", img}}, dir / "in.zip");

    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        (void)v.create_gallery("Leaf");
        auto out = ui::import_zip(v, zip, ZipDest::Append, "Leaf", "", ZipConflictPolicy::AskUser);
        CHECK(out.ok);
        CHECK_EQ(out.imported, 2);
        CHECK_EQ(v.list("Leaf").size(), static_cast<size_t>(2));  // both flattened in
    }
    cleanup_dir(dir);
}

TEST(zip_import_reports_mixed_folder_without_writing)
{
    auto img = fake_jpeg(3);
    auto dir = fresh_dir("osv_zip_test_mix");
    auto zip = make_archive({{"a/x.jpg", img}, {"a/b/y.jpg", img}}, dir / "in.zip");

    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        auto out = ui::import_zip(v, zip, ZipDest::NewGallery, "", "G", ZipConflictPolicy::AskUser);
        CHECK(out.ok);
        CHECK(out.needs_resolution);
        CHECK_EQ(out.imported, 0);
        CHECK(v.list("G").empty());  // nothing written while awaiting resolution
    }
    cleanup_dir(dir);
}

TEST(zip_import_rejects_malformed_archive)
{
    auto dir = fresh_dir("osv_zip_test_bad");
    std::ofstream(dir / "bad.zip", std::ios::binary) << "not a zip at all";

    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        auto out = ui::import_zip(v, dir / "bad.zip", ZipDest::NewGallery, "", "G", ZipConflictPolicy::AskUser);
        CHECK_FALSE(out.ok);
        CHECK_FALSE(out.error.empty());
    }
    cleanup_dir(dir);
}

TEST(zip_import_writes_no_extra_files)
{
    auto img = fake_jpeg(4);
    auto dir = fresh_dir("osv_zip_test_nofs");
    auto zip = make_archive({{"a.jpg", img}}, dir / "in.zip");

    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        auto out = ui::import_zip(v, zip, ZipDest::NewGallery, "", "G", ZipConflictPolicy::AskUser);
        CHECK(out.ok);
        // The only files in `dir` are the input zip and the vault — no decompressed temp.
        int count = 0;
        for (auto& e : fs::directory_iterator(dir)) {
            (void)e;
            ++count;
        }
        CHECK_EQ(count, 2);
    }
    cleanup_dir(dir);
}

TEST(zip_import_meta_json_renames_top_gallery_and_tags)
{
    auto img = fake_jpeg(5);
    auto dir = fresh_dir("osv_zip_test_meta");
    const std::string meta = R"({
        "title": { "english": "Renamed Album" },
        "tags":  [ { "type": "artist", "name": "someone" } ]
    })";
    auto zip = make_archive({{"sub/a.jpg", img},
                             {"meta.json", {meta.begin(), meta.end()}}},
                            dir / "in.zip");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        auto out = ui::import_zip(v, zip, ZipDest::NewGallery, "", "Album", ZipConflictPolicy::AskUser);
        CHECK(out.ok);
        CHECK_EQ(out.imported, 1);
        CHECK_EQ(out.skipped, 0);            // meta.json consumed, not "skipped"
        CHECK(v.list("Album").empty());      // default name overridden by the title
        CHECK_EQ(v.list("Renamed Album/sub").size(), static_cast<size_t>(1));

        const vault::IndexNode* g = nullptr;
        for (const auto* n : v.list(""))
            if (n->name == "Renamed Album") g = n;
        REQUIRE(g != nullptr);
        REQUIRE(g->tags.size() == static_cast<size_t>(1));
        CHECK_EQ(g->tags[0], std::string("artist:someone"));
    }
    cleanup_dir(dir);
}

TEST(zip_import_append_ignores_meta_json)
{
    // Append has no "top gallery" to retitle/tag: the meta.json is simply not
    // imported as a file and the existing target gallery is left untouched.
    auto img = fake_jpeg(6);
    auto dir = fresh_dir("osv_zip_test_meta_app");
    const std::string meta = R"({ "title": { "english": "Ignored" },
                                  "tags": [ { "type": "tag", "name": "x" } ] })";
    auto zip = make_archive({{"a.jpg", img},
                             {"meta.json", {meta.begin(), meta.end()}}},
                            dir / "in.zip");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        (void)v.create_gallery("Leaf");
        auto out = ui::import_zip(v, zip, ZipDest::Append, "Leaf", "", ZipConflictPolicy::AskUser);
        CHECK(out.ok);
        CHECK_EQ(out.imported, 1);
        CHECK_EQ(out.skipped, 0);
        CHECK_EQ(v.list("Leaf").size(), static_cast<size_t>(1));
        CHECK(v.list("Ignored").empty());

        const vault::IndexNode* g = nullptr;
        for (const auto* n : v.list(""))
            if (n->name == "Leaf") g = n;
        REQUIRE(g != nullptr);
        CHECK(g->tags.empty());
    }
    cleanup_dir(dir);
}
