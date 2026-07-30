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
        // Create temp vault
        char tmpdir_template[] = "/tmp/osv-qt-test-XXXXXX";
        tmpdir_ = mkdtemp(tmpdir_template);
        ASSERT_NE(tmpdir_, nullptr);

        vault_path_ = std::string(tmpdir_) + "/test.osv";

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
        if (tmpdir_) {
            std::filesystem::remove_all(tmpdir_);
        }
    }

    char* tmpdir_ = nullptr;
    std::string vault_path_;
    vault::Vault vault_;
    AdvancedSearchController controller_;
};

TEST_F(AdvancedSearchControllerTest, SearchWithEmptyVaultReturnsEmpty)
{
    // Search with no filters on empty vault
    QStringList empty;
    controller_.search(empty, empty, "", 2);  // scope: Both

    EXPECT_EQ(controller_.results().size(), 0);
}

TEST_F(AdvancedSearchControllerTest, SearchReturnsResultsForImages)
{
    // Add test images
    osvqt_test::addTinyImages(vault_, "photo", 2);

    // Search with no filters should return images
    QStringList empty;
    controller_.search(empty, empty, "", 2);  // scope: Both (Images=0, Galleries=1, Both=2)

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

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
