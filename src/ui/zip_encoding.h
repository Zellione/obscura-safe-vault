#pragma once

// Legacy (non-UTF-8) zip/cbz entry-name decoding (Phase 36 part 2).
//
// APPNOTE.TXT's general-purpose bit 11 (the "language encoding flag") tells a
// reader whether an entry's name/comment bytes are UTF-8. Zip tools that
// predate that flag — or that simply never set it — write the bytes of
// whatever the host's OEM code page was at creation time. The overwhelming
// majority of these in the wild are CP437 (MS-DOS/Windows' classic OEM code
// page); Shift_JIS (common for East-Asian archives) is a distinct, much
// larger encoding and is out of scope here — see ROADMAP.md Phase 36. A
// Shift_JIS name without the UTF-8 flag still decodes (as CP437), just not
// correctly; it never crashes or blocks the import.
//
// Pure: no miniz, no vault, no SDL. Unit-tested in tests/ui/test_zip_encoding.cpp.

#include <string>
#include <string_view>

namespace ui {

// Decodes a raw zip/cbz central-directory entry-name byte string to UTF-8.
//
// `utf8_flag` is the entry's general-purpose bit 11: when true, `raw` is
// already UTF-8 and is returned unchanged. When false, `raw` is decoded
// through the CP437->Unicode table UNLESS it happens to already be valid
// UTF-8 (some tools write UTF-8 bytes without ever setting the flag) — in
// that case it is also returned unchanged, since re-decoding valid UTF-8
// bytes as CP437 would turn correct text into mojibake.
[[nodiscard]] std::string decode_zip_entry_name(std::string_view raw, bool utf8_flag);

} // namespace ui
