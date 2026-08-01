#include <gtest/gtest.h>
#include <QCoreApplication>
#include <filesystem>
#include "tag_list_import_adapter.h"
#include "test_vault_util.h"

class TagListImportAdapterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // temp_directory_path(), not a hardcoded "/tmp/...": Windows has no /tmp.
        vault_path_ =
            (std::filesystem::temp_directory_path() / "osv_test_tag_list_import.osv").string();
        try {
            vault_ = osvqt_test::createTestVault(vault_path_);
            osvqt_test::addTinyImages(vault_, "img", 1);
            adapter_.setVault(&vault_);
        } catch (...) {
            FAIL() << "Failed to create test vault";
        }
    }

    void TearDown() override {
        // Close the vault BEFORE unlinking. POSIX happily unlinks a file that
        // still has an open descriptor, so this was invisible on Linux, but
        // Windows refuses ("The process cannot access the file because it is
        // being used by another process") and remove() then throws out of
        // TearDown, failing every test in the fixture. Vault::lock() is not
        // enough — it wipes the key but documents that the file stays open —
        // so move-assign a fresh Vault to run the destructor. gtest destroys
        // the fixture only after TearDown returns, hence doing it by hand.
        vault_ = vault::Vault{};
        std::error_code ec;
        std::filesystem::remove(vault_path_, ec);
    }

    std::string vault_path_;
    vault::Vault vault_;
    TagListImportAdapter adapter_;
};

TEST_F(TagListImportAdapterTest, ParseTagListSplitsOnNewline) {
    const char* content = "apple\nbanana\ncherry\n";
    QByteArray bytes(content);

    auto tags = adapter_.parseTagList(bytes);

    EXPECT_EQ(tags.size(), 3);
    EXPECT_EQ(tags[0], "apple");
    EXPECT_EQ(tags[1], "banana");
    EXPECT_EQ(tags[2], "cherry");
}

TEST_F(TagListImportAdapterTest, ParseTagListTrimsWhitespace) {
    const char* content = "  apple  \n  banana  \n";
    QByteArray bytes(content);

    auto tags = adapter_.parseTagList(bytes);

    EXPECT_EQ(tags.size(), 2);
    EXPECT_EQ(tags[0], "apple");
    EXPECT_EQ(tags[1], "banana");
}

TEST_F(TagListImportAdapterTest, ParseTagListDropsBlankLines) {
    const char* content = "apple\n\nbanana\n\n\ncherry\n";
    QByteArray bytes(content);

    auto tags = adapter_.parseTagList(bytes);

    EXPECT_EQ(tags.size(), 3);
    EXPECT_EQ(tags[0], "apple");
    EXPECT_EQ(tags[1], "banana");
    EXPECT_EQ(tags[2], "cherry");
}

TEST_F(TagListImportAdapterTest, ParseTagListDedupCaseInsensitive) {
    const char* content = "Apple\napple\nAPPLE\nbanana\n";
    QByteArray bytes(content);

    auto tags = adapter_.parseTagList(bytes);

    // Should have 2 tags: Apple (first casing), banana
    EXPECT_EQ(tags.size(), 2);
    EXPECT_EQ(tags[0], "Apple");  // Keep first casing
    EXPECT_EQ(tags[1], "banana");
}

TEST_F(TagListImportAdapterTest, ImportTagsToNode) {
    auto tags = QStringList() << "tag1" << "tag2" << "tag3";

    int count = adapter_.importTagsToNode("", tags);

    EXPECT_EQ(count, 3);

    // Verify tags were added by fetching them
    auto ownTags = vault_.resolve_node("")->tags;
    EXPECT_GE(ownTags.size(), 3);
}

TEST_F(TagListImportAdapterTest, ImportTagsFromBytes) {
    const char* content = "artist:smith\nfavorite\ntrip:2024\n";
    QByteArray bytes(content);

    int count = adapter_.importTagsFromBytes("", bytes);

    EXPECT_EQ(count, 3);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
