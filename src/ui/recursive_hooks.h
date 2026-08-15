#pragma once

// Phase 53: real archive backends behind walk_archive's injected hooks.
//
// walk_archive itself is backend-agnostic so the recursion can be tested with
// fakes. This is the adapter that supplies the genuine article: miniz for
// zip/cbz, ArchiveReader (libarchive) for 7z/rar/tar, and a MediaSink for
// gallery creation and placement.
//
// Gallery paths: walk_archive works in ABSOLUTE vault paths (it has to — a
// nested sub-gallery's path is only known mid-walk), while MediaSink takes
// paths relative to the import root. The adapter strips the root prefix, the
// same adjustment the flat importer makes inline.

#include "ui/recursive_import.h"

#include "crypto/secure_mem.h"

#include <memory>
#include <string>
#include <string_view>

namespace ui {

class MediaSink;
class ArchiveReaderCache;

// The import password copied into a wiping, best-effort-mlock'd buffer, shared
// by the hook closures. `crypto::SecureBytes` crypto_wipe's its storage on
// destruction, so the plaintext password never lingers in freed heap — unlike a
// plain std::string captured by value (invariant #2). Shared so the two closures
// hold one copy; wiped when the last hook referencing it dies.
using SecurePassword = std::shared_ptr<crypto::SecureBytes>;

// Copy `password`'s bytes into a fresh SecurePassword. Always returns non-null;
// an empty password yields an empty (size 0) buffer. On allocation failure the
// buffer is left empty (the import then simply fails to authenticate and is
// tallied+skipped — never a crash).
[[nodiscard]] SecurePassword make_secure_password(std::string_view password);

// Hooks bound to `sink`, with `root_gallery` the absolute path the walk starts
// at. `password` is tried on every nested archive too: the background queue
// takes a password once at enqueue and cannot prompt mid-job, so a nested
// archive needing a different one is tallied and skipped rather than stalling
// the import.
[[nodiscard]] RecursiveHooks make_recursive_hooks(MediaSink&        sink,
                                                  std::string_view  root_gallery,
                                                  std::string_view  password = {},
                                                  ArchiveReaderCache* cache = nullptr);

} // namespace ui
