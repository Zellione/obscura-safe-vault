#pragma once

#include "vault/op_progress.h"
#include "vault/vault.h"

#include <span>
#include <string>
#include <string_view>

namespace ui {

// Where an importer places decoded archive entries. Implementations:
//  - DirectVaultSink: add_image/add_video/create_gallery (synchronous path —
//    behavior identical to pre-Phase-50; used by tests and transfer tooling).
//  - the ImportQueue's staging sink (Task 7): stage + post records.
// `data` is mlock'd and wiped by the CALLER after the call returns, as today.
class MediaSink {
public:
    virtual ~MediaSink() = default;

    // Relative gallery path under the import root ("" = the root itself).
    [[nodiscard]] virtual vault::VaultResult ensure_gallery(std::string_view rel_gallery) = 0;

    [[nodiscard]] virtual vault::VaultResult place_image(std::string_view rel_gallery,
                                                         std::span<const uint8_t> data,
                                                         std::string_view name) = 0;

    [[nodiscard]] virtual vault::VaultResult place_video(std::string_view rel_gallery,
                                                         std::span<const uint8_t> data,
                                                         std::string_view name) = 0;

    // Cooperative cancel poll (replaces direct progress->cancel reads where
    // the executor loops).
    [[nodiscard]] virtual bool cancelled() const { return false; }
};

// The pre-Phase-50 behavior: writes straight into `v` under `base_gallery`
// with per-file commits. Owns nothing.
class DirectVaultSink final : public MediaSink {
public:
    DirectVaultSink(vault::Vault& v, std::string_view base_gallery, std::string_view new_gallery_name,
                    vault::OpProgress* progress = nullptr)
        : v_(v)
        , base_(joined_gallery(base_gallery, new_gallery_name))
        , progress_(progress)
    {
    }

    [[nodiscard]] vault::VaultResult ensure_gallery(std::string_view rel_gallery) override
    {
        const std::string abs_path = joined_gallery(base_, rel_gallery);
        return v_.create_gallery(abs_path);
    }

    [[nodiscard]] vault::VaultResult place_image(std::string_view rel_gallery, std::span<const uint8_t> data,
                                                  std::string_view name) override
    {
        const std::string abs_path = joined_gallery(base_, rel_gallery);
        return v_.add_image(abs_path, data, name);
    }

    [[nodiscard]] vault::VaultResult place_video(std::string_view rel_gallery, std::span<const uint8_t> data,
                                                  std::string_view name) override
    {
        const std::string abs_path = joined_gallery(base_, rel_gallery);
        return v_.add_video(abs_path, data, name);
    }

    [[nodiscard]] bool cancelled() const override
    {
        return progress_ && progress_->cancel.load();
    }

private:
    static std::string joined_gallery(std::string_view base, std::string_view name)
    {
        if (base.empty())
            return std::string(name);
        if (name.empty())
            return std::string(base);
        return std::string(base) + "/" + std::string(name);
    }

    vault::Vault& v_;
    std::string base_;  // base_gallery + "/" + new_gallery_name
    vault::OpProgress* progress_;
};

}  // namespace ui
