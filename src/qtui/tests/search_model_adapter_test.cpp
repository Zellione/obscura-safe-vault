#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <QString>

#include "../search_model_adapter.h"
#include "test_vault_util.h"
#include "vault/vault.h"

class SearchModelAdapterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // temp_directory_path(), not a hardcoded "/tmp/...": Windows has no /tmp.
        std::string path =
            (std::filesystem::temp_directory_path() / "test_search_model_adapter.osv").string();
        vault_ = std::make_unique<vault::Vault>(osvqt_test::createTestVault(path));
        adapter_ = std::make_unique<SearchModelAdapter>();
        adapter_->setVault(vault_.get());
    }

    std::unique_ptr<vault::Vault> vault_;
    std::unique_ptr<SearchModelAdapter> adapter_;
};

// Test: Query filtering preserves ranking order
TEST_F(SearchModelAdapterTest, QueryFilteringRanksResults) {
    ASSERT_TRUE(vault_->is_unlocked());

    // Add test images with distinctive names
    std::vector<uint8_t> image_data(osvqt_test::tiny_jpeg, osvqt_test::tiny_jpeg + sizeof(osvqt_test::tiny_jpeg));
    const std::span<const uint8_t> img_span(image_data.data(), image_data.size());

    // Add images: "photo", "photograph", "image"
    vault_->add_image("", img_span, "photo");
    vault_->add_image("", img_span, "photograph");
    vault_->add_image("", img_span, "image");

    // Query "photo" should rank "photo" highest, then "photograph"
    auto results = adapter_->search("photo", static_cast<int>(vault::SearchScope::Both));
    EXPECT_EQ(results.size(), 2) << "Should find 'photo' and 'photograph'";

    // "photo" exact match should be first
    if (results.size() >= 1) {
        EXPECT_EQ(results[0].name, "photo") << "Exact name match should rank first";
    }
    if (results.size() >= 2) {
        EXPECT_EQ(results[1].name, "photograph") << "Partial match should rank second";
    }
}

// Test: Scope filtering works (Images only)
TEST_F(SearchModelAdapterTest, ScopeFilteringImages) {
    ASSERT_TRUE(vault_->is_unlocked());

    // Add gallery and image
    auto res = vault_->create_gallery("/TestGallery");
    ASSERT_EQ(res, vault::VaultResult::Ok);

    std::vector<uint8_t> image_data(osvqt_test::tiny_jpeg, osvqt_test::tiny_jpeg + sizeof(osvqt_test::tiny_jpeg));
    const std::span<const uint8_t> img_span(image_data.data(), image_data.size());
    res = vault_->add_image("", img_span, "TestImage");
    ASSERT_EQ(res, vault::VaultResult::Ok);

    // Query with Images scope should only return images
    auto results = adapter_->search("Test", static_cast<int>(vault::SearchScope::Images));
    EXPECT_EQ(results.size(), 1);
    if (results.size() >= 1) {
        EXPECT_FALSE(results[0].is_gallery);
    }
}

// Test: Scope filtering works (Galleries only)
TEST_F(SearchModelAdapterTest, ScopeFilteringGalleries) {
    ASSERT_TRUE(vault_->is_unlocked());

    // Add gallery and image
    auto res = vault_->create_gallery("/TestGallery");
    ASSERT_EQ(res, vault::VaultResult::Ok);

    std::vector<uint8_t> image_data(osvqt_test::tiny_jpeg, osvqt_test::tiny_jpeg + sizeof(osvqt_test::tiny_jpeg));
    const std::span<const uint8_t> img_span(image_data.data(), image_data.size());
    res = vault_->add_image("", img_span, "TestImage");
    ASSERT_EQ(res, vault::VaultResult::Ok);

    // Query with Galleries scope should only return galleries
    auto results = adapter_->search("Test", static_cast<int>(vault::SearchScope::Galleries));
    EXPECT_EQ(results.size(), 1);
    if (results.size() >= 1) {
        EXPECT_TRUE(results[0].is_gallery);
    }
}

// Test: Empty query returns all results (in current scope)
TEST_F(SearchModelAdapterTest, EmptyQueryReturnsAll) {
    ASSERT_TRUE(vault_->is_unlocked());

    auto res = vault_->create_gallery("/Gallery1");
    ASSERT_EQ(res, vault::VaultResult::Ok);
    res = vault_->create_gallery("/Gallery2");
    ASSERT_EQ(res, vault::VaultResult::Ok);

    std::vector<uint8_t> image_data(osvqt_test::tiny_jpeg, osvqt_test::tiny_jpeg + sizeof(osvqt_test::tiny_jpeg));
    const std::span<const uint8_t> img_span(image_data.data(), image_data.size());
    res = vault_->add_image("", img_span, "Image1");
    ASSERT_EQ(res, vault::VaultResult::Ok);

    // Empty query with Both scope should return everything
    auto results = adapter_->search("", static_cast<int>(vault::SearchScope::Both));
    EXPECT_EQ(results.size(), 3) << "Empty query should return all items";
}

// Test: Case-insensitive matching
TEST_F(SearchModelAdapterTest, CaseInsensitiveMatching) {
    ASSERT_TRUE(vault_->is_unlocked());

    auto res = vault_->create_gallery("/TestGallery");
    ASSERT_EQ(res, vault::VaultResult::Ok);

    // Query in different case
    auto results = adapter_->search("test", static_cast<int>(vault::SearchScope::Galleries));
    EXPECT_EQ(results.size(), 1);

    results = adapter_->search("TEST", static_cast<int>(vault::SearchScope::Galleries));
    EXPECT_EQ(results.size(), 1);

    results = adapter_->search("TeSt", static_cast<int>(vault::SearchScope::Galleries));
    EXPECT_EQ(results.size(), 1);
}
