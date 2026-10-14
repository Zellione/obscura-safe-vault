#include "test_framework.h"

#include "gfx/renderer.h"
#include "gfx/texture_cache.h"
#include "gfx/window.h"
#include "image/fixtures.h"
#include "image/image.h"
#include "ui/zip_test_helpers.h"

#include <chrono>
#include <thread>
#include "platform/file_dialog.h"
#include "platform/folder_dialog.h"
#include "platform/vault_registry.h"
#include "ui/favorites_galleries.h"
#include "ui/favorites_images.h"
#include "ui/import_queue.h"

#include "ui/tile_thumb.h"
#include "vault/index.h"

using ui::thumb_key_for;
using vault::IndexNode;

TEST(thumb_key_image_with_thumbnail_uses_thumb_offset)
{
    IndexNode n = IndexNode::image("a.jpg");
    n.meta.data_offset  = 111;
    n.meta.thumb_offset = 222;
    n.meta.thumb_length = 20;
    const auto k = thumb_key_for(n);
    CHECK_EQ(k.key, static_cast<uint64_t>(222));
    CHECK_EQ(k.ref.offset, static_cast<uint64_t>(222));
    CHECK_EQ(k.ref.length, static_cast<uint64_t>(20));
    CHECK(k.present);
}

TEST(thumb_key_image_without_thumbnail_is_absent)
{
    IndexNode n = IndexNode::image("a.jpg");
    n.meta.data_offset  = 111;
    n.meta.thumb_length = 0;
    CHECK_FALSE(thumb_key_for(n).present);
}

TEST(thumb_key_video_uses_poster_offset_and_length)
{
    IndexNode n = IndexNode::video("clip.mp4");
    n.vmeta.poster_offset = 500;
    n.vmeta.poster_length = 40;
    const auto k = thumb_key_for(n);
    CHECK_EQ(k.key, static_cast<uint64_t>(500));
    CHECK_EQ(k.ref.offset, static_cast<uint64_t>(500));
    CHECK_EQ(k.ref.length, static_cast<uint64_t>(40));
    CHECK(k.present);
}

TEST(thumb_key_video_regression_previously_always_reported_absent)
{
    // Before the fix, both call sites gated on node.meta.thumb_length alone,
    // which is always 0 for a video node — every video silently reported "no
    // thumbnail" no matter what its poster held. Prove the old field really
    // is 0 here, and that thumb_key_for still reports present=true anyway.
    IndexNode n = IndexNode::video("clip.mp4");
    n.vmeta.poster_offset = 500;
    n.vmeta.poster_length = 40;
    CHECK_EQ(n.meta.thumb_length, static_cast<uint64_t>(0));
    CHECK(thumb_key_for(n).present);
}

TEST(thumb_key_video_without_poster_is_absent)
{
    IndexNode n = IndexNode::video("clip.mp4");
    n.vmeta.poster_offset = 0;
    n.vmeta.poster_length = 0;
    CHECK_FALSE(thumb_key_for(n).present);
}

TEST(tile_badge_shown_for_an_animated_gif)
{
    vault::IndexNode n = vault::IndexNode::image("loop.gif");
    n.meta.format   = vault::ImageFormat::GIF;
    n.meta.animated = true;
    CHECK(ui::tile_shows_animated_badge(n));
}

TEST(tile_badge_hidden_for_a_still_gif)
{
    vault::IndexNode n = vault::IndexNode::image("still.gif");
    n.meta.format   = vault::ImageFormat::GIF;
    n.meta.animated = false;
    CHECK(!ui::tile_shows_animated_badge(n));
}

TEST(tile_badge_hidden_for_a_jpeg)
{
    vault::IndexNode n = vault::IndexNode::image("photo.jpg");
    n.meta.format = vault::ImageFormat::JPEG;
    CHECK(!ui::tile_shows_animated_badge(n));
}

TEST(tile_badge_hidden_for_a_gallery)
{
    const vault::IndexNode n = vault::IndexNode::gallery("album");
    CHECK(!ui::tile_shows_animated_badge(n));
}

TEST(tile_badge_hidden_for_a_video)
{
    const vault::IndexNode n = vault::IndexNode::video("clip.mp4");
    CHECK(!ui::tile_shows_animated_badge(n));
}

TEST(tile_can_hover_animate_true_for_animated_gif_within_budget)
{
    vault::IndexNode n = vault::IndexNode::image("loop.gif");
    n.meta.format   = vault::ImageFormat::GIF;
    n.meta.animated = true;
    n.meta.width    = 320;
    n.meta.height   = 240;
    CHECK(ui::tile_can_hover_animate(n));
}

TEST(tile_can_hover_animate_false_for_animated_gif_exceeding_width)
{
    vault::IndexNode n = vault::IndexNode::image("big.gif");
    n.meta.format   = vault::ImageFormat::GIF;
    n.meta.animated = true;
    n.meta.width    = 4000;
    n.meta.height   = 240;
    CHECK(!ui::tile_can_hover_animate(n));
}

TEST(tile_can_hover_animate_false_for_animated_gif_exceeding_height)
{
    vault::IndexNode n = vault::IndexNode::image("tall.gif");
    n.meta.format   = vault::ImageFormat::GIF;
    n.meta.animated = true;
    n.meta.width    = 320;
    n.meta.height   = 5000;
    CHECK(!ui::tile_can_hover_animate(n));
}

TEST(tile_can_hover_animate_false_for_zero_width_gif)
{
    vault::IndexNode n = vault::IndexNode::image("bad.gif");
    n.meta.format   = vault::ImageFormat::GIF;
    n.meta.animated = true;
    n.meta.width    = 0;
    n.meta.height   = 240;
    CHECK(!ui::tile_can_hover_animate(n));
}

TEST(tile_can_hover_animate_false_for_zero_height_gif)
{
    vault::IndexNode n = vault::IndexNode::image("bad.gif");
    n.meta.format   = vault::ImageFormat::GIF;
    n.meta.animated = true;
    n.meta.width    = 320;
    n.meta.height   = 0;
    CHECK(!ui::tile_can_hover_animate(n));
}

TEST(tile_can_hover_animate_false_for_still_gif)
{
    vault::IndexNode n = vault::IndexNode::image("still.gif");
    n.meta.format   = vault::ImageFormat::GIF;
    n.meta.animated = false;
    n.meta.width    = 320;
    n.meta.height   = 240;
    CHECK(!ui::tile_can_hover_animate(n));
}

TEST(tile_can_hover_animate_false_for_jpeg)
{
    vault::IndexNode n = vault::IndexNode::image("photo.jpg");
    n.meta.format = vault::ImageFormat::JPEG;
    n.meta.width  = 320;
    n.meta.height = 240;
    CHECK(!ui::tile_can_hover_animate(n));
}

TEST(tile_can_hover_animate_false_for_gallery)
{
    vault::IndexNode n = vault::IndexNode::gallery("album");
    CHECK(!ui::tile_can_hover_animate(n));
}

TEST(tile_can_hover_animate_false_for_video)
{
    vault::IndexNode n = vault::IndexNode::video("clip.mp4");
    CHECK(!ui::tile_can_hover_animate(n));
}

// --- Animated WebP (Phase 57) -------------------------------------------------
// The badge and the hover gate defer to vault::format_can_animate, so WebP is
// treated exactly like GIF and a format that cannot animate never badges.

TEST(tile_badge_shown_for_an_animated_webp)
{
    vault::IndexNode n = vault::IndexNode::image("loop.webp");
    n.meta.format   = vault::ImageFormat::WebP;
    n.meta.animated = true;
    CHECK(ui::tile_shows_animated_badge(n));
}

TEST(tile_badge_hidden_for_a_still_webp)
{
    vault::IndexNode n = vault::IndexNode::image("still.webp");
    n.meta.format   = vault::ImageFormat::WebP;
    n.meta.animated = false;
    CHECK(!ui::tile_shows_animated_badge(n));
}

TEST(tile_badge_hidden_for_a_flagged_non_animatable_format)
{
    // Defence in depth: a stale or hostile flag on a format that cannot animate
    // must not produce a badge the viewer can never honour.
    vault::IndexNode n = vault::IndexNode::image("photo.heic");
    n.meta.format   = vault::ImageFormat::HEIC;
    n.meta.animated = true;
    CHECK(!ui::tile_shows_animated_badge(n));
}

TEST(tile_can_hover_animate_true_for_animated_webp_within_budget)
{
    vault::IndexNode n = vault::IndexNode::image("loop.webp");
    n.meta.format   = vault::ImageFormat::WebP;
    n.meta.animated = true;
    n.meta.width    = 320;
    n.meta.height   = 240;
    CHECK(ui::tile_can_hover_animate(n));
}

TEST(tile_can_hover_animate_false_for_animated_webp_exceeding_budget)
{
    vault::IndexNode n = vault::IndexNode::image("big.webp");
    n.meta.format   = vault::ImageFormat::WebP;
    n.meta.animated = true;
    n.meta.width    = 4000;
    n.meta.height   = 240;
    CHECK(!ui::tile_can_hover_animate(n));
}

// Exercise the screen draw hooks with preloaded textures: no display or vault
// I/O, and a missing poster/cover cannot hide behind a warm gallery listing.

namespace {
template <class Base> struct FavoriteDraw : Base {
    using Base::Base;
    using Base::draw_tile_content;
};

struct FavoriteRenderFixture {
    int& _failures;
    explicit FavoriteRenderFixture(int& failures) : _failures(failures) {}
    SDL_Surface* surface = SDL_CreateSurface(100, 100, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
    gfx::Window window;
    gfx::FontAtlas font;
    vault::Vault vault;
    platform::VaultRegistry registry;
    platform::FileDialog file;
    platform::FolderDialog folder;
    ui::ImportQueue queue;
    gfx::TextureCache cache{renderer};
    ui::FavoritesScreen::CollectionOps ops{file, folder, queue, nullptr, ""};
    ~FavoriteRenderFixture()
    {
        cache.clear();
        SDL_DestroyRenderer(renderer);
        SDL_DestroySurface(surface);
    }
    void seed(uint64_t key)
    {
        image::ImageData pixels;
        pixels.width = pixels.height = 1;
        REQUIRE(pixels.pixels.fill(3, 255));
        REQUIRE(cache.get_or_upload(key, pixels) != nullptr);
    }
    template <class Screen> void check_draw(const IndexNode& node)
    {
        Screen screen(window, font, vault, cache, registry, ops);
        check_draw(screen, node);
    }
    template <class Screen> void check_draw(Screen& screen, const IndexNode& node)
    {
        gfx::Renderer drawing(renderer);
        screen.draw_tile_content(drawing,
                                 {.path = "",
                                  .is_gallery = node.is_gallery(),
                                  .name = "",
                                  .effective_tags = {},
                                  .node = &node},
                                 {0, 0, 100, 100});
        SDL_RenderPresent(renderer);
        Uint8 red{}, green{}, blue{}, alpha{};
        REQUIRE(SDL_ReadSurfacePixel(surface, 40, 55, &red, &green, &blue, &alpha));
        CHECK_EQ(red, 255);
        CHECK_EQ(green, 255);
        CHECK_EQ(blue, 255);
    }
};
}  // namespace

TEST(favorite_image_renders_cached_thumbnail)
{
    FavoriteRenderFixture f(_failures);
    f.seed(123);
    auto node = IndexNode::image("photo");
    node.meta.thumb_offset = 123;
    node.meta.thumb_length = 10;
    f.check_draw<FavoriteDraw<ui::FavoritesImages>>(node);
}

TEST(favorite_video_renders_cached_poster)
{
    FavoriteRenderFixture f(_failures);
    f.seed(123);
    auto node = IndexNode::video("clip");
    node.vmeta.poster_offset = 123;
    node.vmeta.poster_length = 10;
    f.check_draw<FavoriteDraw<ui::FavoritesImages>>(node);
}

TEST(favorite_gallery_renders_descendant_poster_cover)
{
    FavoriteRenderFixture f(_failures);
    f.seed(123);
    auto node = IndexNode::gallery("album");
    auto nested = IndexNode::gallery("nested");
    auto video = IndexNode::video("clip");
    video.vmeta.poster_offset = 123;
    video.vmeta.poster_length = 10;
    nested.children.push_back(std::move(video));
    node.children.push_back(std::move(nested));
    f.check_draw<FavoriteDraw<ui::FavoritesGalleries>>(node);
}

TEST(favorite_gallery_refresh_discards_cached_empty_cover)
{
    FavoriteRenderFixture f(_failures);
    f.seed(123);
    FavoriteDraw<ui::FavoritesGalleries> screen(f.window, f.font, f.vault, f.cache, f.registry,
                                                f.ops);
    auto node = IndexNode::gallery("album");
    gfx::Renderer drawing(f.renderer);
    screen.draw_tile_content(
        drawing, {.path = "", .is_gallery = true, .name = "", .effective_tags = {}, .node = &node},
        {0, 0, 100, 100});
    auto photo = IndexNode::image("new photo");
    photo.meta.thumb_offset = 123;
    photo.meta.thumb_length = 10;
    node.children.push_back(std::move(photo));
    screen.on_vault_changed();
    f.check_draw(screen, node);
}

TEST(favorite_gallery_fetches_and_uploads_cover_from_directory_vault)
{
    const auto dir = ziptest::fresh_dir("favorite_cover_fetch");
    {
        FavoriteRenderFixture f(_failures);
        const std::vector<uint8_t> password{'p', 'w'};
        REQUIRE(vault::Vault::create_directory((dir / "vault.osv").string(), password, {},
                                               ziptest::kTestKdf,
                                               f.vault) == vault::VaultResult::Ok);
        REQUIRE(f.vault.create_gallery("album") == vault::VaultResult::Ok);
        REQUIRE(f.vault.add_image("album", fixtures::solid_jpeg(8, 8, 255, 255, 255),
                                  "photo.jpg") == vault::VaultResult::Ok);
        const auto galleries = f.vault.list("");
        const auto photos = f.vault.list("album");
        REQUIRE(galleries.size() == 1);
        REQUIRE(photos.size() == 1);
        const auto key = thumb_key_for(*photos.front());
        REQUIRE(key.present);
        FavoriteDraw<ui::FavoritesGalleries> screen(f.window, f.font, f.vault, f.cache, f.registry,
                                                    f.ops);
        gfx::Renderer drawing(f.renderer);
        screen.draw_tile_content(drawing,
                                 {.path = "album",
                                  .is_gallery = true,
                                  .name = "album",
                                  .effective_tags = {},
                                  .node = galleries.front()},
                                 {0, 0, 100, 100});
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!f.cache.contains(key.key) && std::chrono::steady_clock::now() < deadline) {
            screen.update(0.01);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(f.cache.contains(key.key));
        f.check_draw(screen, *galleries.front());
    }
    ziptest::cleanup_dir(dir);
}
