#include <gtest/gtest.h>
#include <memory>
#include <QTest>

#include "../tag_controller.h"
#include "test_vault_util.h"
#include "vault/vault.h"
#include "vault/index.h"

class TagControllerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temporary test vault
        std::string path = "/tmp/test_tag_controller.osv";
        vault_ = std::make_unique<vault::Vault>(osvqt_test::createTestVault(path));
        controller_ = std::make_unique<TagController>();
        controller_->setVault(vault_.get());
    }

    std::unique_ptr<vault::Vault> vault_;
    std::unique_ptr<TagController> controller_;
};

// Test: add and remove tags persist
TEST_F(TagControllerTest, AddRemoveTagsPersist) {
    ASSERT_TRUE(vault_->is_unlocked());

    // Create a gallery at root
    auto result = vault_->create_gallery("/TestGallery");
    ASSERT_EQ(result, vault::VaultResult::Ok);

    // Add a tag
    bool added = controller_->addTag("/TestGallery", "artist:Kaguya");
    EXPECT_TRUE(added);

    // Verify it was added
    auto tags = controller_->getOwnTags("/TestGallery");
    EXPECT_EQ(tags.size(), 1);
    EXPECT_EQ(tags[0].toStdString(), "artist:Kaguya");

    // Remove the tag
    bool removed = controller_->removeTag("/TestGallery", "artist:Kaguya");
    EXPECT_TRUE(removed);

    // Verify it was removed
    tags = controller_->getOwnTags("/TestGallery");
    EXPECT_EQ(tags.size(), 0);
}

// Test: tag display transform (category prefix stripping)
TEST_F(TagControllerTest, TagDisplayTransform) {
    // Setup a tag category
    vault::VaultSettings settings = vault_settings(*vault_);
    vault::TagCategory cat;
    cat.name = "artist";
    cat.swatch = 0;
    settings.categories = {cat};
    auto res = set_vault_settings(*vault_, settings);
    ASSERT_EQ(res, vault::VaultResult::Ok);

    // Test configured prefix is stripped
    QString display = controller_->getTagDisplayText("artist:Kaguya");
    EXPECT_EQ(display, "Kaguya");

    // Test unconfigured prefix is kept verbatim
    display = controller_->getTagDisplayText("12:30");
    EXPECT_EQ(display, "12:30");

    display = controller_->getTagDisplayText("Re:Zero");
    EXPECT_EQ(display, "Re:Zero");

    // Test tag without colon is unchanged
    display = controller_->getTagDisplayText("simple");
    EXPECT_EQ(display, "simple");
}

// Test: swatch index for categorized vs uncategorized tags
TEST_F(TagControllerTest, TagSwatchIndex) {
    // Setup a tag category
    vault::VaultSettings settings = vault_settings(*vault_);
    vault::TagCategory cat;
    cat.name = "artist";
    cat.swatch = 2;
    settings.categories = {cat};
    auto res = set_vault_settings(*vault_, settings);
    ASSERT_EQ(res, vault::VaultResult::Ok);

    // Categorized tag should have swatch = 2
    int swatch = controller_->getTagSwatchIndex("artist:Kaguya");
    EXPECT_EQ(swatch, 2);

    // Uncategorized tag should have swatch = -1 (text dim)
    swatch = controller_->getTagSwatchIndex("12:30");
    EXPECT_EQ(swatch, -1);
}

// Test: suggestions ranking
TEST_F(TagControllerTest, SuggestionsRanking) {
    // Create two galleries
    auto result = vault_->create_gallery("/Gallery1");
    ASSERT_EQ(result, vault::VaultResult::Ok);

    result = vault_->create_gallery("/Gallery2");
    ASSERT_EQ(result, vault::VaultResult::Ok);

    // Add tags to Gallery1 to build vocabulary
    EXPECT_EQ(vault_->add_tag("/Gallery1", "artist:Kaguya"), vault::VaultResult::Ok);
    EXPECT_EQ(vault_->add_tag("/Gallery1", "artist:Reimu"), vault::VaultResult::Ok);
    EXPECT_EQ(vault_->add_tag("/Gallery1", "series:Touhou"), vault::VaultResult::Ok);

    // Test prefix matching comes before substring matching
    // Gallery2 has no tags, so all tags from the vocabulary can be suggested
    auto suggestions = controller_->getSuggestions("ar", "/Gallery2");
    EXPECT_GT(suggestions.size(), 0);

    // The "artist:*" tags should be in the suggestions
    // (though we don't test the exact order - that's tested in the SDL unit tests)
}

// Test: get own, inherited, and contents tags
TEST_F(TagControllerTest, TagInheritance) {
    // Create parent gallery with a tag
    auto result = vault_->create_gallery("/Parent");
    ASSERT_EQ(result, vault::VaultResult::Ok);
    EXPECT_EQ(vault_->add_tag("/Parent", "parent:tag"), vault::VaultResult::Ok);

    // Create child gallery
    result = vault_->create_gallery("/Parent/Child");
    ASSERT_EQ(result, vault::VaultResult::Ok);
    EXPECT_EQ(vault_->add_tag("/Parent/Child", "child:tag"), vault::VaultResult::Ok);

    // Child should have own tags
    auto own = controller_->getOwnTags("/Parent/Child");
    EXPECT_EQ(own.size(), 1);
    EXPECT_EQ(own[0].toStdString(), "child:tag");

    // Child should have inherited tags from parent
    auto inherited = controller_->getInheritedTags("/Parent/Child");
    EXPECT_EQ(inherited.size(), 1);
    EXPECT_EQ(inherited[0].toStdString(), "parent:tag");
}

// Test: vocabulary retrieval
TEST_F(TagControllerTest, GetVocabulary) {
    // Create a gallery and add tags
    auto result = vault_->create_gallery("/TestGallery");
    ASSERT_EQ(result, vault::VaultResult::Ok);
    EXPECT_EQ(vault_->add_tag("/TestGallery", "artist:Kaguya"), vault::VaultResult::Ok);
    EXPECT_EQ(vault_->add_tag("/TestGallery", "series:Touhou"), vault::VaultResult::Ok);

    auto vocab = controller_->getVocabulary();
    EXPECT_GE(vocab.size(), 2);
    EXPECT_TRUE(vocab.contains("artist:Kaguya"));
    EXPECT_TRUE(vocab.contains("series:Touhou"));
}
