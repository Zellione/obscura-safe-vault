#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "platform/path_utf8.h"
#include "vault/safe_name.h"

namespace ui {

struct NewVaultPath {
    std::filesystem::path path;
    const char* error = nullptr;
};

// This is a UI preflight only: the storage layer still exclusively creates the
// directory, so a destination appearing after this check cannot be overwritten.
[[nodiscard]] inline NewVaultPath new_vault_path(const std::filesystem::path& parent,
                                                std::string_view name)
{
    if (!vault::is_safe_node_name(name))
        return {{}, "Enter a valid vault name without path separators."};
    std::string component(name);
    if (!component.ends_with(".osv")) component += ".osv";
    if (component.size() > 255)
        return {{}, "The vault name is too long."};
    std::error_code ec;
    if (parent.empty() || !std::filesystem::is_directory(parent, ec) || ec)
        return {{}, "The parent folder is unavailable."};
    auto path = parent / platform::utf8_to_path(component);
    // symlink_status also catches dangling links, which exists() would miss.
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec && ec != std::errc::no_such_file_or_directory)
        return {{}, "Could not check the destination."};
    if (status.type() != std::filesystem::file_type::not_found)
        return {{}, "That name already exists. Choose a different name."};
    return {std::move(path), nullptr};
}

} // namespace ui
