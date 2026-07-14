#pragma once

// Node-name safety rules — the guard against CWE-22 (path traversal) on export.
//
// An IndexNode's `name` is a single path COMPONENT, never a path. It matters
// because a vault is a portable, shareable artifact (vault manager, cross-vault
// transfer) and its index is therefore UNTRUSTED INPUT: index.cpp deserialises
// `name` as opaque bytes, and ui::export_* turns it back into a real filesystem
// path under the folder the user picked. std::filesystem::path::operator/ does
// not contain — `fs::path("/out") / "/etc/cron.d/x"` discards "/out" entirely —
// so an unchecked name is an arbitrary-file-write primitive.
//
// Two entry points, deliberately different in temperament:
//   * is_safe_node_name — the vault ingress (Vault::add_image / add_video /
//     create_gallery) REJECTS anything failing this. The API is the trust
//     boundary; callers already handle a VaultResult failure.
//   * sanitize_node_name — the importers REPAIR instead, because an awkward
//     filename inside an archive must not fail the whole import. Its output is
//     guaranteed to satisfy is_safe_node_name.
//
// Pure: no I/O, no SDL, no vault types. Unit-tested in tests/vault/test_safe_name.cpp.

#include <cstddef>
#include <string>
#include <string_view>

namespace vault {

// Every mainstream filesystem's per-component byte limit.
inline constexpr size_t MAX_NODE_NAME_BYTES = 255;

// True iff `name` is usable as a single filename on every target platform.
// Rejects: empty; longer than MAX_NODE_NAME_BYTES; exactly "." or ".."; any '/'
// or '\\' (both, on every platform — a vault written on Linux gets exported on
// Windows); NUL, control bytes and DEL; the Windows-reserved characters
// < > : " | ? * ; a trailing '.' or ' ' (Windows strips those silently, which
// would defeat any check made before the strip); and the reserved DOS device
// names (CON, PRN, AUX, NUL, COM1-9, LPT1-9), case-insensitively, with or
// without an extension.
//
// Bytes >= 0x80 stay opaque and are allowed: names are stored as UTF-8 and CJK
// filenames are entirely normal in the comic archives this app imports.
[[nodiscard]] bool is_safe_node_name(std::string_view name) noexcept;

// Best-effort repair of a name arriving from an archive entry or a picked file.
// Each rejected byte becomes '_'; a reserved device name gains a '_' prefix; an
// over-long name is truncated on a UTF-8 codepoint boundary; a name with nothing
// left (""/"."/"..") becomes "unnamed". Never fails. The result always satisfies
// is_safe_node_name() — tests/vault/test_safe_name.cpp asserts exactly that.
[[nodiscard]] std::string sanitize_node_name(std::string_view name);

} // namespace vault
