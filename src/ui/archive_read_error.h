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
    switch (error) {
    case ArchiveReadError::None:
        return {};
    case ArchiveReadError::PasswordRequired:
        return "This archive requires a password";
    case ArchiveReadError::WrongPassword:
        return "Wrong archive password or damaged encrypted data";
    case ArchiveReadError::Corrupt:
        return "Archive is corrupt or incomplete";
    case ArchiveReadError::Unsupported:
        return "Unsupported archive compression method";
    case ArchiveReadError::MemoryLimit:
        return "Archive exceeds the decoder memory limit";
    }
    return "Could not read archive";
}
}  // namespace ui
