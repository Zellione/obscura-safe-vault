// src/platform/locale_init.h
#pragma once

// Process-wide LC_CTYPE initialization (Phase 72).
//
// WHY: libarchive converts 7z/RAR entry names (stored as UTF-16 in the
// archive header) through the CURRENT LOCALE at parse time. A process that
// never calls setlocale() runs under the default "C" locale, where that
// conversion fails for any non-ASCII name — every accessor (narrow, wide,
// _utf8) then returns NULL and each CJK entry collapsed to the "unnamed"
// fallback on import. Switching LC_CTYPE to a UTF-8 locale makes the narrow
// accessor hand back proper UTF-8 bytes.
//
// LC_CTYPE ONLY — never LC_ALL/LC_NUMERIC: a German/French locale would flip
// printf/strtod to decimal commas and silently corrupt any formatted-number
// round trip.
//
// Windows note: libarchive on Win32 converts with WideCharToMultiByte(GetACP())
// — the CRT locale is not consulted, so this helper cannot fix the narrow
// accessor there. It does not need to: on Win32 libarchive always keeps the
// wide name (AES_SET_WCS), and ui::ArchiveReader's entry_name_utf8 falls back
// to archive_entry_pathname_w. The setlocale here is still correct for CRT
// consistency and is a no-op for that path.
//
// Call once, from main(), before any threads exist — setlocale is not
// thread-safe against concurrent locale-dependent calls.

#include <clocale>
#include <cstring>

#if !defined(_WIN32)
#  include <langinfo.h>
#endif

namespace platform {

inline void init_locale()
{
#if defined(_WIN32)
    // UCRT (Win10 1803+) accepts an explicit UTF-8 locale; on failure the
    // process stays on "C", which degrades gracefully (see Windows note).
    (void)std::setlocale(LC_CTYPE, ".UTF-8");
#else
    // The user's environment locale first (respects their UTF-8 variant);
    // if that lands on a non-UTF-8 codeset (headless CI exporting LANG=C),
    // force C.UTF-8 (glibc and musl both ship it).
    const char* l  = std::setlocale(LC_CTYPE, "");
    const char* cs = nl_langinfo(CODESET);
    if (!l || !cs || std::strcmp(cs, "UTF-8") != 0)
        (void)std::setlocale(LC_CTYPE, "C.UTF-8");
#endif
}

} // namespace platform
