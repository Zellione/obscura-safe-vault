#pragma once

// Phase 5 stub: platform-specific paths (config dir, default vault location)
// and SDL3 native file-dialog wrapper for import.
// Full implementation in Phase 5.

#include <filesystem>

namespace platform {

// TODO (Phase 5): config_dir()  — ~/.config/obscura-safe-vault/ (Linux),
//                                  %APPDATA%/ObscuraSafeVault/ (Windows),
//                                  ~/Library/Application Support/ (macOS)
// TODO (Phase 5): default_vault_path() — default .osv file location
// TODO (Phase 5): show_open_file_dialog(title, filter) -> std::filesystem::path
//                  wraps SDL_ShowOpenFileDialog (SDL3 native, no extra dep)

} // namespace platform
