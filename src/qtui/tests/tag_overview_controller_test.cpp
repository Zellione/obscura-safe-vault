#include <gtest/gtest.h>
#include <QCoreApplication>
#include <filesystem>
#include "tag_overview_controller.h"
#include "test_vault_util.h"

class TagOverviewControllerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temporary test vault
        vault_path_ = "/tmp/osv_test_tag_overview.osv";
        try {
            vault_ = osvqt_test::createTestVault(vault_path_);
            osvqt_test::addTinyImages(vault_, "img", 3);
            controller_.setVault(&vault_);
        } catch (...) {
            FAIL() << "Failed to create test vault";
        }
    }

    void TearDown() override {
        if (std::filesystem::exists(vault_path_)) {
            std::filesystem::remove(vault_path_);
        }
    }

    std::string vault_path_;
    vault::Vault vault_;
    TagOverviewController controller_;
};

TEST_F(TagOverviewControllerTest, RefreshPopulatesTagList) {
    // Add some tags to make the overview non-empty
    (void)vault_.add_tag("", "artist:Test");
    (void)vault_.add_tag("", "favorite");

    controller_.refresh();
    auto tags = controller_.tags();

    EXPECT_FALSE(tags.isEmpty());
    // Verify at least the tags we added appear
    bool hasArtist = false, hasFavorite = false;
    for (const auto& item : tags) {
        if (item.tag == "artist:Test") hasArtist = true;
        if (item.tag == "favorite") hasFavorite = true;
    }
    EXPECT_TRUE(hasArtist);
    EXPECT_TRUE(hasFavorite);
}

TEST_F(TagOverviewControllerTest, SortByName) {
    (void)vault_.add_tag("", "zebra");
    (void)vault_.add_tag("", "apple");
    (void)vault_.add_tag("", "monkey");

    controller_.refresh();
    controller_.sortBy(0);  // Sort by Name
    auto tags = controller_.tags();

    EXPECT_GE(tags.size(), 3);
    // After name sort, apple should come before monkey, which should come before zebra
    int appleIdx = -1, monkeyIdx = -1, zebraIdx = -1;
    for (int i = 0; i < tags.size(); ++i) {
        if (tags[i].tag == "apple") appleIdx = i;
        if (tags[i].tag == "monkey") monkeyIdx = i;
        if (tags[i].tag == "zebra") zebraIdx = i;
    }
    EXPECT_NE(appleIdx, -1);
    EXPECT_NE(monkeyIdx, -1);
    EXPECT_NE(zebraIdx, -1);
    EXPECT_LT(appleIdx, monkeyIdx);
    EXPECT_LT(monkeyIdx, zebraIdx);
}

TEST_F(TagOverviewControllerTest, FilterByPrefix) {
    (void)vault_.add_tag("", "apple");
    (void)vault_.add_tag("", "apricot");
    (void)vault_.add_tag("", "banana");

    controller_.refresh();
    controller_.filterByPrefix("ap");
    auto tags = controller_.tags();

    // Should have apple and apricot, but not banana
    for (const auto& item : tags) {
        EXPECT_TRUE(item.tag.startsWith("ap", Qt::CaseInsensitive));
    }
}

TEST_F(TagOverviewControllerTest, SetTagDescription) {
    (void)vault_.add_tag("", "test");
    controller_.refresh();

    bool success = controller_.setTagDescription("test", "A test tag");
    EXPECT_TRUE(success);

    auto tags = controller_.tags();
    bool found = false;
    for (const auto& item : tags) {
        if (item.tag == "test") {
            EXPECT_EQ(item.description, "A test tag");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(TagOverviewControllerTest, ImportJsonDictionary) {
    // Create a minimal JSON tag dictionary
    const char* jsonContent = R"json(
    [
        {"name": "imported_tag", "category": "artist", "description": "An imported tag"},
        {"name": "another_tag", "description": "No category here"}
    ]
    )json";

    QByteArray jsonBytes(jsonContent);
    QString error = controller_.importTagDictJson(jsonBytes);

    EXPECT_TRUE(error.isEmpty()) << "Import should succeed: " << error.toStdString();

    auto summaryLines = controller_.importSummaryLines();
    EXPECT_FALSE(summaryLines.isEmpty());
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
