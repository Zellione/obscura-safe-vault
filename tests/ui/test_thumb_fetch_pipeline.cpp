#include "test_framework.h"

#include "image/decode_worker.h"
#include "image/fixtures.h"
#include "ui/tile_thumb.h"
#include "ui/zip_test_helpers.h"
#include "vault/vault.h"

using ziptest::cleanup_dir;
using ziptest::fresh_dir;
using ziptest::make_vault;

// ThumbKey carries both the cache identity and the span to read (via ChunkRef).
TEST(thumb_key_carries_read_span_image)
{
    vault::IndexNode img = vault::IndexNode::image("a.png");
    img.meta.data_offset  = 1000;
    img.meta.thumb_offset = 4096;
    img.meta.thumb_length = 512;
    const ui::ThumbKey k = ui::thumb_key_for(img);
    CHECK_EQ(k.key, 1000u);
    CHECK_EQ(k.ref.offset, 4096u);
    CHECK_EQ(k.ref.length, 512u);
    CHECK(k.present);
}

// Video thumbnails come from poster fields, not thumb fields.
TEST(thumb_key_carries_read_span_video)
{
    vault::IndexNode vid = vault::IndexNode::video("clip.mp4");
    vid.vmeta.poster_offset = 500;
    vid.vmeta.poster_length = 40;
    const ui::ThumbKey k = ui::thumb_key_for(vid);
    CHECK_EQ(k.key, 500u);
    CHECK_EQ(k.ref.offset, 500u);
    CHECK_EQ(k.ref.length, 40u);
    CHECK(k.present);
}

// Worker can fetch and decode a real vault thumbnail.
TEST(worker_fetches_and_decodes_a_real_vault_thumbnail)
{
    const auto dir = fresh_dir("thumb_fetch_worker");
    vault::Vault v;
    make_vault(v, dir / "a.osv");
    REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);

    // Add an image with a real thumbnail.
    const auto real_jpeg = fixtures::solid_jpeg(8, 8, 0x11, 0x22, 0x33);
    REQUIRE(v.add_image("g", real_jpeg, "img.jpg") == vault::VaultResult::Ok);

    // Get the node (add_image invalidates pointers).
    auto nodes = v.list("g");
    REQUIRE(nodes.size() >= 1);
    const auto* node = nodes.at(0);
    REQUIRE(node->meta.thumb_length > 0);

    const ui::ThumbKey k = ui::thumb_key_for(*node);
    REQUIRE(k.present);
    REQUIRE(k.ref.offset == node->meta.thumb_offset);
    REQUIRE(k.ref.length == node->meta.thumb_length);

    // Submit a fetch to the worker.
    image::DecodeWorker w(0);
    w.submit_fetch(k.key, [&v, ref = k.ref](crypto::SecureBytes& out) {
        return vault::read_thumb_span(v, ref, out) == vault::VaultResult::Ok;
    });

    // Poll for the result (bounded wait).
    std::optional<image::DecodeWorker::Result> result;
    for (int i = 0; i < 100; ++i) {
        result = w.take_result();
        if (result) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    REQUIRE(result.has_value());
    CHECK_EQ(result->key, k.key);
    REQUIRE(result->image.has_value());
    CHECK(result->image->width > 0);
    CHECK(result->image->height > 0);

    cleanup_dir(dir);
}

// Locked vault fetch lands as failure (nullopt image).
TEST(locked_vault_fetch_lands_as_failure)
{
    const auto dir = fresh_dir("thumb_fetch_locked");
    vault::Vault v;
    make_vault(v, dir / "a.osv");
    REQUIRE(v.create_gallery("g") == vault::VaultResult::Ok);

    // Add an image with a real thumbnail.
    const auto real_jpeg = fixtures::solid_jpeg(8, 8, 0x11, 0x22, 0x33);
    REQUIRE(v.add_image("g", real_jpeg, "img.jpg") == vault::VaultResult::Ok);

    auto nodes = v.list("g");
    REQUIRE(nodes.size() >= 1);
    const auto* node = nodes.at(0);
    REQUIRE(node->meta.thumb_length > 0);

    const ui::ThumbKey k = ui::thumb_key_for(*node);
    REQUIRE(k.present);

    // Lock the vault before fetching.
    v.lock();

    // Submit a fetch that will fail because the vault is locked.
    image::DecodeWorker w(0);
    w.submit_fetch(k.key, [&v, ref = k.ref](crypto::SecureBytes& out) {
        return vault::read_thumb_span(v, ref, out) == vault::VaultResult::Ok;
    });

    // Poll for the result.
    std::optional<image::DecodeWorker::Result> result;
    for (int i = 0; i < 100; ++i) {
        result = w.take_result();
        if (result) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    REQUIRE(result.has_value());
    CHECK_EQ(result->key, k.key);
    CHECK(!result->image.has_value());   // Fetch failed, host memoizes into `failed`

    cleanup_dir(dir);
}
