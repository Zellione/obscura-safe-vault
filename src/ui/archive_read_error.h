#pragma once

#include <string_view>

namespace ui {
enum class ArchiveReadError {
    None,
    PasswordRequired,
    WrongPassword,
    Corrupt,
    Unsupported,
    MemoryLimit
};

[[nodiscard]] inline std::string_view archive_read_error_text(ArchiveReadError error)
{
    using enum ArchiveReadError;
    switch (error) {
    case None:
        return {};
    case PasswordRequired:
        return "This archive requires a password";
    case WrongPassword:
        return "Wrong archive password or damaged encrypted data";
    case Corrupt:
        return "Archive is corrupt or incomplete";
    case Unsupported:
        return "Unsupported archive compression method";
    case MemoryLimit:
        return "Archive exceeds the decoder memory limit";
    }
    return "Could not read archive";
}
}  // namespace ui
