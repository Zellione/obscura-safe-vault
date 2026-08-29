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
//   * fallible assign() preserves the old value on OOM; resize() leaves empty.
//     Infallible copy/construction/assignment terminate on OOM rather than let
//     an empty name masquerade as a valid copy and reach an index commit.
//   * `is_locked()` exposes the mlock outcome for diagnostics/tests.
//
// Read access goes through `view()`; there is NO implicit conversion to
// `std::string_view`, so a name can never silently copy into a plain
// `std::string` without an explicit, visible `.view()`.

#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <initializer_list>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
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
    explicit SecureString(std::string_view s)
    {
        assign_or_terminate(s);
    }

    ~SecureString()
    {
        free_storage();
    }

    // Copyable (deep copy into a freshly mlock'd buffer) — IndexNode relies on
    // being copyable, see the header comment.
    SecureString(const SecureString& other)
    {
        assign_or_terminate(other.view());
    }

    SecureString& operator=(const SecureString& other)
    {
        if (this != &other) assign_or_terminate(other.view());
        return *this;
    }

    // Movable: steal the buffer (the source is left empty, not copied).
    SecureString(SecureString&& other) noexcept
        : data_(std::move(other.data_)), size_(other.size_), locked_(other.locked_)
    {
        other.size_ = 0;
        other.locked_ = false;
    }

    SecureString& operator=(SecureString&& other) noexcept
    {
        if (this != &other) {
            free_storage();
            data_ = std::move(other.data_);
            size_ = other.size_;
            locked_ = other.locked_;
            other.size_ = 0;
            other.locked_ = false;
        }
        return *this;
    }

    // Assignment from string-like input. `std::string`, `const char*` and string
    // literals convert to `std::string_view` implicitly, so this one operator
    // covers them all. Use fallible `assign` when OOM must be reported.
    SecureString& operator=(std::string_view s)
    {
        assign_or_terminate(s);
        return *this;
    }

    // Replace contents with `s`. Allocation happens before the old storage is
    // released, so self-views are safe and OOM preserves the original value.
    [[nodiscard]] bool assign(std::string_view s)
    {
        if (s.size() && !s.data()) return false;
        SecureString replacement;
        if (!replacement.resize(s.size())) return false;
        if (s.size()) std::memcpy(replacement.data_.get(), s.data(), s.size());
        swap(replacement);
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
            if (detail::should_fail_secure_allocation()) throw std::bad_alloc{};
            data_ = std::make_unique<uint8_t[]>(n);
        } catch (const std::bad_alloc&) {
            platform::safe_println(stderr, "[crypto] SecureString alloc of {} bytes failed", n);
            return false;
        }
        size_ = n;
        locked_ = detail::mem_lock(data_.get(), size_);
        if (!locked_) warn_mlock_failure_once();
        return true;
    }

    [[nodiscard]] std::string_view view() const noexcept
    {
        return {reinterpret_cast<const char*>(data_.get()), size_};
    }

    // Writable byte access (deserialisation fills a resized buffer in place).
    [[nodiscard]] uint8_t* data() noexcept
    {
        return data_.get();
    }
    [[nodiscard]] const uint8_t* data() const noexcept
    {
        return data_.get();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return size_ == 0;
    }
    [[nodiscard]] size_t size() const noexcept
    {
        return size_;
    }
    [[nodiscard]] bool is_locked() const noexcept
    {
        return locked_;
    }

    void clear() noexcept
    {
        free_storage();
    }

    void swap(SecureString& other) noexcept
    {
        data_.swap(other.data_);
        std::swap(size_, other.size_);
        std::swap(locked_, other.locked_);
    }

    // Wipe now (idempotent; destruction wipes again harmlessly). Not const on
    // purpose: crypto_wipe's C API needs a mutable pointer, and wiping the
    // contained secret is a real mutation, not logical constness.
    void wipe() noexcept  // NOSONAR cpp:S5817
    {
        if (data_) crypto_wipe(data_.get(), size_);
    }

    // The reversed-than-natural directions (`string_view == SecureString`,
    // `string_view <=> SecureString`) come from C++20's rewritten candidates —
    // defining mirrors here would duplicate them.
    friend bool operator==(const SecureString& a, const SecureString& b) noexcept
    {
        return a.view() == b.view();
    }
    friend bool operator==(const SecureString& a, std::string_view b) noexcept
    {
        return a.view() == b;
    }
    friend std::strong_ordering operator<=>(const SecureString& a, const SecureString& b) noexcept
    {
        return a.view() <=> b.view();
    }
    friend std::strong_ordering operator<=>(const SecureString& a, std::string_view b) noexcept
    {
        return a.view() <=> b;
    }

private:
    void assign_or_terminate(std::string_view s)
    {
        // Constructors and assignment operators cannot report allocation
        // failure. Terminating is preferable to silently manufacturing an
        // empty name/tag that a later commit could persist as vault corruption.
        if (!assign(s)) std::terminate();
    }

    void free_storage() noexcept
    {
        if (!data_) return;
        crypto_wipe(data_.get(), size_);
        if (locked_) detail::mem_unlock(data_.get(), size_);
        detail::record_wipe_for_tests(std::as_bytes(std::span(data_.get(), size_)));
        data_.reset();
        size_ = 0;
        locked_ = false;
    }

    // `unique_ptr<uint8_t[]>` is the codebase's secure-buffer idiom (RawBytes)
    // — raw, allocator-owned memory we mlock; std::vector/std::array cannot
    // express that.
    std::unique_ptr<uint8_t[]> data_;  // NOSONAR cpp:S5945
    size_t size_ = 0;
    bool locked_ = false;
};

// Copyable secure storage for opaque metadata blobs. Saved-search queries are
// byte-encoded, but contain human-readable tags/group names/name filters and
// therefore need the same mlock+wiping behavior as SecureString.
class SecureBlob {
public:
    SecureBlob() noexcept = default;

    explicit SecureBlob(std::span<const uint8_t> bytes)
    {
        if (!assign(bytes)) std::terminate();
    }

    SecureBlob(std::initializer_list<uint8_t> bytes)
        : SecureBlob(std::span<const uint8_t>(bytes.begin(), bytes.size()))
    {}

    [[nodiscard]] bool assign(std::span<const uint8_t> bytes)
    {
        if (bytes.size() && !bytes.data()) return false;
        if (bytes.empty()) return storage_.assign({});
        return storage_.assign(std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                                bytes.size()));
    }

    [[nodiscard]] bool resize(size_t n) { return storage_.resize(n); }
    [[nodiscard]] uint8_t* data() noexcept { return storage_.data(); }
    [[nodiscard]] const uint8_t* data() const noexcept { return storage_.data(); }
    [[nodiscard]] size_t size() const noexcept { return storage_.size(); }
    [[nodiscard]] bool empty() const noexcept { return storage_.empty(); }
    [[nodiscard]] bool is_locked() const noexcept { return storage_.is_locked(); }
    [[nodiscard]] std::span<uint8_t> span() noexcept { return {data(), size()}; }
    [[nodiscard]] std::span<const uint8_t> as_span() const noexcept { return {data(), size()}; }

    [[nodiscard]] explicit operator std::span<const uint8_t>() const noexcept { return as_span(); }

private:
    SecureString storage_;
};

}  // namespace crypto

namespace std {

template <> struct hash<crypto::SecureString> {
    [[nodiscard]] size_t operator()(const crypto::SecureString& s) const noexcept
    {
        return std::hash<std::string_view>{}(s.view());
    }
};

}  // namespace std
