#pragma once

#include <filesystem>
#include <string_view>

namespace platform {

// Persistent, append-only error log. Testable via an injected path (mirrors
// ThemePref/VolumePref); the app uses default_location() (config_dir()/error.log).
class ErrorLog {
public:
    ErrorLog() = default;   // empty path -> inert no-op
    explicit ErrorLog(std::filesystem::path file);
    static ErrorLog default_location();

    // Appends "[tag] message\n" to the file. Best-effort: silently no-ops if
    // the file can't be opened (never throws, never blocks the caller on a
    // logging failure). Never pass decrypted image bytes, passwords, or key
    // material here (security invariant #5).
    void append(std::string_view tag, std::string_view message) const;

private:
    std::filesystem::path file_;
};

// Convenience used throughout the app: stderr print (existing behaviour,
// visible in Debug's console) + append to default_location() (so Release,
// which is a windowless app with no console, still leaves a diagnostic
// trail). Best-effort; never throws.
void log_error(std::string_view tag, std::string_view message);

// Installs std::set_terminate so an uncaught exception logs what() (or "non-
// std exception") via log_error("Fatal", ...) before the process dies —
// otherwise the process vanishes with zero trace (this is what a console-less
// Release build does today). Call once, early in app startup.
void install_terminate_logger();

} // namespace platform
