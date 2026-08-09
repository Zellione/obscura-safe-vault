// src/platform/path_utf8.h
#pragma once

// UTF-8 <-> std::filesystem::path, never through the ANSI code page.
//
// WHY: on Windows, path::string() converts the native UTF-16 path through the
// active code page and THROWS std::system_error when a character has no
// mapping (CJK filename on a Latin-1 system — the "Mehrbytecodepage" import
// crash). The narrow path{std::string} constructor mis-decodes SDL's UTF-8
// dialog strings the same way, and fopen(p.string()) cannot open such files
// at all. Project convention: a std::string holding a path is UTF-8 by
// definition; these helpers are the ONLY sanctioned conversions.
//
// Layering: pure std (no SDL, no OS handles beyond _wfopen), so this header
// is includable from ANY module — a documented exception to the platform/
// layering rule, like a std header.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

namespace platform {

// Interpret `utf8` as UTF-8 bytes (never the ANSI code page) and build a path.
[[nodiscard]] inline std::filesystem::path utf8_to_path(std::string_view utf8)
{
    return std::filesystem::path(
        std::u8string_view(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

// Native-format path as UTF-8. No-throw: an ill-formed native name (unpaired
// UTF-16 surrogate on NTFS) degrades to a placeholder instead of terminating —
// callers use the result for display, log lines, and vault node names, all of
// which tolerate a lossy fallback (node names pass sanitize_node_name anyway).
[[nodiscard]] inline std::string path_to_utf8(const std::filesystem::path& p)
{
    try {
        const std::u8string s = p.u8string();
        return std::string(reinterpret_cast<const char*>(s.data()), s.size());
    } catch (...) {
        return "(unrepresentable path)";
    }
}

// Generic-format (forward-slash) path as UTF-8 — vaults.list stays in one
// portable shape on every platform (see VaultRegistry::write).
[[nodiscard]] inline std::string path_to_utf8_generic(const std::filesystem::path& p)
{
    try {
        const std::u8string s = p.generic_u8string();
        return std::string(reinterpret_cast<const char*>(s.data()), s.size());
    } catch (...) {
        return "(unrepresentable path)";
    }
}

// fopen that accepts the FULL native path range: _wfopen on Windows (narrow
// fopen goes through the ANSI code page and fails on unmappable names),
// plain fopen elsewhere (path::c_str() is already the native char*).
[[nodiscard]] inline std::FILE* fopen_path(const std::filesystem::path& p, const char* mode)
{
#if defined(_WIN32)
    std::wstring wmode(mode, mode + std::strlen(mode));   // modes are ASCII
    return ::_wfopen(p.c_str(), wmode.c_str());
#else
    return std::fopen(p.c_str(), mode);
#endif
}

} // namespace platform
