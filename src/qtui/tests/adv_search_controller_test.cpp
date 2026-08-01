#include <gtest/gtest.h>
#include <QGuiApplication>
#include <QString>
#include <filesystem>
#include <cstdlib>

#include "../adv_search_controller.h"
#include "test_vault_util.h"

class AdvancedSearchControllerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Create temp vault. Uses temp_directory_path() rather than mkdtemp()
        // + a hardcoded "/tmp/...": mkdtemp is POSIX-only (MSVC: "error C3861:
        // 'mkdtemp': identifier not found"), and its old use here also left
        // tmpdir_ dangling — mkdtemp rewrites its argument in place and returns
        // that same pointer, so tmpdir_ pointed into a local array that died at
        // the end of SetUp and was then handed to remove_all() in TearDown.
        // The owning fs::path here has the test's lifetime.
        tmpdir_ = std::filesystem::temp_directory_path() /
                  ("osv-qt-adv-search-test-" + std::to_string(
                       ::testing::UnitTest::GetInstance()->random_seed()));
        std::error_code ec;
        std::filesystem::remove_all(tmpdir_, ec);
        ASSERT_TRUE(std::filesystem::create_directories(tmpdir_, ec)) << ec.message();

        vault_path_ = (tmpdir_ / "test.osv").string();

        // Create test vault
        try {
            vault_ = osvqt_test::createTestVault(vault_path_);
        } catch (...) {
            FAIL() << "Failed to create test vault";
        }

        controller_.setVault(&vault_);
    }

    void TearDown() override
    {
        if (!tmpdir_.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(tmpdir_, ec);
        }
    }

    std::filesystem::path tmpdir_;
    std::string vault_path_;
    vault::Vault vault_;
    AdvancedSearchController controller_;
};

TEST_F(AdvancedSearchControllerTest, SearchWithEmptyVaultReturnsEmpty)
{
    // Search with no filters on empty vault (arm debounce and fire)
    QStringList empty;
    controller_.search(empty, empty, "", 2);  // scope: Both
    controller_.updateDebounce(0.2);  // Advance 200ms to trigger debounce

    EXPECT_EQ(controller_.results().size(), 0);
}

TEST_F(AdvancedSearchControllerTest, SearchReturnsResultsForImages)
{
    // Add test images
    osvqt_test::addTinyImages(vault_, "photo", 2);

    // Search with no filters should return images (arm debounce and fire)
    QStringList empty;
    controller_.search(empty, empty, "", 2);  // scope: Both (Images=0, Galleries=1, Both=2)
    controller_.updateDebounce(0.2);  // Advance 200ms to trigger debounce

    EXPECT_GE(controller_.results().size(), 2);
}

TEST_F(AdvancedSearchControllerTest, SaveSearchPersists)
{
    QStringList include{"test"};
    QStringList exclude;

    auto error = controller_.saveSearch("My Search", include, exclude, "", 2);
    EXPECT_TRUE(error.isEmpty());

    controller_.refreshSavedSearches();
    EXPECT_EQ(controller_.savedSearches().size(), 1);
    EXPECT_EQ(controller_.savedSearches()[0].name, "My Search");
}

TEST_F(AdvancedSearchControllerTest, DeleteSavedSearch)
{
    QStringList include{"test"};
    QStringList exclude;

    controller_.saveSearch("Search to Delete", include, exclude, "", 2);
    controller_.refreshSavedSearches();
    EXPECT_EQ(controller_.savedSearches().size(), 1);

    auto error = controller_.deleteSavedSearch("Search to Delete");
    EXPECT_TRUE(error.isEmpty());

    controller_.refreshSavedSearches();
    EXPECT_EQ(controller_.savedSearches().size(), 0);
}

TEST_F(AdvancedSearchControllerTest, ResultsClearedOnVaultChange)
{
    // Add test images
    osvqt_test::addTinyImages(vault_, "photo", 1);

    // Perform search and trigger it
    QStringList empty;
    controller_.search(empty, empty, "", 2);
    controller_.updateDebounce(0.2);  // Fire search
    EXPECT_GT(controller_.results().size(), 0);
    EXPECT_NE(controller_.results()[0].nodeKey, 0);

    // Simulate vault change (lock) by calling onVaultChanged
    controller_.onVaultChanged();

    // Results should be cleared (no dangling pointers)
    EXPECT_EQ(controller_.results().size(), 0);
}

TEST_F(AdvancedSearchControllerTest, DebounceDelaysSearchWithFakeClock)
{
    // Add test images
    osvqt_test::addTinyImages(vault_, "photo", 1);

    // Simulate rapid text input: 3 search calls in quick succession
    // with fake time advancement between them
    QStringList empty;

    // First search (armed, not fired yet)
    controller_.arm();
    EXPECT_TRUE(controller_.isArmed());

    // Advance 50ms (< 150ms threshold)
    controller_.updateDebounce(0.05);
    EXPECT_TRUE(controller_.isArmed());  // Still waiting
    EXPECT_EQ(controller_.results().size(), 0);  // No search executed

    // Advance another 50ms (total 100ms, still < 150ms)
    controller_.updateDebounce(0.05);
    EXPECT_TRUE(controller_.isArmed());
    EXPECT_EQ(controller_.results().size(), 0);

    // Advance another 60ms (total 160ms, now > 150ms)
    controller_.updateDebounce(0.06);
    EXPECT_FALSE(controller_.isArmed());  // Now fired
    EXPECT_GT(controller_.results().size(), 0);  // Search executed
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
