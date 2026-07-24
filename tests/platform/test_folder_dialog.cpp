#include "test_framework.h"

#include "platform/folder_dialog.h"
#include "platform/paths.h"

#include <filesystem>
#include <string>
#include <vector>

namespace {

// Every path the dialog delivers has already passed through
// platform::normalize_user_path (the single choke point for externally-chosen
// paths). That rewrites separators and canonicalises, so a POSIX literal like
// "/tmp/pictures" does NOT survive verbatim on Windows — asserting the raw
// string failed the MSVC leg while passing on Linux. Compare against the
// normaliser's own output instead, which still catches a dropped or
// wrong-entry delivery on every platform.
[[nodiscard]] std::string normalized(std::string_view raw)
{
    const auto p = platform::normalize_user_path(raw);
    return p ? p->string() : std::string{};
}

// A platform-appropriate absolute path that normalisation will accept.
[[nodiscard]] std::string temp_path(std::string_view leaf)
{
    return (std::filesystem::temp_directory_path() / leaf).string();
}

}  // namespace

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
    const std::string picked = temp_path("osv_pictures");
    Peer::complete(d, {picked});

    // The export poller must skip an import-tagged result.
    CHECK_FALSE(d.take_result(Purpose::Export).has_value());

    // The import poller still finds it (the result was not consumed above).
    auto got = d.take_result(Purpose::ImportFolder);
    REQUIRE(got.has_value());
    REQUIRE(got->size() == 1);
    CHECK_EQ((*got)[0], normalized(picked));
}

TEST(folder_dialog_delivers_every_selected_folder)
{
    FolderDialog d;
    Peer::arm(d, Purpose::ImportFolder);
    Peer::complete(d, {temp_path("osv_a"), temp_path("osv_b"), temp_path("osv_c")});

    auto got = d.take_result(Purpose::ImportFolder);
    REQUIRE(got.has_value());
    CHECK_EQ(got->size(), 3u);
}

// An export result must NOT be visible to import pollers.
TEST(folder_dialog_export_result_not_taken_by_import_purpose)
{
    FolderDialog d;
    Peer::arm(d, Purpose::Export);
    const std::string picked = temp_path("osv_export_dest");
    Peer::complete(d, {picked});

    CHECK_FALSE(d.take_result(Purpose::ImportFolder).has_value());

    auto got = d.take_result(Purpose::Export);
    REQUIRE(got.has_value());
    REQUIRE(got->size() == 1);
    CHECK_EQ((*got)[0], normalized(picked));
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
