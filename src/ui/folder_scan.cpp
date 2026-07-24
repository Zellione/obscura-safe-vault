#include "ui/folder_scan.h"

#include <print>
#include <system_error>

namespace fs = std::filesystem;

namespace ui {

std::vector<ZipEntry> scan_folder(const fs::path& root, ScanLimits limits)
{
    std::vector<ZipEntry> out;
    std::error_code ec;
    if (!fs::is_directory(root, ec) || ec) { return out; }

    // skip_permission_denied: an unreadable subtree is skipped, not fatal.
    // We deliberately do NOT pass follow_directory_symlink — cycles, and a
    // symlink can point outside the root the user actually chose.
    fs::recursive_directory_iterator it(
        root, fs::directory_options::skip_permission_denied, ec);
    if (ec) { return out; }

    for (const fs::directory_entry& e : it) {
        if (out.size() >= limits.max_entries) {
            std::println(stderr, "[FolderScan] entry cap reached; import truncated");
            break;
        }
        if (e.is_symlink(ec) || ec) { ec.clear(); continue; }

        const fs::path rel = fs::relative(e.path(), root, ec);
        if (ec) { ec.clear(); continue; }

        out.push_back({.path = rel.generic_string(), .is_dir = e.is_directory(ec)});
        ec.clear();
    }
    return out;
}

} // namespace ui
