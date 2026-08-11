#pragma once

#include "vault/transfer.h"

#include <string>
#include <vector>

namespace ui {

// Pure state machine for the split-mode M prompt (Phase 78). SDL-free.
// Manages the multi-stage transfer confirmation flow:
// 1. Mode stage: choose Move or Copy
// 2. Conflict stage (optional): if collisions exist, choose how to resolve
class DualTransferPrompt {
public:
    enum class Stage { Closed, Mode, Conflict };
    enum class Key { Up, Down, Enter, Esc };

    struct Launch {
        vault::TransferMode   mode;
        vault::CollisionPolicy policy;
        bool                  fire = false;   // false => nothing to do (yet/cancelled)
    };

    // Open at the Mode stage. `conflicts` = names from vault::colliding_galleries
    // (host pre-computes; empty => Enter on Mode fires immediately).
    void open(std::string dst_label, std::vector<std::string> conflicts);

    // Advances the state machine based on the key.
    [[nodiscard]] Launch key(Key k);

    [[nodiscard]] Stage stage() const { return stage_; }
    [[nodiscard]] int   selected() const { return selected_; }
    [[nodiscard]] const std::string& dst_label() const { return dst_label_; }
    [[nodiscard]] const std::vector<std::string>& conflicts() const { return conflicts_; }

private:
    Stage               stage_ = Stage::Closed;
    int                 selected_ = 0;
    std::string         dst_label_;
    std::vector<std::string> conflicts_;
    vault::TransferMode mode_ = vault::TransferMode::Move;  // Chosen in Mode stage, used in Conflict stage
};

} // namespace ui
