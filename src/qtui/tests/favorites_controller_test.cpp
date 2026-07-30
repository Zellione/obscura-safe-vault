#include <gtest/gtest.h>
#include <memory>
#include <QTest>

#include "../favorites_controller.h"
#include "test_vault_util.h"
#include "vault/vault.h"

class FavoritesControllerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temporary test vault
        std::string path = "/tmp/test_favorites_controller.osv";
        vault_ = std::make_unique<vault::Vault>(osvqt_test::createTestVault(path));
        controller_ = std::make_unique<FavoritesController>();
        controller_->setVault(vault_.get());
    }

    std::unique_ptr<vault::Vault> vault_;
    std::unique_ptr<FavoritesController> controller_;
};

// Test: toggle favorite persists
TEST_F(FavoritesControllerTest, ToggleFavoritePersists) {
    ASSERT_TRUE(vault_->is_unlocked());

    // Create a gallery
    auto result = vault_->create_gallery("/TestGallery");
    ASSERT_EQ(result, vault::VaultResult::Ok);

    // Toggle favorite on (initially not favorite)
    bool toggled = controller_->toggleFavorite("/TestGallery");
    EXPECT_TRUE(toggled);

    // Verify it was toggled (by checking the listing)
    auto favGalleries = controller_->getFavoriteGalleries();
    EXPECT_EQ(favGalleries.size(), 1);
    EXPECT_EQ(favGalleries[0].path.toStdString(), "TestGallery");

    // Toggle favorite off
    toggled = controller_->toggleFavorite("/TestGallery");
    EXPECT_TRUE(toggled);

    // Verify it was removed from favorites
    favGalleries = controller_->getFavoriteGalleries();
    EXPECT_EQ(favGalleries.size(), 0);
}

// Test: getFavoriteImages returns matching listing
TEST_F(FavoritesControllerTest, GetFavoriteImagesMatching) {
    ASSERT_TRUE(vault_->is_unlocked());

    // Add an image to root
    std::vector<uint8_t> image_data(osvqt_test::tiny_jpeg, osvqt_test::tiny_jpeg + sizeof(osvqt_test::tiny_jpeg));
    const std::span<const uint8_t> img_span(image_data.data(), image_data.size());
    auto result = vault_->add_image("", img_span, "TestImage");
    ASSERT_EQ(result, vault::VaultResult::Ok);

    // Toggle it as favorite
    bool toggled = controller_->toggleFavorite("/TestImage");
    EXPECT_TRUE(toggled);

    // Get favorites listing
    auto favImages = controller_->getFavoriteImages();
    EXPECT_EQ(favImages.size(), 1);
    EXPECT_EQ(favImages[0].path.toStdString(), "TestImage");
    EXPECT_FALSE(favImages[0].is_gallery);
}

// Test: getFavoriteGalleries returns matching listing
TEST_F(FavoritesControllerTest, GetFavoriteGalleriesMatching) {
    ASSERT_TRUE(vault_->is_unlocked());

    // Create two galleries
    auto result = vault_->create_gallery("/Gallery1");
    ASSERT_EQ(result, vault::VaultResult::Ok);
    result = vault_->create_gallery("/Gallery2");
    ASSERT_EQ(result, vault::VaultResult::Ok);

    // Favorite only Gallery1
    bool toggled = controller_->toggleFavorite("/Gallery1");
    EXPECT_TRUE(toggled);

    // Get favorites listing
    auto favGalleries = controller_->getFavoriteGalleries();
    EXPECT_EQ(favGalleries.size(), 1);
    EXPECT_EQ(favGalleries[0].path.toStdString(), "Gallery1");
    EXPECT_TRUE(favGalleries[0].is_gallery);
}

// Test: isFavorite reflects actual state
TEST_F(FavoritesControllerTest, IsFavoriteReflectsState) {
    ASSERT_TRUE(vault_->is_unlocked());

    // Create a gallery
    auto result = vault_->create_gallery("/TestGallery");
    ASSERT_EQ(result, vault::VaultResult::Ok);

    // Initially not favorite
    bool fav = controller_->isFavorite("/TestGallery");
    EXPECT_FALSE(fav);

    // Toggle it
    controller_->toggleFavorite("/TestGallery");
    fav = controller_->isFavorite("/TestGallery");
    EXPECT_TRUE(fav);
}
