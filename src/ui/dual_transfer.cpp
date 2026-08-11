#include "ui/dual_transfer.h"

#include <algorithm>

namespace ui {

void DualTransferPrompt::open(std::string dst_label, std::vector<std::string> conflicts)
{
    stage_      = Stage::Mode;
    selected_   = 0;
    dst_label_  = std::move(dst_label);
    conflicts_  = std::move(conflicts);
}

DualTransferPrompt::Launch DualTransferPrompt::key(Key k)
{
    Launch result;
    result.mode   = vault::TransferMode::Move;
    result.policy = vault::CollisionPolicy::Fail;
    result.fire   = false;

    using enum Stage;
    if (stage_ == Mode) mode_key(k, result);
    else if (stage_ == Conflict) conflict_key(k, result);
    return result;
}

// Mode stage: 3 rows = {"Move to <dst>", "Copy to <dst>", "Cancel"}
void DualTransferPrompt::mode_key(Key k, Launch& result)
{
    using enum Key;
    switch (k) {
        case Up:    selected_ = std::max(0, selected_ - 1); break;
        case Down:  selected_ = std::min(2, selected_ + 1); break;
        case Esc:   stage_ = Stage::Closed; break;
        case Enter: mode_enter(result); break;
    }
}

void DualTransferPrompt::mode_enter(Launch& result)
{
    using enum Stage;
    if (selected_ == 2) {  // Cancel row
        stage_ = Closed;
        return;
    }
    mode_       = selected_ == 0 ? vault::TransferMode::Move : vault::TransferMode::Copy;
    result.mode = mode_;
    if (conflicts_.empty()) {
        result.policy = vault::CollisionPolicy::Fail;
        result.fire   = true;
        stage_        = Closed;
    } else {
        // Transition to Conflict stage
        stage_    = Conflict;
        selected_ = 0;  // reset selection for conflict stage
    }
}

// Conflict stage: 3 rows = {"Combine into existing", "Rename (_2)", "Cancel"}
void DualTransferPrompt::conflict_key(Key k, Launch& result)
{
    using enum Key;
    switch (k) {
        case Up:    selected_ = std::max(0, selected_ - 1); break;
        case Down:  selected_ = std::min(2, selected_ + 1); break;
        case Esc:
            // Return to Mode stage, reset selection
            stage_    = Stage::Mode;
            selected_ = 0;
            break;
        case Enter: conflict_enter(result); break;
    }
}

void DualTransferPrompt::conflict_enter(Launch& result)
{
    using enum Stage;
    if (selected_ == 2) {  // Cancel row
        stage_ = Closed;
        return;
    }
    result.mode   = mode_;  // mode chosen in Mode stage
    result.policy = selected_ == 0 ? vault::CollisionPolicy::Combine : vault::CollisionPolicy::Suffix;
    result.fire   = true;
    stage_        = Closed;
}

} // namespace ui
