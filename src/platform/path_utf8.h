// src/platform/path_utf8.h
#pragma once

// UTF-8 <-> std::filesystem::path round-tripping for Linux. On Linux,
// path::string() / path::c_str() already IS UTF-8, so the project convention
// ("a std::string holding a path is UTF-8 by definition") holds trivially —
// these helpers just centralise the conversion so callers never reach for
// path::string() (the narrow conversion is still banned project-wide; see
// AGENTS.md / mem:conventions).
//
// Layering: pure std (no SDL, no OS handles), so this header is includable
// from ANY module — a documented exception to the platform/ layering rule,
// like a std header.

#include <bit>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <new>
#include <string>
#include <string_view>
#include <system_error>

namespace platform {

// Interpret `utf8` as UTF-8 bytes and build a path.
[[nodiscard]] inline std::filesystem::path utf8_to_path(std::string_view utf8)
{
    return std::filesystem::path(
        std::u8string_view(std::bit_cast<const char8_t*>(utf8.data()), utf8.size()));
}

// Native-format path as UTF-8. On Linux the native path is already UTF-8, so
// this is the round-trip through u8string() to keep the convention (never
// use path::string()).
[[nodiscard]] inline std::string path_to_utf8(const std::filesystem::path& p)
{
    try {
        const std::u8string s = p.u8string();
        return std::string(std::bit_cast<const char*>(s.data()), s.size());
    } catch (const std::bad_alloc&) {
        return "(unrepresentable path)";
    }
}

// Generic-format (forward-slash) path as UTF-8 — vaults.list stays in one
// portable shape on every platform (see VaultRegistry::write).
[[nodiscard]] inline std::string path_to_utf8_generic(const std::filesystem::path& p)
{
    try {
        const std::u8string s = p.generic_u8string();
        return std::string(std::bit_cast<const char*>(s.data()), s.size());
    } catch (const std::bad_alloc&) {
        return "(unrepresentable path)";
    }
}

// fopen that accepts a path: on Linux path::c_str() IS the native char*, so
// plain fopen is correct.
[[nodiscard]] inline std::FILE* fopen_path(const std::filesystem::path& p, const char* mode)
{
    return std::fopen(p.c_str(), mode);
}

// freopen counterpart.
[[nodiscard]] inline std::FILE* freopen_path(const std::filesystem::path& p, const char* mode, std::FILE* stream)
{
    return std::freopen(p.c_str(), mode, stream);
}

} // namespace platform
