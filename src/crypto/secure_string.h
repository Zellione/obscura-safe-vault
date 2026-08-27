#pragma once

// SecureString — a secure, runtime-sized string for the index tree's
// human-readable metadata (Phase 91).
//
// The vault index tree (node names, tags, category names, tag descriptions, tag
// field values, saved-search names) is plaintext *metadata* about the user's
// library. Before Phase 91 it lived in plain `std::string` heap buffers: never
// page-locked and never wiped, so after a vault lock / rename the freed bytes
// lingered in allocator memory and any decode could swap them to disk. This is
// the same class of gap Phase 89 closed for `ImageData::pixels`, applied to the
// index tree.
//
// Unlike `SecureBytes` this type is COPYABLE: `IndexNode` must stay copyable for
// `Vault::compact()`'s copy-rebuild-publish (`IndexNode new_root = root_;`),
// combine, transfer, and sort. A copy is a deep copy into a freshly mlock'd
// buffer — like `SecureBytes`, both instances wipe on destruction.
//
// There is deliberately NO small-string optimisation: every instance owns its
// own heap allocation, so even a 4-byte name is mlock'd (best-effort) and wiped.
//
// Like `SecureBytes`:
//   * mlock is best-effort — a failed lock degrades that one buffer to swappable
//     with the once-per-process warn (the two share the lockable budget).
//   * `crypto_wipe` on destruction so freed heap holds no names/tags.
//   * OOM returns false and leaves the object empty; no exceptions escape.
//   * `is_locked()` exposes the mlock outcome for diagnostics/tests.
//
// Read access goes through `view()`; there is NO implicit conversion to
// `std::string_view`, so a name can never silently copy into a plain
// `std::string` without an explicit, visible `.view()`.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <compare>
#include <utility>

#include "platform/safe_print.h"
#include "secure_mem.h"

namespace crypto {

class SecureString {
public:
    SecureString() noexcept = default;

    // Direct-init from any string-like input (`std::string`, `const char*`,
    // literals) via the common `string_view` conversion. Deliberately `explicit`:
    // implicit conversion from string-like input to SecureString is what makes
    // `s = "literal"` ambiguous with `operator=(string_view)` below.
    explicit SecureString(std::string_view s) { (void)assign(s); }

    ~SecureString() { free_storage(); }

    // Copyable (deep copy into a freshly mlock'd buffer) — IndexNode relies on
    // being copyable, see the header comment.
    SecureString(const SecureString& other) { (void)copy_from(other); }

    SecureString& operator=(const SecureString& other)
    {
        if (this != &other) (void)copy_from(other);
        return *this;
    }

    // Movable: steal the buffer (the source is left empty, not copied).
    SecureString(SecureString&& other) noexcept
        : data_(std::move(other.data_))
        , size_(other.size_)
        , locked_(other.locked_)
    {
        other.size_   = 0;
        other.locked_ = false;
    }

    SecureString& operator=(SecureString&& other) noexcept
    {
        if (this != &other) {
            free_storage();
            data_         = std::move(other.data_);
            size_         = other.size_;
            locked_       = other.locked_;
            other.size_   = 0;
            other.locked_ = false;
        }
        return *this;
    }

    // Assignment from string-like input. `std::string`, `const char*` and string
    // literals convert to `std::string_view` implicitly, so this one operator
    // covers them all. OOM leaves the object empty (use `assign` to check it).
    SecureString& operator=(std::string_view s)
    {
        (void)assign(s);
        return *this;
    }

    // Replace contents with `s`. False on OOM, leaving the object empty.
    [[nodiscard]] bool assign(std::string_view s)
    {
        if (s.size() && !s.data()) return false;
        if (!resize(s.size())) return false;
        if (s.size()) std::memcpy(data_.get(), s.data(), s.size());
        return true;
    }

    // Resize to exactly `n` bytes (wiping the old contents first). The new
    // storage is zero-initialised by `make_unique`'s value-initialisation.
    // False (leaving the object empty) on OOM; n == 0 frees.
    [[nodiscard]] bool resize(size_t n)
    {
        free_storage();
        if (n == 0) return true;

        try {
            data_ = std::make_unique<uint8_t[]>(n);
        } catch (const std::bad_alloc&) {
            platform::safe_println(stderr, "[crypto] SecureString alloc of {} bytes failed", n);
            return false;
        }
        size_   = n;
        locked_ = detail::mem_lock(data_.get(), size_);
        if (!locked_) warn_mlock_failure_once();
        return true;
    }

    [[nodiscard]] std::string_view view() const noexcept
    {
        return {reinterpret_cast<const char*>(data_.get()), size_};
    }

    // Writable byte access (deserialisation fills a resized buffer in place).
    [[nodiscard]] uint8_t*       data()       noexcept { return data_.get(); }
    [[nodiscard]] const uint8_t* data() const noexcept { return data_.get(); }

    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] bool is_locked() const noexcept { return locked_; }

    void clear() noexcept { free_storage(); }

    // Wipe now (idempotent; destruction wipes again harmlessly).
    void wipe() noexcept { if (data_) crypto_wipe(data_.get(), size_); }

    friend bool operator==(const SecureString& a, const SecureString& b) noexcept
    {
        return a.view() == b.view();
    }
    friend bool operator==(const SecureString& a, std::string_view b) noexcept
    {
        return a.view() == b;
    }
    friend bool operator==(std::string_view a, const SecureString& b) noexcept
    {
        return a == b.view();
    }
    friend std::strong_ordering operator<=>(const SecureString& a, const SecureString& b) noexcept
    {
        return a.view() <=> b.view();
    }
    friend std::strong_ordering operator<=>(const SecureString& a, std::string_view b) noexcept
    {
        return a.view() <=> b;
    }
    friend std::strong_ordering operator<=>(std::string_view a, const SecureString& b) noexcept
    {
        return a <=> b.view();
    }

private:
    bool copy_from(const SecureString& other)
    {
        if (!resize(other.size_)) return false;
        if (other.size_) std::memcpy(data_.get(), other.data_.get(), other.size_);
        return true;
    }

    void free_storage() noexcept
    {
        if (!data_) return;
        crypto_wipe(data_.get(), size_);
        if (locked_) detail::mem_unlock(data_.get(), size_);
        data_.reset();
        size_   = 0;
        locked_ = false;
    }

    std::unique_ptr<uint8_t[]> data_;
    size_t                     size_   = 0;
    bool                       locked_ = false;
};

} // namespace crypto

namespace std {

template <>
struct hash<crypto::SecureString> {
    [[nodiscard]] size_t operator()(const crypto::SecureString& s) const noexcept
    {
        return std::hash<std::string_view>{}(s.view());
    }
};

} // namespace std