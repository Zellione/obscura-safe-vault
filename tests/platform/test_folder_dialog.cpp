#include "test_framework.h"

#include "platform/folder_dialog.h"

#include <string>
#include <vector>

// Test seam: FolderDialog's Open/Done transition is normally driven by SDL (the
// open method shows a real dialog; on_folder is the async SDL callback). The
// peer reaches the same private state machine without showing a dialog, so the
// result-routing logic can be exercised headlessly.
namespace platform {
struct FolderDialogTestPeer {
    static void arm(FolderDialog& d, FolderDialog::Purpose p) { d.begin_open(p); }

    static void complete(FolderDialog& d, const std::vector<std::string>& paths)
    {
        std::vector<const char*> argv;
        argv.reserve(paths.size() + 1);
        for (const auto& s : paths) argv.push_back(s.c_str());
        argv.push_back(nullptr);
        FolderDialog::on_folder(&d, argv.data(), 0);
    }
};
}  // namespace platform

using platform::FolderDialog;
using Peer = platform::FolderDialogTestPeer;
using Purpose = platform::FolderDialog::Purpose;

// Phase 51: the folder dialog is drained by two handlers (export destination and
// folder import), so its result must be purpose-tagged or one steals the other's.
TEST(folder_dialog_result_is_not_drained_by_the_wrong_purpose)
{
    FolderDialog d;
    Peer::arm(d, Purpose::ImportFolder);
    Peer::complete(d, {"/tmp/pictures"});

    // The export poller must skip an import-tagged result.
    CHECK_FALSE(d.take_result(Purpose::Export).has_value());

    // The import poller still finds it (the result was not consumed above).
    auto got = d.take_result(Purpose::ImportFolder);
    REQUIRE(got.has_value());
    REQUIRE(got->size() == 1);
    CHECK_EQ((*got)[0], std::string("/tmp/pictures"));
}

TEST(folder_dialog_delivers_every_selected_folder)
{
    FolderDialog d;
    Peer::arm(d, Purpose::ImportFolder);
    Peer::complete(d, {"/tmp/a", "/tmp/b", "/tmp/c"});

    auto got = d.take_result(Purpose::ImportFolder);
    REQUIRE(got.has_value());
    CHECK_EQ(got->size(), 3u);
}

// An export result must NOT be visible to import pollers.
TEST(folder_dialog_export_result_not_taken_by_import_purpose)
{
    FolderDialog d;
    Peer::arm(d, Purpose::Export);
    Peer::complete(d, {"/tmp/export_dest"});

    CHECK_FALSE(d.take_result(Purpose::ImportFolder).has_value());

    auto got = d.take_result(Purpose::Export);
    REQUIRE(got.has_value());
    REQUIRE(got->size() == 1);
    CHECK_EQ((*got)[0], std::string("/tmp/export_dest"));
}

// A cancelled dialog (no folders) still resolves for the matching purpose.
TEST(folder_dialog_cancel_routes_to_matching_purpose)
{
    FolderDialog d;
    Peer::arm(d, Purpose::ImportFolder);
    Peer::complete(d, {});  // user cancelled

    // Wrong purpose must skip the result.
    CHECK_FALSE(d.take_result(Purpose::Export).has_value());

    // Matching purpose gets it (present but empty == cancelled).
    auto got = d.take_result(Purpose::ImportFolder);
    REQUIRE(got.has_value());
    CHECK(got->empty());
}
